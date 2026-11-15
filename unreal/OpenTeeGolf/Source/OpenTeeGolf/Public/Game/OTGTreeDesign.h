// How a course's trees and rocks look, as its tree designer left them (SOTGTreeDesigner; saved in
// the course's folder as trees_design.json): for each species on the course, the Unreal model it's
// drawn with (or the game's own choice), its size and how many (fewer: some taken out; more: others
// grown round them, off the mown grass, sand, paths and water), and for the rocks, whether they're
// there, their size and how many.
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

struct OPENTEEGOLF_API FOTGTreeDesign
{
	TMap<FString, FOTGSpeciesLook> Species;
	float AllSize = 1.f, AllDensity = 1.f;                     // (every species, on top of its own)
	bool bRocks = true;
	float RockSize = 1.f, RockDensity = 1.f;

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
