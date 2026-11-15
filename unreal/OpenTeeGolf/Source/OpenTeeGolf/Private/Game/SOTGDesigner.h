// The course's designers, opened in a round over the course itself, every change seen at once:
//
//  - SOTGGrassDesigner (R): what the rough and the tall grass (deep rough, heather) are made of -
//    which kinds grow (the game's own and any Megascans plant it has), where (only the kinds in the
//    rough's own colours on the rough: it stays one even colour; the rest in the tall grass), how
//    many, how tall and how varied, how far out, and in what patches. Saved for the course
//    (grass.json in its folder: AOTGRuntimeCourse::SaveGrassKinds), the game's own a click away.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "OTGGrass.h"

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
