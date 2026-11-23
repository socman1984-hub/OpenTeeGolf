// A mowing pattern's look, drawn small (the course editor's course panel).

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class SOTGMowSwatch : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGMowSwatch) : _WidthM(3.5) {}
		SLATE_ARGUMENT(FString, Pattern)
		SLATE_ARGUMENT(double, WidthM)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(64, 64); }

private:
	FString Id;
	double Width = 3.5;
};
