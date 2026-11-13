// See SOTGHud.h.

#include "SOTGHud.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

namespace
{
	FSlateFontInfo Font(int32 Size, const char* Weight = "Regular") { return FCoreStyle::GetDefaultFontStyle(Weight, Size); }
	const FLinearColor Panel(0.02f, 0.03f, 0.02f, 0.55f);
	const FLinearColor Ink(0.96f, 0.96f, 0.92f, 1.f);
	const FLinearColor Soft(0.80f, 0.84f, 0.78f, 1.f);
	const FLinearColor Gold(1.0f, 0.82f, 0.35f, 1.f);

	TSharedRef<SBorder> Box(const TSharedRef<SWidget>& Content, FMargin Pad = FMargin(14, 8))
	{
		return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(Pad)[Content];
	}
}

void SOTGHud::Construct(const FArguments&)
{
	ChildSlot
	[
		SNew(SOverlay)
		// the hole, the lie, the club
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(24)
		[
			Box(SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SAssignNew(Info1, STextBlock).Font(Font(22, "Bold")).ColorAndOpacity(Ink)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[SAssignNew(Info2, STextBlock).Font(Font(15)).ColorAndOpacity(Soft)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[SAssignNew(ClubText, STextBlock).Font(Font(16, "Bold")).ColorAndOpacity(Gold)])
		]
		// the shot data
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(24, 24, 24, 48)
		[
			SAssignNew(ShotBox, SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(FMargin(18, 8)).Visibility(EVisibility::Collapsed)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SAssignNew(Shot1, STextBlock).Font(Font(17, "Bold")).ColorAndOpacity(Ink)]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 3, 0, 0)[SAssignNew(Shot2, STextBlock).Font(Font(15)).ColorAndOpacity(Gold)]
			]
		]
		// the launch monitor
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(24)
		[
			Box(SAssignNew(Status, STextBlock).Font(Font(13)).ColorAndOpacity(Soft), FMargin(10, 5))
		]
		// the keys
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(24)
		[
			SAssignNew(Keys, STextBlock).Font(Font(12)).ColorAndOpacity(FLinearColor(1, 1, 1, 0.7f)).ShadowOffset(FVector2D(1, 1))
			.Text(FText::FromString(TEXT("Left / Right: aim     Up / Down: club     Space: test shot     Enter: skip / continue     Tab: scorecard     Backspace: mulligan")))
		]
		// banners and toasts
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 140, 0, 0)
		[
			SAssignNew(BannerBox, SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(FMargin(36, 14)).Visibility(EVisibility::Collapsed)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SAssignNew(BannerTitle, STextBlock).Font(Font(34, "Bold")).ColorAndOpacity(Ink)]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 0)[SAssignNew(BannerSmall, STextBlock).Font(Font(16)).ColorAndOpacity(Soft)]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 260, 0, 0) // (above the ball, which sits low in the view)
		[
			SAssignNew(ToastBox, SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(FMargin(16, 7)).Visibility(EVisibility::Collapsed)
			[
				SAssignNew(ToastText, STextBlock).Font(Font(16)).ColorAndOpacity(Ink)
			]
		]
		// the scorecard
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SAssignNew(CardSlot, SBox).Visibility(EVisibility::Collapsed)
		]
	];
}

void SOTGHud::Tick(const FGeometry& Geometry, const double Time, const float Dt)
{
	SCompoundWidget::Tick(Geometry, Time, Dt);
	auto Fade = [Dt](float& Left, const TSharedPtr<SBorder>& B) {
		if (Left <= 0) return;
		Left -= Dt;
		B->SetRenderOpacity(FMath::Clamp(Left / 0.4f, 0.f, 1.f));
		if (Left <= 0) B->SetVisibility(EVisibility::Collapsed);
	};
	Fade(BannerLeft, BannerBox);
	Fade(ToastLeft, ToastBox);
}

void SOTGHud::SetInfo(const FString& Line1, const FString& Line2, const FString& ClubLine)
{
	Info1->SetText(FText::FromString(Line1));
	Info2->SetText(FText::FromString(Line2));
	ClubText->SetText(FText::FromString(ClubLine));
}

