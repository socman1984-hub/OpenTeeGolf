// A round on a course from a course pack: the golf the web version's main.js plays (hole flow,
// aiming, the auto club, the lie's effect, penalties, relief, holing out, the scorecard), with the
// C++ physics (OTGBallPhysics) on the course's play data and shots from the launch monitor
// (OTGLaunchMonitorSubsystem). Shows it: the ball, its tracer, the aim marker, the cameras (the
// hole from above the tee, behind the ball at address, following it in flight) and the HUD.
//
// Players take turns as in the web version (FOTGGame, OTGPlayers.h): whoever's away plays next,
// the others' balls lie where they are, and the game's format scores it.
//
// Spawned by AOTGGameMode in a course's level; AOTGPlayerController sends it the keys.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LaunchMonitor/OTGLaunchMonitorTypes.h"
#include "Game/OTGPlayers.h"
#include "Game/OTGWeather.h"
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
	void AimAt(double X, double Z);    // (course metres: the minimap, a click on the ground)
	void AimAtWorld(const FVector& W);
	void ToggleShotPanel();
	// the free view (F): fly round the course
	void ToggleFreeView();
	bool IsFreeView() const { return bFreeView; }
	void FreeFly(float Dt, const FVector& Move, const FVector2D& Look, bool bFast);
	void FreeLookAt(const FVector& Target, double Dist, double Height, double YawDeg);   // (testing)
	FVector PinWorld() const;
	void TogglePerf();                 // (P: frame rate and timings)
	// the weather panel (L): the weather changed as you play
	void ToggleWeatherPanel();
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
	void ShowShotData(bool bWithResult);
	void Banner(const FString& Title, const FString& Small, float Seconds = 3.f);
	void Toast(const FString& Text, float Seconds = 2.5f);
	void ShowScorecard(bool bShow);
	void TellLaunchMonitor(bool bTurn);
	FString Len(double M) const;       // yards
	FString Putt(double M) const;      // feet

	UPROPERTY() TObjectPtr<UOTGCourseData> Data;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> BallMesh;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> AimMesh;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PinMesh;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> FlagMesh;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> TeeMesh;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> CupMesh;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> TracerMesh;
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
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CloudMat;
	UPROPERTY() TObjectPtr<class APostProcessVolume> LookVolume;
	float OrigSun = -1, OrigMie = -1, OrigRayleigh = -1, OrigFog = -1, OrigSky = -1;
	FRotator OrigSunRot = FRotator::ZeroRotator;
	FLinearColor OrigSunColour = FLinearColor::White;
	uint32 LookStamp = 0;
	double SunHour = -1;
	void SunAt(double Hour, double& ElevDeg, double& AzDeg) const;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> RainMesh;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> FlagCloth;
	double FlagTime = 0;
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
};
