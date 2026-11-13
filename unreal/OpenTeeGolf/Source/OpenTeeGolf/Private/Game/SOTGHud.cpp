// See SOTGHud.h.

#include "SOTGHud.h"

#include "OTGUi.h"
#include "SOTGMenus.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

using namespace otg::ui;

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
		// who's winning (players together)
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(24)
		[
			SAssignNew(StandBox, SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(FMargin(14, 8)).Visibility(EVisibility::Collapsed)
			[
				SAssignNew(StandList, SVerticalBox)
			]
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
			.Text(FText::FromString(TEXT("Left / Right: aim     Up / Down: club     Space: test shot     Enter: skip / continue     Tab: scorecard     Backspace: mulligan     Esc: menu")))
		]
		// banners and toasts
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 140, 0, 0)
		[
			SAssignNew(BannerBox, SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(FMargin(36, 14)).Visibility(EVisibility::Collapsed)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SAssignNew(BannerTitle, STextBlock).Font(Font(34, "Bold")).ColorAndOpacity(Ink)]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 0)[SAssignNew(BannerSmall, STextBlock).Font(Font(16)).ColorAndOpacity(Soft).Justification(ETextJustify::Center)]
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
		// the pause menu
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SAssignNew(PauseSlot, SBox).Visibility(EVisibility::Collapsed)
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
	Status->SetColorAndOpacity(bReady ? Green : Soft);
}

