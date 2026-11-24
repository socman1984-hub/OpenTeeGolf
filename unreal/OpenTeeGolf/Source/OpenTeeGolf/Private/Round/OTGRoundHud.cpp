// The round's HUD wiring (see OTGRound.h): what the HUD (SOTGHud) shows of the round - the hole, the
// shot's numbers, the pin's yardage, the minimap, the wind box, banners, the scorecard.

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

void FOTGRoundHud::UpdatePinTag()
{
	if (!Round->Hud) return;
	const bool bWant = !Round->bRange && !Round->bMini && (Round->Phase == AOTGRound::EPhase::Ready || Round->Phase == AOTGRound::EPhase::Resting) && !Round->bPaused;
	APlayerController* PC = bWant ? Round->GetWorld()->GetFirstPlayerController() : nullptr;
	FVector2D Px;
	const otg::V3 P = Round->Pin();
	// (over the flag, about its top: 2.5 m up)
	if (!PC || !PC->ProjectWorldLocationToScreen(Round->World(otg::V3{ P.x, P.y + 2.5, P.z }), Px, true)) { Round->Hud->SetPinTag(false, FVector2D::ZeroVector, FString(), FString(), FLinearColor::White); return; }
	// (the viewport's pixels to the HUD's own units)
	FVector2D Vp(1, 1);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Vp);
	const FVector2D Local = Round->Hud->GetCachedGeometry().GetLocalSize();
	const double K = Local.X > 1 && Vp.X > 1 ? Local.X / Vp.X : 1.0;
	const FVector2D At = Px * K;
	if (At.X < 0 || At.Y < 0 || At.X > Local.X || At.Y > Local.Y) { Round->Hud->SetPinTag(false, FVector2D::ZeroVector, FString(), FString(), FLinearColor::White); return; }
	const double D = Round->DistToPin();
	const double Up = (P.y - Round->Ball.y) * M_TO_FT;   // (the hole above the ball +, below -)
	const int32 Ft = FMath::RoundToInt(Up);
	const FString Elev = Ft == 0 ? FString(TEXT("level")) : FString::Printf(TEXT("%d ft %s"), FMath::Abs(Ft), Ft > 0 ? TEXT("uphill") : TEXT("downhill"));
	const FLinearColor C = Ft >= 1 ? FLinearColor(1.f, 0.62f, 0.45f) : Ft <= -1 ? FLinearColor(0.55f, 0.8f, 1.f) : FLinearColor(0.8f, 0.85f, 0.8f);
	Round->Hud->SetPinTag(true, At, Round->OnGreen() ? Round->Putt(D) : Round->Len(D), Elev, C);
}

void FOTGRoundHud::UpdateMap()
{
	if (!Round->Hud || !Round->Hud->Minimap()) return;
	FOTGMapState S;
	S.bValid = true;
	const otg::V3 P = Round->Pin(), T = Round->TeeOf(Round->Hole(), Round->TeeName), A = Round->AimPoint();
	S.Ball = FVector2D(Round->BallShown.x, Round->BallShown.z);
	S.Pin = FVector2D(P.x, P.z);
	S.Tee = FVector2D(T.x, T.z);
	S.Aim = FVector2D(A.x, A.z);
	S.bOnGreen = Round->OnGreen();
	if (!Round->Turns.Game->Single())
	{
		S.Ring = Round->Turns.Game->Sides[Round->Turns.SideIdx].Colour;
		for (int32 i = 0; i < Round->Turns.Game->Sides.Num(); i++)
			if (i != Round->Turns.SideIdx && !Round->Turns.Game->Sides[i].bHoled) S.Others.Emplace(FVector2D(Round->Turns.Game->Sides[i].Ball.x, Round->Turns.Game->Sides[i].Ball.z), Round->Turns.Game->Sides[i].Colour);
	}
	Round->Hud->Minimap()->SetState(S);
}

