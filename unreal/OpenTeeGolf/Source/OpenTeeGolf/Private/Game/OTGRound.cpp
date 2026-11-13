// See OTGRound.h. The golf follows public/js/main.js (startHole, resetAim, autoClub, onShot, tick,
// finishShot, pathRelief, waterDrop, holeOut, nextHole, mulligan).

#include "Game/OTGRound.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "OTGCourseData.h"
#include "OTGLaunchMonitorSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "SOTGHud.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGRound, Log, All);

namespace
{
	AOTGRound* RoundIn(UWorld* W)
	{
		for (TActorIterator<AOTGRound> It(W); It; ++It) return *It;
		return nullptr;
	}
	using FCmd = FConsoleCommandWithWorldAndArgsDelegate;
	FAutoConsoleCommandWithWorldAndArgs TestShotCmd(TEXT("OTG.Round.TestShot"), TEXT("A test shot for the club and distance (the Space key)"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) { if (AOTGRound* R = RoundIn(W)) R->TestShot(); }));
	FAutoConsoleCommandWithWorldAndArgs ContinueCmd(TEXT("OTG.Round.Continue"), TEXT("Skip the flight / next hole (the Enter key)"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) { if (AOTGRound* R = RoundIn(W)) R->Continue(); }));
	FAutoConsoleCommandWithWorldAndArgs AimCmd(TEXT("OTG.Round.Aim"), TEXT("OTG.Round.Aim <degrees>: turn the aim (+ right)"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) { if (AOTGRound* R = RoundIn(W); R && A.Num()) R->AimBy(FCString::Atod(*A[0])); }));
	FAutoConsoleCommandWithWorldAndArgs ClubCmd(TEXT("OTG.Round.Club"), TEXT("OTG.Round.Club 1|-1: a longer / shorter club"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) { if (AOTGRound* R = RoundIn(W); R && A.Num()) R->ChangeClub(FCString::Atoi(*A[0])); }));
	FAutoConsoleCommandWithWorldAndArgs DropCmd(TEXT("OTG.Round.Drop"), TEXT("OTG.Round.Drop <yards>: (testing) the ball that far from the pin, on the line it's on"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) { if (AOTGRound* R = RoundIn(W); R && A.Num()) R->DropNearPin(FCString::Atod(*A[0])); }));
	FAutoConsoleCommandWithWorldAndArgs CardCmd(TEXT("OTG.Round.Scorecard"), TEXT("Show / hide the scorecard (the Tab key)"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) { if (AOTGRound* R = RoundIn(W)) R->ToggleScorecard(); }));

	constexpr double M_TO_YD = 1.09361, M_TO_FT = 3.28084;
	const TCHAR* DEG = TEXT("°");

	// main.js CLUBS (code, name, full carry in yards), and a typical strike with each for the test
	// shot (mph, launch, spin)
	struct FClub { const TCHAR* Code; const TCHAR* Name; double CarryYd, Mph, Vla, Spin; };
	const FClub Clubs[] = {
		{ TEXT("DR"), TEXT("Driver"), 230, 150, 11, 2600 }, { TEXT("W3"), TEXT("3 Wood"), 215, 142, 12, 3500 },
		{ TEXT("W5"), TEXT("5 Wood"), 200, 136, 13, 4200 }, { TEXT("H4"), TEXT("4 Hybrid"), 190, 131, 14, 4500 },
		{ TEXT("I4"), TEXT("4 Iron"), 180, 126, 14, 4800 }, { TEXT("I5"), TEXT("5 Iron"), 170, 121, 15, 5300 },
		{ TEXT("I6"), TEXT("6 Iron"), 160, 116, 16, 6000 }, { TEXT("I7"), TEXT("7 Iron"), 150, 110, 17, 6900 },
		{ TEXT("I8"), TEXT("8 Iron"), 140, 105, 19, 7800 }, { TEXT("I9"), TEXT("9 Iron"), 130, 100, 21, 8600 },
		{ TEXT("PW"), TEXT("Pitching Wedge"), 118, 95, 24, 9300 }, { TEXT("GW"), TEXT("Gap Wedge"), 105, 87, 27, 9800 },
		{ TEXT("SW"), TEXT("Sand Wedge"), 90, 76, 30, 10000 }, { TEXT("LW"), TEXT("Lob Wedge"), 70, 64, 34, 10500 },
		{ TEXT("PT"), TEXT("Putter"), 0, 0, 0, 0 },
	};
	constexpr int32 NumClubs = UE_ARRAY_COUNT(Clubs);
	int32 ClubIndex(const FString& Code)
	{
		for (int32 i = 0; i < NumClubs; i++) if (Code == Clubs[i].Code) return i;
		return 0;
	}
	const TCHAR* TeeTypes[] = { TEXT("Black"), TEXT("Blue"), TEXT("White"), TEXT("Green"), TEXT("Yellow"), TEXT("Red"), TEXT("Junior"), TEXT("Par3") };

	FString SurfaceDisplay(otg::Surface S)
	{
		switch (S)
		{
		case otg::Surface::Tee: return TEXT("Tee");
		case otg::Surface::Fairway: return TEXT("Fairway");
		case otg::Surface::FirstCut: return TEXT("First cut");
		case otg::Surface::Fringe: return TEXT("Fringe");
		case otg::Surface::Green: return TEXT("Green");
		case otg::Surface::Rough: return TEXT("Rough");
		case otg::Surface::DeepRough: return TEXT("Deep rough");
		case otg::Surface::Native: return TEXT("Heather");
		case otg::Surface::Path: return TEXT("Cart path");
		case otg::Surface::Sand: return TEXT("Bunker");
		case otg::Surface::Water: return TEXT("Water");
		case otg::Surface::Oob: return TEXT("Out of bounds");
		default: return TEXT("Off the course");
		}
	}
	FString ScoreName(int32 Strokes, int32 Par)
	{
		const int32 D = Strokes - Par;
		if (Strokes == 1) return TEXT("Hole in one!");
		switch (D)
		{
		case -3: return TEXT("Albatross!");
		case -2: return TEXT("Eagle!");
		case -1: return TEXT("Birdie!");
		case 0: return TEXT("Par");
		case 1: return TEXT("Bogey");
		case 2: return TEXT("Double bogey");
		case 3: return TEXT("Triple bogey");
		default: return FString::Printf(TEXT("%+d"), D);
		}
	}
	FString Side(double Deg) { return FMath::Abs(Deg) < 0.05 ? FString::Printf(TEXT("0.0%s"), DEG) : FString::Printf(TEXT("%s%.1f%s"), Deg > 0 ? TEXT("R") : TEXT("L"), FMath::Abs(Deg), DEG); }
	double Dist(const otg::V3& A, const otg::V3& B) { return FMath::Sqrt((A.x - B.x) * (A.x - B.x) + (A.z - B.z) * (A.z - B.z)); }
	double HeadingTo(const otg::V3& From, double X, double Z) { return FMath::Atan2(X - From.x, Z - From.z); }
	bool Playable(otg::Surface S) { return S != otg::Surface::None && S != otg::Surface::Water && S != otg::Surface::Oob; }
}

