// The minimap (main.js drawMinimap): the course's aerial photo turned so the hole runs up the map
// (the tee at the bottom, the green at the top), with the ball, the aim line and point, the pin,
// the other players' balls, and the yardages - to the aim point and what it leaves to the pin, or
// under the cursor. Click or drag on it to aim. It frames the ball and the pin - the whole hole from the
// tee, closer in as the ball nears the green - and its + and - (or the mouse wheel) zoom in or out on that.

#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

class UTexture2D;

struct OTGMINIMAP_API FOTGMapState
{
	bool bValid = false;
	FVector2D Ball, Aim, Pin, Tee;      // course x, z (metres)
	bool bOnGreen = false;
	FLinearColor Ring = FLinearColor::Transparent;   // (players together: the ball being played)
	TArray<TPair<FVector2D, FLinearColor>> Others;
};

class OTGMINIMAP_API SOTGMinimap : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGMinimap) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	// The photo over the landscape: its north-west corner (course x0, z1) and size (metres)
	void SetCourse(UTexture2D* Photo, double X0, double Z1, double SizeM, TFunction<FString(double, double)> InSurfaceName);
	void SetState(const FOTGMapState& S) { State = S; }
	TFunction<void(double X, double Z)> OnAim;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(250, 360); }
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override;
	virtual void OnMouseLeave(const FPointerEvent& Event) override;

private:
	// course (x, z) <-> the widget's local space, for its size now
	struct FFrame { FVector2D C, Mid; double K = 1, Cos = 1, Sin = 0; };
	FFrame Frame(const FVector2D& Size) const;          // (as shown: easing toward Target)
	FFrame Target(const FVector2D& Size) const;         // (where it is going: the ball and pin, and the zoom)
	FFrame Turned(const FVector2D& Size) const;         // (the hole turned up the map, centred in the widget)
	FSlateRect ZoomButton(bool bIn) const;
	void ZoomBy(int32 Steps);
	FVector2D ToLocal(const FFrame& F, const FVector2D& P) const;
	FVector2D ToCourse(const FFrame& F, const FVector2D& L) const;
	void AimFrom(const FGeometry& Geometry, const FPointerEvent& Event);

	TWeakObjectPtr<UTexture2D> Photo;
	FSlateBrush PhotoBrush;
	FSlateRoundedBoxBrush Dot = FSlateRoundedBoxBrush(FLinearColor::White, 8.f);
	FSlateRoundedBoxBrush Ring = FSlateRoundedBoxBrush(FLinearColor::Transparent, 8.f, FLinearColor::White, 2.f);
	double X0 = 0, Z1 = 0, SizeM = 1;
	TFunction<FString(double, double)> SurfaceName;
	FOTGMapState State;
	TOptional<FVector2D> Hover;   // (course x, z)
	bool bDragging = false;
	// the zoom the player asked for (steps of x1.5 on the framing; reset on a new hole), and the frame shown
	int32 ZoomSteps = 0;
	FVector2D ZoomHole = FVector2D(1e30, 1e30);
	bool bShown = false;
	FVector2D ShownMid = FVector2D::ZeroVector;
	double ShownK = 1;
	TOptional<FFrame> Held;   // (dragging to aim: the map stays put under the cursor)
};
