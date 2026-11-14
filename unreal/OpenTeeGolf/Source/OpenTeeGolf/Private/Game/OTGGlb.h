// A glTF binary (.glb) read at run time: every primitive's positions, normals, vertex colours and
// triangles, merged - what the exported course's water and props are (public/js/export-unreal.js,
// in the export's own frame: metres, x east, y up, z south of its north-west corner).

#pragma once

#include "CoreMinimal.h"

namespace otg
{
	struct FGlbMesh
	{
		TArray<FVector3f> Positions;
		TArray<FVector3f> Normals;     // (empty if the file has none)
		TArray<FLinearColor> Colors;   // (empty if the file has none)
		TArray<int32> Indices;
	};

	bool ReadGlb(const FString& File, FGlbMesh& Out);
}
