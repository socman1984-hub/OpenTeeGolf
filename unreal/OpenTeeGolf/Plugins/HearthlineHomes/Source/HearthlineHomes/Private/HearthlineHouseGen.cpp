// Copyright __PUBLISHER__. All Rights Reserved.
#include "HearthlineHouseGen.h"
#include "HearthlineHouse.h"
#include "HearthlinePlanting.h"

namespace HS = HearthlineSlot;
using FB = FHearthlineMeshBuilder;

namespace
{
	const double Reveal = 9.0;
	const uint8 NoBack = FB::All & ~FB::YN;
	const FVector Up(0, 0, 1);

	double TanDeg(double Deg) { return FMath::Tan(FMath::DegreesToRadians(Deg)); }

	/** Sorts spans and merges the ones that touch. */
	void MergeSpans(TArray<FVector2D>& Spans)
	{
		Spans.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });
		TArray<FVector2D> Out;
		for (const FVector2D& S : Spans)
		{
			if (Out.Num() > 0 && S.X <= Out.Last().Y) { Out.Last().Y = FMath::Max(Out.Last().Y, S.Y); }
			else { Out.Add(S); }
		}
		Spans = MoveTemp(Out);
	}

	bool Overlaps(const TArray<FVector2D>& Spans, double A, double B)
	{
		for (const FVector2D& S : Spans) { if (A < S.Y && B > S.X) { return true; } }
		return false;
	}
}

FHearthlineHouseGen::FHearthlineHouseGen(const AHearthlineHouse& InHouse) : H(InHouse)
{
	Rng.Initialize(H.Seed * 7919 + 17);
}

void FHearthlineHouseGen::SideFrame(const FVol& V, int32 Side, FVector& Origin, FVector& Dir, FVector& N, double& Len)
{
	switch (Side & 3)
	{
	case 0: N = FVector(1, 0, 0);  Origin = FVector(V.X1, V.Y1, 0); Dir = FVector(0, -1, 0); Len = V.Y1 - V.Y0; break;
	case 1: N = FVector(0, 1, 0);  Origin = FVector(V.X0, V.Y1, 0); Dir = FVector(1, 0, 0);  Len = V.X1 - V.X0; break;
	case 2: N = FVector(-1, 0, 0); Origin = FVector(V.X0, V.Y0, 0); Dir = FVector(0, 1, 0);  Len = V.Y1 - V.Y0; break;
	default: N = FVector(0, -1, 0); Origin = FVector(V.X1, V.Y0, 0); Dir = FVector(-1, 0, 0); Len = V.X1 - V.X0; break;
	}
}

bool FHearthlineHouseGen::InsideOther(const FVector& P, int32 Self, int32* OutOther) const
{
	for (int32 j = 0; j < Vols.Num(); ++j)
	{
		if (j == Self) { continue; }
		const FVol& O = Vols[j];
		if (P.X > O.X0 + 1 && P.X < O.X1 - 1 && P.Y > O.Y0 + 1 && P.Y < O.Y1 - 1 && P.Z < O.ZTop)
		{
			if (OutOther) { *OutOther = j; }
			return true;
		}
	}
	return false;
}

void FHearthlineHouseGen::Blocked(int32 VolIndex, int32 Side, double ZMid, TArray<FVector2D>& Out) const
{
	Out.Reset();
	FVector O, Dir, N; double Len;
	SideFrame(Vols[VolIndex], Side, O, Dir, N, Len);
	const FVector P0 = O + N * 2.0;
	for (int32 j = 0; j < Vols.Num(); ++j)
	{
		if (j == VolIndex) { continue; }
		const FVol& V = Vols[j];
		if (ZMid > V.ZTop) { continue; }
		double A, C;
		if (FMath::Abs(Dir.X) > 0.5)
		{
			if (P0.Y < V.Y0 - 1 || P0.Y > V.Y1 + 1) { continue; }
			A = (V.X0 - P0.X) / Dir.X; C = (V.X1 - P0.X) / Dir.X;
		}
		else
		{
			if (P0.X < V.X0 - 1 || P0.X > V.X1 + 1) { continue; }
			A = (V.Y0 - P0.Y) / Dir.Y; C = (V.Y1 - P0.Y) / Dir.Y;
		}
		const double S0 = FMath::Max(0.0, FMath::Min(A, C)), S1 = FMath::Min(Len, FMath::Max(A, C));
		if (S1 - S0 > 1.0) { Out.Add(FVector2D(S0, S1)); }
	}
	MergeSpans(Out);
}

void FHearthlineHouseGen::CladdingFor(const FVol& V, int32 Storey, bool bGable, int32& Mat, FLinearColor& Col) const
{
	EHearthlineCladding C = V.V->bOverrideCladding ? V.V->Cladding : H.Cladding;
	Col = V.V->bOverrideColor ? V.V->WallColor : H.WallColor;
	const bool bAccent = (H.Accent == EHearthlineAccent::UpperStoreys && (Storey >= 1 || bGable)) || (H.Accent == EHearthlineAccent::Gables && bGable);
	if (bAccent) { C = H.AccentCladding; Col = H.AccentColor; }
	Mat = int32(C);
}

void FHearthlineHouseGen::CollectOpenings(int32 VolIndex, int32 Side, int32 Storey, double L, double Height, const TArray<FVector2D>& BlockedSpans, TArray<FOpen>& Out) const
{
	Out.Reset();
	const FVol& V = Vols[VolIndex];
	TArray<FVector2D> Taken;

	auto TryAdd = [&](double Centre, double W, double Z0, double Z1, EHearthlineOpening Type, bool bFront) -> bool
	{
		if (W > L - 40) { return false; }
		Centre = FMath::Clamp(Centre, W * 0.5 + 20, L - W * 0.5 - 20);
		const double S0 = Centre - W * 0.5, S1 = Centre + W * 0.5;
		if (Overlaps(BlockedSpans, S0 - 15, S1 + 15) || Overlaps(Taken, S0 - 12, S1 + 12)) { return false; }
		FOpen O; O.S0 = S0; O.S1 = S1; O.Z0 = Z0; O.Z1 = FMath::Min(Z1, Height - 12); O.Type = Type; O.bFrontDoor = bFront;
		if (O.Z1 - O.Z0 < 20) { return false; }
		Out.Add(O);
		Taken.Add(FVector2D(S0, S1));
		return true;
	};

	const double WinH = FMath::Min<double>(H.WindowHeight, Height - 45);
	const double Sill = FMath::Clamp<double>(H.SillHeight, 5, Height - WinH - 28);

	if (Storey == 0 && H.bFrontDoor && H.DoorVolume == VolIndex && int32(H.DoorSide) == Side)
	{
		const double W = H.DoorWidth + (H.bSidelights ? 84.0 : 0.0);
		TryAdd(H.DoorPosition * L, W, 0, H.DoorHeight, EHearthlineOpening::Door, true);
	}
	if (Storey == 0 && H.bGarage && H.GarageVolume == VolIndex && int32(H.GarageSide) == Side)
	{
		const int32 N = FMath::Clamp(H.GarageDoors, 1, 3);
		const double Gap = 45.0, Total = N * H.GarageDoorWidth + (N - 1) * Gap;
		const double Start = FMath::Clamp(H.GaragePosition * L - Total * 0.5, 25.0, FMath::Max(25.0, L - Total - 25.0));
		for (int32 i = 0; i < N; ++i)
		{
			const double C = Start + i * (H.GarageDoorWidth + Gap) + H.GarageDoorWidth * 0.5;
			TryAdd(C, H.GarageDoorWidth, -V.ZBase, H.GarageDoorHeight - V.ZBase, EHearthlineOpening::GarageDoor, false);
		}
	}
	for (const FHearthlineOpeningSpec& Spec : H.Openings)
	{
		if (Spec.Volume != VolIndex || int32(Spec.Side) != Side || Spec.Storey != Storey) { continue; }
		switch (Spec.Type)
		{
		case EHearthlineOpening::Window:
		{
			const double W = Spec.Width > 0 ? Spec.Width : H.WindowWidth, Ht = Spec.Height > 0 ? FMath::Min<double>(Spec.Height, Height - 30) : WinH;
			const double Top = FMath::Min(Sill + WinH, Height - 14);
			TryAdd(Spec.Position * L, W, FMath::Max(4.0, Top - Ht), Top, EHearthlineOpening::Window, false);
			break;
		}
		case EHearthlineOpening::Door:
			TryAdd(Spec.Position * L, Spec.Width > 0 ? Spec.Width : H.DoorWidth, 0, Spec.Height > 0 ? Spec.Height : H.DoorHeight, EHearthlineOpening::Door, false);
			break;
		case EHearthlineOpening::GarageDoor:
			if (Storey == 0)
			{
				TryAdd(Spec.Position * L, Spec.Width > 0 ? Spec.Width : H.GarageDoorWidth, -V.ZBase, (Spec.Height > 0 ? Spec.Height : H.GarageDoorHeight) - V.ZBase, EHearthlineOpening::GarageDoor, false);
			}
			break;
		default:
			TryAdd(Spec.Position * L, Spec.Width > 0 ? Spec.Width : 100.0, 0, Height, EHearthlineOpening::Blank, false);
			break;
		}
	}

	if ((V.V->WindowSides & (1 << Side)) != 0 && WinH > 30)
	{
		const double Extra = H.CasingWidth + (H.bShutters ? FMath::Min<double>(H.WindowWidth * 0.5, 45.0) : 0.0);
		const double Need = H.WindowWidth + 2 * Extra;
		TArray<FVector2D> Forbidden;
		Forbidden.Add(FVector2D(-1e6, 45));
		Forbidden.Add(FVector2D(L - 45, 1e6));
		for (const FVector2D& S : BlockedSpans) { Forbidden.Add(FVector2D(S.X - 45, S.Y + 45)); }
		for (const FVector2D& S : Taken) { Forbidden.Add(FVector2D(S.X - 40, S.Y + 40)); }
		MergeSpans(Forbidden);
		for (int32 i = 0; i + 1 < Forbidden.Num(); ++i)
		{
			const double A = Forbidden[i].Y, C = Forbidden[i + 1].X, U = C - A;
			if (U < Need) { continue; }
			const int32 MaxN = FMath::Max(1, FMath::FloorToInt32((U + 30) / (Need + 30)));
			const int32 N = FMath::Clamp(FMath::RoundToInt32(U / FMath::Max<double>(H.WindowSpacing, 60.0)), 1, MaxN);
			for (int32 k = 0; k < N; ++k)
			{
				FOpen O; const double Centre = A + U * (k + 0.5) / N;
				O.S0 = Centre - H.WindowWidth * 0.5; O.S1 = Centre + H.WindowWidth * 0.5; O.Z0 = Sill; O.Z1 = Sill + WinH; O.Type = EHearthlineOpening::Window;
				Out.Add(O);
			}
		}
	}
	Out.Sort([](const FOpen& A, const FOpen& C) { return A.S0 < C.S0; });
}

