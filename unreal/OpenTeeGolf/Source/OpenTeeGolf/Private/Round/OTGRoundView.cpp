// Showing the round (see OTGRound.h): the ball, the aim marker, the tracer, and the cameras - the hole
// from above the tee, behind the ball at address, following it in flight.

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
