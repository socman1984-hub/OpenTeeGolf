// See SOTGBuilder.h.

#include "SOTGBuilder.h"

#include "OTGUi.h"
#include "OTGTextures.h"
#include "OTGCourseSubsystem.h"
#include "Game/OTGHelperSubsystem.h"
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

namespace
{
	UGameInstance* GI()
	{
		return GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetGameInstance() : nullptr;
	}
	UOTGHelperSubsystem* HelperSub() { UGameInstance* G = GI(); return G ? G->GetSubsystem<UOTGHelperSubsystem>() : nullptr; }
	constexpr double MinZoom = 2, MaxZoom = 18;
	constexpr int32 MaxTiles = 400, MaxAsking = 8;
	FString OsmTypeName(const FString& T) { return T == TEXT("n") ? FString(TEXT("node")) : T == TEXT("w") ? FString(TEXT("way")) : T == TEXT("r") ? FString(TEXT("relation")) : T; }
}

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

// ------------------------------------------------------------------ the builder

UOTGHelperSubsystem* SOTGCourseBuilder::Helper() const { return HelperSub(); }

void SOTGCourseBuilder::Construct(const FArguments&)
{
	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 14, 0)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[SAssignNew(Map, SOTGWorldMap)]
			// the key, and how far the world's courses are gathered (builder.js map-note)
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(10, 10, 10, 30)
			[
				Box(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(14).HeightOverride(14)[SNew(SBorder).BorderImage(new FSlateRoundedBoxBrush(FLinearColor(0.18f, 0.68f, 0.31f, 1.f), 7.f, FLinearColor(1.f, 0.85f, 0.29f, 1.f), 2.f))]]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5, 0, 14, 0)[Text(TEXT("Built"), 12)]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(12).HeightOverride(12)[SNew(SBorder).BorderImage(new FSlateRoundedBoxBrush(FLinearColor(0.18f, 0.55f, 0.9f, 1.f), 6.f, FLinearColor::White, 2.f))]]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5, 0, 14, 0)[Text(TEXT("Not built yet"), 12)]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(12).HeightOverride(12)[SNew(SBorder).BorderImage(new FSlateRoundedBoxBrush(FLinearColor(1.f, 0.62f, 0.18f, 1.f), 6.f, FLinearColor::White, 2.f))]]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5, 0, 14, 0)[Text(TEXT("Search results"), 12)]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(16).HeightOverride(16)[SNew(SBorder).BorderImage(new FSlateRoundedBoxBrush(FLinearColor(0.23f, 0.56f, 0.85f, 0.9f), 8.f, FLinearColor::White, 2.f))]]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5, 0, 0, 0)[Text(TEXT("Several: click to zoom in"), 12)]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
					[
						SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity_Lambda([this]() { return bWorldDone ? FSlateColor(Soft) : FSlateColor(Gold); }).Text_Lambda([this]() {
							int32 Built = 0;
							for (const FOTGMapCourse& M : Courses) Built += M.BuiltId.IsEmpty() ? 0 : 1;
							if (bWorldDone) return FText::FromString(FString::Printf(TEXT("%s golf courses (%d built) - click one to build it, or search"), *FText::AsNumber(Courses.Num()).ToString(), Built));
							// (from the list that comes with the game, checked for courses mapped since)
							if (WorldSeeded > 0)
								return FText::FromString(FString::Printf(TEXT("%s golf courses (%d built) - checking OpenStreetMap for new ones (%d%%)"),
									*FText::AsNumber(Courses.Num()).ToString(), Built, FMath::RoundToInt(WorldProgress * 100)));
							return FText::FromString(FString::Printf(TEXT("Gathering every golf course in OpenStreetMap: %s so far (%d%% of the world; the first time takes a few minutes)"),
								*FText::AsNumber(Courses.Num()).ToString(), FMath::RoundToInt(WorldProgress * 100)));
						})
					], FMargin(10, 6), FLinearColor(0.02f, 0.03f, 0.02f, 0.78f))
			]
			// Map / Aerial photos
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(10)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)[Button(TEXT("Map"), [this]() { Map->SetLayer(TEXT("map")); }, 12)]
				+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Aerial photos"), [this]() { Map->SetLayer(TEXT("photo")); }, 12)]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SAssignNew(Side, SBox).WidthOverride(440)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					Box(SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[Text(TEXT("Find a course"), 15, Gold, "Bold")]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1)
							[
								SAssignNew(SearchBox, SEditableTextBox).Font(Font(14)).HintText(FText::FromString(TEXT("Course name or town")))
								.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type T) { if (T == ETextCommit::OnEnter) Search(); })
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)[Button(TEXT("Search"), [this]() { Search(); }, 13)]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
						[
							SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(400).Text_Lambda([this]() {
								return FText::FromString(!Status.IsEmpty() ? Status
									: bWorldDone ? FString::Printf(TEXT("%d golf courses on the map"), Courses.Num())
									: FString::Printf(TEXT("Finding the world's golf courses: %d so far (%d%%)"), Courses.Num(), FMath::RoundToInt(WorldProgress * 100)));
							})
						], FMargin(12, 10), Solid)
				]
				+ SVerticalBox::Slot().FillHeight(1)[SAssignNew(Lower, SBox)]
			]
		]
	];
	Map->SetCourses(&Courses);
	Map->OnPick = [this](int32 I) { Pick(I, false); };
	if (UOTGHelperSubsystem* H = Helper())
	{
		JobHandle = H->OnJobDone.AddLambda([this](const FOTGHelperJob& J) { JobDone(J); });
		FString Why;
		if (!H->Available(&Why)) Status = Why;
	}
	LoadBuilt();
	Rebuild();
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SOTGCourseBuilder::Pulse));
	if (FParse::Value(FCommandLine::Get(), TEXT("buildertest="), TestQuery, false) && !TestQuery.IsEmpty())
	{
		TestQuery.ReplaceInline(TEXT("+"), TEXT(" "));
		bTestBuild = FParse::Param(FCommandLine::Get(), TEXT("buildertestbuild"));
		TestStep = 1;
		TestAt = FPlatformTime::Seconds() + 3;
	}
}

