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
	FString Model = TEXT("auto");   // auto (the game's choice by height), beech, hornbeam, spruce, pine
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
	static constexpr float SizeLo = 0.3f, SizeHi = 4.f;
};

struct OPENTEEGOLF_API FOTGTreeDesign
{
	TMap<FString, FOTGSpeciesLook> Species;
	float AllSize = 1.f, AllDensity = 1.f;                     // (every species, on top of its own)
	bool bRocks = true;
	float RockSize = 1.f, RockDensity = 1.f;
	TArray<FOTGTreeEdit> Trees;                                // (one at a time: picked, changed, added)
	TArray<FOTGRockEdit> Rocks;

	// The edit of the tree or rock standing at (X, Z), if it has one (to 15 cm)
	const FOTGTreeEdit* TreeEditAt(double X, double Z) const;
	FOTGTreeEdit& TreeEdit(double X, double Z);                // (found or made)
	const FOTGRockEdit* RockEditAt(double X, double Z) const;
	FOTGRockEdit& RockEdit(double X, double Z);
	void ClearTreeEdit(double X, double Z);
	void ClearRockEdit(double X, double Z);
	static const TArray<FString>& AllSpecies();                // (every species SpeciesLabel knows, for adding)
	static bool IsConifer(const FString& Kind);

	static const TArray<FString>& Models();                    // auto beech hornbeam spruce pine
	static FString ModelLabel(const FString& Model);
	static FString SpeciesLabel(const FString& Kind);          // (the web version's names: "Red maple")

	static FOTGTreeDesign Load(const FString& Dir);
	bool Save(const FString& Dir) const;
	bool IsDefault() const;

	FOTGSpeciesLook For(const FString& Kind) const;
	float SizeOf(const FString& Kind) const { return AllSize * For(Kind).Size; }
	// How many of a tree at (X, Z) (course metres): 0 (taken out), 1, or more
	int32 Copies(const FString& Kind, double X, double Z) const;
	// Where copy I (1, 2 ...) of a tree stands, R metres its crown's radius: round it, clear of its crown
	static FVector2D CopyAt(double X, double Z, int32 I, double R);
	// A rock at (X, Z): kept, and its size
	bool KeepRock(double X, double Z) const;
	static double Hash01(double X, double Z, int32 Salt);
};
