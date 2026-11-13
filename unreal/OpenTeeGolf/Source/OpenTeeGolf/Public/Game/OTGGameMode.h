// The game mode for every level (DefaultEngine.ini GlobalDefaultGameMode). In a course's level
// (the course the course subsystem opened, UOTGCourseSubsystem::Current) it starts a round
// (AOTGRound) on hole 1, or the hole -hole=<n> names. No pawn: the round moves its own camera.
//
// AOTGPlayerController: the keys. Left / Right (or A / D) aim, Up / Down (W / S) change club, Space
// a test shot, Enter skip the flight or go on to the next hole, Tab the scorecard, Backspace a
// mulligan. Console: OTG.Shot <name> saves a screenshot (with the HUD) in Saved/Screenshots.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "OTGGameMode.generated.h"

class AOTGRound;

UCLASS()
class OPENTEEGOLF_API AOTGGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AOTGGameMode();
	virtual void StartPlay() override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
};

UCLASS()
class OPENTEEGOLF_API AOTGPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AOTGPlayerController();
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float Dt) override;

private:
	AOTGRound* Round();
	TWeakObjectPtr<AOTGRound> Cached;
};
