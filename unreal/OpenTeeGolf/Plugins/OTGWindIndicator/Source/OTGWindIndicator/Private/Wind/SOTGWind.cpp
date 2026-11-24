// See SOTGWind.h.

#include "Wind/SOTGWind.h"

#include "UI/OTGUi.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

using namespace otg::ui;

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
