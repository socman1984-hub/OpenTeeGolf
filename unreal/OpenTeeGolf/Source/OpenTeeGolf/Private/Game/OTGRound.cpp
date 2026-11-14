// See OTGRound.h. The golf follows public/js/main.js (startHole, resetAim, autoClub, onShot, tick,
// finishShot, pathRelief, waterDrop, holeOut, nextHole, mulligan).

#include "Game/OTGRound.h"

#include "Camera/CameraActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Texture2D.h"
#include "SOTGMinimap.h"
#include "SOTGPanels.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
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

// The course's look: a clear summer day, rich warm greens and a deep blue sky (tunable live)
static TAutoConsoleVariable<float> CVarLookExposure(TEXT("OTG.Look.Exposure"), -0.45f, TEXT("Exposure bias (stops; - darker)"));
static TAutoConsoleVariable<float> CVarLookSaturation(TEXT("OTG.Look.Saturation"), 1.1f, TEXT("Colour saturation (1: as is)"));
static TAutoConsoleVariable<float> CVarLookContrast(TEXT("OTG.Look.Contrast"), 1.1f, TEXT("Contrast (1: as is)"));
static TAutoConsoleVariable<float> CVarLookTemp(TEXT("OTG.Look.Temp"), 6900.f, TEXT("White balance (K; 6500 neutral, higher warmer)"));
static TAutoConsoleVariable<float> CVarLookSun(TEXT("OTG.Look.Sun"), 1.35f, TEXT("Sunlight strength (x the level's)"));
static TAutoConsoleVariable<float> CVarLookSunWarm(TEXT("OTG.Look.SunWarm"), 0.35f, TEXT("Sunlight warmth (0 white .. 1 golden)"));
static TAutoConsoleVariable<float> CVarLookSunPitch(TEXT("OTG.Look.SunPitch"), -42.f, TEXT("Sun height (degrees below the horizon's pitch; 999: the level's)"));
static TAutoConsoleVariable<float> CVarLookSunSide(TEXT("OTG.Look.SunSide"), 40.f, TEXT("The sun from behind the tee, this many degrees to the left (999: the level's own direction)"));
static TAutoConsoleVariable<float> CVarLookHaze(TEXT("OTG.Look.Haze"), 0.35f, TEXT("Haze in the air (x the level's; lower: a deeper blue sky)"));
static TAutoConsoleVariable<float> CVarLookFog(TEXT("OTG.Look.Fog"), 0.5f, TEXT("Height fog (x the level's)"));
static TAutoConsoleVariable<float> CVarLookSunSoft(TEXT("OTG.Look.SunSoft"), 1.2f, TEXT("The sun's size in the sky (degrees: softer shadow edges; the real sun is 0.53)"));
static TAutoConsoleVariable<float> CVarLookSlopeBias(TEXT("OTG.Look.SlopeBias"), 2.0f, TEXT("Shadow slope bias (smooths the hills' own shadow edges in a low sun)"));
static TAutoConsoleVariable<float> CVarLookTreeLift(TEXT("OTG.Look.TreeLift"), 0.25f, TEXT("Sky light let into shade (Lumen skylight leaking: lighter tree crowns; 0 none)"));
static TAutoConsoleVariable<float> CVarLookShadows(TEXT("OTG.Look.Shadows"), 1.18f, TEXT("The darkest tones lifted (gain; 1: as is)"));
static TAutoConsoleVariable<float> CVarLookSky(TEXT("OTG.Look.Sky"), 1.3f, TEXT("Sky light (x the level's)"));
static TAutoConsoleVariable<float> CVarLookVignette(TEXT("OTG.Look.Vignette"), 0.3f, TEXT("Vignette"));

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
	FAutoConsoleCommandWithWorldAndArgs PauseCmd(TEXT("OTG.Round.Pause"), TEXT("The pause menu (the Esc key)"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) { if (AOTGRound* R = RoundIn(W)) R->TogglePause(); }));
	FAutoConsoleCommandWithWorldAndArgs CastCmd(TEXT("OTG.Round.CastTest"), TEXT("(testing) what a ball meets flying at the flagstick and at the nearest trunk"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) { if (AOTGRound* R = RoundIn(W)) R->CastTest(); }));
	FAutoConsoleCommandWithWorldAndArgs PanelCmd(TEXT("OTG.Round.ShotPanel"), TEXT("The test shot panel (the T key)"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) { if (AOTGRound* R = RoundIn(W)) R->ToggleShotPanel(); }));
	FAutoConsoleCommandWithWorldAndArgs FreeCmd(TEXT("OTG.Round.FreeView"), TEXT("The free view (the F key)"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) { if (AOTGRound* R = RoundIn(W)) R->ToggleFreeView(); }));
	FAutoConsoleCommandWithWorldAndArgs FlyCmd(TEXT("OTG.Round.Fly"), TEXT("OTG.Round.Fly <forward m> <right m> <up m> [yaw] [pitch]: (testing) move the free view"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		AOTGRound* R = RoundIn(W);
		if (!R || A.Num() < 3) return;
		auto Arg = [&](int32 i) { return A.IsValidIndex(i) ? FCString::Atod(*A[i]) : 0.0; };
		R->FreeFly(1.f / 60, FVector(0, 0, 0), FVector2D(Arg(3), Arg(4)), false);
		for (int32 k = 0; k < 60; k++) R->FreeFly(1.f / 60, FVector(Arg(0), Arg(1), Arg(2)).GetClampedToMaxSize(1.0), FVector2D::ZeroVector, false);
	}));
	FAutoConsoleCommandWithWorldAndArgs LookAtCmd(TEXT("OTG.Round.LookAt"), TEXT("OTG.Round.LookAt <X> <Y> <Z> [distance cm] [height cm] [yaw]: (testing) the free view looking at a spot in the level, or 'pin' for the flag"),
		FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
			AOTGRound* R = RoundIn(W);
			if (!R || !A.Num()) return;
			auto Arg = [&](int32 i, double D) { return A.IsValidIndex(i) ? FCString::Atod(*A[i]) : D; };
			const bool bPin = A[0] == TEXT("pin");
			const int32 O = bPin ? 1 : 3;
			R->FreeLookAt(bPin ? R->PinWorld() + FVector(0, 0, 200) : FVector(Arg(0, 0), Arg(1, 0), Arg(2, 0)), Arg(O, 800), Arg(O + 1, 300), Arg(O + 2, 30));
		}));
	FAutoConsoleCommandWithWorldAndArgs PerfCmd(TEXT("OTG.Round.Perf"), TEXT("Frame rate and timings (the P key)"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) { if (AOTGRound* R = RoundIn(W)) R->TogglePerf(); }));
	FAutoConsoleCommandWithWorldAndArgs WxCmd(TEXT("OTG.Round.WeatherPanel"), TEXT("The weather panel (the L key)"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) { if (AOTGRound* R = RoundIn(W)) R->ToggleWeatherPanel(); }));
	FAutoConsoleCommandWithWorldAndArgs TimeCmd(TEXT("OTG.Round.Time"), TEXT("OTG.Round.Time <hour, 5.5-19.5> | auto: (testing) the time of day"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		if (AOTGRound* R = RoundIn(W)) R->SetSunHour(A.Num() && A[0] != TEXT("auto") ? FCString::Atod(*A[0]) : -1.0);
	}));
	FAutoConsoleCommandWithWorldAndArgs WxSetCmd(TEXT("OTG.Round.Weather"), TEXT("OTG.Round.Weather <key> | wind <mph> | from <deg> | cloud <0-1> | rain <mm> | fog <0-1>"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		AOTGRound* R = RoundIn(W);
		if (!R || !A.Num()) return;
		const double V = A.Num() > 1 ? FCString::Atod(*A[1]) : 0;
		if (A[0] == TEXT("wind")) R->SetWindMph(V);
		else if (A[0] == TEXT("from")) R->SetWindFrom(V);
		else if (A[0] == TEXT("cloud")) R->SetCloud(V);
		else if (A[0] == TEXT("rain")) R->SetRain(V);
		else if (A[0] == TEXT("fog")) R->SetFog(V);
		else R->SetWeatherKey(A[0]);
	}));
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
	FlagCloth = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FlagCloth"));
	FlagCloth->SetupAttachment(RootComponent);
	FlagCloth->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Rain"));
	RainMesh->SetupAttachment(RootComponent);
	RainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RainMesh->SetCastShadow(false);
}