EActiveTimerReturnType SOTGCourseBuilder::Pulse(double Time, float Dt)
{
	// the world's courses: asked for a page at a time while the helper gathers them
	if (!bWorldDone && !bWorldAsking && FPlatformTime::Seconds() >= NextWorld) LoadWorld();
	// (the test: one step at a time)
	if (TestStep > 0 && FPlatformTime::Seconds() >= TestAt)
	{
		UOTGHelperSubsystem* H = Helper();
		auto Shot = [](const TCHAR* Name) { if (GEngine) GEngine->Exec(nullptr, *FString::Printf(TEXT("OTG.Shot %s"), Name)); };
		auto Quit = []() { FPlatformMisc::RequestExit(false, TEXT("buildertest")); };
		// (-buildertest=world: the map zoomed out, then over Ohio, no search)
		if (TestStep == 1 && TestQuery == TEXT("world")) { Map->FlyTo(38, -40, 2.6); TestStep = 20; TestAt = FPlatformTime::Seconds() + 25; }
		// (each shot a frame before the map moves on: a screenshot is taken at the end of its frame)
		else if (TestStep == 20) { Shot(TEXT("builder_world")); TestStep = 21; TestAt = FPlatformTime::Seconds() + 1; }
		else if (TestStep == 21) { Map->FlyTo(40, -84, 6.5); TestStep = 22; TestAt = FPlatformTime::Seconds() + 10; }
		else if (TestStep == 22) { Shot(TEXT("builder_ohio")); TestStep = 23; TestAt = FPlatformTime::Seconds() + 1; }
		else if (TestStep == 23) { Map->FlyTo(39.33, -84.43, 10); TestStep = 24; TestAt = FPlatformTime::Seconds() + 10; }
		else if (TestStep == 24) { Shot(TEXT("builder_near")); TestStep = 9; TestAt = FPlatformTime::Seconds() + 3; }
		else if (TestStep == 1) { SearchBox->SetText(FText::FromString(TestQuery)); Search(); TestStep = 2; TestAt = FPlatformTime::Seconds() + 1; }
		else if (TestStep == 2 && Found.Num()) { Pick(Found[0], true); TestStep = 3; TestAt = FPlatformTime::Seconds() + 25; }
		else if (TestStep == 2 && !Status.StartsWith(TEXT("Searching")) && FPlatformTime::Seconds() > TestAt + 60) { UE_LOG(LogTemp, Warning, TEXT("buildertest: nothing found")); Quit(); TestStep = 0; }
		else if (TestStep == 3 && !bChecking) { Shot(TEXT("builder_picked")); TestStep = bTestBuild ? 4 : 9; TestAt = FPlatformTime::Seconds() + 3; }
		else if (TestStep == 4)
		{
			if (Courses.IsValidIndex(Picked) && !Courses[Picked].BuiltId.IsEmpty()) { UE_LOG(LogTemp, Display, TEXT("buildertest: built already (%s): exporting"), *Courses[Picked].BuiltId); if (H) H->Export(Courses[Picked].BuiltId); }
			else StartBuild();
			// (-buildertestexport=<id>: another course queued with it, put in the game meanwhile)
			FString Also;
			if (H && FParse::Value(FCommandLine::Get(), TEXT("buildertestexport="), Also)) H->Export(Also);
			TestStep = 5; TestAt = FPlatformTime::Seconds() + 30;
		}
		else if (TestStep == 5) { Shot(TEXT("builder_building")); TestStep = 6; TestAt = FPlatformTime::Seconds() + 5; }
		else if (TestStep == 6 && H && H->GetQueue().Num() && !H->ActiveCount())
		{
			UE_LOG(LogTemp, Display, TEXT("buildertest: %s %s %s (ready: %s)"), *H->GetJob().Kind, *H->GetJob().State, *H->GetJob().Message, *ReadyId);
			Shot(TEXT("builder_done"));
			TestStep = 9; TestAt = FPlatformTime::Seconds() + 4;
		}
		else if (TestStep == 6) TestAt = FPlatformTime::Seconds() + 5;
		else if (TestStep == 9) { TestStep = 0; Quit(); }
	}
	// the queue: its card kept up to date
	if (UOTGHelperSubsystem* H = Helper())
	{
		FString Now = H->GetJob().State + H->GetJob().Message;
		for (const FOTGHelperJob& J : H->GetQueue()) Now += J.QueueId + J.Kind + J.State + J.Message + FString::FromInt(FMath::RoundToInt(J.Frac * 100));
		if (Now != JobShown) { JobShown = Now; Rebuild(); }
	}
	return EActiveTimerReturnType::Continue;
}

