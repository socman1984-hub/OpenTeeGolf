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
class UMaterialInterface;
class UStaticMesh;
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

	// The grass designer's view (SOTGGrassDesigner): the kinds growing now, others grown at once
	// (not saved), saved for the course (grass.json in its folder), the Megascans plants the game
	// has (their folders), and a kind's height at scale 1 (cm; 0: no mesh)
	const TArray<otg::FGrassKind>& GrassKinds() const;
	void SetGrassKinds(const TArray<otg::FGrassKind>& Kinds);
	bool SaveGrassKinds(FString* Where = nullptr) const;
	TArray<FString> GrassPlants() const;
	float GrassHeightCm(const FString& Kind) const;
	int32 GrassInstances() const;
	TArray<otg::FGrassKind> SavedGrassKinds() const;   // (as saved for the course, or the game's own)

private:
	void LoadPictures();
	void BuildGround(TFunction<void(float, const FString&)> Progress);
	void BuildGroundTiles(int32 From, int32 To);
	void PrepareGround();
	void LiftPondBanks();
	double GroundAt(double X, double Y) const;
	void BuildTrees();
	void BuildBoulders();
	void BuildWater();
	void BuildSea();
	void BuildFarLand();
	bool BuildRealFarLand();   // (the export's farland.json: real heights and photos; false if it hasn't one)
	void BuildProps();
	void BuildGrass();
	TMap<FString, TArray<TPair<UStaticMesh*, UMaterialInterface*>>> GrassMeshes(const TArray<otg::FGrassKind>& Kinds);
	TMap<FString, TArray<TPair<UStaticMesh*, UMaterialInterface*>>> GrassMeshCache;
	void SetPictures(UMaterialInstanceDynamic* M) const;
	class UHierarchicalInstancedStaticMeshComponent* Instances(class UStaticMesh* Mesh, const TArray<FTransform>& Xfs, int32 CullFrom, int32 CullTo);

	UPROPERTY() TObjectPtr<UOTGCourseData> Data;
	UPROPERTY() TMap<FName, TObjectPtr<UTexture2D>> Textures;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GroundMat;
	TArray<uint16> GroundHeights;   // (the heights the ground is drawn with: the course's, the ponds' banks lifted clear of the water)
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Tiles;
	UPROPERTY() TArray<TObjectPtr<UActorComponent>> Parts;
	double LandCm = 0;
	// (the sea's level, metres in the export's frame: BuildSea. bSea false: no sea)
	bool bSea = false;
	double SeaLevelM = 0;
	// (the steps still to do, BuildInSteps, and what to say)
	TArray<TPair<FString, TFunction<void()>>> Steps;
	int32 StepsTotal = 0;
	TFunction<void(float, const FString&)> StepProgress;
	TFunction<void()> StepsDone;
	// (the ground's tiles, PrepareGround: their count a side and samples a side)
	int32 TileCount = 0, TileQuads = 64;
	// (the ground's tiles near the course - its surfaces or trees in them or the next ones - drawn;
	// the rest left out, the land round it showing there: PlanGroundTiles. Empty: all drawn)
	TArray<bool> TileKeep;
	void PlanGroundTiles();
	bool GroundDrawnAt(double XCm, double YCm) const;
	TMap<FString, struct otg::FPicture> MaskPixels;   // (the masks the grass reads, on the CPU)
	TSharedPtr<class otg::FGrass> Grass;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> TurfMats;
	UPROPERTY() TArray<TObjectPtr<UStaticMesh>> TurfMeshes;   // (kept loaded: the grass makes cells of them as the camera moves)
};
