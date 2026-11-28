// See OTGRound.h: setting the round up, the course and the hole (tees, pins, the aim, the club), every
// frame, and the pause menu. The rest of the round is in files by purpose: OTGRoundInput.cpp (the
// keys), OTGRoundShot.cpp (shots, relief, holing out), OTGRoundTurns.cpp (players taking turns),
// OTGRoundView.cpp (the ball, the aim, the tracer, the cameras), OTGRoundHud.cpp (the HUD),
// OTGRoundLook.cpp (the look and the weather on the scene), OTGRoundSlopeGrid.cpp, OTGRoundRange.cpp,
// OTGRoundExtras.cpp, OTGSky.cpp, and the console commands: OTGRoundCommands.cpp.

#include "Round/OTGRound.h"
#include "Tracer/OTGShotTracerComponent.h"
#include "World/OTGRuntimeCourse.h"
#include "App/OTGHelperSubsystem.h"
#include "Round/OTGMiniGolf.h"
#include "App/OTGNetSubsystem.h"

#include "Camera/CameraActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Texture2D.h"
#include "Minimap/SOTGMinimap.h"
#include "App/OTGPrefs.h"
#include "UI/Hud/SOTGPanels.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Course/OTGCourseData.h"
#include "OTGLaunchMonitorSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UI/Hud/SOTGHud.h"
#include "UI/Hud/SOTGRange.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGRound, Log, All);

#include "Round/OTGRoundShared.h"

AOTGRound::AOTGRound()
{
	Turns.Round = Camera.Round = HudView.Round = Scene.Round = Changing.Round = RainFx.Round = WaterFx.Round = Flag.Round = SlopeGrid.Round = RangeMode.Round = Online.Round = this;
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
	// a golf tee: a thin peg out of the ground, a cup on top for the ball
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	TeeMesh = MakeMesh(TEXT("TeePeg"), Cylinder.Object, false);
	TeeHeadMesh = MakeMesh(TEXT("TeeHead"), Cone.Object, false);
	// (the aim: where the shot's aimed - this just holds the spot; a yardage stick stands there)
	AimMesh->SetHiddenInGame(true);
	AimStick = MakeMesh(TEXT("AimStick"), Cylinder.Object, true);
	AimStickTop = MakeMesh(TEXT("AimStickTop"), Cylinder.Object, false);
	CupMesh = MakeMesh(TEXT("Cup"), Cylinder.Object, false);
	PuttCircle = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PuttCircle"));
	PuttCircle->SetupAttachment(RootComponent);
	PuttCircle->SetStaticMesh(Cube.Object);
	PuttCircle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PuttCircle->SetCastShadow(false);
	PuttCircle->bVisibleInRayTracing = false;
	PuttCircle->SetMobility(EComponentMobility::Movable);
	SlopeGrid.GridMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SlopeGrid"));
	SlopeGrid.GridMesh->SetupAttachment(RootComponent);
	SlopeGrid.GridMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SlopeGrid.GridMesh->SetCastShadow(false);
	SlopeGrid.GridMesh->bVisibleInRayTracing = false;
	SlopeGrid.GridDots = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("SlopeDots"));
	SlopeGrid.GridDots->SetupAttachment(RootComponent);
	SlopeGrid.GridDots->SetStaticMesh(Sphere.Object);
	SlopeGrid.GridDots->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SlopeGrid.GridDots->SetCastShadow(false);
	SlopeGrid.GridDots->bVisibleInRayTracing = false;
	SlopeGrid.GridDots->SetMobility(EComponentMobility::Movable);
	TracerMesh = CreateDefaultSubobject<UOTGShotTracerComponent>(TEXT("Tracer"));
	TracerMesh->SetupAttachment(RootComponent);
	TracerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TracerMesh->SetCastShadow(false);
	TracerMesh->bUseAsyncCooking = true;
	Flag.FlagCloth = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FlagCloth"));
	Flag.FlagCloth->SetupAttachment(RootComponent);
	Flag.FlagCloth->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RainFx.RainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Rain"));
	RainFx.RainMesh->SetupAttachment(RootComponent);
	RainFx.RainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RainFx.RainMesh->SetCastShadow(false);
}

void AOTGRound::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
	AOTGRound* R = CastChecked<AOTGRound>(InThis);
	R->Turns.AddReferencedObjects(Collector);
	R->Camera.AddReferencedObjects(Collector);
	R->Scene.AddReferencedObjects(Collector);
	R->RainFx.AddReferencedObjects(Collector);
	R->WaterFx.AddReferencedObjects(Collector);
	R->Flag.AddReferencedObjects(Collector);
	R->SlopeGrid.AddReferencedObjects(Collector);
	R->RangeMode.AddReferencedObjects(Collector);
	R->Online.AddReferencedObjects(Collector);
}

