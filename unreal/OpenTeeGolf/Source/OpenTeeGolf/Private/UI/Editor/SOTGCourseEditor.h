// The course editor in the game (the web version's public/js/editor.js, everything it does): a
// top-down map of a built course - its aerial photo (or the ground's shading) - with the holes
// (tees, aim points, the green's centre, the pins), the shapes (greens, fairways, bunkers, tees,
// water, penalty areas, the boundary, tree areas), the trees (found and your own), the tall grass
// you paint and the buildings; and beside it the course's looks, the hole, the selection and the
// tools. Save writes the course's source through the course helper (FOTGCourseSource::SaveBody,
// as the web editor's Save does); "Save and put in the game" exports it again.
//
//  - SOTGEditCanvas: the map, and the mouse and keys on it
//  - SOTGCourseEditor: the page - the canvas, the panels, undo, saving
//
// Its files: SOTGCourseEditor.cpp (opening and saving), SOTGCourseEditorTools.cpp (changes, undo, the
// tools), SOTGCourseEditorPanels.cpp (the panels), SOTGEditCanvas.cpp (the map).

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Course/OTGCourseSource.h"
#include "World/OTGTreeDesign.h"
#include "World/OTGBiomes.h"
#include "UI/Editor/SOTGEditCanvas.h"
#include "UI/Editor/SOTGMowSwatch.h"

class SBox;
class UTexture2D;
class UOTGHelperSubsystem;

class SOTGCourseEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGCourseEditor) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& Args, const FString& CourseId);
	// (each frame, before the layout - not in Tick, which runs as it's drawn: panels made there
	// are drawn once unmeasured, piled on each other)
	EActiveTimerReturnType Pulse(double Time, float Dt);
	TFunction<void(bool bExported)> OnClose;

	// ---- the course and what's picked (the canvas reads and changes them)
	FString Id;
	FOTGCourseSource Src;
	bool bLoaded = false, bDirty = false;
	int32 HoleI = 0;
	int32 SelShape = INDEX_NONE, SelTree = INDEX_NONE, SelFound = INDEX_NONE, SelRemoved = INDEX_NONE, SelBuilding = INDEX_NONE;
	TArray<int32> SelMany;                 // (found trees, picked together)
	enum class EMode : uint8 { None, Draw, Place, AddTree, Cut, Paint, Erase, Forest, Patch, Join };
	EMode Mode = EMode::None;
	FString ModeKind, ModeKey;             // (Draw: the shape's kind; Place: tee/aim/green/pin and which)
	TArray<FVector2D> DrawPts;
	// the tools' settings
	FString TreeSize = TEXT("medium"), TreeKind = TEXT("auto"), AreaDensity = TEXT("medium"), AreaKind = TEXT("auto");
	double AreaHMin = 10, AreaHMax = 18, CutR = 6, BrushR = 4;
	bool bShowPhoto = true, bShowTrees = true, bShowGrass = true, bShowBuildings = true, bColourSpecies = false;
	// the trees the game found (the course's play data, when it's in the game), the buildings
	// (Design: what the game's tree designer - R in a round - did to it: 0 nothing, 1 took it out,
	// 2 took it out with its species' "how many", 3 added it)
	struct FFound { FVector2D P; float R = 3, H = 10; FString Kind; uint8 Design = 0; };
	TArray<FFound> Found;
	FOTGTreeDesign Design;           // (the course's trees_design.json, in the game's folder)
	FString GameDir;
	void DesignUndo(int32 FoundI);   // (the designer's change to this tree taken back, and saved)
	// The course's forests (Design.Forests) and grass biomes (biomes.json), both in the game's folder
	// and saved there as they change - the game reads them as it builds the course. The library's
	// biomes and forests (the studio's: OTGBiomes.h) to give them.
	FOTGCourseBiomes Biomes;
	TArray<FOTGBiome> LibBiomes;
	TArray<FOTGForestPreset> LibForests;
	int32 SelForest = INDEX_NONE, SelArea = INDEX_NONE;
	FString AreaBiome;               // (the biome a new area's drawn with)
	void MarkForest(const FVector2D& P);   // (the group of found trees round P, as one forest)
	void ForestOf(const TArray<int32>& In); // (these found trees, as one forest)
	void JoinForest(const FVector2D& P);   // (the forest at P joined to the picked one)
	void MarkPatch(const FVector2D& P);    // (the patch of tall grass round P - found or painted - as an area of AreaBiome)
	void SaveForests(const FString& Say);
	void SaveBiomes(const FString& Say);
	int32 CurrentTab() const { return Tab; }
	struct FBuilding { TArray<FVector2D> Pts; FString Name, Kind, Key; double Area = 0; };
	TArray<FBuilding> Buildings;
	// the map's picture: the aerial photo over [X0, X0 + SizeM] x [Z1 - SizeM, Z1], or the ground's shading
	TStrongObjectPtr<UTexture2D> Photo, Shade, Heather;
	TSharedPtr<FSlateBrush> PhotoBrush, ShadeBrush, HeatherBrush;
	double HeatherX0 = 0, HeatherZ1 = 0, HeatherW = 0, HeatherH = 0;   // (the tall grass the game found in the aerial photo)
	TArray<uint8> HeatherPx;                                          // (and its cells, 1 where it's tall grass: row 0 at HeatherZ1)
	int32 HeatherPxW = 0, HeatherPxH = 0;
	double HeatherRes = 0;
	double PhotoX0 = 0, PhotoZ1 = 0, PhotoSizeM = 0, ShadeX0 = 0, ShadeZ1 = 0, ShadeW = 0, ShadeH = 0;
	// reviewing the trees in the line of play: the found ones, one at a time
	TArray<int32> Review;
	int32 ReviewAt = INDEX_NONE;

	// ---- changes (the canvas calls these)
	void Change();                         // (before each: one undo step)
	void Undo();
	void Changed();                        // (after: the panels again)
	void SetMode(EMode M, const FString& Kind = FString(), const FString& Key = FString());
	void FinishDraw();
	void PlacePoint(const FVector2D& P);
	void ClearPick();
	void DeleteSelected();
	int32 CutTrees(const FVector2D& P);
	void RemoveFound(bool bReplace);
	void RemoveMany();
	void RestoreRemoved();
	void FitHole();
	void FitAll();
	void SetStatus(const FString& S) { Status = S; }
	TArray<FVector2D> HoleLine(const FOTGSourceHole& H) const;
	bool Shapes_Valid(int32 I) const { return Src.Shapes.IsValidIndex(I); }