void SOTGCourseBuilder::LoadWorld()
{
	UOTGHelperSubsystem* H = Helper();
	if (!H || !H->Available()) { bWorldDone = true; return; }
	bWorldAsking = true;
	TWeakPtr<SOTGCourseBuilder> Weak = SharedThis(this);
	H->World(WorldFrom, [Weak](int32 Code, TSharedPtr<FJsonValue> J, const FString& Body) {
		TSharedPtr<SOTGCourseBuilder> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->bWorldAsking = false;
		Self->NextWorld = FPlatformTime::Seconds() + 2.0;
		const TSharedPtr<FJsonObject> O = J.IsValid() ? J->AsObject() : nullptr;
		if (Code != 200 || !O.IsValid()) { Self->NextWorld = FPlatformTime::Seconds() + 10.0; return; }
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (O->TryGetArrayField(TEXT("courses"), Rows))
			for (const TSharedPtr<FJsonValue>& V : *Rows)
			{
				// [lat, lon, n|w|r, id, name]
				const TArray<TSharedPtr<FJsonValue>>& R = V->AsArray();
				if (R.Num() < 5) continue;
				FOTGMapCourse M;
				M.Lat = R[0]->AsNumber(); M.Lon = R[1]->AsNumber(); M.OsmType = OsmTypeName(R[2]->AsString()); M.OsmId = int64(R[3]->AsNumber()); M.Name = R[4]->AsString();
				if (M.Name.IsEmpty()) M.Name = TEXT("Golf course");
				if (Self->ById.Contains(M.OsmId)) continue;
				for (const FBuilt& B : Self->Built) if (B.OsmId == M.OsmId) { M.BuiltId = B.Id; M.bInGame = B.bInGame; }
				Self->ById.Add(M.OsmId, Self->Courses.Add(M));
			}
		Self->WorldFrom += Rows ? Rows->Num() : 0;
		Self->bWorldDone = O->GetBoolField(TEXT("done"));
		Self->WorldProgress = O->GetNumberField(TEXT("progress"));
		O->TryGetNumberField(TEXT("seeded"), Self->WorldSeeded);
	});
}

void SOTGCourseBuilder::LoadBuilt()
{
	UOTGHelperSubsystem* H = Helper();
	if (!H || !H->Available()) return;
	TWeakPtr<SOTGCourseBuilder> Weak = SharedThis(this);
	H->Get(TEXT("/api/courses"), [Weak](int32 Code, TSharedPtr<FJsonValue> J, const FString&) {
		TSharedPtr<SOTGCourseBuilder> Self = Weak.Pin();
		const TSharedPtr<FJsonObject> O = J.IsValid() ? J->AsObject() : nullptr;
		if (!Self.IsValid() || Code != 200 || !O.IsValid()) return;
		// (the ones the game has: exported to its courses folder)
		TSet<FString> InGame;
		if (UGameInstance* G = GI()) if (UOTGCourseSubsystem* C = G->GetSubsystem<UOTGCourseSubsystem>()) for (const FOTGCourseInfo& I : C->ListCourses()) if (!I.ExportDir.IsEmpty()) InGame.Add(I.Id);
		Self->Built.Reset();
		for (const TSharedPtr<FJsonValue>& V : O->GetArrayField(TEXT("courses")))
		{
			const TSharedPtr<FJsonObject> C = V->AsObject();
			if (!C.IsValid()) continue;
			FBuilt B;
			B.Id = C->GetStringField(TEXT("id"));
			B.Name = C->GetStringField(TEXT("name"));
			C->TryGetStringField(TEXT("location"), B.Location);
			double Id = 0;
			if (C->TryGetNumberField(TEXT("osmId"), Id)) B.OsmId = int64(Id);
			B.bInGame = InGame.Contains(B.Id);
			Self->Built.Add(B);
			double La = 0, Lo = 0;
			C->TryGetNumberField(TEXT("lat"), La); C->TryGetNumberField(TEXT("lon"), Lo);
			// (its dot: the world's list's, or one of its own until that list brings it)
			int32 Idx = INDEX_NONE;
			if (const int32* Have = B.OsmId ? Self->ById.Find(B.OsmId) : nullptr) Idx = *Have;
			else if (La || Lo)
			{
				FOTGMapCourse M;
				M.Lat = La; M.Lon = Lo; M.Name = B.Name; M.Location = B.Location; M.OsmId = B.OsmId;
				FString T;
				M.OsmType = C->TryGetStringField(TEXT("osmType"), T) ? T : FString(TEXT("way"));
				Idx = Self->Courses.Add(M);
				if (B.OsmId) Self->ById.Add(B.OsmId, Idx);
			}
			if (Self->Courses.IsValidIndex(Idx)) { Self->Courses[Idx].BuiltId = B.Id; Self->Courses[Idx].bInGame = B.bInGame; }
		}
		Self->Rebuild();
	});
}