void SOTGHud::SetStandings(const FString& Title, const TArray<FOTGStanding>& Rows, const FString& Summary)
{
	// (rebuilt only when something changed)
	FString Key = Title + Summary;
	for (const FOTGStanding& R : Rows) Key += R.Name + R.Value + R.Sub;
	if (Key == StandKey) return;
	StandKey = Key;
	StandList->ClearChildren();
	StandList->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[Text(Title, 13, Soft, "Bold")];
	for (const FOTGStanding& R : Rows)
	{
		StandList->AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(SBox).WidthOverride(10).HeightOverride(10)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(R.Colour)]]
			+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 14, 0)[Text(R.Name, 15, Ink, "Bold")]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Text(R.Value, 15, Gold, "Bold")]
			+ SHorizontalBox::Slot().AutoWidth()[Text(R.Sub, 13, Soft)]
		];
	}
	if (!Summary.IsEmpty()) StandList->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Text(Summary, 13, Soft)];
	StandBox->SetVisibility(Rows.Num() ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
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

void SOTGHud::Toast(const FString& TextS, float Seconds)
{
	ToastText->SetText(FText::FromString(TextS));
	ToastBox->SetVisibility(EVisibility::HitTestInvisible);
	ToastBox->SetRenderOpacity(1);
	ToastLeft = Seconds;
}

void SOTGHud::ShowScorecard(const FOTGCard& Card)
{
	TSharedRef<SGridPanel> Grid = SNew(SGridPanel);
	auto Cell = [&](int32 Col, int32 Row, const FString& S, const FLinearColor& C, bool bBold = false) {
		Grid->AddSlot(Col, Row).Padding(FMargin(7, 3)).HAlign(HAlign_Center)[Text(S, 15, C, bBold ? "Bold" : "Regular")];
	};
	// front and back nine in rows of their own (a short course: one row)
	const int32 N = Card.Holes.Num();
	const int32 PerRow = N > 9 ? 9 : N;
	int32 Row = 0;
	TArray<int32> Totals;
	Totals.Init(0, Card.Sides.Num());
	TArray<int32> PlayedPar;
	PlayedPar.Init(0, Card.Sides.Num());
	for (int32 Start = 0; Start < N; Start += PerRow)
	{
		const int32 End = FMath::Min(N, Start + PerRow);
		Cell(0, Row, TEXT("Hole"), Soft, true);
		Cell(0, Row + 1, TEXT("Par"), Soft, true);
		int32 SubPar = 0;
		for (int32 i = Start; i < End; i++)
		{
			Cell(1 + i - Start, Row, FString::FromInt(Card.Holes[i].Number), Soft);
			Cell(1 + i - Start, Row + 1, FString::FromInt(Card.Holes[i].Par), Ink);
			SubPar += Card.Holes[i].Par;
		}
		const FString SubName = N > 9 ? (Start == 0 ? TEXT("Out") : TEXT("In")) : TEXT("Total");
		Cell(1 + PerRow, Row, SubName, Soft, true);
		Cell(1 + PerRow, Row + 1, FString::FromInt(SubPar), Ink, true);
		for (int32 k = 0; k < Card.Sides.Num(); k++)
		{
			const FOTGCard::FSide& S = Card.Sides[k];
			Cell(0, Row + 2 + k, S.Name, Card.Sides.Num() > 1 ? S.Colour : Soft, true);
			int32 Sub = 0;
			for (int32 i = Start; i < End; i++)
			{
				const int32 V = S.Scores.IsValidIndex(i) ? S.Scores[i] : -1;
				const int32 Par = Card.Holes[i].Par;
				const FLinearColor C = V < 0 ? Soft : V < Par ? FLinearColor(1.f, 0.45f, 0.4f) : V > Par ? FLinearColor(0.55f, 0.75f, 1.f) : Ink;
				Cell(1 + i - Start, Row + 2 + k, V < 0 ? TEXT("-") : FString::FromInt(V), C, true);
				if (V >= 0) { Sub += V; Totals[k] += V; PlayedPar[k] += Par; }
			}
			Cell(1 + PerRow, Row + 2 + k, FString::FromInt(Sub), Ink, true);
		}
		Row += 3 + Card.Sides.Num();
	}
	TSharedRef<SVerticalBox> Foot = SNew(SVerticalBox);
	if (Card.Sides.Num() == 1)
	{
		const int32 ToPar = Totals[0] - PlayedPar[0];
		Foot->AddSlot().AutoHeight().HAlign(HAlign_Center)[Text(FString::Printf(TEXT("%d strokes, %s"), Totals[0], ToPar == 0 ? TEXT("even par") : *FString::Printf(TEXT("%+d"), ToPar)), 18, Gold, "Bold")];
	}
	else
	{
		for (const FOTGStanding& R : Card.Standings)
			Foot->AddSlot().AutoHeight().HAlign(HAlign_Center)[Text(FString::Printf(TEXT("%s  %s  %s"), *R.Name, *R.Value, *R.Sub), 16, R.Colour, "Bold")];
		if (!Card.Summary.IsEmpty()) Foot->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 0)[Text(Card.Summary, 15, Gold, "Bold")];
	}
	const FString Heading = (Card.bFinal ? Card.Course + TEXT(": round complete") : Card.Course) + (Card.Format.IsEmpty() ? FString() : TEXT("  ·  ") + Card.Format);
	CardSlot->SetContent(
		Box(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)[Text(Heading, 24, Ink, "Bold")]
			+ SVerticalBox::Slot().AutoHeight()[Grid]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)[Foot]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)[Text(Card.bFinal ? TEXT("Enter: back to the menu") : TEXT("Enter: next hole"), 13, Soft)],
			FMargin(28, 18), Solid));
	CardSlot->SetVisibility(EVisibility::HitTestInvisible);
}

void SOTGHud::HideScorecard()
{
	CardSlot->SetVisibility(EVisibility::Collapsed);
}

void SOTGHud::ShowPause(FSimpleDelegate OnResume, FSimpleDelegate OnScorecard, FSimpleDelegate OnQuit)
{
	PauseSlot->SetContent(
		SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0, 0, 0, 0.45f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			Box(SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 24, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)[Text(TEXT("Paused"), 28, Ink, "Bold")]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)[Button(TEXT("Resume"), [OnResume]() { OnResume.ExecuteIfBound(); }, 17)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)[Button(TEXT("Scorecard"), [OnScorecard]() { OnScorecard.ExecuteIfBound(); }, 17)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)[Button(TEXT("Quit to the menu"), [OnQuit]() { OnQuit.ExecuteIfBound(); }, 17)]
				]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SOTGLaunchMonitorPanel)],
				FMargin(28, 22), Solid)
		]);
	PauseSlot->SetVisibility(EVisibility::Visible);
}

void SOTGHud::HidePause()
{
	PauseSlot->SetVisibility(EVisibility::Collapsed);
	PauseSlot->SetContent(SNullWidget::NullWidget);
}
