// An exported course (a folder the web version's Export to Unreal wrote: play.json, heightmap.png,
// photo.jpg, layer_*.png, trees, water, props, boulders) built at run time in the template level
// L_Course - what a course pack's level has, made from the folder without the Unreal Editor:
//  - the ground: the heightmap as tiles of mesh made here (four levels of detail each), with the
//    game's ground material (M_CourseGround_Runtime) reading the course's photo, masks and mowing
//    directions;
//  - trees, boulders, water, paths and buildings (BuildTrees ...), and the grass (AOTGGrass).
// The pieces it's built from are the game's own content, /Game/OpenTeeGolf/Game/Runtime
// (make_game_assets.py, build_course.py in game-assets mode).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/OTGGrassFwd.h"
#include "OTGRuntimeCourse.generated.h"

class UOTGCourseData;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;

UCLASS()
class OPENTEEGOLF_API AOTGRuntimeCourse : public AActor
{
	GENERATED_BODY()

public:
	AOTGRuntimeCourse();
	virtual void Tick(float Dt) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// Build the course in this level from its export folder (D->ExportDir), all at once
	void Build(UOTGCourseData* D, TFunction<void(float, const FString&)> Progress = nullptr);
	// ... or a step a frame (the picture, a few dozen of the ground's tiles, the trees ...), so a
	// loading screen can show it: Progress(0..1, what) after each, Done at the end
	void BuildInSteps(UOTGCourseData* D, TFunction<void(float, const FString&)> Progress, TFunction<void()> Done);

	// The course's pictures, as textures, by the ground material's parameter names (Photo, Mask_*,
	// Mow): the grass and turf read them too
	const TMap<FName, TObjectPtr<UTexture2D>>& Pictures() const { return Textures; }
	double LandSizeCm() const { return LandCm; }

private:
	void LoadPictures();
	void BuildGround(TFunction<void(float, const FString&)> Progress);
	void BuildGroundTiles(int32 From, int32 To);
	void PrepareGround();
	void BuildTrees();
	void BuildBoulders();
	void BuildWater();
	void BuildProps();
	void BuildGrass();
	void SetPictures(UMaterialInstanceDynamic* M) const;
	class UHierarchicalInstancedStaticMeshComponent* Instances(class UStaticMesh* Mesh, const TArray<FTransform>& Xfs, int32 CullFrom, int32 CullTo);

	UPROPERTY() TObjectPtr<UOTGCourseData> Data;
	UPROPERTY() TMap<FName, TObjectPtr<UTexture2D>> Textures;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GroundMat;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Tiles;
	UPROPERTY() TArray<TObjectPtr<UActorComponent>> Parts;
	double LandCm = 0;
	// (the steps still to do, BuildInSteps, and what to say)
	TArray<TPair<FString, TFunction<void()>>> Steps;
	int32 StepsTotal = 0;
	TFunction<void(float, const FString&)> StepProgress;
	TFunction<void()> StepsDone;
	// (the ground's tiles, PrepareGround: their count a side and samples a side)
	int32 TileCount = 0, TileQuads = 64;
	TMap<FString, struct otg::FPicture> MaskPixels;   // (the masks the grass reads, on the CPU)
	TSharedPtr<class otg::FGrass> Grass;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> TurfMats;
};