// ------------------------------------------------------------------ setting up

void AOTGRound::Setup(UOTGCourseData* InData, const FOTGRoundSetup& InSetup)
{
	Data = InData;
	Setup_ = InSetup;
	Data->FillPlayCourse(Course);
	Data->FillTrees(Trees);
	std::vector<otg::Solid> Solids;
	for (const FOTGSolid& S : Data->Solids)
	{
		otg::Solid O;
		O.h = S.Height;
		O.kind = TCHAR_TO_UTF8(*S.Kind);
		for (const FVector2D& Q : S.Footprint) O.pts.emplace_back(Q.X, Q.Y);
		Solids.push_back(O);
	}
	Collider.Build(Course, Trees.all, Solids);
	SetActorLocation(FVector::ZeroVector);
	TeeName = Setup_.Tee.IsEmpty() ? FString(TEXT("White")) : Setup_.Tee;
	PinName = Setup_.Pin.IsEmpty() ? FString(TEXT("Saturday")) : Setup_.Pin;

	// The players and the game (nobody picked: a round on your own)
	TArray<FOTGGamePlayer> Players;
	const TArray<FOTGPlayerProfile> Profiles = FOTGPlayers::Load();
	for (const FString& Id : Setup_.Picked)
		for (const FOTGPlayerProfile& P : Profiles)
			if (P.Id == Id)
			{
				FOTGGamePlayer G;
				G.Id = P.Id; G.Name = P.Name; G.Colour = P.Colour; G.Handicap = P.Handicap; G.bLeftHanded = P.bLeftHanded; G.Tee = P.Tee;
				G.Team = Setup_.Teams.Contains(Id) ? Setup_.Teams[Id] : TEXT("A");
				Players.Add(G);
			}
	if (!Players.Num() || !FOTGGame::Problem(Setup_.Format, Setup_.Picked, Setup_.Teams).IsEmpty())
	{
		if (Players.Num()) UE_LOG(LogOTGRound, Warning, TEXT("%s: playing on your own"), *FOTGGame::Problem(Setup_.Format, Setup_.Picked, Setup_.Teams));
		Players.Reset();
		FOTGGamePlayer You;
		You.Name = TEXT("You");
		You.Colour = FLinearColor(0.85f, 0.85f, 0.85f);
		Players.Add(You);
		Setup_.Format = TEXT("stroke");
	}
	TArray<FOTGHoleInfo> HoleInfo;
	for (const FOTGHole& H : Data->Holes) HoleInfo.Add(FOTGHoleInfo{ H.Par, H.Index });
	Game = MakeUnique<FOTGGame>(Players, Setup_.Format, Setup_.bHandicaps, HoleInfo);
	const int32 First = FMath::Clamp(Setup_.StartHole - 1, 0, FMath::Max(0, Data->Holes.Num() - 1));
	LastHole = Setup_.Holes == 9 ? FMath::Min(First + 9, Data->Holes.Num()) : Data->Holes.Num();

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
	// (the other sides' balls, in their colours)
	UStaticMesh* SphereMesh = BallMesh->GetStaticMesh();
	if (!Game->Single())
		for (int32 i = 0; i < Game->Sides.Num(); i++)
		{
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
			C->SetStaticMesh(SphereMesh);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetMobility(EComponentMobility::Movable);
			C->SetupAttachment(RootComponent);
			C->RegisterComponent();
			Mat(C, Game->Sides[i].Colour);
			C->SetVisibility(false);
			OtherBalls.Add(C);
		}
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
		// the minimap: the course photo, over the landscape's area
		const FString PhotoPath = FString::Printf(TEXT("/Game/Courses/%s/Build/Textures/T_CoursePhoto.T_CoursePhoto"), *Data->CourseId);
		UTexture2D* Photo = LoadObject<UTexture2D>(nullptr, *PhotoPath);
		if (!Photo) UE_LOG(LogOTGRound, Warning, TEXT("No course photo for the minimap (%s)"), *PhotoPath);
		TWeakObjectPtr<AOTGRound> Self(this);
		Hud->Minimap()->SetCourse(Photo, Data->HeightX0, Data->HeightZ1, (Data->HeightSamples - 1) * Data->MetresPerSample, [Self](double X, double Z) {
			return Self.IsValid() ? SurfaceDisplay(Self->Course.SurfaceAt(X, Z)) : FString();
		});
		Hud->Minimap()->OnAim = [Self](double X, double Z) { if (Self.IsValid()) Self->AimAt(X, Z); };
		// the test shot panel (T): hit as if the launch monitor had sent it
		Hud->ShotPanel()->PuttMph = [Self]() {
			return Self.IsValid() ? otg::of::PuttSpeed(Self->DistToPin() + 0.4, Self->Data->DefaultStimp) / 0.44704 : 5.0;
		};
		Hud->WeatherPanel()->OnKey = [Self](const FString& K) { if (Self.IsValid()) Self->SetWeatherKey(K); };
		Hud->WeatherPanel()->OnWind = [Self](double V) { if (Self.IsValid()) Self->SetWindMph(V); };
		Hud->WeatherPanel()->OnFrom = [Self](double V) { if (Self.IsValid()) Self->SetWindFrom(V); };
		Hud->WeatherPanel()->OnCloud = [Self](double V) { if (Self.IsValid()) Self->SetCloud(V); };
		Hud->WeatherPanel()->OnRain = [Self](double V) { if (Self.IsValid()) Self->SetRain(V); };
		Hud->WeatherPanel()->OnFog = [Self](double V) { if (Self.IsValid()) Self->SetFog(V); };
		Hud->WeatherPanel()->OnTemp = [Self](double V) { if (Self.IsValid()) Self->SetTemp(V); };
		Hud->WeatherPanel()->OnHour = [Self](double V) { if (Self.IsValid()) Self->SetSunHour(V); };
		Hud->WeatherPanel()->Current = [Self](FOTGWeatherView& Out) {
			if (!Self.IsValid()) return;
			const FOTGWeather& W = Self->GetWeather();
			Out.Key = W.bLive ? FString(TEXT("live")) : W.Key; Out.Label = W.Label; Out.WindMph = W.WindMph; Out.From = Self->WindFromRelative();
			Out.Cloud = W.Cloud; Out.RainMm = W.RainMm; Out.Fog = W.Fog; Out.TempC = W.TempC; Out.Hour = Self->GetSunHour();
		};
		Hud->ShotPanel()->OnHit = [Self](double Mph, double Vla, double Hla, double Spin, double Axis) {
			if (!Self.IsValid()) return;
			if (UOTGLaunchMonitorSubsystem* LM = Self->GetGameInstance()->GetSubsystem<UOTGLaunchMonitorSubsystem>()) LM->InjectShot(Mph, Vla, Hla, Spin, Axis);
		};
	}
	UMaterialInterface* Rain = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenTeeGolf/Game/M_Rain.M_Rain"));
	if (Rain)
	{
		RainMat = UMaterialInstanceDynamic::Create(Rain, this);
		RainMesh->SetMaterial(0, RainMat);
	}
	else UE_LOG(LogOTGRound, Warning, TEXT("No rain material (make_pack.ps1 -Assets makes it)"));

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
	ApplyLook();
	SetupWeather();
	UE_LOG(LogOTGRound, Display, TEXT("Round on %s: %d holes, %d trunks and buildings to hit; %s, %d side(s), %s tees, %s pins"), *Data->CourseName, Data->Holes.Num(), int32(Collider.Count()),
		*Game->F.Name, Game->Sides.Num(), *TeeName, *PinName);
	StartHole(First);
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

