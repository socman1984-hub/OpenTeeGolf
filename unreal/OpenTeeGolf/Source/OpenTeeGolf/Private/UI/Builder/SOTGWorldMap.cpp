// See SOTGWorldMap.h.

#include "UI/Builder/SOTGWorldMap.h"
#include "UI/Builder/SOTGCourseBuilder.h"

#include "UI/OTGUi.h"
#include "Course/OTGTextures.h"
#include "Course/OTGCourseSubsystem.h"
#include "App/OTGHelperSubsystem.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

using namespace otg::ui;

#include "UI/Builder/SOTGBuilderShared.h"

// ------------------------------------------------------------------ the map

void SOTGWorldMap::Construct(const FArguments&)
{
	SetClipping(EWidgetClipping::ClipToBounds);
}

FVector2D SOTGWorldMap::ToPx(double InLat, double InLon, double Z) const
{
	// (Web Mercator, as the map tiles are)
	const double N = 256.0 * FMath::Pow(2.0, Z);
	const double La = FMath::DegreesToRadians(FMath::Clamp(InLat, -85.05, 85.05));
	return FVector2D((InLon + 180.0) / 360.0 * N, (1.0 - FMath::Loge(FMath::Tan(La) + 1.0 / FMath::Cos(La)) / PI) / 2.0 * N);
}

void SOTGWorldMap::FromPx(const FVector2D& P, double Z, double& OutLat, double& OutLon) const
{
	const double N = 256.0 * FMath::Pow(2.0, Z);
	OutLon = P.X / N * 360.0 - 180.0;
	OutLat = FMath::RadiansToDegrees(FMath::Atan(FMath::Sinh(PI * (1.0 - 2.0 * P.Y / N))));
}

FVector2D SOTGWorldMap::ScreenOf(const FGeometry& G, double InLat, double InLon) const
{
	FVector2D D = ToPx(InLat, InLon, Zoom) - ToPx(Lat, Lon, Zoom);
	// (the nearer way round the world)
	const double W = 256.0 * FMath::Pow(2.0, Zoom);
	if (D.X > W / 2) D.X -= W; else if (D.X < -W / 2) D.X += W;
	return G.GetLocalSize() / 2 + D;
}

void SOTGWorldMap::FlyTo(double InLat, double InLon, double InZoom)
{
	Lat = InLat; Lon = InLon; Zoom = FMath::Clamp(InZoom, MinZoom, MaxZoom);
}

const FSlateBrush* SOTGWorldMap::Tile(int32 Z, int32 X, int32 Y) const
{
	const FString Key = FString::Printf(TEXT("%s/%d/%d/%d"), *Layer_, Z, X, Y);
	FTile& T = Tiles.FindOrAdd(Key);
	T.Used = FPlatformTime::Seconds();
	if (T.Brush.IsValid()) return T.Brush.Get();
	if (T.bAsked || T.bFailed || Asking >= MaxAsking) return nullptr;
	UOTGHelperSubsystem* H = HelperSub();
	if (!H || H->GetState() == EOTGHelperState::Failed) return nullptr;
	T.bAsked = true;
	Asking++;
	TWeakPtr<const SOTGWorldMap> Weak = SharedThis(this);
	H->Get(FString::Printf(TEXT("/api/helper/tile?layer=%s&z=%d&x=%d&y=%d"), *Layer_, Z, X, Y), [Weak, Key](int32 Code, TSharedPtr<FJsonValue> J, const FString&) {
		TSharedPtr<const SOTGWorldMap> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->Asking--;
		FTile* T = Self->Tiles.Find(Key);
		if (!T) return;
		FString File;
		UTexture2D* Tex = nullptr;
		if (Code == 200 && J.IsValid() && J->AsObject().IsValid() && J->AsObject()->TryGetStringField(TEXT("file"), File))
			Tex = otg::LoadTexture(File, otg::ETexKind::Photo, 256, GetTransientPackage());
		if (!Tex) { T->bFailed = true; return; }
		T->Tex.Reset(Tex);
		T->Brush = MakeShared<FSlateBrush>();
		T->Brush->SetResourceObject(Tex);
		T->Brush->ImageSize = FVector2D(256, 256);
		T->Brush->DrawAs = ESlateBrushDrawType::Image;
	});
	// (the least lately used dropped, past a few hundred)
	if (Tiles.Num() > MaxTiles)
	{
		TArray<FString> Keys;
		Tiles.GetKeys(Keys);
		Keys.Sort([this](const FString& A, const FString& B) { return Tiles[A].Used < Tiles[B].Used; });
		for (int32 i = 0; i < Keys.Num() - MaxTiles; i++) if (!Tiles[Keys[i]].bAsked || Tiles[Keys[i]].Brush.IsValid()) Tiles.Remove(Keys[i]);
	}
	return nullptr;
}