void SOTGCourseBuilder::Search()
{
	UOTGHelperSubsystem* H = Helper();
	const FString Q = SearchBox.IsValid() ? SearchBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (!H || Q.Len() < 3) return;
	Status = TEXT("Searching OpenStreetMap…");
	Rebuild();
	TWeakPtr<SOTGCourseBuilder> Weak = SharedThis(this);
	H->Search(Q, [Weak](int32 Code, TSharedPtr<FJsonValue> J, const FString& Body) {
		TSharedPtr<SOTGCourseBuilder> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		const TSharedPtr<FJsonObject> O = J.IsValid() ? J->AsObject() : nullptr;
		FString Err;
		if (Code != 200 || !O.IsValid()) { Self->Status = O.IsValid() && O->TryGetStringField(TEXT("error"), Err) ? Err : FString(TEXT("The search didn't work: try again in a minute")); Self->Rebuild(); return; }
		for (int32 F : Self->Found) if (Self->Courses.IsValidIndex(F)) Self->Courses[F].bFound = false;
		Self->Found.Reset();
		for (const TSharedPtr<FJsonValue>& V : O->GetArrayField(TEXT("results")))
		{
			const TSharedPtr<FJsonObject> R = V->AsObject();
			if (!R.IsValid()) continue;
			const int64 Id = int64(R->GetNumberField(TEXT("osmId")));
			int32 I;
			if (const int32* Have = Self->ById.Find(Id)) I = *Have;
			else
			{
				FOTGMapCourse M;
				M.Lat = R->GetNumberField(TEXT("lat")); M.Lon = R->GetNumberField(TEXT("lon"));
				M.OsmType = R->GetStringField(TEXT("osmType")); M.OsmId = Id;
				I = Self->Courses.Add(M);
				Self->ById.Add(Id, I);
			}
			FOTGMapCourse& M = Self->Courses[I];
			M.Name = R->GetStringField(TEXT("name"));
			R->TryGetStringField(TEXT("location"), M.Location);
			FString B;
			if (R->TryGetStringField(TEXT("built"), B)) M.BuiltId = B;
			M.bFound = true;
			Self->Found.Add(I);
		}
		Self->Status = Self->Found.Num() ? FString::Printf(TEXT("%d found"), Self->Found.Num()) : FString(TEXT("No golf course by that name on OpenStreetMap"));
		if (Self->Found.Num() == 1) Self->Pick(Self->Found[0], true);
		else Self->Rebuild();
	});
}

void SOTGCourseBuilder::Pick(int32 I, bool bFly)
{
	if (!Courses.IsValidIndex(I)) return;
	Picked = I;
	Map->SetPicked(I);
	Checked.Reset();
	bCheckFailed = false;
	Group.Empty();
	ReadyId.Empty();
	Status.Empty();
	FOTGMapCourse& M = Courses[I];
	NameDraft = M.Name;
	if (bFly) Map->FlyTo(M.Lat, M.Lon, 15);
	// (where it is, when the world list didn't say)
	if (M.Location.IsEmpty())
		if (UOTGHelperSubsystem* H = Helper())
		{
			TWeakPtr<SOTGCourseBuilder> Weak = SharedThis(this);
			H->Where(M.Lat, M.Lon, [Weak, I](int32 Code, TSharedPtr<FJsonValue> J, const FString&) {
				TSharedPtr<SOTGCourseBuilder> Self = Weak.Pin();
				FString L;
				if (Self.IsValid() && Code == 200 && J.IsValid() && J->AsObject().IsValid() && J->AsObject()->TryGetStringField(TEXT("location"), L) && Self->Courses.IsValidIndex(I))
				{
					Self->Courses[I].Location = L;
					Self->Rebuild();
				}
			});
		}
	Rebuild();
	if (M.BuiltId.IsEmpty()) Check();
}