// The wind box: an arrow against the aim (up: helping), its speed and gusts, how it plays, the weather
void FOTGRoundHud::UpdateWindBox()
{
	if (!Round->Hud || !Round->Hud->Wind()) return;
	double Mps, FromDeg;
	Round->HoleWind(Mps, FromDeg);
	const double TempF = Round->Weather.TempC * 1.8 + 32;
	const FString Line2 = FString::Printf(TEXT("%s%s  ·  %d\u00B0F%s"), Round->Weather.bLive ? TEXT("Live: ") : TEXT(""), *Round->Weather.Label, FMath::RoundToInt(TempF), Round->Weather.Wetness() > 0 ? TEXT("  ·  wet ground") : TEXT(""));
	if (Mps <= 0)
	{
		Round->Hud->Wind()->Set(0, true, TEXT("Calm"), Line2);
		return;
	}
	const double BlowRel = FromDeg + 180 - FMath::RadiansToDegrees(Round->Heading);
	const double Along = FMath::Cos(FMath::DegreesToRadians(BlowRel));
	const FString Feel = FMath::Abs(Along) < 0.4 ? TEXT("cross") : Along > 0 ? TEXT("helping") : TEXT("into you");
	const FString Gust = Round->Weather.GustMph > Round->Weather.WindMph + 3 ? FString::Printf(TEXT(", gusts %d"), FMath::RoundToInt(Round->Weather.GustMph)) : FString();
	Round->Hud->Wind()->Set(BlowRel, false, FString::Printf(TEXT("%d mph%s  ·  %s"), FMath::RoundToInt(Round->Weather.WindMph), *Gust, *Feel), Line2);
}

void FOTGRoundHud::UpdateHudInfo()
{
	if (!Round->Hud || !Round->Data) return;
	if (Round->bRange)
	{
		UpdateWindBox();
		const FClub& RC = Clubs[ClubIndex(Round->Club)];
		const FString R2 = Round->Phase == AOTGRound::EPhase::Flying ? FString::Printf(TEXT("Ball %d  ·  on its way"), Round->RangeMode.RangeBalls + 1)
			: Round->Phase == AOTGRound::EPhase::Resting ? FString::Printf(TEXT("Ball %d  ·  a new ball in a moment (Enter: now)"), Round->RangeMode.RangeBalls)
			: FString::Printf(TEXT("Ball %d  ·  on the mat"), Round->RangeMode.RangeBalls + 1);
		Round->Hud->SetInfo(TEXT("Driving range"), R2, FString::Printf(TEXT("%s  ·  aim %s"), RC.Name, *Round->Len(Round->AimDist)));
		return;
	}
	const FOTGHole& H = Round->Hole();
	const double D = Round->DistToPin();
	const FString L1 = Round->bMini ? FString::Printf(TEXT("Hole %d  ·  %s  ·  Par %d"), H.Number, *H.Name, H.Par)
		: FString::Printf(TEXT("Hole %d  ·  Par %d  ·  %s"), H.Number, H.Par, *Round->Len(Dist(Round->TeeOf(H, Round->TeeName), Round->Pin())));
	FString L2;
	const FOTGSide& Sd = Round->Turns.Game->Sides[Round->Turns.SideIdx];
	const FString Who = Round->Turns.Game->Single() ? FString() : Round->Turns.Game->SideLabel(Round->Turns.SideIdx) + TEXT("  ·  ");
	if (Round->Phase == AOTGRound::EPhase::Holed || Round->Phase == AOTGRound::EPhase::Done) L2 = FString::Printf(TEXT("%d stroke%s"), Sd.Scores[Round->HoleIndex], Sd.Scores[Round->HoleIndex] == 1 ? TEXT("") : TEXT("s"));
	else if (Round->Phase == AOTGRound::EPhase::Between) L2 = FString::Printf(TEXT("%sholed out"), *Who);
	else if (Round->Phase == AOTGRound::EPhase::Flying) L2 = FString::Printf(TEXT("%sStroke %d  ·  on its way"), *Who, Round->Strokes);
	else if (Round->bMini) L2 = FString::Printf(TEXT("%sStroke %d  ·  %s to the cup"), *Who, Round->Strokes + 1, *Round->Putt(D));
	else L2 = FString::Printf(TEXT("%sStroke %d  ·  %s to the pin"), *Who, Round->Strokes + 1, Round->OnGreen() ? *Round->Putt(D) : *Round->Len(D));
	// (the lie, on its own line in its colour - not in the air, nor after the hole)
	{
		const bool bLie = !Round->bMini && (Round->Phase == AOTGRound::EPhase::Ready || Round->Phase == AOTGRound::EPhase::Resting);
		Round->Hud->SetLie(bLie ? SurfaceDisplay(Round->Lie) : FString(), LieColour(Round->Lie));
	}
	const FClub& C = Clubs[ClubIndex(Round->Club)];
	FString L3 = Round->OnGreen() || Round->Club == TEXT("PT") ? FString(C.Name) : FString::Printf(TEXT("%s  ·  aim %s"), C.Name, *Round->Len(Round->AimDist));
	UpdateWindBox();
	Round->Hud->SetInfo(L1, L2, L3);
	// (players together: who's winning)
	if (!Round->Turns.Game->Single())
	{
		FString Summary;
		Round->Hud->SetStandings(Round->Turns.Game->F.Name, Round->Turns.Game->Standings(Summary), Summary);
	}
}

