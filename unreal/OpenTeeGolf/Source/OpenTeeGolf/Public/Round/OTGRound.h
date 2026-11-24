// A round on a course from a course pack: the golf the web version's main.js plays (hole flow,
// aiming, the auto club, the lie's effect, penalties, relief, holing out, the scorecard), with the
// C++ physics (OTGBallPhysics) on the course's play data and shots from the launch monitor
// (OTGLaunchMonitorSubsystem). Shows it: the ball, its tracer, the aim marker, the cameras (the
// hole from above the tee, behind the ball at address, following it in flight) and the HUD.
//
// Players take turns as in the web version (FOTGGame, OTGPlayers.h): whoever's away plays next,
// the others' balls lie where they are, and the game's format scores it.
//
// On the driving range (otg::range, OTGRange.h) it plays the range instead: one player, the ball
// teed on the middle mat, every shot's numbers in the range panel (SOTGRange.h) with the club's
// recent shots and averages, and a new ball on the mat a few seconds after each one stops (at once
// with Enter or Backspace). The club stays as picked; the aim starts straight down the range at the
// club's carry.
//
// Spawned by AOTGGameMode in a course's level; AOTGPlayerController sends it the keys.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LaunchMonitor/OTGLaunchMonitorTypes.h"
#include "Round/OTGPlayers.h"
#include "Round/OTGWeather.h"
#include "Round/OTGRange.h"
#include "Physics/OTGPlayCourse.h"
#include "OTGRound.generated.h"

class UOTGCourseData;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class ACameraActor;
class SOTGHud;

UCLASS()
class OPENTEEGOLF_API AOTGRound : public AActor
{
	GENERATED_BODY()

public:
	AOTGRound();
	void Setup(UOTGCourseData* InData, const FOTGRoundSetup& InSetup);

	virtual void Tick(float Dt) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// ---- from the keys (AOTGPlayerController)
	void AimBy(double Degrees);
	void ChangeClub(int32 Step);       // +1 longer club, -1 shorter
	void TestShot();                   // a shot for the club and distance, without a launch monitor
	void Continue();                   // skip the flight / the next hole / back to the menu
	void ToggleScorecard();
	void Mulligan();
	void DropNearPin(double Yards);    // (testing)
	void CastTest();                   // (testing)
	void SurfaceTest();                // (testing) carry and roll by landing surface, logged
	void AimAt(double X, double Z);    // (course metres: the minimap, a click on the ground)
	void AimAtWorld(const FVector& W);
	void AimAlongRay(const FVector& Origin, const FVector& Dir);   // (a click: where the line meets the ground)
	void ToggleShotPanel();
	void ToggleShotSettings();        // (D: which shot numbers show, and where)
	// the free view (F): fly round the course
	void ToggleFreeView();
	// The slope grid on the green (the G key): on unless turned off
	void ToggleSlopeGrid() { bGridOn = !bGridOn; GridFor = FVector::ZeroVector; }
	bool IsFreeView() const { return bFreeView; }
	void FreeFly(float Dt, const FVector& Move, const FVector2D& Look, bool bFast);
	void FreeLookAt(const FVector& Target, double Dist, double Height, double YawDeg);   // (testing)
	FVector PinWorld() const;
	void TogglePerf();                 // (P: frame rate and timings)
	// the weather panel (L): the weather changed as you play
	void ToggleWeatherPanel();
	// the course designer (R): the course's grass, trees and rocks, changed over the course itself;
	// the ball's trees made again as the designer has them, and the free view flown to a tree
	void ToggleGrassDesigner(int32 Tab = -1);   // (-1: the tab it was on)
	void RefreshTrees();
	void ShowTree(const FString& Kind);
	void ShowGrass(const FString& Kind);   // (the free view flown to where it grows thickly nearby)
	// A click on the course (the camera's line through the cursor): the course designer's, when it's
	// open on its trees tab (true: it used it), else nothing
	bool DesignClick(const FVector& Origin, const FVector& Dir);
	// (testing: a click at a point on the screen, 0..1 across and down)
	void DesignClickAt(double FX, double FY);
	void SetDesignTool(int32 Tool);
	void EditPicked(const FString& Field, const FString& Value);   // (testing: OTG.Trees.Edit)   // (testing: the trees tab's click tool, 0 pick .. 3 take out)
	// Where a line from the camera meets the course's ground (its own heights: no collision needed)
	bool GroundAlongRay(const FVector& Origin, const FVector& Dir, FVector& Out) const;
	void SetWeatherKey(const FString& Key);
	void SetWindMph(double Mph);
	void SetWindFrom(double RelDeg);
	void SetCloud(double Cloud);
	void SetRain(double Mm);
	void SetFog(double Fog);
	void SetTemp(double C);
	// the time of day (hours, local sun time): the sun where it really is then, for the course's
	// latitude on a midsummer day; below 0, lit from behind the player on every hole as golf games are
	void SetSunHour(double Hour);
	double GetSunHour() const { return SunHour; }
	double WindFromRelative() const;
	const FOTGWeather& GetWeather() const { return Weather; }
	void TogglePause();
	bool IsPaused() const { return bPaused; }
	void QuitToMenu();

