// The course editor's map (see SOTGEditor.h): drawing the course, and the mouse and keys on it.

#include "SOTGEditor.h"

#include "OTGUi.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"

using namespace otg::ui;

namespace
{
	FLinearColor Hex(uint32 C, float A = 1.f) { return FLinearColor(FColor((C >> 16) & 255, (C >> 8) & 255, C & 255, uint8(A * 255))); }
	// the shapes' colours (editor.js SHAPES): fill, outline
	void ShapeColours(const FString& K, FLinearColor& Fill, FLinearColor& Line)
	{
		if (K == TEXT("fairway")) { Fill = Hex(0x76b446, 0.85f); Line = Hex(0xa6dc76); }
		else if (K == TEXT("green")) { Fill = Hex(0x62d65c, 0.92f); Line = Hex(0xc4ffbd); }
		else if (K == TEXT("tee")) { Fill = Hex(0x88c462, 0.92f); Line = Hex(0xdcffd0); }
		else if (K == TEXT("bunker")) { Fill = Hex(0xe8d7a5, 0.95f); Line = Hex(0xfff3cf); }
		else if (K == TEXT("water")) { Fill = Hex(0x3778af, 0.88f); Line = Hex(0x8fd0ff); }
		else if (K == TEXT("penalty")) { Fill = Hex(0xdc3228, 0.16f); Line = Hex(0xff5a48); }
		else if (K == TEXT("trees")) { Fill = Hex(0x287828, 0.28f); Line = Hex(0x8fe37f); }
		else { Fill = FLinearColor::Transparent; Line = FLinearColor::White; }
	}
	int32 DrawRank(const FString& K)
	{
		static const TArray<FString> Order = { TEXT("oob"), TEXT("trees"), TEXT("fairway"), TEXT("tee"), TEXT("green"), TEXT("bunker"), TEXT("water"), TEXT("penalty") };
		return Order.IndexOfByKey(K);
	}
	FLinearColor TeeColour(const FString& T)
	{
		static const TMap<FString, uint32> C = { { TEXT("Black"), 0x1d1d1d }, { TEXT("Blue"), 0x2f62e8 }, { TEXT("White"), 0xffffff }, { TEXT("Green"), 0x22aa33 },
			{ TEXT("Yellow"), 0xeecc22 }, { TEXT("Red"), 0xdd2222 }, { TEXT("Junior"), 0xff8800 }, { TEXT("Par3"), 0x9a9a9a } };
		const uint32* V = C.Find(T);
		return Hex(V ? *V : 0xffffff);
	}
	FLinearColor BuildingColour(const FString& T)
	{
		static const TMap<FString, uint32> C = { { TEXT("house"), 0xe8c46a }, { TEXT("clubhouse"), 0xff7a59 }, { TEXT("garage"), 0x9fb6c9 }, { TEXT("shed"), 0xb8a07a },
			{ TEXT("barn"), 0xc0473a }, { TEXT("porch"), 0x7fd1b9 }, { TEXT("commercial"), 0xb48fe0 } };
		const uint32* V = C.Find(T);
		return Hex(V ? *V : 0xcfcfcf);
	}
	bool InPoly(const TArray<FVector2D>& P, const FVector2D& Q)
	{
		bool In = false;
		for (int32 i = 0, j = P.Num() - 1; i < P.Num(); j = i++)
			if ((P[i].Y > Q.Y) != (P[j].Y > Q.Y) && Q.X < (P[j].X - P[i].X) * (Q.Y - P[i].Y) / (P[j].Y - P[i].Y) + P[i].X) In = !In;
		return In;
	}
	double SegDist(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D D = B - A;
		const double L = D.SizeSquared();
		const double T = L > 0 ? FMath::Clamp(FVector2D::DotProduct(P - A, D) / L, 0.0, 1.0) : 0.0;
		return FVector2D::Distance(P, A + D * T);
	}
	// Ear clipping: a simple polygon's triangles (indices into P)
	void Triangulate(const TArray<FVector2D>& P, TArray<int32>& Out)
	{
		const int32 N = P.Num();
		if (N < 3) return;
		double A = 0;
		for (int32 i = 0, j = N - 1; i < N; j = i++) A += P[j].X * P[i].Y - P[i].X * P[j].Y;
		TArray<int32> V;
		for (int32 i = 0; i < N; i++) V.Add(A > 0 ? i : N - 1 - i);
		auto Cross = [&](int32 a, int32 b, int32 c) { return (P[b].X - P[a].X) * (P[c].Y - P[a].Y) - (P[b].Y - P[a].Y) * (P[c].X - P[a].X); };
		int32 Guard = 0;
		while (V.Num() > 3 && Guard++ < N * N)
		{
			bool bCut = false;
			for (int32 i = 0; i < V.Num(); i++)
			{
				const int32 a = V[(i + V.Num() - 1) % V.Num()], b = V[i], c = V[(i + 1) % V.Num()];
				if (Cross(a, b, c) <= 0) continue;
				bool bInside = false;
				for (int32 k : V)
				{
					if (k == a || k == b || k == c) continue;
					if (Cross(a, b, k) >= 0 && Cross(b, c, k) >= 0 && Cross(c, a, k) >= 0) { bInside = true; break; }
				}
				if (bInside) continue;
				Out.Append({ a, b, c });
				V.RemoveAt(i);
				bCut = true;
				break;
			}
			if (!bCut) break;   // (not simple: what's left as a fan)
		}
		for (int32 i = 1; i + 1 < V.Num(); i++) Out.Append({ V[0], V[i], V[i + 1] });
	}
	const FSlateRoundedBoxBrush& Round() { static const FSlateRoundedBoxBrush B(FLinearColor::White, 20.f); return B; }
}

