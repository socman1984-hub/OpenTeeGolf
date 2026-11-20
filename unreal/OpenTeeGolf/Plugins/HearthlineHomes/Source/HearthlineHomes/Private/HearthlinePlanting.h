// Copyright __PUBLISHER__. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "HearthlineMeshBuilder.h"

/** Procedural trees and shrubs: smooth limbs, dark inner masses and alpha-masked leaf cards. */
struct FHearthlinePlanting
{
	using FB = FHearthlineMeshBuilder;

	struct FSlots
	{
		int32 Bark = 0, Foliage = 0, Inner = 0;
		FLinearColor Tint = FLinearColor::White;
	};

	/** Tapered round limb between two points, smooth shaded, bark grain along its length. Uses the builder's current material. */
	static void Limb(FB& B, const FVector& P0, double R0, const FVector& P1, double R1, int32 Sides, const FLinearColor& Col)
	{
		const FVector Axis = (P1 - P0);
		const double Len = Axis.Size();
		if (Len < 1.0) { return; }
		const FVector Z = Axis / Len;
		FVector X = FVector::CrossProduct(Z, FMath::Abs(Z.Z) > 0.9 ? FVector(1, 0, 0) : FVector(0, 0, 1)).GetSafeNormal();
		const FVector Y = FVector::CrossProduct(Z, X);
		for (int32 i = 0; i < Sides; ++i)
		{
			const double A0 = 2 * PI * i / Sides, A1 = 2 * PI * (i + 1) / Sides;
			const FVector D0 = X * FMath::Cos(A0) + Y * FMath::Sin(A0), D1 = X * FMath::Cos(A1) + Y * FMath::Sin(A1);
			FB::FRawVertex V[4];
			V[0].P = P0 + D0 * R0; V[0].N = D0; V[0].UV = FVector2f(0.f, float(i) / Sides * 0.6f);
			V[1].P = P0 + D1 * R0; V[1].N = D1; V[1].UV = FVector2f(0.f, float(i + 1) / Sides * 0.6f);
			V[2].P = P1 + D1 * R1; V[2].N = D1; V[2].UV = FVector2f(float(Len * 0.01), float(i + 1) / Sides * 0.6f);
			V[3].P = P1 + D0 * R1; V[3].N = D0; V[3].UV = FVector2f(float(Len * 0.01), float(i) / Sides * 0.6f);
			for (FB::FRawVertex& Vt : V) { Vt.C = Col; }
			// choose the winding that faces away from the axis
			if (FVector::DotProduct(FVector::CrossProduct(V[1].P - V[0].P, V[3].P - V[0].P), D0) < 0) { B.RawQuad(V[0], V[3], V[2], V[1]); }
			else { B.RawQuad(V[0], V[1], V[2], V[3]); }
		}
	}

	/** Smooth ellipsoid. Uses the builder's current material. */
	static void Blob(FB& B, const FVector& C, const FVector& Radii, const FLinearColor& Col, int32 Segs = 8, int32 Rings = 5)
	{
		auto Point = [&](int32 s, int32 r, FB::FRawVertex& V)
		{
			const double Th = 2 * PI * s / Segs, Ph = PI * r / Rings;
			const FVector D(FMath::Sin(Ph) * FMath::Cos(Th), FMath::Sin(Ph) * FMath::Sin(Th), FMath::Cos(Ph));
			V.P = C + D * Radii; V.N = D; V.UV = FVector2f(float(s) / Segs * 3.f, float(r) / Rings * 1.5f); V.C = Col * float(0.6 + 0.4 * (D.Z * 0.5 + 0.5)); V.C.A = 1.f;
		};
		for (int32 r = 0; r < Rings; ++r) for (int32 s = 0; s < Segs; ++s)
		{
			FB::FRawVertex V[4];
			Point(s, r, V[0]); Point(s + 1, r, V[1]); Point(s + 1, r + 1, V[2]); Point(s, r + 1, V[3]);
			const FVector Mid = (V[0].P + V[2].P) * 0.5 - C;
			if (FVector::DotProduct(FVector::CrossProduct(V[1].P - V[0].P, V[3].P - V[0].P) + FVector::CrossProduct(V[2].P - V[1].P, V[3].P - V[1].P), Mid) < 0) { B.RawQuad(V[0], V[3], V[2], V[1]); }
			else { B.RawQuad(V[0], V[1], V[2], V[3]); }
		}
	}

