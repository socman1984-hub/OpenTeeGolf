// The round's shots (see OTGRound.h; the web version's main.js onShot, finishShot): a shot played on the
// course's physics, where it finished, penalties and relief, holing out and the next hole.

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

// ------------------------------------------------------------------ shots

void AOTGRound::OnShot(const FOTGShot& Shot)
{
	if (bPaused)
	{
		Toast(TEXT("Paused: the shot wasn't played"));
		return;
	}
	// A swing while the last ball is still moving: finish that one at once, then play this one
	// from where it stops (rather than losing it)
	if (Phase == EPhase::Flying || Phase == EPhase::Resting)
	{
		PendingShot = Shot;
		if (Phase == EPhase::Flying) AnimT = 1e9;
		else PhaseTimer = 0;
		Toast(TEXT("New shot: skipping to where the last ball stopped"));
		return;
	}
	if (Phase != EPhase::Ready)
	{
		Toast(TEXT("Hole finished: Enter for the next one"));
		return;
	}
	LastShot = Shot;
	const otg::LieEffect Fx = otg::LieEffectFor(Lie);
	otg::Shot Play;
	Play.speedMph = Shot.SpeedMph * Fx.speed;
	Play.vla = Shot.VLA;
	Play.hla = Shot.HLA;
	Play.backSpin = Shot.BackSpin * Fx.spin;
	Play.sideSpin = Shot.SideSpin * Fx.spin;
	otg::ShotOptions O;
	O.heading = Heading;
	O.altitudeM = Data->AltitudeM;
	// the wind for this shot (the hole's, with a gust), and the air and the ground
	{
		double Mps, FromDeg;
		HoleWind(Mps, FromDeg);
		if (Mps > 0)
		{
			double GustMph, Wobble;
			Weather.Gust(GustMph, Wobble);
			Mps = GustMph * 0.44704;
			FromDeg += Wobble;
		}
		O.windMps = Mps;
		O.windFromDeg = FromDeg;
		O.tempC = Weather.TempC;
		O.humidity = Weather.Humidity;
		O.pressureHpa = Weather.PressureHpa;
		O.wet = Weather.Wetness();
	}
	O.stimp = Data->DefaultStimp;
	O.hasPin = true;
	O.pin = Pin();
	O.trees = &Trees;
	O.collider = &Collider;
	O.random = []() { return double(FMath::FRand()); };
	if (bMini)
	{
		// (a putt rolls: no chipping over the boards; and the windmill's sails turn as it goes)
		Play.vla = FMath::Min(Play.vla, 4.0);
		if (AOTGMiniGolf* M = AOTGMiniGolf::In(GetWorld())) M->AddMovers(O);
	}
	const double T0 = FPlatformTime::Seconds();
	Res = otg::SimulateShot(Course, Ball, Play, O);
	bHasRes = true;
	UE_LOG(LogOTGRound, Display, TEXT("Shot from %s: %s -> %s, carry %.1f m, total %.1f m (%.0f ms)"), *SurfaceDisplay(Lie), *OTGShotLine(Shot),
		UTF8_TO_TCHAR(Res.result.c_str()), Res.carry, Res.total, (FPlatformTime::Seconds() - T0) * 1000);
	Undo = FUndo{ Ball, Strokes, Lie, Heading };
	PrevBall = Ball;
	Strokes++;
	Phase = EPhase::Flying;
	AnimT = 0;
	bCarryShown = false;
	ShotFrom = World(Ball);
	ClearTracer();
	AimMesh->SetVisibility(false);
	TellLaunchMonitor(false);
	ShowShotData(false);
	if (Lie != otg::Surface::Tee && Fx.speed < 1)
		Toast(FString::Printf(TEXT("From the %s: -%d%% ball speed, -%d%% spin"), *SurfaceDisplay(Lie).ToLower(), FMath::RoundToInt((1 - Fx.speed) * 100), FMath::RoundToInt((1 - Fx.spin) * 100)), 3.f);
	UpdateHudInfo();
}

