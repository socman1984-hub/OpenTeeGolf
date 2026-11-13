// The game's look in Slate: fonts, colours, panels and buttons, shared by the HUD and the menus.

#pragma once

#include "CoreMinimal.h"
#include "Styling/CoreStyle.h"
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
	inline TSharedRef<SButton> Button(const FString& Label, TFunction<void()> OnClick, int32 Size = 15, bool bOn = false)
	{
		return SNew(SButton)
			.ButtonColorAndOpacity(bOn ? FLinearColor(0.25f, 0.55f, 0.3f, 1.f) : FLinearColor(0.18f, 0.22f, 0.19f, 1.f))
			.ContentPadding(FMargin(12, 6))
			.OnClicked_Lambda([OnClick]() { if (OnClick) OnClick(); return FReply::Handled(); })
			[SNew(STextBlock).Text(FText::FromString(Label)).Font(Font(Size, bOn ? "Bold" : "Regular")).ColorAndOpacity(Ink)];
	}
}
