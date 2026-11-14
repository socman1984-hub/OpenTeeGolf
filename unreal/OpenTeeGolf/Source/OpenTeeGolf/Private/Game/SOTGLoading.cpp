// See SOTGLoading.h.

#include "SOTGLoading.h"

#include "OTGUi.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"

using namespace otg::ui;

void SOTGLoading::Construct(const FArguments& Args)
{
	ChildSlot
	[
		SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.03f, 0.05f, 0.035f, 1.f))
		.HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(560)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Text(TEXT("Building the course"), 14, Gold)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 16)[Text(Args._Title, 28, Ink, "Bold")]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).HeightOverride(12)[SNew(SProgressBar).Percent_Lambda([this]() { return TOptional<float>(Frac); }).FillColorAndOpacity(FLinearColor(0.3f, 0.75f, 0.4f, 1.f))]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
				[
					SNew(STextBlock).Font(Font(14)).ColorAndOpacity(Soft)
					.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%s  \u00B7  %d%%"), *Step, FMath::RoundToInt(Frac * 100))); })
				]
			]
		]
	];
}

void SOTGLoading::Set(float Fraction, const FString& What)
{
	Frac = FMath::Clamp(Fraction, 0.f, 1.f);
	Step = What;
}
