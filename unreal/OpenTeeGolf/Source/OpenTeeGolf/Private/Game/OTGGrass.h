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
#include "OTGTextures.h"

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
		TArray<FString> Fab;           // Megascans plants (their folders under Content/Fab/Megascans/Plants), or none: our own patch
		float Margin = 0;              // (m: kept this far inside its surface, clear of the mown ones - a clump is wide)
		float CellM = 16;              // (its cells, m a side: bigger for the far-reaching kinds, fewer components)
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
		const TArray<FGrassKind>& Kinds() const { return KindList; }
		// (a kind's first mesh's height, cm, at scale 1; 0 if it has none)
		float MeshHeightCm(int32 Kind) const;
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

	private:
		struct FCellKey { int32 Kind, X, Y; bool operator==(const FCellKey& O) const { return Kind == O.Kind && X == O.X && Y == O.Y; } };
		friend uint32 GetTypeHash(const FCellKey& K) { return HashCombine(HashCombine(::GetTypeHash(K.Kind), ::GetTypeHash(K.X)), ::GetTypeHash(K.Y)); }
		struct FCell { TArray<UInstancedStaticMeshComponent*> Comps; bool bPending = false; };
		using FPlaced = TArray<TArray<FTransform>>;   // (a cell's patches, by variant)

		FPlaced Place(int32 Kind, int32 CX, int32 CY) const;
		// (how likely a patch of the kind is at X, Y - the level's cm from the origin - before its random keep)
		float Chance(const FGrassKind& K, double X, double Y) const;
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
		TSharedPtr<TQueue<TPair<FCellKey, FPlaced>, EQueueMode::Mpsc>> Done;
		TSharedPtr<FThreadSafeCounter> InFlight;
		int32 Count = 0;
	};
}
