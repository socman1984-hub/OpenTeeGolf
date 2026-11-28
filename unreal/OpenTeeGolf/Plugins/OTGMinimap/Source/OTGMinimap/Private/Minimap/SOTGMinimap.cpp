// See SOTGMinimap.h (public/js/main.js drawMinimap, mapLabel, minimapInput).

#include "Minimap/SOTGMinimap.h"

#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/OTGUi.h"
#include "Rendering/DrawElements.h"

using namespace otg::ui;

namespace
{
	constexpr double M_TO_YD = 1.09361, M_TO_FT = 3.28084;
	FString Yd(double M) { return FString::Printf(TEXT("%d yd"), FMath::RoundToInt(M * M_TO_YD)); }
	FString Ft(double M) { return FString::Printf(TEXT("%d ft"), FMath::RoundToInt(M * M_TO_FT)); }
}

void SOTGMinimap::Construct(const FArguments&)
{
	SetClipping(EWidgetClipping::ClipToBounds);
}

void SOTGMinimap::SetCourse(UTexture2D* InPhoto, double InX0, double InZ1, double InSizeM, TFunction<FString(double, double)> InSurfaceName)
{
	Photo = InPhoto;
	X0 = InX0; Z1 = InZ1; SizeM = FMath::Max(1.0, InSizeM);
	SurfaceName = InSurfaceName;
	PhotoBrush = FSlateBrush();
	if (InPhoto)
	{
		// (all of it in memory: drawn on the map, nothing tells the texture streamer it's needed)
		InPhoto->SetForceMipLevelsToBeResident(3600.f);
		PhotoBrush.SetResourceObject(InPhoto);
		PhotoBrush.DrawAs = ESlateBrushDrawType::Image;
	}
}

namespace
{
	constexpr double ZOOM_STEP = 1.5;
	constexpr int32 ZOOM_MIN = -3, ZOOM_MAX = 5;
	constexpr double BUTTON = 26, BUTTON_GAP = 4;
}

SOTGMinimap::FFrame SOTGMinimap::Turned(const FVector2D& Size) const
{
	FFrame F;
	F.C = FVector2D(Size.X / 2, (Size.Y - 34) / 2);
	F.Mid = (State.Tee + State.Pin) / 2;
	// (the hole up the map: the tee at the bottom, the green at the top)
	const FVector2D D(State.Pin.X - State.Tee.X, State.Tee.Y - State.Pin.Y); // (screen: x right, y down; course z up)
	const double Theta = -UE_DOUBLE_HALF_PI - FMath::Atan2(D.Y, D.X);
	F.Cos = FMath::Cos(Theta);
	F.Sin = FMath::Sin(Theta);
	return F;
}

SOTGMinimap::FFrame SOTGMinimap::Target(const FVector2D& Size) const
{
	FFrame F = Turned(Size);
	const double H = FMath::Max(Size.Y - 34, 1.0), W = FMath::Max(Size.X, 1.0);
	// (the farthest out it frames by itself: the whole hole, tee to green, as from the tee)
	const double KHole = H * 0.8 / ((State.Pin - State.Tee).Size() + 40);
	// the ball and the pin, with room round the green, turned with the hole (in metres)
	FFrame U = F; U.K = 1; U.C = FVector2D::ZeroVector;
	const FVector2D Bu = ToLocal(U, State.Ball), Pu = ToLocal(U, State.Pin);
	const double Room = 22;   // (the green and what is round it)
	const FVector2D Lo(FMath::Min(Bu.X, Pu.X) - Room, FMath::Min(Bu.Y - 8, Pu.Y - Room));
	const FVector2D Hi(FMath::Max(Bu.X, Pu.X) + Room, FMath::Max(Bu.Y + 8, Pu.Y + Room));
	const FVector2D Span = Hi - Lo;
	// (closer in as the ball nears the hole, never closer than about 60 m top to bottom)
	const double K = FMath::Max(KHole, FMath::Min(H * 0.86 / FMath::Max(Span.Y, 60.0), W * 0.9 / FMath::Max(Span.X, 1.0)));
	F.Mid = K <= KHole * 1.0001 ? (State.Tee + State.Pin) / 2 : ToCourse(U, (Lo + Hi) / 2);
	// the player's zoom on that, keeping the ball on the map when zoomed in
	const double Z = FMath::Pow(ZOOM_STEP, double(ZoomSteps));
	F.K = K * Z;
	if (ZoomSteps > 0) F.Mid = F.Mid + (State.Ball - F.Mid) * (1 - 1 / Z) * 0.6;
	return F;
}

