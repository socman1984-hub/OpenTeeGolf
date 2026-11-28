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
#include "Round/OTGRoundParts.h"
#include "OTGRound.generated.h"

class UOTGCourseData;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class ACameraActor;
class SOTGHud;
class UOTGShotTracerComponent;

UCLASS()
class OPENTEEGOLF_API AOTGRound : public AActor
{
	GENERATED_BODY()

public:
	AOTGRound();
	// (the parts' objects, kept from the garbage collector: they aren't UPROPERTYs)
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
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
	void ToggleFreeView() { Camera.ToggleFreeView(); }
	// The slope grid on the green (the G key): on unless turned off
	void ToggleSlopeGrid() { SlopeGrid.ToggleSlopeGrid(); }
	bool IsFreeView() const { return Camera.bFreeView; }
	void FreeFly(float Dt, const FVector& Move, const FVector2D& Look, bool bFast) { Camera.FreeFly(Dt, Move, Look, bFast); }
	void FreeLookAt(const FVector& Target, double Dist, double Height, double YawDeg) { Camera.FreeLookAt(Target, Dist, Height, YawDeg); }   // (testing)
	FVector PinWorld() const;
	void TogglePerf();                 // (P: frame rate and timings)
	// the weather panel (L): the weather changed as you play
	void ToggleWeatherPanel();
	// the course designer (R): the course's grass, trees and rocks, changed over the course itself;
	// the ball's trees made again as the designer has them, and the free view flown to a tree
	void ToggleGrassDesigner(int32 Tab = -1);   // (-1: the tab it was on)
	void RefreshTrees();
	void RefreshBridges();   // (the bridges changed in the designer: the ball's decks and rails again)
	void BuildCollider();
	void ShowTree(const FString& Kind);
	void ShowGrass(const FString& Kind);
	void ShowArea(const TArray<FVector2D>& Pts);   // (the free view flown back over these course metres, all of them in sight)   // (the free view flown to where it grows thickly nearby)
	// A click on the course (the camera's line through the cursor): the course designer's, when it's
	// open on its trees tab (true: it used it), else nothing
	bool DesignClick(const FVector& Origin, const FVector& Dir);
	// (testing: a click at a point on the screen, 0..1 across and down)
	void DesignClickAt(double FX, double FY);
	void SetDesignTool(int32 Tool);
	// (testing: OTG.Bridges.* - the bridges tab's click tool, 0 pick 1 add; the picked or new bridge changed)
	void SetBridgeTool(int32 Tool);
	void EditBridge(const FString& Field, const FString& Value);
	void AddBridgeAt(double AX, double AZ, double BX, double BZ);
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
	// Mevo+ fast shots: the full result of the shot started early - flown again from the same start
	// with it, the ball easing onto the new path (main.js onShotUpdate)
	void OnShotUpdate(const FOTGShot& Shot);

	// ---- the driving range
	bool IsRange() const { return bRange; }
	void NewBall() { RangeMode.NewBall(); }   // (the range: a new ball on the mat now)


	// a ball into water: a splash, then rings spreading over the surface (FOTGRoundWaterFx)
	void AddRipple(const FVector& WorldAt) { WaterFx.AddRipple(WorldAt); }

private:
	enum class EPhase : uint8 { Ready, Flying, Resting, Between, Holed, Done };

	// (the parts reach the round's state - and one another - through it: OTGRoundParts.h)
	friend struct FOTGRoundTurns;
	friend struct FOTGRoundCamera;
	friend struct FOTGRoundHud;
	friend struct FOTGRoundScene;
	friend struct FOTGRoundChanging;
	friend struct FOTGRoundRain;
	friend struct FOTGRoundWaterFx;
	friend struct FOTGRoundFlag;
	friend struct FOTGRoundSlopeGrid;
	friend struct FOTGRoundRangeMode;
	friend struct FOTGRoundNet;
	FOTGRoundTurns Turns;
	FOTGRoundCamera Camera;
	FOTGRoundHud HudView;
	FOTGRoundScene Scene;
	FOTGRoundChanging Changing;
	FOTGRoundRain RainFx;
	FOTGRoundWaterFx WaterFx;
	FOTGRoundFlag Flag;
	FOTGRoundSlopeGrid SlopeGrid;
	FOTGRoundRangeMode RangeMode;
	FOTGRoundNet Online;

