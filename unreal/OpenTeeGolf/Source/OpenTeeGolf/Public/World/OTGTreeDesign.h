// How a course's trees and rocks look, as its tree designer left them (SOTGTreeDesigner; saved in
// the course's folder as trees_design.json): for each species on the course, the Unreal model it's
// drawn with (or the game's own choice), its size and how many (fewer: some taken out; more: others
// grown round them, off the mown grass, sand, paths and water), and for the rocks, whether they're
// there, their size and how many.
//
// And one tree or rock at a time, picked on the course in the designer: a tree's species, model,
// height and leaf colour changed, or it taken out; trees and rocks added where you click (Trees,
// Rocks: each found again by where it stands, to the decimetre).
//
// The same trees for the eye and for the ball: the export's trees.json (drawn) and play.json's
// trees (what the ball hits) are the same trees in the same order, and both go through ForEach
// (UOTGCourseData::ForEachDesignedTree), which decides by the tree's position - every time alike.

#pragma once

#include "CoreMinimal.h"

struct OPENTEEGOLF_API FOTGSpeciesLook
{
	FString Model = TEXT("auto");   // auto (the game's choice by height), beech, hornbeam, spruce, pine, scotspine, hilltree
	float Size = 1.f;               // x the tree's own height
	float Density = 1.f;            // 0 none .. 1 as mapped .. 3 three times as many
};

// One tree changed, taken out or added (course metres: where it stands as the design has it)
struct FOTGTreeEdit
{
	double X = 0, Z = 0;
	bool bAdded = false, bRemoved = false;
	FString Kind;                   // (its species instead; an added tree's own)
	FString Model;                  // (drawn as this instead: Models(); empty: as its species is)
	float Size = 1.f;               // x its height as it stands (an added tree: x Height)
	float Height = 12.f;            // m (an added tree)
	// its leaves' colour: the hue turned (- toward autumn gold and red, + toward blue-green) and how bright
	float Hue = 0.f, Bright = 1.f;
	static constexpr float HueLo = -70, HueHi = 30, BrightLo = 0.6f, BrightHi = 1.4f, SizeLo = 0.4f, SizeHi = 2.5f, HeightLo = 2, HeightHi = 40;
	bool HasColour() const { return Hue != 0 || Bright != 1; }
	// (the leaves' multiplier, less one: what the leaf material adds to 1 - BuildTrees's per-instance data)
	FLinearColor LeafTint() const;
};

// One rock taken out, resized or added (course metres)
struct FOTGRockEdit
{
	double X = 0, Z = 0;
	bool bAdded = false, bRemoved = false;
	float Size = 1.f;               // x its size (an added rock: metres across)
	FString Model;                  // (its look instead: RockModels; empty: as the course's rocks are)
	static constexpr float SizeLo = 0.3f, SizeHi = 4.f;
};

// A forest's make-up - a forest preset, made in the studio (SOTGStudio) and kept in the library
// (OTGBiomes.h OTGLibrary): its species, each with its share and the model it's drawn with, how tall,
// how close they stand, and the grass biome under them
struct FOTGForestSpecies
{
	FString Kind;                   // (a species: FOTGTreeDesign::AllSpecies)
	float Share = 1.f;              // (against the others': 0..10)
	FString Model;                  // (drawn as this: Models(); empty or auto: the game's choice)
};

struct FOTGForestPreset
{
	FString Name;
	TArray<FOTGForestSpecies> Species;
	float HeightLo = 12.f, HeightHi = 22.f;   // m
	float PerHa = 300.f;                      // trees a hectare, where a forest is planted to it
	FString Under;                            // (the grass biome under them, by name: none, the course's)
	static constexpr float PerHaLo = 20, PerHaHi = 1200, HeightMin = 2, HeightMax = 40;
	bool IsSet() const { return Species.ContainsByPredicate([](const FOTGForestSpecies& S) { return S.Share > 0 && !S.Kind.IsEmpty(); }); }
	// (the species a tree at X, Z is, by the shares - the same every time; null if none set)
	const FOTGForestSpecies* PickAt(double X, double Z) const;
	float HeightAt(double X, double Z) const;
	TSharedRef<class FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<class FJsonObject>& O, FOTGForestPreset& Out);
};

// A forest on the course: trees inside an outline (course metres) taken together - as many as
// mapped or more or fewer (Amount), larger or smaller, and as the preset has them (their species
// and models; empty: their own). bFill plants it out to the preset's spacing where it has fewer.
struct FOTGForest
{
	FString Name;
	TArray<FVector2D> Pts;
	FOTGForestPreset Preset;        // (a copy: the course keeps it as picked, the library may change)
	float Amount = 1.f, Size = 1.f;
	bool bFill = false;
	static constexpr float AmountLo = 0, AmountHi = 3, SizeLo = 0.5f, SizeHi = 2.f;
	bool Contains(double X, double Z) const;
	FBox2D Bounds() const;
	double AreaM2() const;
};

struct OPENTEEGOLF_API FOTGTreeDesign
{
	TMap<FString, FOTGSpeciesLook> Species;
	float AllSize = 1.f, AllDensity = 1.f;                     // (every species, on top of its own)
	bool bRocks = true;
	float RockSize = 1.f, RockDensity = 1.f;
	FString RockModel;                                         // (the rocks' look: RockModels; empty: the game's stones)
	TArray<FOTGTreeEdit> Trees;                                // (one at a time: picked, changed, added)
	TArray<FOTGRockEdit> Rocks;
	TArray<FOTGForest> Forests;                                // (the course editor's: OTGCourseData ForEachDesignedTree)
	const FOTGForest* ForestAt(double X, double Z) const;      // (the last drawn that holds it)

	// The edit of the tree or rock standing at (X, Z), if it has one (to 15 cm)
	const FOTGTreeEdit* TreeEditAt(double X, double Z) const;
	FOTGTreeEdit& TreeEdit(double X, double Z);                // (found or made)
	const FOTGRockEdit* RockEditAt(double X, double Z) const;
	FOTGRockEdit& RockEdit(double X, double Z);
	void ClearTreeEdit(double X, double Z);
	void ClearRockEdit(double X, double Z);
	static const TArray<FString>& AllSpecies();                // (every species SpeciesLabel knows, for adding)
	static bool IsConifer(const FString& Kind);

	static const TArray<FString>& Models();                    // auto beech hornbeam spruce pine scotspine hilltree
	static FString ModelLabel(const FString& Model);
	static const TArray<FString>& RockModels();                // "" (the game's stones) mossy grey river field plains volcanic
	static FString RockModelLabel(const FString& Model);
	static FString SpeciesLabel(const FString& Kind);          // (the web version's names: "Red maple")

	static FOTGTreeDesign Load(const FString& Dir);
	bool Save(const FString& Dir) const;
	bool IsDefault() const;

	FOTGSpeciesLook For(const FString& Kind) const;
	float SizeOf(const FString& Kind) const { return AllSize * For(Kind).Size; }
	// How many of a tree at (X, Z) (course metres): 0 (taken out), 1, or more
	int32 Copies(const FString& Kind, double X, double Z, float Times = 1.f) const;
	// Where copy I (1, 2 ...) of a tree stands, R metres its crown's radius: round it, clear of its crown
	static FVector2D CopyAt(double X, double Z, int32 I, double R);
	// A rock at (X, Z): kept, and its size
	bool KeepRock(double X, double Z) const;
	static double Hash01(double X, double Z, int32 Salt);
};
