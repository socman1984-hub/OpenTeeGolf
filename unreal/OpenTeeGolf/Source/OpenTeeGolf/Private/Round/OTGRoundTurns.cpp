// Players taking turns (see OTGRound.h; the web version's main.js loadSide, saveSide, roundAfterShot,
// beginTurn): whoever's away plays next, the others' balls lie where they are, the round recorded.

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

// ------------------------------------------------------------------ players taking turns

// Side I plays: its ball, lie and strokes into the round's own, the others' balls where they lie
void FOTGRoundTurns::LoadSide(int32 I)
{
	SideIdx = FMath::Max(0, I);
	const FOTGSide& S = Game->Sides[SideIdx];
	Round->Ball = S.Ball; Round->PrevBall = S.PrevBall; Round->Lie = S.Lie; Round->Strokes = S.Strokes;
	if (Round->bMini && Round->Course.SurfaceAt(Round->Ball.x, Round->Ball.z) == otg::Surface::Green) Round->Lie = otg::Surface::Green;   // (the tee's on the felt)
	Round->Undo.Reset();
	Round->PlaceBall(Round->Ball);
	ShowOtherBalls();
}

// The playing side's ball, lie and strokes back into its own record
void FOTGRoundTurns::SaveSide()
{
	FOTGSide& S = Game->Sides[SideIdx];
	S.Ball = Round->Ball; S.PrevBall = Round->PrevBall; S.Lie = Round->Lie; S.Strokes = Round->Strokes;
}

void FOTGRoundTurns::ShowOtherBalls()
{
	for (int32 i = 0; i < OtherBalls.Num(); i++)
	{
		const FOTGSide& S = Game->Sides[i];
		// (not one sharing the spot with this ball - the same tee - where it would hide it)
		const bool bShow = i != SideIdx && !S.bHoled && Dist(S.Ball, Round->Ball) > 1.0;
		OtherBalls[i]->SetVisibility(bShow);
		if (bShow)
		{
			OtherBalls[i]->SetWorldScale3D(FVector(0.0427 * 2));
			OtherBalls[i]->SetWorldLocation(Round->World(S.Ball) + FVector(0, 0, 2.135 * 2));
		}
	}
}

// After a shot with several sides: the scramble's next player from the same spot (or the team's
// best shot once everyone's hit), then whoever's away. False: the same player plays on.
bool FOTGRoundTurns::RoundAfterShot()
{
	FOTGSide& Sd = Game->Sides[SideIdx];
	if (Game->Single())
	{
		SaveSide();
		Sd.Shots++;
		return false;
	}
	auto D = [this](const otg::V3& B) { return Round->PinDist(B); };
	if (Game->F.Shared == TEXT("scramble"))
	{
		FOTGAttempt A;
		A.Ball = Round->Ball; A.Lie = Round->Lie; A.Strokes = Round->Strokes;
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
		Round->HudView.Toast(FString::Printf(TEXT("%s plays %s's shot"), *Sd.Name, *Game->Players[Best->By].Name), 3.f);
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
void FOTGRoundTurns::BeginTurn(int32 I, bool bSameSpot)
{
	LoadSide(I);
	Round->Phase = AOTGRound::EPhase::Ready;
	Round->ClearTracer();
	Round->ResetAim(Round->Strokes > 0);
	Round->Camera.AddressView(false);
	Round->HudView.Banner(Game->SideLabel(SideIdx), bSameSpot ? FString(TEXT("from the same spot")) : FString::Printf(TEXT("%s to the pin  ·  %s"), Round->OnGreen() ? *Round->Putt(Round->DistToPin()) : *Round->Len(Round->DistToPin()), *SurfaceDisplay(Round->Lie)), 2.2f);
	Round->HudView.UpdateHudInfo();
	Round->TellLaunchMonitor(true);
	if (Round->PendingShot)
	{
		const FOTGShot S = *Round->PendingShot;
		Round->PendingShot.Reset();
		Round->OnShot(S);
	}
}

// Each player's round kept with their profile
void FOTGRoundTurns::RecordRound()
{
	if (Round->Setup_.Picked.IsEmpty()) return;
	TArray<FOTGPlayerProfile> Profiles = FOTGPlayers::Load();
	for (const FOTGGamePlayer& P : Game->Players)
	{
		const FOTGSide* S = nullptr;
		for (const FOTGSide& X : Game->Sides) for (int32 M : X.Members) if (Game->Players[M].Id == P.Id) S = &X;
		FOTGPlayerProfile* Prof = Profiles.FindByPredicate([&](const FOTGPlayerProfile& X) { return X.Id == P.Id; });
		if (!S || !Prof) continue;
		FOTGPlayedRound R;
		R.At = FDateTime::Now();
		R.Course = Round->Data->CourseName;
		R.Format = Game->F.Key;
		for (int32 H = 0; H < S->Scores.Num(); H++)
			if (S->Scores[H] >= 0) { R.Holes++; R.Strokes += S->Scores[H]; R.Par += Round->Data->Holes[H].Par; }
		if (R.Holes) Prof->Rounds.Add(R);
	}
	FOTGPlayers::Save(Profiles);
}