void SOTGHud::SetShotData(const FString& Line1, const FString& Line2)
{
	ShotBox->SetVisibility(Line1.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
	Shot1->SetText(FText::FromString(Line1));
	Shot2->SetText(FText::FromString(Line2));
	Shot2->SetVisibility(Line2.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
}

void SOTGHud::SetStatus(const FString& Text, bool bReady)
{
	Status->SetText(FText::FromString(Text));
	Status->SetColorAndOpacity(bReady ? FLinearColor(0.6f, 1.f, 0.6f, 1.f) : Soft);
}

void SOTGHud::Banner(const FString& Title, const FString& Small, float Seconds)
{
	BannerTitle->SetText(FText::FromString(Title));
	BannerSmall->SetText(FText::FromString(Small));
	BannerSmall->SetVisibility(Small.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
	BannerBox->SetVisibility(EVisibility::HitTestInvisible);
	BannerBox->SetRenderOpacity(1);
	BannerLeft = Seconds;
}

void SOTGHud::Toast(const FString& Text, float Seconds)
{
	ToastText->SetText(FText::FromString(Text));
	ToastBox->SetVisibility(EVisibility::HitTestInvisible);
	ToastBox->SetRenderOpacity(1);
	ToastLeft = Seconds;
}

void SOTGHud::ShowScorecard(const FString& Course, const TArray<FOTGScoreRow>& Rows, bool bFinal)
{
	TSharedRef<SGridPanel> Grid = SNew(SGridPanel);
	auto Cell = [&](int32 Col, int32 Row, const FString& S, const FLinearColor& C, bool bBold = false) {
		Grid->AddSlot(Col, Row).Padding(FMargin(7, 3)).HAlign(HAlign_Center)[SNew(STextBlock).Text(FText::FromString(S)).Font(Font(15, bBold ? "Bold" : "Regular")).ColorAndOpacity(C)];
	};
	// front and back nine in rows of their own (a short course: one row)
	const int32 PerRow = Rows.Num() > 9 ? 9 : Rows.Num();
	int32 Row = 0, TotalPar = 0, TotalStrokes = 0, PlayedPar = 0;
	for (int32 Start = 0; Start < Rows.Num(); Start += PerRow)
	{
		const int32 End = FMath::Min(Rows.Num(), Start + PerRow);
		Cell(0, Row, TEXT("Hole"), Soft, true);
		Cell(0, Row + 1, TEXT("Par"), Soft, true);
		Cell(0, Row + 2, TEXT("Score"), Soft, true);
		int32 SubPar = 0, SubStrokes = 0;
		for (int32 i = Start; i < End; i++)
		{
			const FOTGScoreRow& R = Rows[i];
			Cell(1 + i - Start, Row, FString::FromInt(R.Number), Soft);
			Cell(1 + i - Start, Row + 1, FString::FromInt(R.Par), Ink);
			const FLinearColor C = R.Strokes < 0 ? Soft : R.Strokes < R.Par ? FLinearColor(1.f, 0.45f, 0.4f) : R.Strokes > R.Par ? FLinearColor(0.55f, 0.75f, 1.f) : Ink;
			Cell(1 + i - Start, Row + 2, R.Strokes < 0 ? TEXT("-") : FString::FromInt(R.Strokes), C, true);
			SubPar += R.Par;
			TotalPar += R.Par;
			if (R.Strokes >= 0) { SubStrokes += R.Strokes; TotalStrokes += R.Strokes; PlayedPar += R.Par; }
		}
		Cell(1 + PerRow, Row, Start == 0 && Rows.Num() > 9 ? TEXT("Out") : Rows.Num() > 9 ? TEXT("In") : TEXT("Total"), Soft, true);
		Cell(1 + PerRow, Row + 1, FString::FromInt(SubPar), Ink, true);
		Cell(1 + PerRow, Row + 2, FString::FromInt(SubStrokes), Ink, true);
		Row += 4;
	}
	const int32 ToPar = TotalStrokes - PlayedPar;
	const FString Summary = FString::Printf(TEXT("%d strokes, %s"), TotalStrokes, ToPar == 0 ? TEXT("even par") : *FString::Printf(TEXT("%+d"), ToPar));
	CardSlot->SetContent(
		Box(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)[SNew(STextBlock).Text(FText::FromString(bFinal ? Course + TEXT(": round complete") : Course)).Font(Font(24, "Bold")).ColorAndOpacity(Ink)]
			+ SVerticalBox::Slot().AutoHeight()[Grid]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)[SNew(STextBlock).Text(FText::FromString(Summary)).Font(Font(18, "Bold")).ColorAndOpacity(Gold)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)[SNew(STextBlock).Text(FText::FromString(bFinal ? TEXT("Enter: back to the start") : TEXT("Enter: next hole"))).Font(Font(13)).ColorAndOpacity(Soft)],
			FMargin(28, 18)));
	CardSlot->SetVisibility(EVisibility::HitTestInvisible);
}

void SOTGHud::HideScorecard()
{
	CardSlot->SetVisibility(EVisibility::Collapsed);
}