otg::V3 AOTGRound::TeeOf(const FOTGHole& H, const FString& TeeWanted) const
{
	const FString TeeName_ = TeeWanted.IsEmpty() ? TeeName : TeeWanted;
	const FVector* T = H.Tees.Find(TeeName_);
	if (!T)
	{
		// the closest tee in the standard order
		int32 I = 2;
		for (int32 k = 0; k < UE_ARRAY_COUNT(TeeTypes); k++) if (TeeName_ == TeeTypes[k]) I = k;
		for (int32 D = 1; D < UE_ARRAY_COUNT(TeeTypes) && !T; D++)
			for (int32 J : { I + D, I - D })
				if (!T && J >= 0 && J < UE_ARRAY_COUNT(TeeTypes)) T = H.Tees.Find(TeeTypes[J]);
	}
	if (!T && H.Tees.Num()) T = &H.Tees.CreateConstIterator()->Value;
	const FVector Q = T ? *T : H.GreenCenter;
	return OnGround(Q.X, Q.Z);
}

double AOTGRound::DistToPin() const { return Dist(Ball, Pin()); }
double AOTGRound::PinDist(const otg::V3& B) const { return Dist(B, Pin()); }
int32 AOTGRound::MaxStrokes() const { return Hole().Par + 5; }
otg::V3 AOTGRound::AimPoint() const { return OnGround(Ball.x + FMath::Sin(Heading) * AimDist, Ball.z + FMath::Cos(Heading) * AimDist); }

FString AOTGRound::Len(double M) const { return FString::Printf(TEXT("%d yd"), FMath::RoundToInt(M * M_TO_YD)); }
FString AOTGRound::Putt(double M) const { return FString::Printf(TEXT("%d ft"), FMath::RoundToInt(M * M_TO_FT)); }

// ------------------------------------------------------------------ the hole

