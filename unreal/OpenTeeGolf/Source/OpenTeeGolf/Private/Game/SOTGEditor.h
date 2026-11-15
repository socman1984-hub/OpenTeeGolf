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

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Game/OTGCourseSource.h"

class SBox;
class UTexture2D;
class UOTGHelperSubsystem;
class SOTGCourseEditor;

class SOTGEditCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGEditCanvas) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& Args, SOTGCourseEditor* InEditor);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(800, 600); }
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseMove(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseWheel(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& G, const FPointerEvent& E) override;
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& G, const FPointerEvent& E) const override;

	// The view: its centre (course metres) and pixels a metre
	double Cx = 0, Cz = 0, Scale = 0.5;
	void Fit(const TArray<FVector2D>& Pts, double PadM);
	FVector2D ToScreen(const FVector2D& W) const;
	FVector2D ToWorld(const FVector2D& L) const;

private:
	SOTGCourseEditor* Ed = nullptr;
	mutable FVector2D Size = FVector2D(800, 600);
	FVector2D Hover = FVector2D(-1, -1);
	enum class EDrag : uint8 { None, Pan, Handle, Vertex, Move, Tree, Box, Cut, Brush };
	EDrag Drag = EDrag::None;
	FVector2D DragFrom, DragLast, PanFrom;
	bool bMoved = false;
	int32 DragIndex = INDEX_NONE;          // (the vertex, the tree)
	FString DragHandle;                    // (tee:Black, aim:0, green, pin:Friday)
	TArray<FVector2D> MoveOrig;            // (a shape being moved: its points before)
	FVector2D MoveFromW;
	void PolyFill(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const TArray<FVector2D>& Pts, const FLinearColor& C) const;
	void PolyLine(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const TArray<FVector2D>& Pts, const FLinearColor& C, float W, bool bClosed) const;
	void Dot(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& P, float R, const FLinearColor& C, bool bOutline = true) const;
	void Label(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& P, const FString& S, const FLinearColor& C) const;
};

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
	enum class EMode : uint8 { None, Draw, Place, AddTree, Cut, Paint, Erase };
	EMode Mode = EMode::None;
	FString ModeKind, ModeKey;             // (Draw: the shape's kind; Place: tee/aim/green/pin and which)
	TArray<FVector2D> DrawPts;
	// the tools' settings
	FString TreeSize = TEXT("medium"), TreeKind = TEXT("auto"), AreaDensity = TEXT("medium"), AreaKind = TEXT("auto");
	double AreaHMin = 10, AreaHMax = 18, CutR = 6, BrushR = 4;
	bool bShowPhoto = true, bShowTrees = true, bShowGrass = true, bShowBuildings = true, bColourSpecies = false;
	// the trees the game found (the course's play data, when it's in the game), the buildings
	struct FFound { FVector2D P; float R = 3, H = 10; FString Kind; };
	TArray<FFound> Found;
	struct FBuilding { TArray<FVector2D> Pts; FString Name, Kind, Key; double Area = 0; };
	TArray<FBuilding> Buildings;
	// the map's picture: the aerial photo over [X0, X0 + SizeM] x [Z1 - SizeM, Z1], or the ground's shading
	TStrongObjectPtr<UTexture2D> Photo, Shade;
	TSharedPtr<FSlateBrush> PhotoBrush, ShadeBrush;
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
	void Save(bool bExport);
	TSharedRef<SWidget> TopBar();
	TSharedRef<SWidget> CoursePanel();
	TSharedRef<SWidget> HolePanel();
	TSharedRef<SWidget> ShapePanel();
	TSharedRef<SWidget> SelectionPanel();
	TSharedRef<SWidget> TreePanel();
	TSharedRef<SWidget> PlantPanel();
	TSharedRef<SWidget> GrassPanel();
	TSharedRef<SWidget> BuildingPanel();
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
	// (-editortest=<id>: the editor open, a screenshot; -editortestfix: a hole's par of 10 or more set to 4, saved, quit)
	int32 TestStep = 0;
	double TestAt = 0;
};