AOTGRound::AOTGRound()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto MakeMesh = [&](const TCHAR* Name, UStaticMesh* Mesh, bool bShadow) {
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(RootComponent);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(bShadow);
		C->SetMobility(EComponentMobility::Movable);
		return C;
	};
	BallMesh = MakeMesh(TEXT("Ball"), Sphere.Object, true);
	BallMesh->SetRelativeScale3D(FVector(0.0427)); // (the engine's sphere is a metre across; a ball 4.27 cm)
	AimMesh = MakeMesh(TEXT("Aim"), Cylinder.Object, false);
	PinMesh = MakeMesh(TEXT("Flagstick"), Cylinder.Object, true);
	FlagMesh = MakeMesh(TEXT("Flag"), Cube.Object, true);
	TeeMesh = MakeMesh(TEXT("TeeMarker"), Cube.Object, false);
	CupMesh = MakeMesh(TEXT("Cup"), Cylinder.Object, false);
	TracerMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Tracer"));
	TracerMesh->SetupAttachment(RootComponent);
	TracerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TracerMesh->SetCastShadow(false);
	TracerMesh->bUseAsyncCooking = true;
}

// ------------------------------------------------------------------ setting up

void AOTGRound::Setup(UOTGCourseData* InData, int32 FirstHole)
{
	Data = InData;
	Data->FillPlayCourse(Course);
	Data->FillTrees(Trees);
	Scores.Init(-1, Data->Holes.Num());
	SetActorLocation(FVector::ZeroVector);

	// (the level's own markers: every tee and every pin day; the round shows its own)
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		if (It->ActorHasTag(TEXT("OTG_TeeMarker")) || It->ActorHasTag(TEXT("OTG_PinMarker"))) It->SetActorHiddenInGame(true);

	// Materials: the engine's basic shape material, coloured
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	auto Mat = [&](UStaticMeshComponent* C, const FLinearColor& Color) {
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, this);
		M->SetVectorParameterValue(TEXT("Color"), Color);
		if (C) C->SetMaterial(0, M);
		return M;
	};
	BallMat = Mat(BallMesh, FLinearColor(0.85f, 0.85f, 0.85f));
	AimMat = Mat(AimMesh, FLinearColor(1.f, 0.78f, 0.15f));
	PinMat = Mat(PinMesh, FLinearColor(0.9f, 0.9f, 0.85f));
	FlagMat = Mat(FlagMesh, FLinearColor(0.85f, 0.05f, 0.04f));
	Mat(TeeMesh, FLinearColor(0.9f, 0.9f, 0.9f));
	Mat(CupMesh, FLinearColor(0.01f, 0.012f, 0.01f));
	TracerMat = Mat(nullptr, FLinearColor(1.f, 0.72f, 0.2f));
	TracerMesh->SetMaterial(0, TracerMat);

	// The camera
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	Cam = GetWorld()->SpawnActor<ACameraActor>();
	Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
	Cam->GetCameraComponent()->SetFieldOfView(60);
	if (PC) PC->SetViewTarget(Cam);

	// The HUD
	if (GEngine && GEngine->GameViewport)
	{
		Hud = SNew(SOTGHud);
		GEngine->GameViewport->AddViewportWidgetContent(Hud.ToSharedRef(), 10);
	}

	// Shots
	if (UOTGLaunchMonitorSubsystem* LM = GetGameInstance()->GetSubsystem<UOTGLaunchMonitorSubsystem>())
		ShotHandle = LM->OnShotNative.AddUObject(this, &AOTGRound::OnShot);

	// (-roundexec="<s>:<console command>|...": commands at those times after the round starts, for
	// checking it without anyone at the game)
	FString Exec;
	if (FParse::Value(FCommandLine::Get(), TEXT("roundexec="), Exec, false))
	{
		TArray<FString> Items;
		Exec.ParseIntoArray(Items, TEXT("|"));
		for (const FString& I : Items)
		{
			FString At, Command;
			if (I.Split(TEXT(":"), &At, &Command)) ExecAt.Emplace(FCString::Atod(*At), Command.TrimStartAndEnd());
		}
	}
	UE_LOG(LogOTGRound, Display, TEXT("Round on %s: %d holes, %d trees"), *Data->CourseName, Data->Holes.Num(), int32(Trees.all.size()));
	StartHole(FMath::Clamp(FirstHole, 0, FMath::Max(0, Data->Holes.Num() - 1)));
}