void AOTGRound::StartHole(int32 Index)
{
	HoleIndex = Index;
	const FOTGHole& H = Hole();
	// every side on its tee (a player's own tees, or the round's), the honour first
	Game->StartHole(Index, [&](const FOTGSide& S) {
		const FString Own = S.Members.Num() == 1 ? Game->Players[S.Members[0]].Tee : FString();
		return TeeOf(H, Own);
	});
	LoadSide(Game->Next([this](const otg::V3& B) { return PinDist(B); }));
	PendingShot.Reset();
	bHasRes = false;
	Phase = EPhase::Ready;
	ClearTracer();
	ShowScorecard(false);
	if (Hud) Hud->SetShotData(FString(), FString());
	// the flag, and a marker on the tee
	const otg::V3 P = Pin();
	PinMesh->SetWorldLocation(World(P) + FVector(0, 0, 115));
	PinMesh->SetWorldScale3D(FVector(0.02, 0.02, 2.3));
	FlagMesh->SetVisibility(false);   // (the cloth flag instead: UpdateFlag)
	// (the cup: a dark disc 10.8 cm across, just above the green)
	CupMesh->SetWorldLocation(World(P) + FVector(0, 0, 0.3));
	CupMesh->SetWorldScale3D(FVector(0.108, 0.108, 0.002));
	TeeMesh->SetWorldLocation(World(TeeOf(H, TeeName)) + FVector(0, 0, 1));
	TeeMesh->SetWorldScale3D(FVector(0.05, 0.05, 0.02));
	ResetAim(false);
	PlaceBall(Ball);
	OverviewView();
	const double LenM = Dist(Ball, P);
	Banner(FString::Printf(TEXT("Hole %d"), H.Number), FString::Printf(TEXT("Par %d  ·  %s%s"), H.Par, *Len(LenM),
		Game->Single() ? TEXT("") : *FString::Printf(TEXT("  ·  %s to play"), *Game->SideLabel(SideIdx))), 3.f);
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
		const bool bLeft = Game && Game->Sides.IsValidIndex(SideIdx) ? Game->Players[Game->Hitter(Game->Sides[SideIdx])].bLeftHanded : false;
		LM->SetPlayer(Club, DistToPin() * M_TO_YD, bLeft);
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
	// one sized to the distance, a little off line (only on the player's turn: it's made for their
	// club and distance)
	if (Phase != EPhase::Ready || bPaused)
	{
		Toast(TEXT("Test shot: wait for your turn"), 1.5f);
		return;
	}
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
	else if (Phase == EPhase::Done) QuitToMenu();
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

void AOTGRound::CastTest()
{
	auto Report = [&](const TCHAR* What, const otg::V3& From, const otg::V3& To) {
		otg::Hit H;
		const bool bHit = Collider.Cast(From, To, false, H);
		UE_LOG(LogOTGRound, Display, TEXT("CastTest %s: %s at (%.2f, %.2f, %.2f), normal (%.2f, %.2f, %.2f)"), What, bHit ? UTF8_TO_TCHAR(H.kind.c_str()) : TEXT("nothing"),
			H.point.x, H.point.y, H.point.z, H.normal.x, H.normal.y, H.normal.z);
	};
	const otg::V3 P = Pin();
	Collider.SetPin(&P);
	// at the flagstick, a metre up, from 5 m short of it
	const double D = FMath::Max(0.1, DistToPin());
	const double UX = (P.x - Ball.x) / D, UZ = (P.z - Ball.z) / D;
	Report(TEXT("flagstick"), otg::V3{ P.x - UX * 5, P.y + 1, P.z - UZ * 5 }, otg::V3{ P.x + UX, P.y + 1, P.z + UZ });
	// at the nearest trunk, a metre up
	const otg::Tree* Near = nullptr;
	double Best = 1e18;
	for (const otg::Tree& T : Trees.all) { const double DD = (T.x - Ball.x) * (T.x - Ball.x) + (T.z - Ball.z) * (T.z - Ball.z); if (DD < Best) { Best = DD; Near = &T; } }
	if (Near)
	{
		const double G = Course.HeightAt(Near->x, Near->z);
		Report(TEXT("trunk"), otg::V3{ Near->x - 5, G + 1, Near->z }, otg::V3{ Near->x + 5, G + 1, Near->z });
	}
	// straight down onto the ground at the ball
	Report(TEXT("ground"), otg::V3{ Ball.x, Ball.y + 10, Ball.z }, otg::V3{ Ball.x, Ball.y - 10, Ball.z });
	UE_LOG(LogOTGRound, Display, TEXT("CastTest: ground at the ball %.3f"), Course.HeightAt(Ball.x, Ball.z));
}

// Aim at a spot (course metres): the minimap, or a click on the ground
void AOTGRound::AimAt(double X, double Z)
{
	if (Phase != EPhase::Ready || bPaused) return;
	const double D = FMath::Sqrt((X - Ball.x) * (X - Ball.x) + (Z - Ball.z) * (Z - Ball.z));
	if (D < 1) return;
	Heading = HeadingTo(Ball, X, Z);
	AimDist = D;
	ShowAim();
	AddressView(false);
}

void AOTGRound::AimAtWorld(const FVector& W)
{
	const FVector C = Data->WorldToCourse(W);
	AimAt(C.X, C.Z);
}

void AOTGRound::TogglePerf()
{
	if (Hud) Hud->ShowPerf(!Hud->IsPerfShown());
}

FVector AOTGRound::PinWorld() const
{
	return World(Pin());
}

void AOTGRound::FreeLookAt(const FVector& Target, double Dist, double Height, double YawDeg)
{
	if (!bFreeView) ToggleFreeView();
	const double Y = FMath::DegreesToRadians(YawDeg);
	FreePos = Target - FVector(FMath::Cos(Y), FMath::Sin(Y), 0) * Dist + FVector(0, 0, Height);
	FreeRot = (Target - FreePos).Rotation();
	if (Cam) Cam->SetActorLocationAndRotation(FreePos, FreeRot);
}

void AOTGRound::ToggleShotPanel()
{
	if (Hud) Hud->ShowShotPanel(!Hud->IsShotPanelShown());
}

void AOTGRound::UpdateMap()
{
	if (!Hud || !Hud->Minimap()) return;
	FOTGMapState S;
	S.bValid = true;
	const otg::V3 P = Pin(), T = TeeOf(Hole(), TeeName), A = AimPoint();
	S.Ball = FVector2D(BallShown.x, BallShown.z);
	S.Pin = FVector2D(P.x, P.z);
	S.Tee = FVector2D(T.x, T.z);
	S.Aim = FVector2D(A.x, A.z);
	S.bOnGreen = OnGreen();
	if (!Game->Single())
	{
		S.Ring = Game->Sides[SideIdx].Colour;
		for (int32 i = 0; i < Game->Sides.Num(); i++)
			if (i != SideIdx && !Game->Sides[i].bHoled) S.Others.Emplace(FVector2D(Game->Sides[i].Ball.x, Game->Sides[i].Ball.z), Game->Sides[i].Colour);
	}
	Hud->Minimap()->SetState(S);
}

// The wind box: an arrow against the aim (up: helping), its speed and gusts, how it plays, the weather
void AOTGRound::UpdateWindBox()
{
	if (!Hud || !Hud->Wind()) return;
	double Mps, FromDeg;
	HoleWind(Mps, FromDeg);
	const double TempF = Weather.TempC * 1.8 + 32;
	const FString Line2 = FString::Printf(TEXT("%s%s  ·  %d\u00B0F%s"), Weather.bLive ? TEXT("Live: ") : TEXT(""), *Weather.Label, FMath::RoundToInt(TempF), Weather.Wetness() > 0 ? TEXT("  ·  wet ground") : TEXT(""));
	if (Mps <= 0)
	{
		Hud->Wind()->Set(0, true, TEXT("Calm"), Line2);
		return;
	}
	const double BlowRel = FromDeg + 180 - FMath::RadiansToDegrees(Heading);
	const double Along = FMath::Cos(FMath::DegreesToRadians(BlowRel));
	const FString Feel = FMath::Abs(Along) < 0.4 ? TEXT("cross") : Along > 0 ? TEXT("helping") : TEXT("into you");
	const FString Gust = Weather.GustMph > Weather.WindMph + 3 ? FString::Printf(TEXT(", gusts %d"), FMath::RoundToInt(Weather.GustMph)) : FString();
	Hud->Wind()->Set(BlowRel, false, FString::Printf(TEXT("%d mph%s  ·  %s"), FMath::RoundToInt(Weather.WindMph), *Gust, *Feel), Line2);
}

// Rain: streaks round the camera, falling and blown with the wind (M_Rain: make_game_assets.py)
void AOTGRound::UpdateRain(float Dt)
{
	const double Mm = Weather.RainMm;
	if (Mm <= 0.05 || !RainMat || !Cam)
	{
		if (RainVerts) { RainMesh->ClearAllMeshSections(); RainVerts = 0; RainDrops.Reset(); }
		return;
	}
	const int32 N = FMath::Clamp(int32(700 + Mm * 320), 700, 3200);
	const FVector Half(2200, 2200, 1300);
	const FVector C = CamPos;
	if (RainDrops.Num() != N)
	{
		RainDrops.SetNum(N);
		for (FVector& D : RainDrops) D = C + FVector(FMath::FRandRange(-Half.X, Half.X), FMath::FRandRange(-Half.Y, Half.Y), FMath::FRandRange(-400, Half.Z));
	}
	double Mps, FromDeg;
	HoleWind(Mps, FromDeg);
	const double F = FMath::DegreesToRadians(FromDeg);
	// (blowing away from where it comes from: course (-sin, -cos) is world (-sin, +cos))
	const FVector V(-FMath::Sin(F) * Mps * 60, FMath::Cos(F) * Mps * 60, -950);
	const FVector Dir = V.GetSafeNormal();
	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector2D> UV;
	Verts.Reserve(N * 4);
	Tris.Reserve(N * 6);
	UV.Reserve(N * 4);
	for (FVector& D : RainDrops)
	{
		D += V * Dt;
		FVector R = D - C;
		if (R.Z < -400) { R.Z += Half.Z + 400; R.X = FMath::FRandRange(-Half.X, Half.X); R.Y = FMath::FRandRange(-Half.Y, Half.Y); }
		if (R.X < -Half.X) R.X += 2 * Half.X; else if (R.X > Half.X) R.X -= 2 * Half.X;
		if (R.Y < -Half.Y) R.Y += 2 * Half.Y; else if (R.Y > Half.Y) R.Y -= 2 * Half.Y;
		D = C + R;
		// (none right in front of the lens: a drop there is a blur across the view)
		if ((D - C).SizeSquared() < 250 * 250) D += FVector(0, 0, Half.Z);
		const FVector ToCam = (C - D).GetSafeNormal();
		const FVector Side = FVector::CrossProduct(Dir, ToCam).GetSafeNormal() * 0.35;
		const FVector Bottom = D + Dir * 70;
		const int32 B = Verts.Num();
		Verts.Append({ D - Side, D + Side, Bottom - Side, Bottom + Side });
		UV.Append({ FVector2D(0, 0), FVector2D(1, 0), FVector2D(0, 1), FVector2D(1, 1) });
		Tris.Append({ B, B + 2, B + 1, B + 1, B + 2, B + 3 });
	}
	if (RainVerts != Verts.Num())
	{
		RainMesh->CreateMeshSection_LinearColor(0, Verts, Tris, TArray<FVector>(), UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		RainMesh->SetMaterial(0, RainMat);
		RainVerts = Verts.Num();
	}
	else RainMesh->UpdateMeshSection_LinearColor(0, Verts, TArray<FVector>(), UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>());
	RainMat->SetScalarParameterValue(TEXT("Opacity"), float(FMath::Clamp(0.18 + Mm * 0.04, 0.18, 0.45)));
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
	if (bPaused)
	{
		Toast(TEXT("Paused: the shot wasn't played"));
		return;
	}
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
	// the wind for this shot (the hole's, with a gust), and the air and the ground
	{
		double Mps, FromDeg;
		HoleWind(Mps, FromDeg);
		if (Mps > 0)
		{
			double GustMph, Wobble;
			Weather.Gust(GustMph, Wobble);
			Mps = GustMph * 0.44704;
			FromDeg += Wobble;
		}
		O.windMps = Mps;
		O.windFromDeg = FromDeg;
		O.tempC = Weather.TempC;
		O.humidity = Weather.Humidity;
		O.pressureHpa = Weather.PressureHpa;
		O.wet = Weather.Wetness();
	}
	O.stimp = Data->DefaultStimp;
	O.hasPin = true;
	O.pin = Pin();
	O.trees = &Trees;
	O.collider = &Collider;
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
	PendingShot.Reset();
	const int32 Total = bPickedUp ? MaxStrokes() : Strokes;
	FOTGSide& Sd = Game->Sides[SideIdx];
	Sd.Attempts.Reset();
	Strokes = Total;
	SaveSide();
	Game->HoleOut(Sd, Total, bPickedUp);
	Sd.Ball = Pin();
	const FString ToPar = Total == H.Par ? TEXT("E") : FString::Printf(TEXT("%+d"), Total - H.Par);
	const FString Who = Game->Single() ? FString() : Sd.Name + TEXT(": ");
	const FString Title = Who + (bPickedUp ? FString(TEXT("Picked up")) : ScoreName(Total, H.Par));
	FString Small = FString::Printf(TEXT("Hole %d: %d (%s)%s"), H.Number, Total, *ToPar, Note.IsEmpty() ? TEXT("") : *(TEXT("  ·  ") + Note));
	UE_LOG(LogOTGRound, Display, TEXT("Hole %d: %s%d (%s)"), H.Number, *Who, Total, *ToPar);
	AimMesh->SetVisibility(false);
	// Someone else still to finish the hole: their turn next
	const int32 Next = Game->Next([this](const otg::V3& B) { return PinDist(B); });
	if (Next >= 0)
	{
		Phase = EPhase::Between;
		NextSide = Next;
		PhaseTimer = 2.4;
		Banner(Title, Small, 2.6f);
		TellLaunchMonitor(false);
		UpdateHudInfo();
		return;
	}
	// everyone's in
	Game->EndHole();
	Phase = EPhase::Holed;
	if (!Game->Single())
	{
		const FString News = Game->HoleNews(HoleIndex);
		if (!News.IsEmpty()) Small += TEXT("\n") + News;
		UE_LOG(LogOTGRound, Display, TEXT("%s"), *News);
	}
	Banner(Title, Small, 3.8f);
	TellLaunchMonitor(false);
	PhaseTimer = 2.6;
	bScorecardAuto = true;
	UpdateHudInfo();
}

void AOTGRound::NextHole()
{
	ShowScorecard(false);
	if (HoleIndex + 1 < LastHole) return StartHole(HoleIndex + 1);
	Phase = EPhase::Done;
	RecordRound();
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
	if (bPaused) {} // (everything waits)
	else if (Phase == EPhase::Flying)
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
		if (PhaseTimer <= 0 && !RoundAfterShot()) NextShot();
	}
	else if (Phase == EPhase::Between)
	{
		PhaseTimer -= Dt;
		if (PhaseTimer <= 0) BeginTurn(NextSide, false);
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
	// (a look setting changed: again)
	{
		uint32 Stamp = 0;
		for (const auto* V : { &CVarLookExposure, &CVarLookSaturation, &CVarLookContrast, &CVarLookTemp, &CVarLookSun, &CVarLookSunWarm, &CVarLookSunPitch, &CVarLookSunSide, &CVarLookHaze, &CVarLookFog, &CVarLookVignette, &CVarLookTreeLift, &CVarLookShadows, &CVarLookSky, &CVarLookSunSoft, &CVarLookSlopeBias })
			Stamp = HashCombine(Stamp, GetTypeHash(V->GetValueOnGameThread()));
		Stamp = HashCombine(HashCombine(Stamp, GetTypeHash(HoleIndex)), GetTypeHash(SunHour));
		if (Stamp != LookStamp) { ApplyLook(); ApplyWeatherToScene(); }
	}
	if (!bFreeView) UpdateCamera(Dt);   // (the free view moves it itself: FreeFly)
	UpdateFlag(Dt);
	UpdateMap();
	UpdateRain(Dt);
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
	const FString L1 = FString::Printf(TEXT("Hole %d  ·  Par %d  ·  %s"), H.Number, H.Par, *Len(Dist(TeeOf(H, TeeName), Pin())));
	FString L2;
	const FOTGSide& Sd = Game->Sides[SideIdx];
	const FString Who = Game->Single() ? FString() : Game->SideLabel(SideIdx) + TEXT("  ·  ");
	if (Phase == EPhase::Holed || Phase == EPhase::Done) L2 = FString::Printf(TEXT("%d stroke%s"), Sd.Scores[HoleIndex], Sd.Scores[HoleIndex] == 1 ? TEXT("") : TEXT("s"));
	else if (Phase == EPhase::Between) L2 = FString::Printf(TEXT("%sholed out"), *Who);
	else if (Phase == EPhase::Flying) L2 = FString::Printf(TEXT("%sStroke %d  ·  on its way"), *Who, Strokes);
	else L2 = FString::Printf(TEXT("%sStroke %d  ·  %s to the pin  ·  %s"), *Who, Strokes + 1, OnGreen() ? *Putt(D) : *Len(D), *SurfaceDisplay(Lie));
	const FClub& C = Clubs[ClubIndex(Club)];
	FString L3 = OnGreen() || Club == TEXT("PT") ? FString(C.Name) : FString::Printf(TEXT("%s  ·  aim %s"), C.Name, *Len(AimDist));
	UpdateWindBox();
	Hud->SetInfo(L1, L2, L3);
	// (players together: who's winning)
	if (!Game->Single())
	{
		FString Summary;
		Hud->SetStandings(Game->F.Name, Game->Standings(Summary), Summary);
	}
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
	FOTGCard Card;
	Card.Course = Data->CourseName;
	Card.Format = Game->Single() ? FString() : Game->F.Name;
	Card.bFinal = Phase == EPhase::Done;
	for (int32 i = 0; i < Data->Holes.Num(); i++) Card.Holes.Add(FOTGScoreRow{ Data->Holes[i].Number, Data->Holes[i].Par });
	for (const FOTGSide& S : Game->Sides) Card.Sides.Add(FOTGCard::FSide{ Game->Single() ? FString(TEXT("Score")) : S.Name, S.Colour, S.Scores });
	if (!Game->Single()) Card.Standings = Game->Standings(Card.Summary);
	Hud->ShowScorecard(Card);
}

// ------------------------------------------------------------------ the look

// A clear summer day as golf games show one: the level's sun stronger, warmer and higher, less haze
// (a deeper blue), and the picture graded richer - more saturated and contrasty, a little warm,
// exposed a touch darker so the greens aren't washed out. The weather then works from this.
void AOTGRound::ApplyLook()
{
	uint32 Stamp = 0;
	for (const auto* V : { &CVarLookExposure, &CVarLookSaturation, &CVarLookContrast, &CVarLookTemp, &CVarLookSun, &CVarLookSunWarm, &CVarLookSunPitch, &CVarLookSunSide, &CVarLookHaze, &CVarLookFog, &CVarLookVignette, &CVarLookTreeLift, &CVarLookShadows, &CVarLookSky, &CVarLookSunSoft, &CVarLookSlopeBias })
		Stamp = HashCombine(Stamp, GetTypeHash(V->GetValueOnGameThread()));
	LookStamp = HashCombine(HashCombine(Stamp, GetTypeHash(HoleIndex)), GetTypeHash(SunHour));
	// (the time of day: the sun's height and bearing then, and how much day there is - the sun and
	// sky dimming through dawn and dusk)
	double Elev = 45, Az = 180;
	const bool bTimed = SunHour >= 0;
	if (bTimed) SunAt(SunHour, Elev, Az);
	const float SunDay = bTimed ? float(FMath::SmoothStep(-3.0, 8.0, Elev)) : 1.f;
	const float SkyDay = bTimed ? FMath::Max(0.04f, float(FMath::SmoothStep(-10.0, 10.0, Elev))) : 1.f;
	if (!LookVolume)
	{
		LookVolume = GetWorld()->SpawnActor<APostProcessVolume>();
		LookVolume->bUnbound = true;
		LookVolume->Priority = 10;
	}
	FPostProcessSettings& P = LookVolume->Settings;
	P.bOverride_AutoExposureBias = true;
	P.AutoExposureBias = CVarLookExposure.GetValueOnGameThread();
	const float Sat = CVarLookSaturation.GetValueOnGameThread(), Con = CVarLookContrast.GetValueOnGameThread();
	P.bOverride_ColorSaturation = true;
	P.ColorSaturation = FVector4(Sat, Sat, Sat, 1);
	P.bOverride_ColorContrast = true;
	P.ColorContrast = FVector4(Con, Con, Con, 1);
	P.bOverride_WhiteTemp = true;
	P.WhiteTemp = CVarLookTemp.GetValueOnGameThread();
	P.bOverride_VignetteIntensity = true;
	P.VignetteIntensity = CVarLookVignette.GetValueOnGameThread();
	// (lighter trees: their crowns' insides get some of the sky's light, and the darkest tones lift)
	P.bOverride_LumenSkylightLeaking = true;
	P.LumenSkylightLeaking = CVarLookTreeLift.GetValueOnGameThread();
	const float Sh = CVarLookShadows.GetValueOnGameThread();
	P.bOverride_ColorGainShadows = true;
	P.ColorGainShadows = FVector4(Sh, Sh, Sh, 1);
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		if (!L) continue;
		if (OrigSun < 0) { OrigSun = L->Intensity; OrigSunRot = It->GetActorRotation(); OrigSunColour = L->GetLightColor(); }
		L->SetIntensity(OrigSun * CVarLookSun.GetValueOnGameThread() * SunDay);
		// (smooth shadow edges on the hills: the ground is a mesh on a 1 m grid, and a low sun along
		// it shadows its facets in a comb of teeth - a slightly softer sun and more slope bias)
		L->SetLightSourceAngle(CVarLookSunSoft.GetValueOnGameThread());
		L->SetShadowSlopeBias(CVarLookSlopeBias.GetValueOnGameThread());
		L->SetLightColor(FMath::Lerp(OrigSunColour, FLinearColor(1.f, 0.86f, 0.68f), CVarLookSunWarm.GetValueOnGameThread()));
		// (lit from behind the player on every hole, a little to one side, as golf games light them:
		// looking into the sun the trees are dark shapes)
		if (bTimed)
		{
			// (towards the sun: X east, Y south; the light travels the other way)
			const double E = FMath::DegreesToRadians(Elev), A = FMath::DegreesToRadians(Az);
			const FVector ToSun(FMath::Sin(A) * FMath::Cos(E), -FMath::Cos(A) * FMath::Cos(E), FMath::Sin(E));
			It->SetActorRotation((-ToSun).Rotation());
			break;
		}
		const float Pitch = CVarLookSunPitch.GetValueOnGameThread(), Side = CVarLookSunSide.GetValueOnGameThread();
		double Yaw = OrigSunRot.Yaw;
		if (Side < 900 && Data && Data->Holes.IsValidIndex(HoleIndex))
		{
			const FOTGHole& H = Hole();
			const otg::V3 T = TeeOf(H, TeeName);
			const double Hd = HeadingTo(T, H.GreenCenter.X, H.GreenCenter.Z);
			Yaw = FMath::RadiansToDegrees(FMath::Atan2(-FMath::Cos(Hd), FMath::Sin(Hd))) + Side; // (the light travelling down the hole)
		}
		It->SetActorRotation(FRotator(Pitch > 900 ? OrigSunRot.Pitch : Pitch, Yaw, OrigSunRot.Roll));
		break;
	}
	for (TObjectIterator<USkyAtmosphereComponent> It; It; ++It)
	{
		if (It->GetWorld() != GetWorld()) continue;
		if (OrigMie < 0) { OrigMie = It->MieScatteringScale; OrigRayleigh = It->RayleighScatteringScale; }
		It->SetMieScatteringScale(OrigMie * CVarLookHaze.GetValueOnGameThread());
		break;
	}
	for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
	{
		USkyLightComponent* SL = It->GetLightComponent();
		if (!SL) continue;
		if (OrigSky < 0) OrigSky = SL->Intensity;
		SL->SetIntensity(OrigSky * CVarLookSky.GetValueOnGameThread() * SkyDay);
		if (!SL->bRealTimeCapture) SL->RecaptureSky();   // (the sky's own colour at this hour)
		break;
	}
	for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It)
	{
		UExponentialHeightFogComponent* F = It->GetComponent();
		if (!F) continue;
		if (OrigFog < 0) OrigFog = F->FogDensity;
		F->SetFogDensity(OrigFog * CVarLookFog.GetValueOnGameThread());
		break;
	}
	// (the weather works from these now)
	BaseSun = BaseFogDensity = BaseRayleigh = BaseMie = BaseSkyLight = -1;
}

