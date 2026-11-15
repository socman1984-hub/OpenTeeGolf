// A course's source, as the course editor works on it: what the web version's editor reads and
// saves (public/js/editor.js load() and save()), from the course file (<id>.course.json: holes,
// penalty areas, the boundary, the course's looks) and its real-world part (<id>.real.json: the
// mapped shapes, the player's trees, tree areas, tall grass, removed trees, building types, and the
// ground's heights). Positions in metres: X east, Y north (the course's x and z).
//
// SaveBody() is the body the editor's Save sends (PUT /api/courses/<id>, through the helper:
// UOTGHelperSubsystem::SaveSource): the course file made again as coursefile.js makeCourseData
// makes it, and the rest. What the model doesn't hold (the course's looks, other mapped features,
// the builder's notes) goes back as it came.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

struct OPENTEEGOLF_API FOTGSourceHole
{
	int32 Number = 1;
	int32 Par = 4;
	int32 Index = 0;                              // handicap index (0: none)
	TMap<FString, FVector2D> Tees;                // by tee name (TeesOrder)
	TArray<FVector2D> Aims;                       // 0-2 aim points
	FVector2D Green = FVector2D::ZeroVector;      // the green's centre
	TMap<FString, FVector2D> Pins;                // by day (PinDays)
};

struct OPENTEEGOLF_API FOTGSourceShape
{
	FString Kind;                                 // green fairway bunker tee water penalty oob trees
	TArray<FVector2D> Pts;
	bool bInner = false;                          // a cut-out (the feature's "hole")
	bool bFlow = false, bBanks = false;           // water: a stream (and its banks fitted)
	bool bInnerOOB = false;                       // penalty: out of bounds inside it
	FString Fit;                                  // bunker: how it was fitted (kept)
	TSharedPtr<FJsonValue> Osm;                   // bunker: the map's own outline (kept)
	bool bEdited = false;                         // bunker: moved in the editor (saved as the player's)
	// tree areas
	FString Density = TEXT("medium");             // sparse medium dense
	FString Species = TEXT("auto");
	double HMin = NAN, HMax = NAN;                // m (none: the measured heights)
};

struct OPENTEEGOLF_API FOTGSourceTree
{
	FVector2D P = FVector2D::ZeroVector;
	FString Size = TEXT("medium");                // small medium large
	FString Kind = TEXT("auto");                  // species (TREE_SPECIES, TREE_MIXES)
};

struct OPENTEEGOLF_API FOTGGrassStroke
{
	TArray<FVector2D> Pts;
	double R = 4;                                 // brush radius, m
	bool bErase = false;
};

class OPENTEEGOLF_API FOTGCourseSource
{
public:
	static const TArray<FString>& TeesOrder();    // Black Blue White Green Yellow Red Junior Par3
	static const TArray<FString>& PinDays();      // Thursday .. Sunday

	bool Load(const FString& Id, const TSharedPtr<FJsonObject>& Course, const TSharedPtr<FJsonObject>& Real, FString* Why = nullptr);
	TSharedRef<FJsonObject> SaveBody() const;
	// The ground's height (m) where the course has heights, else 0 (bilinear, as the editor's heightAt)
	double HeightAt(double X, double Z) const;
	bool HasHeights() const { return Heights.Num() > 0; }
	FString Summary() const;

	FString Id;
	FString Name, Location;
	double Stimp = 10;
	TArray<FOTGSourceHole> Holes;
	TArray<FOTGSourceShape> Shapes;
	TArray<FOTGSourceTree> UserTrees;
	TArray<FVector2D> RemovedTrees;
	TArray<FOTGGrassStroke> TallGrass;
	TMap<FString, FString> BuildingTypes;         // "x,z" -> type

	// As loaded (what the model doesn't hold goes back from these)
	TSharedPtr<FJsonObject> CourseJson, RealJson;

private:
	TMap<FString, TSharedPtr<FJsonValue>> Passthrough;   // mapped features other than the shapes'
	double GridMinX = 0, GridMinZ = 0, GridCell = 1;
	int32 Nx = 0, Nz = 0;
	TArray<float> Heights;
};
