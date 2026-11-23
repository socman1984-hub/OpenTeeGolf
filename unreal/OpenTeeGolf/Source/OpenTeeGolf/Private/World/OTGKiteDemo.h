// The KiteDemo pack's pieces the game uses (Content/KiteDemo, Epic's free Open World Demo
// Collection: not in git - each machine adds it): its heather and wildflowers (the grass designer's
// plants), two of its trees (models the tree designer can draw a species or a tree as) and its rocks
// (the rocks' looks). Only these are cooked (DefaultGame.ini DirectoriesToAlwaysCook), into their
// own pak chunk (make_game_assets.py SCENERY_CHUNKS: chunk 5).
//
// Where the pack isn't in the game, none of it is offered and anything a course saved with it is
// drawn as the game's own (a tree by its species, a rock as the game's stones, a plant not at all).

#pragma once

#include "CoreMinimal.h"

namespace otgkite
{
	// A plant for the tall grass: its meshes and how it grows when added (otg::FGrass::PlantKind)
	struct FPlant
	{
		const TCHAR* Key;              // (kite_<name>: a grass kind's Fab entry)
		const TCHAR* Label;
		TArray<const TCHAR*> Meshes;   // (object paths)
		float Density, DrawTo, ScaleLo, ScaleHi, PatchM, PatchLo, PatchHi;
	};

	inline const TArray<FPlant>& Plants()
	{
		static const TArray<FPlant> P = {
			{ TEXT("kite_heather"), TEXT("Heather"), { TEXT("/Game/KiteDemo/Environments/Foliage/Flowers/Heather/SM_Heather_Mesh_Clumps2.SM_Heather_Mesh_Clumps2") },
				180, 150, 0.8f, 1.25f, 18, 0.34f, 0.46f },
			{ TEXT("kite_ferns"), TEXT("Bracken ferns"), { TEXT("/Game/KiteDemo/Environments/Foliage/Ferns/SM_Fern_01.SM_Fern_01"), TEXT("/Game/KiteDemo/Environments/Foliage/Ferns/SM_Fern_02.SM_Fern_02"), TEXT("/Game/KiteDemo/Environments/Foliage/Ferns/SM_Fern_03.SM_Fern_03") },
				60, 90, 0.7f, 1.1f, 12, 0.40f, 0.48f },
			{ TEXT("kite_yarrow"), TEXT("Yarrow"), { TEXT("/Game/KiteDemo/Environments/Foliage/Flowers/Yarrow/SM_Yarrow.SM_Yarrow"), TEXT("/Game/KiteDemo/Environments/Foliage/Flowers/Yarrow/SM_Yarrow3.SM_Yarrow3") },
				90, 70, 0.8f, 1.2f, 9, 0.42f, 0.48f },
			{ TEXT("kite_buttercup"), TEXT("Buttercups"), { TEXT("/Game/KiteDemo/Environments/Foliage/Flowers/Buttercup/SM_Buttercup_Patch_01.SM_Buttercup_Patch_01") },
				50, 80, 0.8f, 1.2f, 10, 0.42f, 0.48f },
			{ TEXT("kite_scabious"), TEXT("Field scabious"), { TEXT("/Game/KiteDemo/Environments/Foliage/Flowers/FieldScabious/SM_FieldScabious_01.SM_FieldScabious_01") },
				60, 70, 0.8f, 1.2f, 9, 0.42f, 0.48f },
			{ TEXT("kite_bogmyrtle"), TEXT("Bog myrtle"), { TEXT("/Game/KiteDemo/Environments/Foliage/BogMyrtle_01/BogMyrtle_01.BogMyrtle_01"), TEXT("/Game/KiteDemo/Environments/Foliage/BogMyrtleBush_01/BogMyrtleBush_01.BogMyrtleBush_01"), TEXT("/Game/KiteDemo/Environments/Foliage/BogMyrtleBush_02/BogMyrtleBush_02.BogMyrtleBush_02") },
				30, 90, 0.8f, 1.2f, 14, 0.40f, 0.47f },
			{ TEXT("kite_fieldgrass"), TEXT("Field grass"), { TEXT("/Game/KiteDemo/Environments/Foliage/Grass/FieldGrass/SM_FieldGrass_01.SM_FieldGrass_01") },
				300, 70, 0.9f, 1.3f, 16, 0.33f, 0.40f },
			{ TEXT("kite_leaves"), TEXT("Fallen leaves"), { TEXT("/Game/KiteDemo/Environments/Foliage/Leaves/SM_DeadLeaves.SM_DeadLeaves") },
				80, 50, 0.8f, 1.3f, 12, 0.40f, 0.47f },
		};
		return P;
	}
	inline const FPlant* Plant(const FString& Key)
	{
		return Plants().FindByPredicate([&](const FPlant& P) { return Key == P.Key; });
	}

	// The tree designer's models from the pack (FOTGTreeDesign::Models: scotspine, hilltree)
	inline TArray<const TCHAR*> TreeMeshes(const FString& Model)
	{
		if (Model == TEXT("scotspine"))
			return { TEXT("/Game/KiteDemo/Environments/Trees/ScotsPine_01/ScotsPine_01.ScotsPine_01"), TEXT("/Game/KiteDemo/Environments/Trees/ScotsPineTall_01/ScotsPineTall_01.ScotsPineTall_01") };
		if (Model == TEXT("hilltree"))
			return { TEXT("/Game/KiteDemo/Environments/Trees/HillTree_02/HillTree_02.HillTree_02"), TEXT("/Game/KiteDemo/Environments/Trees/HillTree_Tall_02/HillTree_Tall_02.HillTree_Tall_02") };
		return {};
	}

	// The rocks' looks from the pack (FOTGTreeDesign::RockModels)
	inline TArray<const TCHAR*> RockMeshes(const FString& Model)
	{
		if (Model == TEXT("mossy")) return { TEXT("/Game/KiteDemo/Environments/Rocks/Medium_Boulder_001/Medium_Boulder_001.Medium_Boulder_001") };
		if (Model == TEXT("grey")) return { TEXT("/Game/KiteDemo/Environments/Rocks/Medium_Boulder_002/Medium_Boulder_LowPoly.Medium_Boulder_LowPoly") };
		if (Model == TEXT("river")) return { TEXT("/Game/KiteDemo/Environments/Rocks/River_Rock_01/SM_River_Rock_01.SM_River_Rock_01") };
		if (Model == TEXT("field")) return { TEXT("/Game/KiteDemo/Environments/Rocks/Boulder_05a/SM_Boulder05a.SM_Boulder05a") };
		if (Model == TEXT("plains")) return { TEXT("/Game/KiteDemo/Environments/Rocks/LargePlainsBoulder002/SM_LargePlainsBoulder002.SM_LargePlainsBoulder002") };
		if (Model == TEXT("volcanic"))
			return { TEXT("/Game/KiteDemo/Environments/Rocks/Large_Volcanic_Rock_001/LargeVolcanicRock_001.LargeVolcanicRock_001"), TEXT("/Game/KiteDemo/Environments/Rocks/Large_Volcanic_Rock_001/LargeVolcanicRock_002.LargeVolcanicRock_002"),
				TEXT("/Game/KiteDemo/Environments/Rocks/Large_Volcanic_Rock_001/LargeVolcanicRock_003.LargeVolcanicRock_003"), TEXT("/Game/KiteDemo/Environments/Rocks/Large_Volcanic_Rock_001/LargeVolcanicRock_004.LargeVolcanicRock_004") };
		return {};
	}
}