void FOTGRoundHud::ShowShotData(bool bWithResult)
{
	if (!Round->Hud) return;
	const FOTGShot& S = Round->LastShot;
	const FString L1 = FString::Printf(TEXT("%.1f mph  ·  %.1f%s up  ·  %s  ·  %s rpm  ·  axis %s"), S.SpeedMph, S.VLA, DEG, *Side(S.HLA),
		*FText::AsNumber(FMath::RoundToInt(S.TotalSpin)).ToString(), *Side(S.SpinAxis));
	FString L2;
	if (Round->bHasRes && (Round->bCarryShown || bWithResult))
	{
		const bool bPutt = Round->Undo && Round->Undo->Lie == otg::Surface::Green;
		L2 = bPutt ? FString::Printf(TEXT("Rolled %s"), *Round->Putt(Round->Res.total)) : FString::Printf(TEXT("Carry %s"), *Round->Len(Round->Res.carry));
		if (bWithResult && !bPutt)
		{
			L2 += FString::Printf(TEXT("  ·  Total %s"), *Round->Len(Round->Res.total));
			if (FMath::Abs(Round->Res.offline) >= 0.5) L2 += FString::Printf(TEXT("  ·  %d yd %s"), FMath::RoundToInt(FMath::Abs(Round->Res.offline) * M_TO_YD), Round->Res.offline > 0 ? TEXT("right") : TEXT("left"));
			L2 += FString::Printf(TEXT("  ·  apex %d ft"), FMath::RoundToInt(Round->Res.apex * M_TO_FT));
		}
	}
	Round->Hud->SetShotData(FString(), FString());
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
	if (Round->bHasRes && (Round->bCarryShown || bWithResult))
	{
		const bool bPutt = Round->Undo && Round->Undo->Lie == otg::Surface::Green;
		if (bPutt) V.Add(TEXT("total"), Round->Putt(Round->Res.total));
		else
		{
			V.Add(TEXT("carry"), Round->Len(Round->Res.carry));
			if (bWithResult)
			{
				V.Add(TEXT("total"), Round->Len(Round->Res.total));
				const int32 Off = FMath::RoundToInt(FMath::Abs(Round->Res.offline) * M_TO_YD);
				V.Add(TEXT("offline"), Off == 0 ? FString(TEXT("0 yd")) : FString::Printf(TEXT("%d yd %s"), Off, Round->Res.offline > 0 ? TEXT("R") : TEXT("L")));
				V.Add(TEXT("apex"), FString::Printf(TEXT("%d ft"), FMath::RoundToInt(Round->Res.apex * M_TO_FT)));
			}
		}
	}
	(void)L1; (void)L2;
	Round->Hud->SetShotValues(V);
}

void FOTGRoundHud::Banner(const FString& Title, const FString& Small, float Seconds)
{
	if (Round->Hud) Round->Hud->Banner(Title, Small, Seconds);
}

void FOTGRoundHud::Toast(const FString& Text, float Seconds)
{
	if (Round->Hud) Round->Hud->Toast(Text, Seconds);
	UE_LOG(LogOTGRound, Display, TEXT("%s"), *Text);
}

void FOTGRoundHud::ShowScorecard(bool bShow)
{
	Round->bScorecard = bShow;
	if (!Round->Hud) return;
	if (!bShow) return Round->Hud->HideScorecard();
	FOTGCard Card;
	Card.Course = Round->Data->CourseName;
	Card.Format = Round->Turns.Game->Single() ? FString() : Round->Turns.Game->F.Name;
	Card.bFinal = Round->Phase == AOTGRound::EPhase::Done;
	for (int32 i = 0; i < Round->Data->Holes.Num(); i++) Card.Holes.Add(FOTGScoreRow{ Round->Data->Holes[i].Number, Round->Data->Holes[i].Par });
	for (const FOTGSide& S : Round->Turns.Game->Sides) Card.Sides.Add(FOTGCard::FSide{ Round->Turns.Game->Single() ? FString(TEXT("Score")) : S.Name, S.Colour, S.Scores });
	if (!Round->Turns.Game->Single()) Card.Standings = Round->Turns.Game->Standings(Card.Summary);
	Round->Hud->ShowScorecard(Card);
}