void SOTGCourseBuilder::Check()
{
	UOTGHelperSubsystem* H = Helper();
	if (!H || !Courses.IsValidIndex(Picked)) return;
	bChecking = true;
	bCheckFailed = false;
	const int32 I = Picked;
	TWeakPtr<SOTGCourseBuilder> Weak = SharedThis(this);
	H->Check(Courses[I].OsmType, Courses[I].OsmId, [Weak, I](int32 Code, TSharedPtr<FJsonValue> J, const FString& Body) {
		TSharedPtr<SOTGCourseBuilder> Self = Weak.Pin();
		if (!Self.IsValid() || Self->Picked != I) return;
		Self->bChecking = false;
		const TSharedPtr<FJsonObject> O = J.IsValid() ? J->AsObject() : nullptr;
		FString Err;
		if (Code != 200 || !O.IsValid())
		{
			Self->bCheckFailed = true;
			Self->Status = O.IsValid() && O->TryGetStringField(TEXT("error"), Err) ? Err : FString(TEXT("OpenStreetMap didn't answer (it's busy at times)"));
			Self->Rebuild();
			return;
		}
		Self->Checked = O;
		const FString Mapped = O->GetStringField(TEXT("name"));
		Self->NameDraft = Mapped.IsEmpty() || Mapped == TEXT("Unnamed course") ? Self->Courses[I].Name : Mapped;
		const TArray<TSharedPtr<FJsonValue>>* G = nullptr;
		if (O->TryGetArrayField(TEXT("groups"), G) && G->Num()) Self->Group = (*G)[0]->AsObject()->GetStringField(TEXT("name"));
		Self->Rebuild();
	});
	Rebuild();
}

void SOTGCourseBuilder::StartBuild()
{
	UOTGHelperSubsystem* H = Helper();
	if (!H || !Courses.IsValidIndex(Picked) || H->ActiveFor(FString(), Courses[Picked].OsmId)) return;
	const FOTGMapCourse& M = Courses[Picked];
	const FString Name = NameDraft.TrimStartAndEnd();
	H->Build(M.OsmType, M.OsmId, Name.IsEmpty() ? M.Name : Name, M.Location, Group);
	Rebuild();
}

void SOTGCourseBuilder::JobDone(const FOTGHelperJob& J)
{
	// (the queue builds, then puts it in the game, itself: here the lists catch up)
	LoadBuilt();
	if (J.Kind == TEXT("export") && J.State == TEXT("done")) ReadyId = J.CourseId;
	Rebuild();
}