// ------------------------------------------------------------------ the weather

void AOTGRound::SetupWeather()
{
	FString Mode = Setup_.Weather.IsEmpty() ? FString(TEXT("live")) : Setup_.Weather;
	// (-weather=<key> on the command line: for the round's start only, so the weather panel can change it)
	if (!bWeatherFromCommandLine)
	{
		bWeatherFromCommandLine = true;
		if (FParse::Value(FCommandLine::Get(), TEXT("weather="), Mode)) Setup_.Weather = Mode;
	}
	WindRel.Reset();
	for (int32 i = 0; i < Data->Holes.Num(); i++) WindRel.Add(Setup_.WindDir == TEXT("rand") ? FMath::FRand() * 360 : FCString::Atod(*Setup_.WindDir));
	if (Mode == TEXT("custom")) Weather = FOTGWeather::Custom(Setup_.WindMph);
	else Weather = FOTGWeather::Preset(Mode);
	if (Mode == TEXT("live"))
	{
		// (breezy until the course's real weather arrives, or if it can't)
		Weather = FOTGWeather::Preset(TEXT("breezy"));
		if (Data->Latitude == 0 && Data->Longitude == 0) Toast(TEXT("This course has no location: breezy weather"), 4.f);
		else
		{
			TWeakObjectPtr<AOTGRound> Self(this);
			FOTGWeather::FetchLive(Data->Latitude, Data->Longitude, [Self](bool bOk, const FOTGWeather& W, const FString& Why) {
				AOTGRound* R = Self.Get();
				if (!R) return;
				if (!bOk)
				{
					R->Toast(FString::Printf(TEXT("Live weather unavailable (%s): breezy"), *Why), 4.f);
					return;
				}
				R->Weather = W;
				R->ApplyWeatherToScene();
				R->UpdateHudInfo();
				R->Toast(FString::Printf(TEXT("Weather at the course: %s, %.0f\u00B0C, wind %.0f mph"), *W.Label, W.TempC, W.WindMph), 4.f);
			});
		}
	}
	ApplyWeatherToScene();
	UE_LOG(LogOTGRound, Display, TEXT("Weather: %s, wind %.0f mph (gusts %.0f), %.0f C"), *Weather.Label, Weather.WindMph, Weather.GustMph, Weather.TempC);
}

