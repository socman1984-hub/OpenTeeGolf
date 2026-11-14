// (OTGRuntimeCourse.h's view of the grass and pictures: Private/Game/OTGGrass.h, OTGTextures.h)

#pragma once

#include "CoreMinimal.h"

namespace otg
{
	struct FPicture { int32 W = 0, H = 0, Channels = 1; TArray<uint8> Pixels; };
	class FGrass;
}
