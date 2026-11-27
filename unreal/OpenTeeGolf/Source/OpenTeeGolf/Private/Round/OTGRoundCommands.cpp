// The round's console commands (OTG.Round.*, OTG.Trees.*, OTG.Look.*, OTG.Later ...): each finds the
// round (or the course) in the world and calls it. See OTGRound.h.

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
#include "Minimap/SOTGMinimap.h"
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

namespace
{

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
	FAutoConsoleCommandWithWorldAndArgs GrassCmd(TEXT("OTG.Round.GrassDesigner"), TEXT("The course designer (the R key; OTG.Round.GrassDesigner trees|bridges: on that tab)"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		if (AOTGRound* R = RoundIn(W)) R->ToggleGrassDesigner(A.Num() && A[0] == TEXT("trees") ? 1 : A.Num() && A[0] == TEXT("bridges") ? 2 : -1);
	}));
	// (testing the bridges tab - the designer open on it first, OTG.Round.GrassDesigner bridges: a click
	// on the course, a bridge from one point to another in course metres, the picked or new one changed)
	FAutoConsoleCommandWithWorldAndArgs BridgeClickCmd(TEXT("OTG.Bridges.Click"), TEXT("OTG.Bridges.Click <0..1 across> <0..1 down> [pick|add]: (testing) a click on the course in the designer's bridges tab"),
		FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
			AOTGRound* R = RoundIn(W);
			if (!R || A.Num() < 2) return;
			if (A.Num() > 2) R->SetBridgeTool(A[2] == TEXT("add") ? 1 : 0);
			R->DesignClickAt(FCString::Atod(*A[0]), FCString::Atod(*A[1]));
		}));
	FAutoConsoleCommandWithWorldAndArgs BridgeAddCmd(TEXT("OTG.Bridges.Add"), TEXT("OTG.Bridges.Add <x> <z> <x> <z>: (testing) a new bridge between two points (course metres), as two clicks would make it"),
		FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
			if (AOTGRound* R = RoundIn(W); R && A.Num() >= 4) R->AddBridgeAt(FCString::Atod(*A[0]), FCString::Atod(*A[1]), FCString::Atod(*A[2]), FCString::Atod(*A[3]));
		}));
	FAutoConsoleCommandWithWorldAndArgs BridgeShowCmd(TEXT("OTG.Bridges.Show"), TEXT("OTG.Bridges.Show [which, 1..; last] [distance m] [height m] [yaw]: (testing) the free view looking at a bridge, from its side"),
		FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
			AOTGRuntimeCourse* C = CourseOf(W);
			AOTGRound* R = RoundIn(W);
			if (!C || !R || !C->BridgeDesign().Bridges.Num()) return;
			const TArray<FOTGBridge>& L = C->BridgeDesign().Bridges;
			auto Arg = [&](int32 i, double D) { return A.IsValidIndex(i) ? FCString::Atod(*A[i]) : D; };
			const int32 I = FMath::Clamp(int32(Arg(0, L.Num())) - 1, 0, L.Num() - 1);
			const FOTGBridge& B = L[I];
			const FVector Mid = C->CourseToWorldAt((B.AX + B.BX) / 2, B.TopAt(0.5), (B.AZ + B.BZ) / 2);
			const FVector Along = C->CourseToWorldAt(B.BX, B.BY, B.BZ) - C->CourseToWorldAt(B.AX, B.AY, B.AZ);
			const double Side = FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X)) + 90;
			R->FreeLookAt(Mid, Arg(1, 25) * 100, Arg(2, 6) * 100, A.IsValidIndex(3) ? Arg(3, 0) : Side);
		}));
	FAutoConsoleCommandWithWorldAndArgs BridgeEditCmd(TEXT("OTG.Bridges.Edit"), TEXT("OTG.Bridges.Edit shape flat|arch, rails on|off, look wood|stone|white, width|rise <m>, build|cancel|out: (testing) the picked or new bridge"),
		FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
			if (AOTGRound* R = RoundIn(W); R && A.Num()) R->EditBridge(A[0], A.Num() > 1 ? A[1] : FString());
		}));
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

}
