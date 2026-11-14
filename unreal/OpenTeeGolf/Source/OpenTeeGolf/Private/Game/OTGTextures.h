// Pictures from an exported course's folder as textures, at run time: decoded (PNG or JPEG), cut
// down to a size, given mips (box-filtered), and made into transient textures the course's materials
// read (AOTGRuntimeCourse).

#pragma once

#include "CoreMinimal.h"
#include "Game/OTGGrassFwd.h"

class UTexture2D;

namespace otg
{
	enum class ETexKind : uint8
	{
		Mask,   // greyscale, linear (the surface masks: distance fields)
		Photo,  // colour, sRGB (the aerial photo)
		Data,   // colour, linear (the mowing directions)
	};

	// The picture at File as a texture no bigger than MaxSize a side (null if it isn't there or
	// doesn't read)
	UTexture2D* LoadTexture(const FString& File, ETexKind Kind, int32 MaxSize, UObject* Outer);

	// A picture's pixels kept on the CPU too (the grass reads the masks where it grows)
	UTexture2D* LoadTexture(const FString& File, ETexKind Kind, int32 MaxSize, UObject* Outer, FPicture* OutPixels);
}
