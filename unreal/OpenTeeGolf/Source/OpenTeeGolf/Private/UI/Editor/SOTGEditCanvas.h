// The course editor's map (SOTGCourseEditor.h): the course drawn from above, and the mouse and keys on
// it. (SOTGEditCanvas.cpp)

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Course/OTGCourseSource.h"

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
