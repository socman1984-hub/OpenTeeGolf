// See SOTGPanels.h (public/js/main.js updateHud's wind, and the test panel).

#include "SOTGPanels.h"

#include "OTGUi.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"

using namespace otg::ui;

// ------------------------------------------------------------------ the wind

int32 SOTGWindArrow::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
	const FVector2D S = G.GetLocalSize(), C = S / 2;
	const double R = FMath::Min(S.X, S.Y) / 2 - 3;
	// a ring, and the arrow across it
	TArray<FVector2f> Circle;
	for (int32 i = 0; i <= 32; i++) { const double A = i / 32.0 * UE_DOUBLE_TWO_PI; Circle.Add(FVector2f(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R)); }
	FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Circle, ESlateDrawEffect::None, FLinearColor(1, 1, 1, 0.35f), true, 1.5f);
	if (bNone) return Layer;
	const double A = FMath::DegreesToRadians(Angle);
	const FVector2D U(FMath::Sin(A), -FMath::Cos(A)), N(-U.Y, U.X);
	const FVector2D Tip = C + U * (R - 2), Tail = C - U * (R - 2);
	const FLinearColor Col(0.6f, 0.9f, 1.f);
	FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(Tail), FVector2f(Tip) }, ESlateDrawEffect::None, Col, true, 3.f);
	FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(Tip - U * 9 + N * 7), FVector2f(Tip), FVector2f(Tip - U * 9 - N * 7) }, ESlateDrawEffect::None, Col, true, 3.f);
	return Layer + 1;
}

void SOTGWind::Construct(const FArguments&)
{
	ChildSlot
	[
		Box(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[SAssignNew(Arrow, SOTGWindArrow)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SAssignNew(Text1, STextBlock).Font(Font(15, "Bold")).ColorAndOpacity(Ink)]
				+ SVerticalBox::Slot().AutoHeight()[SAssignNew(Text2, STextBlock).Font(Font(12)).ColorAndOpacity(Soft)]
			], FMargin(10, 6))
	];
}

void SOTGWind::Set(double ArrowDeg, bool bCalm, const FString& Line1, const FString& Line2)
{
	Arrow->SetAngle(ArrowDeg, bCalm);
	Text1->SetText(FText::FromString(Line1));
	Text2->SetText(FText::FromString(Line2));
}

// ------------------------------------------------------------------ the shot panel

void SOTGShotPanel::Preset(double InMph, double InVla, double InHla, double InSpin, double InAxis)
{
	Mph = InMph; Vla = InVla; Hla = InHla; Spin = InSpin; Axis = InAxis;
}

void SOTGShotPanel::Construct(const FArguments&)
{
	auto Field = [](const FString& Label, double Min, double Max, double Delta, double* V) {
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Text(Label, 12, Soft)]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).WidthOverride(120)[SNew(SSpinBox<double>).MinValue(Min).MaxValue(Max).Delta(Delta)
					.Value_Lambda([V]() { return *V; }).OnValueChanged_Lambda([V](double X) { *V = X; })]
			];
	};
	ChildSlot
	[
		Box(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[Text(TEXT("Test shot"), 18, Ink, "Bold")]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(8, 0, 0, 2)[Text(TEXT("(no launch monitor needed)"), 12, Soft)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)[Button(TEXT("Driver"), [this]() { Preset(150, 12, 0, 2700, 0); }, 13)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)[Button(TEXT("7 iron"), [this]() { Preset(118, 17, 0, 6800, 0); }, 13)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)[Button(TEXT("Wedge"), [this]() { Preset(95, 25, 0, 9000, 0); }, 13)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)[Button(TEXT("Chip"), [this]() { Preset(40, 22, 0, 4000, 0); }, 13)]
				+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Putt"), [this]() { Preset(PuttMph ? FMath::RoundToDouble(PuttMph() * 100) / 100 : 5, 1, 0, 0, 0); }, 13)]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SGridPanel)
				+ SGridPanel::Slot(0, 0).Padding(0, 0, 10, 6)[Field(TEXT("Ball speed (mph)"), 0, 240, 0.5, &Mph)]
				+ SGridPanel::Slot(1, 0).Padding(0, 0, 0, 6)[Field(TEXT("Launch (deg)"), -20, 85, 0.1, &Vla)]
				+ SGridPanel::Slot(0, 1).Padding(0, 0, 10, 6)[Field(TEXT("Direction (deg, +R)"), -60, 60, 0.1, &Hla)]
				+ SGridPanel::Slot(1, 1).Padding(0, 0, 0, 6)[Field(TEXT("Total spin (rpm)"), 0, 15000, 10, &Spin)]
				+ SGridPanel::Slot(0, 2).Padding(0, 0, 10, 6)[Field(TEXT("Spin axis (deg, +R)"), -90, 90, 0.1, &Axis)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 4)[Button(TEXT("Hit"), [this]() { if (OnHit) OnHit(Mph, Vla, Hla, Spin, Axis); }, 16, true)]
			+ SVerticalBox::Slot().AutoHeight()[Text(TEXT("Putt: a speed for the distance to the hole.  T: close"), 11, Soft)],
			FMargin(14, 10), Solid)
	];
}
