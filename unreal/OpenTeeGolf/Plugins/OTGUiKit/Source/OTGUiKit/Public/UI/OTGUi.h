// The game's look in Slate: fonts, colours, panels and buttons, shared by the HUD and the menus.

#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

namespace otg::ui
{
	inline FSlateFontInfo Font(int32 Size, const char* Weight = "Regular") { return FCoreStyle::GetDefaultFontStyle(Weight, Size); }
	inline const FLinearColor Panel(0.02f, 0.03f, 0.02f, 0.55f);
	inline const FLinearColor Solid(0.03f, 0.05f, 0.035f, 0.92f);
	inline const FLinearColor Ink(0.96f, 0.96f, 0.92f, 1.f);
	inline const FLinearColor Soft(0.80f, 0.84f, 0.78f, 1.f);
	inline const FLinearColor Gold(1.0f, 0.82f, 0.35f, 1.f);
	inline const FLinearColor Warn(1.0f, 0.55f, 0.4f, 1.f);
	inline const FLinearColor Green(0.6f, 1.f, 0.6f, 1.f);

	inline TSharedRef<SBorder> Box(const TSharedRef<SWidget>& Content, FMargin Pad = FMargin(14, 8), const FLinearColor& Colour = Panel)
	{
		return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Colour).Padding(Pad)[Content];
	}
	inline TSharedRef<STextBlock> Text(const FString& S, int32 Size = 15, const FLinearColor& Colour = Ink, const char* Weight = "Regular")
	{
		return SNew(STextBlock).Text(FText::FromString(S)).Font(Font(Size, Weight)).ColorAndOpacity(Colour).AutoWrapText(false);
	}
	// Rounded buttons: dark glass, lighter under the mouse; "on" ones in the accent green
	inline const FButtonStyle& ButtonStyle(bool bOn)
	{
		static const FButtonStyle Off = FButtonStyle()
			.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.10f, 0.14f, 0.11f, 0.88f), 7.f, FLinearColor(1, 1, 1, 0.08f), 1.f))
			.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.18f, 0.25f, 0.19f, 0.95f), 7.f, FLinearColor(1, 1, 1, 0.25f), 1.f))
			.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.07f, 0.10f, 0.08f, 0.95f), 7.f, FLinearColor(1, 1, 1, 0.25f), 1.f))
			.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(0.10f, 0.12f, 0.11f, 0.5f), 7.f))
			.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0, 1, 0, 0));
		static const FButtonStyle On = FButtonStyle()
			.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.20f, 0.52f, 0.26f, 0.95f), 7.f, FLinearColor(0.6f, 1.f, 0.6f, 0.35f), 1.f))
			.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.26f, 0.62f, 0.32f, 1.f), 7.f, FLinearColor(0.7f, 1.f, 0.7f, 0.5f), 1.f))
			.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.16f, 0.44f, 0.22f, 1.f), 7.f, FLinearColor(0.7f, 1.f, 0.7f, 0.5f), 1.f))
			.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(0.16f, 0.30f, 0.19f, 0.5f), 7.f))
			.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0, 1, 0, 0));
		return bOn ? On : Off;
	}

	inline TSharedRef<SButton> Button(const FString& Label, TFunction<void()> OnClick, int32 Size = 15, bool bOn = false)
	{
		return SNew(SButton)
			.ButtonStyle(&ButtonStyle(bOn))
			.ContentPadding(FMargin(12, 6))
			.OnClicked_Lambda([OnClick]() { if (OnClick) OnClick(); return FReply::Handled(); })
			[SNew(STextBlock).Text(FText::FromString(Label)).Font(Font(Size, bOn ? "Bold" : "Regular")).ColorAndOpacity(Ink)];
	}
}