void SOTGEditCanvas::Construct(const FArguments&, SOTGCourseEditor* InEditor)
{
	Ed = InEditor;
	SetClipping(EWidgetClipping::ClipToBounds);
}

FVector2D SOTGEditCanvas::ToScreen(const FVector2D& W) const
{
	return FVector2D((W.X - Cx) * Scale + Size.X / 2, Size.Y / 2 - (W.Y - Cz) * Scale);
}

FVector2D SOTGEditCanvas::ToWorld(const FVector2D& L) const
{
	return FVector2D((L.X - Size.X / 2) / Scale + Cx, (Size.Y / 2 - L.Y) / Scale + Cz);
}

void SOTGEditCanvas::Fit(const TArray<FVector2D>& Pts, double PadM)
{
	if (!Pts.Num()) return;
	FBox2D B(ForceInit);
	for (const FVector2D& P : Pts) B += P;
	B = B.ExpandBy(PadM);
	Cx = B.GetCenter().X; Cz = B.GetCenter().Y;
	const FVector2D S = B.GetSize();
	Scale = FMath::Clamp(FMath::Min(Size.X / FMath::Max(1.0, S.X), Size.Y / FMath::Max(1.0, S.Y)), 0.02, 40.0);
}

void SOTGEditCanvas::PolyFill(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const TArray<FVector2D>& Pts, const FLinearColor& C) const
{
	if (Pts.Num() < 3 || C.A <= 0) return;
	TArray<FVector2D> S;
	for (const FVector2D& P : Pts) S.Add(ToScreen(P));
	TArray<int32> Tri;
	Triangulate(S, Tri);
	const FSlateRenderTransform& RT = G.GetAccumulatedRenderTransform();
	const FColor Col = C.ToFColor(true);
	TArray<FSlateVertex> V;
	for (const FVector2D& P : S) V.Add(FSlateVertex::Make(RT, FVector2f(P), FVector2f(0.5f, 0.5f), Col));
	TArray<SlateIndex> I;
	for (int32 t : Tri) I.Add(SlateIndex(t));
	const FSlateResourceHandle H = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush("WhiteBrush"));
	FSlateDrawElement::MakeCustomVerts(Out, Layer, H, V, I, nullptr, 0, 0);
}

void SOTGEditCanvas::PolyLine(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const TArray<FVector2D>& Pts, const FLinearColor& C, float W, bool bClosed) const
{
	if (Pts.Num() < 2) return;
	TArray<FVector2f> S;
	for (const FVector2D& P : Pts) S.Add(FVector2f(ToScreen(P)));
	if (bClosed) { const FVector2f First = S[0]; S.Add(First); }
	FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), S, ESlateDrawEffect::None, C, true, W);
}

