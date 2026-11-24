// The wind box (the web version's main.js updateHud wind): an arrow showing which way the wind
// blows against the aim (up: helping, down: into you; SetAngle in degrees clockwise from up), and
// two lines beside it - its speed and gusts, how it plays. The round sets it each time either changes.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class STextBlock;

class OTGWINDINDICATOR_API SOTGWindArrow : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGWindArrow) {}
	SLATE_END_ARGS()
	void Construct(const FArguments&) {}
	void SetAngle(double Deg, bool bCalm) { Angle = Deg; bNone = bCalm; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(44, 44); }

private:
	double Angle = 0;   // degrees clockwise from up (up: blowing toward the target)
	bool bNone = true;
};

class OTGWINDINDICATOR_API SOTGWind : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGWind) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	void Set(double ArrowDeg, bool bCalm, const FString& Line1, const FString& Line2);

private:
	TSharedPtr<SOTGWindArrow> Arrow;
	TSharedPtr<STextBlock> Text1, Text2;
};