// Fog thickens the level's height fog (rain a little too); cloud dims the sun and covers the sky;
// a storm darkens the clouds
void AOTGRound::ApplyWeatherToScene()
{
	for (TActorIterator<AVolumetricCloud> It(GetWorld()); It; ++It)
	{
		UVolumetricCloudComponent* VC = It->FindComponentByClass<UVolumetricCloudComponent>();
		if (!VC) continue;
		if (!CloudMat)
		{
			UMaterialInterface* Base = VC->GetMaterial();
			if (!Base) break;
			Base->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Cloud_GlobalCoverage")), BaseCoverage);
			Base->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Cloud_GlobalDensity")), BaseDensity);
			CloudMat = UMaterialInstanceDynamic::Create(Base, this);
			VC->SetMaterial(CloudMat);
			UE_LOG(LogOTGRound, Display, TEXT("Clouds: coverage %.2f, density %.2f in the level"), BaseCoverage, BaseDensity);
		}
		const float Cl = float(Weather.Cloud);
		// (coverage is an offset: the level's own about right for a fair day's few clouds)
		CloudMat->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), BaseCoverage + FMath::Lerp(-0.25f, 1.5f, Cl));
		CloudMat->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"), BaseDensity * FMath::Lerp(0.8f, 4.f, Cl));
		CloudMat->SetScalarParameterValue(TEXT("StormClouds"), Weather.RainMm >= 5 ? 1.f : Weather.RainMm > 0.05 ? 0.4f : 0.f);
		break;
	}
	for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It)
	{
		UExponentialHeightFogComponent* F = It->GetComponent();
		if (!F) continue;
		if (BaseFogDensity < 0) BaseFogDensity = F->FogDensity;
		F->SetFogDensity(FMath::Lerp(BaseFogDensity, 0.25f, float(FMath::Min(1.0, Weather.Fog + (Weather.RainMm > 0.05 ? 0.06 : 0.0)))));
		break;
	}
	// an overcast sky: grey rather than blue (less of the air's blue scattering, more haze), and less
	// light from it
	const float Over = float(FMath::Clamp((Weather.Cloud - 0.5) / 0.5, 0.0, 1.0));
	for (TObjectIterator<USkyAtmosphereComponent> It; It; ++It)
	{
		if (It->GetWorld() != GetWorld()) continue;
		if (BaseRayleigh < 0) { BaseRayleigh = It->RayleighScatteringScale; BaseMie = It->MieScatteringScale; }
		It->SetRayleighScatteringScale(BaseRayleigh * FMath::Lerp(1.f, 0.3f, Over));
		It->SetMieScatteringScale(BaseMie * FMath::Lerp(1.f, 6.f, Over));
		break;
	}
	for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
	{
		USkyLightComponent* SL = It->GetLightComponent();
		if (!SL) continue;
		if (BaseSkyLight < 0) BaseSkyLight = SL->Intensity;
		SL->SetIntensity(BaseSkyLight * FMath::Lerp(1.f, 0.7f, Over));
		break;
	}
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		if (!L) continue;
		if (BaseSun < 0) BaseSun = L->Intensity;
		L->SetIntensity(BaseSun * FMath::Lerp(1.f, 0.35f, float(FMath::Clamp(Weather.Cloud - 0.3, 0.0, 0.7) / 0.7)));
		break;
	}
}