SOTGMinimap::FFrame SOTGMinimap::Frame(const FVector2D& Size) const
{
	if (Held) return *Held;
	if (!bShown) return Target(Size);
	FFrame F = Turned(Size);
	F.Mid = ShownMid;
	F.K = ShownK;
	return F;
}

void SOTGMinimap::Tick(const FGeometry& Geometry, const double, const float DeltaTime)
{
	if (!State.bValid) return;
	// (a new hole: the player's zoom starts again, and the map jumps to it)
	if (!ZoomHole.Equals(State.Pin, 0.5)) { ZoomHole = State.Pin; ZoomSteps = 0; bShown = false; }
	const FFrame T = Target(Geometry.GetLocalSize());
	if (!bShown) { ShownMid = T.Mid; ShownK = T.K; bShown = true; return; }
	if (Held) return;
	// (eases there: most of the way in about half a second)
	const double A = 1 - FMath::Exp(-6.0 * FMath::Clamp(double(DeltaTime), 0.0, 0.25));
	ShownMid = FMath::Lerp(ShownMid, T.Mid, A);
	ShownK = FMath::Exp(FMath::Lerp(FMath::Loge(ShownK), FMath::Loge(T.K), A));
}

FSlateRect SOTGMinimap::ZoomButton(bool bIn) const
{
	// (top left: + over -)
	const double Top = 6 + (bIn ? 0 : BUTTON + BUTTON_GAP);
	return FSlateRect(6, Top, 6 + BUTTON, Top + BUTTON);
}

void SOTGMinimap::ZoomBy(int32 Steps)
{
	ZoomSteps = FMath::Clamp(ZoomSteps + Steps, ZOOM_MIN, ZOOM_MAX);
}

FVector2D SOTGMinimap::ToLocal(const FFrame& F, const FVector2D& P) const
{
	const double A = (P.X - F.Mid.X) * F.K, B = (F.Mid.Y - P.Y) * F.K;
	return F.C + FVector2D(A * F.Cos - B * F.Sin, A * F.Sin + B * F.Cos);
}

FVector2D SOTGMinimap::ToCourse(const FFrame& F, const FVector2D& L) const
{
	const FVector2D R = L - F.C;
	const double A = R.X * F.Cos + R.Y * F.Sin, B = -R.X * F.Sin + R.Y * F.Cos;
	return FVector2D(F.Mid.X + A / F.K, F.Mid.Y - B / F.K);
}

