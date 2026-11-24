// The rain near the camera (see OTGRound.h): its streaks round the camera, as heavy as the weather's.
// (The far rain, the splashes and the wet ground: OTGRoundExtras.cpp.)

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

// Rain: streaks round the camera, falling and blown with the wind (M_Rain: make_game_assets.py)
void FOTGRoundRain::UpdateRain(float Dt)
{
	const double Mm = Round->Weather.RainMm;
	UpdateRainFar(Dt, Mm);
	Round->WaterFx.UpdateRipples(Dt);
	if (Mm <= 0.05 || !RainMat || !Round->Camera.Cam)
	{
		if (RainVerts) { RainMesh->ClearAllMeshSections(); RainVerts = 0; RainDrops.Reset(); }
		return;
	}
	const int32 N = FMath::Clamp(int32(900 + Mm * 450), 900, 4500);
	const FVector Half(2200, 2200, 1300);
	const FVector C = Round->Camera.CamPos;
	if (RainDrops.Num() != N)
	{
		RainDrops.SetNum(N);
		for (FVector& D : RainDrops) D = C + FVector(FMath::FRandRange(-Half.X, Half.X), FMath::FRandRange(-Half.Y, Half.Y), FMath::FRandRange(-400, Half.Z));
	}
	double Mps, FromDeg;
	Round->HoleWind(Mps, FromDeg);
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
