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

void AOTGRound::UpdatePinTag()
{
	if (!Hud) return;
	const bool bWant = !bRange && !bMini && (Phase == EPhase::Ready || Phase == EPhase::Resting) && !bPaused;
	APlayerController* PC = bWant ? GetWorld()->GetFirstPlayerController() : nullptr;
	FVector2D Px;
	const otg::V3 P = Pin();
	// (over the flag, about its top: 2.5 m up)
	if (!PC || !PC->ProjectWorldLocationToScreen(World(otg::V3{ P.x, P.y + 2.5, P.z }), Px, true)) { Hud->SetPinTag(false, FVector2D::ZeroVector, FString(), FString(), FLinearColor::White); return; }
	// (the viewport's pixels to the HUD's own units)
	FVector2D Vp(1, 1);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Vp);
	const FVector2D Local = Hud->GetCachedGeometry().GetLocalSize();
	const double K = Local.X > 1 && Vp.X > 1 ? Local.X / Vp.X : 1.0;
	const FVector2D At = Px * K;
	if (At.X < 0 || At.Y < 0 || At.X > Local.X || At.Y > Local.Y) { Hud->SetPinTag(false, FVector2D::ZeroVector, FString(), FString(), FLinearColor::White); return; }
	const double D = DistToPin();
	const double Up = (P.y - Ball.y) * M_TO_FT;   // (the hole above the ball +, below -)
	const int32 Ft = FMath::RoundToInt(Up);
	const FString Elev = Ft == 0 ? FString(TEXT("level")) : FString::Printf(TEXT("%d ft %s"), FMath::Abs(Ft), Ft > 0 ? TEXT("uphill") : TEXT("downhill"));
	const FLinearColor C = Ft >= 1 ? FLinearColor(1.f, 0.62f, 0.45f) : Ft <= -1 ? FLinearColor(0.55f, 0.8f, 1.f) : FLinearColor(0.8f, 0.85f, 0.8f);
	Hud->SetPinTag(true, At, OnGreen() ? Putt(D) : Len(D), Elev, C);
}

void AOTGRound::UpdateMap()
{
	if (!Hud || !Hud->Minimap()) return;
	FOTGMapState S;
	S.bValid = true;
	const otg::V3 P = Pin(), T = TeeOf(Hole(), TeeName), A = AimPoint();
	S.Ball = FVector2D(BallShown.x, BallShown.z);
	S.Pin = FVector2D(P.x, P.z);
	S.Tee = FVector2D(T.x, T.z);
	S.Aim = FVector2D(A.x, A.z);
	S.bOnGreen = OnGreen();
	if (!Game->Single())
	{
		S.Ring = Game->Sides[SideIdx].Colour;
		for (int32 i = 0; i < Game->Sides.Num(); i++)
			if (i != SideIdx && !Game->Sides[i].bHoled) S.Others.Emplace(FVector2D(Game->Sides[i].Ball.x, Game->Sides[i].Ball.z), Game->Sides[i].Colour);
	}
	Hud->Minimap()->SetState(S);
}

// The wind box: an arrow against the aim (up: helping), its speed and gusts, how it plays, the weather
void AOTGRound::UpdateWindBox()
{
	if (!Hud || !Hud->Wind()) return;
	double Mps, FromDeg;
	HoleWind(Mps, FromDeg);
	const double TempF = Weather.TempC * 1.8 + 32;
	const FString Line2 = FString::Printf(TEXT("%s%s  ·  %d\u00B0F%s"), Weather.bLive ? TEXT("Live: ") : TEXT(""), *Weather.Label, FMath::RoundToInt(TempF), Weather.Wetness() > 0 ? TEXT("  ·  wet ground") : TEXT(""));
	if (Mps <= 0)
	{
		Hud->Wind()->Set(0, true, TEXT("Calm"), Line2);
		return;
	}
	const double BlowRel = FromDeg + 180 - FMath::RadiansToDegrees(Heading);
	const double Along = FMath::Cos(FMath::DegreesToRadians(BlowRel));
	const FString Feel = FMath::Abs(Along) < 0.4 ? TEXT("cross") : Along > 0 ? TEXT("helping") : TEXT("into you");
	const FString Gust = Weather.GustMph > Weather.WindMph + 3 ? FString::Printf(TEXT(", gusts %d"), FMath::RoundToInt(Weather.GustMph)) : FString();
	Hud->Wind()->Set(BlowRel, false, FString::Printf(TEXT("%d mph%s  ·  %s"), FMath::RoundToInt(Weather.WindMph), *Gust, *Feel), Line2);
}

