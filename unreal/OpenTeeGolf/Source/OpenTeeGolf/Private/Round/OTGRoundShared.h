// Helpers shared by the round's files (OTGRound.h): finding the round and the course in a world, the
// clubs, units, the lie's names and colours, scores' names, distances. Private to Round.

#pragma once

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

namespace
{
	AOTGRound* RoundIn(UWorld* W)
	{
		for (TActorIterator<AOTGRound> It(W); It; ++It) return *It;
		return nullptr;
	}
	// (the tree designer's changes, for testing: OTG.Trees.Set <species|all|rocks> model|size|density|on <value>,
	// OTG.Trees.Save, OTG.Trees.Show <species>, OTG.Trees.List)
	AOTGRuntimeCourse* CourseOf(UWorld* W) { if (W) for (TActorIterator<AOTGRuntimeCourse> It(W); It; ++It) return *It; return nullptr; }
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
