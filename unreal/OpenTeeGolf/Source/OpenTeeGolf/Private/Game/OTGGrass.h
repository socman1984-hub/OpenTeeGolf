// The grass on an exported course built at run time (AOTGRuntimeCourse): what a course pack's
// landscape grass does, for ground that's a mesh. Each kind grows in cells round the camera out to
// its draw distance: the patches placed on a worker thread from the course's masks (on a grid with
// jitter, kept where the mask, the water, the paths and the meadow's patches say), then added as
// instances; cells too far away dropped. A kind is either our own turf patch (build_course.py
// TURF_GRASS: SM_Turf_<Name>, made in game-assets mode) or Megascans plants from Fab (Content/Fab/
// Megascans/Plants/<folder>: every variant mesh of each, a patch taking one at random, drawn with
// our foliage material so they sway in the game's wind, their own billboards far off).

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
		FString Name;                  // (TURF_GRASS's key: SM_Turf_<Name>, M_Turf_<Name>)
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
	};

	class FGrass
	{
	public:
		// Set up for the course: its masks (by name: fairway, water ...), heights, size; the kinds'
		// meshes and materials (null where the game hasn't got them)
		void Init(AActor* Owner, UOTGCourseData* Data, const TMap<FString, FPicture>& Masks, double LandCm,
			const TMap<FString, TArray<TPair<UStaticMesh*, UMaterialInterface*>>>& Kinds);
		// Each frame: cells added round the camera, far ones dropped (a few at a time)
		void Update(const FVector& Camera);
		int32 Instances() const { return Count; }
		// (no worker still placing: before the course goes)
		void Wait() const { while (InFlight.IsValid() && InFlight->GetValue() > 0) FPlatformProcess::Sleep(0.002f); }

		static const TArray<FGrassKind>& Kinds();

	private:
		struct FCellKey { int32 Kind, X, Y; bool operator==(const FCellKey& O) const { return Kind == O.Kind && X == O.X && Y == O.Y; } };
		friend uint32 GetTypeHash(const FCellKey& K) { return HashCombine(HashCombine(::GetTypeHash(K.Kind), ::GetTypeHash(K.X)), ::GetTypeHash(K.Y)); }
		struct FCell { TArray<UInstancedStaticMeshComponent*> Comps; bool bPending = false; };
		using FPlaced = TArray<TArray<FTransform>>;   // (a cell's patches, by variant)

		FPlaced Place(int32 Kind, int32 CX, int32 CY) const;
		float Mask(const FString& Name, double X, double Y) const;
		double GroundZ(double X, double Y, FVector* OutNormal) const;

		TWeakObjectPtr<AActor> Owner;
		UOTGCourseData* Data = nullptr;
		TMap<FString, FPicture> Pics;
		double Land = 0;
		TArray<int32> KindIndex;                              // (kinds the game has a mesh for)
		TArray<TArray<UStaticMesh*>> Meshes;                   // (by kind, its variants)
		TArray<TArray<UMaterialInterface*>> Mats;
		TMap<FCellKey, FCell> Cells;
		TSharedPtr<TQueue<TPair<FCellKey, FPlaced>, EQueueMode::Mpsc>> Done;
		TSharedPtr<FThreadSafeCounter> InFlight;
		int32 Count = 0;
	};
}
