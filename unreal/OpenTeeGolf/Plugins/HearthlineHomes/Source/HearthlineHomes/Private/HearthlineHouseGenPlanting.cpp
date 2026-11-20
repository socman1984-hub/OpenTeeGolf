// Copyright __PUBLISHER__. All Rights Reserved.
// Planting and fences for FHearthlineHouseGen.
#include "HearthlineHouseGen.h"
#include "HearthlineHouse.h"
#include "HearthlinePlanting.h"

namespace HS = HearthlineSlot;
using FB = FHearthlineMeshBuilder;

namespace
{
	const FVector PlantUp(0, 0, 1);

	bool SpanHit(const TArray<FVector2D>& Spans, double A, double B)
	{
		for (const FVector2D& S : Spans) { if (A < S.Y && B > S.X) { return true; } }
		return false;
	}
}

bool FHearthlineHouseGen::OnPaving(const FVector& P, double Margin) const
{
	if (bHaveGarage)
	{
		const FVector Rel = P - GarageOrigin;
		const double S = FVector::DotProduct(Rel, GarageDir), Out = FVector::DotProduct(Rel, FVector::CrossProduct(PlantUp, GarageDir));
		if (Out > -10 && S > GarageS0 - 35 - Margin && S < GarageS1 + 35 + Margin) { return true; }
	}
	if (bHaveDoor)
	{
		const FVector Rel = P - DoorOrigin;
		const double S = FVector::DotProduct(Rel, DoorDir), Out = FVector::DotProduct(Rel, FVector::CrossProduct(PlantUp, DoorDir));
		if (Out > -10 && S > DoorS - 60 - Margin && S < DoorS + 60 + Margin) { return true; }
	}
	return false;
}

void FHearthlineHouseGen::EmitPlanting()
{
	FRandomStream R(H.PlantSeed * 9176 + H.Seed * 31 + 5);
	FHearthlinePlanting::FSlots Slots;
	Slots.Bark = HS::Wood; Slots.Foliage = HS::Foliage; Slots.Inner = HS::Grass; Slots.Tint = H.FoliageColor;

	if (H.bFoundationShrubs)
	{
		TArray<FVector2D> Spans; TArray<FOpen> Opens;
		for (int32 vi = 0; vi < Vols.Num(); ++vi)
		{
			const FVol& V = Vols[vi];
			for (int32 Side = 0; Side < 4; ++Side)
			{
				if (Side == 2) { continue; }
				FVector O, Dir, N; double Len;
				SideFrame(V, Side, O, Dir, N, Len);
				Blocked(vi, Side, V.ZBase + 40, Spans);
				CollectOpenings(vi, Side, 0, Len, V.V->StoreyHeight, Spans, Opens);
				for (const FHearthlinePorch& P : H.Porches)
				{
					if (P.Volume == vi && int32(P.Side) == Side) { Spans.Add(FVector2D(FMath::Min(P.Start, P.End) * Len - 25, FMath::Max(P.Start, P.End) * Len + 25)); }
				}
				for (const FOpen& Op : Opens)
				{
					if (Op.Type == EHearthlineOpening::Door || Op.Type == EHearthlineOpening::GarageDoor) { Spans.Add(FVector2D(Op.S0 - 55, Op.S1 + 55)); }
				}
				const double Odds = H.ShrubDensity * (Side == 0 ? 1.0 : 0.45);
				for (double S = 55 + R.FRandRange(0.f, 40.f); S < Len - 45; S += R.FRandRange(95.f, 150.f))
				{
					const bool bWant = R.FRand() < Odds;
					const bool bCone = R.FRand() < 0.14f;
					const double Rad = bCone ? R.FRandRange(28.f, 36.f) : R.FRandRange(30.f, 48.f), Ht = bCone ? R.FRandRange(170.f, 240.f) : R.FRandRange(50.f, 90.f);
					const FVector P = O + Dir * S + N * (48 + R.FRandRange(0.f, 14.f));
					if (!bWant || SpanHit(Spans, S - 40, S + 40) || InsideOther(P + PlantUp * 20, vi) || OnPaving(P, 30)) { continue; }
					FHearthlinePlanting::Shrub(B, R, P, Rad, Ht, bCone, Slots);
				}
			}
		}
	}

	if (H.bLot && H.LotTrees > 0 && Vols.Num() > 0)
	{
		const double X1 = H.FrontYard, X0 = H.FrontYard - H.LotSize.X, Y0 = -H.LotSize.Y * 0.5, Y1 = H.LotSize.Y * 0.5;
		double FrontMost = -1e9;
		for (const FVol& V : Vols) { FrontMost = FMath::Max(FrontMost, V.X1); }
		TArray<FVector> Placed;
		for (int32 i = 0; i < H.LotTrees; ++i)
		{
			for (int32 Try = 0; Try < 40; ++Try)
			{
				const bool bFront = i == 0 && FrontMost + 350 < X1 - 230 && Try < 25;
				const FVector P(bFront ? R.FRandRange(float(FrontMost + 350), float(X1 - 230)) : R.FRandRange(float(X0 + 230), float(X1 - 230)), R.FRandRange(float(Y0 + 230), float(Y1 - 230)), 0);
				bool bOk = !OnPaving(P, 170);
				for (const FVol& V : Vols) { if (P.X > V.X0 - 340 && P.X < V.X1 + 340 && P.Y > V.Y0 - 340 && P.Y < V.Y1 + 340) { bOk = false; } }
				for (const FVector& Q : Placed) { if (FVector::Dist2D(P, Q) < 500) { bOk = false; } }
				if (!bOk) { continue; }
				Placed.Add(P);
				FHearthlinePlanting::Tree(B, R, P, R.FRandRange(float(H.TreeHeight.X), float(FMath::Max(H.TreeHeight.X, H.TreeHeight.Y))), Slots);
				break;
			}
		}
	}
}

