// The studio's live preview (SOTGStudio): a round plot of tall grass in short rough, 64 m across,
// built in the menu's own level by the course's own code (AOTGRuntimeCourse::BuildPreview: its
// ground material, grass streaming and trees) under a sun, sky and sky light of its own, seen
// through a camera the studio turns round it. A biome's kinds grow on it again as they change; a
// forest preset is planted on it, its biome under the trees.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/OTGTreeDesign.h"
#include "World/OTGGrassFwd.h"
#include "OTGPreviewStage.generated.h"

class AOTGRuntimeCourse;
class ACameraActor;

UCLASS()
class AOTGPreviewStage : public AActor
{
	GENERATED_BODY()

public:
	AOTGPreviewStage();
	// Spawned in World: the plot, lights and camera, and the view looked through it
	static AOTGPreviewStage* Open(UWorld* World);
	void Close();   // (the view back to the player's, everything gone)

	void ShowGrass(const TArray<otg::FGrassKind>& Kinds);
	void ShowForest(const FOTGForestPreset& Preset, const TArray<otg::FGrassKind>& Under);

	// The camera: turned round the plot (degrees), up and down, nearer and further (x the distance)
	void Orbit(float DYaw, float DPitch);
	void Zoom(float Times);
	void Frame(bool bForest);
	// ... or flown as the round's free view: moved (cm along where it looks, to its right, and up) and
	// turned where it stands. Turning round the plot again (Orbit, Zoom, Frame) leaves it
	void Fly(const FVector& Move);
	void Look(float DYaw, float DPitch);
	bool IsFree() const { return bFree; }
	void FrameClose();   // (close over the grass, looking down into it: a kind's picture, -studioicons)   // (from eye height near the grass; further back and up for a forest)
	int32 GrassCount() const;
	TArray<FString> Plants() const;   // (the Megascans and KiteDemo plants the game has: kinds to add)
	int32 TreeCount() const;

	virtual void Tick(float Dt) override;

private:
	UPROPERTY() TObjectPtr<AOTGRuntimeCourse> Plot;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;
	UPROPERTY() TArray<TObjectPtr<AActor>> Lights;
	UPROPERTY() TObjectPtr<AActor> ViewBefore;
	float Yaw = 35, Pitch = -8, Dist = 1400, LookUp = 60;
	bool bTurning = true;   // (a slow turn until the camera's moved by hand)
	bool bFree = false;
	FVector FreeAt = FVector::ZeroVector;
	float FreeYaw = 0, FreePitch = 0;
	void StartFree();
	void Place();
};