void AOTGRound::UpdateHudInfo()
{
	if (!Hud || !Data) return;
	if (bRange)
	{
		UpdateWindBox();
		const FClub& RC = Clubs[ClubIndex(Club)];
		const FString R2 = Phase == EPhase::Flying ? FString::Printf(TEXT("Ball %d  ·  on its way"), RangeBalls + 1)
			: Phase == EPhase::Resting ? FString::Printf(TEXT("Ball %d  ·  a new ball in a moment (Enter: now)"), RangeBalls)
			: FString::Printf(TEXT("Ball %d  ·  on the mat"), RangeBalls + 1);
		Hud->SetInfo(TEXT("Driving range"), R2, FString::Printf(TEXT("%s  ·  aim %s"), RC.Name, *Len(AimDist)));
		return;
	}
	const FOTGHole& H = Hole();
	const double D = DistToPin();
	const FString L1 = bMini ? FString::Printf(TEXT("Hole %d  ·  %s  ·  Par %d"), H.Number, *H.Name, H.Par)
		: FString::Printf(TEXT("Hole %d  ·  Par %d  ·  %s"), H.Number, H.Par, *Len(Dist(TeeOf(H, TeeName), Pin())));
	FString L2;
	const FOTGSide& Sd = Game->Sides[SideIdx];
	const FString Who = Game->Single() ? FString() : Game->SideLabel(SideIdx) + TEXT("  ·  ");
	if (Phase == EPhase::Holed || Phase == EPhase::Done) L2 = FString::Printf(TEXT("%d stroke%s"), Sd.Scores[HoleIndex], Sd.Scores[HoleIndex] == 1 ? TEXT("") : TEXT("s"));
	else if (Phase == EPhase::Between) L2 = FString::Printf(TEXT("%sholed out"), *Who);
	else if (Phase == EPhase::Flying) L2 = FString::Printf(TEXT("%sStroke %d  ·  on its way"), *Who, Strokes);
	else if (bMini) L2 = FString::Printf(TEXT("%sStroke %d  ·  %s to the cup"), *Who, Strokes + 1, *Putt(D));
	else L2 = FString::Printf(TEXT("%sStroke %d  ·  %s to the pin"), *Who, Strokes + 1, OnGreen() ? *Putt(D) : *Len(D));
	// (the lie, on its own line in its colour - not in the air, nor after the hole)
	{
		const bool bLie = !bMini && (Phase == EPhase::Ready || Phase == EPhase::Resting);
		Hud->SetLie(bLie ? SurfaceDisplay(Lie) : FString(), LieColour(Lie));
	}
	const FClub& C = Clubs[ClubIndex(Club)];
	FString L3 = OnGreen() || Club == TEXT("PT") ? FString(C.Name) : FString::Printf(TEXT("%s  ·  aim %s"), C.Name, *Len(AimDist));
	UpdateWindBox();
	Hud->SetInfo(L1, L2, L3);
	// (players together: who's winning)
	if (!Game->Single())
	{
		FString Summary;
		Hud->SetStandings(Game->F.Name, Game->Standings(Summary), Summary);
	}
}

void AOTGRound::ShowShotData(bool bWithResult)
{
	if (!Hud) return;
	const FOTGShot& S = LastShot;
	const FString L1 = FString::Printf(TEXT("%.1f mph  ·  %.1f%s up  ·  %s  ·  %s rpm  ·  axis %s"), S.SpeedMph, S.VLA, DEG, *Side(S.HLA),
		*FText::AsNumber(FMath::RoundToInt(S.TotalSpin)).ToString(), *Side(S.SpinAxis));
	FString L2;
	if (bHasRes && (bCarryShown || bWithResult))
	{
		const bool bPutt = Undo && Undo->Lie == otg::Surface::Green;
		L2 = bPutt ? FString::Printf(TEXT("Rolled %s"), *Putt(Res.total)) : FString::Printf(TEXT("Carry %s"), *Len(Res.carry));
		if (bWithResult && !bPutt)
		{
			L2 += FString::Printf(TEXT("  ·  Total %s"), *Len(Res.total));
			if (FMath::Abs(Res.offline) >= 0.5) L2 += FString::Printf(TEXT("  ·  %d yd %s"), FMath::RoundToInt(FMath::Abs(Res.offline) * M_TO_YD), Res.offline > 0 ? TEXT("right") : TEXT("left"));
			L2 += FString::Printf(TEXT("  ·  apex %d ft"), FMath::RoundToInt(Res.apex * M_TO_FT));
		}
	}
	Hud->SetShotData(FString(), FString());
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
	if (bHasRes && (bCarryShown || bWithResult))
	{
		const bool bPutt = Undo && Undo->Lie == otg::Surface::Green;
		if (bPutt) V.Add(TEXT("total"), Putt(Res.total));
		else
		{
			V.Add(TEXT("carry"), Len(Res.carry));
			if (bWithResult)
			{
				V.Add(TEXT("total"), Len(Res.total));
				const int32 Off = FMath::RoundToInt(FMath::Abs(Res.offline) * M_TO_YD);
				V.Add(TEXT("offline"), Off == 0 ? FString(TEXT("0 yd")) : FString::Printf(TEXT("%d yd %s"), Off, Res.offline > 0 ? TEXT("R") : TEXT("L")));
				V.Add(TEXT("apex"), FString::Printf(TEXT("%d ft"), FMath::RoundToInt(Res.apex * M_TO_FT)));
			}
		}
	}
	(void)L1; (void)L2;
	Hud->SetShotValues(V);
}

void AOTGRound::Banner(const FString& Title, const FString& Small, float Seconds)
{
	if (Hud) Hud->Banner(Title, Small, Seconds);
}

void AOTGRound::Toast(const FString& Text, float Seconds)
{
	if (Hud) Hud->Toast(Text, Seconds);
	UE_LOG(LogOTGRound, Display, TEXT("%s"), *Text);
}

void AOTGRound::ShowScorecard(bool bShow)
{
	bScorecard = bShow;
	if (!Hud) return;
	if (!bShow) return Hud->HideScorecard();
	FOTGCard Card;
	Card.Course = Data->CourseName;
	Card.Format = Game->Single() ? FString() : Game->F.Name;
	Card.bFinal = Phase == EPhase::Done;
	for (int32 i = 0; i < Data->Holes.Num(); i++) Card.Holes.Add(FOTGScoreRow{ Data->Holes[i].Number, Data->Holes[i].Par });
	for (const FOTGSide& S : Game->Sides) Card.Sides.Add(FOTGCard::FSide{ Game->Single() ? FString(TEXT("Score")) : S.Name, S.Colour, S.Scores });
	if (!Game->Single()) Card.Standings = Game->Standings(Card.Summary);
	Hud->ShowScorecard(Card);
}
