// The driving range (see OTGRound.h): each shot's numbers in the range panel, where the club's last
// shots stopped, and a new ball on the mat.

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

// ------------------------------------------------------------------ the driving range

double FOTGRoundRangeMode::ClubCarryM() const { return FMath::Max(10.0, Clubs[ClubIndex(Round->Club)].CarryYd / M_TO_YD); }

// A shot on the range has stopped: its numbers kept and shown, where it went said, and a new ball
// on the mat in a few seconds
void FOTGRoundRangeMode::RangeShotDone()
{
	Round->Ball = Round->Res.rest;
	Round->PlaceBall(Round->Ball);
	RangeBalls++;
	FOTGRangeShot S;
	S.Club = Round->Club;
	S.ClubName = Clubs[ClubIndex(Round->Club)].Name;
	S.CarryYd = Round->Res.carry * M_TO_YD;
	S.TotalYd = Round->Res.total * M_TO_YD;
	S.OfflineYd = Round->Res.offline * M_TO_YD;
	S.ApexFt = Round->Res.apex * M_TO_FT;
	S.Mph = Round->LastShot.SpeedMph;
	S.Launch = Round->LastShot.VLA;
	S.Direction = Round->LastShot.HLA;
	S.Spin = Round->LastShot.TotalSpin > 0 ? Round->LastShot.TotalSpin : FMath::Sqrt(FMath::Square(Round->LastShot.BackSpin) + FMath::Square(Round->LastShot.SideSpin));
	S.Axis = Round->LastShot.SpinAxis;
	S.RestX = Round->Res.rest.x;
	S.RestZ = Round->Res.rest.z;
	RangeShots.Add(S);
	// where it went: on a target green, near one, or off the range
	const otg::Surface Rest = Round->Course.SurfaceAt(Round->Res.rest.x, Round->Res.rest.z);
	const otg::range::FTarget* Near = nullptr;
	double NearM = 1e9;
	for (const otg::range::FTarget& T : otg::range::Targets())
	{
		const double D = FMath::Sqrt(FMath::Square(Round->Res.rest.x - T.X) + FMath::Square(Round->Res.rest.z - T.Z));
		if (D < NearM) { NearM = D; Near = &T; }
	}
	FString Say;
	if (Round->Res.result == "holed") Say = TEXT("In the hole at the 150!");
	else if (Round->Res.result == "oob" || Rest == otg::Surface::None || Rest == otg::Surface::Oob) Say = TEXT("Off the range");
	else if (Near && Rest == otg::Surface::Green && NearM < Near->R * 1.4) Say = FString::Printf(TEXT("On the %d green, %s from the flag"), Near->Yards, *Round->Putt(NearM));
	else if (Near && NearM < 25) Say = FString::Printf(TEXT("%s from the %d flag"), *Round->Len(NearM), Near->Yards);
	else Say = FString::Printf(TEXT("In the %s"), *SurfaceDisplay(Rest).ToLower());
	Round->HudView.Toast(Say, 3.f);
	UE_LOG(LogOTGRound, Display, TEXT("Range ball %d, %s: carry %.0f yd, total %.0f yd, offline %.1f yd, apex %.0f ft (%s)"), RangeBalls, *S.ClubName, S.CarryYd, S.TotalYd, S.OfflineYd, S.ApexFt, *Say);
	Round->HudView.ShowShotData(true);
	UpdateRange();
	Round->Phase = AOTGRound::EPhase::Resting;
	Round->PhaseTimer = Round->PendingShot ? 0.0 : 3.5;
	Round->HudView.UpdateHudInfo();
}

// A new ball on the middle mat: ready for the next shot (the aim and club as they were)
void FOTGRoundRangeMode::NewBall()
{
	if (!Round->bRange || !Round->Data) return;
	if (Round->Phase == AOTGRound::EPhase::Flying) { Round->AnimT = 1e9; return; }   // (the flight skipped first: its numbers kept)
	Round->Ball = Round->TeeOf(Round->Hole(), Round->TeeName);
	Round->Lie = otg::Surface::Tee;
	Round->Strokes = 0;
	Round->ClearTracer();
	Round->Phase = AOTGRound::EPhase::Ready;
	Round->PlaceBall(Round->Ball);
	Round->ShowAim();
	Round->Camera.AddressView(false);
	Round->HudView.UpdateHudInfo();
	Round->TellLaunchMonitor(true);
	if (Round->PendingShot)
	{
		const FOTGShot Next = *Round->PendingShot;
		Round->PendingShot.Reset();
		Round->OnShot(Next);
	}
}

// The range panel, and a gold mark where each of the last ten shots with this club stopped
void FOTGRoundRangeMode::UpdateRange()
{
	if (RangePanel) RangePanel->Show(RangeShots, Round->Club, Clubs[ClubIndex(Round->Club)].Name);
	if (!RangeDots) return;
	RangeDots->ClearInstances();
	int32 Shown = 0;
	for (int32 i = RangeShots.Num() - 1; i >= 0 && Shown < 10; i--)
	{
		const FOTGRangeShot& S = RangeShots[i];
		if (S.Club != Round->Club) continue;
		Shown++;
		RangeDots->AddInstance(FTransform(FRotator::ZeroRotator, Round->World(Round->OnGround(S.RestX, S.RestZ)) + FVector(0, 0, 2), FVector(0.7, 0.7, 0.02)), true);
	}
}