	// ---- shots
	void OnShot(const FOTGShot& Shot);

	// ---- the driving range
	bool IsRange() const { return bRange; }
	void NewBall();                    // (the range: a new ball on the mat now)

private:
	enum class EPhase : uint8 { Ready, Flying, Resting, Between, Holed, Done };

	// the golf
	void StartHole(int32 Index);
	void ResetAim(bool bToPin);
	void AutoClub();
	void SetClub(const FString& Code);
	void FinishShot();
	void NextShot();
	// players taking turns (main.js loadSide, saveSide, roundAfterShot, beginTurn)
	void LoadSide(int32 I);
	void SaveSide();
	bool RoundAfterShot();
	void BeginTurn(int32 I, bool bSameSpot);
	void RecordRound();
	void ShowOtherBalls();
	double PinDist(const otg::V3& B) const;
	// the weather (main.js setupWeather, wind, shotWind)
	void SetupWeather();
	void ApplyWeatherToScene();
	void ApplyLook();                   // (the course's colour and light: OTG.Look.* console variables)
	uint32 LookStampNow() const;        // (the look's settings, the hole and the hour, hashed: ApplyLook again when it changes)
	void HoleWind(double& Mps, double& FromDeg) const;
	FString WindText() const;
	void UpdateMap();
	void UpdateWindBox();
	void UpdateRain(float Dt);
	void UpdateFlag(float Dt);
	void WeatherChanged();
	void HoleOut(bool bPickedUp, const FString& Note);
	void NextHole();
	bool WaterDrop(otg::V3& Out) const;
	bool PathRelief(const otg::V3& Ball, otg::V3& Out) const;

	const struct FOTGHole& Hole() const;
	otg::V3 Pin() const;
	otg::V3 TeeOf(const struct FOTGHole& H, const FString& Tee) const;
	otg::V3 OnGround(double X, double Z) const;
	otg::V3 AimPoint() const;
	double DistToPin() const;
	bool OnGreen() const { return Lie == otg::Surface::Green; }
	int32 MaxStrokes() const;

	// showing it
	FVector World(const otg::V3& P) const;
	FVector BallWorld(const otg::V3& P) const;
	void PlaceBall(const otg::V3& P);
	void ShowAim();
	void UpdateTracer(int32 Upto, const FVector& Head);
	void ClearTracer();
	void AddressView(bool bSnap);
	void OverviewView();
	void UpdateCamera(float Dt);
	void UpdateHudInfo();
	void UpdatePinTag();               // (the pin's yardage and height over the flag, each frame)
	void ShowShotData(bool bWithResult);
	void Banner(const FString& Title, const FString& Small, float Seconds = 3.f);
	void Toast(const FString& Text, float Seconds = 2.5f);
	void ShowScorecard(bool bShow);
	void TellLaunchMonitor(bool bTurn);
	FString Len(double M) const;       // yards
	FString Putt(double M) const;      // feet

	// the putt-putt (AOTGMiniGolf): every shot a putt, off the felt is out, six strokes at most
	bool bMini = false;
	FString HoleLen(double M) const { return bMini ? Putt(M) : Len(M); }
	double PuttMps(double Extra) const;   // (a putt's pace for the test shot, m/s: to the pin, Extra beyond it)
	double FlagTopCm = 228;               // (the flag's top above the cup)
	int32 AimTurn = -1;                   // (aimed at one of the hole's turns - its aim point - not the cup)