int32 SOTGWorldMap::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
	const FVector2D Size = G.GetLocalSize();
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(), White, ESlateDrawEffect::None, FLinearColor(0.08f, 0.1f, 0.12f, 1.f));
	// the tiles: the zoom level nearest, scaled to the zoom between
	const int32 Z = FMath::Clamp(FMath::FloorToInt32(Zoom + 0.4), 1, 18);
	const double Scale = FMath::Pow(2.0, Zoom - Z), Ts = 256.0 * Scale;
	const int32 N = 1 << Z;
	const FVector2D C = ToPx(Lat, Lon, Z) * Scale;
	const FVector2D Origin = Size / 2 - C;   // (where world pixel 0, 0 is on screen)
	const int32 X0 = FMath::FloorToInt32(-Origin.X / Ts), X1 = FMath::FloorToInt32((Size.X - Origin.X) / Ts);
	const int32 Y0 = FMath::Max(0, FMath::FloorToInt32(-Origin.Y / Ts)), Y1 = FMath::Min(N - 1, FMath::FloorToInt32((Size.Y - Origin.Y) / Ts));
	for (int32 Y = Y0; Y <= Y1; Y++)
		for (int32 X = X0; X <= X1; X++)
		{
			const int32 WX = ((X % N) + N) % N;
			const FVector2D P = Origin + FVector2D(X * Ts, Y * Ts);
			if (const FSlateBrush* B = Tile(Z, WX, Y))
				FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2D(Ts + 0.5, Ts + 0.5), FSlateLayoutTransform(P)), B);
		}
	// the courses: clusters and dots (Layout), the one picked bigger, with its name
	// (circles: a rounded box whose corners are half its side - larger, and Slate draws nonsense)
	static TMap<int32, TSharedPtr<FSlateRoundedBoxBrush>> Circles;
	auto Circle = [](double Side) -> const FSlateBrush* {
		const int32 K = FMath::Max(2, FMath::RoundToInt32(Side));
		TSharedPtr<FSlateRoundedBoxBrush>& B = Circles.FindOrAdd(K);
		if (!B.IsValid()) B = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White, K * 0.5f);
		return B.Get();
	};
	if (Courses)
	{
		Layout(G);
		const FLinearColor Blue(0.18f, 0.55f, 0.9f, 0.95f), Orange(1.f, 0.62f, 0.18f, 1.f), GreenC(0.18f, 0.68f, 0.31f, 1.f), Ring(1.f, 0.85f, 0.29f, 1.f);
		const FSlateFontInfo CountFont = Font(11, "Bold");
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		int32 L = Layer + 2;
		auto Disc = [&](const FVector2D& P, double R, const FLinearColor& Fill, const FLinearColor& Edge, double EdgeW) {
			const double Rr = FMath::RoundToDouble(R), In = Rr - EdgeW;
			FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(FVector2D(2 * Rr + 2, 2 * Rr + 2), FSlateLayoutTransform(FVector2D(FMath::RoundToDouble(P.X), FMath::RoundToDouble(P.Y)) - FVector2D(Rr + 1, Rr + 1))), Circle(2 * Rr + 2), ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.45f));
			FSlateDrawElement::MakeBox(Out, L + 1, G.ToPaintGeometry(FVector2D(2 * Rr, 2 * Rr), FSlateLayoutTransform(FVector2D(FMath::RoundToDouble(P.X), FMath::RoundToDouble(P.Y)) - FVector2D(Rr, Rr))), Circle(2 * Rr), ESlateDrawEffect::None, Edge);
			FSlateDrawElement::MakeBox(Out, L + 2, G.ToPaintGeometry(FVector2D(2 * In, 2 * In), FSlateLayoutTransform(FVector2D(FMath::RoundToDouble(P.X), FMath::RoundToDouble(P.Y)) - FVector2D(In, In))), Circle(2 * In), ESlateDrawEffect::None, Fill);
		};
		for (const FDot& D : Dots)
		{
			if (D.N > 1)
			{
				// (a cluster: how many in it, bigger for more)
				const double R = (D.N < 100 ? 30 : D.N < 1000 ? 38 : 46) / 2.0;
				Disc(D.P, R, FLinearColor(0.23f, 0.56f, 0.85f, 0.88f), FLinearColor(1, 1, 1, 0.9f), 2);
				const FString T = D.N < 1000 ? FString::FromInt(D.N) : FString::Printf(TEXT("%.1fk"), FMath::RoundToDouble(D.N / 100.0) / 10.0);
				const FVector2D TS = Measure->Measure(T, CountFont);
				FSlateDrawElement::MakeText(Out, L + 3, G.ToPaintGeometry(TS, FSlateLayoutTransform(D.P - TS / 2)), T, CountFont, ESlateDrawEffect::None, FLinearColor::White);
				continue;
			}
			const FOTGMapCourse& M = (*Courses)[D.Index];
			if (!M.BuiltId.IsEmpty()) Disc(D.P, 9, GreenC, M.bInGame ? Ring : FLinearColor::White, 2);
			else if (M.bFound) Disc(D.P, 7, Orange, FLinearColor::White, 2);
			else Disc(D.P, 7, Blue, FLinearColor::White, 2);
		}
		Layer = L + 4 - 4;
		if (Courses->IsValidIndex(Picked))
		{
			const FOTGMapCourse& M = (*Courses)[Picked];
			const FVector2D P = ScreenOf(G, M.Lat, M.Lon);
			FSlateDrawElement::MakeBox(Out, Layer + 6, G.ToPaintGeometry(FVector2D(22, 22), FSlateLayoutTransform(P - FVector2D(11, 11))), Circle(22), ESlateDrawEffect::None, FLinearColor(1, 1, 1, 0.95f));
			FSlateDrawElement::MakeBox(Out, Layer + 7, G.ToPaintGeometry(FVector2D(14, 14), FSlateLayoutTransform(P - FVector2D(7, 7))), Circle(14), ESlateDrawEffect::None, FLinearColor(0.9f, 0.2f, 0.15f, 1.f));
			const FSlateFontInfo F = Font(13, "Bold");
			const FVector2D T = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(M.Name, F) + FVector2D(12, 6);
			FSlateDrawElement::MakeBox(Out, Layer + 8, G.ToPaintGeometry(T, FSlateLayoutTransform(P + FVector2D(-T.X / 2, 14))), White, ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.75f));
			FSlateDrawElement::MakeText(Out, Layer + 9, G.ToPaintGeometry(T, FSlateLayoutTransform(P + FVector2D(-T.X / 2 + 6, 17))), M.Name, F, ESlateDrawEffect::None, FLinearColor::White);
		}
	}
	// whose map it is
	const FString Credit = Layer_ == TEXT("map") ? FString(TEXT("© OpenStreetMap contributors")) : FString(TEXT("Imagery © Esri, Maxar, Earthstar Geographics"));
	FSlateDrawElement::MakeBox(Out, Layer + 10, G.ToPaintGeometry(FVector2D(330, 20), FSlateLayoutTransform(FVector2D(Size.X - 330, Size.Y - 20))), White, ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.55f));
	FSlateDrawElement::MakeText(Out, Layer + 11, G.ToPaintGeometry(FVector2D(320, 18), FSlateLayoutTransform(FVector2D(Size.X - 324, Size.Y - 18))), Credit, Font(10), ESlateDrawEffect::None, FLinearColor(1, 1, 1, 0.85f));
	return Layer + 11;
}