TSharedRef<SWidget> SOTGCourseBuilder::JobCard()
{
	UOTGHelperSubsystem* H = Helper();
	if (!H) return SNullWidget::NullWidget;
	const TArray<FOTGHelperJob>& Q = H->GetQueue();
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	// (one that couldn't even be added: why)
	const FOTGHelperJob& Last = H->GetJob();
	if (!Q.Num())
	{
		if (Last.QueueId.IsEmpty() && Last.State == TEXT("failed"))
			return Box(SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Warn).WrapTextAt(400).Text(FText::FromString(TEXT("It couldn't be added: ") + Last.Message)), FMargin(12, 10), Solid);
		return SNullWidget::NullWidget;
	}
	const int32 Active = H->ActiveCount();
	V->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(Active ? FString::Printf(TEXT("Being built (%d)"), Active) : FString(TEXT("Built lately")), 15, Gold, "Bold")]
		+ SHorizontalBox::Slot().AutoWidth()[Active < Q.Num() ? StaticCastSharedRef<SWidget>(Button(TEXT("Clear finished"), [this]() { if (UOTGHelperSubsystem* H2 = Helper()) H2->ClearFinished(); Rebuild(); }, 11)) : StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)]
	];
	// (going and waiting ones in their order, then the finished, newest first)
	TArray<int32> Order;
	for (int32 i = 0; i < Q.Num(); i++) if (Q[i].IsRunning()) Order.Add(i);
	for (int32 i = 0; i < Q.Num(); i++) if (Q[i].IsWaiting()) Order.Add(i);
	for (int32 i = Q.Num() - 1, n = 0; i >= 0 && n < 6; i--) if (!Q[i].IsActive()) { Order.Add(i); n++; }
	for (int32 i : Order)
	{
		const FOTGHelperJob& J = Q[i];
		const FString QId = J.QueueId, CourseId = J.CourseId;
		const bool bRun = J.IsRunning(), bFailed = J.State == TEXT("failed"), bDone = J.State == TEXT("done");
		const FString Step = J.Kind == TEXT("export") ? FString(TEXT("getting it ready for the game")) : J.Kind == TEXT("update") ? FString(TEXT("updating from OpenStreetMap")) : FString(TEXT("building"));
		TSharedRef<SVerticalBox> R = SNew(SVerticalBox);
		// its name, the step and a clock
		R->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Font(Font(13, "Bold")).ColorAndOpacity(bFailed ? Warn : bDone ? Green : Ink).WrapTextAt(330)
				.Text(FText::FromString(J.Name + (bRun ? TEXT(": ") + Step : bDone && !J.bNeedsReview ? FString(TEXT(": in the game")) : bFailed ? FString(TEXT(": it didn't work")) : FString())))]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Soft).Text_Lambda([this, QId]() {
				const UOTGHelperSubsystem* H2 = Helper();
				const FOTGHelperJob* Jn = H2 ? H2->GetQueue().FindByPredicate([&](const FOTGHelperJob& X) { return X.QueueId == QId; }) : nullptr;
				if (!Jn || !Jn->StartedAt) return FText::GetEmpty();
				const int32 S = FMath::Max(0, FMath::RoundToInt32((Jn->IsActive() ? FPlatformTime::Seconds() : Jn->EndedAt) - Jn->StartedAt));
				return FText::FromString(FString::Printf(TEXT("%d:%02d"), S / 60, S % 60));
			})]
		];
		if (J.Kind == TEXT("export") && bRun)
		{
			R->AddSlot().AutoHeight().Padding(0, 4, 0, 2)
			[
				SNew(SBox).HeightOverride(6)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.15f))]
					+ SOverlay::Slot().HAlign(HAlign_Left)[SNew(SBox).WidthOverride(400.f * FMath::Clamp(J.Frac, 0.f, 1.f))[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Green)]]
				]
			];
		}
		if (!J.Message.IsEmpty() && !(bDone && J.Message == TEXT("In the game")))
			R->AddSlot().AutoHeight().Padding(0, 2, 0, 0)[SNew(STextBlock).Font(Font(11, bRun ? "Bold" : "Regular")).ColorAndOpacity(bFailed ? Warn : bRun ? Ink : Soft).WrapTextAt(400).Text(FText::FromString(J.Message))];
		// (a running one: what it did just before)
		if (bRun)
		{
			const int32 From = FMath::Max(0, J.Log.Num() - 4);
			for (int32 k = J.Log.Num() - 2; k >= From; k--)
				if (!J.Log[k].IsEmpty() && J.Log[k] != J.Message)
					R->AddSlot().AutoHeight().Padding(0, 1, 0, 0)[SNew(STextBlock).Font(Font(10)).ColorAndOpacity(FLinearColor(0.8f, 0.84f, 0.78f, 0.7f)).WrapTextAt(400).Text(FText::FromString(J.Log[k]))];
			if (J.Message.Contains(TEXT("OpenStreetMap")))
				R->AddSlot().AutoHeight().Padding(0, 2, 0, 0)[SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Soft).WrapTextAt(400)
					.Text(FText::FromString(TEXT("OpenStreetMap's public servers are busy at times: it tries the next one, then the main OpenStreetMap site.")))];
		}
		// what can be done with it
		TSharedRef<SHorizontalBox> B = SNew(SHorizontalBox);
		auto Add = [&](const TCHAR* L, TFunction<void()> F, bool bMain = false) { B->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[Button(L, MoveTemp(F), 11, bMain)]; };
		if (bDone && J.bNeedsReview && !CourseId.IsEmpty()) Add(TEXT("Look over its holes"), [this, CourseId]() { if (OnEdit) OnEdit(CourseId); }, true);
		else if (bDone && !CourseId.IsEmpty()) Add(TEXT("Play it"), [this, CourseId]() { if (OnReady) OnReady(CourseId); }, true);
		if (bFailed) Add(TEXT("Try again"), [this, QId]() { if (UOTGHelperSubsystem* H2 = Helper()) H2->Retry(QId); Rebuild(); }, true);
		if (J.IsWaiting() || bFailed) Add(TEXT("Remove"), [this, QId]() { if (UOTGHelperSubsystem* H2 = Helper()) H2->Remove(QId); Rebuild(); });
		if (bRun) Add(J.Kind == TEXT("export") ? TEXT("Stop") : TEXT("Stop after this step"), [this, QId]() { if (UOTGHelperSubsystem* H2 = Helper()) H2->Remove(QId); Rebuild(); });
		R->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[B];
		V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[R];
	}
	if (Active)
		V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(400)
			.Text(FText::FromString(TEXT("Go and play meanwhile: they carry on in the background (two built at a time, one got ready for the game at a time), you're told in the round when one is ready, and what's left carries on next time if you close the game.")))];
	return Box(V, FMargin(12, 10), Solid);
}