private:
	UOTGHelperSubsystem* Helper() const;
	void Loaded();
	void LoadExtras();
	void MakeShade();
	void Rebuild();
	// 0 save; 1 save and update the game at once (OTGQuickUpdate); 2 save and the full export
	void Save(int32 Then);   // (3: save, then close)
	// What's changed since the course was opened or last saved (Saved), a line each: shown before a
	// save to confirm it, and on closing with changes not saved
	FOTGCourseSource Saved;
	TArray<FString> ChangeList() const;
	void AskSave(int32 Then);
	void AskClose();
	void ShowDialog(const FString& Title, const FString& Intro, const TArray<FString>& Lines, TArray<TPair<FString, TFunction<void()>>> Buttons);
	void HideDialog();
	TSharedPtr<SBox> DialogHost;
	void Quick();
	FOTGCourseSource Applied;   // (the course as the game's folder has it: what the quick update compares with)
	bool bQuicking = false;
	TSharedRef<SWidget> TopBar();
	TSharedRef<SWidget> CoursePanel();
	TSharedRef<SWidget> HolePanel();
	TSharedRef<SWidget> ShapePanel();
	TSharedRef<SWidget> SelectionPanel();
	TSharedRef<SWidget> TreePanel();
	TSharedRef<SWidget> PlantPanel();
	TSharedRef<SWidget> GrassPanel();
	TSharedRef<SWidget> BuildingPanel();
	TSharedRef<SWidget> ForestPanel();
	TSharedRef<SWidget> BiomePanel();
	void AutoPins(FOTGSourceHole& H);
	void StartReview();
	void ShowReview();

	struct FSnap
	{
		TArray<FOTGSourceHole> Holes; TArray<FOTGSourceShape> Shapes; TArray<FOTGSourceTree> UserTrees; TArray<FVector2D> RemovedTrees;
		TArray<FOTGGrassStroke> TallGrass; TMap<FString, FString> BuildingTypes;
		FString Name, Location; double Stimp; bool bPF, bPG, bPR; FString MowF, MowG; float RS, NS; TSet<FString> TreesOff, PlantsOff, PlantsExtra; float PlantAmount;
	};
	FSnap Snap() const;
	void Restore(const FSnap& S);
	TArray<FSnap> UndoStack;

	TSharedPtr<SOTGEditCanvas> Canvas;
	TSharedPtr<SBox> Side, Top;
	FString Status;
	bool bExporting = false, bExported = false, bWantExport = false;
	FString JobShown;
	int32 PanelKey = 0;   // (bumped when the panels need making again)
	int32 PanelBuilt = -1;
	// the side panel: one section at a time, on tabs (Hole Shapes Trees Tall grass Course Plants Buildings)
	int32 Tab = 0;
	bool bSpeciesOpen = false;   // (the trees' species list, folded away until opened)
public:
	void ShowTab(int32 T) { Tab = T; Changed(); }
private:
	// (-editortest=<id>: the editor open, a screenshot; -editortestfix: a hole's par of 10 or more set to 4, saved, quit)
	int32 TestStep = 0;
	double TestAt = 0;
	bool bForestTested = false, bExtraTested = false, bDialogTested = false;   // (-editortestforest=X,Z[,preset] [-editortestbiome=<biome>]: a forest marked there, an area of that biome round it)
};
