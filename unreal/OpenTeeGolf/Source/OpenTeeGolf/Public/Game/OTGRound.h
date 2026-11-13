// A round on a course from a course pack: the golf the web version's main.js plays (hole flow,
// aiming, the auto club, the lie's effect, penalties, relief, holing out, the scorecard), with the
// C++ physics (OTGBallPhysics) on the course's play data and shots from the launch monitor
// (OTGLaunchMonitorSubsystem). Shows it: the ball, its tracer, the aim marker, the cameras (the
// hole from above the tee, behind the ball at address, following it in flight) and the HUD.
//
// Spawned by AOTGGameMode in a course's level; AOTGPlayerController sends it the keys.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LaunchMonitor/OTGLaunchMonitorTypes.h"
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
	void Setup(UOTGCourseData* InData, int32 FirstHole);

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

	// ---- shots
	void OnShot(const FOTGShot& Shot);

private:
	enum class EPhase : uint8 { Ready, Flying, Resting, Holed, Done };

	// the golf
	void StartHole(int32 Index);
	void ResetAim(bool bToPin);
	void AutoClub();
	void SetClub(const FString& Code);
	void FinishShot();
	void NextShot();
	void HoleOut(bool bPickedUp, const FString& Note);
	void NextHole();
	bool WaterDrop(otg::V3& Out) const;
	bool PathRelief(const otg::V3& Ball, otg::V3& Out) const;

	const struct FOTGHole& Hole() const;
	otg::V3 Pin() const;
	otg::V3 TeeOf(const struct FOTGHole& H) const;
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
	TSharedPtr<SOTGHud> Hud;
	FDelegateHandle ShotHandle;

	otg::PlayCourse Course;
	otg::PlayTrees Trees;

	// the round
	EPhase Phase = EPhase::Ready;
	int32 HoleIndex = 0, Strokes = 0;
	TArray<int32> Scores;               // per hole (-1: not played)
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