TSharedRef<SWidget> SOTGCourseBuilder::PickedCard()
{
	if (!Courses.IsValidIndex(Picked))
		return Box(SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Soft).WrapTextAt(400)
			.Text(FText::FromString(TEXT("Find a course: search for it, or drag and zoom the map (the mouse wheel) and click its dot. Every dot is a golf course mapped in OpenStreetMap; gold ones you've built already."))), FMargin(12, 10), Solid);
	const FOTGMapCourse& M = Courses[Picked];
	UOTGHelperSubsystem* H = Helper();
	const FOTGHelperJob* Queued = H ? H->ActiveFor(M.BuiltId, M.OsmId) : nullptr;
	const bool bBusy = Queued != nullptr;
	const bool bLine = H && H->ActiveCount() > 0;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight()[SNew(STextBlock).Font(Font(18, "Bold")).ColorAndOpacity(Ink).WrapTextAt(400).Text(FText::FromString(M.Name))];
	if (!M.Location.IsEmpty()) V->AddSlot().AutoHeight()[SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Soft).WrapTextAt(400).Text(FText::FromString(M.Location))];
	if (!M.BuiltId.IsEmpty())
	{
		const FBuilt* B = Built.FindByPredicate([&](const FBuilt& X) { return X.Id == M.BuiltId; });
		V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[Text(B && B->bInGame ? TEXT("Built, and in the game.") : TEXT("Built (not in the game yet)."), 13, Green)];
		TSharedRef<SHorizontalBox> Btns = SNew(SHorizontalBox);
		const FString Id = M.BuiltId;
		if (B && B->bInGame) Btns->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Play it"), [this, Id]() { if (OnReady) OnReady(Id); }, 13, true)];
		if (!bBusy) Btns->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Edit"), [this, Id]() { if (OnEdit) OnEdit(Id); }, 13)];
		if (!bBusy) Btns->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[Button(B && B->bInGame ? TEXT("Export again") : TEXT("Put it in the game"), [this, Id]() { if (UOTGHelperSubsystem* H2 = Helper()) H2->Export(Id); Rebuild(); }, 13, !(B && B->bInGame))];
		V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Btns];
		if (Queued) V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Text(TEXT("In the queue: ") + Queued->Message, 12, Soft)];
		return Box(V, FMargin(12, 10), Solid);
	}
	if (bChecking) V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[Text(TEXT("Looking at what's mapped there…"), 13, Soft)];
	if (bCheckFailed && !Checked.IsValid())
	{
		// (OpenStreetMap busy: again, or build without seeing what's mapped)
		V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Warn).WrapTextAt(400).Text(FText::FromString(TEXT("Couldn't see what's mapped there: ") + Status))];
		V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Check again"), [this]() { Status.Empty(); Check(); }, 13)]
			+ SHorizontalBox::Slot().AutoWidth()[bBusy ? StaticCastSharedRef<SWidget>(SNullWidget::NullWidget) : StaticCastSharedRef<SWidget>(Button(TEXT("Build anyway"), [this]() { StartBuild(); }, 13, true))]
		];
	}
	if (Checked.IsValid())
	{
		auto N = [this](const TCHAR* K) { return int32(Checked->GetNumberField(K)); };
		V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Ink).WrapTextAt(400).Text(FText::FromString(FString::Printf(
			TEXT("Mapped: %d holes, %d greens, %d fairways, %d tees, %d bunkers, %d water, %d trees%s"), N(TEXT("holes")), N(TEXT("greens")), N(TEXT("fairways")), N(TEXT("tees")), N(TEXT("bunkers")), N(TEXT("water")), N(TEXT("trees")),
			Checked->GetBoolField(TEXT("hasBoundary")) ? TEXT(", the boundary") : TEXT(""))))];
		const FString Q = Checked->GetStringField(TEXT("quality"));
		for (const TSharedPtr<FJsonValue>& Note : Checked->GetArrayField(TEXT("notes")))
			V->AddSlot().AutoHeight().Padding(0, 3, 0, 0)[SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Q == TEXT("good") ? Green : Q == TEXT("partial") ? Gold : Warn).WrapTextAt(400).Text(FText::FromString(Note->AsString()))];
		// (a club with more than one set of holes: which to build)
		const TArray<TSharedPtr<FJsonValue>>& Groups = Checked->GetArrayField(TEXT("groups"));
		if (Groups.Num() > 1)
		{
			V->AddSlot().AutoHeight().Padding(0, 8, 0, 2)[Text(TEXT("Which holes"), 13, Soft)];
			TSharedRef<SVerticalBox> GB = SNew(SVerticalBox);
			for (const TSharedPtr<FJsonValue>& GV : Groups)
			{
				const TSharedPtr<FJsonObject> G = GV->AsObject();
				const FString Name = G->GetStringField(TEXT("name"));
				const FString Label = FString::Printf(TEXT("%s (%d holes, par %d)"), Name.IsEmpty() ? TEXT("The holes") : *Name, int32(G->GetNumberField(TEXT("holes"))), int32(G->GetNumberField(TEXT("par"))));
				GB->AddSlot().AutoHeight().Padding(0, 2)[Button(Label, [this, Name]() { Group = Name; Rebuild(); }, 12, Group == Name)];
			}
			V->AddSlot().AutoHeight()[GB];
		}
		V->AddSlot().AutoHeight().Padding(0, 10, 0, 2)[Text(TEXT("Its name in the game"), 12, Soft)];
		V->AddSlot().AutoHeight()[SAssignNew(NameBox, SEditableTextBox).Text(FText::FromString(NameDraft)).Font(Font(14))
			.OnTextChanged_Lambda([this](const FText& T) { NameDraft = T.ToString(); })];
		V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)
		[
			bBusy ? StaticCastSharedRef<SWidget>(Text(TEXT("In the queue: ") + Queued->Message, 12, Soft))
				: StaticCastSharedRef<SWidget>(Button(bLine ? TEXT("Add it to the queue") : TEXT("Build this course"), [this]() { StartBuild(); }, 15, true))
		];
		V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(400)
			.Text(FText::FromString(TEXT("It downloads the map data, the ground's heights and aerial photos, builds the course, then gets it ready for the game - about 5 to 20 minutes, most of it the last step.")))];
	}
	return Box(V, FMargin(12, 10), Solid);
}