void AOTGRound::FinishShot()
{
	if (bRange) return RangeShotDone();
	const otg::Surface Surface = Course.SurfaceAt(Res.rest.x, Res.rest.z);
	// (into the water: a splash and rings where it went in)
	if (Res.result == "water" || Surface == otg::Surface::Water) AddRipple(World(Res.rest));
	FString Title, Small;
	if (Res.result == "holed")
	{
		Ball = Res.rest;
		PlaceBall(Ball);
		return HoleOut(false, FString());
	}
	// (putt-putt: anywhere off the felt is out, and the pond's water too: from where it was)
	if (bMini && (Res.result != "rest" || Surface != otg::Surface::Green))
	{
		Strokes++;
		Ball = PrevBall;
		Title = Res.result == "water" || Surface == otg::Surface::Water ? TEXT("In the pond") : TEXT("Off the course");
		Small = TEXT("+1 stroke: play again from where you were");
	}
	else if (Res.result == "oob" || Surface == otg::Surface::Oob || Surface == otg::Surface::None)
	{
		Strokes++; // stroke and distance
		Ball = PrevBall;
		Title = TEXT("Out of bounds");
		Small = TEXT("Stroke and distance: replay from the previous spot");
	}
	else if (Res.result == "water" || Surface == otg::Surface::Water)
	{
		Strokes++;
		WaterDrop(Ball);
		Title = TEXT("Penalty area");
		Small = TEXT("+1 stroke: drop where it went in");
	}
	else Ball = Res.rest;
	Lie = Course.SurfaceAt(Ball.x, Ball.z);
	if (!Playable(Lie)) Lie = otg::Surface::Rough;
	// Free relief from a cart path: the nearest spot off the path, no nearer the hole
	if (Lie == otg::Surface::Path && Title.IsEmpty())
	{
		otg::V3 Drop;
		if (PathRelief(Ball, Drop))
		{
			Ball = Drop;
			Lie = Course.SurfaceAt(Ball.x, Ball.z);
			ReliefNote = TEXT("Free relief from the cart path");
		}
	}
	if (!Title.IsEmpty()) Banner(Title, Small, 2.6f);
	else
	{
		const otg::Surface Landed = Res.hasCarryPoint ? Course.SurfaceAt(Res.carryPoint.x, Res.carryPoint.z) : otg::Surface::None;
		auto Has = [&](const char* K) { for (const std::string& H : Res.treeHits) if (H == K) return true; return false; };
		const FString Tree = Has("pole") ? TEXT("Off the flagstick! ") : Has("building") ? TEXT("Off the building! ") : Has("trunk") ? TEXT("Off the trunk! ")
			: Has("branch") ? TEXT("Hit a branch! ") : Has("leaves") ? TEXT("Clipped the leaves. ") : TEXT("");
		if (bMini) { if (Has("blade")) Toast(TEXT("Blocked by the windmill!"), 2.5f); }
		else if (!ReliefNote.IsEmpty()) Toast(FString::Printf(TEXT("%s%s: dropped in the %s"), *Tree, *ReliefNote, *SurfaceDisplay(Lie).ToLower()), 4.f);
		else if (Landed != otg::Surface::None && Landed != Lie) Toast(FString::Printf(TEXT("%sLanded: %s, finished: %s"), *Tree, *SurfaceDisplay(Landed), *SurfaceDisplay(Lie)), 3.5f);
		else Toast(FString::Printf(TEXT("%sIn the %s"), *Tree, *SurfaceDisplay(Lie).ToLower()), Tree.IsEmpty() ? 2.5f : 3.5f);
		ReliefNote.Reset();
	}
	PlaceBall(Ball);
	ShowShotData(true);
	if (Strokes >= MaxStrokes()) return HoleOut(true, TEXT("Maximum strokes reached: picked up"));
	Phase = EPhase::Resting;
	PhaseTimer = PendingShot ? 0.0 : Title.IsEmpty() ? 1.6 : 2.4;
	UpdateHudInfo();
}

void AOTGRound::NextShot()
{
	Phase = EPhase::Ready;
	if (OnGreen()) ClearTracer();
	ResetAim(true);
	AddressView(false);
	UpdateHudInfo();
	TellLaunchMonitor(true);
	if (PendingShot)
	{
		const FOTGShot S = *PendingShot;
		PendingShot.Reset();
		OnShot(S);
	}
}

// The nearest point off the cart path (searching outward), not closer to the hole
bool AOTGRound::PathRelief(const otg::V3& B, otg::V3& Out) const
{
	const otg::V3 P = Pin();
	const double D0 = Dist(B, P);
	for (double R = 0.5; R <= 8; R += 0.25)
	{
		bool bFound = false;
		double BX = 0, BZ = 0, BD = 0;
		for (int32 K = 0; K < 24; K++)
		{
			const double A = K / 24.0 * UE_DOUBLE_TWO_PI, X = B.x + FMath::Cos(A) * R, Z = B.z + FMath::Sin(A) * R;
			const otg::Surface S = Course.SurfaceAt(X, Z);
			if (!Playable(S) || S == otg::Surface::Path) continue;
			const double DD = FMath::Sqrt((X - P.x) * (X - P.x) + (Z - P.z) * (Z - P.z));
			if (DD < D0 - 0.01) continue;
			if (!bFound || DD < BD) { bFound = true; BX = X; BZ = Z; BD = DD; }
		}
		if (bFound)
		{
			// a little further, clear of the edge
			const double EX = BX + (BX - B.x) / R * 0.3, EZ = BZ + (BZ - B.z) / R * 0.3;
			const bool bOk = Course.SurfaceAt(EX, EZ) != otg::Surface::Path;
			Out = OnGround(bOk ? EX : BX, bOk ? EZ : BZ);
			return true;
		}
	}
	return false;
}

