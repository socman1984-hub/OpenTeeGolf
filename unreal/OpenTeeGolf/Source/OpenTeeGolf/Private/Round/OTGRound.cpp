// See OTGRound.h. The golf follows public/js/main.js (startHole, resetAim, autoClub, onShot, tick,
// finishShot, pathRelief, waterDrop, holeOut, nextHole, mulligan).

#include "Round/OTGRound.h"
#include "World/OTGRuntimeCourse.h"
#include "App/OTGHelperSubsystem.h"
#include "Round/OTGMiniGolf.h"

#include "Camera/CameraActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Texture2D.h"
#include "UI/SOTGMinimap.h"
#include "UI/SOTGPanels.h"
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
#include "UI/SOTGHud.h"
#include "UI/SOTGRange.h"
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
// the weather's sky, at full cloud (a clear sky: the level's own)
static TAutoConsoleVariable<float> CVarWxCover(TEXT("OTG.Weather.Cover"), 0.7f, TEXT("Cloud coverage added at full cloud (the cloud material's offset)"));
static TAutoConsoleVariable<float> CVarWxDensity(TEXT("OTG.Weather.Density"), 4.f, TEXT("Cloud density at full cloud (x the level's)"));
static TAutoConsoleVariable<float> CVarWxGrey(TEXT("OTG.Weather.Grey"), 0.85f, TEXT("How grey an overcast sky goes (0: blue as ever, 1: all grey)"));
static TAutoConsoleVariable<float> CVarWxSun(TEXT("OTG.Weather.Sun"), 0.06f, TEXT("Sunlight left under full cloud (x a clear day's)"));
static TAutoConsoleVariable<float> CVarWxMaskAt(TEXT("OTG.Weather.MaskAt"), 0.75f, TEXT("Cloud from which the clouds cover the whole sky (not the cloud material's patches)"));
static TAutoConsoleVariable<float> CVarWxRainDark(TEXT("OTG.Weather.RainDark"), 0.6f, TEXT("How dark the clouds go in rain (a storm: all the way)"));
static TAutoConsoleVariable<int32> CVarWxOwnClouds(TEXT("OTG.Weather.OwnClouds"), 1, TEXT("1: the game's own clouds (M_Clouds); 0: the engine's sample clouds"));
static TAutoConsoleVariable<float> CVarWxCloudFill(TEXT("OTG.Weather.CloudFill"), 0.f, TEXT("A test of the clouds: 1 the noise as it comes, 2 the layer solid"));
static TAutoConsoleVariable<int32> CVarWxCloudLayer(TEXT("OTG.Weather.CloudLayer"), 1, TEXT("1: our clouds' layer 1.5 km up, 3 km thick; 0: the level's own"));
static TAutoConsoleVariable<float> CVarWxSunOwn(TEXT("OTG.Weather.SunOwn"), 0.4f, TEXT("Sunlight left under full cloud with our own clouds (they shade the ground themselves)"));
static TAutoConsoleVariable<float> CVarWxSoft(TEXT("OTG.Weather.Soft"), 20.f, TEXT("The sun's size under full cloud (degrees: soft, faint shadows)"));

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
	FAutoConsoleCommandWithWorldAndArgs SurfTestCmd(TEXT("OTG.Round.SurfaceTest"), TEXT("(testing) shots fanned out from the ball: carry, roll and bounce by the surface each lands on, in the log"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) { if (AOTGRound* R = RoundIn(W)) R->SurfaceTest(); }));
	FAutoConsoleCommandWithWorldAndArgs CastCmd(TEXT("OTG.Round.CastTest"), TEXT("(testing) what a ball meets flying at the flagstick and at the nearest trunk"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) { if (AOTGRound* R = RoundIn(W)) R->CastTest(); }));
	FAutoConsoleCommandWithWorldAndArgs PanelCmd(TEXT("OTG.Round.ShotPanel"), TEXT("The test shot panel (the T key)"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) { if (AOTGRound* R = RoundIn(W)) R->ToggleShotPanel(); }));
	FAutoConsoleCommandWithWorldAndArgs ShotDataCmd(TEXT("OTG.Round.ShotData"), TEXT("Which shot numbers show, and where (the D key)"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) { if (AOTGRound* R = RoundIn(W)) R->ToggleShotSettings(); }));
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
	// (the tree designer's changes, for testing: OTG.Trees.Set <species|all|rocks> model|size|density|on <value>,
	// OTG.Trees.Save, OTG.Trees.Show <species>, OTG.Trees.List)
	AOTGRuntimeCourse* CourseOf(UWorld* W) { if (W) for (TActorIterator<AOTGRuntimeCourse> It(W); It; ++It) return *It; return nullptr; }
	FAutoConsoleCommandWithWorldAndArgs TreesSetCmd(TEXT("OTG.Trees.Set"), TEXT("OTG.Trees.Set <species|all|rocks> model|size|density|on <value>"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		AOTGRuntimeCourse* C = CourseOf(W);
		if (!C || A.Num() < 3) return;
		FOTGTreeDesign D = C->TreeDesign();
		const FString F = A[1].ToLower();
		const float V = FCString::Atof(*A[2]);
		if (A[0] == TEXT("all")) { if (F == TEXT("size")) D.AllSize = V; else if (F == TEXT("density")) D.AllDensity = V; }
		else if (A[0] == TEXT("rocks")) { if (F == TEXT("size")) D.RockSize = V; else if (F == TEXT("density")) D.RockDensity = V; else if (F == TEXT("on")) D.bRocks = V != 0; else if (F == TEXT("model")) D.RockModel = FOTGTreeDesign::RockModels().Contains(A[2]) ? A[2] : FString(); }
		else
		{
			FOTGSpeciesLook& L = D.Species.FindOrAdd(A[0]);
			if (F == TEXT("model") && FOTGTreeDesign::Models().Contains(A[2])) L.Model = A[2];
			else if (F == TEXT("size")) L.Size = V;
			else if (F == TEXT("density")) L.Density = V;
		}
		C->SetTreeDesign(D);
		if (AOTGRound* R = RoundIn(W)) R->RefreshTrees();
	}));
	FAutoConsoleCommandWithWorldAndArgs TreesSaveCmd(TEXT("OTG.Trees.Save"), TEXT("Save the course's trees and rocks as they are now (trees_design.json)"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) {
		FString Where;
		// (saved first: a Shipping build leaves out the log, its arguments and all)
		if (AOTGRuntimeCourse* C = CourseOf(W)) { const bool bOk = C->SaveTreeDesign(&Where); UE_LOG(LogTemp, Display, TEXT("Trees %s %s"), bOk ? TEXT("saved to") : TEXT("NOT saved to"), *Where); }
	}));
	FAutoConsoleCommandWithWorldAndArgs TurfSetCmd(TEXT("OTG.Turf.Set"), TEXT("OTG.Turf.Set fairway|green hue|sat|bright <value>: (testing) the mown turf's colour, as the grass designer turns it"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		AOTGRuntimeCourse* C = CourseOf(W);
		if (!C || A.Num() < 3) return;
		FOTGTurfLook L = C->TurfLook(A[0]);
		const float V = FCString::Atof(*A[2]);
		if (A[1] == TEXT("hue")) L.Hue = V; else if (A[1] == TEXT("sat")) L.Sat = V; else if (A[1] == TEXT("bright")) L.Bright = V;
		C->SetTurfLook(A[0], L);
	}));
	// (the turf's colour checked: unreal/tests/turf_colours.ps1 photographs each cut so - the free view
	// 5 m from a spot well inside the surface, 2.5 m up, looking at it the way the camera already faced)
	FAutoConsoleCommandWithWorldAndArgs SwatchCmd(TEXT("OTG.Look.Swatch"), TEXT("OTG.Look.Swatch <fairway|green|fringe|rough|native|tee> [distance cm] [height cm] [metres in]: (testing) the free view on a patch of that surface"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		AOTGRuntimeCourse* C = CourseOf(W);
		AOTGRound* R = RoundIn(W);
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!C || !R || !A.Num() || !PC || !PC->PlayerCameraManager) return;
		const FVector From = PC->PlayerCameraManager->GetCameraLocation();
		FVector At;
		auto Arg = [&](int32 i, double D) { return A.IsValidIndex(i) ? FCString::Atod(*A[i]) : D; };
		// (as far in as it goes: a collar is only a metre or so wide)
		bool bFound = false;
		for (const double In : { Arg(3, 3), 1.5, 0.6, 0.3, 0.05 })
			if ((bFound = C->FindSurface(A[0], From, At, In))) break;
		if (!bFound) { UE_LOG(LogTemp, Warning, TEXT("Swatch %s: none near here"), *A[0]); return; }
		const double Yaw = (At - From).Rotation().Yaw;
		R->FreeLookAt(At, Arg(1, 500), Arg(2, 250), Yaw);
		UE_LOG(LogTemp, Display, TEXT("Swatch %s at %s"), *A[0], *At.ToString());
	}));
	// (the ground's turf textures made from the grass itself: OTGTurfBake.cpp. Bring the camera near
	// first - OTG.Look.Swatch - so the grass there is drawn)
	FAutoConsoleCommandWithWorldAndArgs BakeCmd(TEXT("OTG.Look.Bake"), TEXT("OTG.Look.Bake <fairway|green|fringe|rough|native|tee> <size cm> <name>: (making textures) the grass on a patch of that surface, from above, into Saved/TurfBake"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		AOTGRuntimeCourse* C = CourseOf(W);
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!C || A.Num() < 3 || !PC || !PC->PlayerCameraManager) return;
		C->BakeTurf(A[0], FCString::Atod(*A[1]), A[2], PC->PlayerCameraManager->GetCameraLocation());
	}));
	FAutoConsoleCommandWithWorldAndArgs TreesShowCmd(TEXT("OTG.Trees.Show"), TEXT("OTG.Trees.Show <species>: the free view flown to one"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		if (AOTGRound* R = RoundIn(W)) if (A.Num()) R->ShowTree(A[0]);
	}));
	FAutoConsoleCommandWithWorldAndArgs TreesListCmd(TEXT("OTG.Trees.List"), TEXT("The species on the course, and the trees and rocks drawn"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) {
		AOTGRuntimeCourse* C = CourseOf(W);
		if (!C) return;
		for (const TPair<FString, int32>& S : C->TreeSpecies())
		{
			const FOTGSpeciesLook L = C->TreeDesign().For(S.Key);
			UE_LOG(LogTemp, Display, TEXT("%-12s %5d  %s size %.2f density %.2f"), *S.Key, S.Value, *L.Model, L.Size, L.Density);
		}
		UE_LOG(LogTemp, Display, TEXT("Drawn: %d trees, %d rocks"), C->TreesDrawn(), C->RocksDrawn());
	}));
	// (testing: a command a while later - the screenshot's commands all run at once, and a click
	// after "Show me" needs the camera to have got there: OTG.Later 2 OTG.Trees.Click 0.5 0.5)
	FAutoConsoleCommandWithWorldAndArgs LaterCmd(TEXT("OTG.Later"), TEXT("OTG.Later <seconds> <command ...>: (testing) a console command run that much later"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		if (!W || A.Num() < 2) return;
		const FString Cmd = FString::Join(TArrayView<const FString>(A).RightChop(1), TEXT(" "));
		FTimerHandle H;
		TWeakObjectPtr<UWorld> WW(W);
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Cmd]() {
			if (APlayerController* PC = WW.IsValid() ? WW->GetFirstPlayerController() : nullptr) PC->ConsoleCommand(Cmd);
		}), FMath::Max(0.01f, FCString::Atof(*A[0])), false);
	}));
	FAutoConsoleCommandWithWorldAndArgs TreesEditCmd(TEXT("OTG.Trees.Edit"), TEXT("OTG.Trees.Edit size|height|hue|bright|kind|model|out|reset [value]: (testing) the picked tree or rock changed"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		if (AOTGRound* R = RoundIn(W); R && A.Num()) R->EditPicked(A[0], A.Num() > 1 ? A[1] : FString());
	}));
	FAutoConsoleCommandWithWorldAndArgs GrassShowCmd(TEXT("OTG.Grass.Show"), TEXT("OTG.Grass.Show <kind>: the free view flown to where it grows"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		if (AOTGRound* R = RoundIn(W)) if (A.Num()) R->ShowGrass(A[0]);
	}));
	FAutoConsoleCommandWithWorldAndArgs DesignClickCmd(TEXT("OTG.Trees.Click"), TEXT("OTG.Trees.Click <0..1 across> <0..1 down> [pick|tree|rock|out]: (testing) a click on the course in the designer's trees tab"),
		FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
			AOTGRound* R = RoundIn(W);
			if (!R || A.Num() < 2) return;
			if (A.Num() > 2) R->SetDesignTool(A[2] == TEXT("tree") ? 1 : A[2] == TEXT("rock") ? 2 : A[2] == TEXT("out") ? 3 : 0);
			R->DesignClickAt(FCString::Atod(*A[0]), FCString::Atod(*A[1]));
		}));
	FAutoConsoleCommandWithWorldAndArgs GrassCmd(TEXT("OTG.Round.GrassDesigner"), TEXT("The course designer (the R key; OTG.Round.GrassDesigner trees: on its trees tab)"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) { if (AOTGRound* R = RoundIn(W)) R->ToggleGrassDesigner(A.Num() && A[0] == TEXT("trees") ? 1 : -1); }));
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
	FAutoConsoleCommandWithWorldAndArgs NewBallCmd(TEXT("OTG.Range.NewBall"), TEXT("The driving range: a new ball on the mat now (Enter / Backspace)"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) { if (AOTGRound* R = RoundIn(W)) R->NewBall(); }));
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
	// (the lie's dot: the surface's colour)
	FLinearColor LieColour(otg::Surface S)
	{
		switch (S)
		{
		case otg::Surface::Tee: case otg::Surface::Fairway: return FLinearColor(0.35f, 0.8f, 0.3f);
		case otg::Surface::FirstCut: case otg::Surface::Fringe: return FLinearColor(0.45f, 0.72f, 0.3f);
		case otg::Surface::Green: return FLinearColor(0.5f, 0.95f, 0.45f);
		case otg::Surface::Rough: return FLinearColor(0.22f, 0.5f, 0.18f);
		case otg::Surface::DeepRough: case otg::Surface::Native: return FLinearColor(0.62f, 0.55f, 0.25f);
		case otg::Surface::Path: return FLinearColor(0.7f, 0.7f, 0.68f);
		case otg::Surface::Sand: return FLinearColor(0.93f, 0.82f, 0.55f);
		case otg::Surface::Water: return FLinearColor(0.3f, 0.55f, 0.95f);
		default: return FLinearColor(0.9f, 0.3f, 0.3f);
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
	// a golf tee: a thin peg out of the ground, a cup on top for the ball
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	TeeMesh = MakeMesh(TEXT("TeePeg"), Cylinder.Object, false);
	TeeHeadMesh = MakeMesh(TEXT("TeeHead"), Cone.Object, false);
	// (the aim: where the shot's aimed - this just holds the spot; a yardage stick stands there)
	AimMesh->SetHiddenInGame(true);
	AimStick = MakeMesh(TEXT("AimStick"), Cylinder.Object, true);
	AimStickTop = MakeMesh(TEXT("AimStickTop"), Cylinder.Object, false);
	CupMesh = MakeMesh(TEXT("Cup"), Cylinder.Object, false);
	GridMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SlopeGrid"));
	GridMesh->SetupAttachment(RootComponent);
	GridMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GridMesh->SetCastShadow(false);
	GridMesh->bVisibleInRayTracing = false;
	GridDots = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("SlopeDots"));
	GridDots->SetupAttachment(RootComponent);
	GridDots->SetStaticMesh(Sphere.Object);
	GridDots->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GridDots->SetCastShadow(false);
	GridDots->bVisibleInRayTracing = false;
	GridDots->SetMobility(EComponentMobility::Movable);
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
		bGridOn = false;
		FlagTopCm = 105;
	}
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
	Mat(TeeHeadMesh, FLinearColor(0.9f, 0.9f, 0.9f));
	Mat(CupMesh, FLinearColor(0.01f, 0.012f, 0.01f));
	TracerMat = Mat(nullptr, FLinearColor(1.f, 0.72f, 0.2f));
	Mat(AimStick, FLinearColor(0.92f, 0.92f, 0.9f));
	Mat(AimStickTop, FLinearColor(1.f, 0.78f, 0.1f));
	// (the slope grid's colours: flat white, then yellow, orange and red at 3% and over - as the web
	// version's; and its dots white)
	for (const FLinearColor& C : { FLinearColor(0.95f, 0.95f, 0.92f), FLinearColor(1.f, 0.9f, 0.2f), FLinearColor(1.f, 0.55f, 0.12f), FLinearColor(0.95f, 0.18f, 0.1f) })
		GridMats.Add(Mat(nullptr, C));
	GridDots->SetMaterial(0, Mat(nullptr, FLinearColor(1.f, 1.f, 1.f)));
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
			RangePanel = SNew(SOTGRangePanel);
			Hud->SetPractice(RangePanel.ToSharedRef(), TEXT("Click the ground to aim     Left / Right: aim     Up / Down: club     T: test shot panel     Space: quick test shot     Enter / Backspace: new ball     Tab: hide the numbers     D: shot data     F: free view     L: weather and time of day     P: frame rate     Esc: menu"));
		}
		// (the course's name over the hole's)
		else Hud->SetCourseName(bMini ? FString(TEXT("Putt-putt")) : !Data->CourseName.IsEmpty() ? Data->CourseName : Data->CourseId.Replace(TEXT("_"), TEXT(" ")));
	}
	// the driving range's mats, flags and distance boards, and a mark where each of the club's last
	// shots stopped
	if (bRange)
	{
		RangeProps = GetWorld()->SpawnActor<AOTGRangeProps>();
		if (RangeProps) RangeProps->Build(Data);
		RangeDots = NewObject<UInstancedStaticMeshComponent>(this);
		RangeDots->SetStaticMesh(CupMesh->GetStaticMesh());
		RangeDots->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RangeDots->SetCastShadow(false);
		RangeDots->SetMobility(EComponentMobility::Movable);
		RangeDots->SetupAttachment(RootComponent);
		RangeDots->RegisterComponent();
		RangeDotMat = Mat(RangeDots, FLinearColor(1.f, 0.7f, 0.1f));
	}
	UMaterialInterface* Rain = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenTeeGolf/Game/M_Rain.M_Rain"));
	if (Rain)
	{
		RainMat = UMaterialInstanceDynamic::Create(Rain, this);
		RainMesh->SetMaterial(0, RainMat);
	}
	else UE_LOG(LogOTGRound, Warning, TEXT("No rain material (unreal/make_assets.ps1 makes it)"));

	// Shots
	if (UOTGLaunchMonitorSubsystem* LM = GetGameInstance()->GetSubsystem<UOTGLaunchMonitorSubsystem>())
		ShotHandle = LM->OnShotNative.AddUObject(this, &AOTGRound::OnShot);

	// Courses built in the background (the New course page's queue): told here when one is ready
	if (UOTGHelperSubsystem* HS = GetGameInstance()->GetSubsystem<UOTGHelperSubsystem>())
		HS->OnJobDone.AddWeakLambda(this, [this](const FOTGHelperJob& J) {
			if (J.Name.IsEmpty()) return;
			if (J.State == TEXT("done") && J.Kind == TEXT("export")) Toast(J.Name + TEXT(" is ready: play it from the menu"), 6.f);
			else if (J.State == TEXT("done") && J.bNeedsReview) Toast(J.Name + TEXT(" is built: its holes need looking over in the editor"), 6.f);
			else if (J.State == TEXT("failed")) Toast(J.Name + TEXT(": building it didn't work (see New course)"), 6.f);
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
	ResetAim(false);
	PlaceBall(Ball);
	OverviewView();
	double LenM = Dist(Ball, P);
	if (bMini && H.Path.Num() > 1)
	{
		LenM = 0;
		for (int32 i = 1; i < H.Path.Num(); i++) LenM += FVector2D::Distance(FVector2D(H.Path[i - 1].X, H.Path[i - 1].Z), FVector2D(H.Path[i].X, H.Path[i].Z));
	}
	if (bRange)
	{
		Banner(TEXT("Driving range"), TEXT("Targets at 50, 100, 150, 200 and 250 yards"), 3.f);
		UpdateRange();
	}
	else Banner(H.Name.IsEmpty() ? FString::Printf(TEXT("Hole %d"), H.Number) : FString::Printf(TEXT("Hole %d  ·  %s"), H.Number, *H.Name), FString::Printf(TEXT("Par %d  ·  %s%s"), H.Par, *HoleLen(LenM),
		Game->Single() ? TEXT("") : *FString::Printf(TEXT("  ·  %s to play"), *Game->SideLabel(SideIdx))), 3.f);
	UpdateHudInfo();
	TellLaunchMonitor(true);
	UE_LOG(LogOTGRound, Display, TEXT("Hole %d, par %d, %s"), H.Number, H.Par, *HoleLen(LenM));
}

void AOTGRound::ResetAim(bool bToPin)
{
	// (the range: the aim stays where the player turned it, at the club's carry)
	if (bRange)
	{
		AimDist = ClubCarryM();
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
	UpdateHudInfo();
	TellLaunchMonitor(Phase == EPhase::Ready);
}

void AOTGRound::TellLaunchMonitor(bool bTurn)
{
	if (!Data) return;
	if (UOTGLaunchMonitorSubsystem* LM = GetGameInstance()->GetSubsystem<UOTGLaunchMonitorSubsystem>())
	{
		const bool bLeft = Game && Game->Sides.IsValidIndex(SideIdx) ? Game->Players[Game->Hitter(Game->Sides[SideIdx])].bLeftHanded : false;
		LM->SetPlayer(Club, (bRange ? AimDist : DistToPin()) * M_TO_YD, bLeft);
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
	if (bRange && Phase == EPhase::Resting) NewBall();   // (the range: a new ball for the new club)
	if (Phase != EPhase::Ready) return;
	if (bMini) { Toast(TEXT("Putt-putt: putter only"), 1.2f); return; }
	const int32 I = FMath::Clamp(ClubIndex(Club) - Step, 0, NumClubs - (bRange ? 2 : 1));   // (no putter on the range)
	bClubPicked = true;
	SetClub(Clubs[I].Code);
	Toast(Clubs[I].Name, 1.2f);
	if (bRange)
	{
		AimDist = ClubCarryM();
		ShowAim();
		UpdateRange();
	}
}

void AOTGRound::TestShot()
{
	// A strike as a launch monitor would send it: the club's typical one, or for a wedge (or a putt)
	// one sized to the distance, a little off line (only on the player's turn: it's made for their
	// club and distance)
	if (bRange && Phase == EPhase::Resting && !bPaused) NewBall();   // (the range: a new ball, and hit it)
	if (Phase != EPhase::Ready || bPaused)
	{
		Toast(TEXT("Test shot: wait for your turn"), 1.5f);
		return;
	}
	const FClub& C = Clubs[ClubIndex(Club)];
	FOTGBallData B;
	const double D = bRange ? AimDist : DistToPin();
	if (Club == TEXT("PT"))
	{
		const double Mps = PuttMps(0);
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
	else if (bRange && Phase == EPhase::Resting) NewBall();
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

void AOTGRound::SurfaceTest()
{
	// Each club's typical strike, still air, fanned 60 degrees either side of the aim and in 4 lengths
	// (90-105% speed), from the ball: carry, roll on and first bounce, grouped by where each lands
	struct FRow { int32 N = 0; double Carry = 0, Roll = 0, Hop = 0, RollMax = 0; };
	const TCHAR* Codes[] = { TEXT("DR"), TEXT("H4"), TEXT("I7"), TEXT("PW") };
	const otg::Surface Order[] = { otg::Surface::Green, otg::Surface::Fringe, otg::Surface::Fairway, otg::Surface::Tee, otg::Surface::FirstCut, otg::Surface::Rough,
		otg::Surface::DeepRough, otg::Surface::Native, otg::Surface::Sand, otg::Surface::Path, otg::Surface::Water };
	otg::ShotOptions O;
	O.altitudeM = Data->AltitudeM;
	O.tempC = Weather.TempC; O.humidity = Weather.Humidity; O.pressureHpa = Weather.PressureHpa; O.wet = Weather.Wetness();
	O.stimp = Data->DefaultStimp;
	O.trees = &Trees;
	O.collider = &Collider;
	UE_LOG(LogOTGRound, Display, TEXT("SurfaceTest from %s (wet %.2f): yards, roll = rest minus landing, hop = first bounce in m"), *SurfaceDisplay(Lie), O.wet);
	for (const TCHAR* Code : Codes)
	{
		const FClub& C = Clubs[ClubIndex(Code)];
		TMap<uint8, FRow> Rows;
		for (int32 A = -40; A <= 40; A++)
			for (int32 L = 0; L < 4; L++)
			{
				otg::Shot S;
				S.speedMph = C.Mph * (0.9 + 0.05 * L); S.vla = C.Vla; S.hla = 0; S.backSpin = C.Spin; S.sideSpin = 0;
				O.heading = Heading + A * 1.5 * PI / 180;
				const otg::ShotResult R = otg::SimulateShot(Course, Ball, S, O);
				if (!R.hasCarryPoint) continue;
				const otg::Surface Landed = Course.SurfaceAt(R.carryPoint.x, R.carryPoint.z);
				double Hop = 0;
				for (const otg::PathPoint& P : R.points) if (P.t > R.carryPoint.t) Hop = FMath::Max(Hop, P.y - R.carryPoint.y);
				const double Roll = FMath::Sqrt(FMath::Square(R.rest.x - R.carryPoint.x) + FMath::Square(R.rest.z - R.carryPoint.z)) * M_TO_YD;
				FRow& Row = Rows.FindOrAdd(uint8(Landed));
				Row.N++; Row.Carry += R.carry * M_TO_YD; Row.Roll += Roll; Row.Hop = FMath::Max(Row.Hop, Hop); Row.RollMax = FMath::Max(Row.RollMax, Roll);
			}
		for (otg::Surface Su : Order)
			if (const FRow* Row = Rows.Find(uint8(Su)))
				UE_LOG(LogOTGRound, Display, TEXT("SurfaceTest %-8s landed %-10s n=%3d  carry %5.0f  roll %5.1f (max %5.1f)  hop %4.2f"), C.Name, *SurfaceDisplay(Su), Row->N,
					Row->Carry / Row->N, Row->Roll / Row->N, Row->RollMax, Row->Hop);
	}
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

// Where a line (world space, from the camera through the cursor) first meets the ground: marched
// along in 2 m steps against the course's heights, then halved down to 5 cm
void AOTGRound::AimAlongRay(const FVector& Origin, const FVector& Dir)
{
	FVector Hit;
	if (GroundAlongRay(Origin, Dir, Hit)) AimAtWorld(Hit);
}

bool AOTGRound::GroundAlongRay(const FVector& Origin, const FVector& Dir, FVector& Out) const
{
	if (!Data) return false;
	auto Below = [&](const FVector& W) {
		const FVector C = Data->WorldToCourse(W);
		const double G = Course.HeightAt(C.X, C.Z);
		return FMath::IsFinite(G) && C.Y <= G;
	};
	const FVector D = Dir.GetSafeNormal();
	double A = 0, B = -1;
	for (double T = 200; T < 400000; T += 200)
	{
		if (Below(Origin + D * T)) { B = T; break; }
		A = T;
	}
	if (B < 0) return false;
	for (int32 k = 0; k < 14; k++)
	{
		const double M = (A + B) / 2;
		(Below(Origin + D * M) ? B : A) = M;
	}
	Out = Origin + D * B;
	return true;
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
	if (const AOTGRuntimeCourse* RC = CourseOf(GetWorld()))
		FreePos.Z = FMath::Max(FreePos.Z, RC->WaterTopAt(FreePos) + 60);
	FreeRot = (Target - FreePos).Rotation();
	if (Cam) Cam->SetActorLocationAndRotation(FreePos, FreeRot);
}

void AOTGRound::ToggleShotPanel()
{
	if (Hud) Hud->ShowShotPanel(!Hud->IsShotPanelShown());
}

void AOTGRound::UpdatePinTag()
{
	if (!Hud) return;
	const bool bWant = !bRange && !bMini && (Phase == EPhase::Ready || Phase == EPhase::Resting) && !bPaused;
	APlayerController* PC = bWant ? GetWorld()->GetFirstPlayerController() : nullptr;
	FVector2D Px;
	const otg::V3 P = Pin();
	// (over the flag, about its top: 2.5 m up)
	if (!PC || !PC->ProjectWorldLocationToScreen(World(otg::V3{ P.x, P.y + 2.5, P.z }), Px, true)) { Hud->SetPinTag(false, FVector2D::ZeroVector, FString(), FString(), FLinearColor::White); return; }
	// (the viewport's pixels to the HUD's own units)
	FVector2D Vp(1, 1);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Vp);
	const FVector2D Local = Hud->GetCachedGeometry().GetLocalSize();
	const double K = Local.X > 1 && Vp.X > 1 ? Local.X / Vp.X : 1.0;
	const FVector2D At = Px * K;
	if (At.X < 0 || At.Y < 0 || At.X > Local.X || At.Y > Local.Y) { Hud->SetPinTag(false, FVector2D::ZeroVector, FString(), FString(), FLinearColor::White); return; }
	const double D = DistToPin();
	const double Up = (P.y - Ball.y) * M_TO_FT;   // (the hole above the ball +, below -)
	const int32 Ft = FMath::RoundToInt(Up);
	const FString Elev = Ft == 0 ? FString(TEXT("level")) : FString::Printf(TEXT("%d ft %s"), FMath::Abs(Ft), Ft > 0 ? TEXT("uphill") : TEXT("downhill"));
	const FLinearColor C = Ft >= 1 ? FLinearColor(1.f, 0.62f, 0.45f) : Ft <= -1 ? FLinearColor(0.55f, 0.8f, 1.f) : FLinearColor(0.8f, 0.85f, 0.8f);
	Hud->SetPinTag(true, At, OnGreen() ? Putt(D) : Len(D), Elev, C);
}

void AOTGRound::ToggleShotSettings()
{
	if (Hud) Hud->ShowShotSettings(!Hud->IsShotSettingsShown());
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
	UpdateRainFar(Dt, Mm);
	UpdateRipples(Dt);
	if (Mm <= 0.05 || !RainMat || !Cam)
	{
		if (RainVerts) { RainMesh->ClearAllMeshSections(); RainVerts = 0; RainDrops.Reset(); }
		return;
	}
	const int32 N = FMath::Clamp(int32(900 + Mm * 450), 900, 4500);
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
	for (int32 i = 0; i < RainDrops.Num(); i++)
	{
		FVector& D = RainDrops[i];
		// (each drop its own: bigger drops fall faster and streak longer - not a grid of the same lines)
		const float K = float(((uint32(i) * 2654435761u) >> 8) & 0xFFFFu) / 65535.f;
		D += V * (0.8 + 0.4 * K) * Dt;
		FVector R = D - C;
		if (R.Z < -400) { R.Z += Half.Z + 400; R.X = FMath::FRandRange(-Half.X, Half.X); R.Y = FMath::FRandRange(-Half.Y, Half.Y); }
		if (R.X < -Half.X) R.X += 2 * Half.X; else if (R.X > Half.X) R.X -= 2 * Half.X;
		if (R.Y < -Half.Y) R.Y += 2 * Half.Y; else if (R.Y > Half.Y) R.Y -= 2 * Half.Y;
		D = C + R;
		// (none right in front of the lens: a drop there is a blur across the view)
		if ((D - C).SizeSquared() < 450 * 450) D += FVector(0, 0, Half.Z);
		const FVector ToCam = (C - D).GetSafeNormal();
		// (thin; a far one a touch wider so it doesn't flicker to nothing)
		const double Far = FMath::Clamp((D - C).Size() / 2000.0, 0.0, 1.0);
		const FVector Side = FVector::CrossProduct(Dir, ToCam).GetSafeNormal() * FMath::Lerp(0.18, 0.5, Far);
		const FVector Bottom = D + Dir * (40 + 70 * K);
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
	RainMat->SetScalarParameterValue(TEXT("Opacity"), float(FMath::Clamp(0.14 + Mm * 0.03, 0.14, 0.34)));
}

void AOTGRound::ToggleScorecard()
{
	// (the range has no card: Tab hides and shows its numbers)
	if (bRange && RangePanel)
	{
		RangePanel->SetVisibility(RangePanel->GetVisibility() == EVisibility::Collapsed ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
		return;
	}
	ShowScorecard(!bScorecard);
}

void AOTGRound::Mulligan()
{
	if (bRange) return NewBall();
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
	if (bMini)
	{
		// (a putt rolls: no chipping over the boards; and the windmill's sails turn as it goes)
		Play.vla = FMath::Min(Play.vla, 4.0);
		if (AOTGMiniGolf* M = AOTGMiniGolf::In(GetWorld())) M->AddMovers(O);
	}
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
	if (bRange) return RangeShotDone();
	const otg::Surface Surface = Course.SurfaceAt(Res.rest.x, Res.rest.z);
	// (into the water: a splash and rings where it went in)
	if (Res.result == "water" || Surface == otg::Surface::Water) AddRipple(World(Res.rest));
	FString Title, Small;
	if (Res.result == "holed")
	{
		Ball = Res.rest;
		PlaceBall(Ball);
		return HoleOut(false, FString());
	}
	// (putt-putt: anywhere off the felt is out, and the pond's water too: from where it was)
	if (bMini && (Res.result != "rest" || Surface != otg::Surface::Green))
	{
		Strokes++;
		Ball = PrevBall;
		Title = Res.result == "water" || Surface == otg::Surface::Water ? TEXT("In the pond") : TEXT("Off the course");
		Small = TEXT("+1 stroke: play again from where you were");
	}
	else if (Res.result == "oob" || Surface == otg::Surface::Oob || Surface == otg::Surface::None)
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
		if (bMini) { if (Has("blade")) Toast(TEXT("Blocked by the windmill!"), 2.5f); }
		else if (!ReliefNote.IsEmpty()) Toast(FString::Printf(TEXT("%s%s: dropped in the %s"), *Tree, *ReliefNote, *SurfaceDisplay(Lie).ToLower()), 4.f);
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
	UpdateSlopeGrid(Dt);
	// the aim: a yardage stick standing on the spot - white, its top yellow - sized by how far it is
	// from the camera so it reads the same on screen near or far (true size it'd vanish at 250 m)
	{
		const bool bShow = AimMesh && AimMesh->GetVisibleFlag();
		AimStick->SetVisibility(bShow);
		AimStickTop->SetVisibility(bShow);
		if (bShow)
		{
			const FVector At = AimMesh->GetComponentLocation() - FVector(0, 0, 3);
			const double H = FMath::Max(120.0, FVector::Dist(CamPos, At) * 0.03);   // cm
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
	UpdatePinTag();
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
		if (bMini)
		{
			// (the putt-putt: up behind the ball, looking down the hole, following it round)
			const FVector Pw = World(Pin());
			const FVector Dh = FVector(Pw.X - ShotFrom.X, Pw.Y - ShotFrom.Y, 0).GetSafeNormal();
			CamGoal = B - Dh * 330 + FVector(0, 0, 300);
			CamSpeed = 2.f;
		}
		else if (Flat.Size() > 300)
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
		if (PhaseTimer <= 0) { if (bRange) NewBall(); else if (!RoundAfterShot()) NextShot(); }
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
		for (const auto* V : { &CVarLookExposure, &CVarLookSaturation, &CVarLookContrast, &CVarLookTemp, &CVarLookSun, &CVarLookSunWarm, &CVarLookSunPitch, &CVarLookSunSide, &CVarLookHaze, &CVarLookFog, &CVarLookVignette, &CVarLookTreeLift, &CVarLookShadows, &CVarLookSky, &CVarLookSunSoft, &CVarLookSlopeBias, &CVarWxCover, &CVarWxDensity, &CVarWxGrey, &CVarWxSun, &CVarWxSoft, &CVarWxMaskAt, &CVarWxRainDark, &CVarWxCloudFill, &CVarWxSunOwn })
			Stamp = HashCombine(Stamp, GetTypeHash(V->GetValueOnGameThread()));
		Stamp = HashCombine(HashCombine(HashCombine(Stamp, GetTypeHash(CVarWxOwnClouds.GetValueOnGameThread())), GetTypeHash(HoleIndex)), GetTypeHash(SunHour));
		if (Stamp != LookStamp) { ApplyLook(); ApplyWeatherToScene(); }
	}
	if (!bFreeView) UpdateCamera(Dt);   // (the free view moves it itself: FreeFly)
	UpdateFlag(Dt);
	if (RangeProps)
	{
		double Mps, FromDeg;
		HoleWind(Mps, FromDeg);
		const double F = FMath::DegreesToRadians(FromDeg);
		RangeProps->SetWind(Mps > 0.05 ? FVector(-FMath::Sin(F), FMath::Cos(F), 0) : FVector(1, 0, 0), Mps / 0.44704 / 16.0);
	}
	UpdateMap();
	UpdateChanging(Dt);
	UpdateRain(Dt);
	UpdateWet(Dt);
	UpdateStorm(Dt);
	// the ball a little bigger the further off it is while it flies, so it can be followed against the
	// sky; on the ground, its true size (blown up four times at 30 m it looked like a beach ball by the pin)
	const float CamDist = FVector::Dist(CamPos, BallMesh->GetComponentLocation());
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

// ------------------------------------------------------------------ showing it

namespace { constexpr double TeeLiftCm = 3.4; }   // (a teed-up ball's bottom above the ground)

FVector AOTGRound::World(const otg::V3& P) const
{
	return Data->CourseToWorld(FVector(P.x, P.y, P.z));
}

FVector AOTGRound::BallWorld(const otg::V3& P) const
{
	const float K = BallMesh->GetComponentScale().X / 0.0427f;
	// (on the tee spot, before it's hit: sat on the tee)
	const FVector Tee = TeeMesh ? TeeMesh->GetComponentLocation() - FVector(0, 0, 1.5) : FVector::ZeroVector;
	const bool bTeed = TeeMesh && TeeMesh->IsVisible() && FVector::Dist2D(World(P), Tee) < 3.0;
	return World(P) + FVector(0, 0, 2.135 * K + (bTeed ? TeeLiftCm : 0.0));
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
	OrbitCentre = B;
	OrbitGoal = CamGoal;   // (UpdateCamera swings round the ball to it, not across)
	if (bSnap) { CamPos = CamGoal; CamLookNow = CamLook; }
}

void AOTGRound::OverviewView()
{
	if (bMini)
	{
		// (the putt-putt: the whole hole - tee, turn and cup - from above behind the tee)
		const FOTGHole& H = Hole();
		FBox Box(ForceInit);
		for (const FVector& Q : H.Path) Box += World(OnGround(Q.X, Q.Z));
		Box += World(Ball);
		const FVector Mid = Box.GetCenter();
		const double L = FMath::Max(Box.GetSize().X, Box.GetSize().Y);
		FVector D = FVector(Mid.X - World(Ball).X, Mid.Y - World(Ball).Y, 0).GetSafeNormal();
		if (D.IsNearlyZero()) D = FVector(FMath::Sin(Heading), -FMath::Cos(Heading), 0);
		CamPos = CamGoal = Mid - D * (L * 0.55 + 200) + FVector(0, 0, L * 0.7 + 280);
		CamLookNow = CamLook = Mid;
		OverviewLeft = 2.8;
		return;
	}
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
	if (Phase != EPhase::Flying && CamGoal.Equals(OrbitGoal, 1.0))
	{
		// Turning to a new aim behind the ball: round the ball, the shorter way, keeping the
		// camera's height and distance from it (in a straight line, a big turn cut low across the ball)
		auto Swing = [&](FVector& Now, const FVector& Goal) {
			const FVector A = Now - OrbitCentre, G = Goal - OrbitCentre;
			double Yaw = FMath::Atan2(A.Y, A.X);
			const double YawG = FMath::Atan2(G.Y, G.X);
			Yaw += FMath::FindDeltaAngleRadians(Yaw, YawG) * K;
			const double R = FMath::Lerp(FVector2D(A).Size(), FVector2D(G).Size(), double(K));
			const double Z = FMath::Lerp(A.Z, G.Z, double(K));
			Now = OrbitCentre + FVector(FMath::Cos(Yaw) * R, FMath::Sin(Yaw) * R, Z);
		};
		Swing(CamPos, CamGoal);
		Swing(CamLookNow, CamLook);
	}
	else
	{
		CamPos += (CamGoal - CamPos) * K;
		if (Phase != EPhase::Flying) CamLookNow += (CamLook - CamLookNow) * K;
	}
	// (never under the ground)
	const FVector C = Data->WorldToCourse(CamPos);
	const double G = Course.HeightAt(C.X, C.Z);
	if (FMath::IsFinite(G))
	{
		const double MinZ = Data->CourseToWorld(FVector(C.X, G, C.Z)).Z + 60;
		if (CamPos.Z < MinZ) CamPos.Z = MinZ;
	}
	// (nor under the water: there's nothing to see down there)
	if (const AOTGRuntimeCourse* RC = CourseOf(GetWorld()))
		CamPos.Z = FMath::Max(CamPos.Z, RC->WaterTopAt(CamPos) + 60);
	if (Cam) Cam->SetActorLocationAndRotation(CamPos, (CamLookNow - CamPos).Rotation());
}

void AOTGRound::UpdateHudInfo()
{
	if (!Hud || !Data) return;
	if (bRange)
	{
		UpdateWindBox();
		const FClub& RC = Clubs[ClubIndex(Club)];
		const FString R2 = Phase == EPhase::Flying ? FString::Printf(TEXT("Ball %d  ·  on its way"), RangeBalls + 1)
			: Phase == EPhase::Resting ? FString::Printf(TEXT("Ball %d  ·  a new ball in a moment (Enter: now)"), RangeBalls)
			: FString::Printf(TEXT("Ball %d  ·  on the mat"), RangeBalls + 1);
		Hud->SetInfo(TEXT("Driving range"), R2, FString::Printf(TEXT("%s  ·  aim %s"), RC.Name, *Len(AimDist)));
		return;
	}
	const FOTGHole& H = Hole();
	const double D = DistToPin();
	const FString L1 = bMini ? FString::Printf(TEXT("Hole %d  ·  %s  ·  Par %d"), H.Number, *H.Name, H.Par)
		: FString::Printf(TEXT("Hole %d  ·  Par %d  ·  %s"), H.Number, H.Par, *Len(Dist(TeeOf(H, TeeName), Pin())));
	FString L2;
	const FOTGSide& Sd = Game->Sides[SideIdx];
	const FString Who = Game->Single() ? FString() : Game->SideLabel(SideIdx) + TEXT("  ·  ");
	if (Phase == EPhase::Holed || Phase == EPhase::Done) L2 = FString::Printf(TEXT("%d stroke%s"), Sd.Scores[HoleIndex], Sd.Scores[HoleIndex] == 1 ? TEXT("") : TEXT("s"));
	else if (Phase == EPhase::Between) L2 = FString::Printf(TEXT("%sholed out"), *Who);
	else if (Phase == EPhase::Flying) L2 = FString::Printf(TEXT("%sStroke %d  ·  on its way"), *Who, Strokes);
	else if (bMini) L2 = FString::Printf(TEXT("%sStroke %d  ·  %s to the cup"), *Who, Strokes + 1, *Putt(D));
	else L2 = FString::Printf(TEXT("%sStroke %d  ·  %s to the pin"), *Who, Strokes + 1, OnGreen() ? *Putt(D) : *Len(D));
	// (the lie, on its own line in its colour - not in the air, nor after the hole)
	{
		const bool bLie = !bMini && (Phase == EPhase::Ready || Phase == EPhase::Resting);
		Hud->SetLie(bLie ? SurfaceDisplay(Lie) : FString(), LieColour(Lie));
	}
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
	Hud->SetShotData(FString(), FString());
	// the tiles (SOTGHud: which show and where are the player's)
	TMap<FString, FString> V;
	auto Num = [](double X, int32 Places, const TCHAR* Unit) { return FMath::IsFinite(X) ? FString::SanitizeFloat(FMath::RoundToDouble(X * FMath::Pow(10.0, Places)) / FMath::Pow(10.0, Places), Places) + Unit : FString(); };
	auto Rpm = [](double X) { return FText::AsNumber(FMath::RoundToInt(FMath::Abs(X))).ToString() + TEXT(" rpm"); };
	const FOTGClubData& Cl = S.Club;
	V.Add(TEXT("ballspeed"), FString::Printf(TEXT("%.1f mph"), S.SpeedMph));
	V.Add(TEXT("clubspeed"), Cl.bValid ? Num(Cl.SpeedMph, 1, TEXT(" mph")) : FString());
	{
		double Smash = Cl.bValid ? Cl.SmashFactor : NAN;
		if (!FMath::IsFinite(Smash) && Cl.bValid && FMath::IsFinite(Cl.SpeedMph) && Cl.SpeedMph > 1) Smash = S.SpeedMph / Cl.SpeedMph;
		V.Add(TEXT("smash"), Num(Smash, 2, TEXT("")));
	}
	V.Add(TEXT("launch"), FString::Printf(TEXT("%.1f%s"), S.VLA, DEG));
	V.Add(TEXT("direction"), Side(S.HLA));
	V.Add(TEXT("spin"), Rpm(S.TotalSpin));
	V.Add(TEXT("backspin"), Rpm(S.BackSpin));
	V.Add(TEXT("sidespin"), FMath::Abs(S.SideSpin) < 1 ? FString(TEXT("0 rpm")) : Rpm(S.SideSpin) + (S.SideSpin > 0 ? TEXT(" R") : TEXT(" L")));
	V.Add(TEXT("axis"), Side(S.SpinAxis));
	V.Add(TEXT("aoa"), Cl.bValid && FMath::IsFinite(Cl.AngleOfAttack) ? FString::Printf(TEXT("%.1f%s %s"), FMath::Abs(Cl.AngleOfAttack), DEG, Cl.AngleOfAttack >= 0 ? TEXT("up") : TEXT("down")) : FString());
	V.Add(TEXT("path"), Cl.bValid && FMath::IsFinite(Cl.Path) ? Side(Cl.Path) : FString());
	V.Add(TEXT("face"), Cl.bValid && FMath::IsFinite(Cl.FaceToTarget) ? Side(Cl.FaceToTarget) : FString());
	V.Add(TEXT("loft"), Cl.bValid ? Num(Cl.Loft, 1, DEG) : FString());
	if (bHasRes && (bCarryShown || bWithResult))
	{
		const bool bPutt = Undo && Undo->Lie == otg::Surface::Green;
		if (bPutt) V.Add(TEXT("total"), Putt(Res.total));
		else
		{
			V.Add(TEXT("carry"), Len(Res.carry));
			if (bWithResult)
			{
				V.Add(TEXT("total"), Len(Res.total));
				const int32 Off = FMath::RoundToInt(FMath::Abs(Res.offline) * M_TO_YD);
				V.Add(TEXT("offline"), Off == 0 ? FString(TEXT("0 yd")) : FString::Printf(TEXT("%d yd %s"), Off, Res.offline > 0 ? TEXT("R") : TEXT("L")));
				V.Add(TEXT("apex"), FString::Printf(TEXT("%d ft"), FMath::RoundToInt(Res.apex * M_TO_FT)));
			}
		}
	}
	(void)L1; (void)L2;
	Hud->SetShotValues(V);
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
	for (const auto* V : { &CVarLookExposure, &CVarLookSaturation, &CVarLookContrast, &CVarLookTemp, &CVarLookSun, &CVarLookSunWarm, &CVarLookSunPitch, &CVarLookSunSide, &CVarLookHaze, &CVarLookFog, &CVarLookVignette, &CVarLookTreeLift, &CVarLookShadows, &CVarLookSky, &CVarLookSunSoft, &CVarLookSlopeBias, &CVarWxCover, &CVarWxDensity, &CVarWxGrey, &CVarWxSun, &CVarWxSoft, &CVarWxMaskAt, &CVarWxRainDark, &CVarWxCloudFill, &CVarWxSunOwn })
		Stamp = HashCombine(Stamp, GetTypeHash(V->GetValueOnGameThread()));
	LookStamp = HashCombine(HashCombine(HashCombine(Stamp, GetTypeHash(CVarWxOwnClouds.GetValueOnGameThread())), GetTypeHash(HoleIndex)), GetTypeHash(SunHour));
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
	// (the Meridian sky: the sun, sky, fog and clouds are its - OTGSky.cpp)
	if (EnsureSky())
	{
		ApplySky();
		return;
	}
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
		// (the level's own blue back: the weather greys it from this, not from what it greyed last
		// time - each new time of day under cloud would grey it again, to a black sky)
		It->SetRayleighScatteringScale(OrigRayleigh);
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
	if (bRange && Mode == TEXT("live")) Mode = TEXT("calm");   // (the range is nowhere: calm, until the weather panel changes it)
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
	if (EnsureSky())
	{
		ApplySky();
		return;
	}
	for (TActorIterator<AVolumetricCloud> It(GetWorld()); It; ++It)
	{
		UVolumetricCloudComponent* VC = It->FindComponentByClass<UVolumetricCloudComponent>();
		if (!VC) continue;
		// our own clouds (M_Clouds: make_sky.py): the weather's cover, the kind of cloud, carried by
		// the wind, darker in rain
		if (CVarWxOwnClouds.GetValueOnGameThread() > 0)
		{
			if (!OwnCloudMat)
				if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenTeeGolf/Game/M_Clouds.M_Clouds")))
				{
					OwnCloudMat = UMaterialInstanceDynamic::Create(M, this);
					UE_LOG(LogOTGRound, Display, TEXT("Clouds: our own (the layer %.1f km up, %.1f km thick)"), VC->LayerBottomAltitude, VC->LayerHeight);
				}
			if (OwnCloudMat)
			{
				if (VC->GetMaterial() != OwnCloudMat) VC->SetMaterial(OwnCloudMat);
				const double Cloudy = Weather.RainMm > 0.05 ? FMath::Max(Weather.Cloud, 0.92) : Weather.Cloud;
				const float Storm = float(FMath::Clamp((Weather.RainMm - 0.05) / 6.0, 0.0, 1.0));
				// (the layer: the material works in the same metres)
				if (CVarWxCloudLayer.GetValueOnGameThread() > 0)
				{
					VC->SetLayerBottomAltitude(1.5f);
					VC->SetLayerHeight(3.f);
				}
				OwnCloudMat->SetScalarParameterValue(TEXT("Bottom"), VC->LayerBottomAltitude * 1000.f);
				OwnCloudMat->SetScalarParameterValue(TEXT("Thick"), VC->LayerHeight * 1000.f);
				OwnCloudMat->SetScalarParameterValue(TEXT("Fill"), CVarWxCloudFill.GetValueOnGameThread());
				OwnCloudMat->SetScalarParameterValue(TEXT("Cover"), float(Cloudy));
				OwnCloudMat->SetScalarParameterValue(TEXT("Stratus"), float(FMath::SmoothStep(0.75, 1.0, Cloudy)));
				OwnCloudMat->SetScalarParameterValue(TEXT("Density"), FMath::Lerp(0.035f, 0.05f, float(Cloudy)) * (1.f + Storm * 2.f) * CVarWxDensity.GetValueOnGameThread() / 4.f);
				OwnCloudMat->SetVectorParameterValue(TEXT("Albedo"), FMath::Lerp(FLinearColor(0.93f, 0.93f, 0.95f), FLinearColor(0.32f, 0.34f, 0.38f), Storm > 0 ? 0.45f + 0.55f * Storm : 0.f));
				// (the wind up there: the hole's wind, in the world, faster aloft - and never quite still)
				double Mps = 0, FromDeg = 0;
				HoleWind(Mps, FromDeg);
				const double F = FMath::DegreesToRadians(FromDeg), Aloft = FMath::Max(3.0, Mps * 2.5);
				OwnCloudMat->SetScalarParameterValue(TEXT("WindX"), float(-FMath::Sin(F) * Aloft));
				OwnCloudMat->SetScalarParameterValue(TEXT("WindY"), float(FMath::Cos(F) * Aloft));
				break;
			}
		}
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
		CloudMat->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), BaseCoverage + FMath::Lerp(-0.25f, CVarWxCover.GetValueOnGameThread(), Cl));
		CloudMat->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"), BaseDensity * FMath::Lerp(0.8f, CVarWxDensity.GetValueOnGameThread(), Cl));
		// (the engine's clouds are kept to the patches of a mask: an overcast sky clouds over everywhere)
		{
			static UTexture* Mask0 = nullptr;
			static TWeakObjectPtr<UTexture> White;
			if (!White.IsValid()) White = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
			if (!Mask0) CloudMat->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("Layout_GlobalCloudMask")), Mask0);
			const bool bOvercast = Cl >= CVarWxMaskAt.GetValueOnGameThread();
			if (UTexture* T = bOvercast ? White.Get() : Mask0) CloudMat->SetTextureParameterValue(TEXT("Layout_GlobalCloudMask"), T);
		}
		// (rain: darker clouds - their colour; the material's own storm clouds thin the deck out to a
		// few streaks, so not those)
		{
			static FLinearColor Albedo0(-1, -1, -1);
			if (Albedo0.R < 0 && !CloudMat->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Cloud_AlbedoColor")), Albedo0)) Albedo0 = FLinearColor::White;
			const float Dark = Weather.RainMm >= 5 ? 1.f : Weather.RainMm > 0.05 ? CVarWxRainDark.GetValueOnGameThread() : 0.f;
			CloudMat->SetVectorParameterValue(TEXT("Cloud_AlbedoColor"), FMath::Lerp(Albedo0, FLinearColor(0.3f, 0.32f, 0.35f), Dark));
			CloudMat->SetScalarParameterValue(TEXT("StormClouds"), 0.f);
		}
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
	// an overcast sky: grey rather than blue - more haze (white), the blue taken out of what the sky
	// gives - and the sun behind the cloud: hardly any direct light, its shadows faint and soft, the
	// sky's light doing the lighting (rain: overcast whatever the cloud says)
	const double Cloudy = Weather.RainMm > 0.05 ? FMath::Max(Weather.Cloud, 0.9) : Weather.Cloud;
	const float Over = float(FMath::SmoothStep(0.45, 0.95, Cloudy));
	const float Dim = float(FMath::SmoothStep(0.3, 0.95, Cloudy));
	const float Grey = Over * CVarWxGrey.GetValueOnGameThread();
	for (TObjectIterator<USkyAtmosphereComponent> It; It; ++It)
	{
		if (It->GetWorld() != GetWorld()) continue;
		if (BaseRayleigh < 0) { BaseRayleigh = It->RayleighScatteringScale; BaseMie = It->MieScatteringScale; }
		if (BaseSkyLum.R < 0) BaseSkyLum = It->SkyLuminanceFactor;
		It->SetRayleighScatteringScale(BaseRayleigh * FMath::Lerp(1.f, 0.6f, Over));
		It->SetMieScatteringScale(BaseMie * FMath::Lerp(1.f, 10.f, Over));
		// (a blue sky's light is mostly blue: less of the blue, and of the green, leaves it grey)
		It->SetSkyLuminanceFactor(BaseSkyLum * FMath::Lerp(FLinearColor::White, FLinearColor(0.95f, 0.72f, 0.42f), Grey));
		break;
	}
	for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
	{
		USkyLightComponent* SL = It->GetLightComponent();
		if (!SL) continue;
		if (BaseSkyLight < 0) BaseSkyLight = SL->Intensity;
		SkyNow = BaseSkyLight * FMath::Lerp(1.f, 0.85f, Over);
		SL->SetIntensity(SkyNow);
		break;
	}
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		if (!L) continue;
		if (BaseSun < 0) BaseSun = L->Intensity;
		// (our own clouds shade the ground themselves, and are lit by this sun: it's dimmed less)
		L->SetIntensity(BaseSun * FMath::Lerp(1.f, OwnCloudMat ? CVarWxSunOwn.GetValueOnGameThread() : CVarWxSun.GetValueOnGameThread(), Dim));
		L->SetLightSourceAngle(FMath::Lerp(CVarLookSunSoft.GetValueOnGameThread(), CVarWxSoft.GetValueOnGameThread(), Dim));
		// (the clouds' shadows on the course, drifting with them)
		if (OwnCloudMat && !L->bCastCloudShadows)
		{
			L->bCastCloudShadows = true;
			L->CloudShadowOnSurfaceStrength = 0.85f;
			L->CloudShadowExtent = 30.f;   // (km round the camera)
			L->MarkRenderStateDirty();
		}
		break;
	}
	// (the rain's streaks: grey-white under a dark sky, not glowing)
	if (RainMat) RainMat->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(FLinearColor(0.75f, 0.78f, 0.82f), FLinearColor(0.58f, 0.61f, 0.66f), Dim));
	// (rain and storms are darker days: the eye's exposure doesn't lift them back to a bright one)
	if (LookVolume)
	{
		const float Gloom = float(FMath::Clamp((Weather.RainMm - 0.05) / 7.0, 0.0, 1.0));
		LookVolume->Settings.AutoExposureBias = CVarLookExposure.GetValueOnGameThread() - (Weather.RainMm > 0.05 ? 0.3f + 0.9f * Gloom : 0.f) - 0.25f * Over;
	}
}

// The steady wind on this hole, in course space (FromDeg: where it blows from, as the aim's heading)
void AOTGRound::HoleWind(double& Mps, double& FromDeg) const
{
	Mps = 0;
	FromDeg = 0;
	if (Weather.WindMph < 0.5 || bMini) return;   // (the putt-putt's sheltered)
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
	if (bMini && Course.SurfaceAt(Ball.x, Ball.z) == otg::Surface::Green) Lie = otg::Surface::Green;   // (the tee's on the felt)
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

// ------------------------------------------------------------------ the slope grid (the web version's putting grid)

// On the green, ready to putt (and not turned off): a grid of lines draped over the green, square to
// the putt about a metre apart, each coloured by the slope under it (white flat, under 1%; yellow
// about 2%; orange; red 3% and more), and at every crossing two dots - one on each line - sliding
// downhill along it, faster the steeper that way is, still where it's flat
void AOTGRound::UpdateSlopeGrid(float Dt)
{
	const bool bWant = bGridOn && Phase == EPhase::Ready && OnGreen() && !bFreeView;
	if (!bWant) { HideSlopeGrid(); return; }
	const otg::V3 P = Pin();
	const FVector Key(Ball.x + Ball.z * 1e-3, P.x + P.z * 1e-3, 1);
	if (!GridFor.Equals(Key, 1e-4)) { GridFor = Key; BuildSlopeGrid(); }
	TArray<FTransform> T;
	T.Reserve(Dots.Num());
	for (FGridDot& D : Dots)
	{
		if (D.Pct > 0) D.Phase = FMath::Fmod(D.Phase + Dt * (0.08 + D.Pct * 0.5), 1.0);
		const double Travel = D.Pct > 0 ? D.Phase * GridSpacing : 0;
		const double X = D.X + D.Dx * Travel, Z = D.Z + D.Dz * Travel;
		T.Add(FTransform(FQuat::Identity, World(otg::V3{ X, Course.HeightAt(X, Z) + 0.02, Z }), FVector(0.028)));
	}
	if (GridDots->GetInstanceCount() == T.Num()) GridDots->BatchUpdateInstancesTransforms(0, T, true, true, true);
	else { GridDots->ClearInstances(); GridDots->AddInstances(T, false, true); }
}

void AOTGRound::HideSlopeGrid()
{
	if (GridFor.IsZero() && !GridMesh->GetNumSections()) return;
	GridFor = FVector::ZeroVector;
	GridMesh->ClearAllMeshSections();
	GridDots->ClearInstances();
	Dots.Reset();
}

void AOTGRound::BuildSlopeGrid()
{
	GridMesh->ClearAllMeshSections();
	GridDots->ClearInstances();
	Dots.Reset();
	const otg::V3 P = Pin();
	const double dx = P.x - Ball.x, dz = P.z - Ball.z, Dist = FMath::Max(FMath::Sqrt(dx * dx + dz * dz), 1e-3);
	const double Fx = dx / Dist, Fz = dz / Dist, Rx = Fz, Rz = -Fx;   // along the putt, across it
	const double Half = FMath::Min(16.0, FMath::Max(5.0, Dist * 0.6 + 3));
	const double Cx = (Ball.x + P.x) / 2, Cz = (Ball.z + P.z) / 2;
	GridSpacing = FMath::Min(1.2, FMath::Max(0.6, Half * 2 / 18));
	const double Sp = GridSpacing;
	auto OnGreenAt = [&](double X, double Z) { const otg::Surface S = Course.SurfaceAt(X, Z); return S == otg::Surface::Green || S == otg::Surface::Fringe; };
	// (the slope: % and its downhill way, from the heights a few cm either side)
	auto SlopeAt = [&](double X, double Z, double& Gx, double& Gz) {
		const double e = 0.25;
		Gx = -(Course.HeightAt(X + e, Z) - Course.HeightAt(X - e, Z)) / (2 * e);
		Gz = -(Course.HeightAt(X, Z + e) - Course.HeightAt(X, Z - e)) / (2 * e);
		return FMath::Sqrt(Gx * Gx + Gz * Gz) * 100.0;
	};
	auto Band = [](double Pct) { const double t = FMath::Min(1.0, Pct / 3.0); return t < 0.33 ? 0 : t < 0.6 ? 1 : t < 0.85 ? 2 : 3; };
	struct FSec { TArray<FVector> V; TArray<int32> Tri; TArray<FVector> N; };
	FSec Secs[4];
	const int32 Steps = FMath::CeilToInt(Half / Sp);
	const double W = 0.012;   // (half a line's width, m)
	for (int32 Dir = 0; Dir < 2; Dir++)
	{
		const double Ax = Dir ? Rx : Fx, Az = Dir ? Rz : Fz, Bx = Dir ? Fx : Rx, Bz = Dir ? Fz : Rz;
		for (int32 k = -Steps; k <= Steps; k++)
		{
			const double Ox = Cx + Bx * k * Sp, Oz = Cz + Bz * k * Sp;
			bool bPrev = false;
			FVector PrevL, PrevR;
			for (double s = -Half; s <= Half + 1e-6; s += Sp / 4)
			{
				const double X = Ox + Ax * s, Z = Oz + Az * s;
				if (!OnGreenAt(X, Z)) { bPrev = false; continue; }
				const double Y = Course.HeightAt(X, Z) + 0.012;
				const FVector L = World(otg::V3{ X - Bx * W, Y, Z - Bz * W }), R = World(otg::V3{ X + Bx * W, Y, Z + Bz * W });
				if (bPrev)
				{
					double gx, gz;
					FSec& S = Secs[Band(SlopeAt(X, Z, gx, gz))];
					const int32 b = S.V.Num();
					S.V.Append({ PrevL, PrevR, L, R });
					S.N.Append({ FVector::UpVector, FVector::UpVector, FVector::UpVector, FVector::UpVector });
					// (both faces: whichever way the frame turns them)
					S.Tri.Append({ b, b + 1, b + 2, b + 2, b + 1, b + 3, b, b + 2, b + 1, b + 2, b + 3, b + 1 });
				}
				PrevL = L; PrevR = R; bPrev = true;
			}
		}
	}
	for (int32 i = 0; i < 4; i++)
		if (Secs[i].V.Num())
		{
			GridMesh->CreateMeshSection_LinearColor(i, Secs[i].V, Secs[i].Tri, Secs[i].N, {}, {}, {}, false);
			GridMesh->SetMaterial(i, GridMats[i]);
		}
	// the dots: two at each crossing on the green, one on each of its lines
	for (int32 i = -Steps; i <= Steps; i++)
		for (int32 j = -Steps; j <= Steps; j++)
		{
			const double X = Cx + Fx * i * Sp + Rx * j * Sp, Z = Cz + Fz * i * Sp + Rz * j * Sp;
			if (!OnGreenAt(X, Z)) continue;
			double gx, gz;
			const double Pct = SlopeAt(X, Z, gx, gz), L = FMath::Max(FMath::Sqrt(gx * gx + gz * gz), 1e-9);
			for (int32 Dir = 0; Dir < 2; Dir++)
			{
				const double Ux = Dir ? Rx : Fx, Uz = Dir ? Rz : Fz;
				const double Part = (gx * Ux + gz * Uz) / L, PartPct = Pct * FMath::Abs(Part);
				const double Sx = Ux * FMath::Sign(Part), Sz = Uz * FMath::Sign(Part);
				const bool bMoves = PartPct >= 0.3 && OnGreenAt(X + Sx * Sp, Z + Sz * Sp);
				Dots.Add({ X, Z, Sx, Sz, bMoves ? PartPct : 0.0, FMath::FRand() });
			}
		}
}

// ------------------------------------------------------------------ the driving range

double AOTGRound::ClubCarryM() const { return FMath::Max(10.0, Clubs[ClubIndex(Club)].CarryYd / M_TO_YD); }

// A shot on the range has stopped: its numbers kept and shown, where it went said, and a new ball
// on the mat in a few seconds
void AOTGRound::RangeShotDone()
{
	Ball = Res.rest;
	PlaceBall(Ball);
	RangeBalls++;
	FOTGRangeShot S;
	S.Club = Club;
	S.ClubName = Clubs[ClubIndex(Club)].Name;
	S.CarryYd = Res.carry * M_TO_YD;
	S.TotalYd = Res.total * M_TO_YD;
	S.OfflineYd = Res.offline * M_TO_YD;
	S.ApexFt = Res.apex * M_TO_FT;
	S.Mph = LastShot.SpeedMph;
	S.Launch = LastShot.VLA;
	S.Direction = LastShot.HLA;
	S.Spin = LastShot.TotalSpin > 0 ? LastShot.TotalSpin : FMath::Sqrt(FMath::Square(LastShot.BackSpin) + FMath::Square(LastShot.SideSpin));
	S.Axis = LastShot.SpinAxis;
	S.RestX = Res.rest.x;
	S.RestZ = Res.rest.z;
	RangeShots.Add(S);
	// where it went: on a target green, near one, or off the range
	const otg::Surface Rest = Course.SurfaceAt(Res.rest.x, Res.rest.z);
	const otg::range::FTarget* Near = nullptr;
	double NearM = 1e9;
	for (const otg::range::FTarget& T : otg::range::Targets())
	{
		const double D = FMath::Sqrt(FMath::Square(Res.rest.x - T.X) + FMath::Square(Res.rest.z - T.Z));
		if (D < NearM) { NearM = D; Near = &T; }
	}
	FString Say;
	if (Res.result == "holed") Say = TEXT("In the hole at the 150!");
	else if (Res.result == "oob" || Rest == otg::Surface::None || Rest == otg::Surface::Oob) Say = TEXT("Off the range");
	else if (Near && Rest == otg::Surface::Green && NearM < Near->R * 1.4) Say = FString::Printf(TEXT("On the %d green, %s from the flag"), Near->Yards, *Putt(NearM));
	else if (Near && NearM < 25) Say = FString::Printf(TEXT("%s from the %d flag"), *Len(NearM), Near->Yards);
	else Say = FString::Printf(TEXT("In the %s"), *SurfaceDisplay(Rest).ToLower());
	Toast(Say, 3.f);
	UE_LOG(LogOTGRound, Display, TEXT("Range ball %d, %s: carry %.0f yd, total %.0f yd, offline %.1f yd, apex %.0f ft (%s)"), RangeBalls, *S.ClubName, S.CarryYd, S.TotalYd, S.OfflineYd, S.ApexFt, *Say);
	ShowShotData(true);
	UpdateRange();
	Phase = EPhase::Resting;
	PhaseTimer = PendingShot ? 0.0 : 3.5;
	UpdateHudInfo();
}

// A new ball on the middle mat: ready for the next shot (the aim and club as they were)
void AOTGRound::NewBall()
{
	if (!bRange || !Data) return;
	if (Phase == EPhase::Flying) { AnimT = 1e9; return; }   // (the flight skipped first: its numbers kept)
	Ball = TeeOf(Hole(), TeeName);
	Lie = otg::Surface::Tee;
	Strokes = 0;
	ClearTracer();
	Phase = EPhase::Ready;
	PlaceBall(Ball);
	ShowAim();
	AddressView(false);
	UpdateHudInfo();
	TellLaunchMonitor(true);
	if (PendingShot)
	{
		const FOTGShot Next = *PendingShot;
		PendingShot.Reset();
		OnShot(Next);
	}
}

// The range panel, and a gold mark where each of the last ten shots with this club stopped
void AOTGRound::UpdateRange()
{
	if (RangePanel) RangePanel->Show(RangeShots, Club, Clubs[ClubIndex(Club)].Name);
	if (!RangeDots) return;
	RangeDots->ClearInstances();
	int32 Shown = 0;
	for (int32 i = RangeShots.Num() - 1; i >= 0 && Shown < 10; i--)
	{
		const FOTGRangeShot& S = RangeShots[i];
		if (S.Club != Club) continue;
		Shown++;
		RangeDots->AddInstance(FTransform(FRotator::ZeroRotator, World(OnGround(S.RestX, S.RestZ)) + FVector(0, 0, 2), FVector(0.7, 0.7, 0.02)), true);
	}
}