void AOTGRound::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UGameInstance* GI = GetGameInstance())
		if (UOTGLaunchMonitorSubsystem* LM = GI->GetSubsystem<UOTGLaunchMonitorSubsystem>())
		{
			LM->OnShotNative.Remove(ShotHandle);
			LM->SetTurn(false);
		}
	if (Hud && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Hud.ToSharedRef());
	Hud.Reset();
	Super::EndPlay(Reason);
}

// ------------------------------------------------------------------ the course

const FOTGHole& AOTGRound::Hole() const { return Data->Holes[HoleIndex]; }

otg::V3 AOTGRound::OnGround(double X, double Z) const
{
	const double Y = Course.HeightAt(X, Z);
	return otg::V3{ X, FMath::IsFinite(Y) ? Y : 0.0, Z };
}

otg::V3 AOTGRound::Pin() const
{
	const FOTGHole& H = Hole();
	const FVector* P = H.Pins.Find(PinName);
	if (!P && H.Pins.Num()) P = &H.Pins.CreateConstIterator()->Value;
	const FVector Q = P ? *P : H.GreenCenter;
	return OnGround(Q.X, Q.Z);
}

otg::V3 AOTGRound::TeeOf(const FOTGHole& H) const
{
	const FVector* T = H.Tees.Find(TeeName);
	if (!T)
	{
		// the closest tee in the standard order
		int32 I = 2;
		for (int32 k = 0; k < UE_ARRAY_COUNT(TeeTypes); k++) if (TeeName == TeeTypes[k]) I = k;
		for (int32 D = 1; D < UE_ARRAY_COUNT(TeeTypes) && !T; D++)
			for (int32 J : { I + D, I - D })
				if (!T && J >= 0 && J < UE_ARRAY_COUNT(TeeTypes)) T = H.Tees.Find(TeeTypes[J]);
	}
	if (!T && H.Tees.Num()) T = &H.Tees.CreateConstIterator()->Value;
	const FVector Q = T ? *T : H.GreenCenter;
	return OnGround(Q.X, Q.Z);
}

double AOTGRound::DistToPin() const { return Dist(Ball, Pin()); }
int32 AOTGRound::MaxStrokes() const { return Hole().Par + 5; }
otg::V3 AOTGRound::AimPoint() const { return OnGround(Ball.x + FMath::Sin(Heading) * AimDist, Ball.z + FMath::Cos(Heading) * AimDist); }

FString AOTGRound::Len(double M) const { return FString::Printf(TEXT("%d yd"), FMath::RoundToInt(M * M_TO_YD)); }
FString AOTGRound::Putt(double M) const { return FString::Printf(TEXT("%d ft"), FMath::RoundToInt(M * M_TO_FT)); }

// ------------------------------------------------------------------ the hole

void AOTGRound::StartHole(int32 Index)
{
	HoleIndex = Index;
	const FOTGHole& H = Hole();
	Strokes = 0;
	Lie = otg::Surface::Tee;
	Ball = TeeOf(H);
	PrevBall = Ball;
	Undo.Reset();
	PendingShot.Reset();
	bHasRes = false;
	Phase = EPhase::Ready;
	ClearTracer();
	ShowScorecard(false);
	if (Hud) Hud->SetShotData(FString(), FString());
	// the flag, and a marker on the tee
	const otg::V3 P = Pin();
	PinMesh->SetWorldLocation(World(P) + FVector(0, 0, 115));
	PinMesh->SetWorldScale3D(FVector(0.025, 0.025, 2.3));
	FlagMesh->SetWorldLocation(World(P) + FVector(0, 26, 205));
	FlagMesh->SetWorldScale3D(FVector(0.01, 0.5, 0.34));
	// (the cup: a dark disc 10.8 cm across, just above the green)
	CupMesh->SetWorldLocation(World(P) + FVector(0, 0, 0.3));
	CupMesh->SetWorldScale3D(FVector(0.108, 0.108, 0.002));
	TeeMesh->SetWorldLocation(World(Ball) + FVector(0, 0, 1));
	TeeMesh->SetWorldScale3D(FVector(0.05, 0.05, 0.02));
	ResetAim(false);
	PlaceBall(Ball);
	OverviewView();
	const double LenM = Dist(Ball, P);
	Banner(FString::Printf(TEXT("Hole %d"), H.Number), FString::Printf(TEXT("Par %d  ·  %s"), H.Par, *Len(LenM)), 3.f);
	UpdateHudInfo();
	TellLaunchMonitor(true);
	UE_LOG(LogOTGRound, Display, TEXT("Hole %d, par %d, %s"), H.Number, H.Par, *Len(LenM));
}

void AOTGRound::ResetAim(bool bToPin)
{
	const FOTGHole& H = Hole();
	const otg::V3 P = Pin();
	double TX = P.x, TZ = P.z;
	// From the tee on long holes aim down the fairway (the first aim point)
	if (!bToPin && Strokes == 0 && H.Par >= 4 && H.Aims.Num()) { TX = H.Aims[0].X; TZ = H.Aims[0].Z; }
	Heading = HeadingTo(Ball, TX, TZ);
	AimDist = FMath::Max(1.0, FMath::Sqrt((TX - Ball.x) * (TX - Ball.x) + (TZ - Ball.z) * (TZ - Ball.z)));
	bClubPicked = false;
	AutoClub();
	ShowAim();
}

void AOTGRound::AutoClub()
{
	FString C = TEXT("PT");
	if (!OnGreen())
	{
		const double D = DistToPin() * M_TO_YD;
		int32 Pick = 0;
		for (int32 i = 0; i < NumClubs; i++) if (Clubs[i].CarryYd > 0 && Clubs[i].CarryYd >= D * 0.95) Pick = i;
		C = Clubs[Pick].Code;
		if (Strokes == 0 && Hole().Par >= 4) C = TEXT("DR");
	}
	SetClub(C);
}

