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
#include "Game/OTGTreeDesign.h"
#include "OTGRuntimeCourse.generated.h"

class UOTGCourseData;
namespace otg { struct FGlbMesh; }
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UTexture2D;

// A tree or rock picked on the course (AOTGRuntimeCourse::Pick): where it stands, as its edit in
// the tree design is keyed (FOTGTreeDesign::TreeEdit, RockEdit), and what it is
struct FOTGPicked
{
	bool bRock = false;
	double X = 0, Z = 0;          // (a tree: course metres; a rock: the level's metres, as boulders.json)
	FString Kind;                 // (a tree's species as drawn)
	double HeightM = 0;           // (a tree's height as drawn; a rock's size across)
	bool bAdded = false;          // (added in the designer)
	bool bColourable = false;     // (a tree drawn with the game's own leaves, which take a colour; the packs' own don't)
	FVector Base = FVector::ZeroVector;   // (the level's cm: its foot)
	double RadiusM = 0;           // (its crown's reach, or a rock's half size)
};

// The mown turf's colour - the fairways (and tees), the greens - as the grass designer turns it:
// its hue (degrees, + bluer), colour strength and brightness (1: as it is). Saved for the course
// (turf.json)
struct FOTGTurfLook
{
	float Hue = 0, Sat = 1, Bright = 1;
	bool IsOwn() const { return Hue == 0 && Sat == 1 && Bright == 1; }
};

UCLASS()
class OPENTEEGOLF_API AOTGRuntimeCourse : public AActor
{
	GENERATED_BODY()

public:
	// (the highest water surface over a point, world cm, or a huge negative: none - OTGWater.cpp)
	double WaterTopAt(const FVector& P) const;
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
	// The fairways' and greens' colour ("fairway", "green"): as it is now, changed (seen at once),
	// saved, as saved
	FOTGTurfLook TurfLook(const FString& Surface) const { const FOTGTurfLook* L = TurfLooks.Find(Surface); return L ? *L : FOTGTurfLook(); }
	void SetTurfLook(const FString& Surface, const FOTGTurfLook& Look);
	bool SaveTurfLooks(FString* Where = nullptr) const;
	TMap<FString, FOTGTurfLook> SavedTurfLooks() const;
	FLinearColor TurfLookTint(const FString& Kind) const;   // (what a mown kind's blades multiply by: white, or its surface's look)
	// The kinds' colours changed at once (their materials tinted: nothing grown again), and where a
	// kind grows thickly nearest a point (the level's cm; false: nowhere near)
	void SetGrassColours(const TArray<otg::FGrassKind>& Kinds);
	bool FindGrass(const FString& Kind, const FVector& From, FVector& Out) const;
	bool FindSurface(const FString& Mask, const FVector& From, FVector& Out, double InsideM = 3) const;   // (well inside it: OTG.Look.Swatch)
	void DumpGrassTints() const;   // (testing: OTG.Grass.Tints)
	void SetGrassParam(const FString& Kind, const FString& Param, const FLinearColor& V);   // (testing: OTG.Grass.Param)