// ---------------------------------------------------------------------------------------------------------------
// Walls and openings (all in a wall frame: X along the wall, Y out of it, Z up)
// ---------------------------------------------------------------------------------------------------------------

void FHearthlineHouseGen::EmitWallSkin(double L, double Height, const TArray<FOpen>& Opens)
{
	double Cur = 0;
	for (const FOpen& O : Opens)
	{
		if (O.Type == EHearthlineOpening::Blank) { continue; }
		B.WallQuad(Cur, O.S0, 0, Height);
		if (O.Z0 > 0) { B.WallQuad(O.S0, O.S1, 0, O.Z0); }
		if (O.Z1 < Height) { B.WallQuad(O.S0, O.S1, FMath::Max(O.Z1, 0.0), Height); }
		Cur = O.S1;
	}
	B.WallQuad(Cur, L, 0, Height);
}

void FHearthlineHouseGen::EmitReveal(double S0, double S1, double Z0, double Z1, double D)
{
	B.QuadF(FVector(S0, -D, Z0), FVector(S0, 0, Z0), FVector(S0, 0, Z1), FVector(S0, -D, Z1), FVector(1, 0, 0));
	B.QuadF(FVector(S1, -D, Z0), FVector(S1, 0, Z0), FVector(S1, 0, Z1), FVector(S1, -D, Z1), FVector(-1, 0, 0));
	B.QuadF(FVector(S0, -D, Z0), FVector(S1, -D, Z0), FVector(S1, 0, Z0), FVector(S0, 0, Z0), FVector(0, 0, 1));
	B.QuadF(FVector(S0, -D, Z1), FVector(S1, -D, Z1), FVector(S1, 0, Z1), FVector(S0, 0, Z1), FVector(0, 0, -1));
}

void FHearthlineHouseGen::EmitCasing(double S0, double S1, double Z0, double Z1, bool bSill)
{
	const double Cw = H.CasingWidth, Ct = 2.5;
	B.Set(HS::Trim, H.TrimColor);
	if (Cw > 0.5)
	{
		B.Box(FVector(S0 - Cw, 0, Z0), FVector(S0, Ct, Z1), NoBack);
		B.Box(FVector(S1, 0, Z0), FVector(S1 + Cw, Ct, Z1), NoBack);
		B.Box(FVector(S0 - Cw - 2, 0, Z1), FVector(S1 + Cw + 2, Ct + 1, Z1 + Cw * 1.2), NoBack);
		if (bSill) { B.Box(FVector(S0 - Cw - 3, 0, Z0 - 4.5), FVector(S1 + Cw + 3, 5.5, Z0), NoBack); }
	}
	else if (bSill)
	{
		B.Box(FVector(S0 - 2, 0, Z0 - 4), FVector(S1 + 2, 4, Z0), NoBack);
	}
}

void FHearthlineHouseGen::EmitGlass(double S0, double S1, double Z0, double Z1, double Y)
{
	static const FLinearColor Backings[4] = { FLinearColor(0.85f, 0.83f, 0.78f), FLinearColor(0.7f, 0.72f, 0.75f), FLinearColor(0.9f, 0.88f, 0.8f), FLinearColor(0.6f, 0.58f, 0.55f) };
	FLinearColor C = Backings[Rng.RandRange(0, 3)];
	C.A = Rng.FRand() < H.LitWindowFraction ? 0.65f : 0.f;
	B.Set(HS::Glass, C);
	B.UVOffset = FVector2f(Rng.FRand(), Rng.FRand());
	B.WallQuad(S0, S1, Z0, Z1, Y);
	B.UVOffset = FVector2f::ZeroVector;
}

void FHearthlineHouseGen::EmitWindow(const FOpen& O, bool bAllowShutters)
{
	const double R = Reveal, Fw = 5.0;
	EmitCasing(O.S0, O.S1, O.Z0, O.Z1, true);
	B.Set(HS::Trim, H.TrimColor);
	EmitReveal(O.S0, O.S1, O.Z0, O.Z1, R);

	B.Set(HS::Trim, H.SashColor);
	B.Box(FVector(O.S0, -R, O.Z0), FVector(O.S0 + Fw, -R + 4, O.Z1), NoBack);
	B.Box(FVector(O.S1 - Fw, -R, O.Z0), FVector(O.S1, -R + 4, O.Z1), NoBack);
	B.Box(FVector(O.S0 + Fw, -R, O.Z0), FVector(O.S1 - Fw, -R + 4, O.Z0 + Fw), NoBack);
	B.Box(FVector(O.S0 + Fw, -R, O.Z1 - Fw), FVector(O.S1 - Fw, -R + 4, O.Z1), NoBack);
	const double G0 = O.S0 + Fw, G1 = O.S1 - Fw, Zb = O.Z0 + Fw, Zt = O.Z1 - Fw;
	EmitGlass(G0, G1, Zb, Zt, -R + 1.5);

	B.Set(HS::Trim, H.SashColor);
	TArray<FVector2D> Regions;
	if (H.bDoubleHung && (Zt - Zb) > 80)
	{
		const double Mid = (Zb + Zt) * 0.5;
		B.Box(FVector(G0, -R + 1.5, Mid - 2.5), FVector(G1, -R + 4.5, Mid + 2.5), NoBack);
		Regions.Add(FVector2D(Zb, Mid - 2.5)); Regions.Add(FVector2D(Mid + 2.5, Zt));
	}
	else { Regions.Add(FVector2D(Zb, Zt)); }
	// wide windows get proportionally more glazing bars so panes stay upright
	const int32 Cols = H.MuntinColumns > 0 ? FMath::Max(H.MuntinColumns, FMath::RoundToInt32((G1 - G0) / 95.0 * (H.MuntinColumns + 1)) - 1) : 0;
	for (const FVector2D& Rg : Regions)
	{
		for (int32 c = 1; c <= Cols; ++c)
		{
			const double X = G0 + (G1 - G0) * c / (Cols + 1);
			B.Box(FVector(X - 1, -R + 1.5, Rg.X), FVector(X + 1, -R + 3, Rg.Y), NoBack);
		}
		for (int32 r = 1; r <= H.MuntinRows; ++r)
		{
			const double Z = Rg.X + (Rg.Y - Rg.X) * r / (H.MuntinRows + 1);
			B.Box(FVector(G0, -R + 1.5, Z - 1), FVector(G1, -R + 3, Z + 1), NoBack);
		}
	}

	if (H.bShutters && bAllowShutters)
	{
		const double Sw = FMath::Min((O.S1 - O.S0) * 0.5, 45.0), Cw = H.CasingWidth;
		B.Set(HS::Trim, H.ShutterColor);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const double A = Side == 0 ? O.S0 - Cw - Sw - 1 : O.S1 + Cw + 1, C = A + Sw;
			B.Box(FVector(A, 0, O.Z0), FVector(C, 1.2, O.Z1), NoBack);
			B.Box(FVector(A, 1.2, O.Z0), FVector(A + 5, 3.2, O.Z1), NoBack);
			B.Box(FVector(C - 5, 1.2, O.Z0), FVector(C, 3.2, O.Z1), NoBack);
			B.Box(FVector(A + 5, 1.2, O.Z0), FVector(C - 5, 3.2, O.Z0 + 6), NoBack);
			B.Box(FVector(A + 5, 1.2, O.Z1 - 6), FVector(C - 5, 3.2, O.Z1), NoBack);
			const double Mid = (O.Z0 + O.Z1) * 0.5;
			B.Box(FVector(A + 5, 1.2, Mid - 3), FVector(C - 5, 3.2, Mid + 3), NoBack);
			for (double Z = O.Z0 + 7.5; Z < O.Z1 - 9; Z += 5.0)
			{
				if (Z > Mid - 6 && Z < Mid + 3) { continue; }
				B.Box(FVector(A + 5, 1.2, Z), FVector(C - 5, 2.6, Z + 3.2), FB::YP | FB::ZP | FB::ZN);
			}
		}
	}
}