void AOTGRound::SetClub(const FString& Code)
{
	Club = Code;
	UpdateHudInfo();
	TellLaunchMonitor(Phase == EPhase::Ready);
}

void AOTGRound::TellLaunchMonitor(bool bTurn)
{
	if (!Data) return;
	if (UOTGLaunchMonitorSubsystem* LM = GetGameInstance()->GetSubsystem<UOTGLaunchMonitorSubsystem>())
	{
		LM->SetPlayer(Club, DistToPin() * M_TO_YD, false);
		LM->SetTurn(bTurn);
	}
}

// ------------------------------------------------------------------ the keys

void AOTGRound::AimBy(double Degrees)
{
	if (Phase != EPhase::Ready) return;
	Heading += FMath::DegreesToRadians(Degrees);
	ShowAim();
	AddressView(false);
}

void AOTGRound::ChangeClub(int32 Step)
{
	if (Phase != EPhase::Ready) return;
	const int32 I = FMath::Clamp(ClubIndex(Club) - Step, 0, NumClubs - 1);
	bClubPicked = true;
	SetClub(Clubs[I].Code);
	Toast(Clubs[I].Name, 1.2f);
}

void AOTGRound::TestShot()
{
	// A strike as a launch monitor would send it: the club's typical one, or for a wedge (or a putt)
	// one sized to the distance, a little off line
	const FClub& C = Clubs[ClubIndex(Club)];
	FOTGBallData B;
	const double D = DistToPin();
	if (Club == TEXT("PT"))
	{
		const double Mps = otg::of::PuttSpeed(D, Data->DefaultStimp);
		B.Speed = Mps * 2.23694 * FMath::FRandRange(0.97, 1.05);
		B.VLA = 1.5;
		B.HLA = FMath::FRandRange(-1.0, 1.0);
		B.TotalSpin = 0;
		B.SpinAxis = 0;
	}
	else
	{
		const double Yd = D * M_TO_YD;
		const double Scale = C.CarryYd > Yd && C.CarryYd > 0 && ClubIndex(Club) >= ClubIndex(TEXT("PW")) ? FMath::Sqrt(FMath::Max(0.15, Yd / C.CarryYd)) : 1.0;
		B.Speed = C.Mph * Scale * FMath::FRandRange(0.98, 1.02);
		B.VLA = C.Vla * FMath::FRandRange(0.95, 1.05);
		B.HLA = FMath::FRandRange(-2.5, 2.5);
		B.TotalSpin = C.Spin * FMath::FRandRange(0.9, 1.1);
		B.SpinAxis = FMath::FRandRange(-8.0, 8.0);
	}
	OnShot(OTGToShot(B, TEXT("test"), TEXT("test shot")));
}

void AOTGRound::Continue()
{
	if (Phase == EPhase::Flying) AnimT = 1e9;
	else if (Phase == EPhase::Holed) NextHole();
	else if (Phase == EPhase::Done) UGameplayStatics::OpenLevel(this, TEXT("/Engine/Maps/Entry"));
}

void AOTGRound::DropNearPin(double Yards)
{
	if (Phase != EPhase::Ready) return;
	const otg::V3 P = Pin();
	const double D = FMath::Max(0.1, DistToPin()), M = FMath::Max(0.3, Yards / M_TO_YD);
	Ball = OnGround(P.x + (Ball.x - P.x) / D * M, P.z + (Ball.z - P.z) / D * M);
	Lie = Course.SurfaceAt(Ball.x, Ball.z);
	if (!Playable(Lie)) Lie = otg::Surface::Rough;
	ClearTracer();
	PlaceBall(Ball);
	ResetAim(true);
	AddressView(true);
	UpdateHudInfo();
	Toast(FString::Printf(TEXT("(testing) dropped %s from the pin, %s"), *Len(M), *SurfaceDisplay(Lie).ToLower()));
}

void AOTGRound::ToggleScorecard()
{
	ShowScorecard(!bScorecard);
}

void AOTGRound::Mulligan()
{
	if (!Undo || Phase != EPhase::Ready) return;
	Ball = Undo->Ball; Strokes = Undo->Strokes; Lie = Undo->Lie; Heading = Undo->Heading;
	Undo.Reset();
	ClearTracer();
	PlaceBall(Ball);
	ShowAim();
	AddressView(true);
	UpdateHudInfo();
	Toast(TEXT("Mulligan: replay the shot"));
	TellLaunchMonitor(true);
}

// ------------------------------------------------------------------ shots

void AOTGRound::OnShot(const FOTGShot& Shot)
{
	// A swing while the last ball is still moving: finish that one at once, then play this one
	// from where it stops (rather than losing it)
	if (Phase == EPhase::Flying || Phase == EPhase::Resting)
	{
		PendingShot = Shot;
		if (Phase == EPhase::Flying) AnimT = 1e9;
		else PhaseTimer = 0;
		Toast(TEXT("New shot: skipping to where the last ball stopped"));
		return;
	}
	if (Phase != EPhase::Ready)
	{
		Toast(TEXT("Hole finished: Enter for the next one"));
		return;
	}
	LastShot = Shot;
	const otg::LieEffect Fx = otg::LieEffectFor(Lie);
	otg::Shot Play;
	Play.speedMph = Shot.SpeedMph * Fx.speed;
	Play.vla = Shot.VLA;
	Play.hla = Shot.HLA;
	Play.backSpin = Shot.BackSpin * Fx.spin;
	Play.sideSpin = Shot.SideSpin * Fx.spin;
	otg::ShotOptions O;
	O.heading = Heading;
	O.altitudeM = Data->AltitudeM;
	O.stimp = Data->DefaultStimp;
	O.hasPin = true;
	O.pin = Pin();
	O.trees = &Trees;
	O.random = []() { return double(FMath::FRand()); };
	const double T0 = FPlatformTime::Seconds();
	Res = otg::SimulateShot(Course, Ball, Play, O);
	bHasRes = true;
	UE_LOG(LogOTGRound, Display, TEXT("Shot from %s: %s -> %s, carry %.1f m, total %.1f m (%.0f ms)"), *SurfaceDisplay(Lie), *OTGShotLine(Shot),
		UTF8_TO_TCHAR(Res.result.c_str()), Res.carry, Res.total, (FPlatformTime::Seconds() - T0) * 1000);
	Undo = FUndo{ Ball, Strokes, Lie, Heading };
	PrevBall = Ball;
	Strokes++;
	Phase = EPhase::Flying;
	AnimT = 0;
	bCarryShown = false;
	ShotFrom = World(Ball);
	ClearTracer();
	AimMesh->SetVisibility(false);
	TellLaunchMonitor(false);
	ShowShotData(false);
	if (Lie != otg::Surface::Tee && Fx.speed < 1)
		Toast(FString::Printf(TEXT("From the %s: -%d%% ball speed, -%d%% spin"), *SurfaceDisplay(Lie).ToLower(), FMath::RoundToInt((1 - Fx.speed) * 100), FMath::RoundToInt((1 - Fx.spin) * 100)), 3.f);
	UpdateHudInfo();
}