	UPROPERTY() TObjectPtr<UOTGCourseData> Data;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> BallMesh;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> AimMesh;      // (where the shot's aimed: holds the spot, not drawn)
	UPROPERTY() TObjectPtr<UStaticMeshComponent> AimStick;     // (a yardage stick standing there...)
	UPROPERTY() TObjectPtr<UStaticMeshComponent> AimStickTop;  // (...its coloured top)
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PinMesh;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> FlagMesh;
	UPROPERTY() TObjectPtr<class UMaterialParameterCollection> WindMPC;   // (the wind for the trees and grass)
	UPROPERTY() TObjectPtr<class UMaterialParameterCollection> FabWindMPC;   // (the Megascans plants' wind)
	UPROPERTY() TObjectPtr<class UMaterialParameterCollection> WeatherMPC;   // (MPC_Weather: Wet, Rain)
	UPROPERTY() TObjectPtr<class UMaterialParameterCollection> GsMPC;        // (MPC_Greensward: its grass's wetness)
	// the rain's splashes on hard ground near the camera (far rain is the height fog's)
	struct FSplash { FVector P; float Age = 0, Size = 1; };
	TArray<FSplash> Splashes;
	float SplashCarry = 0;
	int32 SplashVerts = 0;
	void UpdateRainFar(float Dt, double Mm);
	// a ball into water: a splash, then rings spreading over the surface (OTGRoundExtras.cpp)
	struct FRipple { FVector P; float Age = 0; };
	TArray<FRipple> Ripples;
	// rain on the water: small rings where the drops land, more the harder it rains
	struct FRainRing { FVector P; float Age = 0; float Size = 1; float Width = 1; };
	TArray<FRainRing> RainRings;
	float RainRingCarry = 0;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RainRingMat;
	UPROPERTY() TObjectPtr<class UProceduralMeshComponent> RippleMesh;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RippleMat;
public:
	void AddRipple(const FVector& WorldAt);
private:
	void UpdateRipples(float Dt);
	void SpawnRainRings(float Dt);
	// a storm's lightning, and the weather's sound (UOTGWeatherSound: rain, wind, thunder)
	UPROPERTY() TObjectPtr<class UOTGWeatherSound> WxSound;
	UPROPERTY() TObjectPtr<class UAudioComponent> WxAudio;
	float NextBolt = 5.f, BoltAge = 99.f, BoltNear = 0.f;
	float SkyNow = -1.f;   // (the sky light as the weather set it: a flash lifts it for a moment)
	void UpdateStorm(float Dt);
	// the sky, the time of day and the weather's look: one Meridian Sky actor (the plugin) in place of
	// the level's sun, sky, sky light, fog and clouds, driven from the round's weather and SunHour
	// (OTGSky.cpp). -oldsky on the command line: the level's own sky, as before
	UPROPERTY() TObjectPtr<class AMeridianSkyActor> Sky;
	bool bSkyTried = false;
	bool EnsureSky();                   // (true: the Meridian sky's in charge - spawned the first time)
	void ApplySky();                    // (the look and the weather on it: from ApplyLook and ApplyWeatherToScene)
	UFUNCTION() void OnSkyBolt(FVector Location, float DistanceMeters);   // (its lightning: our thunder)
	TWeakObjectPtr<class UExponentialHeightFogComponent> LevelFog;   // (kept: the level's haze, not Meridian's)
	// changing weather ("changing": showers come and go; live: fetched again every 15 minutes) -
	// from one state to the next over a minute and a half, each held a few minutes
	FOTGWeather WxFrom, WxTo;
	float WxBlend = 1.f, WxHold = 0.f, WxRefresh = 0.f, WxApplyIn = 0.f;
	int32 WxStage = 0;
	void UpdateChanging(float Dt);
	void BlendTo(const FOTGWeather& Next, const FString& Say);
	float Wet = -1.f;   // (0 dry .. 1 soaked: rises in rain, dries slowly; -1 until the round's weather is set)
	void UpdateWet(float Dt);
	UPROPERTY() TObjectPtr<UStaticMeshComponent> TeeMesh;      // (the tee's peg...)
	UPROPERTY() TObjectPtr<UStaticMeshComponent> TeeHeadMesh;  // (...and its cup)
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CupMesh;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> TracerMesh;
	// the slope grid: lines draped over the green coloured by its slope, dots sliding downhill
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> GridMesh;
	UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> GridDots;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> GridMats;
	struct FGridDot { double X, Z, Dx, Dz, Pct, Phase; };
	TArray<FGridDot> Dots;
	double GridSpacing = 1.0;
	bool bGridOn = true;
	FVector GridFor = FVector::ZeroVector;   // (the ball and pin it was drawn for: drawn again if either moves)
	void UpdateSlopeGrid(float Dt);
	void BuildSlopeGrid();
	void HideSlopeGrid();
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BallMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> AimMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TracerMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PinMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FlagMat;
	UPROPERTY() TObjectPtr<ACameraActor> Cam;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> OtherBalls; // (one per side)
	TSharedPtr<SOTGHud> Hud;
	FDelegateHandle ShotHandle;