	// The tree designer's view (SOTGTreeDesigner): the trees and rocks drawn again with a design
	// (the course data's, which the ball's trees follow too: AOTGRound::RefreshTrees), saved for
	// the course (trees_design.json), the species on the course and how many of each, and the
	// nearest tree of a species to a point (the level's cm; false: none)
	const FOTGTreeDesign& TreeDesign() const;
	void SetTreeDesign(const FOTGTreeDesign& Design);
	bool SaveTreeDesign(FString* Where = nullptr) const;
	FOTGTreeDesign SavedTreeDesign() const;
	TArray<TPair<FString, int32>> TreeSpecies() const;
	// (the nearest of a species to From, not right under it - or, Kind "rock", the nearest rock)
	bool NearestTree(const FString& Kind, const FVector& From, FVector& OutBase, double& OutHeightCm) const;
	int32 TreesDrawn() const { return TreeCount; }
	int32 RocksDrawn() const { return RockCount; }
	// The tree or rock along a line from the camera (the level's cm), before the ground at MaxT cm:
	// false if none. And one again by where it stands (after the trees are drawn again)
	bool Pick(const FVector& Origin, const FVector& Dir, double MaxT, FOTGPicked& Out) const;
	bool Picked(bool bRock, double X, double Z, FOTGPicked& Out) const;
	// The picked one marked on the course (a gold ring round it, a gold marker above): called each
	// frame while it's picked - not called for a moment, the marks go
	void MarkPicked(const FOTGPicked& P);
	// (a point in the level, cm, as course metres x z / as the level's metres a rock is keyed by)
	FVector2D CourseXZ(const FVector& World) const;
	FVector2D RockXY(const FVector& World) const;

private:
	UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> PickRing;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PickCone;
	double PickMarkedAt = -1;
	TMap<FString, FOTGTurfLook> TurfLooks;
	FVector PickConeAt = FVector::ZeroVector;
	void LoadPictures();
	void BuildGround(TFunction<void(float, const FString&)> Progress);
	void BuildGroundTiles(int32 From, int32 To);
	void PrepareGround();
	void LiftPondBanks();
	void SettleWater();
	// (water.glb's surface, its levels smoothed, read before the ground: SettleWater)
	TArray<FVector3f> WaterPos;
	TArray<int32> WaterIdx;
	bool bWaterSettled = false;
	UPROPERTY() TArray<TObjectPtr<class ATidewrightWater>> Lakes;
	UPROPERTY() TArray<TObjectPtr<class ATidewrightRiver>> Rivers;
	UPROPERTY() TObjectPtr<class ATidewrightWater> Sea;
	TArray<FBox2D> RiverBoxes;   // (Rivers' footprints, in step)
	double TidewrightCheckAt = 0;
	double GroundAt(double X, double Y) const;
	void BuildTrees();
	void BuildBoulders();
	struct FTreeRec { double X = 0, Y = 0, Z = 0, H = 0; FString Kind; };   // (trees.json: cm)
	TArray<FTreeRec> TreeRecs;
	struct FRockRec { FVector P = FVector::ZeroVector; double Size = 0, Yaw = 0, Flat = 1; int32 Kind = 0; };   // (boulders.json)
	TArray<FRockRec> RockRecs;
	bool bTreesRead = false, bRocksRead = false;
	UPROPERTY() TArray<TObjectPtr<class UHierarchicalInstancedStaticMeshComponent>> TreeComps;
	UPROPERTY() TArray<TObjectPtr<class UHierarchicalInstancedStaticMeshComponent>> RockComps;
	int32 TreeCount = 0, RockCount = 0;
	// (each tree and rock as drawn, for picking: foot, height or radius, cm; key; species)
	struct FDrawn { FVector Base; double H = 0, R = 0; double X = 0, Z = 0; FString Kind; bool bAdded = false; class UStaticMesh* Mesh = nullptr; bool bColourable = false; };
	TArray<FDrawn> DrawnTrees, DrawnRocks;
	void BuildWater();
	// (the ponds and rivers as Tidewright water - OTGWater.cpp; OutGone: the game's own water's points under them)
	bool BuildTidewright(TArray<bool>& OutGone);
	bool BuildStillPond(const TArray<FVector>& Poly, double Level);
	void UpdateTidewright();
	// (the sea as a Tidewright ocean, with a shoreline for the surf: false - none, the game's own)
	bool BuildTidewrightSea(const otg::FGlbMesh& Mesh, const FBox& Box, const FVector2D& Swell, double LevelM);
	void BuildSea();
	void BuildFarLand();
	bool BuildRealFarLand();   // (the export's farland.json: real heights and photos; false if it hasn't one)
	void BuildProps();
	// (the houses as Hearthline houses - OTGHouses.cpp - and the footprints they stand on, in the
	// buildings glb's frame: the export's own blocks there go)
	void BuildHouses();
	struct FHouseFoot { TArray<FVector2D> Pts; FBox2D Box = FBox2D(ForceInit); };
	TArray<FHouseFoot> HouseFeet;
	bool UnderHouse(const FVector2D& GlbXZ) const;
	void BuildGrass();
	TMap<FString, TArray<TPair<UStaticMesh*, UMaterialInterface*>>> GrassMeshes(const TArray<otg::FGrassKind>& Kinds);
	TMap<FString, TArray<TPair<UStaticMesh*, UMaterialInterface*>>> GrassMeshCache;
	// (each kind's materials the designer's colour tints: our turf's Tint, a Megascans plant's own
	// tints - their copies drawn in place of its materials, GrassSwaps)
	// (bGs: a Greensward mat's material - Base and BaseThrough its root and tip colours)
	struct FGrassTint { TWeakObjectPtr<UMaterialInstanceDynamic> M; bool bFab = false; FLinearColor Base, BaseThrough, Controls; bool bGs = false; };
	TMap<FString, TArray<FGrassTint>> GrassTints;
	TMap<UMaterialInterface*, UMaterialInterface*> GrassSwaps;
	void TintGrass();
	void TintGround();   // (the ground's colour on each surface: the turf's palette, OTGPalette.h)
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