void FOTGRoundTurns::AddReferencedObjects(FReferenceCollector& Collector) { Collector.AddReferencedObjects(OtherBalls); }
void FOTGRoundCamera::AddReferencedObjects(FReferenceCollector& Collector) { Collector.AddReferencedObject(Cam); }
void FOTGRoundRain::AddReferencedObjects(FReferenceCollector& Collector) { Collector.AddReferencedObject(RainMesh); Collector.AddReferencedObject(RainMat); }
void FOTGRoundWaterFx::AddReferencedObjects(FReferenceCollector& Collector) { Collector.AddReferencedObject(RainRingMat); Collector.AddReferencedObject(RippleMesh); Collector.AddReferencedObject(RippleMat); }
void FOTGRoundFlag::AddReferencedObjects(FReferenceCollector& Collector) { Collector.AddReferencedObject(FlagCloth); }
void FOTGRoundSlopeGrid::AddReferencedObjects(FReferenceCollector& Collector) { Collector.AddReferencedObject(GridMesh); Collector.AddReferencedObject(GridDots); Collector.AddReferencedObjects(GridMats); }
void FOTGRoundRangeMode::AddReferencedObjects(FReferenceCollector& Collector) { Collector.AddReferencedObject(RangeProps); Collector.AddReferencedObject(RangeDots); Collector.AddReferencedObject(RangeDotMat); }

// ------------------------------------------------------------------ setting up

