// The putt-putt's layout (see OTGMiniGolf.h): the nine holes' lanes, obstacles and heights, the
// lawn, and the geometry helpers that draw them. Shared by OTGMiniGolf*.cpp; private to Round.

#pragma once

#include "Round/OTGMiniGolf.h"
#include "Algo/Reverse.h"
#include "Async/ParallelFor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Course/OTGCourseData.h"
#include "Course/OTGCourseSubsystem.h"
#include "Physics/OTGBallPhysics.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	using P2 = FVector2D;   // (x, z): course metres, x east, z north

	// the boards round each lane: thickness, height above the felt and below it (as practice.js WALL)
	constexpr double WallT = 0.06, WallUp = 0.1, WallDown = 0.05;
	constexpr double BallR = 0.02135;
	// the windmill's sails: how fast they turn (rad/s) and how far either side of straight down one
	// blocks the tunnel (rad)
	constexpr double SailSpeed = 1.1, SailBlock = 0.2;
	// the fenced lawn the holes are in
	const P2 LawnMin(-16.5, -3.5), LawnMax(15.0, 44.5);
	// the height grid (5 cm), and the surfaces (2.5 cm) over the lawn
	constexpr double GridX0 = -27.5, GridZ1 = 55.5, GridStep = 0.05;
	constexpr int32 GridN = 1401;
	constexpr double GridX1 = GridX0 + (GridN - 1) * GridStep, GridZ0 = GridZ1 - (GridN - 1) * GridStep;
	constexpr double SurfX0 = -17.5, SurfZ1 = 45.5, SurfRes = 0.025;
	constexpr int32 SurfW = 1340, SurfH = 2000;   // (to x 16, z -4.5)
	constexpr double HeightBase = -0.2, HeightRange = 1.6;

	double Smooth(double E0, double E1, double X)
	{
		const double T = FMath::Clamp((X - E0) / (E1 - E0), 0.0, 1.0);
		return T * T * (3 - 2 * T);
	}
	FLinearColor Hex(uint32 Rgb) { return FLinearColor(FColor((Rgb >> 16) & 255, (Rgb >> 8) & 255, Rgb & 255)); }

	TArray<P2> RectPts(double X0, double Z0, double X1, double Z1) { return { P2(X0, Z0), P2(X1, Z0), P2(X1, Z1), P2(X0, Z1) }; }
	double Area(const TArray<P2>& Poly)
	{
		double A = 0;
		for (int32 i = 0, j = Poly.Num() - 1; i < Poly.Num(); j = i++) A += Poly[j].X * Poly[i].Y - Poly[i].X * Poly[j].Y;
		return A / 2;
	}
	bool InPoly(const TArray<P2>& Poly, double X, double Z)
	{
		bool bIn = false;
		for (int32 i = 0, j = Poly.Num() - 1; i < Poly.Num(); j = i++)
		{
			const P2 &A = Poly[i], &B = Poly[j];
			if ((A.Y > Z) != (B.Y > Z) && X < (B.X - A.X) * (Z - A.Y) / (B.Y - A.Y) + A.X) bIn = !bIn;
		}
		return bIn;
	}
	// Each edge with its outward normal
	struct FLaneEdge { P2 A, B, N; };
	TArray<FLaneEdge> Edges(const TArray<P2>& Poly)
	{
		const double S = Area(Poly) >= 0 ? 1 : -1;
		TArray<FLaneEdge> Out;
		for (int32 i = 0; i < Poly.Num(); i++)
		{
			const P2 A = Poly[i], B = Poly[(i + 1) % Poly.Num()];
			const double L = FMath::Max(1e-9, (B - A).Size());
			Out.Add(FLaneEdge{ A, B, P2((B.Y - A.Y) / L * S, -(B.X - A.X) / L * S) });
		}
		return Out;
	}
	// A convex outline grown outward by D (corners mitred)
	TArray<P2> Grow(const TArray<P2>& Poly, double D)
	{
		const TArray<FLaneEdge> E = Edges(Poly);
		TArray<P2> Out;
		for (int32 i = 0; i < E.Num(); i++)
		{
			const FLaneEdge& M = E[(i + E.Num() - 1) % E.Num()];
			const FLaneEdge& L = E[i];
			const P2 PM = M.A + M.N * D, DM = M.B - M.A, PL = L.A + L.N * D, DL = L.B - L.A;
			const double Den = DM.X * DL.Y - DM.Y * DL.X;
			if (FMath::Abs(Den) < 1e-12) { Out.Add(PL); continue; }
			const double T = ((PL.X - PM.X) * DL.Y - (PL.Y - PM.Y) * DL.X) / Den;
			Out.Add(PM + DM * T);
		}
		return Out;
	}
	double NearEdge(const TArray<P2>& Poly, double X, double Z)
	{
		double Best = 1e18;
		for (const FLaneEdge& E : Edges(Poly))
		{
			const P2 D = E.B - E.A;
			const double T = FMath::Clamp(((X - E.A.X) * D.X + (Z - E.A.Y) * D.Y) / FMath::Max(1e-12, D.SizeSquared()), 0.0, 1.0);
			Best = FMath::Min(Best, (P2(X, Z) - (E.A + D * T)).Size());
		}
		return Best;
	}
	// Sutherland-Hodgman: an outline cut to a box
	TArray<P2> ClipBox(const TArray<P2>& Poly, double X0, double Z0, double X1, double Z1)
	{
		TArray<P2> Out = Poly;
		for (int32 Side = 0; Side < 4 && Out.Num(); Side++)
		{
			auto In = [&](const P2& P) { return Side == 0 ? P.X >= X0 : Side == 1 ? P.X <= X1 : Side == 2 ? P.Y >= Z0 : P.Y <= Z1; };
			auto Cut = [&](const P2& A, const P2& B) {
				if (Side < 2) { const double X = Side == 0 ? X0 : X1; return P2(X, A.Y + (B.Y - A.Y) * (X - A.X) / (B.X - A.X)); }
				const double Z = Side == 2 ? Z0 : Z1;
				return P2(A.X + (B.X - A.X) * (Z - A.Y) / (B.Y - A.Y), Z);
			};
			const TArray<P2> Src = Out;
			Out.Reset();
			for (int32 i = 0; i < Src.Num(); i++)
			{
				const P2& A = Src[(i + Src.Num() - 1) % Src.Num()];
				const P2& B = Src[i];
				if (In(B)) { if (!In(A)) Out.Add(Cut(A, B)); Out.Add(B); }
				else if (In(A)) Out.Add(Cut(A, B));
			}
		}
		return Out;
	}
	// Ear clipping: an outline (either way round) as triangles, indices into it
	void Triangulate(const TArray<P2>& Poly, TArray<int32>& Out)
	{
		TArray<int32> Idx;
		for (int32 i = 0; i < Poly.Num(); i++) Idx.Add(i);
		if (Area(Poly) < 0) Algo::Reverse(Idx);
		auto Cross = [&](int32 A, int32 B, int32 C) { const P2 U = Poly[B] - Poly[A], V = Poly[C] - Poly[B]; return U.X * V.Y - U.Y * V.X; };
		for (int32 Guard = 0; Idx.Num() > 3 && Guard < 4000; Guard++)
		{
			bool bCut = false;
			const int32 N = Idx.Num();
			for (int32 k = 0; k < N && !bCut; k++)
			{
				const int32 A = Idx[(k + N - 1) % N], B = Idx[k], C = Idx[(k + 1) % N];
				const double X = Cross(A, B, C);
				if (FMath::Abs(X) < 1e-14) { Idx.RemoveAt(k); bCut = true; break; }   // (in a line: dropped)
				if (X < 0) continue;   // (a reflex corner)
				bool bEmpty = true;
				for (int32 m : Idx)
				{
					if (m == A || m == B || m == C) continue;
					const P2 P = Poly[m];
					auto Side = [&](const P2& U, const P2& V) { return (V.X - U.X) * (P.Y - U.Y) - (V.Y - U.Y) * (P.X - U.X); };
					if (Side(Poly[A], Poly[B]) > 1e-12 && Side(Poly[B], Poly[C]) > 1e-12 && Side(Poly[C], Poly[A]) > 1e-12) { bEmpty = false; break; }
				}
				if (!bEmpty) continue;
				Out.Append({ A, B, C });
				Idx.RemoveAt(k);
				bCut = true;
			}
			if (!bCut) break;
		}
		if (Idx.Num() == 3) Out.Append({ Idx[0], Idx[1], Idx[2] });
	}

	// ------------------------------------------------------------------ the holes

	struct FMiniHole
	{
		FString Name;
		int32 Par = 2;
		P2 At;                 // (the tee: the lane is in metres from it, played along +z)
		TArray<P2> Lane;
		P2 Cup;
		TArray<P2> Aims;      // (where to aim round the turns: the first from the tee)

		TArray<P2> Posts;
		TArray<TArray<P2>> Blocks;
		bool bRamp = false;    double RampZ0 = 0, RampZ1 = 0, RampRise = 0, RampEnd = 0;
		bool bMill = false;    double MillZ = 0, MillDepth = 0, MillHalf = 0;
		bool bTunnel = false;  double TunZ0 = 0, TunZ1 = 0;
		bool bVolcano = false; P2 VolC; double VolR = 0, VolH = 0;
		TArray<double> Humps;  double HumpLen = 0, HumpH = 0;
		bool bPond = false;    double PondZ0 = 0, PondZ1 = 0, PondHalfW = 0, Bridge = 0;
		bool bBend = false;    P2 BendC; double BendRin = 0, BendRout = 0, BendRise = 0;
		FLinearColor Colour;
		// (where it is)
		TArray<P2> LaneAbs;
		P2 Min, Max;
		P2 Abs(const P2& L) const { return L + At; }
	};

	TArray<P2> VolcanoLane(double Hw, double Cz, double R)
	{
		const double Zc = Cz - FMath::Sqrt(R * R - Hw * Hw), A0 = FMath::Atan2(Hw, Zc - Cz);
		TArray<P2> Pts = { P2(-Hw, -0.8), P2(Hw, -0.8), P2(Hw, Zc) };
		for (int32 k = 1; k < 40; k++) { const double A = A0 - 2 * A0 * k / 40; Pts.Add(P2(R * FMath::Sin(A), Cz + R * FMath::Cos(A))); }
		Pts.Add(P2(-Hw, Zc));
		return Pts;
	}
	// A U-turn: up the left leg, round the bend (centre C, radii Rin .. Rout) and back down the right
	TArray<P2> BendLane(const P2& C, double Rin, double Rout, double End)
	{
		TArray<P2> Pts = { P2(C.X - Rout, -0.8), P2(C.X - Rin, -0.8), P2(C.X - Rin, C.Y) };
		for (int32 k = 1; k < 24; k++) { const double A = PI - PI * k / 24; Pts.Add(P2(C.X + Rin * FMath::Cos(A), C.Y + Rin * FMath::Sin(A))); }
		Pts.Append({ P2(C.X + Rin, C.Y), P2(C.X + Rin, End), P2(C.X + Rout, End), P2(C.X + Rout, C.Y) });
		for (int32 k = 1; k < 24; k++) { const double A = PI * k / 24; Pts.Add(P2(C.X + Rout * FMath::Cos(A), C.Y + Rout * FMath::Sin(A))); }
		Pts.Add(P2(C.X - Rout, C.Y));
		return Pts;
	}

	const TArray<FMiniHole>& Holes()
	{
		static TArray<FMiniHole> All;
		if (All.Num()) return All;
		const uint32 Colours[] = { 0xd84a3a, 0x2f7fd8, 0xe8b628, 0xe0752d, 0x8e4fc4, 0x22a39a, 0x5aa832, 0xe05a9c, 0x3d5bd9 };
		auto Add = [&](const TCHAR* Name, int32 Par, P2 At, TArray<P2> Lane, P2 Cup) -> FMiniHole& {
			FMiniHole& H = All.AddDefaulted_GetRef();
			H.Name = Name; H.Par = Par; H.At = At; H.Lane = MoveTemp(Lane); H.Cup = Cup;
			return H;
		};
		{
			FMiniHole& H = Add(TEXT("Warm-up"), 2, P2(-12, 0), RectPts(-0.6, -0.8, 0.6, 8.6), P2(0, 7.7));
			H.Posts = { P2(-0.24, 3.4), P2(0.26, 5.4) };
		}
		{
			FMiniHole& H = Add(TEXT("Dogleg"), 3, P2(-2.5, 0), { P2(-0.6, -0.8), P2(0.6, -0.8), P2(0.6, 5.6), P2(4.6, 5.6), P2(4.6, 7.9), P2(-0.6, 7.9) }, P2(3.8, 6.75));
			H.Aims = { P2(0, 6.6) };
			H.Blocks = { { P2(-0.6, 6.2), P2(1.1, 7.9), P2(-0.6, 7.9) } };
			H.Posts = { P2(2.3, 7.3) };
		}
		{
			FMiniHole& H = Add(TEXT("The ramp"), 2, P2(9, 0), RectPts(-0.6, -0.8, 0.6, 10.2), P2(0, 9.0));
			H.bRamp = true; H.RampZ0 = 3.6; H.RampZ1 = 5.8; H.RampRise = 0.28; H.RampEnd = 10.2;
			H.Posts = { P2(-0.34, 7.3), P2(0.34, 7.3) };
		}
		{
			FMiniHole& H = Add(TEXT("The windmill"), 3, P2(-12, 15), { P2(-0.6, -0.8), P2(0.6, -0.8), P2(0.6, 3.9), P2(0.17, 5.0), P2(0.17, 6.9), P2(0.6, 7.8), P2(0.6, 12),
				P2(-0.6, 12), P2(-0.6, 7.8), P2(-0.17, 6.9), P2(-0.17, 5.0), P2(-0.6, 3.9) }, P2(0, 10.8));
			H.bMill = true; H.MillZ = 5.2; H.MillDepth = 1.6; H.MillHalf = 0.17;
		}
		{
			FMiniHole& H = Add(TEXT("Tunnel and volcano"), 3, P2(-1, 15), VolcanoLane(0.65, 9.6, 1.9), P2(0, 9.6));
			H.bTunnel = true; H.TunZ0 = 2.2; H.TunZ1 = 4.6;
			H.bVolcano = true; H.VolC = P2(0, 9.6); H.VolR = 1.6; H.VolH = 0.12;
		}
		{
			FMiniHole& H = Add(TEXT("Zig-zag"), 3, P2(10.5, 15), { P2(-0.6, -0.8), P2(0.6, -0.8), P2(0.6, 4.4), P2(-2.4, 4.4), P2(-2.4, 10), P2(-3.6, 10), P2(-3.6, 3.2), P2(-0.6, 3.2) }, P2(-3.0, 9.0));
			H.Aims = { P2(-0.66, 3.9), P2(-3.0, 3.8) };
			H.Posts = { P2(-3.0, 6.4) };
		}
		{
			FMiniHole& H = Add(TEXT("Camel humps"), 2, P2(-12, 30), RectPts(-0.7, -0.8, 0.7, 10.4), P2(0.22, 9.4));
			H.Humps = { 2.8, 4.9, 7.0 }; H.HumpLen = 1.5; H.HumpH = 0.075;
			H.Posts = { P2(-0.36, 8.3) };
		}
		{
			FMiniHole& H = Add(TEXT("The pond"), 3, P2(-1.5, 30), RectPts(-1.2, -0.8, 1.2, 10.2), P2(0.45, 8.9));
			H.bPond = true; H.PondZ0 = 3.8; H.PondZ1 = 6.2; H.PondHalfW = 1.2; H.Bridge = 0.3;
			H.Aims = { P2(0, 7.2) };   // (straight over the bridge)
		}
		{
			const P2 C(1.9, 6.0);
			FMiniHole& H = Add(TEXT("The big bend"), 3, P2(6.5, 30), BendLane(C, 1.3, 2.5, 0.2), P2(3.8, 1.3));
			H.Aims = { P2(-0.15, 7.6), P2(3.8, 5.0) };
			H.bBend = true; H.BendC = C; H.BendRin = 1.3; H.BendRout = 2.5; H.BendRise = 0.2;
		}
		for (int32 i = 0; i < All.Num(); i++)
		{
			FMiniHole& H = All[i];
			H.Colour = Hex(Colours[i % UE_ARRAY_COUNT(Colours)]);
			H.Min = P2(1e9, 1e9); H.Max = P2(-1e9, -1e9);
			for (const P2& L : H.Lane)
			{
				const P2 A = H.Abs(L);
				H.LaneAbs.Add(A);
				H.Min = P2(FMath::Min(H.Min.X, A.X), FMath::Min(H.Min.Y, A.Y));
				H.Max = P2(FMath::Max(H.Max.X, A.X), FMath::Max(H.Max.Y, A.Y));
			}
		}
		return All;
	}

	// The pond's water: two pieces either side of the bridge (local metres)
	void PondRects(const FMiniHole& H, TArray<FBox2D>& Out)
	{
		if (!H.bPond) return;
		Out.Add(FBox2D(P2(-H.PondHalfW, H.PondZ0), P2(-H.Bridge, H.PondZ1)));
		Out.Add(FBox2D(P2(H.Bridge, H.PondZ0), P2(H.PondHalfW, H.PondZ1)));
	}

	// The ground's height (course metres): the lawn flat, the land round it rising gently, and each
	// hole's ramp, volcano, humps, pond and banked bend
	double HeightAt(double X, double Z)
	{
		double H = 0;
		const double Out = FMath::Max(FMath::Max(LawnMin.X - X, X - LawnMax.X), FMath::Max(LawnMin.Y - Z, Z - LawnMax.Y));
		if (Out > 0)
		{
			const double Edge = FMath::Min(FMath::Min(X - GridX0, GridX1 - X), FMath::Min(Z - GridZ0, GridZ1 - Z));
			H += Smooth(0, 7, Out) * Smooth(0, 6, Edge) * FMath::Max(0.0, 0.45 + 0.3 * FMath::PerlinNoise2D(P2(X / 13, Z / 13)) + 0.12 * FMath::PerlinNoise2D(P2(X / 5 + 3, Z / 5)));
		}
		for (const FMiniHole& M : Holes())
		{
			if (X < M.Min.X - 2 || X > M.Max.X + 2 || Z < M.Min.Y - 2 || Z > M.Max.Y + 2) continue;
			const double LX = X - M.At.X, LZ = Z - M.At.Y;
			if (M.bRamp)
			{
				const double Across = 1 - Smooth(0.75, 1.9, FMath::Abs(LX)), Along = 1 - Smooth(M.RampEnd + 0.1, M.RampEnd + 1.3, LZ);
				H += M.RampRise * Smooth(M.RampZ0, M.RampZ1, LZ) * Across * Along;
			}
			if (M.bVolcano)
			{
				const double D = FMath::Sqrt(FMath::Square(LX - M.VolC.X) + FMath::Square(LZ - M.VolC.Y));
				if (D < M.VolR) H += M.VolH * FMath::Square(FMath::Cos(HALF_PI * D / M.VolR));
			}
			for (double A : M.Humps)
			{
				const double D = FMath::Abs(LZ - A);
				if (D < M.HumpLen / 2) H += M.HumpH * 0.5 * (1 + FMath::Cos(2 * PI * D / M.HumpLen)) * (1 - Smooth(0.9, 1.5, FMath::Abs(LX)));
			}
			if (M.bPond)
			{
				TArray<FBox2D> R;
				PondRects(M, R);
				for (const FBox2D& B : R)
					if (B.IsInside(P2(LX, LZ)))
						H -= 0.07 * Smooth(0, 0.08, FMath::Min(FMath::Min(LX - B.Min.X, B.Max.X - LX), FMath::Min(LZ - B.Min.Y, B.Max.Y - LZ)));
			}
			if (M.bBend)
			{
				const double R = LZ >= M.BendC.Y ? FMath::Sqrt(FMath::Square(LX - M.BendC.X) + FMath::Square(LZ - M.BendC.Y)) : FMath::Abs(LX - M.BendC.X);
				const double F = LZ >= M.BendC.Y ? 1.0 : Smooth(M.BendC.Y - 2.2, M.BendC.Y, LZ);
				const double T = (R - M.BendRin) / (M.BendRout - M.BendRin);
				if (T > 0) H += M.BendRise * F * Smooth(0, 1, T) * (1 - Smooth(M.BendRout + 0.05, M.BendRout + 0.7, R));
			}
		}
		return H;
	}

	otg::Surface SurfaceAt(double X, double Z)
	{
		for (const FMiniHole& M : Holes())
		{
			if (X < M.Min.X || X > M.Max.X || Z < M.Min.Y || Z > M.Max.Y || !InPoly(M.LaneAbs, X, Z)) continue;
			TArray<FBox2D> R;
			PondRects(M, R);
			for (const FBox2D& B : R) if (B.IsInside(P2(X - M.At.X, Z - M.At.Y))) return otg::Surface::Water;
			return otg::Surface::Green;
		}
		if (X >= LawnMin.X && X <= LawnMax.X && Z >= LawnMin.Y && Z <= LawnMax.Y) return otg::Surface::Tee;
		return otg::Surface::Rough;
	}

	FVector3d Normal(double X, double Z)
	{
		const double E = 0.04;
		return FVector3d(HeightAt(X - E, Z) - HeightAt(X + E, Z), 2 * E, HeightAt(X, Z - E) - HeightAt(X, Z + E)).GetSafeNormal();
	}

	// A mesh being made: triangles turned to face the way they're meant to
	struct FMeshOut
	{
		TArray<FVector> V, N;
		TArray<FVector2D> UV;
		TArray<int32> T;
		int32 Vert(const FVector& P, const FVector& Nrm) { V.Add(P); N.Add(Nrm); UV.Add(FVector2D(P.X / 100, P.Y / 100)); return V.Num() - 1; }
		// (Unreal's front face: (c - a) x (b - a) toward the viewer)
		void Tri(int32 A, int32 B, int32 C, const FVector& Facing)
		{
			const FVector G = FVector::CrossProduct(V[C] - V[A], V[B] - V[A]);
			if (FVector::DotProduct(G, Facing) < 0) Swap(B, C);
			T.Append({ A, B, C });
		}
		void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Facing)
		{
			const FVector Nrm = Facing.GetSafeNormal();
			const int32 a = Vert(A, Nrm), b = Vert(B, Nrm), c = Vert(C, Nrm), d = Vert(D, Nrm);
			Tri(a, b, c, Facing);
			Tri(a, c, d, Facing);
		}
	};
}