TSharedRef<SWidget> SOTGCourseBuilder::BuiltList()
{
	if (!Built.Num()) return SNullWidget::NullWidget;
	UOTGHelperSubsystem* H = Helper();
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[Text(TEXT("Courses you've built"), 15, Gold, "Bold")];
	for (const FBuilt& B : Built)
	{
		const FString Id = B.Id;
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		Row->AddSlot().FillWidth(1).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Text(B.Name, 13, Ink, "Bold")]
			+ SVerticalBox::Slot().AutoHeight()[Text(B.bInGame ? TEXT("in the game") : TEXT("not in the game yet"), 11, B.bInGame ? Green : Soft)]
		];
		if (H && H->ActiveFor(Id)) Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)[Text(TEXT("in the queue"), 11, Soft)];
		else
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)[Button(TEXT("Edit"), [this, Id]() { if (OnEdit) OnEdit(Id); }, 11)];
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)[Button(B.bInGame ? TEXT("Export again") : TEXT("Export"), [this, Id]() { if (UOTGHelperSubsystem* H2 = Helper()) H2->Export(Id); Rebuild(); }, 11)];
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)[Button(TEXT("Update"), [this, Id]() { if (UOTGHelperSubsystem* H2 = Helper()) H2->Update(Id); Rebuild(); }, 11)];
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)[Button(ConfirmDelete == Id ? TEXT("Sure? Delete") : TEXT("Delete"), [this, Id]() {
				if (ConfirmDelete != Id) { ConfirmDelete = Id; Rebuild(); return; }
				ConfirmDelete.Empty();
				// (the course's source: what the game has of it, in its courses folder, stays)
				if (UOTGHelperSubsystem* H2 = Helper())
				{
					TWeakPtr<SOTGCourseBuilder> Weak = SharedThis(this);
					H2->Request(TEXT("DELETE"), TEXT("/api/courses/") + Id, FString(), [Weak](int32, TSharedPtr<FJsonValue>, const FString&) { if (TSharedPtr<SOTGCourseBuilder> S = Weak.Pin()) S->LoadBuilt(); });
				}
			}, 11, ConfirmDelete == Id)];
		}
		V->AddSlot().AutoHeight().Padding(0, 3)[Row];
	}
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(400)
		.Text(FText::FromString(TEXT("Export again after editing a course. Update fetches its map data afresh (your trees, grass and looks are kept). Delete removes what it was built from; the game keeps what it has.")))];
	return Box(V, FMargin(12, 10), Solid);
}

void SOTGCourseBuilder::Rebuild()
{
	if (!Lower.IsValid()) return;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	// what the search found
	if (Found.Num())
	{
		TSharedRef<SVerticalBox> F = SNew(SVerticalBox);
		for (int32 I : Found)
		{
			if (!Courses.IsValidIndex(I)) continue;
			const FOTGMapCourse& M = Courses[I];
			F->AddSlot().AutoHeight().Padding(0, 2)
			[
				SNew(SButton).ButtonStyle(&ButtonStyle(I == Picked)).ContentPadding(FMargin(10, 4))
				.OnClicked_Lambda([this, I]() { Pick(I, true); return FReply::Handled(); })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Text(M.Name + (M.BuiltId.IsEmpty() ? FString() : FString(TEXT("  (built)"))), 13, Ink, "Bold")]
					+ SVerticalBox::Slot().AutoHeight()[Text(M.Location, 11, Soft)]
				]
			];
		}
		V->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[SNew(SBox).MaxDesiredHeight(180)[SNew(SScrollBox) + SScrollBox::Slot()[F]]];
	}
	TSharedRef<SVerticalBox> Rest = SNew(SVerticalBox);
	Rest->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[PickedCard()];
	Rest->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[JobCard()];
	Rest->AddSlot().AutoHeight()[BuiltList()];
	V->AddSlot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot()[Rest]];
	Lower->SetContent(V);
}