void FHearthlineHouseGen::EmitLantern(double S, double Z)
{
	const FLinearColor Dark(0.03f, 0.03f, 0.035f);
	B.Set(HS::Metal, Dark);
	B.Box(FVector(S - 5, 0, Z - 11), FVector(S + 5, 1.5, Z + 11), NoBack);
	B.Box(FVector(S - 2, 1.5, Z + 6), FVector(S + 2, 6, Z + 9), NoBack);
	B.Box(FVector(S - 6, 3, Z + 7), FVector(S + 6, 13, Z + 9.5));
	B.Box(FVector(S - 5, 4, Z - 9), FVector(S + 5, 12, Z - 7.5));
	FLinearColor Lamp(1.f, 0.86f, 0.6f, 1.f);
	B.Set(HS::Glass, Lamp);
	B.Box(FVector(S - 4, 5, Z - 7.5), FVector(S + 4, 11, Z + 7), FB::All & ~(FB::ZP | FB::ZN));
}

void FHearthlineHouseGen::EmitDoor(const FOpen& O)
{
	const double R = Reveal;
	EmitCasing(O.S0, O.S1, O.Z0, O.Z1, false);
	B.Set(HS::Trim, H.TrimColor);
	EmitReveal(O.S0, O.S1, O.Z0, O.Z1, R);
	B.Set(HS::Metal, FLinearColor(0.35f, 0.35f, 0.36f));
	B.Box(FVector(O.S0, -R, O.Z0), FVector(O.S1, 3, O.Z0 + 2.5), NoBack);

	double D0 = O.S0, D1 = O.S1;
	const double Base = O.Z0 + 2.5;
	if (O.bFrontDoor && H.bSidelights && (O.S1 - O.S0) > H.DoorWidth + 60)
	{
		const double Sl = 36, Mu = 6;
		D0 = O.S0 + Sl + Mu; D1 = O.S1 - Sl - Mu;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const double A = Side == 0 ? O.S0 : D1 + Mu, C = A + Sl, M0 = Side == 0 ? D0 - Mu : D1;
			B.Set(HS::Trim, H.TrimColor);
			B.Box(FVector(M0, -R, Base), FVector(M0 + Mu, 0, O.Z1), NoBack);
			B.Box(FVector(A, -R, Base), FVector(C, -R + 4, Base + 62), NoBack);
			B.Box(FVector(A + 5, -R + 4, Base + 8), FVector(C - 5, -R + 5, Base + 54), NoBack);
			B.Box(FVector(A, -R, Base + 62), FVector(A + 4, -R + 4, O.Z1), NoBack);
			B.Box(FVector(C - 4, -R, Base + 62), FVector(C, -R + 4, O.Z1), NoBack);
			B.Box(FVector(A + 4, -R, O.Z1 - 4), FVector(C - 4, -R + 4, O.Z1), NoBack);
			EmitGlass(A + 4, C - 4, Base + 62, O.Z1 - 4, -R + 1.5);
		}
	}

	const double YF = -R + 4.5, Ht = O.Z1 - Base, W = D1 - D0;
	B.Set(HS::Trim, H.DoorColor);
	B.Box(FVector(D0, -R, Base), FVector(D1, YF, O.Z1), NoBack);
	auto Panel = [&](double A, double C, double F0, double F1)
	{
		B.Box(FVector(A, YF, Base + Ht * F0), FVector(C, YF + 1.2, Base + Ht * F1), NoBack);
		B.Box(FVector(A + 3.5, YF + 1.2, Base + Ht * F0 + 3.5), FVector(C - 3.5, YF + 2.0, Base + Ht * F1 - 3.5), NoBack);
	};
	const double Mid = (D0 + D1) * 0.5;
	switch (H.DoorStyle)
	{
	case EHearthlineDoorStyle::SixPanel:
		for (int32 c = 0; c < 2; ++c)
		{
			const double A = c == 0 ? D0 + 11 : Mid + 4.5, C = c == 0 ? Mid - 4.5 : D1 - 11;
			Panel(A, C, 0.80, 0.94); Panel(A, C, 0.42, 0.76); Panel(A, C, 0.09, 0.38);
		}
		break;
	case EHearthlineDoorStyle::HalfGlass:
	{
		const double A = D0 + 13, C = D1 - 13, Z0 = Base + Ht * 0.5, Z1 = Base + Ht * 0.92;
		EmitGlass(A, C, Z0, Z1, YF + 0.3);
		B.Set(HS::Trim, H.DoorColor);
		for (int32 i = 1; i <= 2; ++i)
		{
			const double X = A + (C - A) * i / 3.0, Z = Z0 + (Z1 - Z0) * i / 3.0;
			B.Box(FVector(X - 1, YF, Z0), FVector(X + 1, YF + 1.5, Z1), NoBack);
			B.Box(FVector(A, YF, Z - 1), FVector(C, YF + 1.5, Z + 1), NoBack);
		}
		Panel(D0 + 11, Mid - 4.5, 0.09, 0.43); Panel(Mid + 4.5, D1 - 11, 0.09, 0.43);
		break;
	}
	case EHearthlineDoorStyle::Craftsman:
	{
		const double A = D0 + 11, C = D1 - 11, Z0 = Base + Ht * 0.75, Z1 = Base + Ht * 0.92, Lw = (C - A - 8) / 3.0;
		for (int32 i = 0; i < 3; ++i) { EmitGlass(A + i * (Lw + 4), A + i * (Lw + 4) + Lw, Z0, Z1, YF + 0.3); }
		B.Set(HS::Trim, H.DoorColor);
		B.Box(FVector(D0 + 6, YF, Base + Ht * 0.70 - 3), FVector(D1 - 6, YF + 3.5, Base + Ht * 0.70), NoBack);
		Panel(D0 + 11, Mid - 4.5, 0.09, 0.64); Panel(Mid + 4.5, D1 - 11, 0.09, 0.64);
		break;
	}
	default:
		EmitGlass(D0 + 15, D0 + 29, Base + Ht * 0.12, Base + Ht * 0.9, YF + 0.3);
		break;
	}
	const FLinearColor Brass(0.55f, 0.42f, 0.2f), Steel(0.5f, 0.5f, 0.52f);
	if (H.DoorStyle == EHearthlineDoorStyle::Modern)
	{
		B.Set(HS::Metal, Steel);
		B.Box(FVector(D1 - 14, YF + 3, Base + Ht * 0.3), FVector(D1 - 11, YF + 6, Base + Ht * 0.62));
		B.Box(FVector(D1 - 13.5, YF, Base + Ht * 0.34), FVector(D1 - 11.5, YF + 3, Base + Ht * 0.36));
		B.Box(FVector(D1 - 13.5, YF, Base + Ht * 0.56), FVector(D1 - 11.5, YF + 3, Base + Ht * 0.58));
	}
	else
	{
		B.Set(HS::Metal, Brass);
		B.Box(FVector(D1 - 11, YF, Base + 88), FVector(D1 - 4, YF + 0.8, Base + 112), NoBack);
		B.Box(FVector(D1 - 9, YF + 0.8, Base + 94), FVector(D1 - 6, YF + 4, Base + 97));
		B.Box(FVector(D1 - 10.5, YF + 4, Base + 92.5), FVector(D1 - 4.5, YF + 7, Base + 98.5));
		B.Box(FVector(D1 - 9.5, YF + 0.8, Base + 105), FVector(D1 - 5.5, YF + 2.5, Base + 109));
	}
	if (O.bFrontDoor && H.bWallLights) { EmitLantern(O.S1 + H.CasingWidth + 24, O.Z0 + 168); }
}

