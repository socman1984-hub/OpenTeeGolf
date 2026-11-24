// The round's keys (see OTGRound.h; AOTGPlayerController sends them): aiming, the club, the test shot,
// continuing, clicks on the course, the test panels, the scorecard, a mulligan; and the testing aids.

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

// ------------------------------------------------------------------ the keys

void AOTGRound::AimBy(double Degrees)
{
	if (Phase != EPhase::Ready) return;
	Heading += FMath::DegreesToRadians(Degrees);
	ShowAim();
	Camera.AddressView(false);
}

void AOTGRound::ChangeClub(int32 Step)
{
	if (bRange && Phase == EPhase::Resting) RangeMode.NewBall();   // (the range: a new ball for the new club)
	if (Phase != EPhase::Ready) return;
	if (bMini) { HudView.Toast(TEXT("Putt-putt: putter only"), 1.2f); return; }
	const int32 I = FMath::Clamp(ClubIndex(Club) - Step, 0, NumClubs - (bRange ? 2 : 1));   // (no putter on the range)
	bClubPicked = true;
	SetClub(Clubs[I].Code);
	HudView.Toast(Clubs[I].Name, 1.2f);
	if (bRange)
	{
		AimDist = RangeMode.ClubCarryM();
		ShowAim();
		RangeMode.UpdateRange();
	}
}

void AOTGRound::TestShot()
{
	// A strike as a launch monitor would send it: the club's typical one, or for a wedge (or a putt)
	// one sized to the distance, a little off line (only on the player's turn: it's made for their
	// club and distance)
	if (bRange && Phase == EPhase::Resting && !bPaused) RangeMode.NewBall();   // (the range: a new ball, and hit it)
	if (Phase != EPhase::Ready || bPaused)
	{
		HudView.Toast(TEXT("Test shot: wait for your turn"), 1.5f);
		return;
	}
	const FClub& C = Clubs[ClubIndex(Club)];
	FOTGBallData B;
	const double D = bRange ? AimDist : DistToPin();
	if (Club == TEXT("PT"))
	{
		const double Mps = PuttMps(0);
		B.Speed = Mps * 2.23694 * FMath::FRandRange(0.97, 1.05);
		B.VLA = 1.5;
		B.HLA = FMath::FRandRange(-1.0, 1.0);
		B.TotalSpin = 0;
		B.SpinAxis = 0;
	}
	else
	{
		const double Yd = D * M_TO_YD;
		const double Scale = C.CarryYd > Yd && C.CarryYd > 0 && ClubIndex(Club) >= ClubIndex(TEXT("PW")) ? FMath::Sqrt(FMath::Max(0.15, Yd / C.CarryYd)) : 1.0;
		B.Speed = C.Mph * Scale * FMath::FRandRange(0.98, 1.02);
		B.VLA = C.Vla * FMath::FRandRange(0.95, 1.05);
		B.HLA = FMath::FRandRange(-2.5, 2.5);
		B.TotalSpin = C.Spin * FMath::FRandRange(0.9, 1.1);
		B.SpinAxis = FMath::FRandRange(-8.0, 8.0);
	}
	OnShot(OTGToShot(B, TEXT("test"), TEXT("test shot")));
}

void AOTGRound::Continue()
{
	if (Phase == EPhase::Flying) AnimT = 1e9;
	else if (bRange && Phase == EPhase::Resting) RangeMode.NewBall();
	else if (Phase == EPhase::Holed) NextHole();
	else if (Phase == EPhase::Done) QuitToMenu();
}

void AOTGRound::DropNearPin(double Yards)
{
	if (Phase != EPhase::Ready) return;
	const otg::V3 P = Pin();
	const double D = FMath::Max(0.1, DistToPin()), M = FMath::Max(0.3, Yards / M_TO_YD);
	Ball = OnGround(P.x + (Ball.x - P.x) / D * M, P.z + (Ball.z - P.z) / D * M);
	Lie = Course.SurfaceAt(Ball.x, Ball.z);
	if (!Playable(Lie)) Lie = otg::Surface::Rough;
	ClearTracer();
	PlaceBall(Ball);
	ResetAim(true);
	Camera.AddressView(true);
	HudView.UpdateHudInfo();
	HudView.Toast(FString::Printf(TEXT("(testing) dropped %s from the pin, %s"), *Len(M), *SurfaceDisplay(Lie).ToLower()));
}

