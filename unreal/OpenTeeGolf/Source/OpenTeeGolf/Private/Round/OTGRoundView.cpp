// Showing the round (see OTGRound.h): the ball, the aim marker, the tracer, and the cameras - the hole
// from above the tee, behind the ball at address, following it in flight.

#include "Round/OTGRound.h"
#include "Tracer/OTGShotTracerComponent.h"
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
	HudView.UpdateHudInfo();
}

void AOTGRound::UpdateTracer(int32 Upto, const FVector& Head)
{
	// The path so far (the level's cm), drawn by the tracer (Plugins/OTGShotTracer) as a ribbon facing the camera
	TArray<FVector> Path;
	const std::vector<otg::PathPoint>& Pts = Res.points;
	if (!Pts.empty() && !TracerFrom(otg::V3{ Pts[0].x, Pts[0].y, Pts[0].z })) { TracerMesh->Clear(); return; }
	if (Res.hasCarryPoint && AnimT >= Res.carryPoint.t)
	{
		// down: drawn once more to where it landed, then left to fade (Tick)
		if (TracerLandedAt >= 0) return;
		int32 C = 0;
		while (C < int32(Pts.size()) && Pts[C].t <= Res.carryPoint.t) C++;
		for (int32 i = 0; i < C; i++) Path.Add(World(otg::V3{ Pts[i].x, Pts[i].y, Pts[i].z }));
		TracerMesh->Draw(Path, World(otg::V3{ Res.carryPoint.x, Res.carryPoint.y, Res.carryPoint.z }), Camera.CamPos);
		TracerLandedAt = RoundTime;
		return;
	}
	for (int32 i = 0; i < Upto && i < int32(Pts.size()); i++) Path.Add(World(otg::V3{ Pts[i].x, Pts[i].y, Pts[i].z }));
	TracerMesh->Draw(Path, Head, Camera.CamPos);
}

bool AOTGRound::TracerFrom(const otg::V3& Start) const
{
	if (bRange || bMini) return true;   // (the range and mini golf: as ever)
	constexpr double NearGreenM = 30.0 / M_TO_YD;   // (30 yards from the pin)
	return Dist(Start, Pin()) > NearGreenM;
}

void AOTGRound::ClearTracer()
{
	TracerMesh->Clear();
	TracerLandedAt = -1;
}

void FOTGRoundCamera::AddressView(bool bSnap)
{
	// Behind the ball, looking along the aim (lower and closer for a putt)
	const FVector B = Round->World(Round->Ball);
	const FVector D(FMath::Sin(Round->Heading), -FMath::Cos(Round->Heading), 0);
	if (Round->OnGreen())
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

void FOTGRoundCamera::OverviewView()
{
	if (Round->bMini)
	{
		// (the putt-putt: the whole hole - tee, turn and cup - from above behind the tee)
		const FOTGHole& H = Round->Hole();
		FBox Box(ForceInit);
		for (const FVector& Q : H.Path) Box += Round->World(Round->OnGround(Q.X, Q.Z));
		Box += Round->World(Round->Ball);
		const FVector Mid = Box.GetCenter();
		const double L = FMath::Max(Box.GetSize().X, Box.GetSize().Y);
		FVector D = FVector(Mid.X - Round->World(Round->Ball).X, Mid.Y - Round->World(Round->Ball).Y, 0).GetSafeNormal();
		if (D.IsNearlyZero()) D = FVector(FMath::Sin(Round->Heading), -FMath::Cos(Round->Heading), 0);
		CamPos = CamGoal = Mid - D * (L * 0.55 + 200) + FVector(0, 0, L * 0.7 + 280);
		CamLookNow = CamLook = Mid;
		OverviewLeft = 2.8;
		return;
	}
	// The hole from high behind the tee, looking down at where it's aimed, then down to the ball
	const FVector B = Round->World(Round->Ball);
	const FVector D(FMath::Sin(Round->Heading), -FMath::Cos(Round->Heading), 0);
	const double Ahead = FMath::Min(Round->AimDist, 200.0) * 100;
	CamPos = CamGoal = B - D * 2600 + FVector(0, 0, 1800);
	CamLookNow = CamLook = Round->World(Round->OnGround(Round->Ball.x + FMath::Sin(Round->Heading) * Ahead / 100, Round->Ball.z + FMath::Cos(Round->Heading) * Ahead / 100));
	OverviewLeft = 2.8;
}

void FOTGRoundCamera::UpdateCamera(float Dt)
{
	const float K = 1.f - FMath::Exp(-Dt * CamSpeed);
	if (Round->Phase != AOTGRound::EPhase::Flying && CamGoal.Equals(OrbitGoal, 1.0))
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
		if (Round->Phase != AOTGRound::EPhase::Flying) CamLookNow += (CamLook - CamLookNow) * K;
	}
	// (never under the ground)
	const FVector C = Round->Data->WorldToCourse(CamPos);
	const double G = Round->Course.HeightAt(C.X, C.Z);
	if (FMath::IsFinite(G))
	{
		const double MinZ = Round->Data->CourseToWorld(FVector(C.X, G, C.Z)).Z + 60;
		if (CamPos.Z < MinZ) CamPos.Z = MinZ;
	}
	// (nor under the water: there's nothing to see down there)
	if (const AOTGRuntimeCourse* RC = CourseOf(Round->GetWorld()))
		CamPos.Z = FMath::Max(CamPos.Z, RC->WaterTopAt(CamPos) + 60);
	if (Cam) Cam->SetActorLocationAndRotation(CamPos, (CamLookNow - CamPos).Rotation());
}
