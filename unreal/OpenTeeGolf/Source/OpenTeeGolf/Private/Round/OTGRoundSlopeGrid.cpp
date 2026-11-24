// The slope grid on the green (see OTGRound.h; the web version's putting grid, the G key): lines draped
// over the green coloured by its slope, and dots sliding downhill.

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
