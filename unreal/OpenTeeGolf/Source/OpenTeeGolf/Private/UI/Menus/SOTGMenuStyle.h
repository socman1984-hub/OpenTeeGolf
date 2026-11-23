// The menus' own look and small widgets, shared by their pages (rounded glass cards, small capital
// labels, chips, the Discord button; everything else is OTGUi.h's). Private to UI/Menus.

#pragma once

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

namespace
{
	UGameInstance* GameInstance()
	{
		return GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetGameInstance() : nullptr;
	}
	const TCHAR* TeeOrder[] = { TEXT("Black"), TEXT("Blue"), TEXT("White"), TEXT("Gold"), TEXT("Green"), TEXT("Yellow"), TEXT("Red"), TEXT("Junior"), TEXT("Par3") };
	const TCHAR* PinDays[] = { TEXT("Thursday"), TEXT("Friday"), TEXT("Saturday"), TEXT("Sunday") };
	const TCHAR* PlayerTees[] = { TEXT(""), TEXT("Black"), TEXT("Blue"), TEXT("White"), TEXT("Gold"), TEXT("Green"), TEXT("Red") };

	TSharedRef<SWidget> Heading(const FString& S) { return Text(S, 14, Soft, "Bold"); }
	const FSpinBoxStyle& SpinStyle()
	{
		static const FSpinBoxStyle S = FSpinBoxStyle(FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox"))
			.SetBackgroundBrush(FSlateRoundedBoxBrush(FLinearColor(0, 0, 0, 0.35f), 7.f, FLinearColor(1, 1, 1, 0.14f), 1.f))
			.SetHoveredBackgroundBrush(FSlateRoundedBoxBrush(FLinearColor(0, 0, 0, 0.42f), 7.f, FLinearColor(1, 1, 1, 0.28f), 1.f))
			.SetActiveBackgroundBrush(FSlateRoundedBoxBrush(FLinearColor(0, 0, 0, 0.45f), 7.f, FLinearColor(0.45f, 0.95f, 0.52f, 0.8f), 1.f))
			.SetActiveFillBrush(FSlateRoundedBoxBrush(FLinearColor(0.3f, 0.75f, 0.4f, 0.35f), 7.f))
			.SetHoveredFillBrush(FSlateRoundedBoxBrush(FLinearColor(0.3f, 0.75f, 0.4f, 0.30f), 7.f))
			.SetInactiveFillBrush(FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, 0.06f), 7.f))
			.SetForegroundColor(Ink).SetTextPadding(FMargin(10, 6));
		return S;
	}
	TSharedRef<SWidget> Number(double Min, double Max, double Delta, TFunction<double()> Get, TFunction<void(double)> Set)
	{
		return SNew(SBox).WidthOverride(110)
			[
				SNew(SSpinBox<double>).Style(&SpinStyle()).MinValue(Min).MaxValue(Max).Delta(Delta)
				.Value_Lambda([Get]() { return Get(); })
				.OnValueChanged_Lambda([Set](double V) { Set(V); })
			];
	}
	TSharedRef<SWidget> Row(const FString& Label, const TSharedRef<SWidget>& W)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[SNew(SBox).WidthOverride(190)[Text(Label, 14, Soft)]]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[W];
	}

	// ---------------------------------------------------------------- the main menu's look
	// (rounded glass cards, small capital labels, chips; everything else is OTGUi.h's)

	const FSlateBrush* CardBrush()
	{
		static const FSlateRoundedBoxBrush B(FLinearColor(0.018f, 0.03f, 0.022f, 0.90f), 12.f, FLinearColor(1, 1, 1, 0.07f), 1.f);
		return &B;
	}
	const FSlateBrush* InsetBrush()
	{
		static const FSlateRoundedBoxBrush B(FLinearColor(1, 1, 1, 0.045f), 8.f);
		return &B;
	}
	const FSlateBrush* PillBrush()
	{
		static const FSlateRoundedBoxBrush B(FLinearColor::White, 9.f);
		return &B;
	}
	const FLinearColor Dim(0.62f, 0.68f, 0.62f, 1.f);
	const FLinearColor Accent(0.40f, 0.86f, 0.48f, 1.f);

	TSharedRef<SBorder> Card(const TSharedRef<SWidget>& Content, FMargin Pad = FMargin(20, 16))
	{
		return SNew(SBorder).BorderImage(CardBrush()).Padding(Pad)[Content];
	}
	// a small spaced-out capitals label
	TSharedRef<STextBlock> Caps(const FString& S, int32 Size = 11, const FLinearColor& Colour = Dim)
	{
		FSlateFontInfo F = Font(Size, "Bold");
		F.LetterSpacing = 110;
		return SNew(STextBlock).Text(FText::FromString(S.ToUpper())).Font(F).ColorAndOpacity(Colour);
	}
	TSharedRef<STextBlock> Para(const FString& S, int32 Size = 14, const FLinearColor& Colour = Soft)
	{
		return SNew(STextBlock).Text(FText::FromString(S)).Font(Font(Size)).ColorAndOpacity(Colour).AutoWrapText(true);
	}
	TSharedRef<SWidget> Pill(const FString& S, const FLinearColor& Fill, const FLinearColor& Ink_ = Ink, int32 Size = 10)
	{
		return SNew(SBorder).BorderImage(PillBrush()).BorderBackgroundColor(Fill).Padding(FMargin(9, 3)).VAlign(VAlign_Center)[Caps(S, Size, Ink_)];
	}

	// the top tabs: words, the page shown underlined in the accent
	const FButtonStyle& TabStyle()
	{
		static const FButtonStyle S = FButtonStyle()
			.SetNormal(FSlateNoResource())
			.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, 0.06f), 6.f))
			.SetPressed(FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, 0.03f), 6.f))
			.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
		return S;
	}
	// a course's row in the list: faint, lighter under the mouse; the one picked green-edged
	const FButtonStyle& RowStyle(bool bOn)
	{
		static const FButtonStyle Off = FButtonStyle()
			.SetNormal(FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, 0.025f), 9.f))
			.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, 0.08f), 9.f, FLinearColor(1, 1, 1, 0.12f), 1.f))
			.SetPressed(FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, 0.05f), 9.f, FLinearColor(1, 1, 1, 0.12f), 1.f))
			.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
		static const FButtonStyle On = FButtonStyle()
			.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.13f, 0.34f, 0.18f, 0.80f), 9.f, FLinearColor(0.45f, 0.95f, 0.52f, 0.65f), 1.5f))
			.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.15f, 0.38f, 0.20f, 0.85f), 9.f, FLinearColor(0.5f, 1.f, 0.58f, 0.8f), 1.5f))
			.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.12f, 0.30f, 0.16f, 0.85f), 9.f, FLinearColor(0.5f, 1.f, 0.58f, 0.8f), 1.5f))
			.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
		return bOn ? On : Off;
	}
	// a choice: a rounded chip, the chosen one green (with a colour dot, for tees)
	TSharedRef<SWidget> Chip(const FString& Label, TFunction<void()> OnClick, bool bOn, const FLinearColor& Dot = FLinearColor::Transparent, int32 Size = 13)
	{
		TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
		if (Dot.A > 0)
			H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 7, 0)
			[
				SNew(SBox).WidthOverride(10).HeightOverride(10)[SNew(SBorder).BorderImage(PillBrush()).BorderBackgroundColor(Dot).Padding(0)]
			];
		H->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Label)).Font(Font(Size, bOn ? "Bold" : "Regular")).ColorAndOpacity(bOn ? Ink : Soft)];
		return SNew(SButton).ButtonStyle(&ButtonStyle(bOn)).ContentPadding(FMargin(12, 6))
			.OnClicked_Lambda([OnClick]() { if (OnClick) OnClick(); return FReply::Handled(); })[H];
	}
	TSharedRef<SWidget> Ghost(const FString& Label, TFunction<void()> OnClick, int32 Size = 12)
	{
		return SNew(SButton).ButtonStyle(&TabStyle()).ContentPadding(FMargin(10, 5))
			.OnClicked_Lambda([OnClick]() { if (OnClick) OnClick(); return FReply::Handled(); })
			[SNew(STextBlock).Text(FText::FromString(Label)).Font(Font(Size, "Bold")).ColorAndOpacity(Accent)];
	}
	// where players talk, and every release's notes (opened in the browser)
	const TCHAR* DiscordUrl = TEXT("https://discord.gg/YNsyWkCayd");
	const TCHAR* ReleasesUrl = TEXT("https://github.com/socman1984-hub/OpenTeeGolf/releases");
	void OpenUrl(const TCHAR* Url) { FPlatformProcess::LaunchURL(Url, nullptr, nullptr); }
	// the Discord button: its own blurple
	TSharedRef<SWidget> DiscordButton(int32 Size = 14)
	{
		static const FButtonStyle S = FButtonStyle()
			.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.10f, 0.12f, 0.80f, 0.95f), 7.f, FLinearColor(1, 1, 1, 0.15f), 1.f))
			.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.14f, 0.17f, 0.92f, 1.f), 7.f, FLinearColor(1, 1, 1, 0.35f), 1.f))
			.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.08f, 0.09f, 0.62f, 1.f), 7.f, FLinearColor(1, 1, 1, 0.35f), 1.f))
			.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0, 1, 0, 0));
		return SNew(SButton).ButtonStyle(&S).ContentPadding(FMargin(12, 6))
			.ToolTipText(FText::FromString(TEXT("Questions, problems, course requests and what's coming next: opens discord.gg/YNsyWkCayd in your browser")))
			.OnClicked_Lambda([]() { OpenUrl(DiscordUrl); return FReply::Handled(); })
			[SNew(STextBlock).Text(FText::FromString(TEXT("Join the Discord"))).Font(Font(Size, "Bold")).ColorAndOpacity(Ink)];
	}
	const FEditableTextBoxStyle& SearchStyle()
	{
		static const FEditableTextBoxStyle S = FEditableTextBoxStyle(FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
			.SetBackgroundImageNormal(FSlateRoundedBoxBrush(FLinearColor(0, 0, 0, 0.35f), 8.f, FLinearColor(1, 1, 1, 0.12f), 1.f))
			.SetBackgroundImageHovered(FSlateRoundedBoxBrush(FLinearColor(0, 0, 0, 0.40f), 8.f, FLinearColor(1, 1, 1, 0.25f), 1.f))
			.SetBackgroundImageFocused(FSlateRoundedBoxBrush(FLinearColor(0, 0, 0, 0.45f), 8.f, FLinearColor(0.45f, 0.95f, 0.52f, 0.8f), 1.5f))
			.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(FLinearColor(0, 0, 0, 0.35f), 8.f))
			.SetPadding(FMargin(12, 9))
			.SetForegroundColor(Ink).SetFocusedForegroundColor(Ink).SetBackgroundColor(FLinearColor::White);
		return S;
	}
	FLinearColor TeeColour(const FString& T)
	{
		if (T == TEXT("Black")) return FLinearColor(0.02f, 0.02f, 0.02f, 1.f);
		if (T == TEXT("Blue")) return FLinearColor(0.15f, 0.35f, 0.95f, 1.f);
		if (T == TEXT("White")) return FLinearColor(0.95f, 0.95f, 0.95f, 1.f);
		if (T == TEXT("Gold") || T == TEXT("Yellow")) return FLinearColor(1.f, 0.78f, 0.2f, 1.f);
		if (T == TEXT("Green")) return FLinearColor(0.2f, 0.7f, 0.3f, 1.f);
		if (T == TEXT("Red")) return FLinearColor(0.9f, 0.15f, 0.12f, 1.f);
		if (T == TEXT("Junior")) return FLinearColor(1.f, 0.5f, 0.15f, 1.f);
		return FLinearColor(0.6f, 0.6f, 0.6f, 1.f);
	}
	FString Ago(const FDateTime& Utc)
	{
		const FTimespan D = FDateTime::UtcNow() - Utc;
		if (D.GetTotalHours() < 1) return TEXT("Just now");
		if (D.GetTotalDays() < 1) return FString::Printf(TEXT("%d h ago"), int32(D.GetTotalHours()));
		if (D.GetTotalDays() < 2) return TEXT("Yesterday");
		if (D.GetTotalDays() < 31) return FString::Printf(TEXT("%d days ago"), int32(D.GetTotalDays()));
		return (Utc + (FDateTime::Now() - FDateTime::UtcNow())).ToString(TEXT("%d %b %Y"));
	}
	// the weather's short name ("Live (the course's weather now)": "Live")
	FString ShortName(const FString& S)
	{
		int32 At;
		return S.FindChar(TEXT('('), At) ? S.Left(At).TrimStartAndEnd() : S;
	}
}