// The drop zone of the water it went in (the export's), or where it last crossed into the water,
// moved back toward where it was hit until clear of it
bool AOTGRound::WaterDrop(otg::V3& Out) const
{
	const FOTGDropZone* Best = nullptr;
	double BestD = 1e18;
	for (const FOTGDropZone& Z : Data->DropZones)
	{
		const bool bIn = Z.bHasBox && Res.rest.x >= Z.Box.X - 5 && Res.rest.x <= Z.Box.Z + 5 && Res.rest.z >= Z.Box.Y - 5 && Res.rest.z <= Z.Box.W + 5;
		const double D = FMath::Square(Z.Position.X - Res.rest.x) + FMath::Square(Z.Position.Z - Res.rest.z);
		if ((bIn ? D - 1e12 : D) < BestD) { BestD = bIn ? D - 1e12 : D; Best = &Z; }
	}
	if (Best && (Best->bHasBox ? BestD < 0 : BestD < 200 * 200))
	{
		Out = OnGround(Best->Position.X, Best->Position.Z);
		return true;
	}
	const std::vector<otg::PathPoint>& Pts = Res.points;
	for (int32 i = int32(Pts.size()) - 1; i >= 0; i--)
	{
		if (!Playable(Course.SurfaceAt(Pts[i].x, Pts[i].z))) continue;
		const double OX = PrevBall.x - Pts[i].x, OZ = PrevBall.z - Pts[i].z, OL = FMath::Max(1e-6, FMath::Sqrt(OX * OX + OZ * OZ));
		for (double Back = 2; Back < 40; Back += 1)
		{
			const double X = Pts[i].x + OX / OL * Back, Z = Pts[i].z + OZ / OL * Back;
			if (Playable(Course.SurfaceAt(X, Z))) { Out = OnGround(X, Z); return true; }
		}
		Out = OnGround(Pts[i].x, Pts[i].z);
		return true;
	}
	Out = PrevBall;
	return false;
}

void AOTGRound::HoleOut(bool bPickedUp, const FString& Note)
{
	const FOTGHole& H = Hole();
	PendingShot.Reset();
	const int32 Total = bPickedUp ? MaxStrokes() : Strokes;
	FOTGSide& Sd = Game->Sides[SideIdx];
	Sd.Attempts.Reset();
	Strokes = Total;
	SaveSide();
	Game->HoleOut(Sd, Total, bPickedUp);
	Sd.Ball = Pin();
	const FString ToPar = Total == H.Par ? TEXT("E") : FString::Printf(TEXT("%+d"), Total - H.Par);
	const FString Who = Game->Single() ? FString() : Sd.Name + TEXT(": ");
	const FString Title = Who + (bPickedUp ? FString(TEXT("Picked up")) : ScoreName(Total, H.Par));
	FString Small = FString::Printf(TEXT("Hole %d: %d (%s)%s"), H.Number, Total, *ToPar, Note.IsEmpty() ? TEXT("") : *(TEXT("  ·  ") + Note));
	UE_LOG(LogOTGRound, Display, TEXT("Hole %d: %s%d (%s)"), H.Number, *Who, Total, *ToPar);
	AimMesh->SetVisibility(false);
	// Someone else still to finish the hole: their turn next
	const int32 Next = Game->Next([this](const otg::V3& B) { return PinDist(B); });
	if (Next >= 0)
	{
		Phase = EPhase::Between;
		NextSide = Next;
		PhaseTimer = 2.4;
		Banner(Title, Small, 2.6f);
		TellLaunchMonitor(false);
		UpdateHudInfo();
		return;
	}
	// everyone's in
	Game->EndHole();
	Phase = EPhase::Holed;
	if (!Game->Single())
	{
		const FString News = Game->HoleNews(HoleIndex);
		if (!News.IsEmpty()) Small += TEXT("\n") + News;
		UE_LOG(LogOTGRound, Display, TEXT("%s"), *News);
	}
	Banner(Title, Small, 3.8f);
	TellLaunchMonitor(false);
	PhaseTimer = 2.6;
	bScorecardAuto = true;
	UpdateHudInfo();
}

void AOTGRound::NextHole()
{
	ShowScorecard(false);
	if (HoleIndex + 1 < LastHole) return StartHole(HoleIndex + 1);
	Phase = EPhase::Done;
	RecordRound();
	ShowScorecard(true);
	TellLaunchMonitor(false);
	UE_LOG(LogOTGRound, Display, TEXT("Round complete"));
}
