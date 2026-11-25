// The grass on an exported course built at run time (AOTGRuntimeCourse): what a course pack's
// landscape grass does, for ground that's a mesh. Each kind grows in cells round the camera out to
// its draw distance: the patches placed on a worker thread from the course's masks (on a grid with
// jitter, kept where the mask, the water, the paths and the meadow's patches say), then added as
// instances; cells too far away dropped. A kind is either our own turf patch (build_course.py
// TURF_GRASS: SM_Turf_<Name>, made in game-assets mode) or Megascans plants from Fab (Content/Fab/
// Megascans/Plants/<folder>: every variant mesh of each, a patch taking one at random, drawn with
// our foliage material so they sway in the game's wind, their own billboards far off).
//
// The kinds are the game's own (DefaultKinds) as the course's grass designer changed them
// (SOTGGrassDesigner; saved in the course's folder as grass.json: LoadKinds, SaveKinds): which
// grow, on the rough or the tall grass, how dense, how tall, how far out, in what patches, and
// Megascans plants added. SetKinds grows them again at once. The rough stays one even colour: only
// kinds in its own colours (bEven) grow on it; the others only in the tall grass.

#pragma once

#include "CoreMinimal.h"
#include "Course/OTGTextures.h"

class AActor;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UOTGCourseData;
class UStaticMesh;

namespace otg
{
	struct FGrassKind
	{
		FString Name;                  // (TURF_GRASS's key: SM_Turf_<Name>, M_Turf_<Name>; fab_<folder> for an added plant)
		FString Label;                 // (as the designer shows it)
		FString FarOf;                 // (a far-off copy of that kind: follows it, not shown on its own)
		bool bOn = true;
		bool bEven = false;            // in the rough's own colours: may grow on the rough
		bool bAdded = false;           // a Megascans plant added in the designer
		TArray<FString> Masks;         // the surfaces it grows on
		float Density = 0;             // patches per 100 m2
		float DrawFrom = 0, DrawTo = 0; // m: fading out between them
		float Jitter = 1, ScaleLo = 0.85f, ScaleHi = 1.2f;
		// the meadow's patches: noise this many metres across, between these (and the other patches if Invert)
		float PatchM = 0, PatchLo = 0, PatchHi = 0;
		bool bInvert = false;
		float MinDrawM = 0;            // (drawn only beyond this, m: the far kinds)
		TArray<FString> Fab;           // Megascans plants (their folders under Content/Fab/Megascans/Plants), or none: our own patch;
		                               // or a KiteDemo plant (kite_<name>: OTGKiteDemo.h)
		bool IsKite() const { return Fab.Num() && Fab[0].StartsWith(TEXT("kite_")); }
		float Margin = 0;              // (m: kept this far inside its surface, clear of the mown ones - a clump is wide)
		bool bMown = false;            // a mown cut's own blades (make_turf.py: green, collar, fairway): on its own surface, not kept off it
		float BandM = 0;               // (the first cut: grows only this many metres out from a fairway's, tee's or collar's outline)
		float CellM = 16;              // (its cells, m a side: bigger for the far-reaching kinds, fewer components)
		// (a grass biome's kind - OTGBiomes.h: it grows only where the course's biome map has this biome,
		// 0 the course's own tall grass; -1 anywhere its surface is. Its name then "<kind>@<biome>")
		int32 Biome = -1;
		FString BaseName() const { int32 At; return Name.FindChar(TEXT('@'), At) ? Name.Left(At) : Name; }
		// Greensward's grass mats (the plugin's /Greensward/Mats/SM_GS_<name>: 50 cm square Nanite turf),
		// tiled on a grid TileCm a side, drawn with its static grass material in this kind's colours
		// (OTGRuntimeCourse GrassMeshes); none: our own patch. -oldgrass: none anywhere
		TArray<FString> GsMats;
		float TileCm = 0;              // (a tiled patch's size, cm: the mown cuts' are 20)
		bool IsGreensward() const { return GsMats.Num() > 0; }
		// its colour, as the designer turned it (within believable limits: ClampColour): the hue turned
		// this many degrees (- toward gold, + toward blue-green), its strength and brightness times these
		float Hue = 0, Sat = 1, Bright = 1;
		static constexpr float HueMax = 40, SatLo = 0.4f, SatHi = 1.6f, BrightLo = 0.5f, BrightHi = 1.6f;
		void ClampColour();
		bool IsOwnColour() const { return Hue == 0 && Sat == 1 && Bright == 1; }
		// (what to multiply a material's colour by: Ref, the colour it draws, turned as above)
		FLinearColor TintFor(const FLinearColor& Ref) const;
	};

