// Helpers shared by the course editor's files: the kinds of shape, building types, polygon sums, the
// panels' rows. Private to UI/Editor.

#pragma once

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

namespace
{
	UGameInstance* GI() { return GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetGameInstance() : nullptr; }
	constexpr double MToYd = 1.09361;
	const TArray<FString> DrawKinds = { TEXT("green"), TEXT("fairway"), TEXT("bunker"), TEXT("tee"), TEXT("water"), TEXT("penalty"), TEXT("oob") };
	FString KindLabel(const FString& K)
	{
		if (K == TEXT("green")) return TEXT("Green");
		if (K == TEXT("fairway")) return TEXT("Fairway");
		if (K == TEXT("bunker")) return TEXT("Bunker");
		if (K == TEXT("tee")) return TEXT("Tee box");
		if (K == TEXT("water")) return TEXT("Water");
		if (K == TEXT("penalty")) return TEXT("Penalty area");
		if (K == TEXT("oob")) return TEXT("Out of bounds");
		if (K == TEXT("trees")) return TEXT("Tree area");
		if (K == TEXT("forest")) return TEXT("Forest");
		if (K == TEXT("biome")) return TEXT("Grass biome area");
		return K;
	}
	const TArray<TPair<FString, FString>>& BuildingTypes()
	{
		static const TArray<TPair<FString, FString>> T = { { TEXT("house"), TEXT("House") }, { TEXT("clubhouse"), TEXT("Clubhouse") }, { TEXT("garage"), TEXT("Garage") },
			{ TEXT("shed"), TEXT("Shed") }, { TEXT("barn"), TEXT("Barn") }, { TEXT("porch"), TEXT("Porch / shelter") }, { TEXT("commercial"), TEXT("Commercial") } };
		return T;
	}
	double PolyArea(const TArray<FVector2D>& P)
	{
		double A = 0;
		for (int32 i = 0, j = P.Num() - 1; i < P.Num(); j = i++) A += P[j].X * P[i].Y - P[i].X * P[j].Y;
		return FMath::Abs(A / 2);
	}
	FVector2D Centroid(const TArray<FVector2D>& P)
	{
		double A = 0, X = 0, Y = 0;
		const int32 N = P.Num() - (P.Num() > 1 && P[0] == P.Last() ? 1 : 0);
		for (int32 i = 0; i < N; i++)
		{
			const FVector2D& P0 = P[i]; const FVector2D& P1 = P[(i + 1) % N];
			const double C = P0.X * P1.Y - P1.X * P0.Y;
			A += C; X += (P0.X + P1.X) * C; Y += (P0.Y + P1.Y) * C;
		}
		if (FMath::Abs(A) < 1e-6) { FVector2D S = FVector2D::ZeroVector; for (int32 i = 0; i < N; i++) S += P[i]; return S / FMath::Max(1, N); }
		return FVector2D(X / (3 * A), Y / (3 * A));
	}
	bool InPoly(const TArray<FVector2D>& P, const FVector2D& Q)
	{
		bool In = false;
		for (int32 i = 0, j = P.Num() - 1; i < P.Num(); j = i++)
			if ((P[i].Y > Q.Y) != (P[j].Y > Q.Y) && Q.X < (P[j].X - P[i].X) * (Q.Y - P[i].Y) / (P[j].Y - P[i].Y) + P[i].X) In = !In;
		return In;
	}
	TSharedRef<SWidget> Section(const FString& Title, TSharedRef<SWidget> Body)
	{
		return Box(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)[Text(Title, 15, Gold, "Bold")]
			+ SVerticalBox::Slot().AutoHeight()[Body], FMargin(12, 8), Solid);
	}
	TSharedRef<SWidget> Row(const FString& Label, TSharedRef<SWidget> W, float LabelW = 120)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(LabelW)[Text(Label, 12, Soft)]]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[W];
	}
	TSharedRef<SWidget> Note(const FString& S)
	{
		return SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(400).Text(FText::FromString(S));
	}
}
