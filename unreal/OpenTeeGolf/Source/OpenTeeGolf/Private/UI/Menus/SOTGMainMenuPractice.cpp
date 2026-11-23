// The main menu's Practice and Players pages (see SOTGMainMenu.h).

#include "UI/Menus/SOTGMainMenu.h"
#include "UI/Menus/SOTGLaunchMonitorPanel.h"
#include "Round/OTGMiniGolf.h"
#include "UI/Builder/SOTGCourseBuilder.h"
#include "UI/Editor/SOTGCourseEditor.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "GeneralProjectSettings.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "App/OTGUpdateSubsystem.h"
#include "App/OTGHelperSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Course/OTGTextures.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Course/OTGCourseData.h"
#include "OTGLaunchMonitorSubsystem.h"
#include "UI/OTGUi.h"
#include "Round/OTGWeather.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Engine/Texture2D.h"

using namespace otg::ui;

#include "UI/Menus/SOTGMenuStyle.h"

// ------------------------------------------------------------------ Practice

void SOTGMainMenu::SetPracticeMode(const FString& Name, TFunction<void()> Open)
{
	if (FPracticeMode* M = PracticeModes.FindByPredicate([&](const FPracticeMode& X) { return X.Name == Name; })) M->Open = MoveTemp(Open);
	else PracticeModes.Add({ Name, FString(), MoveTemp(Open) });
	if (Page == EPage::Practice) Rebuild();
}

TSharedRef<SWidget> SOTGMainMenu::PracticePage()
{
	// a simple picture for each: a range's distance boards, a green with its cup
	auto RangeArt = []() {
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		const TCHAR* Boards[] = { TEXT("250"), TEXT("200"), TEXT("150"), TEXT("100"), TEXT("50") };
		for (const TCHAR* B : Boards)
			V->AddSlot().FillHeight(1).VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SBox).HeightOverride(1)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.18f))]]
				+ SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)[Caps(B, 9, FLinearColor(1, 1, 1, 0.55f))]
			];
		return StaticCastSharedRef<SWidget>(V);
	};
	auto PuttArt = []() {
		return StaticCastSharedRef<SWidget>(SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(150).HeightOverride(110)
				[
					SNew(SBorder).BorderImage(PillBrush()).BorderBackgroundColor(FLinearColor(0.22f, 0.55f, 0.28f, 1.f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(18).HeightOverride(18)[SNew(SBorder).BorderImage(PillBrush()).BorderBackgroundColor(FLinearColor(0.02f, 0.03f, 0.02f, 1.f))]
					]
				]
			]);
	};
	static const FSlateRoundedBoxBrush ArtBrush(FLinearColor::White, 9.f);
	TSharedRef<SHorizontalBox> Cards = SNew(SHorizontalBox);
	for (int32 i = 0; i < PracticeModes.Num(); i++)
	{
		const FPracticeMode& M = PracticeModes[i];
		const bool bReady = bool(M.Open);
		const TFunction<void()> Open = M.Open;
		const FLinearColor Tint = i % 2 ? FLinearColor(0.10f, 0.30f, 0.30f, 1.f) : FLinearColor(0.12f, 0.32f, 0.16f, 1.f);
		Cards->AddSlot().AutoWidth().Padding(0, 0, 20, 0)
		[
			SNew(SBox).WidthOverride(440)
			[
				Card(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).HeightOverride(180)
						[
							SNew(SBorder).BorderImage(&ArtBrush).BorderBackgroundColor(Tint).Padding(FMargin(20, 14))
							[i % 2 ? PuttArt() : RangeArt()]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(M.Name, 22, Ink, "Bold")]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[bReady ? SNullWidget::NullWidget : Pill(TEXT("Coming soon"), FLinearColor(1.f, 0.82f, 0.35f, 0.18f), Gold)]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[SNew(SBox).MinDesiredHeight(60)[Para(M.About, 14)]]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 0).HAlign(HAlign_Left)
					[
						SNew(SBox).IsEnabled(bReady)
						[
							SNew(SButton).ButtonStyle(&ButtonStyle(bReady)).ContentPadding(FMargin(26, 10)).OnClicked_Lambda([Open]() { if (Open) Open(); return FReply::Handled(); })
							[Text(bReady ? TEXT("Start") : TEXT("Not ready yet"), 16, bReady ? Ink : Dim, "Bold")]
						]
					], FMargin(16, 16, 16, 20))
			]
		];
	}
	return SNew(SScrollBox) + SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Text(TEXT("Practice"), 30, Ink, "Bold")]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 22)[Para(TEXT("Warm up before a round, or work on one part of your game."), 15)]
			+ SVerticalBox::Slot().AutoHeight()[Cards]
		];
}

