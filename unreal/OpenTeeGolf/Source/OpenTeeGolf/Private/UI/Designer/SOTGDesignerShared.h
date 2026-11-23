// Small widgets and grass-kind helpers shared by the designers' files. Private to UI/Designer.

#pragma once

#include "UI/Designer/SOTGCourseDesigner.h"
#include "UI/OTGUi.h"
#include "World/OTGRuntimeCourse.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
using namespace otg::ui;

namespace
{
	// A drop-down of choices (Values, shown by Label), the current one shown; Set when one's picked
	TSharedRef<SWidget> Dropdown(const TArray<FString>& Values, const FString& Current, TFunction<FString(const FString&)> Label, TFunction<void(const FString&)> Set)
	{
		// (the options outlive this call: the combo box holds them by pointer, the lambdas keep them)
		TSharedRef<TArray<TSharedPtr<FString>>> Opts = MakeShared<TArray<TSharedPtr<FString>>>();
		TSharedPtr<FString> Sel;
		for (const FString& V : Values)
		{
			Opts->Add(MakeShared<FString>(V));
			if (V == Current) Sel = Opts->Last();
		}
		return SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&Opts.Get())
			.InitiallySelectedItem(Sel)
			.OnGenerateWidget_Lambda([Opts, Label](TSharedPtr<FString> P) { return StaticCastSharedRef<SWidget>(Text(Label(*P), 12)); })
			.OnSelectionChanged_Lambda([Opts, Set](TSharedPtr<FString> P, ESelectInfo::Type How) { if (P.IsValid() && How != ESelectInfo::Direct) Set(*P); })
			[SNew(STextBlock).Font(Font(12)).Text(FText::FromString(Label(Current)))];
	}

	// The tall grass's patches: the noise's lightness runs 0.27..0.46 (OTGGrass.cpp Place); a kind
	// grows where it is past the middle of its Lo..Hi (or short of it, inverted)
	constexpr float PatchL0 = 0.27f, PatchL1 = 0.46f;
	float Cover(const otg::FGrassKind& K)
	{
		const float C = FMath::Clamp(((K.PatchLo + K.PatchHi) / 2 - PatchL0) / (PatchL1 - PatchL0), 0.f, 1.f);
		return K.bInvert ? C : 1.f - C;
	}
	void SetCover(otg::FGrassKind& K, float V)
	{
		const float W = FMath::Max(0.01f, K.PatchHi - K.PatchLo);
		const float C = PatchL0 + (PatchL1 - PatchL0) * (K.bInvert ? V : 1.f - V);
		K.PatchLo = C - W / 2; K.PatchHi = C + W / 2;
	}
	float Mid(const otg::FGrassKind& K) { return (K.ScaleLo + K.ScaleHi) / 2; }
	float Spread(const otg::FGrassKind& K) { return (K.ScaleHi - K.ScaleLo) / FMath::Max(0.01f, K.ScaleHi + K.ScaleLo); }
	void SetScale(otg::FGrassKind& K, float M, float S) { K.ScaleLo = FMath::Max(0.02f, M * (1 - S)); K.ScaleHi = FMath::Max(K.ScaleLo, M * (1 + S)); }
}