void SOTGEditCanvas::Dot(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& P, float R, const FLinearColor& C, bool bOutline) const
{
	if (bOutline) FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2D(2 * R + 3, 2 * R + 3), FSlateLayoutTransform(P - FVector2D(R + 1.5, R + 1.5))), &Round(), ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.8f));
	FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2D(2 * R, 2 * R), FSlateLayoutTransform(P - FVector2D(R, R))), &Round(), ESlateDrawEffect::None, C);
}

void SOTGEditCanvas::Label(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& P, const FString& S, const FLinearColor& C) const
{
	const FSlateFontInfo F = Font(11, "Bold");
	const FVector2D T = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(S, F);
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(T + FVector2D(8, 4), FSlateLayoutTransform(P - FVector2D(4, 2))), FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.7f));
	FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(T, FSlateLayoutTransform(P)), S, F, ESlateDrawEffect::None, C);
}

int32 SOTGEditCanvas::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
	Size = G.GetLocalSize();
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(), White, ESlateDrawEffect::None, FLinearColor(0.06f, 0.08f, 0.06f, 1.f));
	if (!Ed || !Ed->bLoaded)
	{
		FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(FVector2D(400, 30), FSlateLayoutTransform(Size / 2 - FVector2D(120, 10))), FString(TEXT("Loading the course…")), Font(16), ESlateDrawEffect::None, FLinearColor::White);
		return Layer + 1;
	}
	const FOTGCourseSource& S = Ed->Src;
	int32 L = Layer + 1;
	// the picture: the aerial photo, or the ground's shading
	auto Picture = [&](const FSlateBrush* B, double X0, double Z1, double W, double H, const FLinearColor& Tint) {
		const FVector2D A = ToScreen(FVector2D(X0, Z1)), Bm = ToScreen(FVector2D(X0 + W, Z1 - H));
		FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(Bm - A, FSlateLayoutTransform(A)), B, ESlateDrawEffect::None, Tint);
	};
	if (Ed->bShowPhoto && Ed->PhotoBrush.IsValid()) Picture(Ed->PhotoBrush.Get(), Ed->PhotoX0, Ed->PhotoZ1, Ed->PhotoSizeM, Ed->PhotoSizeM, FLinearColor(0.85f, 0.85f, 0.85f, 1));
	else if (Ed->ShadeBrush.IsValid()) Picture(Ed->ShadeBrush.Get(), Ed->ShadeX0, Ed->ShadeZ1, Ed->ShadeW, Ed->ShadeH, FLinearColor::White);
	L++;
	// the shapes, in the web editor's order (selected one last)
	TArray<int32> Order;
	for (int32 i = 0; i < S.Shapes.Num(); i++) Order.Add(i);
	Order.StableSort([&](int32 A, int32 B) { return DrawRank(S.Shapes[A].Kind) < DrawRank(S.Shapes[B].Kind); });
	for (int32 i : Order)
	{
		const FOTGSourceShape& Sh = S.Shapes[i];
		FLinearColor Fill, Line;
		ShapeColours(Sh.Kind, Fill, Line);
		if (Sh.bInner) Fill = FLinearColor(0.1f, 0.25f, 0.1f, 0.85f);   // (a cut-out: rough showing through)
		if (Ed->bShowPhoto && Ed->PhotoBrush.IsValid()) Fill.A *= 0.55f;
		PolyFill(Out, L, G, Sh.Pts, Fill);
		PolyLine(Out, L + 1, G, Sh.Pts, Line, Sh.Kind == TEXT("oob") ? 2.f : 1.f, true);
	}
	L += 2;
	if (Ed->Shapes_Valid(Ed->SelShape))
	{
		const FOTGSourceShape& Sh = S.Shapes[Ed->SelShape];
		PolyLine(Out, L, G, Sh.Pts, FLinearColor(1, 0.85f, 0.2f, 1), 2.5f, true);
		// its corners (dragged), and their middles (a new corner)
		for (int32 k = 0; k < Sh.Pts.Num(); k++)
		{
			Dot(Out, L + 1, G, ToScreen(Sh.Pts[k]), 4, FLinearColor::White);
			Dot(Out, L + 1, G, ToScreen((Sh.Pts[k] + Sh.Pts[(k + 1) % Sh.Pts.Num()]) / 2), 2.5f, FLinearColor(1, 1, 1, 0.5f), false);
		}
		L += 3;
	}
	// the tall grass painted (amber), rubbed out (grey)
	if (Ed->bShowGrass)
		for (const FOTGGrassStroke& St : S.TallGrass)
		{
			const FLinearColor C = St.bErase ? FLinearColor(0.5f, 0.5f, 0.5f, 0.45f) : FLinearColor(0.95f, 0.65f, 0.15f, 0.45f);
			const float W = FMath::Max(2.f, float(St.R * 2 * Scale));
			if (St.Pts.Num() == 1) Dot(Out, L, G, ToScreen(St.Pts[0]), W / 2, C, false);
			else PolyLine(Out, L, G, St.Pts, C, W, false);
		}
	L += 2;
	// the buildings, in their type's colour
	if (Ed->bShowBuildings)
		for (int32 b = 0; b < Ed->Buildings.Num(); b++)
		{
			const SOTGCourseEditor::FBuilding& B = Ed->Buildings[b];
			const FString* T = S.BuildingTypes.Find(B.Key);
			const FLinearColor C = BuildingColour(T ? *T : FString());
			PolyFill(Out, L, G, B.Pts, FLinearColor(C.R, C.G, C.B, b == Ed->SelBuilding ? 0.85f : 0.45f));
			PolyLine(Out, L + 1, G, B.Pts, b == Ed->SelBuilding ? FLinearColor::Yellow : C, b == Ed->SelBuilding ? 2.f : 1.f, true);
		}
	L += 2;
	// the trees: found (dark), removed (a cross), your own (bright, sized)
	if (Ed->bShowTrees)
	{
		const float Rf = FMath::Clamp(float(2.0 * Scale), 1.5f, 6.f);
		for (int32 i = 0; i < Ed->Found.Num(); i++)
		{
			const SOTGCourseEditor::FFound& F = Ed->Found[i];
			const FVector2D P = ToScreen(F.P);
			if (P.X < -10 || P.Y < -10 || P.X > Size.X + 10 || P.Y > Size.Y + 10) continue;
			const bool bSel = i == Ed->SelFound || Ed->SelMany.Contains(i);
			const bool bOff = S.TreesOff.Contains(F.Kind);
			Dot(Out, L, G, P, FMath::Max(Rf, float(F.R * Scale * 0.6)), bSel ? FLinearColor(1, 0.85f, 0.2f, 1) : bOff ? FLinearColor(0.45f, 0.45f, 0.45f, 0.6f) : FLinearColor(0.1f, 0.42f, 0.12f, 0.75f), bSel);
		}
		for (int32 i = 0; i < S.RemovedTrees.Num(); i++)
		{
			const FVector2D P = ToScreen(S.RemovedTrees[i]);
			const FLinearColor C = i == Ed->SelRemoved ? FLinearColor::Yellow : FLinearColor(1, 0.3f, 0.25f, 0.85f);
			FSlateDrawElement::MakeLines(Out, L + 2, G.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(P + FVector2D(-4, -4)), FVector2f(P + FVector2D(4, 4)) }, ESlateDrawEffect::None, C, true, 2);
			FSlateDrawElement::MakeLines(Out, L + 2, G.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(P + FVector2D(-4, 4)), FVector2f(P + FVector2D(4, -4)) }, ESlateDrawEffect::None, C, true, 2);
		}
		for (int32 i = 0; i < S.UserTrees.Num(); i++)
		{
			const FOTGSourceTree& T = S.UserTrees[i];
			const float R = T.Size == TEXT("small") ? 3.f : T.Size == TEXT("large") ? 6.f : 4.5f;
			Dot(Out, L + 3, G, ToScreen(T.P), FMath::Max(3.f, float(R * Scale)), i == Ed->SelTree ? FLinearColor(1, 0.85f, 0.2f, 1) : FLinearColor(0.35f, 0.95f, 0.35f, 0.95f));
		}
	}
	L += 5;
	// the holes: each one's line, the one being edited with its tees, aim points, green and pins
	for (int32 h = 0; h < S.Holes.Num(); h++)
	{
		const FOTGSourceHole& H = S.Holes[h];
		const bool bOn = h == Ed->HoleI;
		PolyLine(Out, L, G, Ed->HoleLine(H), bOn ? FLinearColor(1, 1, 1, 0.9f) : FLinearColor(1, 1, 1, 0.35f), bOn ? 2.f : 1.f, false);
		Label(Out, L + 1, G, ToScreen(H.Green) + FVector2D(8, -18), FString::FromInt(H.Number), bOn ? FLinearColor::Yellow : FLinearColor::White);
		if (!bOn) continue;
		for (const auto& T : H.Tees) Dot(Out, L + 3, G, ToScreen(T.Value), 6, TeeColour(T.Key));
		for (const FVector2D& A : H.Aims) Dot(Out, L + 3, G, ToScreen(A), 5, FLinearColor(1, 0.85f, 0.29f, 1));
		Dot(Out, L + 3, G, ToScreen(H.Green), 6, FLinearColor(1, 1, 1, 0.25f));
		for (const auto& P : H.Pins) Dot(Out, L + 3, G, ToScreen(P.Value), 4, FLinearColor(0.91f, 0.16f, 0.17f, 1));
	}
	L += 6;
	// what's being drawn, and the brush under the mouse
	if (Ed->Mode == SOTGCourseEditor::EMode::Draw && Ed->DrawPts.Num())
	{
		TArray<FVector2D> P = Ed->DrawPts;
		if (Hover.X >= 0) P.Add(ToWorld(Hover));
		PolyLine(Out, L, G, P, FLinearColor::Yellow, 2, false);
		for (const FVector2D& Q : Ed->DrawPts) Dot(Out, L + 1, G, ToScreen(Q), 3.5f, FLinearColor::Yellow);
	}
	if (Hover.X >= 0 && (Ed->Mode == SOTGCourseEditor::EMode::Cut || Ed->Mode == SOTGCourseEditor::EMode::Paint || Ed->Mode == SOTGCourseEditor::EMode::Erase))
	{
		const double R = (Ed->Mode == SOTGCourseEditor::EMode::Cut ? Ed->CutR : Ed->BrushR) * Scale;
		TArray<FVector2f> C;
		for (int32 k = 0; k <= 32; k++) C.Add(FVector2f(Hover + FVector2D(FMath::Cos(k * PI / 16), FMath::Sin(k * PI / 16)) * R));
		FSlateDrawElement::MakeLines(Out, L + 2, G.ToPaintGeometry(), C, ESlateDrawEffect::None, Ed->Mode == SOTGCourseEditor::EMode::Cut ? FLinearColor(1, 0.3f, 0.25f, 1) : FLinearColor(1, 0.75f, 0.2f, 1), true, 1.5f);
	}
	if (Drag == EDrag::Box)
	{
		const FVector2D A(FMath::Min(DragFrom.X, DragLast.X), FMath::Min(DragFrom.Y, DragLast.Y)), B(FMath::Max(DragFrom.X, DragLast.X), FMath::Max(DragFrom.Y, DragLast.Y));
		FSlateDrawElement::MakeBox(Out, L + 3, G.ToPaintGeometry(B - A, FSlateLayoutTransform(A)), White, ESlateDrawEffect::None, FLinearColor(1, 0.85f, 0.2f, 0.15f));
	}
	// the scale
	const double M = FMath::Pow(10.0, FMath::FloorToDouble(FMath::LogX(10.0, 120.0 / Scale)));
	const double Bar = (120.0 / Scale >= 5 * M ? 5 * M : 120.0 / Scale >= 2 * M ? 2 * M : M);
	FSlateDrawElement::MakeLines(Out, L + 4, G.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(16, Size.Y - 16), FVector2f(16 + Bar * Scale, Size.Y - 16) }, ESlateDrawEffect::None, FLinearColor::White, true, 3);
	Label(Out, L + 5, G, FVector2D(16, Size.Y - 36), FString::Printf(TEXT("%.0f m"), Bar), FLinearColor::White);
	return L + 6;
}