	class FGrass
	{
	public:
		// Set up for the course: its masks (by name: fairway, water ...), heights, size; the kinds'
		// meshes and materials (null where the game hasn't got them)
		void Init(AActor* Owner, UOTGCourseData* Data, const TMap<FString, FPicture>& Masks, double LandCm,
			const TArray<FGrassKind>& Kinds, const TMap<FString, TArray<TPair<UStaticMesh*, UMaterialInterface*>>>& Meshes);
		// Other kinds, grown again at once (every cell dropped; the near ones come back in a moment)
		void SetKinds(const TArray<FGrassKind>& Kinds, const TMap<FString, TArray<TPair<UStaticMesh*, UMaterialInterface*>>>& Meshes);
		// (the kinds' colours only, nothing grown again: their materials are tinted by the course)
		void SetColours(const TArray<FGrassKind>& Kinds);
		// (a mesh's own material drawn as another: the Megascans plants' tinted copies)
		void SetSwaps(const TMap<UMaterialInterface*, UMaterialInterface*>& InSwaps) { Swaps = InSwaps; }
		// Where a kind grows thickly nearest From (the level's cm), not right under it: false if nowhere
		// within MaxM metres
		bool FindNear(const FString& Kind, const FVector& From, FVector& Out, double MaxM = 700) const;
		// A spot well inside a surface (its mask: fairway, green, rough ...) nearest From, InsideM metres
		// in from every edge and on no other surface - for photographing its colour (OTG.Look.Swatch)
		bool FindSurface(const FString& Mask, const FVector& From, FVector& Out, double InsideM = 3, double MaxM = 600) const;
		const TArray<FGrassKind>& Kinds() const { return KindList; }
		// The grass drawn in cells within R cm of At (world cm): OTG.Look.Bake
		TArray<UPrimitiveComponent*> ComponentsNear(const FVector& At, double R) const;
		// (a kind's first mesh's height, cm, at scale 1; 0 if it has none)
		float MeshHeightCm(int32 Kind) const;
		// How wide the first cut is (m: its BandM), or 0 when it's off - the rough grows to the fairway then
		static float FirstCutM(const TArray<FGrassKind>& Kinds);
		// Each frame: cells added round the camera, far ones dropped (a few at a time)
		void Update(const FVector& Camera);
		int32 Instances() const { return Count; }
		// (no worker still placing: before the course goes)
		void Wait() const { while (InFlight.IsValid() && InFlight->GetValue() > 0) FPlatformProcess::Sleep(0.002f); }

		// The game's own kinds (build_course.py TURF_GRASS, and the Megascans grasses)
		static const TArray<FGrassKind>& DefaultKinds();
		// The course's (grass.json in its folder, over the game's own), and saving them there
		static TArray<FGrassKind> LoadKinds(const FString& Dir);
		static bool SaveKinds(const FString& Dir, const TArray<FGrassKind>& Kinds);
		// The far-off copies made to follow the kinds they copy (after any change)
		static void Derive(TArray<FGrassKind>& Kinds);
		// A Megascans plant (its folder under Content/Fab/Megascans/Plants) as a kind of tall grass
		static FGrassKind PlantKind(const FString& Folder);
		// (a kind as grass.json has it, and read back over Out's kind of that name - or added, a plant -
		// as LoadKinds does: false if it names nothing the game has)
		static TSharedRef<class FJsonObject> KindToJson(const FGrassKind& K);
		static bool ApplyKindJson(TArray<FGrassKind>& Out, const TSharedPtr<class FJsonObject>& O, double Version = 2);
		// Which grass biome grows where (OTGBiomes.h FOTGCourseBiomes::Raster): Size x Size cells over the
		// land, each a biome's number (0 the course's own tall grass). Kinds with a Biome grow only in
		// their own; none set: every kind everywhere its surface is
		void SetBiomeMap(TArray<uint8> Map, int32 Size);

	private:
		struct FCellKey { int32 Kind, X, Y; bool operator==(const FCellKey& O) const { return Kind == O.Kind && X == O.X && Y == O.Y; } };
		friend uint32 GetTypeHash(const FCellKey& K) { return HashCombine(HashCombine(::GetTypeHash(K.Kind), ::GetTypeHash(K.X)), ::GetTypeHash(K.Y)); }
		struct FCell { TArray<UInstancedStaticMeshComponent*> Comps; bool bPending = false; };
		using FPlaced = TArray<TArray<FTransform>>;   // (a cell's patches, by variant)

		FPlaced Place(int32 Kind, int32 CX, int32 CY) const;
		// (how likely a patch of the kind is at X, Y - the level's cm from the origin - before its random keep)
		float Chance(const FGrassKind& K, double X, double Y) const;
		float DeepRamp(float W, double X, double Y) const;
		int32 BiomeAt(double X, double Y) const;
		TArray<uint8> BiomeMap;
		int32 BiomeSize = 0;
		TMap<UMaterialInterface*, UMaterialInterface*> Swaps;
		float Mask(const FString& Name, double X, double Y) const;
		double GroundZ(double X, double Y, FVector* OutNormal) const;

		TWeakObjectPtr<AActor> Owner;
		UOTGCourseData* Data = nullptr;
		TMap<FString, FPicture> Pics;
		double Land = 0;
		void Clear();
		TArray<FGrassKind> KindList;
		TArray<int32> KindIndex;                              // (kinds on, that the game has a mesh for)
		TArray<TArray<UStaticMesh*>> Meshes;                   // (by kind, its variants)
		TArray<TArray<UMaterialInterface*>> Mats;
		TMap<FCellKey, FCell> Cells;
		float FirstCut = 0;                                    // (FirstCutM of the kinds growing)
		TSharedPtr<TQueue<TPair<FCellKey, FPlaced>, EQueueMode::Mpsc>> Done;
		TSharedPtr<FThreadSafeCounter> InFlight;
		int32 Count = 0;
	};
}