// The steady wind on this hole, in course space (FromDeg: where it blows from, as the aim's heading)
void AOTGRound::HoleWind(double& Mps, double& FromDeg) const
{
	Mps = 0;
	FromDeg = 0;
	if (Weather.WindMph < 0.5) return;
	Mps = Weather.WindMph * 0.44704;
	if (Weather.bRelative)
	{
		const FOTGHole& H = Hole();
		const otg::V3 T = TeeOf(H, TeeName);
		FromDeg = FMath::RadiansToDegrees(HeadingTo(T, H.GreenCenter.X, H.GreenCenter.Z)) + (WindRel.IsValidIndex(HoleIndex) ? WindRel[HoleIndex] : 0);
	}
	else FromDeg = Weather.WindFromMet; // (the course's z is north)
}

// "Wind 9 mph, into you from the left": against the way the shot is aimed
FString AOTGRound::WindText() const
{
	double Mps, FromDeg;
	HoleWind(Mps, FromDeg);
	if (Mps < 0.5 * 0.44704) return TEXT("No wind");
	double Rel = FMath::Fmod(FromDeg - FMath::RadiansToDegrees(Heading) + 540.0, 360.0) - 180.0; // (0: from straight ahead, + from the right)
	const double A = FMath::Abs(Rel);
	const FString SideS = Rel > 0 ? TEXT("right") : TEXT("left");
	FString Where;
	if (A < 22.5) Where = TEXT("into you");
	else if (A < 67.5) Where = TEXT("into you from the ") + SideS;
	else if (A < 112.5) Where = TEXT("from the ") + SideS;
	else if (A < 157.5) Where = TEXT("helping, from the ") + SideS;
	else Where = TEXT("helping");
	return FString::Printf(TEXT("Wind %.0f mph %s"), Weather.WindMph, *Where);
}

