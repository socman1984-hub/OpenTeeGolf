// The tree designer: the course's trees, species by species - drawn with which of the game's tree
// models (or its own choice by height), how big, how many (fewer: some taken out; more: others grown
// round them in the rough) - all trees together, and the rocks. "Show me" flies the free view to
// one of a species. And one at a time, by clicking on the course (WorldClick): a tree picked to
// change its species, model, height and leaf colour, or a rock its size, or either taken out; trees
// (of any species, any height) and rocks added where you click. The ball's trees go the same way
// (OnChanged: AOTGRound::RefreshTrees). Saved for the course (trees_design.json:
// AOTGRuntimeCourse::SaveTreeDesign).

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "World/OTGGrass.h"
#include "World/OTGTreeDesign.h"
#include "World/OTGRuntimeCourse.h"

class AOTGRuntimeCourse;
class SBox;

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
