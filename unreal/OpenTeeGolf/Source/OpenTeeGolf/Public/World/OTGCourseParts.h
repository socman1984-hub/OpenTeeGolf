// The runtime course's parts (AOTGRuntimeCourse, OTGRuntimeCourse.h): each part of the course built
// and kept by a class of its own, which the course owns and calls in order. Each reaches the course
// (its data, pictures, ground material and the other parts) through Course.
//
//  - FOTGCourseGround: the ground - the heightmap as tiles of mesh, the ponds' banks (OTGCourseGround.cpp)
//  - FOTGCourseWater: ponds, creeks and the sea - Tidewright water, or the game's own (OTGCourseWater.cpp,
//    OTGWater.cpp, OTGRiverRocks.cpp)
//  - FOTGCourseTrees: the trees and rocks, as the tree designer has them, and picking them (OTGCourseTrees.cpp)
//  - FOTGCourseFarLand: the land beyond the course, out to the horizon (OTGCourseFarLand.cpp)
//  - FOTGCourseProps: cart paths, bridges, buildings, houses and cars (OTGCourseProps.cpp, OTGHouses.cpp,
//    OTGCars.cpp)
//  - FOTGCourseTurf: the grass and the turf's colours, as the grass designer has them (OTGCourseGrass.cpp)
//
// They're plain structs, not USTRUCTs: the objects they hold are kept from the garbage collector by
// the course (AOTGRuntimeCourse::AddReferencedObjects -> each part's AddReferencedObjects).

#pragma once

#include "CoreMinimal.h"
#include "World/OTGGrassFwd.h"
#include "World/OTGTreeDesign.h"

class AOTGRuntimeCourse;
class FReferenceCollector;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UInstancedStaticMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;
class ATidewrightWater;
class ATidewrightRiver;
namespace otg { struct FGlbMesh; }

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

// The ground: the heightmap as tiles of mesh made here (four levels of detail each), with the game's
// ground material reading the course's photo, masks and mowing directions; the ponds' banks lifted
// clear of the water and held where a creek leaves a pond
struct FOTGCourseGround
{
	AOTGRuntimeCourse* Course = nullptr;

	void BuildGround(TFunction<void(float, const FString&)> Progress);
	void BuildGroundTiles(int32 From, int32 To);
	void PrepareGround();
	void LiftPondBanks();
	void SettleWater();
	void HoldPonds();
	double GroundAt(double X, double Y) const;
	// (the ground's tiles near the course - its surfaces or trees in them or the next ones - drawn;
	// the rest left out, the land round it showing there: PlanGroundTiles. Empty: all drawn)
	void PlanGroundTiles();
	bool GroundDrawnAt(double XCm, double YCm) const;
	void AddReferencedObjects(FReferenceCollector& Collector);

	TArray<FIntPoint> BermCells;   // (the ground's samples HoldPonds raised: dry grass, not the export's water or stream bed)
	TArray<uint16> GroundHeights;   // (the heights the ground is drawn with: the course's, the ponds' banks lifted clear of the water)
	TArray<TObjectPtr<UStaticMeshComponent>> Tiles;
	// (the ground's tiles, PrepareGround: their count a side and samples a side)
	int32 TileCount = 0, TileQuads = 64;
	TArray<bool> TileKeep;
};

// The water: the ponds and streams (Tidewright lakes and rivers, or the game's own water.glb), the
// creeks as drawn between the ponds, and the sea (a Tidewright ocean, or the game's own)
struct FOTGCourseWater
{
	AOTGRuntimeCourse* Course = nullptr;

	// (the highest water surface over a point, world cm, or a huge negative: none - OTGWater.cpp)
	double WaterTopAt(const FVector& P) const;
	// (the same, but only where the water stands over the ground there - not a still pond's sheet
	// running on under its banks: where rain's rings go)
	double OpenWaterAt(const FVector& P) const;
	void BuildWater();
	// (the ponds and rivers as Tidewright water - OTGWater.cpp; OutGone: the game's own water's points under them)
	bool BuildTidewright(TArray<bool>& OutGone);
	bool BuildStillPond(const TArray<FVector>& Poly, double Level, const TArray<FVector2D>* Round = nullptr);
	void UpdateTidewright();
	// (the sea as a Tidewright ocean, with a shoreline for the surf: false - none, the game's own)
	bool BuildTidewrightSea(const otg::FGlbMesh& Mesh, const FBox& Box, const FVector2D& Swell, double LevelM);
	void BuildSea();
	void RedrawPondMasks();
	void RiverAroundRocks(ATidewrightRiver* R);   // (OTGRiverRocks.cpp: the water parts round a rock)
	// The creeks as drawn (OTGWater.cpp PlanCreeks): each piece of a river between the ponds it runs
	// through - its centre line (cm, the level's frame; z its water level, falling downstream) and width
	void PlanCreeks();
	void AddBeach(const TArray<float>& FromWater, const TArray<double>& Gs, int32 Size, const FVector2D& Min, const FVector2D& Cell, double Level);
	void AddReferencedObjects(FReferenceCollector& Collector);