	/** Leaf cards scattered over an ellipsoid shell. Normals point away from ShadeCentre so the clump shades as one soft mass. */
	static void Cards(FB& B, FRandomStream& R, const FVector& C, const FVector& Radii, int32 Count, double Size, const FLinearColor& Tint, const FVector& ShadeCentre, double ShadeRadius)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			FVector Dir = R.VRand();
			if (Dir.Z < -0.35) { Dir.Z = -Dir.Z * 0.6; Dir.Normalize(); }
			const FVector P = C + Dir * Radii * R.FRandRange(0.62f, 1.0f);
			const FVector N = (Dir + R.VRand() * 0.55).GetSafeNormal();
			FVector X = FVector::CrossProduct(N, R.VRand()).GetSafeNormal();
			if (X.IsNearlyZero()) { X = FVector::CrossProduct(N, FVector(0, 0, 1)).GetSafeNormal(); }
			const FVector Y = FVector::CrossProduct(N, X);
			const double S = Size * R.FRandRange(0.75f, 1.25f) * 0.5;
			const double Out = FMath::Clamp(FVector::Dist(P, ShadeCentre) / FMath::Max(ShadeRadius, 1.0), 0.0, 1.0);
			const double Lift = FMath::Clamp((P.Z - ShadeCentre.Z) / FMath::Max(ShadeRadius, 1.0) * 0.5 + 0.5, 0.0, 1.0);
			FLinearColor Col = Tint * float(FMath::Lerp(0.45, 1.05, FMath::Pow(Out, 1.5)) * FMath::Lerp(0.72, 1.0, Lift) * R.FRandRange(0.85f, 1.1f));
			Col.A = 1.f;
			const bool bFlip = R.FRand() < 0.5f;
			const FVector Corners[4] = { P - X * S - Y * S, P + X * S - Y * S, P + X * S + Y * S, P - X * S + Y * S };
			static const FVector2f UVs[4] = { FVector2f(0, 0), FVector2f(1, 0), FVector2f(1, 1), FVector2f(0, 1) };
			FB::FRawVertex V[4];
			for (int32 k = 0; k < 4; ++k)
			{
				V[k].P = Corners[k];
				V[k].N = ((Corners[k] - ShadeCentre).GetSafeNormal() + N * 0.35).GetSafeNormal();
				V[k].UV = bFlip ? FVector2f(1.f - UVs[k].X, UVs[k].Y) : UVs[k];
				V[k].C = Col;
			}
			B.RawQuad(V[0], V[1], V[2], V[3]);
		}
	}

	/** Broadleaf tree standing on Base. */
	static void Tree(FB& B, FRandomStream& R, const FVector& Base, double Height, const FSlots& S)
	{
		const FLinearColor Bark(0.085f, 0.068f, 0.055f);
		const double TrunkH = Height * R.FRandRange(0.3f, 0.4f), R0 = Height * 0.02 + 7.0;
		const FVector Top = Base + FVector(R.FRandRange(-1.f, 1.f) * Height * 0.03, R.FRandRange(-1.f, 1.f) * Height * 0.03, TrunkH);
		const int32 Saved = B.Material;
		B.Material = S.Bark;
		Limb(B, Base - FVector(0, 0, 8), R0 * 1.25, Base + (Top - Base) * 0.12, R0, 8, Bark);
		Limb(B, Base + (Top - Base) * 0.12, R0, Top, R0 * 0.72, 8, Bark);
		const double CrownH = Height - TrunkH, Rx = Height * R.FRandRange(0.27f, 0.36f), Rz = CrownH * 0.5;
		const FVector Crown = Top + FVector(0, 0, CrownH * 0.5);
		const double ShadeR = FMath::Max(Rx, Rz) * 1.25;
		const int32 Clusters = 7;
		const FLinearColor Tint = S.Tint * R.FRandRange(0.85f, 1.1f);
		for (int32 i = 0; i < Clusters; ++i)
		{
			FVector D = R.VRand(); D.Z = D.Z * 0.6 + (i == 0 ? 0.7 : 0.0);
			const FVector C = i == 0 ? Crown + FVector(0, 0, Rz * 0.35) : Crown + D * FVector(Rx, Rx, Rz) * 0.55;
			const double Rc = Rx * R.FRandRange(0.52f, 0.68f);
			B.Material = S.Bark;
			Limb(B, Top - FVector(0, 0, TrunkH * 0.12), R0 * 0.45, C, 3.0, 5, Bark);
			B.Material = S.Inner;
			Blob(B, C, FVector(Rc, Rc, Rc * 0.8) * 0.55, Tint * 0.22f, 7, 4);
			B.Material = S.Foliage;
			Cards(B, R, C, FVector(Rc, Rc, Rc * 0.82), 34, FMath::Clamp(Height * 0.13, 75.0, 135.0), Tint, Crown, ShadeR);
		}
		B.Material = Saved;
	}

	/** Rounded shrub, or a narrow upright one when bConical is set. */
	static void Shrub(FB& B, FRandomStream& R, const FVector& Base, double Radius, double Height, bool bConical, const FSlots& S)
	{
		const int32 Saved = B.Material;
		const FLinearColor Tint = S.Tint * R.FRandRange(0.8f, 1.05f) * (bConical ? FLinearColor(0.75f, 0.9f, 0.85f) : FLinearColor::White);
		const int32 Tiers = bConical ? 3 : 1;
		for (int32 t = 0; t < Tiers; ++t)
		{
			const double F = bConical ? 1.0 - 0.3 * t : 1.0, Hh = Height / Tiers;
			const FVector C = Base + FVector(0, 0, Hh * (t + 0.5));
			const FVector Radii(Radius * F, Radius * F, Hh * (bConical ? 0.75 : 0.55));
			B.Material = S.Inner;
			Blob(B, C, Radii * 0.78, Tint * 0.2f, 7, 4);
			B.Material = S.Foliage;
			Cards(B, R, C, Radii, bConical ? 18 : 30, FMath::Clamp(Radius * 0.95, 26.0, 48.0), Tint, Base + FVector(0, 0, Height * 0.4), FMath::Max(Radius, Height * 0.6) * 1.1);
		}
		B.Material = Saved;
	}
};
