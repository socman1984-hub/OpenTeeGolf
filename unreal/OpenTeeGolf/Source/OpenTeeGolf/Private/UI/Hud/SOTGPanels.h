// Two of the round's panels:
//
//  - SOTGWind (the web version's wind box): an arrow showing which way the wind blows against the
//    aim (up: helping, down: into you), its speed and gusts, how it plays, and the weather
//  - SOTGShotPanel (T; the web version's test-shot panel): a shot without a launch monitor - presets
//    (driver, 7 iron, wedge, chip, a putt sized to the hole) and the numbers to change - hit as if
//    the launch monitor had sent it

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class STextBlock;

class SOTGWindArrow : public SLeafWidget
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

class SOTGWind : public SCompoundWidget
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

// What the weather panel shows
struct FOTGWeatherView
{
	FString Key, Label;
	double WindMph = 0, From = 0, Cloud = 0, RainMm = 0, Fog = 0, TempC = 20;
	double Hour = -1;   // (the time of day; below 0: the sun behind you on every hole)
};

// The weather panel (L): the conditions changed as you play - a preset (or the course's live
// weather), then the wind's speed and direction, the cloud, rain, fog and temperature, each seen at once
class SOTGWeatherPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGWeatherPanel) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	TFunction<void(const FString&)> OnKey;
	TFunction<void(double)> OnWind, OnFrom, OnCloud, OnRain, OnFog, OnTemp, OnHour;
	TFunction<void(FOTGWeatherView&)> Current;

private:
	FOTGWeatherView View() const { FOTGWeatherView V; if (Current) Current(V); return V; }
	void Rebuild();
	TSharedPtr<class SBox> Body;
};

class SOTGShotPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGShotPanel) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	TFunction<double()> PuttMph;      // a putt's ball speed for the distance to the hole
	TFunction<void(double Mph, double Vla, double Hla, double Spin, double Axis)> OnHit;

private:
	void Preset(double Mph, double Vla, double Hla, double Spin, double Axis);
	double Mph = 150, Vla = 12, Hla = 0, Spin = 2700, Axis = 0;
};