	// the golf
	void StartHole(int32 Index);
	void ResetAim(bool bToPin);
	void AutoClub();
	void SetClub(const FString& Code);
	void FinishShot();
	void FinishShotHere();
	// A shot flown (its lie's effect already in Play; the options' wind and air set, the course's
	// trees, collider and pin added here): this computer's player's, or an online game's other player's
	void StartShot(const otg::Shot& Play, otg::ShotOptions O, double FxSpeed, double FxSpin, int32 Seed);
	// (a Mevo+ fast shot's full result: the shot in the air flown again from the same start)
	void RefineShot(const otg::Shot& Play, int32 Seed);
	void EveryoneIn(const FString& Title, FString Small);
	void NextShot();
	double PinDist(const otg::V3& B) const;
	// the weather (main.js setupWeather, wind, shotWind)
	void SetupWeather();
	void HoleWind(double& Mps, double& FromDeg) const;
	FString WindText() const;
	void WeatherChanged();
	void HoleOut(bool bPickedUp, const FString& Note);
	// Auto-putt (the round's setup): on the green, the putts counted - 1 inside the circle, 2 outside - and the hole done
	bool AutoPutting() const { return Setup_.bAutoPutt && !bMini && !bRange; }
	double AutoPuttRadiusM() const { return FMath::Max(0.3, Setup_.AutoPuttFt * 0.3048); }
	void AutoPutt();
	void ShowPuttCircle();
	void SetPuttCircleIn(bool bIn);
	bool bAutoPuttDue = false;
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
	UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> PuttCircle;   // (auto-putt: the circle round the hole, painted on the green)
	UPROPERTY() TObjectPtr<class UMaterialParameterCollection> WindMPC;   // (the wind for the trees and grass)
	UPROPERTY() TObjectPtr<class UMaterialParameterCollection> FabWindMPC;   // (the Megascans plants' wind)
	UPROPERTY() TObjectPtr<class UMaterialParameterCollection> WeatherMPC;   // (MPC_Weather: Wet, Rain)
	UPROPERTY() TObjectPtr<class UMaterialParameterCollection> GsMPC;        // (MPC_Greensward: its grass's wetness)
	// (the Meridian sky's lightning: our thunder - FOTGRoundScene binds it)
	UFUNCTION() void OnSkyBolt(FVector Location, float DistanceMeters);
	UPROPERTY() TObjectPtr<UStaticMeshComponent> TeeMesh;      // (the tee's peg...)
	UPROPERTY() TObjectPtr<UStaticMeshComponent> TeeHeadMesh;  // (...and its cup)
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CupMesh;
	// (the ball's flight drawn as a ribbon: Plugins/OTGShotTracer)
	UPROPERTY() TObjectPtr<UOTGShotTracerComponent> TracerMesh;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BallMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> AimMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TracerMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PinMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FlagMat;
	TSharedPtr<SOTGHud> Hud;
	FDelegateHandle ShotHandle, ShotUpdateHandle, NoticeHandle;

	otg::PlayCourse Course;
	otg::PlayTrees Trees;
	otg::PlayCollider Collider;          // (the ground, trunks, buildings and the flagstick)

	// the round
	EPhase Phase = EPhase::Ready;
	int32 HoleIndex = 0, Strokes = 0;
	FOTGRoundSetup Setup_;
	int32 LastHole = 0;  // (one past the last to play)
	bool bPaused = false;
	FOTGWeather Weather;
	TArray<double> WindRel;             // (custom wind: its direction relative to each hole)
	double SunHour = -1;
	void SunAt(double Hour, double& ElevDeg, double& AzDeg) const;
	bool bWeatherFromCommandLine = false;

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
	// (what the shot was flown with, so a fast shot's full result can fly it again; and the ease
	// from where the ball was onto the corrected path)
	otg::ShotOptions LastOpts;
	double LastFxSpeed = 1, LastFxSpin = 1;
	otg::V3 BlendOffset;
	double BlendT0 = -1;
	double PhaseTimer = 0;              // (Resting, Holed: seconds left)
	bool bScorecard = false, bScorecardAuto = false;
	FString ReliefNote;
	double RoundTime = 0;
	TArray<TPair<double, FString>> ExecAt;

	// the driving range (FOTGRoundRangeMode)
	bool bRange = false;
};