void AOTGRound::FinishShot()
{
	const otg::Surface Surface = Course.SurfaceAt(Res.rest.x, Res.rest.z);
	FString Title, Small;
	if (Res.result == "holed")
	{
		Ball = Res.rest;
		PlaceBall(Ball);
		return HoleOut(false, FString());
	}
	if (Res.result == "oob" || Surface == otg::Surface::Oob || Surface == otg::Surface::None)
	{
		Strokes++; // stroke and distance
		Ball = PrevBall;
		Title = TEXT("Out of bounds");
		Small = TEXT("Stroke and distance: replay from the previous spot");
	}
	else if (Res.result == "water" || Surface == otg::Surface::Water)
	{
		Strokes++;
		WaterDrop(Ball);
		Title = TEXT("Penalty area");
		Small = TEXT("+1 stroke: drop where it went in");
	}
	else Ball = Res.rest;
	Lie = Course.SurfaceAt(Ball.x, Ball.z);
	if (!Playable(Lie)) Lie = otg::Surface::Rough;
	// Free relief from a cart path: the nearest spot off the path, no nearer the hole
	if (Lie == otg::Surface::Path && Title.IsEmpty())
	{
		otg::V3 Drop;
		if (PathRelief(Ball, Drop))
		{
			Ball = Drop;
			Lie = Course.SurfaceAt(Ball.x, Ball.z);
			ReliefNote = TEXT("Free relief from the cart path");
		}
	}
	if (!Title.IsEmpty()) Banner(Title, Small, 2.6f);
	else
	{
		const otg::Surface Landed = Res.hasCarryPoint ? Course.SurfaceAt(Res.carryPoint.x, Res.carryPoint.z) : otg::Surface::None;
		auto Has = [&](const char* K) { for (const std::string& H : Res.treeHits) if (H == K) return true; return false; };
		const FString Tree = Has("pole") ? TEXT("Off the flagstick! ") : Has("building") ? TEXT("Off the building! ") : Has("trunk") ? TEXT("Off the trunk! ")
			: Has("branch") ? TEXT("Hit a branch! ") : Has("leaves") ? TEXT("Clipped the leaves. ") : TEXT("");
		if (!ReliefNote.IsEmpty()) Toast(FString::Printf(TEXT("%s%s: dropped in the %s"), *Tree, *ReliefNote, *SurfaceDisplay(Lie).ToLower()), 4.f);
		else if (Landed != otg::Surface::None && Landed != Lie) Toast(FString::Printf(TEXT("%sLanded: %s, finished: %s"), *Tree, *SurfaceDisplay(Landed), *SurfaceDisplay(Lie)), 3.5f);
		else Toast(FString::Printf(TEXT("%sIn the %s"), *Tree, *SurfaceDisplay(Lie).ToLower()), Tree.IsEmpty() ? 2.5f : 3.5f);
		ReliefNote.Reset();
	}
	PlaceBall(Ball);
	ShowShotData(true);
	if (Strokes >= MaxStrokes()) return HoleOut(true, TEXT("Maximum strokes reached: picked up"));
	Phase = EPhase::Resting;
	PhaseTimer = PendingShot ? 0.0 : Title.IsEmpty() ? 1.6 : 2.4;
	UpdateHudInfo();
}

void AOTGRound::NextShot()
{
	Phase = EPhase::Ready;
	if (OnGreen()) ClearTracer();
	ResetAim(true);
	AddressView(false);
	UpdateHudInfo();
	TellLaunchMonitor(true);
	if (PendingShot)
	{
		const FOTGShot S = *PendingShot;
		PendingShot.Reset();
		OnShot(S);
	}
}

// The nearest point off the cart path (searching outward), not closer to the hole
bool AOTGRound::PathRelief(const otg::V3& B, otg::V3& Out) const
{
	const otg::V3 P = Pin();
	const double D0 = Dist(B, P);
	for (double R = 0.5; R <= 8; R += 0.25)
	{
		bool bFound = false;
		double BX = 0, BZ = 0, BD = 0;
		for (int32 K = 0; K < 24; K++)
		{
			const double A = K / 24.0 * UE_DOUBLE_TWO_PI, X = B.x + FMath::Cos(A) * R, Z = B.z + FMath::Sin(A) * R;
			const otg::Surface S = Course.SurfaceAt(X, Z);
			if (!Playable(S) || S == otg::Surface::Path) continue;
			const double DD = FMath::Sqrt((X - P.x) * (X - P.x) + (Z - P.z) * (Z - P.z));
			if (DD < D0 - 0.01) continue;
			if (!bFound || DD < BD) { bFound = true; BX = X; BZ = Z; BD = DD; }
		}
		if (bFound)
		{
			// a little further, clear of the edge
			const double EX = BX + (BX - B.x) / R * 0.3, EZ = BZ + (BZ - B.z) / R * 0.3;
			const bool bOk = Course.SurfaceAt(EX, EZ) != otg::Surface::Path;
			Out = OnGround(bOk ? EX : BX, bOk ? EZ : BZ);
			return true;
		}
	}
	return false;
}