// ------------------------------------------------------------------ the mouse

FReply SOTGEditCanvas::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (!Ed || !Ed->bLoaded) return FReply::Unhandled();
	const FVector2D L = G.AbsoluteToLocal(E.GetScreenSpacePosition());
	const FVector2D W = ToWorld(L);
	FOTGCourseSource& S = Ed->Src;
	DragFrom = DragLast = L;
	bMoved = false;
	FReply Reply = FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	// (the right button: always the map moving)
	if (E.GetEffectingButton() == EKeys::RightMouseButton) { Drag = EDrag::Pan; PanFrom = FVector2D(Cx, Cz); return Reply; }
	if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	using EM = SOTGCourseEditor::EMode;
	switch (Ed->Mode)
	{
	case EM::Draw: Ed->DrawPts.Add(W); Drag = EDrag::None; return Reply;
	case EM::Place: Ed->PlacePoint(W); Drag = EDrag::None; return Reply;
	case EM::AddTree:
	{
		Ed->Change();
		FOTGSourceTree T; T.P = W; T.Size = Ed->TreeSize; T.Kind = Ed->TreeKind;
		S.UserTrees.Add(T);
		Ed->Changed();
		return Reply;
	}
	case EM::Cut: Ed->Change(); Ed->CutTrees(W); Drag = EDrag::Cut; return Reply;
	case EM::Paint: case EM::Erase:
	{
		Ed->Change();
		FOTGGrassStroke St; St.Pts = { W }; St.R = Ed->BrushR; St.bErase = Ed->Mode == EM::Erase;
		S.TallGrass.Add(St);
		Drag = EDrag::Brush;
		return Reply;
	}
	default: break;
	}
	// the hole's points
	if (S.Holes.IsValidIndex(Ed->HoleI))
	{
		const FOTGSourceHole& H = S.Holes[Ed->HoleI];
		auto Near = [&](const FVector2D& P) { return FVector2D::Distance(ToScreen(P), L) < 9; };
		FString Hit;
		for (const auto& P : H.Pins) if (Near(P.Value)) Hit = TEXT("pin:") + P.Key;
		for (int32 k = 0; k < H.Aims.Num(); k++) if (Near(H.Aims[k])) Hit = FString::Printf(TEXT("aim:%d"), k);
		for (const auto& T : H.Tees) if (Near(T.Value)) Hit = TEXT("tee:") + T.Key;
		if (Hit.IsEmpty() && Near(H.Green)) Hit = TEXT("green");
		if (!Hit.IsEmpty()) { Drag = EDrag::Handle; DragHandle = Hit; return Reply; }
	}
	// the selected shape's corners, and their middles (a new corner)
	if (Ed->Shapes_Valid(Ed->SelShape))
	{
		TArray<FVector2D>& P = S.Shapes[Ed->SelShape].Pts;
		for (int32 k = 0; k < P.Num(); k++)
			if (FVector2D::Distance(ToScreen(P[k]), L) < 7) { Ed->Change(); Drag = EDrag::Vertex; DragIndex = k; return Reply; }
		for (int32 k = 0; k < P.Num(); k++)
			if (FVector2D::Distance(ToScreen((P[k] + P[(k + 1) % P.Num()]) / 2), L) < 7) { Ed->Change(); P.Insert(W, k + 1); Drag = EDrag::Vertex; DragIndex = k + 1; return Reply; }
	}
	// your own trees
	if (Ed->bShowTrees)
		for (int32 i = S.UserTrees.Num() - 1; i >= 0; i--)
			if (FVector2D::Distance(ToScreen(S.UserTrees[i].P), L) < FMath::Max(7.0, 4.5 * Scale))
			{
				Ed->ClearPick(); Ed->SelTree = i; Ed->Change(); Drag = EDrag::Tree; DragIndex = i; Ed->Changed();
				return Reply;
			}
	// inside the selected shape: moved as a whole
	if (Ed->Shapes_Valid(Ed->SelShape) && S.Shapes[Ed->SelShape].Kind != TEXT("oob") && InPoly(S.Shapes[Ed->SelShape].Pts, W))
	{
		Drag = EDrag::Move; MoveOrig = S.Shapes[Ed->SelShape].Pts; MoveFromW = W; return Reply;
	}
	// shift: a box round found trees
	if (E.IsShiftDown()) { Drag = EDrag::Box; return Reply; }
	Drag = EDrag::Pan;   // (a click without moving picks what's under it: OnMouseButtonUp)
	PanFrom = FVector2D(Cx, Cz);
	return Reply;
}