void FHearthlineHouseGen::EmitGarageDoor(const FOpen& O)
{
	const double R = 10.0;
	EmitCasing(O.S0, O.S1, O.Z0, O.Z1, false);
	B.Set(HS::Trim, H.TrimColor);
	EmitReveal(O.S0, O.S1, O.Z0, O.Z1, R);
	B.Set(HS::Metal, FLinearColor(0.02f, 0.02f, 0.02f));
	B.WallQuad(O.S0, O.S1, O.Z0, O.Z1, -R + 0.3);
	const int32 Sections = 4;
	const double Sh = (O.Z1 - O.Z0) / Sections, W = O.S1 - O.S0;
	const int32 N = FMath::Max(2, FMath::RoundToInt32(W / 118.0));
	const double Gap = 9.0, Pw = (W - Gap * (N + 1)) / N;
	for (int32 i = 0; i < Sections; ++i)
	{
		const double Za = O.Z0 + i * Sh + 0.5, Zb = O.Z0 + (i + 1) * Sh - 0.5;
		B.Set(HS::Trim, H.GarageDoorColor);
		B.Box(FVector(O.S0, -R, Za), FVector(O.S1, -R + 4, Zb), NoBack | FB::ZP | FB::ZN);
		for (int32 p = 0; p < N; ++p)
		{
			const double A = O.S0 + Gap + p * (Pw + Gap), C = A + Pw;
			if (i == Sections - 1 && H.bGarageWindows)
			{
				EmitGlass(A + 3, C - 3, Za + 10, Zb - 10, -R + 4.4);
				B.Set(HS::Trim, H.GarageDoorColor);
				B.Box(FVector(A, -R + 4, Za + 7), FVector(A + 3, -R + 5.2, Zb - 7), NoBack);
				B.Box(FVector(C - 3, -R + 4, Za + 7), FVector(C, -R + 5.2, Zb - 7), NoBack);
				B.Box(FVector(A + 3, -R + 4, Za + 7), FVector(C - 3, -R + 5.2, Za + 10), NoBack);
				B.Box(FVector(A + 3, -R + 4, Zb - 10), FVector(C - 3, -R + 5.2, Zb - 7), NoBack);
			}
			else
			{
				B.Box(FVector(A, -R + 4, Za + 7), FVector(C, -R + 4.9, Zb - 7), NoBack);
				B.Box(FVector(A + 4, -R + 4.9, Za + 11), FVector(C - 4, -R + 5.6, Zb - 11), NoBack);
			}
		}
	}
	B.Set(HS::Metal, FLinearColor(0.5f, 0.5f, 0.52f));
	B.Box(FVector((O.S0 + O.S1) * 0.5 - 9, -R + 4, O.Z0 + Sh * 0.42), FVector((O.S0 + O.S1) * 0.5 + 9, -R + 7, O.Z0 + Sh * 0.42 + 3));
	if (H.bWallLights) { EmitLantern(O.S1 + H.CasingWidth + 26, O.Z0 + 195); }
}

void FHearthlineHouseGen::EmitSteps(double SCentre, double Width, double YStart, double TopZ, int32 Mat, const FLinearColor& Col, double& OutRun)
{
	const int32 N = FMath::Max(1, FMath::CeilToInt32(TopZ / 18.5));
	const double Riser = TopZ / N, Tread = 29.0;
	B.Set(Mat, Col);
	for (int32 i = 1; i < N; ++i)
	{
		B.Box(FVector(SCentre - Width * 0.5, YStart + (i - 1) * Tread, 0), FVector(SCentre + Width * 0.5, YStart + i * Tread, TopZ - i * Riser), FB::All & ~(FB::ZN | FB::YN));
	}
	OutRun = (N - 1) * Tread;
}