int32 SOTGMinimap::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
	const FVector2D Size = G.GetLocalSize();
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(), White, ESlateDrawEffect::None, FLinearColor(0.02f, 0.04f, 0.03f, 0.85f));
	if (!State.bValid) return Layer;
	const FFrame F = Frame(Size);
	// the photo, turned with the hole
	if (Photo.IsValid())
	{
		const FVector2D Img(SizeM * F.K, SizeM * F.K);
		const FVector2D Pivot((F.Mid.X - X0) * F.K, (Z1 - F.Mid.Y) * F.K);
		const FVector2D Off = F.C - Pivot;
		FSlateDrawElement::MakeRotatedBox(Out, Layer + 1, G.ToPaintGeometry(Img, FSlateLayoutTransform(Off)), &PhotoBrush, ESlateDrawEffect::None,
			float(FMath::Atan2(F.Sin, F.Cos)), FVector2f(Pivot), FSlateDrawElement::RelativeToElement, FLinearColor::White);
	}
	const int32 L = Layer + 2;
	auto Line = [&](const FVector2D& A, const FVector2D& B, const FLinearColor& C, float W, double Dash = 0, int32 At = -1) {
		if (Dash <= 0)
		{
			FSlateDrawElement::MakeLines(Out, At >= 0 ? At : L, G.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(A), FVector2f(B) }, ESlateDrawEffect::None, C, true, W);
			return;
		}
		const double Len = (B - A).Size();
		const FVector2D U = (B - A) / FMath::Max(Len, 1e-6);
		for (double S = 0; S < Len; S += Dash * 1.7)
			FSlateDrawElement::MakeLines(Out, L, G.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(A + U * S), FVector2f(A + U * FMath::Min(Len, S + Dash)) }, ESlateDrawEffect::None, C, true, W);
	};
	auto DotAt = [&](const FVector2D& P, double R, const FLinearColor& C, bool bOutline = true) {
		if (bOutline) FSlateDrawElement::MakeBox(Out, L + 1, G.ToPaintGeometry(FVector2D(2 * R + 2, 2 * R + 2), FSlateLayoutTransform(P - FVector2D(R + 1, R + 1))), &Dot, ESlateDrawEffect::None, FLinearColor::Black);
		FSlateDrawElement::MakeBox(Out, L + 2, G.ToPaintGeometry(FVector2D(2 * R, 2 * R), FSlateLayoutTransform(P - FVector2D(R, R))), &Dot, ESlateDrawEffect::None, C);
	};
	auto RingAt = [&](const FVector2D& P, double R, const FLinearColor& C) {
		FSlateDrawElement::MakeBox(Out, L + 2, G.ToPaintGeometry(FVector2D(2 * R, 2 * R), FSlateLayoutTransform(P - FVector2D(R, R))), &Ring, ESlateDrawEffect::None, C);
	};
	const FSlateFontInfo LabelFont = Font(11, "Bold");
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	auto Label = [&](const FString& Text, FVector2D P, const FLinearColor& C) {
		const FVector2D T = Measure->Measure(Text, LabelFont);
		const FVector2D Box(T.X + 12, T.Y + 6);
		P.X = FMath::Clamp(P.X, Box.X / 2 + 3, Size.X - Box.X / 2 - 3);
		P.Y = FMath::Clamp(P.Y, Box.Y / 2 + 3, Size.Y - 34 - Box.Y / 2 - 3);
		FSlateDrawElement::MakeBox(Out, L + 3, G.ToPaintGeometry(Box, FSlateLayoutTransform(P - Box / 2)), &Dot, ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.72f));
		FSlateDrawElement::MakeText(Out, L + 4, G.ToPaintGeometry(T, FSlateLayoutTransform(P - T / 2)), Text, LabelFont, ESlateDrawEffect::None, C);
	};
	const FVector2D B = ToLocal(F, State.Ball), A = ToLocal(F, State.Aim), P = ToLocal(F, State.Pin);
	const FLinearColor Yellow(1.f, 0.88f, 0.3f), Sky(0.5f, 0.83f, 1.f);
	// the aim line and point
	Line(B, A, FLinearColor(1, 1, 1, 0.9f), 1.5f, 6);
	if (!State.bOnGreen) RingAt(A, 6, Yellow);
	DotAt(P, 4, FLinearColor(0.91f, 0.16f, 0.17f));
	for (const auto& O : State.Others) DotAt(ToLocal(F, O.Key), 3.5, O.Value);
	DotAt(B, 3.5, FLinearColor::White);
	if (State.Ring.A > 0) RingAt(B, 7, State.Ring);
	auto Dist = [](const FVector2D& X, const FVector2D& Y) { return (X - Y).Size(); };
	auto Fmt = [&](double M) { return State.bOnGreen ? Ft(M) : Yd(M); };
	// (where the mouse points: only a ring on the map - its distances in the bar under it, so they
	// don't cover the hole; the map's own labels are the aim's, where you clicked)
	if (Hover) RingAt(ToLocal(F, *Hover), 5, Sky);
	if (!State.bOnGreen)
	{
		Line(A, P, FLinearColor(1, 1, 1, 0.45f), 1.f, 3);
		Label(Yd(Dist(State.Ball, State.Aim)), (B + A) / 2, Yellow);
		if (Dist(State.Aim, State.Pin) > 5) Label(Yd(Dist(State.Aim, State.Pin)) + TEXT(" left"), (A + P) / 2, FLinearColor::White);
	}
	// zoom in and out
	for (const bool bIn : { true, false })
	{
		const FSlateRect R = ZoomButton(bIn);
		const bool bLive = bIn ? ZoomSteps < ZOOM_MAX : ZoomSteps > ZOOM_MIN;
		FSlateDrawElement::MakeBox(Out, L + 5, G.ToPaintGeometry(R.GetSize(), FSlateLayoutTransform(R.GetTopLeft())), &Dot, ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.62f));
		const FLinearColor C(1, 1, 1, bLive ? 0.95f : 0.35f);
		const FVector2D M = R.GetCenter();
		Line(M - FVector2D(6, 0), M + FVector2D(6, 0), C, 2.f, 0, L + 6);
		if (bIn) Line(M - FVector2D(0, 6), M + FVector2D(0, 6), C, 2.f, 0, L + 6);
	}
	// the bar along the bottom
	FSlateDrawElement::MakeBox(Out, L + 5, G.ToPaintGeometry(FVector2D(Size.X, 34), FSlateLayoutTransform(FVector2D(0, Size.Y - 34))), White, ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.6f));
	FString Bar = FString::Printf(TEXT("To pin %s  ·  click to aim"), *Fmt(Dist(State.Ball, State.Pin)));
	FLinearColor BarInk = FLinearColor::White;
	if (Hover)
	{
		// (the mouse: how far from the ball, how far on to the pin, what's there)
		const FString Surf = SurfaceName ? SurfaceName(Hover->X, Hover->Y) : FString();
		Bar = Fmt(Dist(State.Ball, *Hover));
		if (Dist(*Hover, State.Pin) > 1) Bar += TEXT("  ·  ") + Fmt(Dist(*Hover, State.Pin)) + TEXT(" to pin");
		if (!Surf.IsEmpty()) Bar += TEXT("  ·  ") + Surf;
		BarInk = Sky;
	}
	FSlateDrawElement::MakeText(Out, L + 6, G.ToPaintGeometry(FVector2D(Size.X - 16, 20), FSlateLayoutTransform(FVector2D(8, Size.Y - 26))), Bar, Font(12), ESlateDrawEffect::None, BarInk);
	return L + 6;
}

