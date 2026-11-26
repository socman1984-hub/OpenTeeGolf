// An exported course (a folder the web version's Export to Unreal wrote: play.json, heightmap.png,
// photo.jpg, layer_*.png, trees, water, props, boulders) built at run time in the template level
// L_Course - what a course pack's level has, made from the folder without the Unreal Editor:
//  - the ground: the heightmap as tiles of mesh made here (four levels of detail each), with the
//    game's ground material (M_CourseGround_Runtime) reading the course's photo, masks and mowing
//    directions;
//  - trees, boulders, water, paths and buildings, the land beyond, and the grass.
// Each is built and kept by a part of its own (OTGCourseParts.h: Ground, Water, Trees, FarLand,
// Props, Turf); this actor holds what they share - the course data, its pictures, the ground
// material - builds them in order (Build, BuildInSteps) and gives the rest of the game one place to
// ask (the designers, the round).
// The pieces it's built from are the game's own content, /Game/OpenTeeGolf/Game/Runtime
// (make_game_assets.py, build_course.py in game-assets mode).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/OTGGrassFwd.h"
#include "World/OTGTreeDesign.h"
#include "World/OTGCourseParts.h"
#include "OTGRuntimeCourse.generated.h"

class UOTGCourseData;
namespace otg { struct FGlbMesh; }
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
	// (the highest water surface over a point, world cm, or a huge negative: none - OTGWater.cpp)
	double WaterTopAt(const FVector& P) const { return Water.WaterTopAt(P); }
	// (the same, but only where the water stands over the ground there - not a still pond's sheet
	// running on under its banks: where rain's rings go)
	double OpenWaterAt(const FVector& P) const { return Water.OpenWaterAt(P); }
	AOTGRuntimeCourse();
	virtual void Tick(float Dt) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	// (the parts' objects, kept from the garbage collector: they aren't UPROPERTYs)
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

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
	const TArray<otg::FGrassKind>& GrassKinds() const { return Turf.GrassKinds(); }
	void SetGrassKinds(const TArray<otg::FGrassKind>& Kinds) { Turf.SetGrassKinds(Kinds); }
	bool SaveGrassKinds(FString* Where = nullptr) const { return Turf.SaveGrassKinds(Where); }
	bool CourseBiomes(struct FOTGCourseBiomes& Out) const { return Turf.CourseBiomes(Out); }
	void SetCourseBiomes(const struct FOTGCourseBiomes& B) { Turf.SetCourseBiomes(B); }
	bool SaveCourseBiomes() const { return Turf.SaveCourseBiomes(); }
	TArray<FString> GrassPlants() const { return Turf.GrassPlants(); }
	float GrassHeightCm(const FString& Kind) const { return Turf.GrassHeightCm(Kind); }
	int32 GrassInstances() const { return Turf.GrassInstances(); }
	TArray<otg::FGrassKind> SavedGrassKinds() const;   // (as saved for the course, or the game's own)
	// The fairways' and greens' colour ("fairway", "green"): as it is now, changed (seen at once),
	// saved, as saved
	FOTGTurfLook TurfLook(const FString& Surface) const { return Turf.TurfLook(Surface); }
	void SetTurfLook(const FString& Surface, const FOTGTurfLook& Look) { Turf.SetTurfLook(Surface, Look); }
	bool SaveTurfLooks(FString* Where = nullptr) const { return Turf.SaveTurfLooks(Where); }
	TMap<FString, FOTGTurfLook> SavedTurfLooks() const { return Turf.SavedTurfLooks(); }
	FLinearColor TurfLookTint(const FString& Kind) const { return Turf.TurfLookTint(Kind); }   // (what a mown kind's blades multiply by: white, or its surface's look)
	// The kinds' colours changed at once (their materials tinted: nothing grown again), and where a
	// kind grows thickly nearest a point (the level's cm; false: nowhere near)
	void SetGrassColours(const TArray<otg::FGrassKind>& Kinds) { Turf.SetGrassColours(Kinds); }
	bool FindGrass(const FString& Kind, const FVector& From, FVector& Out) const { return Turf.FindGrass(Kind, From, Out); }
	bool FindSurface(const FString& Mask, const FVector& From, FVector& Out, double InsideM = 3) const { return Turf.FindSurface(Mask, From, Out, InsideM); }   // (well inside it: OTG.Look.Swatch)
	// (making the ground's turf textures: the grass on a patch of a surface photographed from straight
	// above - its colour, normal and height - into Saved/TurfBake/<Name>_*.png. OTGTurfBake.cpp)
	bool BakeTurf(const FString& Surface, double SizeCm, const FString& Name, const FVector& From);
	void DumpGrassTints() const { Turf.DumpGrassTints(); }   // (testing: OTG.Grass.Tints)
	void SetGrassParam(const FString& Kind, const FString& Param, const FLinearColor& V) { Turf.SetGrassParam(Kind, Param, V); }   // (testing: OTG.Grass.Param)

	// The tree designer's view (SOTGTreeDesigner): the trees and rocks drawn again with a design
	// (the course data's, which the ball's trees follow too: AOTGRound::RefreshTrees), saved for
	// the course (trees_design.json), the species on the course and how many of each, and the
	// nearest tree of a species to a point (the level's cm; false: none)
	const FOTGTreeDesign& TreeDesign() const { return Trees.TreeDesign(); }
	void SetTreeDesign(const FOTGTreeDesign& Design) { Trees.SetTreeDesign(Design); }
	bool SaveTreeDesign(FString* Where = nullptr) const { return Trees.SaveTreeDesign(Where); }
	FOTGTreeDesign SavedTreeDesign() const { return Trees.SavedTreeDesign(); }
	TArray<TPair<FString, int32>> TreeSpecies() const { return Trees.TreeSpecies(); }
	// (the nearest of a species to From, not right under it - or, Kind "rock", the nearest rock)
	bool NearestTree(const FString& Kind, const FVector& From, FVector& OutBase, double& OutHeightCm) const { return Trees.NearestTree(Kind, From, OutBase, OutHeightCm); }
	int32 TreesDrawn() const { return Trees.TreeCount; }
	int32 RocksDrawn() const { return Trees.RockCount; }
	// The tree or rock along a line from the camera (the level's cm), before the ground at MaxT cm:
	// false if none. And one again by where it stands (after the trees are drawn again)
	bool Pick(const FVector& Origin, const FVector& Dir, double MaxT, FOTGPicked& Out) const { return Trees.Pick(Origin, Dir, MaxT, Out); }
	bool Picked(bool bRock, double X, double Z, FOTGPicked& Out) const { return Trees.Picked(bRock, X, Z, Out); }
	// The picked one marked on the course (a gold ring round it, a gold marker above): called each
	// frame while it's picked - not called for a moment, the marks go
	void MarkPicked(const FOTGPicked& P) { Trees.MarkPicked(P); }
	void MarkForests(const TArray<int32>& Which) { Trees.MarkForests(Which); }
	// The studio's preview plot (AOTGPreviewStage, OTGCoursePreview.cpp): built with no export folder -
	// flat ground SizeM a side with this actor in the middle, a round of tall grass in short rough -
	// then grown with these kinds, and planted with this forest preset (none: no trees)
	void BuildPreview(double SizeM);
	void PreviewGrass(const TArray<otg::FGrassKind>& Kinds);
	void PreviewForest(const FOTGForestPreset* Preset);
	// (the wind's parameter collections in a level before any grass or tree material reading them draws)
	static void EnsureWindCollections(UWorld* W);

	// (a point in the level, cm, as course metres x z / as the level's metres a rock is keyed by)
	FVector2D CourseXZ(const FVector& World) const;
	FVector2D RockXY(const FVector& World) const;