	struct FPondHold { int32 Pond = 0; FVector2D At; };
	TArray<FPondHold> PondHolds;   // (where a creek leaves a pond sitting over it: PlanCreeks; HoldPonds banks the pond there)
	// (water.glb's surface, its levels smoothed, read before the ground: SettleWater)
	TArray<FVector3f> WaterPos;
	TArray<int32> WaterIdx;
	bool bWaterSettled = false;
	TArray<TObjectPtr<ATidewrightWater>> Lakes;
	TArray<TObjectPtr<ATidewrightRiver>> Rivers;
	TObjectPtr<ATidewrightWater> Sea;
	TArray<FBox2D> RiverBoxes;   // (Rivers' footprints, in step)
	TArray<TArray<FVector2D>> PondOutlines;   // (each lake's outline, rounded, cm in the level's frame: LiftPondBanks; a still pond's water is laid over it)
	TArray<double> PondLevels;   // (each lake's water level, cm, in water_bodies.json's order: LiftPondBanks)
	struct FCreek { TArray<FVector> Pts; TArray<double> W; };
	TArray<FCreek> Creeks;
	bool bCreeksPlanned = false;
	double TidewrightCheckAt = 0;
	// (the sea's level, metres in the export's frame: BuildSea. bSea false: no sea)
	bool bSea = false;
	double SeaLevelM = 0;
};

// The trees and rocks: drawn from trees.json and boulders.json as the course's tree design has them,
// remembered as drawn for picking, and the picked one marked (a gold ring round it, a gold marker
// above it)
struct FOTGCourseTrees
{
	AOTGRuntimeCourse* Course = nullptr;

	void BuildTrees();
	void BuildBoulders();
	const FOTGTreeDesign& TreeDesign() const;
	void SetTreeDesign(const FOTGTreeDesign& Design);
	bool SaveTreeDesign(FString* Where = nullptr) const;
	FOTGTreeDesign SavedTreeDesign() const;
	TArray<TPair<FString, int32>> TreeSpecies() const;
	bool NearestTree(const FString& Kind, const FVector& From, FVector& OutBase, double& OutHeightCm) const;
	bool Pick(const FVector& Origin, const FVector& Dir, double MaxT, FOTGPicked& Out) const;
	bool Picked(bool bRock, double X, double Z, FOTGPicked& Out) const;
	void MarkPicked(const FOTGPicked& P);
	// The design's forests (by their place in it) outlined on the ground with beads, while it's asked
	// for each frame (the tree designer: the forests picked)
	void MarkForests(const TArray<int32>& Which);
	// The trees standing in a strip From to To (the level's cm; HalfWidth either side) hidden from view - only from
	// view: the ball's trees are the course data's - and the ones hidden before shown again. From == To: none hidden
	void SeeThrough(const FVector& From, const FVector& To, double HalfWidthCm);
	void UpdateMarks();   // (each frame: the marks gone once the designer stops marking; the marker bobbing)
	void AddReferencedObjects(FReferenceCollector& Collector);

	TObjectPtr<UInstancedStaticMeshComponent> PickRing;
	TObjectPtr<UStaticMeshComponent> PickCone;
	double PickMarkedAt = -1;
	TObjectPtr<UInstancedStaticMeshComponent> ForestBeads;
	TMap<UStaticMesh*, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> MeshComps;   // (each tree model's instances)
	TSet<UStaticMesh*> SeeThroughMeshes;   // (the tree models whose materials can draw them see-through)
	TMap<int32, FTransform> SeeThroughHidden;   // (DrawnTrees index -> its transform, while it's hidden)
	FString SeeThroughKey;
	double ForestMarkedAt = -1;
	FString ForestMarkKey;   // (what's outlined now: drawn again only when it changes)
	FVector PickConeAt = FVector::ZeroVector;
	struct FTreeRec { double X = 0, Y = 0, Z = 0, H = 0; FString Kind; };   // (trees.json: cm)
	TArray<FTreeRec> TreeRecs;
	struct FRockRec { FVector P = FVector::ZeroVector; double Size = 0, Yaw = 0, Flat = 1; int32 Kind = 0; };   // (boulders.json)
	TArray<FRockRec> RockRecs;
	bool bTreesRead = false, bRocksRead = false;
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> TreeComps;
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> RockComps;
	int32 TreeCount = 0, RockCount = 0;
	// (each tree and rock as drawn, for picking: foot, height or radius, cm; key; species)
	struct FDrawn { FVector Base; double H = 0, R = 0; double X = 0, Z = 0; FString Kind; bool bAdded = false; UStaticMesh* Mesh = nullptr; bool bColourable = false; int32 Instance = INDEX_NONE; };
	TArray<FDrawn> DrawnTrees, DrawnRocks;
};

// The land beyond the course, out to the horizon: the real land round it (farland.json) or rings of
// hills made up
struct FOTGCourseFarLand
{
	AOTGRuntimeCourse* Course = nullptr;

	void BuildFarLand();
	bool BuildRealFarLand();   // (the export's farland.json: real heights and photos; false if it hasn't one)
};