void AOTGRound::SurfaceTest()
{
	// Each club's typical strike, still air, fanned 60 degrees either side of the aim and in 4 lengths
	// (90-105% speed), from the ball: carry, roll on and first bounce, grouped by where each lands
	struct FRow { int32 N = 0; double Carry = 0, Roll = 0, Hop = 0, RollMax = 0; };
	const TCHAR* Codes[] = { TEXT("DR"), TEXT("H4"), TEXT("I7"), TEXT("PW") };
	const otg::Surface Order[] = { otg::Surface::Green, otg::Surface::Fringe, otg::Surface::Fairway, otg::Surface::Tee, otg::Surface::FirstCut, otg::Surface::Rough,
		otg::Surface::DeepRough, otg::Surface::Native, otg::Surface::Sand, otg::Surface::Path, otg::Surface::Water };
	otg::ShotOptions O;
	O.altitudeM = Data->AltitudeM;
	O.tempC = Weather.TempC; O.humidity = Weather.Humidity; O.pressureHpa = Weather.PressureHpa; O.wet = Weather.Wetness();
	O.stimp = Data->DefaultStimp;
	O.trees = &Trees;
	O.collider = &Collider;
	UE_LOG(LogOTGRound, Display, TEXT("SurfaceTest from %s (wet %.2f): yards, roll = rest minus landing, hop = first bounce in m"), *SurfaceDisplay(Lie), O.wet);
	for (const TCHAR* Code : Codes)
	{
		const FClub& C = Clubs[ClubIndex(Code)];
		TMap<uint8, FRow> Rows;
		for (int32 A = -40; A <= 40; A++)
			for (int32 L = 0; L < 4; L++)
			{
				otg::Shot S;
				S.speedMph = C.Mph * (0.9 + 0.05 * L); S.vla = C.Vla; S.hla = 0; S.backSpin = C.Spin; S.sideSpin = 0;
				O.heading = Heading + A * 1.5 * PI / 180;
				const otg::ShotResult R = otg::SimulateShot(Course, Ball, S, O);
				if (!R.hasCarryPoint) continue;
				const otg::Surface Landed = Course.SurfaceAt(R.carryPoint.x, R.carryPoint.z);
				double Hop = 0;
				for (const otg::PathPoint& P : R.points) if (P.t > R.carryPoint.t) Hop = FMath::Max(Hop, P.y - R.carryPoint.y);
				const double Roll = FMath::Sqrt(FMath::Square(R.rest.x - R.carryPoint.x) + FMath::Square(R.rest.z - R.carryPoint.z)) * M_TO_YD;
				FRow& Row = Rows.FindOrAdd(uint8(Landed));
				Row.N++; Row.Carry += R.carry * M_TO_YD; Row.Roll += Roll; Row.Hop = FMath::Max(Row.Hop, Hop); Row.RollMax = FMath::Max(Row.RollMax, Roll);
			}
		for (otg::Surface Su : Order)
			if (const FRow* Row = Rows.Find(uint8(Su)))
				UE_LOG(LogOTGRound, Display, TEXT("SurfaceTest %-8s landed %-10s n=%3d  carry %5.0f  roll %5.1f (max %5.1f)  hop %4.2f"), C.Name, *SurfaceDisplay(Su), Row->N,
					Row->Carry / Row->N, Row->Roll / Row->N, Row->RollMax, Row->Hop);
	}
}

void AOTGRound::CastTest()
{
	auto Report = [&](const TCHAR* What, const otg::V3& From, const otg::V3& To) {
		otg::Hit H;
		const bool bHit = Collider.Cast(From, To, false, H);
		UE_LOG(LogOTGRound, Display, TEXT("CastTest %s: %s at (%.2f, %.2f, %.2f), normal (%.2f, %.2f, %.2f)"), What, bHit ? UTF8_TO_TCHAR(H.kind.c_str()) : TEXT("nothing"),
			H.point.x, H.point.y, H.point.z, H.normal.x, H.normal.y, H.normal.z);
	};
	const otg::V3 P = Pin();
	Collider.SetPin(&P);
	// at the flagstick, a metre up, from 5 m short of it
	const double D = FMath::Max(0.1, DistToPin());
	const double UX = (P.x - Ball.x) / D, UZ = (P.z - Ball.z) / D;
	Report(TEXT("flagstick"), otg::V3{ P.x - UX * 5, P.y + 1, P.z - UZ * 5 }, otg::V3{ P.x + UX, P.y + 1, P.z + UZ });
	// at the nearest trunk, a metre up
	const otg::Tree* Near = nullptr;
	double Best = 1e18;
	for (const otg::Tree& T : Trees.all) { const double DD = (T.x - Ball.x) * (T.x - Ball.x) + (T.z - Ball.z) * (T.z - Ball.z); if (DD < Best) { Best = DD; Near = &T; } }
	if (Near)
	{
		const double G = Course.HeightAt(Near->x, Near->z);
		Report(TEXT("trunk"), otg::V3{ Near->x - 5, G + 1, Near->z }, otg::V3{ Near->x + 5, G + 1, Near->z });
	}
	// straight down onto the ground at the ball
	Report(TEXT("ground"), otg::V3{ Ball.x, Ball.y + 10, Ball.z }, otg::V3{ Ball.x, Ball.y - 10, Ball.z });
	UE_LOG(LogOTGRound, Display, TEXT("CastTest: ground at the ball %.3f"), Course.HeightAt(Ball.x, Ball.z));
}