FReply SOTGEditCanvas::OnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	const FVector2D L = G.AbsoluteToLocal(E.GetScreenSpacePosition());
	Hover = L;
	if (Drag == EDrag::None || !Ed) return FReply::Handled();
	if (FVector2D::Distance(L, DragFrom) > 3) bMoved = true;
	const FVector2D W = ToWorld(L);
	FOTGCourseSource& S = Ed->Src;
	switch (Drag)
	{
	case EDrag::Pan:
		if (!bMoved) break;
		Cx = PanFrom.X - (L.X - DragFrom.X) / Scale;
		Cz = PanFrom.Y + (L.Y - DragFrom.Y) / Scale;
		break;
	case EDrag::Handle:
	{
		if (!bMoved || !S.Holes.IsValidIndex(Ed->HoleI)) break;
		if (DragLast == DragFrom) Ed->Change();
		FOTGSourceHole& H = S.Holes[Ed->HoleI];
		FString K, V;
		DragHandle.Split(TEXT(":"), &K, &V);
		if (DragHandle == TEXT("green")) H.Green = W;
		else if (K == TEXT("tee")) H.Tees.Add(V, W);
		else if (K == TEXT("pin")) H.Pins.Add(V, W);
		else if (K == TEXT("aim") && H.Aims.IsValidIndex(FCString::Atoi(*V))) H.Aims[FCString::Atoi(*V)] = W;
		// (the panels made again when it's let go)
		break;
	}
	case EDrag::Vertex:
		if (Ed->Shapes_Valid(Ed->SelShape) && S.Shapes[Ed->SelShape].Pts.IsValidIndex(DragIndex)) { S.Shapes[Ed->SelShape].Pts[DragIndex] = W; S.Shapes[Ed->SelShape].bEdited = true; }
		break;
	case EDrag::Move:
		if (!bMoved || !Ed->Shapes_Valid(Ed->SelShape)) break;
		if (DragLast == DragFrom) Ed->Change();
		{
			FOTGSourceShape& Sh = S.Shapes[Ed->SelShape];
			Sh.Pts = MoveOrig;
			for (FVector2D& P : Sh.Pts) P += W - MoveFromW;
			Sh.bEdited = true;
		}
		break;
	case EDrag::Tree:
		if (bMoved && S.UserTrees.IsValidIndex(DragIndex)) S.UserTrees[DragIndex].P = W;
		break;
	case EDrag::Cut: Ed->CutTrees(W); break;
	case EDrag::Brush:
		if (S.TallGrass.Num())
		{
			// (a point every third of the brush's width or so)
			FOTGGrassStroke& St = S.TallGrass.Last();
			if (FVector2D::Distance(St.Pts.Last(), W) >= FMath::Max(0.5, St.R / 3)) St.Pts.Add(W);
		}
		break;
	default: break;
	}
	DragLast = L;
	return FReply::Handled();
}