void FHearthlineHouseGen::EmitFenceRun(const FVector& From, const FVector& To)
{
	const double L = FVector::Dist2D(From, To);
	if (L < 25) { return; }
	const bool bPicket = H.FenceStyle == EHearthlineFence::Picket;
	const double Ht = H.FenceHeight > 1 ? H.FenceHeight : (bPicket ? 105.0 : 180.0);
	B.Push(FB::WallFrame(From, To - From));
	B.Set(bPicket ? HS::Trim : HS::Wood, H.FenceColor);
	const uint8 NoBottom = FB::All & ~FB::ZN;
	const int32 Bays = FMath::Max(1, FMath::CeilToInt32(L / 240.0));
	for (int32 i = 0; i <= Bays; ++i)
	{
		const double X = L * i / Bays;
		B.Box(FVector(X - 4.5, -9, 0), FVector(X + 4.5, 0, Ht + (bPicket ? 6 : 2)), NoBottom);
		if (bPicket) { B.Box(FVector(X - 5.8, -10.3, Ht + 6), FVector(X + 5.8, 1.3, Ht + 8.5)); }
	}
	B.Box(FVector(0, -4, Ht * 0.2), FVector(L, 0, Ht * 0.2 + 8));
	B.Box(FVector(0, -4, Ht - 24), FVector(L, 0, Ht - 16));
	if (!bPicket) { B.Box(FVector(0, -4, Ht * 0.52), FVector(L, 0, Ht * 0.52 + 8)); }
	const double W = bPicket ? 8.5 : 14.0, Gap = bPicket ? 6.5 : 0.6;
	const int32 Count = FMath::Max(1, FMath::FloorToInt32((L + Gap) / (W + Gap)));
	const double Start = (L - (Count * (W + Gap) - Gap)) * 0.5;
	B.bSwapUV = !bPicket;
	const uint8 BoardFaces = FB::XP | FB::XN | FB::YP | FB::YN | (bPicket ? 0 : FB::ZP);
	for (int32 k = 0; k < Count; ++k)
	{
		const double A = Start + k * (W + Gap), Top = bPicket ? Ht - 8 : Ht;
		B.Box(FVector(A, 0, 4), FVector(A + W, 2, Top), BoardFaces);
		if (bPicket)
		{
			const FVector P0(A, 0, Top), P1(A + W, 0, Top), P2(A + W, 2, Top), P3(A, 2, Top), A0(A + W * 0.5, 0, Ht), A1(A + W * 0.5, 2, Ht);
			B.TriF(P3, P2, A1, FVector(0, 1, 0));
			B.TriF(P0, P1, A0, FVector(0, -1, 0));
			B.QuadF(P0, P3, A1, A0, FVector(-1, 0, 1));
			B.QuadF(P1, P2, A1, A0, FVector(1, 0, 1));
		}
	}
	B.bSwapUV = false;
	B.Pop();
}

void FHearthlineHouseGen::EmitFence()
{
	if (!H.bLot || !H.bFence || Vols.Num() == 0) { return; }
	const double In = 8, X0 = H.FrontYard - H.LotSize.X + In, Y0 = -H.LotSize.Y * 0.5 + In, Y1 = H.LotSize.Y * 0.5 - In;
	double BY0 = 1e9, BY1 = -1e9;
	for (const FVol& V : Vols) { BY0 = FMath::Min(BY0, V.Y0); BY1 = FMath::Max(BY1, V.Y1); }
	const double Xf = FMath::Max((Vols[0].X0 + Vols[0].X1) * 0.5, X0 + 100);
	if (H.FenceSides & int32(EHearthlineSideMask::Right)) { EmitFenceRun(FVector(Xf, Y1, 0), FVector(X0, Y1, 0)); }
	if (H.FenceSides & int32(EHearthlineSideMask::Back)) { EmitFenceRun(FVector(X0, Y1, 0), FVector(X0, Y0, 0)); }
	if (H.FenceSides & int32(EHearthlineSideMask::Left)) { EmitFenceRun(FVector(X0, Y0, 0), FVector(Xf, Y0, 0)); }
	// returns from the lot edges to the sides of the house close the yard
	EmitFenceRun(FVector(Xf, Y1, 0), FVector(Xf, BY1, 0));
	EmitFenceRun(FVector(Xf, BY0, 0), FVector(Xf, Y0, 0));
}