void SOTGWorldMap::Layout(const FGeometry& G) const
{
	Dots.Reset();
	if (!Courses) return;
	const FVector2D Size = G.GetLocalSize();
	const bool bCluster = Zoom < 12;
	constexpr double Cell = 60;
	// (the clusters on a grid of the world's own pixels, so they keep still as the map moves)
	TMap<FIntPoint, int32> Cells;
	TArray<FDot> Top;   // (built and found: over the rest)
	for (int32 i = 0; i < Courses->Num(); i++)
	{
		const FOTGMapCourse& M = (*Courses)[i];
		// (the key's switches: a course shown if any of what it is is on)
		const bool bBuilt = !M.BuiltId.IsEmpty();
		if (!((bBuilt && bShowBuilt) || (M.bFound && bShowFound) || (!bBuilt && !M.bFound && bShowNotBuilt))) continue;
		const FVector2D P = ScreenOf(G, M.Lat, M.Lon);
		if (P.X < -Cell || P.Y < -Cell || P.X > Size.X + Cell || P.Y > Size.Y + Cell) continue;
		if (!M.BuiltId.IsEmpty() || M.bFound || !bCluster) { FDot D; D.P = P; D.Index = i; (M.BuiltId.IsEmpty() && !M.bFound ? Dots : Top).Add(D); continue; }
		const FVector2D W = ToPx(M.Lat, M.Lon, Zoom);
		const FIntPoint K(FMath::FloorToInt32(W.X / Cell), FMath::FloorToInt32(W.Y / Cell));
		if (const int32* C = Cells.Find(K)) { FDot& D = Dots[*C]; D.N++; D.Sum += P; }
		else { FDot D; D.P = P; D.Index = i; D.Sum = P; Cells.Add(K, Dots.Add(D)); }
	}
	for (FDot& D : Dots) if (D.N > 1) D.P = D.Sum / D.N;
	// (the built ones over the found ones)
	Top.StableSort([this](const FDot& A, const FDot& B) { return (*Courses)[A.Index].BuiltId.IsEmpty() && !(*Courses)[B.Index].BuiltId.IsEmpty(); });
	Dots.Append(Top);
}