// The drop zone of the water it went in (the export's), or where it last crossed into the water,
// moved back toward where it was hit until clear of it
bool AOTGRound::WaterDrop(otg::V3& Out) const
{
	const FOTGDropZone* Best = nullptr;
	double BestD = 1e18;
	for (const FOTGDropZone& Z : Data->DropZones)
	{
		const bool bIn = Z.bHasBox && Res.rest.x >= Z.Box.X - 5 && Res.rest.x <= Z.Box.Z + 5 && Res.rest.z >= Z.Box.Y - 5 && Res.rest.z <= Z.Box.W + 5;
		const double D = FMath::Square(Z.Position.X - Res.rest.x) + FMath::Square(Z.Position.Z - Res.rest.z);
		if ((bIn ? D - 1e12 : D) < BestD) { BestD = bIn ? D - 1e12 : D; Best = &Z; }
	}
	if (Best && (Best->bHasBox ? BestD < 0 : BestD < 200 * 200))
	{
		Out = OnGround(Best->Position.X, Best->Position.Z);
		return true;
	}
	const std::vector<otg::PathPoint>& Pts = Res.points;
	for (int32 i = int32(Pts.size()) - 1; i >= 0; i--)
	{
		if (!Playable(Course.SurfaceAt(Pts[i].x, Pts[i].z))) continue;
		const double OX = PrevBall.x - Pts[i].x, OZ = PrevBall.z - Pts[i].z, OL = FMath::Max(1e-6, FMath::Sqrt(OX * OX + OZ * OZ));
		for (double Back = 2; Back < 40; Back += 1)
		{
			const double X = Pts[i].x + OX / OL * Back, Z = Pts[i].z + OZ / OL * Back;
			if (Playable(Course.SurfaceAt(X, Z))) { Out = OnGround(X, Z); return true; }
		}
		Out = OnGround(Pts[i].x, Pts[i].z);
		return true;
	}
	Out = PrevBall;
	return false;
}

void AOTGRound::HoleOut(bool bPickedUp, const FString& Note)
{
	const FOTGHole& H = Hole();
	Phase = EPhase::Holed;
	PendingShot.Reset();
	const int32 Total = bPickedUp ? MaxStrokes() : Strokes;
	Scores[HoleIndex] = Total;
	const FString ToPar = Total == H.Par ? TEXT("E") : FString::Printf(TEXT("%+d"), Total - H.Par);
	Banner(bPickedUp ? TEXT("Picked up") : ScoreName(Total, H.Par), FString::Printf(TEXT("Hole %d: %d (%s)%s"), H.Number, Total, *ToPar, Note.IsEmpty() ? TEXT("") : *(TEXT("  ·  ") + Note)), 3.8f);
	UE_LOG(LogOTGRound, Display, TEXT("Hole %d: %d (%s)"), H.Number, Total, *ToPar);
	AimMesh->SetVisibility(false);
	TellLaunchMonitor(false);
	PhaseTimer = 2.6;
	bScorecardAuto = true;
	UpdateHudInfo();
}

void AOTGRound::NextHole()
{
	ShowScorecard(false);
	if (HoleIndex + 1 < Data->Holes.Num()) return StartHole(HoleIndex + 1);
	Phase = EPhase::Done;
	ShowScorecard(true);
	TellLaunchMonitor(false);
	UE_LOG(LogOTGRound, Display, TEXT("Round complete"));
}

// ------------------------------------------------------------------ every frame

