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