int32 SOTGWorldMap::HitCourse(const FGeometry& G, const FVector2D& L) const
{
	// (the top one under the mouse: a cluster's index is -2 - its place in Dots)
	for (int32 k = Dots.Num() - 1; k >= 0; k--)
	{
		const FDot& D = Dots[k];
		const double R = D.N > 1 ? (D.N < 100 ? 15 : D.N < 1000 ? 19 : 23) : 11;
		if (FVector2D::DistSquared(D.P, L) <= R * R) return D.N > 1 ? -2 - k : D.Index;
	}
	return INDEX_NONE;
}

FReply SOTGWorldMap::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (E.GetEffectingButton() != EKeys::LeftMouseButton && E.GetEffectingButton() != EKeys::RightMouseButton) return FReply::Unhandled();
	bDragging = true;
	bMoved = false;
	DragFrom = DragLast = G.AbsoluteToLocal(E.GetScreenSpacePosition());
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SOTGWorldMap::OnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	if (!bDragging) return FReply::Unhandled();
	const FVector2D L = G.AbsoluteToLocal(E.GetScreenSpacePosition());
	if (FVector2D::Distance(L, DragFrom) > 4) bMoved = true;
	// (the map follows the mouse)
	const FVector2D C = ToPx(Lat, Lon, Zoom) - (L - DragLast);
	FromPx(C, Zoom, Lat, Lon);
	Lon = FMath::Fmod(Lon + 540.0, 360.0) - 180.0;
	DragLast = L;
	return FReply::Handled();
}

FReply SOTGWorldMap::OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E)
{
	if (!bDragging) return FReply::Unhandled();
	bDragging = false;
	if (!bMoved && E.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const int32 Hit = HitCourse(G, G.AbsoluteToLocal(E.GetScreenSpacePosition()));
		if (Hit <= -2 && Dots.IsValidIndex(-2 - Hit))
		{
			// (a cluster: closer in, round it, until its courses come apart)
			const FDot& D = Dots[-2 - Hit];
			FromPx(ToPx(Lat, Lon, Zoom) + (D.P - G.GetLocalSize() / 2), Zoom, Lat, Lon);
			Zoom = FMath::Clamp(Zoom + 2, MinZoom, MaxZoom);
		}
		else if (Hit >= 0 && OnPick) OnPick(Hit);
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SOTGWorldMap::OnMouseWheel(const FGeometry& G, const FPointerEvent& E)
{
	// (zoomed round the point under the mouse)
	const FVector2D L = G.AbsoluteToLocal(E.GetScreenSpacePosition());
	const FVector2D Off = L - G.GetLocalSize() / 2;
	double MLat, MLon;
	FromPx(ToPx(Lat, Lon, Zoom) + Off, Zoom, MLat, MLon);
	Zoom = FMath::Clamp(Zoom + E.GetWheelDelta() * 0.5, MinZoom, MaxZoom);
	FromPx(ToPx(MLat, MLon, Zoom) - Off, Zoom, Lat, Lon);
	return FReply::Handled();
}

FCursorReply SOTGWorldMap::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return FCursorReply::Cursor(bDragging && bMoved ? EMouseCursor::GrabHandClosed : EMouseCursor::GrabHand);
}