void AOTGRound::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!Data) return;
	RoundTime += Dt;
	for (int32 i = 0; i < ExecAt.Num();)
	{
		if (RoundTime < ExecAt[i].Key) { i++; continue; }
		const FString Command = ExecAt[i].Value;
		ExecAt.RemoveAt(i);
		UE_LOG(LogOTGRound, Display, TEXT("(roundexec) %s"), *Command);
		if (GEngine) GEngine->Exec(GetWorld(), *Command);
	}
	if (Phase == EPhase::Flying)
	{
		AnimT += Dt;
		const std::vector<otg::PathPoint>& Pts = Res.points;
		int32 N = 0;
		while (N < int32(Pts.size()) && Pts[N].t <= AnimT) N++;
		otg::V3 P = Res.rest;
		if (N < int32(Pts.size()) && N > 0)
		{
			const otg::PathPoint& A = Pts[N - 1];
			const otg::PathPoint& B = Pts[N];
			const double F = (AnimT - A.t) / FMath::Max(1e-6, B.t - A.t);
			P = otg::V3{ A.x + (B.x - A.x) * F, A.y + (B.y - A.y) * F, A.z + (B.z - A.z) * F };
		}
		else if (N > 0) P = otg::V3{ Pts.back().x, Pts.back().y, Pts.back().z };
		PlaceBall(P);
		UpdateTracer(N, World(P));
		if (Res.hasCarryPoint && AnimT >= Res.carryPoint.t && !bCarryShown)
		{
			bCarryShown = true;
			ShowShotData(false);
		}
		// the camera: behind the ball as it sets off, then following it
		const FVector B = BallMesh->GetComponentLocation();
		const FVector Flat(B.X - ShotFrom.X, B.Y - ShotFrom.Y, 0);
		const bool bPutt = LastShot.VLA < 6 && Undo && Undo->Lie == otg::Surface::Green;
		if (Flat.Size() > 300)
		{
			const FVector U = Flat.GetSafeNormal();
			CamGoal = B - U * (bPutt ? 450 : 1400) + FVector(0, 0, FMath::Max(B.Z, ShotFrom.Z) - B.Z + (bPutt ? 160 : 450));
			CamSpeed = 3.f;
		}
		CamLook = B;
		CamLookNow = B;
		if (N >= int32(Pts.size())) FinishShot();
	}
	else if (Phase == EPhase::Resting)
	{
		PhaseTimer -= Dt;
		if (PhaseTimer <= 0) NextShot();
	}
	else if (Phase == EPhase::Holed)
	{
		PhaseTimer -= Dt;
		if (bScorecardAuto && PhaseTimer <= 0)
		{
			bScorecardAuto = false;
			ShowScorecard(true);
			PhaseTimer = 8.0; // (then the next hole, or Enter sooner)
		}
		else if (!bScorecardAuto && PhaseTimer <= 0) NextHole();
	}
	if (OverviewLeft > 0)
	{
		OverviewLeft -= Dt;
		if (OverviewLeft <= 0 && Phase == EPhase::Ready) AddressView(false);
	}
	UpdateCamera(Dt);
	// the ball a little bigger the further off it is, so it can be followed
	const float CamDist = FVector::Dist(CamPos, BallMesh->GetComponentLocation());
	const float K = FMath::Clamp(CamDist / 700.f, 1.f, 12.f);
	BallMesh->SetWorldScale3D(FVector(0.0427 * K));
	BallMesh->SetWorldLocation(BallWorld(BallShown)); // (sitting on the ground at its new size)
	// the launch monitor
	if (Hud)
		if (UOTGLaunchMonitorSubsystem* LM = GetGameInstance()->GetSubsystem<UOTGLaunchMonitorSubsystem>())
		{
			const FOTGLaunchMonitorStatus S = LM->GetStatus();
			FString T = S.Device.IsEmpty() ? FString(TEXT("Launch monitor")) : S.Device;
			T += TEXT(": ") + S.Text;
			if (S.Battery >= 0) T += FString::Printf(TEXT("  ·  %d%%"), S.Battery);
			Hud->SetStatus(T, S.bReady);
		}
}

// ------------------------------------------------------------------ showing it

FVector AOTGRound::World(const otg::V3& P) const
{
	return Data->CourseToWorld(FVector(P.x, P.y, P.z));
}

FVector AOTGRound::BallWorld(const otg::V3& P) const
{
	const float K = BallMesh->GetComponentScale().X / 0.0427f;
	return World(P) + FVector(0, 0, 2.135 * K);
}

void AOTGRound::PlaceBall(const otg::V3& P)
{
	BallShown = P;
	BallMesh->SetWorldLocation(BallWorld(P));
}

void AOTGRound::ShowAim()
{
	if (OnGreen())
	{
		AimMesh->SetVisibility(false);
		return;
	}
	const otg::V3 A = AimPoint();
	const double R = FMath::Max(0.8, AimDist * 0.012); // metres
	AimMesh->SetVisibility(true);
	AimMesh->SetWorldLocation(World(A) + FVector(0, 0, 3));
	AimMesh->SetWorldScale3D(FVector(R * 2, R * 2, 0.03));
	UpdateHudInfo();
}

void AOTGRound::UpdateTracer(int32 Upto, const FVector& Head)
{
	// The path so far, thinned to about a point a metre, as a ribbon facing the camera
	TracerPts.Reset();
	const std::vector<otg::PathPoint>& Pts = Res.points;
	for (int32 i = 0; i < Upto && i < int32(Pts.size()); i++)
	{
		const FVector W = World(otg::V3{ Pts[i].x, Pts[i].y, Pts[i].z }) + FVector(0, 0, 2);
		if (!TracerPts.Num() || FVector::DistSquared(W, TracerPts.Last()) > 100 * 100) TracerPts.Add(W);
	}
	TracerPts.Add(Head + FVector(0, 0, 2));
	if (TracerPts.Num() < 2) return;
	TArray<FVector> V, Nrm;
	TArray<int32> Tri;
	TArray<FVector2D> UV;
	TArray<FLinearColor> Col;
	TArray<FProcMeshTangent> Tan;
	const int32 Num = TracerPts.Num();
	for (int32 i = 0; i < Num; i++)
	{
		const FVector P = TracerPts[i];
		const FVector Along = (TracerPts[FMath::Min(i + 1, Num - 1)] - TracerPts[FMath::Max(i - 1, 0)]).GetSafeNormal();
		const FVector ToCam = (CamPos - P).GetSafeNormal();
		const FVector SideV = FVector::CrossProduct(Along, ToCam).GetSafeNormal();
		const float W = FMath::Clamp(FVector::Dist(CamPos, P) * 0.0022f, 1.5f, 90.f);
		V.Add(P - SideV * W * 0.5f);
		V.Add(P + SideV * W * 0.5f);
		Nrm.Add(ToCam); Nrm.Add(ToCam);
		UV.Add(FVector2D(i / float(Num), 0)); UV.Add(FVector2D(i / float(Num), 1));
		Col.Add(FLinearColor::White); Col.Add(FLinearColor::White);
		if (i > 0)
		{
			const int32 A = (i - 1) * 2, B = i * 2;
			Tri.Append({ A, B, A + 1, A + 1, B, B + 1 });
			Tri.Append({ A, A + 1, B, A + 1, B + 1, B }); // (both faces)
		}
	}
	TracerMesh->CreateMeshSection_LinearColor(0, V, Tri, Nrm, UV, Col, Tan, false);
	TracerMesh->SetMaterial(0, TracerMat);
}

void AOTGRound::ClearTracer()
{
	TracerMesh->ClearAllMeshSections();
	TracerPts.Reset();
}

