// See SOTGMowSwatch.h.

#include "UI/Editor/SOTGMowSwatch.h"
#include "UI/Editor/SOTGCourseEditor.h"

#include "UI/OTGUi.h"
#include "Course/OTGTextures.h"
#include "Course/OTGCourseData.h"
#include "Course/OTGCourseSubsystem.h"
#include "App/OTGHelperSubsystem.h"
#include "World/OTGTreeDesign.h"
#include "Course/OTGQuickUpdate.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

using namespace otg::ui;

#include "UI/Editor/SOTGEditorShared.h"

// ------------------------------------------------------------------ the panels

// A mowing pattern, light and dark as the ground's material draws it (mowing.js mowShade), 1 px = 0.25 m
void SOTGMowSwatch::Construct(const FArguments& A) { Id = A._Pattern; Width = A._WidthM; }

int32 SOTGMowSwatch::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const int32 N = 16;
	const double Cell = 64.0 / N, M = 0.25 * 64 / N * 2;   // (each cell this many metres)
	auto S = [this](double A) { return FMath::Clamp(FMath::Sin(A / Width * PI) * 6, -1.0, 1.0); };
	for (int32 j = 0; j < N; j++)
		for (int32 i = 0; i < N; i++)
		{
			const double U = i * M, V = j * M, D1 = (U + V) / UE_SQRT_2, D2 = (V - U) / UE_SQRT_2;
			const double Sh = Id == TEXT("stripes") ? S(U) : Id == TEXT("diagonal") ? S(D1) : Id == TEXT("checker") ? S(U) * S(V) : Id == TEXT("diamond") ? S(D1) * S(D2) : 0;
			const float L = float(0.32 + 0.06 * Sh);
			FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2D(Cell + 0.5, Cell + 0.5), FSlateLayoutTransform(FVector2D(i * Cell, j * Cell))), White, ESlateDrawEffect::None, FLinearColor(L * 0.55f, L, L * 0.4f, 1));
		}
	return Layer;
}