FReply SOTGEditCanvas::OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E)
{
	if (!Ed) return FReply::Unhandled();
	const FVector2D L = G.AbsoluteToLocal(E.GetScreenSpacePosition());
	const FVector2D W = ToWorld(L);
	FOTGCourseSource& S = Ed->Src;
	const EDrag Was = Drag;
	Drag = EDrag::None;
	if (Was == EDrag::Box)
	{
		// the found trees in the box: picked
		const FVector2D A(FMath::Min(DragFrom.X, L.X), FMath::Min(DragFrom.Y, L.Y)), B(FMath::Max(DragFrom.X, L.X), FMath::Max(DragFrom.Y, L.Y));
		Ed->ClearPick();
		for (int32 i = 0; i < Ed->Found.Num(); i++) { const FVector2D P = ToScreen(Ed->Found[i].P); if (P.X >= A.X && P.X <= B.X && P.Y >= A.Y && P.Y <= B.Y) Ed->SelMany.Add(i); }
	}
	else if (Was == EDrag::Pan && !bMoved && E.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// a click: what's under it (a found tree, a removed one, a building, a shape)
		Ed->ClearPick();
		bool bHit = false;
		if (Ed->bShowTrees)
		{
			double Best = 6;
			for (int32 i = 0; i < Ed->Found.Num(); i++) { const double D = FVector2D::Distance(ToScreen(Ed->Found[i].P), L); if (D < Best) { Best = D; Ed->SelFound = i; bHit = true; } }
			if (E.IsShiftDown() && bHit) { Ed->SelMany.AddUnique(Ed->SelFound); Ed->SelFound = INDEX_NONE; }
			for (int32 i = 0; !bHit && i < S.RemovedTrees.Num(); i++) if (FVector2D::Distance(ToScreen(S.RemovedTrees[i]), L) < 6) { Ed->SelRemoved = i; bHit = true; }
		}
		if (!bHit && Ed->bShowBuildings)
			for (int32 b = 0; b < Ed->Buildings.Num(); b++) if (InPoly(Ed->Buildings[b].Pts, W)) { Ed->SelBuilding = b; bHit = true; break; }
		if (!bHit)
		{
			// (the shape on top: the boundary only near its line)
			TArray<int32> Order;
			for (int32 i = 0; i < S.Shapes.Num(); i++) Order.Add(i);
			Order.StableSort([&](int32 A2, int32 B2) { return DrawRank(S.Shapes[A2].Kind) > DrawRank(S.Shapes[B2].Kind); });
			for (int32 i : Order)
			{
				const FOTGSourceShape& Sh = S.Shapes[i];
				bool bIn = false;
				if (Sh.Kind == TEXT("oob")) { for (int32 k = 0; k < Sh.Pts.Num() && !bIn; k++) bIn = SegDist(L, ToScreen(Sh.Pts[k]), ToScreen(Sh.Pts[(k + 1) % Sh.Pts.Num()])) < 6; }
				else bIn = InPoly(Sh.Pts, W);
				if (bIn) { Ed->SelShape = i; break; }
			}
		}
	}
	if (Was != EDrag::None && Was != EDrag::Pan) Ed->Changed();
	else Ed->Changed();
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SOTGEditCanvas::OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent&)
{
	if (Ed && Ed->Mode == SOTGCourseEditor::EMode::Draw) { Ed->FinishDraw(); return FReply::Handled(); }
	return FReply::Unhandled();
}