// The props: cart paths, bridges, buildings (Hearthline houses on their footprints: OTGHouses.cpp)
// and parked cars (a car pack's: OTGCars.cpp), and the path mask drawn again under the paths
struct FOTGCourseProps
{
	AOTGRuntimeCourse* Course = nullptr;

	void BuildProps();
	void RedrawPathMask(const otg::FGlbMesh& G);
	// (the parked cars: a car pack's on the export's box cars' spots, or none - OTGCars.cpp. False: draw the boxes)
	bool PlaceCars(const otg::FGlbMesh& G);
	// (the houses as Hearthline houses - OTGHouses.cpp - and the footprints they stand on, in the
	// buildings glb's frame: the export's own blocks there go)
	void BuildHouses();
	bool UnderHouse(const FVector2D& GlbXZ) const;

	struct FHouseFoot { TArray<FVector2D> Pts; FBox2D Box = FBox2D(ForceInit); };
	TArray<FHouseFoot> HouseFeet;
};

// The grass and the turf's colours: the grass streamed round the camera (otg::FGrass), its kinds and
// colours as the grass designer has them, the fairways' and greens' looks, and the turf's palette on
// the ground and the blades (OTGPalette.h)
struct FOTGCourseTurf
{
	AOTGRuntimeCourse* Course = nullptr;

	void BuildGrass();
	TMap<FString, TArray<TPair<UStaticMesh*, UMaterialInterface*>>> GrassMeshes(const TArray<otg::FGrassKind>& Kinds);
	const TArray<otg::FGrassKind>& GrassKinds() const;
	void SetGrassKinds(const TArray<otg::FGrassKind>& Kinds);
	bool SaveGrassKinds(FString* Where = nullptr) const;
	TArray<FString> GrassPlants() const;
	float GrassHeightCm(const FString& Kind) const;
	int32 GrassInstances() const;
	TArray<otg::FGrassKind> SavedGrassKinds() const;
	FOTGTurfLook TurfLook(const FString& Surface) const { const FOTGTurfLook* L = TurfLooks.Find(Surface); return L ? *L : FOTGTurfLook(); }
	void SetTurfLook(const FString& Surface, const FOTGTurfLook& Look);
	bool SaveTurfLooks(FString* Where = nullptr) const;
	TMap<FString, FOTGTurfLook> SavedTurfLooks() const;
	FLinearColor TurfLookTint(const FString& Kind) const;
	void SetGrassColours(const TArray<otg::FGrassKind>& Kinds);
	bool FindGrass(const FString& Kind, const FVector& From, FVector& Out) const;
	bool FindSurface(const FString& Mask, const FVector& From, FVector& Out, double InsideM = 3) const;
	void DumpGrassTints() const;
	void SetGrassParam(const FString& Kind, const FString& Param, const FLinearColor& V);
	void TintGrass();
	void TintGround();   // (the ground's colour on each surface: the turf's palette, OTGPalette.h)
	void AddReferencedObjects(FReferenceCollector& Collector);

	TMap<FString, FOTGTurfLook> TurfLooks;
	TMap<FString, TArray<TPair<UStaticMesh*, UMaterialInterface*>>> GrassMeshCache;
	// (each kind's materials the designer's colour tints: our turf's Tint, a Megascans plant's own
	// tints - their copies drawn in place of its materials, GrassSwaps)
	// (bGs: a Greensward mat's material - Base and BaseThrough its root and tip colours)
	struct FGrassTint { TWeakObjectPtr<UMaterialInstanceDynamic> M; bool bFab = false; FLinearColor Base, BaseThrough, Controls; bool bGs = false; };
	TMap<FString, TArray<FGrassTint>> GrassTints;
	TMap<UMaterialInterface*, UMaterialInterface*> GrassSwaps;
	TSharedPtr<class otg::FGrass> Grass;
	// (the course's own kinds, as the grass designer has them - grass.json; what grows is these with
	// the course's grass biomes - biomes.json, OTGBiomes.h: Regrow)
	TSharedPtr<TArray<otg::FGrassKind>> OwnKindsP;   // (a pointer: the kinds are only declared here)
	TArray<otg::FGrassKind>& Own();
	TSharedPtr<struct FOTGCourseBiomes> Biomes;
	TArray<otg::FGrassKind> Grown() const;
	// (the studio's preview: these kinds, no grass.json or biomes.json)
	void BuildGrassFrom(const TArray<otg::FGrassKind>& Kinds);
	// (the grass designer in the game: the course's biomes as they are, changed - the grass grown again
	// with them - and saved to biomes.json, as the course editor does)
	bool CourseBiomes(struct FOTGCourseBiomes& Out) const;
	void SetCourseBiomes(const struct FOTGCourseBiomes& B);
	bool SaveCourseBiomes() const;
	TArray<TObjectPtr<UMaterialInstanceDynamic>> TurfMats;
	TArray<TObjectPtr<UStaticMesh>> TurfMeshes;   // (kept loaded: the grass makes cells of them as the camera moves)
};