	otg::PlayCourse Course;
	otg::PlayTrees Trees;
	otg::PlayCollider Collider;          // (the ground, trunks, buildings and the flagstick)

	// the round
	EPhase Phase = EPhase::Ready;
	int32 HoleIndex = 0, Strokes = 0;
	FOTGRoundSetup Setup_;
	TUniquePtr<FOTGGame> Game;
	int32 SideIdx = 0, NextSide = 0, LastHole = 0;  // (LastHole: one past the last to play)
	bool bPaused = false;
	FOTGWeather Weather;
	TArray<double> WindRel;             // (custom wind: its direction relative to each hole)
	float BaseFogDensity = -1, BaseSun = -1, BaseCoverage = -1, BaseDensity = -1, BaseRayleigh = -1, BaseMie = -1, BaseSkyLight = -1;
	FLinearColor BaseSkyLum = FLinearColor(-1, -1, -1);   // (the sky's own brightness, greyed under cloud)
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CloudMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> OwnCloudMat;   // (M_Clouds)
	UPROPERTY() TObjectPtr<class APostProcessVolume> LookVolume;
	float OrigSun = -1, OrigMie = -1, OrigRayleigh = -1, OrigFog = -1, OrigSky = -1;
	FRotator OrigSunRot = FRotator::ZeroRotator;
	FLinearColor OrigSunColour = FLinearColor::White;
	uint32 LookStamp = 0;
	FVector OrbitCentre = FVector::ZeroVector, OrbitGoal = FVector(1e30);   // (the address view: AddressView, UpdateCamera)
	double SunHour = -1;
	void SunAt(double Hour, double& ElevDeg, double& AzDeg) const;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> RainMesh;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> FlagCloth;
	double FlagTime = 0;
	double FlagPhase = 0;   // (the flag's ripples, radians: added up a frame at a time)
	bool bFlagMade = false;
	bool bFreeView = false;
	bool bWeatherFromCommandLine = false;
	FVector FreePos = FVector::ZeroVector;
	FRotator FreeRot = FRotator::ZeroRotator;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RainMat;
	TArray<FVector> RainDrops;
	int32 RainVerts = 0;
	otg::V3 Ball, PrevBall, BallShown;  // (BallShown: where it's drawn, its contact point)
	otg::Surface Lie = otg::Surface::Tee;
	double Heading = 0, AimDist = 100;  // radians (0 = north, + toward east), metres
	FString Club = TEXT("DR");
	bool bClubPicked = false;           // (the player changed it: no auto club until the next shot)
	struct FUndo { otg::V3 Ball; int32 Strokes; otg::Surface Lie; double Heading; };
	TOptional<FUndo> Undo;
	TOptional<FOTGShot> PendingShot;
	FString TeeName = TEXT("White"), PinName = TEXT("Saturday");

	// the shot in the air
	FOTGShot LastShot;
	otg::ShotResult Res;
	bool bHasRes = false, bCarryShown = false;
	double AnimT = 0;
	TArray<FVector> TracerPts;
	double PhaseTimer = 0;              // (Resting, Holed: seconds left)
	bool bScorecard = false, bScorecardAuto = false;
	FString ReliefNote;

	// the camera
	FVector CamGoal = FVector::ZeroVector, CamLook = FVector::ZeroVector, CamPos = FVector::ZeroVector, CamLookNow = FVector::ZeroVector;
	FVector ShotFrom = FVector::ZeroVector;
	double OverviewLeft = 0;
	float CamSpeed = 3.f;

	double RoundTime = 0;
	TArray<TPair<double, FString>> ExecAt;

	// the driving range
	bool bRange = false;
	TArray<FOTGRangeShot> RangeShots;
	TSharedPtr<class SOTGRangePanel> RangePanel;
	UPROPERTY() TObjectPtr<class AOTGRangeProps> RangeProps;
	UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> RangeDots;   // (where the club's last shots stopped)
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RangeDotMat;
	int32 RangeBalls = 0;
	double ClubCarryM() const;
	void RangeShotDone();
	void UpdateRange();
};
