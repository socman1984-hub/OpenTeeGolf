// The grass designer (a tab of SOTGCourseDesigner, R): what the rough and the tall grass (deep rough, heather) are made of -
//    which kinds grow (the game's own and any Megascans plant it has), where (only the kinds in the
//    rough's own colours on the rough: it stays one even colour; the rest in the tall grass), how
//    many, how tall and how varied, how far out, in what patches, and its colour (a hue, strength
//    and brightness within believable limits, tinted as you drag). "Show me" flies the free view to
//    where it grows thickly nearby. Saved for the course (grass.json in its folder:
//    AOTGRuntimeCourse::SaveGrassKinds), the game's own a click away.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "World/OTGGrass.h"
#include "World/OTGTreeDesign.h"
#include "World/OTGRuntimeCourse.h"
#include "World/OTGBiomes.h"

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
	TSharedRef<SWidget> BiomePanel();   // (the course's grass biomes: the tall grass's, and each area's)
	void SetBiomes(const struct FOTGCourseBiomes& B, const FString& Say);
	TArray<FOTGBiome> LibBiomes;   // (the Course builder's grass biomes)

	TWeakObjectPtr<AOTGRuntimeCourse> Course;
	TArray<otg::FGrassKind> Edit;
	TArray<TSharedPtr<FString>> Plants;   // (Megascans plants not yet added, for the list)
	TSharedPtr<FString> PlantPick;
	TSharedPtr<SBox> Body;
	TSharedPtr<class SScrollBox> Scroll;   // (kept where it was across a Rebuild)
	TSet<FString> Opened;   // (the kinds, and the fairways' and greens' rows, opened to their sliders: folded otherwise)
	TSharedRef<SWidget> FoldHead(const FString& Key, const FString& Label, bool bBright);   // (+/- and its name: a click opens or folds it)
	FString Status;
	bool bUnsaved = false;
};