void SOTGMinimap::AimFrom(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!State.bValid) return;
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	if (Local.Y > Geometry.GetLocalSize().Y - 34) return;
	const FVector2D P = ToCourse(Frame(Geometry.GetLocalSize()), Local);
	if (OnAim) OnAim(P.X, P.Y);
}

FReply SOTGMinimap::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	for (const bool bIn : { true, false })
		if (ZoomButton(bIn).ContainsPoint(Local)) { ZoomBy(bIn ? 1 : -1); return FReply::Handled(); }
	bDragging = true;
	Held = Frame(Geometry.GetLocalSize());
	AimFrom(Geometry, Event);
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SOTGMinimap::OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	if (!bDragging) return FReply::Handled();
	bDragging = false;
	Held.Reset();
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SOTGMinimap::OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (bDragging)
	{
		AimFrom(Geometry, Event);
		return FReply::Handled();
	}
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	const bool bOnButton = ZoomButton(true).ContainsPoint(Local) || ZoomButton(false).ContainsPoint(Local);
	if (State.bValid && !bOnButton && Local.Y <= Geometry.GetLocalSize().Y - 34) Hover = ToCourse(Frame(Geometry.GetLocalSize()), Local);
	else Hover.Reset();
	return FReply::Handled();
}

FReply SOTGMinimap::OnMouseWheel(const FGeometry&, const FPointerEvent& Event)
{
	if (!State.bValid || bDragging) return FReply::Unhandled();
	ZoomBy(Event.GetWheelDelta() > 0 ? 1 : -1);
	return FReply::Handled();
}

void SOTGMinimap::OnMouseLeave(const FPointerEvent&)
{
	Hover.Reset();
}
