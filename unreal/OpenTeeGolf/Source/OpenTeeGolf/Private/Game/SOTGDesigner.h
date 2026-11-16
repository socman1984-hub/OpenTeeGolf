// The course's designers, opened in a round over the course itself, every change seen at once:
//
//  - SOTGGrassDesigner (R): what the rough and the tall grass (deep rough, heather) are made of -
//    which kinds grow (the game's own and any Megascans plant it has), where (only the kinds in the
//    rough's own colours on the rough: it stays one even colour; the rest in the tall grass), how
//    many, how tall and how varied, how far out, in what patches, and its colour (a hue, strength
//    and brightness within believable limits, tinted as you drag). "Show me" flies the free view to
//    where it grows thickly nearby. Saved for the course (grass.json in its folder:
//    AOTGRuntimeCourse::SaveGrassKinds), the game's own a click away.
//  - SOTGTreeDesigner: the trees and rocks (below), on the next tab (SOTGCourseDesigner)

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "OTGGrass.h"
#include "Game/OTGTreeDesign.h"
#include "Game/OTGRuntimeCourse.h"

class AOTGRuntimeCourse;
class SBox;

class SOTGGrassDesigner : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGGrassDesigner) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	// Start on the course's grass as it grows now
	void Open(AOTGRuntimeCourse* Course);
	TFunction<void(const FString& Kind)> OnShow;   // (fly to where it grows)

private:
	void Rebuild();
	void Apply();                    // (grown again with Edit)
	void ApplyColours();             // (only the colours: tinted at once)
	TSharedRef<SWidget> TurfRow(const FString& Surface, const FString& Label);   // (the fairways' or greens' colour)
	void SetTurf(const TMap<FString, FOTGTurfLook>& Looks);                       // (both, as these: the rest as they are)
	TSharedRef<SWidget> KindRow(int32 I);
	const otg::FGrassKind* Default(const FString& Name) const;

	TWeakObjectPtr<AOTGRuntimeCourse> Course;
	TArray<otg::FGrassKind> Edit;
	TArray<TSharedPtr<FString>> Plants;   // (Megascans plants not yet added, for the list)
	TSharedPtr<FString> PlantPick;
	TSharedPtr<SBox> Body;
	TSharedPtr<class SScrollBox> Scroll;   // (kept where it was across a Rebuild)
	FString Status;
	bool bUnsaved = false;
};

// The tree designer: the course's trees, species by species - drawn with which of the game's tree
// models (or its own choice by height), how big, how many (fewer: some taken out; more: others grown
// round them in the rough) - all trees together, and the rocks. "Show me" flies the free view to
// one of a species. And one at a time, by clicking on the course (WorldClick): a tree picked to
// change its species, model, height and leaf colour, or a rock its size, or either taken out; trees
// (of any species, any height) and rocks added where you click. The ball's trees go the same way
// (OnChanged: AOTGRound::RefreshTrees). Saved for the course (trees_design.json:
// AOTGRuntimeCourse::SaveTreeDesign).
class SOTGTreeDesigner : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGTreeDesigner) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	void Open(AOTGRuntimeCourse* Course);
	TFunction<void()> OnChanged;                   // (the trees changed: the ball's too)
	TFunction<void(const FString& Kind)> OnShow;   // (fly to one of these)
	// A click on the course: the camera's line through it (the level's cm), and the ground where it
	// meets it (bGround false: none). True if the designer used it (otherwise it aims)
	bool WorldClick(const FVector& Origin, const FVector& Dir, bool bGround, const FVector& Ground);
	// (what a click does: 0 pick, 1 add a tree, 2 add a rock, 3 take out)
	void SetTool(int32 InTool) { Tool = FMath::Clamp(InTool, 0, 3); Rebuild(); }
	// (testing: the picked one changed as its panel would - size|height|hue|bright|kind|model|out|reset <value>)
	void EditPicked(const FString& Field, const FString& Value);

private:
	void Rebuild();
	void Apply();
	TSharedRef<SWidget> PickPanel();
	void Reselect();                 // (the picked one found again, drawn anew)
	TWeakObjectPtr<AOTGRuntimeCourse> Course;
	FOTGTreeDesign Edit;
	int32 Tool = 0;
	bool bPicked = false;
	FOTGPicked Picked;
	double PickedBaseM = 0;          // (the picked tree's height as mapped, before its own size)
	TArray<TSharedPtr<FString>> SpeciesOpts;
	TSharedPtr<FString> AddKind;
	float AddHeight = 12.f, AddRock = 1.2f;
	TArray<TPair<FString, int32>> Species;
	TSharedPtr<SBox> Body;
	TSharedPtr<class SScrollBox> Scroll;   // (kept where it was across a Rebuild)
	FString Status;
};

// Both, on tabs (R)
class SOTGCourseDesigner : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGCourseDesigner) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	void Open(AOTGRuntimeCourse* Course);
	TSharedPtr<SOTGTreeDesigner> Trees() const { return TreePanel; }
	TSharedPtr<SOTGGrassDesigner> GrassTab() const { return GrassPanel; }
	// (a click on the course: the trees tab's to use, when it's showing)
	bool WorldClick(const FVector& Origin, const FVector& Dir, bool bGround, const FVector& Ground)
	{
		return Current == 1 && TreePanel.IsValid() && TreePanel->WorldClick(Origin, Dir, bGround, Ground);
	}
	void ShowTab(int32 Tab);   // 0 the grass, 1 the trees and rocks

private:
	TSharedPtr<SOTGGrassDesigner> GrassPanel;
	TSharedPtr<SOTGTreeDesigner> TreePanel;
	TSharedPtr<class SWidgetSwitcher> Switch;
	TSharedPtr<SBox> TabRow;
	int32 Current = 0;
};