// ------------------------------------------------------------------ players taking turns

// Side I plays: its ball, lie and strokes into the round's own, the others' balls where they lie
void AOTGRound::LoadSide(int32 I)
{
	SideIdx = FMath::Max(0, I);
	const FOTGSide& S = Game->Sides[SideIdx];
	Ball = S.Ball; PrevBall = S.PrevBall; Lie = S.Lie; Strokes = S.Strokes;
	Undo.Reset();
	PlaceBall(Ball);
	ShowOtherBalls();
}

// The playing side's ball, lie and strokes back into its own record
void AOTGRound::SaveSide()
{
	FOTGSide& S = Game->Sides[SideIdx];
	S.Ball = Ball; S.PrevBall = PrevBall; S.Lie = Lie; S.Strokes = Strokes;
}

void AOTGRound::ShowOtherBalls()
{
	for (int32 i = 0; i < OtherBalls.Num(); i++)
	{
		const FOTGSide& S = Game->Sides[i];
		// (not one sharing the spot with this ball - the same tee - where it would hide it)
		const bool bShow = i != SideIdx && !S.bHoled && Dist(S.Ball, Ball) > 1.0;
		OtherBalls[i]->SetVisibility(bShow);
		if (bShow)
		{
			OtherBalls[i]->SetWorldScale3D(FVector(0.0427 * 2));
			OtherBalls[i]->SetWorldLocation(World(S.Ball) + FVector(0, 0, 2.135 * 2));
		}
	}
}

// After a shot with several sides: the scramble's next player from the same spot (or the team's
// best shot once everyone's hit), then whoever's away. False: the same player plays on.
bool AOTGRound::RoundAfterShot()
{
	FOTGSide& Sd = Game->Sides[SideIdx];
	if (Game->Single())
	{
		SaveSide();
		Sd.Shots++;
		return false;
	}
	auto D = [this](const otg::V3& B) { return PinDist(B); };
	if (Game->F.Shared == TEXT("scramble"))
	{
		FOTGAttempt A;
		A.Ball = Ball; A.Lie = Lie; A.Strokes = Strokes;
		const TOptional<FOTGAttempt> Best = Game->ScrambleAttempt(Sd, A, D);
		if (!Best)
		{
			// (back to the spot for the next player in the team)
			BeginTurn(SideIdx, true);
			return true;
		}
		Sd.PrevBall = Sd.Ball;
		Sd.Ball = Best->Ball; Sd.Lie = Best->Lie; Sd.Strokes = Best->Strokes;
		Sd.Shots++;
		Toast(FString::Printf(TEXT("%s plays %s's shot"), *Sd.Name, *Game->Players[Best->By].Name), 3.f);
	}
	else
	{
		SaveSide();
		Sd.Shots++;
	}
	const int32 Next = Game->Next(D);
	if (Next < 0) return false;
	BeginTurn(Next, false);
	return true;
}

// The next player steps up: their ball, the view behind it, whose turn it is
void AOTGRound::BeginTurn(int32 I, bool bSameSpot)
{
	LoadSide(I);
	Phase = EPhase::Ready;
	ClearTracer();
	ResetAim(Strokes > 0);
	AddressView(false);
	Banner(Game->SideLabel(SideIdx), bSameSpot ? FString(TEXT("from the same spot")) : FString::Printf(TEXT("%s to the pin  ·  %s"), OnGreen() ? *Putt(DistToPin()) : *Len(DistToPin()), *SurfaceDisplay(Lie)), 2.2f);
	UpdateHudInfo();
	TellLaunchMonitor(true);
	if (PendingShot)
	{
		const FOTGShot S = *PendingShot;
		PendingShot.Reset();
		OnShot(S);
	}
}

// Each player's round kept with their profile
void AOTGRound::RecordRound()
{
	if (Setup_.Picked.IsEmpty()) return;
	TArray<FOTGPlayerProfile> Profiles = FOTGPlayers::Load();
	for (const FOTGGamePlayer& P : Game->Players)
	{
		const FOTGSide* S = nullptr;
		for (const FOTGSide& X : Game->Sides) for (int32 M : X.Members) if (Game->Players[M].Id == P.Id) S = &X;
		FOTGPlayerProfile* Prof = Profiles.FindByPredicate([&](const FOTGPlayerProfile& X) { return X.Id == P.Id; });
		if (!S || !Prof) continue;
		FOTGPlayedRound R;
		R.At = FDateTime::Now();
		R.Course = Data->CourseName;
		R.Format = Game->F.Key;
		for (int32 H = 0; H < S->Scores.Num(); H++)
			if (S->Scores[H] >= 0) { R.Holes++; R.Strokes += S->Scores[H]; R.Par += Data->Holes[H].Par; }
		if (R.Holes) Prof->Rounds.Add(R);
	}
	FOTGPlayers::Save(Profiles);
}

// ------------------------------------------------------------------ the pause menu

void AOTGRound::TogglePause()
{
	if (Phase == EPhase::Done) return;
	bPaused = !bPaused;
	if (Hud)
	{
		if (bPaused) Hud->ShowPause(FSimpleDelegate::CreateUObject(this, &AOTGRound::TogglePause), FSimpleDelegate::CreateUObject(this, &AOTGRound::ToggleScorecard),
			FSimpleDelegate::CreateUObject(this, &AOTGRound::QuitToMenu));
		else Hud->HidePause();
	}
	TellLaunchMonitor(!bPaused && Phase == EPhase::Ready);
}

void AOTGRound::QuitToMenu()
{
	TellLaunchMonitor(false);
	UGameplayStatics::OpenLevel(this, TEXT("/Engine/Maps/Entry"));
}