void AOTGRound::AddressView(bool bSnap)
{
	// Behind the ball, looking along the aim (lower and closer for a putt)
	const FVector B = World(Ball);
	const FVector D(FMath::Sin(Heading), -FMath::Cos(Heading), 0);
	if (OnGreen())
	{
		CamGoal = B - D * 260 + FVector(0, 0, 120);
		CamLook = B + D * 300;
	}
	else
	{
		CamGoal = B - D * 350 + FVector(0, 0, 135);
		CamLook = B + D * 700;
	}
	CamSpeed = 2.5f;
	OverviewLeft = 0;
	if (bSnap) { CamPos = CamGoal; CamLookNow = CamLook; }
}

void AOTGRound::OverviewView()
{
	// The hole from high behind the tee, looking down at where it's aimed, then down to the ball
	const FVector B = World(Ball);
	const FVector D(FMath::Sin(Heading), -FMath::Cos(Heading), 0);
	const double Ahead = FMath::Min(AimDist, 200.0) * 100;
	CamPos = CamGoal = B - D * 2600 + FVector(0, 0, 1800);
	CamLookNow = CamLook = World(OnGround(Ball.x + FMath::Sin(Heading) * Ahead / 100, Ball.z + FMath::Cos(Heading) * Ahead / 100));
	OverviewLeft = 2.8;
}

void AOTGRound::UpdateCamera(float Dt)
{
	const float K = 1.f - FMath::Exp(-Dt * CamSpeed);
	CamPos += (CamGoal - CamPos) * K;
	if (Phase != EPhase::Flying) CamLookNow += (CamLook - CamLookNow) * K;
	// (never under the ground)
	const FVector C = Data->WorldToCourse(CamPos);
	const double G = Course.HeightAt(C.X, C.Z);
	if (FMath::IsFinite(G))
	{
		const double MinZ = Data->CourseToWorld(FVector(C.X, G, C.Z)).Z + 60;
		if (CamPos.Z < MinZ) CamPos.Z = MinZ;
	}
	if (Cam) Cam->SetActorLocationAndRotation(CamPos, (CamLookNow - CamPos).Rotation());
}

void AOTGRound::UpdateHudInfo()
{
	if (!Hud || !Data) return;
	const FOTGHole& H = Hole();
	const double D = DistToPin();
	const FString L1 = FString::Printf(TEXT("Hole %d  ·  Par %d  ·  %s"), H.Number, H.Par, *Len(Dist(TeeOf(H), Pin())));
	FString L2;
	if (Phase == EPhase::Holed || Phase == EPhase::Done) L2 = FString::Printf(TEXT("%d stroke%s"), Scores[HoleIndex], Scores[HoleIndex] == 1 ? TEXT("") : TEXT("s"));
	else if (Phase == EPhase::Flying) L2 = FString::Printf(TEXT("Stroke %d  ·  on its way"), Strokes);
	else L2 = FString::Printf(TEXT("Stroke %d  ·  %s to the pin  ·  %s"), Strokes + 1, OnGreen() ? *Putt(D) : *Len(D), *SurfaceDisplay(Lie));
	const FClub& C = Clubs[ClubIndex(Club)];
	const FString L3 = OnGreen() || Club == TEXT("PT") ? FString(C.Name) : FString::Printf(TEXT("%s  ·  aim %s"), C.Name, *Len(AimDist));
	Hud->SetInfo(L1, L2, L3);
}

void AOTGRound::ShowShotData(bool bWithResult)
{
	if (!Hud) return;
	const FOTGShot& S = LastShot;
	const FString L1 = FString::Printf(TEXT("%.1f mph  ·  %.1f%s up  ·  %s  ·  %s rpm  ·  axis %s"), S.SpeedMph, S.VLA, DEG, *Side(S.HLA),
		*FText::AsNumber(FMath::RoundToInt(S.TotalSpin)).ToString(), *Side(S.SpinAxis));
	FString L2;
	if (bHasRes && (bCarryShown || bWithResult))
	{
		const bool bPutt = Undo && Undo->Lie == otg::Surface::Green;
		L2 = bPutt ? FString::Printf(TEXT("Rolled %s"), *Putt(Res.total)) : FString::Printf(TEXT("Carry %s"), *Len(Res.carry));
		if (bWithResult && !bPutt)
		{
			L2 += FString::Printf(TEXT("  ·  Total %s"), *Len(Res.total));
			if (FMath::Abs(Res.offline) >= 0.5) L2 += FString::Printf(TEXT("  ·  %d yd %s"), FMath::RoundToInt(FMath::Abs(Res.offline) * M_TO_YD), Res.offline > 0 ? TEXT("right") : TEXT("left"));
			L2 += FString::Printf(TEXT("  ·  apex %d ft"), FMath::RoundToInt(Res.apex * M_TO_FT));
		}
	}
	Hud->SetShotData(L1, L2);
}

void AOTGRound::Banner(const FString& Title, const FString& Small, float Seconds)
{
	if (Hud) Hud->Banner(Title, Small, Seconds);
}

void AOTGRound::Toast(const FString& Text, float Seconds)
{
	if (Hud) Hud->Toast(Text, Seconds);
	UE_LOG(LogOTGRound, Display, TEXT("%s"), *Text);
}

void AOTGRound::ShowScorecard(bool bShow)
{
	bScorecard = bShow;
	if (!Hud) return;
	if (!bShow) return Hud->HideScorecard();
	TArray<FOTGScoreRow> Rows;
	for (int32 i = 0; i < Data->Holes.Num(); i++) Rows.Add(FOTGScoreRow{ Data->Holes[i].Number, Data->Holes[i].Par, Scores[i] });
	Hud->ShowScorecard(Data->CourseName, Rows, Phase == EPhase::Done);
}
