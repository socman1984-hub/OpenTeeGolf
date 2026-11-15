// The course's designers, opened in a round over the course itself, every change seen at once:
//
//  - SOTGGrassDesigner (R): what the rough and the tall grass (deep rough, heather) are made of -
//    which kinds grow (the game's own and any Megascans plant it has), where (only the kinds in the
//    rough's own colours on the rough: it stays one even colour; the rest in the tall grass), how
//    many, how tall and how varied, how far out, and in what patches. Saved for the course
//    (grass.json in its folder: AOTGRuntimeCourse::SaveGrassKinds), the game's own a click away.
//  - SOTGTreeDesigner: the trees and rocks (below), on the next tab (SOTGCourseDesigner)

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "OTGGrass.h"
#include "Game/OTGTreeDesign.h"

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

private:
	void Rebuild();
	void Apply();                    // (grown again with Edit)
	TSharedRef<SWidget> KindRow(int32 I);
	const otg::FGrassKind* Default(const FString& Name) const;

	TWeakObjectPtr<AOTGRuntimeCourse> Course;
	TArray<otg::FGrassKind> Edit;
	TArray<TSharedPtr<FString>> Plants;   // (Megascans plants not yet added, for the list)
	TSharedPtr<FString> PlantPick;
	TSharedPtr<SBox> Body;
	FString Status;
	bool bUnsaved = false;
};

// The tree designer: the course's trees, species by species - drawn with which of the game's tree
// models (or its own choice by height), how big, how many (fewer: some taken out; more: others grown
// round them in the rough) - all trees together, and the rocks. "Show me" flies the free view to
// one of a species. The ball's trees go the same way (OnChanged: AOTGRound::RefreshTrees). Saved
// for the course (trees_design.json: AOTGRuntimeCourse::SaveTreeDesign).
class SOTGTreeDesigner : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGTreeDesigner) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	void Open(AOTGRuntimeCourse* Course);
	TFunction<void()> OnChanged;                   // (the trees changed: the ball's too)
	TFunction<void(const FString& Kind)> OnShow;   // (fly to one of these)

private:
	void Rebuild();
	void Apply();
	TWeakObjectPtr<AOTGRuntimeCourse> Course;
	FOTGTreeDesign Edit;
	TArray<TPair<FString, int32>> Species;
	TSharedPtr<SBox> Body;
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
	void ShowTab(int32 Tab);   // 0 the grass, 1 the trees and rocks

private:
	TSharedPtr<SOTGGrassDesigner> GrassPanel;
	TSharedPtr<SOTGTreeDesigner> TreePanel;
	TSharedPtr<class SWidgetSwitcher> Switch;
	TSharedPtr<SBox> TabRow;
	int32 Current = 0;
};
