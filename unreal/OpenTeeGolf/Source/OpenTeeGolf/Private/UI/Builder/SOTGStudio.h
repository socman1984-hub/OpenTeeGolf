// The studio (the menu's New course > Biomes and forests): grass biomes and forest presets made and
// kept for every course (OTGBiomes.h: the library), each shown as it's changed on a plot of its own
// beside the panel (AOTGPreviewStage: the course's own grass and trees, in the menu's level - drag
// to look round it, the wheel to come closer).
//
//  - Grass biomes: which of the tall grass's kinds grow (the game's grasses, Megascans and KiteDemo
//    plants), how many, how tall, in what patches, their colour.
//  - Forests: the species and their shares, each drawn with a model of its own; how tall, how close,
//    and the grass biome under them.
//
// The course editor then gives a course's tall grass a biome, areas of it their own, and forests a
// preset (SOTGCourseEditor).

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "World/OTGBiomes.h"

class SBox;
class AOTGPreviewStage;

class SOTGStudio : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGStudio) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, bool bForests);
	virtual ~SOTGStudio();
	TFunction<void()> OnClose;   // (back to New course)

	// (testing: -studio=biomes|forests[:<name>], OTG.Studio.*)
	void ShowForests(bool bOn);
	void Pick(const FString& Name);
	void SetKind(const FString& Kind, const FString& Field, float V);
	void AddPlant(const FString& Plant);

	virtual void Tick(const FGeometry& Geometry, const double Time, const float Dt) override;
	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseWheel(const FGeometry& G, const FPointerEvent& E) override;

private:
	bool bForests = false;
	TArray<FOTGBiome> Biomes;
	TArray<FOTGForestPreset> Forests;
	FOTGBiome Biome;                 // (the one being made: a copy until saved)
	FOTGForestPreset Forest;
	FString Was;                     // (its name as saved: a rename saves as the new and drops the old)
	bool bUnsaved = false, bConfirmDelete = false;
	FString Status;
	TWeakObjectPtr<AOTGPreviewStage> Stage;
	bool bDirty = true;
	double AppliedAt = 0;
	bool bDragging = false;
	TArray<FString> PlantList;       // (the Megascans and KiteDemo plants the game has)
	TArray<TSharedPtr<FString>> PlantOpts, SpeciesOpts;
	TSharedPtr<FString> PlantPick, SpeciesPick;

	TSharedPtr<SBox> Panel;
	void Load(const FString& Name);   // (from the library; empty: the first)
	void New();
	void Changed() { bUnsaved = true; bDirty = true; Status = TEXT("Not saved yet"); }
	void Save();
	void Delete();
	void Apply();                    // (the plot grown again: the biome, or the forest on its biome)
	void Rebuild();
	TSharedRef<class SWidget> BiomePanel();
	TSharedRef<class SWidget> ForestPanel();
	TSharedRef<class SWidget> KindRow(int32 I);
	TArray<otg::FGrassKind> UnderKinds() const;   // (the forest's biome, or the game's tall grass)
};