private:
	// (the parts reach the course's shared pieces - and one another - through it)
	friend struct FOTGCourseGround;
	friend struct FOTGCourseWater;
	friend struct FOTGCourseTrees;
	friend struct FOTGCourseFarLand;
	friend struct FOTGCourseProps;
	friend struct FOTGCourseTurf;
	FOTGCourseGround Ground;
	FOTGCourseWater Water;
	FOTGCourseTrees Trees;
	FOTGCourseFarLand FarLand;
	FOTGCourseProps Props;
	FOTGCourseTurf Turf;

	void LoadPictures();
	void SetPictures(UMaterialInstanceDynamic* M) const;
	class UHierarchicalInstancedStaticMeshComponent* Instances(class UStaticMesh* Mesh, const TArray<FTransform>& Xfs, int32 CullFrom, int32 CullTo);

	UPROPERTY() TObjectPtr<UOTGCourseData> Data;
	UPROPERTY() TMap<FName, TObjectPtr<UTexture2D>> Textures;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GroundMat;
	UPROPERTY() TArray<TObjectPtr<UActorComponent>> Parts;
	double LandCm = 0;
	// (the steps still to do, BuildInSteps, and what to say)
	TArray<TPair<FString, TFunction<void()>>> Steps;
	int32 StepsTotal = 0;
	TFunction<void(float, const FString&)> StepProgress;
	TFunction<void()> StepsDone;
	TMap<FString, struct otg::FPicture> MaskPixels;   // (the masks the grass reads, on the CPU)
};
