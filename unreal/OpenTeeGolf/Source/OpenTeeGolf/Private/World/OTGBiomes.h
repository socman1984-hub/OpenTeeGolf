// Grass biomes and forest presets, and the library they're kept in.
//
//  - A grass biome (FOTGBiome): a named mix of the tall grass's kinds - which grow, how many, how
//    tall, in what patches, what colour - made in the studio (SOTGStudio: the menu's Course builder >
//    Biomes and forests, with a live preview) from any of the game's grasses, Megascans and KiteDemo
//    plants. Only the tall grass (the deep rough, heather, meadows): the rough stays one even colour.
//  - A forest preset (FOTGForestPreset, OTGTreeDesign.h): a mix of species, how tall, how close.
//  - The library (OTGLibrary): Documents\OpenTeeGolf\Library\Biomes and \Forests, a .json each,
//    for every course; a few to start with, made the first time.
//  - A course's biomes (FOTGCourseBiomes, biomes.json in its folder - the course editor writes it):
//    the biome its tall grass is (none: its own, as the grass designer has it), and areas drawn on it
//    each with a biome of their own. The biomes it uses are kept in it, as they were picked, so the
//    course looks the same on another computer. The grass grows each area's kinds only inside it
//    (otg::FGrass::SetBiomeMap, FGrassKind::Biome).

#pragma once

#include "CoreMinimal.h"
#include "World/OTGGrass.h"
#include "World/OTGTreeDesign.h"

class UOTGCourseData;

struct FOTGBiome
{
	FString Name;
	TArray<otg::FGrassKind> Kinds;   // (the tall grass's kinds, with their far-off copies: Derive)

	// The game's tall grass (DefaultKinds on the "native" surface) as a biome: what a new one starts from
	static FOTGBiome TallGrass(const FString& Name);
	TSharedRef<class FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<class FJsonObject>& O, FOTGBiome& Out);
	int32 Growing() const;           // (kinds on, not counting far copies)
};

namespace OTGLibrary
{
	FString Dir();                                   // Documents\OpenTeeGolf\Library
	TArray<FOTGBiome> Biomes();                      // (by name; the starters made the first time)
	bool SaveBiome(const FOTGBiome& B);
	bool DeleteBiome(const FString& Name);
	const FOTGBiome* FindIn(const TArray<FOTGBiome>& L, const FString& Name);
	TArray<FOTGForestPreset> Forests();
	bool SaveForest(const FOTGForestPreset& P);
	bool DeleteForest(const FString& Name);
	FString FreeName(const FString& Base, const TArray<FString>& Taken);   // ("Heathland 2")
}

// An area of a course's tall grass with a biome of its own (course metres)
struct FOTGBiomeArea
{
	FString Biome;
	TArray<FVector2D> Pts;
	bool Contains(double X, double Z) const;
};

struct FOTGCourseBiomes
{
	FString Default;                 // (the tall grass's biome; empty: the course's own grass)
	TArray<FOTGBiome> Biomes;        // (those it uses, as picked)
	TArray<FOTGBiomeArea> Areas;

	static FOTGCourseBiomes Load(const FString& Dir);
	bool Save(const FString& Dir) const;
	bool IsEmpty() const { return Default.IsEmpty() && !Areas.Num(); }
	const FOTGBiome* Find(const FString& Name) const { return OTGLibrary::FindIn(Biomes, Name); }
	// (a library biome put in, or its copy here brought up to date; and those no longer used dropped)
	void Use(const FOTGBiome& B);
	void Tidy();
	// The kinds grown: Own's rough as it is; its tall grass as Default's biome has it (Biome 0); and each
	// area's biome's kinds as "<kind>@<n>", growing only in their areas (Biome n: AreaBiomes()[n-1])
	TArray<otg::FGrassKind> Grow(const TArray<otg::FGrassKind>& Own) const;
	TArray<FString> AreaBiomes() const;   // (the biomes the areas have, each once, in order)
	// Which biome where: Size x Size cells over the land (the level's frame, LandCm a side)
	TArray<uint8> Raster(const UOTGCourseData* D, double LandCm, int32 Size) const;
};