void FHearthlineHouseGen::EmitVolumeWalls(int32 VolIndex)
{
	const FVol& V = Vols[VolIndex];
	const double SH = V.V->StoreyHeight;
	TArray<FVector2D> Spans;
	TArray<FOpen> Opens, GroundOpens;
	int32 BaseMat; FLinearColor BaseCol;
	CladdingFor(V, 0, false, BaseMat, BaseCol);
	const bool bBoards = BaseMat == HS::LapSiding || BaseMat == HS::BoardAndBatten;

	for (int32 Side = 0; Side < 4; ++Side)
	{
		FVector O, Dir, N; double Len;
		SideFrame(V, Side, O, Dir, N, Len);
		GroundOpens.Reset();
		int32 PrevMat = -1;
		for (int32 St = 0; St < V.V->Storeys; ++St)
		{
			const double Zf = V.ZBase + St * SH;
			Blocked(VolIndex, Side, Zf + SH * 0.5, Spans);
			CollectOpenings(VolIndex, Side, St, Len, SH, Spans, Opens);
			int32 Mat; FLinearColor Col;
			CladdingFor(V, St, false, Mat, Col);
			B.Push(FB::WallFrame(O + Up * Zf, Dir));
			B.Set(Mat, Col);
			EmitWallSkin(Len, SH, Opens);
			for (const FOpen& Op : Opens)
			{
				switch (Op.Type)
				{
				case EHearthlineOpening::Window: EmitWindow(Op, true); break;
				case EHearthlineOpening::Door: EmitDoor(Op); break;
				case EHearthlineOpening::GarageDoor: EmitGarageDoor(Op); break;
				default: break;
				}
			}
			if (St > 0 && (H.bBandBoards || Mat != PrevMat))
			{
				B.Set(HS::Trim, H.TrimColor);
				B.Box(FVector(-2.5, 0, -9), FVector(Len + 2.5, 2.5, 9), NoBack);
			}
			if (St == V.V->Storeys - 1 && H.bCornerBoards && bBoards && V.Roof != EHearthlineRoof::Flat)
			{
				B.Set(HS::Trim, H.TrimColor);
				B.Box(FVector(0, 0, SH - 15), FVector(Len, 2.2, SH), NoBack);
			}
			B.Pop();
			PrevMat = Mat;
			if (St == 0) { GroundOpens = Opens; }
		}

		// foundation band with gaps at garage doors, plus stoops
		B.Push(FB::WallFrame(O, Dir));
		{
			TArray<FVector2D> Cuts;
			for (const FOpen& Op : GroundOpens) { if (Op.Type == EHearthlineOpening::GarageDoor) { Cuts.Add(FVector2D(Op.S0, Op.S1)); } }
			B.Set(int32(H.FoundationCladding), H.FoundationColor);
			double Cur = -3;
			auto Band = [&](double A, double C)
			{
				if (C - A < 0.5 || V.ZBase < 0.5) { return; }
				B.WallQuad(A, C, 0, V.ZBase, 3);
				B.QuadF(FVector(A, 0, V.ZBase), FVector(C, 0, V.ZBase), FVector(C, 3, V.ZBase), FVector(A, 3, V.ZBase), Up);
			};
			for (const FVector2D& Cut : Cuts) { Band(Cur, Cut.X); Cur = Cut.Y; }
			Band(Cur, Len + 3);

			for (const FOpen& Op : GroundOpens)
			{
				if (Op.Type == EHearthlineOpening::GarageDoor)
				{
					if (!bHaveGarage) { bHaveGarage = true; GarageOrigin = O; GarageDir = Dir; GarageS0 = Op.S0; GarageS1 = Op.S1; }
					else if (GarageOrigin.Equals(O)) { GarageS0 = FMath::Min(GarageS0, Op.S0); GarageS1 = FMath::Max(GarageS1, Op.S1); }
				}
				if (Op.Type != EHearthlineOpening::Door) { continue; }
				const double Sc = (Op.S0 + Op.S1) * 0.5;
				bool bPorch = false;
				for (const FHearthlinePorch& P : H.Porches)
				{
					if (P.Volume == VolIndex && int32(P.Side) == Side && Sc >= P.Start * Len - 1 && Sc <= P.End * Len + 1) { bPorch = true; }
				}
				double Out = 0;
				if (!bPorch && V.ZBase > 6)
				{
					const double Sw = (Op.S1 - Op.S0) + 70, Zd = V.ZBase - 2;
					const FLinearColor StoopCol(0.7f, 0.7f, 0.68f);
					B.Set(HS::Concrete, StoopCol);
					B.Box(FVector(Sc - Sw * 0.5, 0, 0), FVector(Sc + Sw * 0.5, 115, Zd), FB::All & ~(FB::ZN | FB::YN));
					double Run = 0;
					EmitSteps(Sc, Sw, 115, Zd, HS::Concrete, StoopCol, Run);
					Out = 115 + Run;
				}
				if (Op.bFrontDoor) { bHaveDoor = true; DoorOrigin = O; DoorDir = Dir; DoorS = Sc; DoorOut = Out; }
			}
		}
		B.Pop();
	}

	if (H.bCornerBoards && bBoards)
	{
		B.Set(HS::Trim, H.TrimColor);
		const double Xs[2] = { V.X0, V.X1 }, Ys[2] = { V.Y0, V.Y1 };
		for (int32 i = 0; i < 2; ++i) for (int32 j = 0; j < 2; ++j)
		{
			const double Sx = i == 0 ? -1 : 1, Sy = j == 0 ? -1 : 1;
			if (InsideOther(FVector(Xs[i] + Sx * 3, Ys[j] + Sy * 3, V.ZBase + 10), VolIndex)) { continue; }
			B.Box(FVector(Xs[i] - Sx * 9, Ys[j] - Sy * 9, V.ZBase), FVector(Xs[i] + Sx * 2.5, Ys[j] + Sy * 2.5, V.ZTop));
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Roofs (in a roof frame: X along the ridge, Y across the span, origin on the ground under the volume centre)
// ---------------------------------------------------------------------------------------------------------------

void FHearthlineHouseGen::EmitDownspout(const FVector& Top, const FVector& WallPoint)
{
	B.Set(HS::Metal, H.GutterColor);
	B.Beam(Top, WallPoint, 7, 6);
	B.Box(FVector(WallPoint.X - 3.5, WallPoint.Y - 3.5, 0), FVector(WallPoint.X + 3.5, WallPoint.Y + 3.5, WallPoint.Z + 3));
}

void FHearthlineHouseGen::EmitGable(const FGableArgs& A)
{
	const double T = TanDeg(A.Pitch), Rise = A.S * 0.5 * T, Zr = A.Z0 + Rise, Fd = A.FasciaDepth;
	const double Xa = -A.L * 0.5 - (A.bOpen0 ? A.Rake : A.Ext0), Xb = A.L * 0.5 + (A.bOpen1 ? A.Rake : A.Ext1);
	const FVector Dn(0, 0, -Fd);
	for (int32 i = 0; i < 2; ++i)
	{
		const double Sg = i == 0 ? 1.0 : -1.0, Ye = Sg * (A.S * 0.5 + A.Overhang), Ze = A.Z0 - A.Overhang * T;
		const FVector Ea(Xa, Ye, Ze), Eb(Xb, Ye, Ze), Ra(Xa, 0, Zr), Rb(Xb, 0, Zr);
		B.Set(HS::Shingles, H.RoofColor);
		B.QuadF(Ea, Eb, Rb, Ra, FVector(0, Sg * T, 1));
		B.Set(HS::Trim, H.TrimColor);
		B.QuadF(Ea + Dn, Eb + Dn, Rb + Dn, Ra + Dn, FVector(0, -Sg * T, -1));
		B.QuadF(Ea, Eb, Eb + Dn, Ea + Dn, FVector(0, Sg, 0));
		if (A.bOpen0) { B.QuadF(Ea, Ra, Ra + Dn, Ea + Dn, FVector(-1, 0, 0)); }
		if (A.bOpen1) { B.QuadF(Eb, Rb, Rb + Dn, Eb + Dn, FVector(1, 0, 0)); }
		if (A.bGutters)
		{
			B.Set(HS::Metal, H.GutterColor);
			B.Box(FVector(Xa, Ye, Ze - 12), FVector(Xb, Ye + Sg * 11, Ze - 2));
			const double Xd = Sg > 0 ? A.L * 0.5 - 12 : -A.L * 0.5 + 12;
			if (!InsideOther(B.ToActor(FVector(Xd, Sg * (A.S * 0.5 + 6), A.Z0 - 30)), -1))
			{
				EmitDownspout(FVector(Xd, Ye + Sg * 5, Ze - 14), FVector(Xd, Sg * (A.S * 0.5 + 6.5), Ze - 14 - FMath::Max(12.0, A.Overhang * 0.7)));
			}
		}
	}
	B.Set(HS::Shingles, H.RoofColor * 0.8f);
	B.Box(FVector(Xa + 0.5, -10, Zr - 4), FVector(Xb - 0.5, 10, Zr + 1.5), FB::All & ~FB::ZN);
	// gable end walls
	for (int32 End = 0; End < 2; ++End)
	{
		if (!(End == 0 ? A.bOpen0 : A.bOpen1)) { continue; }
		const double X = End == 0 ? -A.L * 0.5 : A.L * 0.5;
		B.Push(FB::WallFrame(FVector(X, 0, 0), End == 0 ? FVector(0, 1, 0) : FVector(0, -1, 0)));
		const FVector Out(0, 1, 0);
		const double Hs = A.S * 0.5;
		B.Set(A.GableMat, A.GableColor);
		const double Ww = FMath::Min<double>(H.WindowWidth, 110.0), Wh = FMath::Min<double>(H.WindowHeight * 0.85, 125.0);
		const double Za = A.Z0 + 55, Zb = Za + Wh, ZAtEdge = A.Z0 + (Hs - Ww * 0.5 - H.CasingWidth - 14) * T;
		if (A.bVents && Rise > 250 && Zb + 20 < ZAtEdge)
		{
			const double ZL = A.Z0 + (Hs - Ww * 0.5) * T;
			B.TriF(FVector(-Hs, 0, A.Z0), FVector(-Ww * 0.5, 0, A.Z0), FVector(-Ww * 0.5, 0, ZL), Out);
			B.TriF(FVector(Hs, 0, A.Z0), FVector(Ww * 0.5, 0, A.Z0), FVector(Ww * 0.5, 0, ZL), Out);
			B.WallQuad(-Ww * 0.5, Ww * 0.5, A.Z0, Za);
			B.WallQuad(-Ww * 0.5, Ww * 0.5, Zb, ZL);
			B.TriF(FVector(-Ww * 0.5, 0, ZL), FVector(Ww * 0.5, 0, ZL), FVector(0, 0, Zr), Out);
			FOpen O; O.S0 = -Ww * 0.5; O.S1 = Ww * 0.5; O.Z0 = Za; O.Z1 = Zb;
			EmitWindow(O, false);
		}
		else
		{
			B.TriF(FVector(-Hs, 0, A.Z0), FVector(Hs, 0, A.Z0), FVector(0, 0, Zr), Out);
			if (A.bVents && Rise > 120)
			{
				const double Vw = 22, Zc = A.Z0 + Rise * 0.42, Vh = FMath::Min(34.0, Rise * 0.2);
				B.Set(HS::Trim, H.TrimColor);
				B.Box(FVector(-Vw - 5, 0, Zc - Vh - 5), FVector(-Vw, 2.5, Zc + Vh + 5), NoBack);
				B.Box(FVector(Vw, 0, Zc - Vh - 5), FVector(Vw + 5, 2.5, Zc + Vh + 5), NoBack);
				B.Box(FVector(-Vw, 0, Zc - Vh - 5), FVector(Vw, 2.5, Zc - Vh), NoBack);
				B.Box(FVector(-Vw, 0, Zc + Vh), FVector(Vw, 2.5, Zc + Vh + 5), NoBack);
				B.Set(HS::Metal, FLinearColor(0.02f, 0.02f, 0.02f));
				B.WallQuad(-Vw, Vw, Zc - Vh, Zc + Vh, 0.3);
				B.Set(HS::Trim, H.TrimColor);
				for (double Z = Zc - Vh + 1; Z < Zc + Vh - 4; Z += 6.5)
				{
					B.QuadF(FVector(-Vw, 0.4, Z + 5), FVector(Vw, 0.4, Z + 5), FVector(Vw, 2.4, Z), FVector(-Vw, 2.4, Z), FVector(0, 1, 1));
				}
			}
		}
		// rake board under the roof edge
		B.Set(HS::Trim, H.TrimColor);
		const double Bd = 14;
		B.QuadF(FVector(-Hs, 1.8, A.Z0 - Bd * 0.2), FVector(0, 1.8, Zr - Bd * 0.2), FVector(0, 1.8, Zr - Bd * 1.25), FVector(-Hs, 1.8, A.Z0 - Bd * 1.25), Out);
		B.QuadF(FVector(Hs, 1.8, A.Z0 - Bd * 0.2), FVector(0, 1.8, Zr - Bd * 0.2), FVector(0, 1.8, Zr - Bd * 1.25), FVector(Hs, 1.8, A.Z0 - Bd * 1.25), Out);
		B.Pop();
	}
}

void FHearthlineHouseGen::EmitHip(double L, double S, double Z0, double Pitch, double Oh)
{
	const double T = TanDeg(Pitch), Zr = Z0 + S * 0.5 * T, Ze = Z0 - Oh * T, Fd = H.FasciaDepth;
	const double Ex = L * 0.5 + Oh, Ey = S * 0.5 + Oh, Rx = (L - S) * 0.5;
	const FVector C00(-Ex, -Ey, Ze), C10(Ex, -Ey, Ze), C11(Ex, Ey, Ze), C01(-Ex, Ey, Ze), Ra(-Rx, 0, Zr), Rb(Rx, 0, Zr), Dn(0, 0, -Fd);
	B.Set(HS::Shingles, H.RoofColor);
	B.QuadF(C01, C11, Rb, Ra, FVector(0, T, 1));
	B.QuadF(C00, C10, Rb, Ra, FVector(0, -T, 1));
	B.TriF(C10, C11, Rb, FVector(T, 0, 1));
	B.TriF(C00, C01, Ra, FVector(-T, 0, 1));
	B.Set(HS::Trim, H.TrimColor);
	B.QuadF(C01, C11, C11 + Dn, C01 + Dn, FVector(0, 1, 0));
	B.QuadF(C00, C10, C10 + Dn, C00 + Dn, FVector(0, -1, 0));
	B.QuadF(C10, C11, C11 + Dn, C10 + Dn, FVector(1, 0, 0));
	B.QuadF(C00, C01, C01 + Dn, C00 + Dn, FVector(-1, 0, 0));
	B.QuadF(C00 + Dn, C10 + Dn, C11 + Dn, C01 + Dn, FVector(0, 0, -1));
	if (H.bGutters)
	{
		B.Set(HS::Metal, H.GutterColor);
		B.Box(FVector(-Ex - 11, Ey, Ze - 12), FVector(Ex + 11, Ey + 11, Ze - 2));
		B.Box(FVector(-Ex - 11, -Ey - 11, Ze - 12), FVector(Ex + 11, -Ey, Ze - 2));
		B.Box(FVector(Ex, -Ey, Ze - 12), FVector(Ex + 11, Ey, Ze - 2));
		B.Box(FVector(-Ex - 11, -Ey, Ze - 12), FVector(-Ex, Ey, Ze - 2));
		for (int32 i = 0; i < 2; ++i)
		{
			const double Sg = i == 0 ? 1 : -1, Xd = Sg * (L * 0.5 - 12);
			if (!InsideOther(B.ToActor(FVector(Xd, Sg * (S * 0.5 + 6), Z0 - 30)), -1))
			{
				EmitDownspout(FVector(Xd, Sg * (Ey + 5), Ze - 14), FVector(Xd, Sg * (S * 0.5 + 6.5), Ze - 14 - FMath::Max(12.0, Oh * 0.7)));
			}
		}
	}
}

void FHearthlineHouseGen::EmitShed(double L, double S, double Z0, double Pitch, double Oh, int32 WallMat, const FLinearColor& WallCol)
{
	const double T = TanDeg(Pitch), Fd = H.FasciaDepth, Ex = L * 0.5 + Oh;
	const double Yl = -S * 0.5 - Oh, Yh = S * 0.5 + Oh, Zl = Z0 - Oh * T, Zh = Z0 + (S + Oh) * T, ZWall = Z0 + S * T;
	const FVector A(-Ex, Yl, Zl), Bp(Ex, Yl, Zl), C(Ex, Yh, Zh), D(-Ex, Yh, Zh), Dn(0, 0, -Fd);
	B.Set(HS::Shingles, H.RoofColor);
	B.QuadF(A, Bp, C, D, FVector(0, -T, 1));
	B.Set(HS::Trim, H.TrimColor);
	B.QuadF(A + Dn, Bp + Dn, C + Dn, D + Dn, FVector(0, T, -1));
	B.QuadF(A, Bp, Bp + Dn, A + Dn, FVector(0, -1, 0));
	B.QuadF(D, C, C + Dn, D + Dn, FVector(0, 1, 0));
	B.QuadF(Bp, C, C + Dn, Bp + Dn, FVector(1, 0, 0));
	B.QuadF(A, D, D + Dn, A + Dn, FVector(-1, 0, 0));
	B.Set(WallMat, WallCol);
	B.QuadF(FVector(-L * 0.5, S * 0.5, Z0), FVector(L * 0.5, S * 0.5, Z0), FVector(L * 0.5, S * 0.5, ZWall), FVector(-L * 0.5, S * 0.5, ZWall), FVector(0, 1, 0));
	B.TriF(FVector(L * 0.5, -S * 0.5, Z0), FVector(L * 0.5, S * 0.5, Z0), FVector(L * 0.5, S * 0.5, ZWall), FVector(1, 0, 0));
	B.TriF(FVector(-L * 0.5, -S * 0.5, Z0), FVector(-L * 0.5, S * 0.5, Z0), FVector(-L * 0.5, S * 0.5, ZWall), FVector(-1, 0, 0));
	if (H.bGutters)
	{
		B.Set(HS::Metal, H.GutterColor);
		B.Box(FVector(-Ex, Yl - 11, Zl - 12), FVector(Ex, Yl, Zl - 2));
		const double Xd = L * 0.5 - 12;
		if (!InsideOther(B.ToActor(FVector(Xd, -S * 0.5 - 6, Z0 - 30)), -1))
		{
			EmitDownspout(FVector(Xd, Yl - 5, Zl - 14), FVector(Xd, -S * 0.5 - 6.5, Zl - 14 - FMath::Max(12.0, Oh * 0.7)));
		}
	}
}

void FHearthlineHouseGen::EmitFlat(double L, double S, double Z0, int32 WallMat, const FLinearColor& WallCol)
{
	const double Ph = H.ParapetHeight, Th = 20, Hx = L * 0.5, Hy = S * 0.5;
	const uint8 NoBottom = FB::All & ~FB::ZN;
	B.Set(HS::Concrete, H.RoofColor);
	B.QuadF(FVector(-Hx, -Hy, Z0 + 6), FVector(Hx, -Hy, Z0 + 6), FVector(Hx, Hy, Z0 + 6), FVector(-Hx, Hy, Z0 + 6), Up);
	if (Ph < 1) { return; }
	B.Set(WallMat, WallCol);
	B.Box(FVector(-Hx, Hy - Th, Z0), FVector(Hx, Hy, Z0 + Ph), NoBottom);
	B.Box(FVector(-Hx, -Hy, Z0), FVector(Hx, -Hy + Th, Z0 + Ph), NoBottom);
	B.Box(FVector(Hx - Th, -Hy + Th, Z0), FVector(Hx, Hy - Th, Z0 + Ph), NoBottom);
	B.Box(FVector(-Hx, -Hy + Th, Z0), FVector(-Hx + Th, Hy - Th, Z0 + Ph), NoBottom);
	B.Set(HS::Metal, H.TrimColor);
	B.Box(FVector(-Hx - 3, Hy - Th - 2, Z0 + Ph), FVector(Hx + 3, Hy + 3, Z0 + Ph + 5));
	B.Box(FVector(-Hx - 3, -Hy - 3, Z0 + Ph), FVector(Hx + 3, -Hy + Th + 2, Z0 + Ph + 5));
	B.Box(FVector(Hx - Th - 2, -Hy + Th + 2, Z0 + Ph), FVector(Hx + 3, Hy - Th - 2, Z0 + Ph + 5));
	B.Box(FVector(-Hx - 3, -Hy + Th + 2, Z0 + Ph), FVector(-Hx + Th + 2, Hy - Th - 2, Z0 + Ph + 5));
}

void FHearthlineHouseGen::EmitDormers(const FVol& V, int32 VolIndex, double Sign, int32 Count)
{
	if (Count <= 0) { return; }
	const double T = TanDeg(V.V->Pitch), Zr = V.ZTop + V.S * 0.5 * T;
	const double Ww = FMath::Min<double>(H.WindowWidth, 100.0), Wh = FMath::Min<double>(H.WindowHeight * 0.8, 115.0);
	const double Wd = Ww + 2 * H.CasingWidth + 34, Inset = 45.0;
	const double Yf = V.S * 0.5 - Inset, Zf = V.ZTop + Inset * T;
	const double Pd = FMath::Clamp<double>(V.V->Pitch, 28.0, 42.0), Zde = Zf + Wh + 34, Zdr = Zde + Wd * 0.5 * TanDeg(Pd);
	if (Zdr > Zr - 5 || V.L < Wd + 120) { return; }
	const double Yb = FMath::Max(0.0, V.S * 0.5 - (Zdr - V.ZTop) / T), Ld = Yf - Yb;
	Count = FMath::Min(Count, FMath::FloorToInt32((V.L - 80) / (Wd + 60)));
	int32 Mat; FLinearColor Col;
	CladdingFor(V, V.V->Storeys, true, Mat, Col);
	for (int32 i = 0; i < Count; ++i)
	{
		const double X = -V.L * 0.5 + V.L * (i + 0.5) / Count;
		const FVector Xd(0, Sign, 0), Yd = FVector::CrossProduct(Up, Xd);
		B.Push(FTransform(FMatrix(Xd, Yd, Up, FVector(X, Sign * (Yf + Yb) * 0.5, 0))));
		B.Set(Mat, Col);
		B.QuadF(FVector(-Ld * 0.5, Wd * 0.5, Zf - 10), FVector(Ld * 0.5, Wd * 0.5, Zf - 10), FVector(Ld * 0.5, Wd * 0.5, Zde), FVector(-Ld * 0.5, Wd * 0.5, Zde), FVector(0, 1, 0));
		B.QuadF(FVector(-Ld * 0.5, -Wd * 0.5, Zf - 10), FVector(Ld * 0.5, -Wd * 0.5, Zf - 10), FVector(Ld * 0.5, -Wd * 0.5, Zde), FVector(-Ld * 0.5, -Wd * 0.5, Zde), FVector(0, -1, 0));
		B.Push(FB::WallFrame(FVector(Ld * 0.5, Wd * 0.5, Zf - 10), FVector(0, -1, 0)));
		TArray<FOpen> Opens; FOpen O; O.S0 = (Wd - Ww) * 0.5; O.S1 = O.S0 + Ww; O.Z0 = 24; O.Z1 = 24 + Wh; Opens.Add(O);
		EmitWallSkin(Wd, Zde - Zf + 10, Opens);
		EmitWindow(O, false);
		B.Pop();
		FGableArgs A; A.L = Ld; A.S = Wd; A.Z0 = Zde; A.Pitch = Pd; A.Overhang = 14; A.Rake = 14; A.bOpen0 = false; A.bOpen1 = true;
		A.GableMat = Mat; A.GableColor = Col; A.FasciaDepth = 10;
		EmitGable(A);
		B.Pop();
	}
}

void FHearthlineHouseGen::EmitVolumeRoof(int32 VolIndex)
{
	const FVol& V = Vols[VolIndex];
	const FVector Centre((V.X0 + V.X1) * 0.5, (V.Y0 + V.Y1) * 0.5, 0);
	B.Push(V.bRot ? FTransform(FMatrix(FVector(0, -1, 0), FVector(1, 0, 0), Up, Centre)) : FTransform(Centre));
	int32 GMat; FLinearColor GCol;
	CladdingFor(V, V.V->Storeys - 1, true, GMat, GCol);
	int32 WMat; FLinearColor WCol;
	CladdingFor(V, V.V->Storeys - 1, false, WMat, WCol);
	switch (V.Roof)
	{
	case EHearthlineRoof::Gable:
	{
		FGableArgs A; A.L = V.L; A.S = V.S; A.Z0 = V.ZTop; A.Pitch = V.V->Pitch; A.Overhang = V.V->Overhang; A.Rake = FMath::Min<double>(V.V->Overhang, 35.0);
		A.GableMat = GMat; A.GableColor = GCol; A.bVents = H.bGableVents; A.bGutters = H.bGutters; A.FasciaDepth = H.FasciaDepth;
		const double Zr = V.ZTop + V.S * 0.5 * TanDeg(V.V->Pitch);
		for (int32 End = 0; End < 2; ++End)
		{
			int32 Other = -1;
			const FVector P = B.ToActor(FVector((End == 0 ? -1 : 1) * (V.L * 0.5 + 6), 0, V.ZTop - 10));
			if (InsideOther(P, VolIndex, &Other))
			{
				const FVol& O = Vols[Other];
				const double Ext = O.ZTop >= Zr - 1 ? 0.0 : (V.bRot ? (O.Y1 - O.Y0) : (O.X1 - O.X0)) * 0.5;
				if (End == 0) { A.bOpen0 = false; A.Ext0 = Ext; } else { A.bOpen1 = false; A.Ext1 = Ext; }
			}
		}
		EmitGable(A);
		EmitDormers(V, VolIndex, 1.0, V.V->DormersFront);
		EmitDormers(V, VolIndex, -1.0, V.V->DormersBack);
		break;
	}
	case EHearthlineRoof::Hip:
		EmitHip(V.L, V.S, V.ZTop, V.V->Pitch, V.V->Overhang);
		break;
	case EHearthlineRoof::Shed:
		if (V.V->bFlipShed) { B.Push(FTransform(FRotator(0, 180, 0))); }
		EmitShed(V.L, V.S, V.ZTop, V.V->Pitch, V.V->Overhang, GMat, GCol);
		if (V.V->bFlipShed) { B.Pop(); }
		break;
	default:
		EmitFlat(V.L, V.S, V.ZTop, WMat, WCol);
		break;
	}
	B.Pop();
}

// ---------------------------------------------------------------------------------------------------------------
// Porch, chimney, lot
// ---------------------------------------------------------------------------------------------------------------

void FHearthlineHouseGen::EmitPorch(const FHearthlinePorch& P)
{
	if (!Vols.IsValidIndex(P.Volume)) { return; }
	const FVol& V = Vols[P.Volume];
	FVector O, Dir, N; double Len;
	SideFrame(V, int32(P.Side), O, Dir, N, Len);
	const double A = FMath::Min(P.Start, P.End) * Len, C = FMath::Max(P.Start, P.End) * Len, Dp = P.Depth, Ps = P.PostSize;
	if (C - A < 60) { return; }
	const double Zd = FMath::Max(4.0, V.ZBase - 2.0);
	B.Push(FB::WallFrame(O, Dir));

	// deck and skirt
	B.Set(HS::Wood, H.DeckColor);
	B.Box(FVector(A, 0, Zd - 10), FVector(C, Dp, Zd), FB::All & ~FB::YN);
	if (Zd > 14)
	{
		B.Set(int32(H.FoundationCladding), H.FoundationColor);
		B.Box(FVector(A + 5, 0, 0), FVector(C - 5, Dp - 5, Zd - 10), FB::XP | FB::XN | FB::YP);
	}

	// steps
	const double Sw = FMath::Min<double>(P.StepsWidth, C - A - 2 * Ps - 10);
	const double Sc = FMath::Clamp(A + (C - A) * P.StepsPosition, A + Ps + Sw * 0.5 + 2, C - Ps - Sw * 0.5 - 2);
	double Run = 0;
	EmitSteps(Sc, Sw, Dp, Zd, HS::Wood, H.DeckColor, Run);
	if (bHaveDoor && DoorOrigin.Equals(O) && DoorS >= A - 1 && DoorS <= C + 1) { DoorS = Sc; DoorOut = Dp + Run; }

	// posts
	TArray<double> Posts;
	const int32 Bays = FMath::Max(1, FMath::CeilToInt32((C - A - Ps) / 290.0));
	for (int32 i = 0; i <= Bays; ++i) { Posts.Add(A + Ps * 0.5 + (C - A - Ps) * i / Bays); }
	// keep posts out of the stair opening by nudging them to its edges
	for (double& X : Posts)
	{
		if (X > Sc - Sw * 0.5 - Ps * 0.5 && X < Sc + Sw * 0.5 + Ps * 0.5) { X = X < Sc ? Sc - Sw * 0.5 - Ps * 0.5 : Sc + Sw * 0.5 + Ps * 0.5; }
	}
	const double Tp = TanDeg(P.RoofPitch), ZAttach = V.ZBase + V.V->StoreyHeight - 8, RoofOh = 28, Fd = 12;
	const double ZBeamTop = P.bRoof ? ZAttach - Dp * Tp - Fd : Zd + 96, BeamH = 22;
	const double PostTop = P.bRoof ? ZBeamTop - BeamH : Zd + 100;
	for (double X : Posts)
	{
		if (!P.bRoof && !P.bRailing) { break; }
		B.Set(HS::Trim, H.TrimColor);
		double Z0 = Zd;
		if (P.bPostPiers && P.bRoof)
		{
			const double Pw = Ps * 1.9;
			B.Set(int32(H.FoundationCladding == EHearthlineCladding::Concrete ? EHearthlineCladding::Brick : H.FoundationCladding), H.FoundationColor);
			B.Box(FVector(X - Pw * 0.5, Dp - Ps * 0.5 - Pw * 0.5, Zd), FVector(X + Pw * 0.5, Dp - Ps * 0.5 + Pw * 0.5, Zd + 85), FB::All & ~FB::ZN);
			B.Set(HS::Concrete, FLinearColor(0.8f, 0.8f, 0.78f));
			B.Box(FVector(X - Pw * 0.5 - 3, Dp - Ps * 0.5 - Pw * 0.5 - 3, Zd + 85), FVector(X + Pw * 0.5 + 3, Dp - Ps * 0.5 + Pw * 0.5 + 3, Zd + 91));
			B.Set(HS::Trim, H.TrimColor);
			Z0 = Zd + 91;
		}
		B.Box(FVector(X - Ps * 0.5, Dp - Ps, Z0), FVector(X + Ps * 0.5, Dp, PostTop), FB::All & ~FB::ZN);
		if (P.bRoof)
		{
			B.Box(FVector(X - Ps * 0.5 - 2, Dp - Ps - 2, PostTop - 9), FVector(X + Ps * 0.5 + 2, Dp + 2, PostTop));
			B.Box(FVector(X - Ps * 0.5 - 2, Dp - Ps - 2, Z0), FVector(X + Ps * 0.5 + 2, Dp + 2, Z0 + 12), FB::All & ~FB::ZN);
		}
	}

	if (P.bRoof)
	{
		B.Set(HS::Trim, H.TrimColor);
		B.Box(FVector(A, Dp - Ps, ZBeamTop - BeamH), FVector(C, Dp, ZBeamTop));
		B.Box(FVector(A, 0, ZBeamTop - BeamH), FVector(A + Ps, Dp - Ps, ZBeamTop));
		B.Box(FVector(C - Ps, 0, ZBeamTop - BeamH), FVector(C, Dp - Ps, ZBeamTop));
		// closed ends between beam and roof
		B.TriF(FVector(A, 0, ZBeamTop), FVector(A, Dp, ZBeamTop), FVector(A, 0, ZAttach - Fd), FVector(-1, 0, 0));
		B.TriF(FVector(C, 0, ZBeamTop), FVector(C, Dp, ZBeamTop), FVector(C, 0, ZAttach - Fd), FVector(1, 0, 0));
		const double Xa = A - 20, Xb = C + 20, Ye = Dp + RoofOh, Ze = ZAttach - Ye * Tp;
		const FVector W0(Xa, 0, ZAttach), W1(Xb, 0, ZAttach), E0(Xa, Ye, Ze), E1(Xb, Ye, Ze), Dn(0, 0, -Fd);
		B.Set(HS::Shingles, H.RoofColor);
		B.QuadF(W0, W1, E1, E0, FVector(0, Tp, 1));
		B.Set(HS::Trim, H.TrimColor);
		B.QuadF(W0 + Dn, W1 + Dn, E1 + Dn, E0 + Dn, FVector(0, -Tp, -1));
		B.QuadF(E0, E1, E1 + Dn, E0 + Dn, FVector(0, 1, 0));
		B.QuadF(W0, E0, E0 + Dn, W0 + Dn, FVector(-1, 0, 0));
		B.QuadF(W1, E1, E1 + Dn, W1 + Dn, FVector(1, 0, 0));
		if (H.bGutters)
		{
			B.Set(HS::Metal, H.GutterColor);
			B.Box(FVector(Xa, Ye, Ze - 12), FVector(Xb, Ye + 10, Ze - 2));
		}
	}

	if (P.bRailing && Zd > 20)
	{
		B.Set(HS::Trim, H.TrimColor);
		auto Rail = [&](const FVector& From, const FVector& To)
		{
			const double D = FVector::Dist(From, To);
			if (D < 25) { return; }
			B.Beam(From + Up * 92, To + Up * 92, 7, 5);
			B.Beam(From + Up * 12, To + Up * 12, 5, 4);
			const int32 Count = FMath::Max(1, FMath::FloorToInt32(D / 12.5));
			const FVector Step = (To - From) / (Count + 1);
			for (int32 i = 1; i <= Count; ++i)
			{
				const FVector Q = From + Step * i;
				B.Box(FVector(Q.X - 1.6, Q.Y - 1.6, Zd + 14), FVector(Q.X + 1.6, Q.Y + 1.6, Zd + 89.5), FB::XP | FB::XN | FB::YP | FB::YN);
			}
		};
		const double Yr = Dp - Ps * 0.5;
		Posts.Sort();
		for (int32 i = 0; i + 1 < Posts.Num(); ++i)
		{
			const double X0 = Posts[i] + Ps * 0.5, X1 = Posts[i + 1] - Ps * 0.5;
			if (X0 >= Sc - Sw * 0.5 - 1 && X1 <= Sc + Sw * 0.5 + 1) { continue; }
			Rail(FVector(X0, Yr, Zd), FVector(X1, Yr, Zd));
		}
		Rail(FVector(A + Ps * 0.5, 2, Zd), FVector(A + Ps * 0.5, Dp - Ps, Zd));
		Rail(FVector(C - Ps * 0.5, 2, Zd), FVector(C - Ps * 0.5, Dp - Ps, Zd));
	}
	B.Pop();
}

void FHearthlineHouseGen::EmitChimney()
{
	const FHearthlineChimney& Ch = H.Chimney;
	if (!Ch.bEnabled || !Vols.IsValidIndex(Ch.Volume)) { return; }
	const FVol& V = Vols[Ch.Volume];
	const double X = FMath::Lerp(V.X0, V.X1, double(Ch.AlongDepth)), Y = FMath::Lerp(V.Y0, V.Y1, double(Ch.AcrossWidth));
	const double Hx = Ch.Size.X * 0.5, Hy = Ch.Size.Y * 0.5, Top = V.ZPeak + Ch.HeightAboveRoof;
	B.Set(int32(Ch.Cladding), Ch.Color);
	B.Box(FVector(X - Hx, Y - Hy, 0), FVector(X + Hx, Y + Hy, Top), FB::All & ~FB::ZN);
	B.Set(HS::Concrete, FLinearColor(0.7f, 0.7f, 0.68f));
	B.Box(FVector(X - Hx - 5, Y - Hy - 5, Top), FVector(X + Hx + 5, Y + Hy + 5, Top + 8));
	B.Set(HS::Stucco, FLinearColor(0.35f, 0.14f, 0.08f));
	const double F = FMath::Min(Hx, Hy) * 0.55;
	B.Box(FVector(X - F, Y - F, Top + 8), FVector(X + F, Y + F, Top + 30), FB::All & ~FB::ZN);
	B.Set(HS::Metal, FLinearColor(0.01f, 0.01f, 0.01f));
	B.QuadF(FVector(X - F + 3, Y - F + 3, Top + 30.2), FVector(X + F - 3, Y - F + 3, Top + 30.2), FVector(X + F - 3, Y + F - 3, Top + 30.2), FVector(X - F + 3, Y + F - 3, Top + 30.2), Up);
}

void FHearthlineHouseGen::EmitLot()
{
	if (!H.bLot) { return; }
	const double X1 = H.FrontYard, X0 = H.FrontYard - H.LotSize.X, Y0 = -H.LotSize.Y * 0.5, Y1 = H.LotSize.Y * 0.5;
	B.Set(HS::Grass, FLinearColor::White);
	B.QuadF(FVector(X0, Y0, 0), FVector(X1, Y0, 0), FVector(X1, Y1, 0), FVector(X0, Y1, 0), Up);

	auto DistanceToEdge = [&](const FVector& P, const FVector& N) -> double
	{
		double D = 1e9;
		if (N.X > 0.5) { D = X1 - P.X; } else if (N.X < -0.5) { D = P.X - X0; }
		else if (N.Y > 0.5) { D = Y1 - P.Y; } else { D = P.Y - Y0; }
		return D;
	};
	const uint8 Slab = FB::All & ~FB::ZN;
	if (H.bDriveway && bHaveGarage)
	{
		const FVector N = FVector::CrossProduct(Up, GarageDir);
		const double Len = DistanceToEdge(GarageOrigin + GarageDir * (GarageS0 + GarageS1) * 0.5, N);
		if (Len > 10)
		{
			B.Push(FB::WallFrame(GarageOrigin, GarageDir));
			B.Set(HS::Concrete, FLinearColor(0.62f, 0.62f, 0.6f));
			B.Box(FVector(GarageS0 - 35, 0, 0), FVector(GarageS1 + 35, Len, 2.0), Slab);
			B.Pop();
		}
	}
	if (H.bWalkway && bHaveDoor)
	{
		const FVector N = FVector::CrossProduct(Up, DoorDir);
		const double Len = DistanceToEdge(DoorOrigin + DoorDir * DoorS, N);
		if (Len > DoorOut + 10)
		{
			B.Push(FB::WallFrame(DoorOrigin, DoorDir));
			B.Set(HS::Concrete, FLinearColor(0.72f, 0.72f, 0.7f));
			B.Box(FVector(DoorS - 60, DoorOut, 0), FVector(DoorS + 60, Len, 1.5), Slab);
			B.Pop();
		}
	}
}

void FHearthlineHouseGen::Build()
{
	Vols.Reset();
	for (const FHearthlineVolume& Src : H.Volumes)
	{
		FVol V; V.V = &Src;
		V.X0 = Src.Offset.X - Src.Depth * 0.5; V.X1 = Src.Offset.X + Src.Depth * 0.5;
		V.Y0 = Src.Offset.Y - Src.Width * 0.5; V.Y1 = Src.Offset.Y + Src.Width * 0.5;
		V.ZBase = H.FoundationHeight + Src.FloorOffset;
		V.ZTop = V.ZBase + FMath::Max(1, Src.Storeys) * Src.StoreyHeight;
		V.Roof = Src.Roof; V.bRot = Src.bRidgeAlongWidth;
		V.L = V.bRot ? Src.Width : Src.Depth; V.S = V.bRot ? Src.Depth : Src.Width;
		if (V.Roof == EHearthlineRoof::Hip && V.L < V.S) { V.bRot = !V.bRot; Swap(V.L, V.S); }
		const double T = TanDeg(Src.Pitch);
		switch (V.Roof)
		{
		case EHearthlineRoof::Shed: V.ZPeak = V.ZTop + V.S * T; break;
		case EHearthlineRoof::Flat: V.ZPeak = V.ZTop + H.ParapetHeight; break;
		default: V.ZPeak = V.ZTop + V.S * 0.5 * T; break;
		}
		Vols.Add(V);
	}
	for (int32 i = 0; i < Vols.Num(); ++i) { EmitVolumeWalls(i); }
	for (int32 i = 0; i < Vols.Num(); ++i) { EmitVolumeRoof(i); }
	for (const FHearthlinePorch& P : H.Porches) { EmitPorch(P); }
	EmitChimney();
	EmitLot();
	EmitPlanting();
	EmitFence();
}