FReply SOTGEditCanvas::OnMouseWheel(const FGeometry& G, const FPointerEvent& E)
{
	// (zoomed round the point under the mouse)
	const FVector2D L = G.AbsoluteToLocal(E.GetScreenSpacePosition());
	const FVector2D Before = ToWorld(L);
	Scale = FMath::Clamp(Scale * FMath::Pow(1.25, E.GetWheelDelta()), 0.02, 40.0);
	const FVector2D After = ToWorld(L);
	Cx += Before.X - After.X; Cz += Before.Y - After.Y;
	return FReply::Handled();
}

FReply SOTGEditCanvas::OnKeyDown(const FGeometry&, const FKeyEvent& E)
{
	if (!Ed) return FReply::Unhandled();
	const FKey K = E.GetKey();
	using EM = SOTGCourseEditor::EMode;
	if (K == EKeys::Z && E.IsControlDown()) { Ed->Undo(); return FReply::Handled(); }
	if (K == EKeys::Escape) { Ed->SetMode(EM::None); return FReply::Handled(); }
	if (K == EKeys::Enter && Ed->Mode == EM::Draw) { Ed->FinishDraw(); return FReply::Handled(); }
	if (K == EKeys::BackSpace && Ed->Mode == EM::Draw) { if (Ed->DrawPts.Num()) Ed->DrawPts.Pop(); return FReply::Handled(); }
	if (K == EKeys::Delete || K == EKeys::BackSpace) { Ed->DeleteSelected(); return FReply::Handled(); }
	return FReply::Unhandled();
}

FCursorReply SOTGEditCanvas::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	if (Ed && Ed->Mode != SOTGCourseEditor::EMode::None) return FCursorReply::Cursor(EMouseCursor::Crosshairs);
	return FCursorReply::Cursor(Drag == EDrag::Pan && bMoved ? EMouseCursor::GrabHandClosed : EMouseCursor::GrabHand);
}