// Aim at a spot (course metres): the minimap, or a click on the ground
void AOTGRound::AimAt(double X, double Z)
{
	if (Phase != EPhase::Ready || bPaused) return;
	const double D = FMath::Sqrt((X - Ball.x) * (X - Ball.x) + (Z - Ball.z) * (Z - Ball.z));
	if (D < 1) return;
	Heading = HeadingTo(Ball, X, Z);
	AimDist = D;
	ShowAim();
	Camera.AddressView(false);
}

// Where a line (world space, from the camera through the cursor) first meets the ground: marched
// along in 2 m steps against the course's heights, then halved down to 5 cm
void AOTGRound::AimAlongRay(const FVector& Origin, const FVector& Dir)
{
	FVector Hit;
	if (GroundAlongRay(Origin, Dir, Hit)) AimAtWorld(Hit);
}

bool AOTGRound::GroundAlongRay(const FVector& Origin, const FVector& Dir, FVector& Out) const
{
	if (!Data) return false;
	auto Below = [&](const FVector& W) {
		const FVector C = Data->WorldToCourse(W);
		const double G = Course.HeightAt(C.X, C.Z);
		return FMath::IsFinite(G) && C.Y <= G;
	};
	const FVector D = Dir.GetSafeNormal();
	double A = 0, B = -1;
	for (double T = 200; T < 400000; T += 200)
	{
		if (Below(Origin + D * T)) { B = T; break; }
		A = T;
	}
	if (B < 0) return false;
	for (int32 k = 0; k < 14; k++)
	{
		const double M = (A + B) / 2;
		(Below(Origin + D * M) ? B : A) = M;
	}
	Out = Origin + D * B;
	return true;
}

void AOTGRound::AimAtWorld(const FVector& W)
{
	const FVector C = Data->WorldToCourse(W);
	AimAt(C.X, C.Z);
}

void AOTGRound::TogglePerf()
{
	if (Hud) Hud->ShowPerf(!Hud->IsPerfShown());
}

FVector AOTGRound::PinWorld() const
{
	return World(Pin());
}

void FOTGRoundCamera::FreeLookAt(const FVector& Target, double Dist, double Height, double YawDeg)
{
	if (!bFreeView) ToggleFreeView();
	const double Y = FMath::DegreesToRadians(YawDeg);
	FreePos = Target - FVector(FMath::Cos(Y), FMath::Sin(Y), 0) * Dist + FVector(0, 0, Height);
	if (const AOTGRuntimeCourse* RC = CourseOf(Round->GetWorld()))
		FreePos.Z = FMath::Max(FreePos.Z, RC->WaterTopAt(FreePos) + 60);
	FreeRot = (Target - FreePos).Rotation();
	if (Cam) Cam->SetActorLocationAndRotation(FreePos, FreeRot);
}

void AOTGRound::ToggleShotPanel()
{
	if (Hud) Hud->ShowShotPanel(!Hud->IsShotPanelShown());
}

void AOTGRound::ToggleShotSettings()
{
	if (Hud) Hud->ShowShotSettings(!Hud->IsShotSettingsShown());
}

void AOTGRound::ToggleScorecard()
{
	// (the range has no card: Tab hides and shows its numbers)
	if (bRange && RangeMode.RangePanel)
	{
		RangeMode.RangePanel->SetVisibility(RangeMode.RangePanel->GetVisibility() == EVisibility::Collapsed ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
		return;
	}
	HudView.ShowScorecard(!bScorecard);
}

void AOTGRound::Mulligan()
{
	if (bRange) return RangeMode.NewBall();
	if (!Undo || Phase != EPhase::Ready) return;
	Ball = Undo->Ball; Strokes = Undo->Strokes; Lie = Undo->Lie; Heading = Undo->Heading;
	Undo.Reset();
	ClearTracer();
	PlaceBall(Ball);
	ShowAim();
	Camera.AddressView(true);
	HudView.UpdateHudInfo();
	HudView.Toast(TEXT("Mulligan: replay the shot"));
	TellLaunchMonitor(true);
}