TSharedRef<SWidget> SOTGMainMenu::PlayersPage()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Text(TEXT("Players"), 22, Ink, "Bold")];
	const TArray<FLinearColor> Colours = FOTGPlayers::Colours();
	// (each player a card: name, handicap and remove on top; then each choice laid out in full - every
	// colour, both hands, every tee - the chosen one green, or ringed for a colour)
	static const FSlateRoundedBoxBrush Ring(FLinearColor::Transparent, 15.f, FLinearColor::White, 2.f);
	static const FSlateRoundedBoxBrush NoRing(FLinearColor::Transparent, 15.f, FLinearColor(1, 1, 1, 0.f), 2.f);
	auto Choice = [](const FString& Label, const TSharedRef<SWidget>& W) -> TSharedRef<SWidget> {
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(80)[Caps(Label)]]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[W];
	};
	for (int32 i = 0; i < Draft.Num(); i++)
	{
		FOTGPlayerProfile& P = Draft[i];
		int32 Played = P.Rounds.Num();
		// the colours: a swatch each
		TSharedRef<SWrapBox> Swatches = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4));
		for (int32 k = 0; k < Colours.Num(); k++)
		{
			const bool bOn = Colours[k].Equals(P.Colour, 0.01f);
			const FLinearColor C = Colours[k];
			Swatches->AddSlot()
			[
				SNew(SButton).ButtonStyle(&TabStyle()).ContentPadding(FMargin(0))
				.ToolTipText(FText::FromString(bOn ? TEXT("This player's colour") : TEXT("Use this colour")))
				.OnClicked_Lambda([this, i, C]() { if (Draft.IsValidIndex(i)) { Draft[i].Colour = C; Rebuild(); } return FReply::Handled(); })
				[
					SNew(SBorder).BorderImage(bOn ? &Ring : &NoRing).Padding(3)
					[
						SNew(SBox).WidthOverride(22).HeightOverride(22)[SNew(SBorder).BorderImage(PillBrush()).BorderBackgroundColor(C).Padding(0)]
					]
				]
			];
		}
		// the tees: the round's, or one of these
		TSharedRef<SWrapBox> Tees = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6, 6));
		for (int32 k = 0; k < UE_ARRAY_COUNT(PlayerTees); k++)
		{
			const FString T = PlayerTees[k];
			Tees->AddSlot()[Chip(T.IsEmpty() ? FString(TEXT("Round's tees")) : T, [this, i, T]() { if (Draft.IsValidIndex(i)) { Draft[i].Tee = T; Rebuild(); } }, P.Tee == T, T.IsEmpty() ? FLinearColor::Transparent : TeeColour(T))];
		}
		TSharedRef<SVerticalBox> CardBody = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
				[
					SNew(SBox).WidthOverride(14).HeightOverride(14)[SNew(SBorder).BorderImage(PillBrush()).BorderBackgroundColor(P.Colour).Padding(0)]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 14, 0)
				[
					SNew(SBox).WidthOverride(240)[SNew(SEditableTextBox).Style(&SearchStyle()).Text(FText::FromString(P.Name)).Font(Font(15))
						.OnTextChanged_Lambda([this, i](const FText& T) { if (Draft.IsValidIndex(i)) Draft[i].Name = T.ToString(); })]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Caps(TEXT("Handicap"))]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)
				[
					Number(-10, 54, 0.1, [this, i]() { return Draft.IsValidIndex(i) ? Draft[i].Handicap : 0.0; }, [this, i](double X) { if (Draft.IsValidIndex(i)) Draft[i].Handicap = FMath::RoundToDouble(X * 10) / 10; })
				]
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(Played ? FString::Printf(TEXT("%d round%s played"), Played, Played == 1 ? TEXT("") : TEXT("s")) : FString(), 12, Soft)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Button(ConfirmRemove == i ? TEXT("Remove (sure?)") : TEXT("Remove"), [this, i]() {
					if (ConfirmRemove != i) { ConfirmRemove = i; Rebuild(); return; }
					Draft.RemoveAt(i);
					ConfirmRemove = -1;
					Rebuild();
				}, 13)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[Choice(TEXT("Colour"), Swatches)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				Choice(TEXT("Hand"), SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Chip(TEXT("Right handed"), [this, i]() { if (Draft.IsValidIndex(i)) { Draft[i].bLeftHanded = false; Rebuild(); } }, !P.bLeftHanded)]
					+ SHorizontalBox::Slot().AutoWidth()[Chip(TEXT("Left handed"), [this, i]() { if (Draft.IsValidIndex(i)) { Draft[i].bLeftHanded = true; Rebuild(); } }, P.bLeftHanded)])
			]
			+ SVerticalBox::Slot().AutoHeight()[Choice(TEXT("Tees"), Tees)];
		V->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[SNew(SBorder).BorderImage(InsetBrush()).Padding(FMargin(16, 14))[CardBody]];
	}
	if (!Draft.Num()) V->AddSlot().AutoHeight()[Text(TEXT("No players yet."), 14, Soft)];
	V->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Button(TEXT("Add a player"), [this]() { Draft.Add(FOTGPlayers::NewProfile(FString::Printf(TEXT("Player %d"), Draft.Num() + 1), Draft.Num())); Rebuild(); }, 15)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Save the players"), [this]() {
			Draft.RemoveAll([](const FOTGPlayerProfile& P) { return P.Name.TrimStartAndEnd().IsEmpty(); });
			Profiles = Draft;
			FOTGPlayers::Save(Profiles);
			Setup.Picked.RemoveAll([&](const FString& Id) { return !Profiles.ContainsByPredicate([&](const FOTGPlayerProfile& P) { return P.Id == Id; }); });
			Remember();
			Page = EPage::Play;
			Rebuild();
		}, 15, true)]
	];
	return SNew(SScrollBox) + SScrollBox::Slot()[Box(V, FMargin(20, 16), Solid)];
}
