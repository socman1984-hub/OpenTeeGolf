// See SOTGMinimap.h (public/js/main.js drawMinimap, mapLabel, minimapInput).

#include "SOTGMinimap.h"

#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "OTGUi.h"
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

SOTGMinimap::FFrame SOTGMinimap::Frame(const FVector2D& Size) const
{
	FFrame F;
	F.C = FVector2D(Size.X / 2, (Size.Y - 34) / 2);
	F.Mid = (State.Tee + State.Pin) / 2;
	// (the hole up the map, tee to green filling most of it)
	const FVector2D D(State.Pin.X - State.Tee.X, State.Tee.Y - State.Pin.Y); // (screen: x right, y down; course z up)
	const double Len = D.Size();
	F.K = (Size.Y - 34) * 0.8 / (Len + 40);
	const double Theta = -UE_DOUBLE_HALF_PI - FMath::Atan2(D.Y, D.X);
	F.Cos = FMath::Cos(Theta);
	F.Sin = FMath::Sin(Theta);
	return F;
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
	auto Line = [&](const FVector2D& A, const FVector2D& B, const FLinearColor& C, float W, double Dash = 0) {
		if (Dash <= 0)
		{
			FSlateDrawElement::MakeLines(Out, L, G.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(A), FVector2f(B) }, ESlateDrawEffect::None, C, true, W);
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
	if (Hover)
	{
		const FVector2D H = ToLocal(F, *Hover);
		Line(B, H, Sky, 1.5f);
		Line(H, P, Sky, 1.5f, 4);
		RingAt(H, 5, Sky);
		const FString Surf = SurfaceName ? SurfaceName(Hover->X, Hover->Y) : FString();
		Label(Fmt(Dist(State.Ball, *Hover)) + (Surf.IsEmpty() ? FString() : TEXT("  ·  ") + Surf), H - FVector2D(0, 22), Sky);
		if (Dist(*Hover, State.Pin) > 1) Label(Fmt(Dist(*Hover, State.Pin)) + TEXT(" to pin"), (H + P) / 2, FLinearColor::White);
	}
	else if (!State.bOnGreen)
	{
		Line(A, P, FLinearColor(1, 1, 1, 0.45f), 1.f, 3);
		Label(Yd(Dist(State.Ball, State.Aim)), (B + A) / 2, Yellow);
		if (Dist(State.Aim, State.Pin) > 5) Label(Yd(Dist(State.Aim, State.Pin)) + TEXT(" left"), (A + P) / 2, FLinearColor::White);
	}
	// the bar along the bottom
	FSlateDrawElement::MakeBox(Out, L + 5, G.ToPaintGeometry(FVector2D(Size.X, 34), FSlateLayoutTransform(FVector2D(0, Size.Y - 34))), White, ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.6f));
	const FString Bar = FString::Printf(TEXT("To pin %s  ·  click to aim"), *Fmt(Dist(State.Ball, State.Pin)));
	FSlateDrawElement::MakeText(Out, L + 6, G.ToPaintGeometry(FVector2D(Size.X - 16, 20), FSlateLayoutTransform(FVector2D(8, Size.Y - 26))), Bar, Font(12), ESlateDrawEffect::None, FLinearColor::White);
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
	bDragging = true;
	AimFrom(Geometry, Event);
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SOTGMinimap::OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	bDragging = false;
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
	if (State.bValid && Local.Y <= Geometry.GetLocalSize().Y - 34) Hover = ToCourse(Frame(Geometry.GetLocalSize()), Local);
	else Hover.Reset();
	return FReply::Handled();
}

void SOTGMinimap::OnMouseLeave(const FPointerEvent&)
{
	Hover.Reset();
}