void AOTGRound::Setup(UOTGCourseData* InData, const FOTGRoundSetup& InSetup)
{
	Data = InData;
	Setup_ = InSetup;
	if (FParse::Param(FCommandLine::Get(), TEXT("autoputt"))) Setup_.bAutoPutt = true;   // (testing: auto-putt whatever the menu has)
	// (the driving range: on your own, a 7 iron to start)
	bRange = otg::range::IsRange(Data);
	if (bRange)
	{
		Setup_.Picked.Reset();
		Setup_.Format = TEXT("stroke");
		Club = TEXT("I7");
	}
	bMini = Data->Generated == AOTGMiniGolf::CourseId;
	if (bMini)
	{
		// (calm, no slope grid, a short flag)
		Setup_.Weather = TEXT("calm");
		SlopeGrid.bGridOn = false;
		FlagTopCm = 105;
	}
	Data->FillPlayCourse(Course);
	Data->FillTrees(Trees);
	BuildCollider();
	SetActorLocation(FVector::ZeroVector);
	TeeName = Setup_.Tee.IsEmpty() ? FString(TEXT("White")) : Setup_.Tee;
	PinName = Setup_.Pin.IsEmpty() ? FString(TEXT("Saturday")) : Setup_.Pin;

	// The players and the game (nobody picked: a round on your own; an online game: its players)
	TArray<FOTGGamePlayer> Players;
	const bool bOnline = !bRange && !bMini && Online.Begin(Setup_, Players);
	const TArray<FOTGPlayerProfile> Profiles = bOnline ? TArray<FOTGPlayerProfile>() : FOTGPlayers::Load();
	for (const FString& Id : Setup_.Picked)
		for (const FOTGPlayerProfile& P : Profiles)
			if (P.Id == Id)
			{
				FOTGGamePlayer G;
				G.Id = P.Id; G.Name = P.Name; G.Colour = P.Colour; G.Handicap = P.Handicap; G.bLeftHanded = P.bLeftHanded; G.Tee = P.Tee;
				G.Team = Setup_.Teams.Contains(Id) ? Setup_.Teams[Id] : TEXT("A");
				Players.Add(G);
			}
	if (!bOnline && (!Players.Num() || !FOTGGame::Problem(Setup_.Format, Setup_.Picked, Setup_.Teams).IsEmpty()))
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
	Turns.Game = MakeUnique<FOTGGame>(Players, Setup_.Format, Setup_.bHandicaps, HoleInfo);
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
	if (!Turns.Game->Single())
		for (int32 i = 0; i < Turns.Game->Sides.Num(); i++)
		{
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
			C->SetStaticMesh(SphereMesh);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetMobility(EComponentMobility::Movable);
			C->SetupAttachment(RootComponent);
			C->RegisterComponent();
			Mat(C, Turns.Game->Sides[i].Colour);
			C->SetVisibility(false);
			Turns.OtherBalls.Add(C);
		}
	AimMat = Mat(AimMesh, FLinearColor(1.f, 0.78f, 0.15f));
	PinMat = Mat(PinMesh, FLinearColor(0.9f, 0.9f, 0.85f));
	FlagMat = Mat(FlagMesh, FLinearColor(0.85f, 0.05f, 0.04f));
	Mat(TeeMesh, FLinearColor(0.9f, 0.9f, 0.9f));
	Mat(TeeHeadMesh, FLinearColor(0.9f, 0.9f, 0.9f));
	Mat(CupMesh, FLinearColor(0.01f, 0.012f, 0.01f));
	TracerMat = Mat(nullptr, FLinearColor(1.f, 0.72f, 0.2f));
	Mat(AimStick, FLinearColor(0.92f, 0.92f, 0.9f));
	Mat(AimStickTop, FLinearColor(1.f, 0.78f, 0.1f));
	// (the slope grid's colours: flat white, then yellow, orange and red at 3% and over - as the web
	// version's; and its dots white)
	for (const FLinearColor& C : { FLinearColor(0.95f, 0.95f, 0.92f), FLinearColor(1.f, 0.9f, 0.2f), FLinearColor(1.f, 0.55f, 0.12f), FLinearColor(0.95f, 0.18f, 0.1f) })
		SlopeGrid.GridMats.Add(Mat(nullptr, C));
	SlopeGrid.GridDots->SetMaterial(0, Mat(nullptr, FLinearColor(1.f, 1.f, 1.f)));
	TracerMesh->SetMaterial(0, TracerMat);
	TracerMesh->RibbonMaterial = TracerMat;

	// The camera
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	Camera.Cam = GetWorld()->SpawnActor<ACameraActor>();
	Camera.Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
	Camera.Cam->GetCameraComponent()->SetFieldOfView(60);
	if (PC) PC->SetViewTarget(Camera.Cam);

	// The HUD
	if (GEngine && GEngine->GameViewport)
	{
		Hud = SNew(SOTGHud);
		GEngine->GameViewport->AddViewportWidgetContent(Hud.ToSharedRef(), 10);
		// the minimap: the course photo, over the landscape's area
		const FString PhotoPath = FString::Printf(TEXT("/Game/Courses/%s/Build/Textures/T_CoursePhoto.T_CoursePhoto"), *Data->CourseId);
		UTexture2D* Photo = nullptr;
		// (an exported course: the photo its builder loaded from the folder; a pack: the pack's)
		if (!Data->ExportDir.IsEmpty())
			for (TActorIterator<AOTGRuntimeCourse> It(GetWorld()); It; ++It) { Photo = It->Pictures().FindRef(TEXT("Photo")); break; }
		if (!Photo) Photo = LoadObject<UTexture2D>(nullptr, *PhotoPath);
		if (!Photo) UE_LOG(LogOTGRound, Warning, TEXT("No course photo for the minimap (%s)"), *PhotoPath);
		TWeakObjectPtr<AOTGRound> Self(this);
		Hud->Minimap()->SetCourse(Photo, Data->HeightX0, Data->HeightZ1, (Data->HeightSamples - 1) * Data->MetresPerSample, [Self](double X, double Z) {
			return Self.IsValid() ? SurfaceDisplay(Self->Course.SurfaceAt(X, Z)) : FString();
		});
		Hud->Minimap()->OnAim = [Self](double X, double Z) { if (Self.IsValid()) Self->AimAt(X, Z); };
		if (bMini) Hud->Minimap()->SetVisibility(EVisibility::Collapsed);   // (no photo of the putt-putt)
		// the test shot panel (T): hit as if the launch monitor had sent it
		Hud->ShotPanel()->PuttMph = [Self]() {
			return Self.IsValid() ? Self->PuttMps(0.4) / 0.44704 : 5.0;
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
		// (the driving range: its panel where the minimap is, and its keys)
		if (bRange)
		{
			RangeMode.RangePanel = SNew(SOTGRangePanel);
			Hud->SetPractice(RangeMode.RangePanel.ToSharedRef(), TEXT("Click the ground to aim     Left / Right: aim     Up / Down: club     T: test shot panel     Space: quick test shot     Enter / Backspace: new ball     Tab: hide the numbers     D: shot data     F: free view     L: weather and time of day     P: frame rate     Esc: menu"));
		}
		// (the course's name over the hole's)
		else Hud->SetCourseName(bMini ? FString(TEXT("Putt-putt")) : !Data->CourseName.IsEmpty() ? Data->CourseName : Data->CourseId.Replace(TEXT("_"), TEXT(" ")));
	}
	// the driving range's mats, flags and distance boards, and a mark where each of the club's last
	// shots stopped
	if (bRange)
	{
		RangeMode.RangeProps = GetWorld()->SpawnActor<AOTGRangeProps>();
		if (RangeMode.RangeProps) RangeMode.RangeProps->Build(Data);
		RangeMode.RangeDots = NewObject<UInstancedStaticMeshComponent>(this);
		RangeMode.RangeDots->SetStaticMesh(CupMesh->GetStaticMesh());
		RangeMode.RangeDots->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RangeMode.RangeDots->SetCastShadow(false);
		RangeMode.RangeDots->SetMobility(EComponentMobility::Movable);
		RangeMode.RangeDots->SetupAttachment(RootComponent);
		RangeMode.RangeDots->RegisterComponent();
		RangeMode.RangeDotMat = Mat(RangeMode.RangeDots, FLinearColor(1.f, 0.7f, 0.1f));
	}
	UMaterialInterface* Rain = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenTeeGolf/Game/M_Rain.M_Rain"));
	if (Rain)
	{
		RainFx.RainMat = UMaterialInstanceDynamic::Create(Rain, this);
		RainFx.RainMesh->SetMaterial(0, RainFx.RainMat);
	}
	else UE_LOG(LogOTGRound, Warning, TEXT("No rain material (unreal/make_assets.ps1 makes it)"));

	// Shots
	if (UOTGLaunchMonitorSubsystem* LM = GetGameInstance()->GetSubsystem<UOTGLaunchMonitorSubsystem>())
	{
		ShotHandle = LM->OnShotNative.AddUObject(this, &AOTGRound::OnShot);
		ShotUpdateHandle = LM->OnShotUpdateNative.AddUObject(this, &AOTGRound::OnShotUpdate);
		NoticeHandle = LM->OnNoticeNative.AddWeakLambda(this, [this](const FString& Text) { HudView.Toast(Text, 6.f); });
	}

	// Courses built in the background (the Course builder page's queue): told here when one is ready
	if (UOTGHelperSubsystem* HS = GetGameInstance()->GetSubsystem<UOTGHelperSubsystem>())
		HS->OnJobDone.AddWeakLambda(this, [this](const FOTGHelperJob& J) {
			if (J.Name.IsEmpty()) return;
			if (J.State == TEXT("done") && J.Kind == TEXT("export")) HudView.Toast(J.Name + TEXT(" is ready: play it from the menu"), 6.f);
			else if (J.State == TEXT("done") && J.bNeedsReview) HudView.Toast(J.Name + TEXT(" is built: its holes need looking over in the editor"), 6.f);
			else if (J.State == TEXT("failed")) HudView.Toast(J.Name + TEXT(": building it didn't work (see Course builder)"), 6.f);
		});

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
	Online.AfterSetup();
	Scene.ApplyLook();
	SetupWeather();
	UE_LOG(LogOTGRound, Display, TEXT("Round on %s: %d holes, %d trunks and buildings to hit; %s, %d side(s), %s tees, %s pins"), *Data->CourseName, Data->Holes.Num(), int32(Collider.Count()),
		*Turns.Game->F.Name, Turns.Game->Sides.Num(), *TeeName, *PinName);
	StartHole(First);
}

void AOTGRound::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UGameInstance* GI = GetGameInstance())
		if (UOTGLaunchMonitorSubsystem* LM = GI->GetSubsystem<UOTGLaunchMonitorSubsystem>())
		{
			LM->OnShotNative.Remove(ShotHandle);
			LM->OnShotUpdateNative.Remove(ShotUpdateHandle);
			LM->OnNoticeNative.Remove(NoticeHandle);
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
int32 AOTGRound::MaxStrokes() const { return bMini ? 6 : Hole().Par + 5; }   // (putt-putt: 6, as on most mini courses)

double AOTGRound::PuttMps(double Extra) const
{
	double D = DistToPin();
	if (!bMini) return otg::of::PuttSpeed(D + Extra, Data->DefaultStimp);
	// (the putt-putt: round the turn on the first putt, and enough more to climb a ramp or a volcano)
	const otg::V3 P = Pin();
	const FOTGHole& H = Hole();
	if (H.Aims.IsValidIndex(AimTurn))
	{
		// (by the turns from that one on)
		otg::V3 From = Ball;
		D = 0;
		for (int32 i = AimTurn; i < H.Aims.Num(); i++)
		{
			const otg::V3 A{ H.Aims[i].X, 0, H.Aims[i].Z };
			D += Dist(From, A);
			From = A;
		}
		D += Dist(From, P);
	}
	const double V = otg::of::PuttSpeed(D + Extra, Data->DefaultStimp);
	return FMath::Sqrt(V * V + 2 * 9.81 * FMath::Max(0.0, P.y - Ball.y) * 1.15);
}
otg::V3 AOTGRound::AimPoint() const { return OnGround(Ball.x + FMath::Sin(Heading) * AimDist, Ball.z + FMath::Cos(Heading) * AimDist); }

FString AOTGRound::Len(double M) const { return FString::Printf(TEXT("%d yd"), FMath::RoundToInt(M * M_TO_YD)); }
FString AOTGRound::Putt(double M) const { return FString::Printf(TEXT("%d ft"), FMath::RoundToInt(M * M_TO_FT)); }

// ------------------------------------------------------------------ the hole

void AOTGRound::StartHole(int32 Index)
{
	HoleIndex = Index;
	const FOTGHole& H = Hole();
	// every side on its tee (a player's own tees, or the round's), the honour first
	Turns.Game->StartHole(Index, [&](const FOTGSide& S) {
		const FString Own = S.Members.Num() == 1 ? Turns.Game->Players[S.Members[0]].Tee : FString();
		return TeeOf(H, Own);
	});
	// (everyone at once online: this computer's player, always)
	Turns.LoadSide(Online.bTogether ? Online.LocalSide : Turns.Game->Next([this](const otg::V3& B) { return PinDist(B); }));
	Online.OnStartHole();
	PendingShot.Reset();
	bHasRes = false;
	Phase = EPhase::Ready;
	ClearTracer();
	HudView.ShowScorecard(false);
	if (Hud) Hud->SetShotData(FString(), FString());
	// the flag, and a marker on the tee
	const otg::V3 P = Pin();
	PinMesh->SetWorldLocation(World(P) + FVector(0, 0, bMini ? FlagTopCm / 2 : 115));
	PinMesh->SetWorldScale3D(FVector(0.02, 0.02, bMini ? FlagTopCm / 100 : 2.3));
	FlagMesh->SetVisibility(false);   // (the cloth flag instead: UpdateFlag)
	// (the cup: a dark disc 10.8 cm across, just above the green)
	CupMesh->SetWorldLocation(World(P) + FVector(0, 0, 0.3));
	CupMesh->SetWorldScale3D(FVector(0.108, 0.108, 0.002));
	// (the engine's cylinder and cone are a metre across and tall, centred: a peg 6 mm thick showing
	// 3 cm, a cup 1.5 cm across turned wide side up, the ball resting in it TeeLiftCm up)
	const FVector TeeAt = World(TeeOf(H, TeeName));
	TeeMesh->SetWorldLocation(TeeAt + FVector(0, 0, 1.5));
	TeeMesh->SetWorldScale3D(FVector(0.006, 0.006, 0.03));
	TeeHeadMesh->SetWorldLocation(TeeAt + FVector(0, 0, 3.0));
	TeeHeadMesh->SetWorldRotation(FRotator(180, 0, 0));
	TeeHeadMesh->SetWorldScale3D(FVector(0.015, 0.015, 0.012));
	TeeMesh->SetVisibility(!bMini);   // (putt-putt: off a mat on the felt)
	TeeHeadMesh->SetVisibility(!bMini);
	bAutoPuttDue = false;
	ShowPuttCircle();
	ResetAim(false);
	PlaceBall(Ball);
	Camera.OverviewView();
	double LenM = Dist(Ball, P);
	if (bMini && H.Path.Num() > 1)
	{
		LenM = 0;
		for (int32 i = 1; i < H.Path.Num(); i++) LenM += FVector2D::Distance(FVector2D(H.Path[i - 1].X, H.Path[i - 1].Z), FVector2D(H.Path[i].X, H.Path[i].Z));
	}
	if (bRange)
	{
		HudView.Banner(TEXT("Driving range"), TEXT("Targets at 50, 100, 150, 200 and 250 yards"), 3.f);
		RangeMode.UpdateRange();
	}
	else HudView.Banner(H.Name.IsEmpty() ? FString::Printf(TEXT("Hole %d"), H.Number) : FString::Printf(TEXT("Hole %d  ·  %s"), H.Number, *H.Name), FString::Printf(TEXT("Par %d  ·  %s%s"), H.Par, *HoleLen(LenM),
		Turns.Game->Single() ? TEXT("") : Online.bTogether ? TEXT("  ·  everyone plays at once") : *FString::Printf(TEXT("  ·  %s to play"), *Turns.Game->SideLabel(Turns.SideIdx))), 3.f);
	HudView.UpdateHudInfo();
	TellLaunchMonitor(true);
	UE_LOG(LogOTGRound, Display, TEXT("Hole %d, par %d, %s"), H.Number, H.Par, *HoleLen(LenM));
}

void AOTGRound::ResetAim(bool bToPin)
{
	// (the range: the aim stays where the player turned it, at the club's carry)
	if (bRange)
	{
		AimDist = RangeMode.ClubCarryM();
		SetClub(Club);
		ShowAim();
		return;
	}
	const FOTGHole& H = Hole();
	const otg::V3 P = Pin();
	double TX = P.x, TZ = P.z;
	// From the tee on long holes aim down the fairway (the first aim point)
	if (!bToPin && Strokes == 0 && H.Par >= 4 && H.Aims.Num()) { TX = H.Aims[0].X; TZ = H.Aims[0].Z; }
	// (the putt-putt: at the turn - the board to bank off, the bridge - from the tee, or while the boards
	// stand between the ball and the cup and not between it and the turn)
	AimTurn = -1;
	if (bMini && H.Aims.Num())
	{
		auto Blocked = [this](const otg::V3& From, const otg::V3& To) {
			otg::Hit Hit;
			Collider.SetPin(nullptr);
			return Collider.Cast(otg::V3{ From.x, From.y + 0.02, From.z }, otg::V3{ To.x, To.y + 0.02, To.z }, true, Hit);
		};
		// (from the tee the first; later, the furthest turn the ball can see, if it can't see the cup)
		if (Strokes == 0) AimTurn = 0;
		else if (Blocked(Ball, P))
			for (int32 i = H.Aims.Num() - 1; i >= 0 && AimTurn < 0; i--)
			{
				const otg::V3 A = OnGround(H.Aims[i].X, H.Aims[i].Z);
				if (Dist(Ball, A) > 0.5 && !Blocked(Ball, A)) AimTurn = i;
			}
		if (AimTurn >= 0) { TX = H.Aims[AimTurn].X; TZ = H.Aims[AimTurn].Z; }
	}
	Heading = HeadingTo(Ball, TX, TZ);
	AimDist = FMath::Max(1.0, FMath::Sqrt((TX - Ball.x) * (TX - Ball.x) + (TZ - Ball.z) * (TZ - Ball.z)));
	bClubPicked = false;
	AutoClub();
	ShowAim();
}

void AOTGRound::AutoClub()
{
	if (bRange) return SetClub(Club);   // (the range: the club as picked)
	if (bMini) return SetClub(TEXT("PT"));
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
	HudView.UpdateHudInfo();
	TellLaunchMonitor(Phase == EPhase::Ready);
}

void AOTGRound::TellLaunchMonitor(bool bTurn)
{
	if (!Data) return;
	if (UOTGLaunchMonitorSubsystem* LM = GetGameInstance()->GetSubsystem<UOTGLaunchMonitorSubsystem>())
	{
		const bool bLeft = Turns.Game && Turns.Game->Sides.IsValidIndex(Turns.SideIdx) ? Turns.Game->Players[Turns.Game->Hitter(Turns.Game->Sides[Turns.SideIdx])].bLeftHanded : false;
		LM->SetPlayer(Club, (bRange ? AimDist : DistToPin()) * M_TO_YD, bLeft);
		LM->SetTurn(bTurn && Online.MayHit());   // (online, taking turns: only on this computer's player's)
	}
}

// ------------------------------------------------------------------ every frame

void AOTGRound::Tick(float Dt)
{
	SlopeGrid.UpdateSlopeGrid(Dt);
	// the aim: a yardage stick standing on the spot - white, its top yellow - sized by how far it is
	// from the camera so it reads the same on screen near or far (true size it'd vanish at 250 m)
	{
		const bool bShow = AimMesh && AimMesh->GetVisibleFlag();
		AimStick->SetVisibility(bShow);
		AimStickTop->SetVisibility(bShow);
		if (bShow)
		{
			const FVector At = AimMesh->GetComponentLocation() - FVector(0, 0, 3);
			const double H = FMath::Max(120.0, FVector::Dist(Camera.CamPos, At) * 0.03);   // cm
			const double Wd = H * 0.02;
			AimStick->SetWorldLocation(At + FVector(0, 0, H * 0.4));
			AimStick->SetWorldScale3D(FVector(Wd / 100, Wd / 100, H * 0.8 / 100));
			AimStickTop->SetWorldLocation(At + FVector(0, 0, H * 0.9));
			AimStickTop->SetWorldScale3D(FVector(Wd * 1.15 / 100, Wd * 1.15 / 100, H * 0.2 / 100));
		}
	}
	Super::Tick(Dt);
	if (!Data) return;
	RoundTime += Dt;
	HudView.UpdatePinTag();
	for (int32 i = 0; i < ExecAt.Num();)
	{
		if (RoundTime < ExecAt[i].Key) { i++; continue; }
		const FString Command = ExecAt[i].Value;
		ExecAt.RemoveAt(i);
		UE_LOG(LogOTGRound, Display, TEXT("(roundexec) %s"), *Command);
		if (GEngine) GEngine->Exec(GetWorld(), *Command);
	}
	// (lining up a shot among trees: those between the camera and 40 m along the aim hidden from view, so
	// you can see where you're aiming - only from view: the ball still meets them; back as the ball's hit)
	if (AOTGRuntimeCourse* SeeRC = CourseOf(GetWorld()))
	{
		if (Phase == EPhase::Ready && !Camera.bFreeView && !OnGreen() && !bMini)
		{
			const FVector Bw = World(Ball);
			const FVector Dir(FMath::Sin(Heading), -FMath::Cos(Heading), 0);
			SeeRC->SeeThrough(Camera.CamPos, Bw + Dir * 4000, 400);
		}
		else SeeRC->SeeThrough(FVector::ZeroVector, FVector::ZeroVector, 0);
	}
	// (the tracer fading out once the ball is down: UpdateTracer)
	if (TracerLandedAt >= 0)
	{
		const double F = 1.0 - (RoundTime - TracerLandedAt) / TracerFadeS;
		TracerMesh->SetFade(float(FMath::Max(F, 0.0)), Camera.CamPos);
		if (F <= 0) TracerLandedAt = -1;
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
		// (a fast shot corrected in flight: the ball eases from where it was onto the corrected path)
		if (BlendT0 >= 0)
		{
			const double K = 1 - (AnimT - BlendT0) / 0.6;
			if (K > 0) P = otg::V3{ P.x + BlendOffset.x * K, P.y + BlendOffset.y * K, P.z + BlendOffset.z * K };
			else BlendT0 = -1;
		}
		PlaceBall(P);
		UpdateTracer(N, World(P));
		if (Res.hasCarryPoint && AnimT >= Res.carryPoint.t && !bCarryShown)
		{
			bCarryShown = true;
			HudView.ShowShotData(false);
		}
		// the camera: behind the ball as it sets off, then following it
		const FVector B = BallMesh->GetComponentLocation();
		const FVector Flat(B.X - Camera.ShotFrom.X, B.Y - Camera.ShotFrom.Y, 0);
		const bool bPutt = LastShot.VLA < 6 && Undo && Undo->Lie == otg::Surface::Green;
		if (bMini)
		{
			// (the putt-putt: up behind the ball, looking down the hole, following it round)
			const FVector Pw = World(Pin());
			const FVector Dh = FVector(Pw.X - Camera.ShotFrom.X, Pw.Y - Camera.ShotFrom.Y, 0).GetSafeNormal();
			Camera.CamGoal = B - Dh * 330 + FVector(0, 0, 300);
			Camera.CamSpeed = 2.f;
			Camera.CamLook = B;
			Camera.CamLookNow = B;
		}
		else if (bPutt)
		{
			if (Flat.Size() > 300)
			{
				const FVector U = Flat.GetSafeNormal();
				Camera.CamGoal = B - U * 450 + FVector(0, 0, FMath::Max(B.Z, Camera.ShotFrom.Z) - B.Z + 160);
				Camera.CamSpeed = 3.f;
			}
			Camera.CamLook = B;
			Camera.CamLookNow = B;
		}
		else
		{
			// A full shot: the camera stays where it was, turned to watch the ball climb; once the ball is a
			// little way down from its top (or has landed, or is getting far off) it goes after it - 14 m behind
			// it along its path and 4.5 m over it (or over where it was hit from) - and from its way down to
			// where it stops, turned toward the pin as far as it can with the ball still well in the picture,
			// so the flag is in view as it lands and rolls
			Camera.FlightTopZ = FMath::Max(Camera.FlightTopZ, B.Z);
			const bool bLanded = Res.hasCarryPoint && AnimT >= Res.carryPoint.t;
			const FVector Land = Res.hasCarryPoint ? World(otg::V3{ Res.carryPoint.x, Res.carryPoint.y, Res.carryPoint.z }) : World(Res.rest);
			// (how far past the apex: 0 at the top, 1 at the landing - and on the ground)
			const double Top = Camera.FlightTopZ, Down = FMath::Max(1.0, Top - Land.Z);
			const double Descent = bLanded ? 1.0 : B.Z < Top - 50 ? FMath::Clamp((Top - B.Z) / Down, 0.0, 1.0) : 0.0;
			// (the way it's going along its path, level, eased - so the camera swings round a curving shot)
			if (N > 0)
			{
				const FVector Prev = World(otg::V3{ Pts[N - 1].x, Pts[N - 1].y, Pts[N - 1].z });
				const FVector V(B.X - Prev.X, B.Y - Prev.Y, 0);
				if (V.SizeSquared() > 1.0)
				{
					const FVector Vd = V.GetSafeNormal();
					Camera.ChaseDir = Camera.ChaseDir.IsNearlyZero() ? Vd : (Camera.ChaseDir + (Vd - Camera.ChaseDir) * (1.0 - FMath::Exp(-Dt * 2.0))).GetSafeNormal();
				}
			}
			if (Camera.ChaseDir.IsNearlyZero() && Flat.Size() > 1) Camera.ChaseDir = Flat.GetSafeNormal();
			if (!Camera.bChasing && (Descent > 0.2 || bLanded || FVector::Dist(Camera.CamPos, B) > 18000.0))
			{
				Camera.bChasing = true;
				Camera.CamSpeed = 1.6f;   // (catching up: a swoop over a second or two)
			}
			if (!Camera.bChasing) Camera.CamGoal = Camera.CamPos;   // (staying put, watching it go)
			else if (!Camera.ChaseDir.IsNearlyZero())
			{
				Camera.CamGoal = B - Camera.ChaseDir * 1400 + FVector(0, 0, FMath::Max(B.Z, Camera.ShotFrom.Z) - B.Z + 450);
				Camera.CamSpeed = FMath::Min(3.f, Camera.CamSpeed + float(Dt) * 0.8f);   // (closing in, then keeping up)
			}
			// (the turn toward the pin eased in over about a second: the camera itself stays locked on the ball)
			const double Want = 0.5 * FMath::SmoothStep(0.0, 1.0, FMath::Min(1.0, Descent * 2.5));
			Camera.PinLean += (Want - Camera.PinLean) * (1.0 - FMath::Exp(-Dt * 3.0));
			FVector Look = B;
			if (Camera.PinLean > 1e-3)
			{
				const FVector Eye = Camera.CamPos;
				const FVector ToBall = (B - Eye).GetSafeNormal(), ToPin = (World(Pin()) + FVector(0, 0, 150) - Eye).GetSafeNormal();
				const double Apart = FMath::Acos(FMath::Clamp(FVector::DotProduct(ToBall, ToPin), -1.0, 1.0));
				// (half way between them; less where they're far apart, so the ball stays 13 degrees or less from the middle)
				double A = Camera.PinLean;
				if (Apart > 1e-3) A = FMath::Min(A, FMath::DegreesToRadians(13.0) / Apart);
				if (Apart < FMath::DegreesToRadians(150.0))
					Look = Eye + FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(ToBall, ToPin), A).RotateVector(ToBall) * FVector::Dist(Eye, B);
			}
			Camera.CamLook = Look;
			Camera.CamLookNow = Look;
		}
		// (online: another computer's shot - where it finished comes from there)
		if (N >= int32(Pts.size()) && !Online.HoldFinish()) FinishShot();
	}
	else if (Phase == EPhase::Resting)
	{
		PhaseTimer -= Dt;
		if (PhaseTimer <= 0) { if (bAutoPuttDue) AutoPutt(); else if (bRange) RangeMode.NewBall(); else if (!Turns.RoundAfterShot()) NextShot(); }
	}
	else if (Phase == EPhase::Between)
	{
		PhaseTimer -= Dt;
		// (everyone at once online: holed out, waiting for the others - then the hole's done)
		if (Online.bWaiting)
		{
			if (Online.AllDone())
			{
				Online.bWaiting = false;
				EveryoneIn(TEXT("Everyone's in"), FString::Printf(TEXT("Hole %d"), Hole().Number));
			}
		}
		else if (PhaseTimer <= 0) Turns.BeginTurn(Turns.NextSide, false);
	}
	else if (Phase == EPhase::Holed)
	{
		PhaseTimer -= Dt;
		if (bScorecardAuto && PhaseTimer <= 0)
		{
			bScorecardAuto = false;
			// (the player chose to go straight on: no card - the last hole's still shows, the round's done)
			if (otg::prefs::SkipScorecard()) NextHole();
			else
			{
				PhaseTimer = 10.0; // (then the next hole, or Enter sooner)
				HudView.ShowScorecard(true);
			}
		}
		else if (!bScorecardAuto && PhaseTimer <= 0) NextHole();
	}
	if (Camera.OverviewLeft > 0)
	{
		Camera.OverviewLeft -= Dt;
		if (Camera.OverviewLeft <= 0 && Phase == EPhase::Ready) Camera.AddressView(false);
	}
	// (a look setting changed: again)
	{
		if (Scene.LookStampNow() != Scene.LookStamp) { Scene.ApplyLook(); Scene.ApplyWeatherToScene(); }
	}
	if (!Camera.bFreeView) Camera.UpdateCamera(Dt);   // (the free view moves it itself: FreeFly)
	Flag.UpdateFlag(Dt);
	if (RangeMode.RangeProps)
	{
		double Mps, FromDeg;
		HoleWind(Mps, FromDeg);
		const double F = FMath::DegreesToRadians(FromDeg);
		RangeMode.RangeProps->SetWind(Mps > 0.05 ? FVector(-FMath::Sin(F), FMath::Cos(F), 0) : FVector(1, 0, 0), Mps / 0.44704 / 16.0);
	}
	HudView.UpdateMap();
	if (!Online.FollowsWeather()) Changing.UpdateChanging(Dt);   // (online: the host's weather, as it changes)
	Online.Tick(Dt);
	RainFx.UpdateRain(Dt);
	RainFx.UpdateWet(Dt);
	Scene.UpdateStorm(Dt);
	// the ball a little bigger the further off it is while it flies, so it can be followed against the
	// sky; on the ground, its true size (blown up four times at 30 m it looked like a beach ball by the pin)
	const float CamDist = FVector::Dist(Camera.CamPos, BallMesh->GetComponentLocation());
	const float K = Phase == EPhase::Flying ? FMath::Clamp(CamDist / 2500.f, 1.f, 6.f) : 1.f;
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
	// (online: the round finished, back to the game's lobby; left before the end, out of the game)
	if (Online.bOn)
		if (UOTGNetSubsystem* N = UOTGNetSubsystem::Get(this))
		{
			if (Phase == EPhase::Done) N->RoundOver();
			else N->Leave(TEXT("You left the round"));
		}
	UGameplayStatics::OpenLevel(this, TEXT("/Engine/Maps/Entry"));
}

// What the ball can hit: the ground (with the bridges' decks), the trees' trunks, the buildings and
// cars, and the bridges' rails
void AOTGRound::BuildCollider()
{
	if (!Data) return;
	std::vector<otg::Solid> Solids;
	for (const FOTGSolid& S : Data->Solids)
	{
		otg::Solid O;
		O.h = S.Height;
		O.kind = TCHAR_TO_UTF8(*S.Kind);
		for (const FVector2D& Q : S.Footprint) O.pts.emplace_back(Q.X, Q.Y);
		Solids.push_back(O);
	}
	for (const FOTGBridge& B : Data->Bridges.Bridges)
	{
		TArray<FOTGBridge::FRail> Rails;
		B.Rails(Rails);
		for (const FOTGBridge::FRail& R : Rails)
		{
			// (the collider stands a solid half a metre into the ground under it - here the deck)
			otg::Solid O;
			O.h = R.Top - R.Bottom + 0.5;
			O.kind = "rail";
			for (const FVector2D& Q : R.Pts) O.pts.emplace_back(Q.X, Q.Y);
			Solids.push_back(O);
		}
	}
	Collider.Build(Course, Trees.all, Solids);
}

void AOTGRound::RefreshBridges()
{
	if (!Data) return;
	Data->FillBridges(Course);
	BuildCollider();
	UE_LOG(LogOTGRound, Display, TEXT("The ball's bridges: %d (%d things to hit)"), Data->Bridges.Bridges.Num(), int32(Collider.Count()));
	for (const FOTGBridge& B : Data->Bridges.Bridges)
	{
		// (what the ball finds halfway across: the deck, a path - not the water under it)
		const double X = (B.AX + B.BX) / 2, Z = (B.AZ + B.BZ) / 2;
		UE_LOG(LogOTGRound, Display, TEXT("  bridge middle %.1f %.1f: ball's ground %.2f m (the land %.2f), %s"), X, Z, Course.HeightAt(X, Z), Course.GroundAt(X, Z), UTF8_TO_TCHAR(otg::SurfaceName(Course.SurfaceAt(X, Z))));
	}
}
