// See OTGMiniGolf.h. The first six holes are the web version's (public/js/practice.js puttPutt),
// laid out the same; holes 7 to 9 are new.

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

DEFINE_LOG_CATEGORY_STATIC(LogOTGMiniGolf, Log, All);

const TCHAR* AOTGMiniGolf::CourseId = TEXT("minigolf");

namespace
{
	FAutoConsoleCommandWithWorldAndArgs MiniGolfCmd(TEXT("OTG.MiniGolf"), TEXT("Play the putt-putt (nine holes of mini golf)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) {
			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			if (UOTGCourseSubsystem* S = GI ? GI->GetSubsystem<UOTGCourseSubsystem>() : nullptr) S->PlayCourse(AOTGMiniGolf::CourseId);
		}));

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

// ------------------------------------------------------------------ the play data

UOTGCourseData* AOTGMiniGolf::MakeCourse(UObject* Outer)
{
	const double T0 = FPlatformTime::Seconds();
	Holes();   // (made here, before the heights and surfaces are worked out on several threads)
	UOTGCourseData* D = NewObject<UOTGCourseData>(Outer);
	D->CourseId = CourseId;
	D->CourseName = TEXT("Putt-Putt");
	D->Location = TEXT("Nine holes of mini golf");
	D->DefaultStimp = 9;
	D->Generated = CourseId;
	D->NormalSpanM = 0.12;   // (the ramps and humps are short)
	D->Level = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/OpenTeeGolf/Game/L_Course.L_Course")));
	D->LevelOrigin = FVector::ZeroVector;
	D->MinX = GridX0; D->MaxX = GridX1; D->MinZ = GridZ0; D->MaxZ = GridZ1;
	D->HeightSamples = GridN;
	D->HeightX0 = GridX0; D->HeightZ1 = GridZ1; D->MetresPerSample = GridStep;
	D->HeightBase = HeightBase; D->HeightRange = HeightRange;
	D->Heights.SetNumUninitialized(GridN * GridN);
	ParallelFor(GridN, [D](int32 J) {
		for (int32 I = 0; I < GridN; I++)
		{
			const double H = HeightAt(GridX0 + I * GridStep, GridZ1 - J * GridStep);
			D->Heights[J * GridN + I] = uint16(FMath::Clamp(FMath::RoundToInt((H - HeightBase) / HeightRange * 65535.0), 0, 65535));
		}
	});
	D->SurfaceW = SurfW; D->SurfaceH = SurfH; D->SurfaceMinX = SurfX0; D->SurfaceMaxZ = SurfZ1; D->SurfaceRes = SurfRes;
	D->Surfaces.SetNumUninitialized(SurfW * SurfH);
	ParallelFor(SurfH, [D](int32 J) {
		for (int32 I = 0; I < SurfW; I++) D->Surfaces[J * SurfW + I] = uint8(SurfaceAt(SurfX0 + (I + 0.5) * SurfRes, SurfZ1 - (J + 0.5) * SurfRes));
	});
	const TArray<FMiniHole>& All = Holes();
	for (int32 i = 0; i < All.Num(); i++)
	{
		const FMiniHole& M = All[i];
		FOTGHole H;
		H.Number = i + 1;
		H.Par = M.Par;
		H.Index = i + 1;
		H.Name = M.Name;
		H.GreenRadius = 1;
		auto At = [](const P2& P) { return FVector(P.X, HeightAt(P.X, P.Y), P.Y); };
		const FVector Tee = At(M.At), Pin = At(M.Abs(M.Cup));
		H.Tees.Add(TEXT("White"), Tee);
		for (const TCHAR* Day : { TEXT("Thursday"), TEXT("Friday"), TEXT("Saturday"), TEXT("Sunday") }) H.Pins.Add(Day, Pin);
		H.GreenCenter = Pin;
		H.Path.Add(Tee);
		for (const P2& A : M.Aims) { H.Aims.Add(At(M.Abs(A))); H.Path.Add(H.Aims.Last()); }
		H.Path.Add(Pin);
		D->Holes.Add(H);

		// What the ball bounces off (physics.js OBSTACLES 'bumper'): the boards, each side in pieces
		// half a metre long, a little thicker on the inside by the ball's radius (the collisions use
		// the ball's centre); posts; blocks; the windmill's walls
		auto Solid = [D](const TArray<P2>& Pts) {
			FOTGSolid S;
			S.Height = float(0.5 + WallUp);
			S.Kind = TEXT("bumper");
			S.Footprint = Pts;
			D->Solids.Add(S);
		};
		for (const FLaneEdge& E : Edges(M.LaneAbs))
		{
			const double Len = (E.B - E.A).Size();
			const int32 K = FMath::Max(1, FMath::CeilToInt32(Len / 0.5));
			const P2 U = (E.B - E.A) / Len;
			const double Ext = BallR + 0.01;
			for (int32 s = 0; s < K; s++)
			{
				const double S0 = s * Len / K - (s == 0 ? Ext : 0), S1 = (s + 1) * Len / K + (s == K - 1 ? Ext : 0);
				auto P = [&](double Along, double Off) { return E.A + U * Along + E.N * Off; };
				Solid({ P(S0, -BallR), P(S1, -BallR), P(S1, WallT), P(S0, WallT) });
			}
		}
		for (const P2& Q : M.Posts)
		{
			const double R = (0.06 + BallR) / FMath::Cos(PI / 8);
			TArray<P2> O;
			for (int32 k = 0; k < 8; k++) O.Add(M.Abs(Q) + P2(R * FMath::Cos(k * PI / 4), R * FMath::Sin(k * PI / 4)));
			Solid(O);
		}
		for (const TArray<P2>& B : M.Blocks)
		{
			TArray<P2> A;
			for (const P2& Q : B) A.Add(M.Abs(Q));
			Solid(Grow(A, BallR));
		}
		if (M.bMill)
		{
			const double Z0 = M.At.Y + M.MillZ, Z1 = Z0 + M.MillDepth;
			Solid(RectPts(M.At.X - 0.9, Z0, M.At.X - M.MillHalf - 0.01, Z1));
			Solid(RectPts(M.At.X + M.MillHalf + 0.01, Z0, M.At.X + 0.9, Z1));
		}
	}
	UE_LOG(LogOTGMiniGolf, Display, TEXT("Putt-putt: %d holes, %d boards and posts, made in %.0f ms"), D->Holes.Num(), D->Solids.Num(), (FPlatformTime::Seconds() - T0) * 1000);
	return D;
}

// ------------------------------------------------------------------ drawing it

AOTGMiniGolf::AOTGMiniGolf()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Base(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	// (the signs' lettering: the engine's own font and text material, referenced here so a packaged
	// game has them)
	static ConstructorHelpers::FObjectFinder<UFont> TextFont(TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TextMat(TEXT("/Engine/EngineMaterials/DefaultTextMaterialOpaque.DefaultTextMaterialOpaque"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;
	SphereMesh = Sphere.Object;
	ConeMesh = Cone.Object;
	BaseMat = Base.Object;
}

AOTGMiniGolf* AOTGMiniGolf::In(UWorld* World)
{
	if (World) for (TActorIterator<AOTGMiniGolf> It(World); It; ++It) return *It;
	return nullptr;
}

FVector AOTGMiniGolf::W(double X, double Y, double Z) const
{
	return Data->CourseToWorld(FVector(X, Y, Z));
}

UMaterialInstanceDynamic* AOTGMiniGolf::Mat(const FLinearColor& C)
{
	const uint32 Key = C.ToFColor(false).ToPackedARGB();
	if (const int32* I = MatIndex.Find(Key)) return Mats[*I];
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, this);
	M->SetVectorParameterValue(TEXT("Color"), C);
	MatIndex.Add(Key, Mats.Add(M));
	return M;
}

UInstancedStaticMeshComponent* AOTGMiniGolf::Ism(UStaticMesh* Mesh, const FLinearColor& C, bool bShadow)
{
	const FString Key = FString::Printf(TEXT("%s %08x %d"), *Mesh->GetName(), C.ToFColor(false).ToPackedARGB(), bShadow ? 1 : 0);
	if (TObjectPtr<UInstancedStaticMeshComponent>* F = Isms.Find(Key)) return *F;
	UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(this);
	I->SetStaticMesh(Mesh);
	I->SetMaterial(0, Mat(C));
	I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	I->SetCastShadow(bShadow);
	I->SetupAttachment(RootComponent);
	I->RegisterComponent();
	Isms.Add(Key, I);
	return I;
}

UStaticMeshComponent* AOTGMiniGolf::Shape(UStaticMesh* Mesh, const FVector& CentreW, const FVector& SizeCm, const FRotator& Rot, const FLinearColor& C, USceneComponent* Parent)
{
	UStaticMeshComponent* S = NewObject<UStaticMeshComponent>(this);
	S->SetStaticMesh(Mesh);
	S->SetMaterial(0, Mat(C));
	S->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	S->SetMobility(EComponentMobility::Movable);
	S->SetupAttachment(Parent ? Parent : RootComponent.Get());
	S->SetRelativeLocationAndRotation(CentreW, Rot);   // (the root's at the level's origin; a parent: relative to it)
	S->SetRelativeScale3D(SizeCm / 100.0);
	S->RegisterComponent();
	return S;
}

void AOTGMiniGolf::Box(const FVector& CentreW, const FVector& SizeCm, const FRotator& Rot, const FLinearColor& C, USceneComponent* Parent)
{
	Shape(CubeMesh, CentreW, SizeCm, Rot, C, Parent);
}

UProceduralMeshComponent* AOTGMiniGolf::NewProc()
{
	UProceduralMeshComponent* P = NewObject<UProceduralMeshComponent>(this);
	P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	P->SetupAttachment(RootComponent);
	P->RegisterComponent();
	return P;
}

void AOTGMiniGolf::Build(UOTGCourseData* D)
{
	Data = D;
	const double T0 = FPlatformTime::Seconds();
	SetActorLocation(FVector::ZeroVector);
	// (the wind's material parameter collections, registered with the world before anything's drawn:
	// a packaged game draws materials that read one not yet registered wrongly - as AOTGRuntimeCourse)
	if (UWorld* World = GetWorld())
		for (const TCHAR* Path : { TEXT("/Game/OpenTeeGolf/Game/MPC_Wind.MPC_Wind"), TEXT("/Game/Fab/MaterialParameterCollection/QMPC_GlobalFoliageActor.QMPC_GlobalFoliageActor") })
			if (UMaterialParameterCollection* C = LoadObject<UMaterialParameterCollection>(nullptr, Path)) World->GetParameterCollectionInstance(C);
	BuildLawn();
	BuildLanes();
	BuildBoards();
	BuildFeatures();
	BuildSurroundings();
	UE_LOG(LogOTGMiniGolf, Display, TEXT("Putt-putt drawn in %.0f ms (%d materials)"), (FPlatformTime::Seconds() - T0) * 1000, Mats.Num());
}

// The lawn: mown in stripes inside the fence, rougher beyond, rising gently to the trees (not under
// the lanes' felt); and a wide meadow round it all
void AOTGMiniGolf::BuildLawn()
{
	const double Cell = 0.3;
	const double X0 = GridX0, Z0 = GridZ0;
	const int32 NI = FMath::FloorToInt32((GridX1 - X0) / Cell), NJ = FMath::FloorToInt32((GridZ1 - Z0) / Cell);
	FMeshOut Sec[3];
	TArray<int32> Ids[3];
	for (TArray<int32>& A : Ids) A.Init(-1, (NI + 1) * (NJ + 1));
	auto Vert = [&](int32 S, int32 I, int32 J) {
		int32& Id = Ids[S][J * (NI + 1) + I];
		if (Id < 0)
		{
			const double X = X0 + I * Cell, Z = Z0 + J * Cell;
			const FVector3d N = Normal(X, Z);
			Id = Sec[S].Vert(W(X, HeightAt(X, Z) - 0.008, Z), FVector(N.X, -N.Z, N.Y));
		}
		return Id;
	};
	const TArray<FMiniHole>& All = Holes();
	for (int32 J = 0; J < NJ; J++)
		for (int32 I = 0; I < NI; I++)
		{
			const double X = X0 + I * Cell, Z = Z0 + J * Cell, CX = X + Cell / 2, CZ = Z + Cell / 2;
			// (under a lane's felt: left out)
			bool bUnder = false;
			for (const FMiniHole& M : All)
				if (CX > M.Min.X && CX < M.Max.X && CZ > M.Min.Y && CZ < M.Max.Y && InPoly(M.LaneAbs, X, Z) && InPoly(M.LaneAbs, X + Cell, Z) && InPoly(M.LaneAbs, X, Z + Cell) && InPoly(M.LaneAbs, X + Cell, Z + Cell))
					bUnder = true;
			if (bUnder) continue;
			const bool bLawn = CX >= LawnMin.X && CX <= LawnMax.X && CZ >= LawnMin.Y && CZ <= LawnMax.Y;
			const int32 S = !bLawn ? 2 : (FMath::FloorToInt32((CX - LawnMin.X) / 1.75) & 1);
			const int32 A = Vert(S, I, J), B = Vert(S, I + 1, J), C = Vert(S, I, J + 1), Dd = Vert(S, I + 1, J + 1);
			Sec[S].Tri(A, B, Dd, FVector::UpVector);
			Sec[S].Tri(A, Dd, C, FVector::UpVector);
		}
	UProceduralMeshComponent* P = NewProc();
	const FLinearColor Colours[3] = { Hex(0x4a8a3a), Hex(0x55963f), Hex(0x4a7f38) };
	for (int32 S = 0; S < 3; S++)
	{
		P->CreateMeshSection_LinearColor(S, Sec[S].V, Sec[S].T, Sec[S].N, Sec[S].UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		P->SetMaterial(S, Mat(Colours[S]));
	}
	// the meadow beyond, out to the horizon
	const FVector Mid = W((GridX0 + GridX1) / 2, -0.07, (GridZ0 + GridZ1) / 2);
	Box(Mid, FVector(600000, 600000, 10), FRotator::ZeroRotator, Hex(0x4a8a35));
}

// The felt: each lane cut into 10 cm squares (clipped to its outline), every corner on the ground,
// so it follows the ramps, humps and banks
void AOTGMiniGolf::BuildLanes()
{
	const FLinearColor Felt = Hex(0x23803d);
	UProceduralMeshComponent* P = NewProc();
	FMeshOut M;
	const double C = 0.1;
	for (const FMiniHole& H : Holes())
	{
		for (double Z = FMath::FloorToDouble(H.Min.Y / C) * C; Z < H.Max.Y; Z += C)
			for (double X = FMath::FloorToDouble(H.Min.X / C) * C; X < H.Max.X; X += C)
			{
				const TArray<P2> Piece = ClipBox(H.LaneAbs, X, Z, X + C, Z + C);
				if (Piece.Num() < 3 || FMath::Abs(Area(Piece)) < 1e-8) continue;
				TArray<int32> Tris;
				Triangulate(Piece, Tris);
				TArray<int32> Ids;
				for (const P2& Q : Piece)
				{
					const FVector3d N = Normal(Q.X, Q.Y);
					Ids.Add(M.Vert(W(Q.X, HeightAt(Q.X, Q.Y) + 0.004, Q.Y), FVector(N.X, -N.Z, N.Y)));
				}
				for (int32 k = 0; k + 2 < Tris.Num(); k += 3) M.Tri(Ids[Tris[k]], Ids[Tris[k + 1]], Ids[Tris[k + 2]], FVector::UpVector);
			}
	}
	P->CreateMeshSection_LinearColor(0, M.V, M.T, M.N, M.UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
	P->SetMaterial(0, Mat(Felt));
	P->SetCastShadow(false);
}

// The boards, in the hole's colour with a white cap, in pieces that follow the ground; posts with a
// coloured band; the blocks
void AOTGMiniGolf::BuildBoards()
{
	const FLinearColor White = Hex(0xf2f0ea);
	UInstancedStaticMeshComponent* Caps = Ism(CubeMesh, White);
	UInstancedStaticMeshComponent* PostIsm = Ism(CylinderMesh, White);
	for (const FMiniHole& H : Holes())
	{
		UInstancedStaticMeshComponent* Boards = Ism(CubeMesh, H.Colour);
		for (const FLaneEdge& E : Edges(H.LaneAbs))
		{
			const double Len = (E.B - E.A).Size();
			const int32 K = FMath::Max(1, FMath::CeilToInt32(Len / 0.4));
			for (int32 s = 0; s < K; s++)
			{
				const P2 Off = E.N * (WallT / 2);
				const P2 Q0 = E.A + (E.B - E.A) * (double(s) / K) + Off, Q1 = E.A + (E.B - E.A) * (double(s + 1) / K) + Off;
				const double Ext = (s == 0 ? WallT / 2 : 0) + (s == K - 1 ? WallT / 2 : 0);
				const FVector A = W(Q0.X, HeightAt(Q0.X, Q0.Y), Q0.Y), B = W(Q1.X, HeightAt(Q1.X, Q1.Y), Q1.Y);
				const FVector Along = (B - A).GetSafeNormal();
				const FQuat Q = FRotationMatrix::MakeFromXZ(Along, FVector::UpVector).ToQuat();
				const FVector Up = Q.GetUpVector();
				// (each side's first and last piece reach past the corner to meet the next side)
				const double L = (B - A).Size() + Ext * 100;
				const FVector Mid = (A + B) / 2;
				Boards->AddInstance(FTransform(Q, Mid + Up * ((WallUp - WallDown) / 2 * 100), FVector(L / 100, WallT, WallUp + WallDown)), true);
				Caps->AddInstance(FTransform(Q, Mid + Up * (WallUp * 100 + 0.6), FVector(L / 100, WallT + 0.012, 0.012)), true);
			}
		}
		UInstancedStaticMeshComponent* Band = Ism(CylinderMesh, H.Colour);
		for (const P2& Q : H.Posts)
		{
			const P2 A = H.Abs(Q);
			const FVector G = W(A.X, HeightAt(A.X, A.Y), A.Y);
			PostIsm->AddInstance(FTransform(FQuat::Identity, G + FVector(0, 0, 7), FVector(0.12, 0.12, 0.24)), true);
			Band->AddInstance(FTransform(FQuat::Identity, G + FVector(0, 0, 12), FVector(0.124, 0.124, 0.06)), true);
		}
		// the blocks: their outline from below the felt to the boards' height
		for (const TArray<P2>& B : H.Blocks)
		{
			TArray<P2> A;
			double G0 = 1e9;
			for (const P2& Q : B) { A.Add(H.Abs(Q)); G0 = FMath::Min(G0, HeightAt(A.Last().X, A.Last().Y)); }
			if (Area(A) < 0) Algo::Reverse(A);
			FMeshOut M;
			const double Y0 = G0 - 0.03, Y1 = G0 + WallUp + 0.01;
			TArray<int32> Tris;
			Triangulate(A, Tris);
			TArray<int32> Top;
			for (const P2& Q : A) Top.Add(M.Vert(W(Q.X, Y1, Q.Y), FVector::UpVector));
			for (int32 k = 0; k + 2 < Tris.Num(); k += 3) M.Tri(Top[Tris[k]], Top[Tris[k + 1]], Top[Tris[k + 2]], FVector::UpVector);
			for (const FLaneEdge& E : Edges(A))
				M.Quad(W(E.A.X, Y0, E.A.Y), W(E.B.X, Y0, E.B.Y), W(E.B.X, Y1, E.B.Y), W(E.A.X, Y1, E.A.Y), FVector(E.N.X, -E.N.Y, 0));
			UProceduralMeshComponent* P = NewProc();
			P->CreateMeshSection_LinearColor(0, M.V, M.T, M.N, M.UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
			P->SetMaterial(0, Mat(H.Colour));
		}
	}
}

// A sign on two posts, turned to face a spot: lines of text (the first big), Wm x Hm, its bottom
// Lift above the ground, a coloured stripe along its top
void AOTGMiniGolf::Sign(double X, double Z, double FaceX, double FaceZ, const TArray<FString>& Lines, double Wm, double Hm, double Lift, const FLinearColor& Stripe)
{
	const double G = HeightAt(X, Z);
	const FVector Base = W(X, G, Z);
	const FVector To = W(FaceX, G, FaceZ) - Base;
	const FRotator Rot(0, To.Rotation().Yaw, 0);
	const FVector Fwd = Rot.Vector(), Side = FRotationMatrix(Rot).GetUnitAxis(EAxis::Y);
	const FLinearColor Wood = Hex(0x6b4a2e), Board = Hex(0xf6f3ea);
	for (const double S : { -1.0, 1.0 })
		Box(Base + Side * (S * Wm * 40) + FVector(0, 0, (Lift + Hm * 0.5) * 50), FVector(5, 5, (Lift + Hm * 0.5) * 100), Rot, Wood);
	const FVector Centre = Base + FVector(0, 0, (Lift + Hm / 2) * 100);
	Box(Centre, FVector(4, Wm * 100, Hm * 100), Rot, Board);
	Box(Centre + FVector(0, 0, Hm * 50 - 4), FVector(4.6, Wm * 100 + 1, 8), Rot, Stripe);
	const double Big = Hm * 100 * (Lines.Num() > 2 ? 0.34 : 0.4), Small = Big * 0.5;
	double Y = Hm * 50 - 10;
	for (int32 i = 0; i < Lines.Num(); i++)
	{
		const double Size = i == 0 ? Big : Small;
		UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
		T->SetupAttachment(RootComponent);
		T->SetText(FText::FromString(Lines[i]));
		T->SetHorizontalAlignment(EHTA_Center);
		T->SetVerticalAlignment(EVRTA_TextTop);
		T->SetWorldSize(Size);
		T->SetTextRenderColor(FColor(40, 40, 44));
		T->SetCastShadow(false);
		T->SetRelativeLocationAndRotation(Centre + Fwd * 2.6 + FVector(0, 0, Y), Rot);
		T->RegisterComponent();
		Y -= Size * 1.05;
	}
}

void AOTGMiniGolf::BuildFeatures()
{
	const TArray<FMiniHole>& All = Holes();
	for (int32 i = 0; i < All.Num(); i++)
	{
		const FMiniHole& H = All[i];
		// the tee mat
		Box(W(H.At.X, HeightAt(H.At.X, H.At.Y) + 0.0045, H.At.Y - 0.05), FVector(50, 34, 0.3), FRotator::ZeroRotator, Hex(0x2b2d2f));
		// the hole's sign beside the lane, facing the tee
		const double SX = H.Min.X - 0.9, SZ = H.At.Y + 1.4;
		Sign(SX, SZ, H.At.X, H.At.Y - 2.6, { FString::FromInt(i + 1), H.Name, FString::Printf(TEXT("Par %d"), H.Par) }, 0.72, 0.5, 0.4, H.Colour);
		if (H.bMill) BuildWindmill(i);
		if (H.bTunnel) BuildTunnel(i);
		if (H.bVolcano)
		{
			// rocks round the crater's rim, outside the boards
			const P2 C = H.Abs(H.VolC);
			for (int32 k = 0; k < 22; k++)
			{
				// (all round but where the lane comes in, from the south)
				const double A = -PI + 0.6 + k * (2 * PI - 1.2) / 21, R = 2.12 + 0.06 * FMath::Sin(k * 2.3);
				const double X = C.X + FMath::Sin(A) * R, Z = C.Y + FMath::Cos(A) * R;
				const double S = 0.16 + 0.07 * FMath::Abs(FMath::Sin(k * 1.7));
				Shape(SphereMesh, W(X, HeightAt(X, Z) + S * 0.2, Z), FVector(S * 130, S * 110, S * 70), FRotator(0, k * 37, 0), Hex(k % 3 ? 0x5a5550 : 0x6e5f52));
			}
		}
		if (H.bPond)
		{
			TArray<FBox2D> R;
			PondRects(H, R);
			for (const FBox2D& B : R)
			{
				const P2 C = H.Abs(B.GetCenter()), S = B.GetSize();
				Box(W(C.X, -0.032 - 0.005, C.Y), FVector(S.X * 100, S.Y * 100, 1), FRotator::ZeroRotator, Hex(0x2a6f9e));
			}
			// lily pads and the bridge: planks across, a beam each side
			for (const P2& L : { P2(-0.75, 4.4), P2(-0.5, 5.6), P2(0.8, 4.3), P2(0.62, 5.75), P2(1.0, 5.1) })
			{
				const P2 A = H.Abs(L);
				Shape(CylinderMesh, W(A.X, -0.031, A.Y), FVector(16, 16, 0.6), FRotator::ZeroRotator, Hex(0x3f8a2f));
			}
			const P2 Pink = H.Abs(P2(0.62, 5.75));
			Shape(SphereMesh, W(Pink.X, -0.02, Pink.Y), FVector(5, 5, 3), FRotator::ZeroRotator, Hex(0xf2a0c0));
			for (double Z = H.PondZ0 - 0.06; Z <= H.PondZ1 + 0.06; Z += 0.11)
			{
				const P2 A = H.Abs(P2(0, Z));
				Box(W(A.X, 0.0025, A.Y), FVector(H.Bridge * 200 + 8, 9, 0.6), FRotator::ZeroRotator, Hex(0x9a6a3e));
			}
			for (const double S : { -1.0, 1.0 })
			{
				const P2 A = H.Abs(P2(S * (H.Bridge + 0.03), (H.PondZ0 + H.PondZ1) / 2));
				Box(W(A.X, -0.01, A.Y), FVector(4, (H.PondZ1 - H.PondZ0 + 0.2) * 100, 3), FRotator::ZeroRotator, Hex(0x6b4a2e));
			}
		}
	}
	// the flower beds between the holes
	const P2 Beds[] = { P2(-7.3, 4), P2(3.5, 3), P2(-7, 19.5), P2(4.5, 20), P2(-15.2, 12.5), P2(13.5, 12.5), P2(-3, 12.2), P2(8.5, 27.5), P2(-8, 27.8),
		P2(-7, 35), P2(2.6, 35.5), P2(8.4, 33.2), P2(-15.2, 26.5), P2(13.6, 26.5), P2(-8.5, 42.2), P2(3.5, 42.5), P2(13, 40), P2(-15, 40) };
	const FLinearColor Blooms[] = { Hex(0xe53950), Hex(0xffd23f), Hex(0xf3f3f3), Hex(0xb04fd8), Hex(0xff7a3d), Hex(0x3f7fe0) };
	UInstancedStaticMeshComponent* Bush = Ism(SphereMesh, Hex(0x2f6a2a));
	UInstancedStaticMeshComponent* Soil = Ism(CylinderMesh, Hex(0x4a3526), false);
	uint32 Seed = 5;
	auto Rnd = [&Seed]() { Seed = Seed * 1664525u + 1013904223u; return (Seed >> 8) / double(1 << 24); };
	auto Clear = [&](double X, double Z, double D) {
		for (const FMiniHole& M : All) if (InPoly(M.LaneAbs, X, Z) || NearEdge(M.LaneAbs, X, Z) < D) return false;
		return true;
	};
	for (const P2& B : Beds)
	{
		if (Clear(B.X, B.Y, 1.5)) Soil->AddInstance(FTransform(FQuat::Identity, W(B.X, HeightAt(B.X, B.Y), B.Y), FVector(2.6, 2.0, 0.03)), true);
		for (int32 k = 0; k < 70; k++)
		{
			const double A = Rnd() * 2 * PI, R = FMath::Sqrt(Rnd()) * 1.1, X = B.X + FMath::Cos(A) * R * 1.3, Z = B.Y + FMath::Sin(A) * R, C = Rnd();
			if (!Clear(X, Z, 0.45)) continue;
			const double Y = HeightAt(X, Z);
			Bush->AddInstance(FTransform(FQuat::Identity, W(X, Y + 0.06, Z), FVector(0.26, 0.26, 0.21)), true);
			Ism(SphereMesh, Blooms[FMath::Min(5, int32(C * 6))])->AddInstance(FTransform(FQuat::Identity, W(X + (C - 0.5) * 0.08, Y + 0.17, Z + (C - 0.5) * 0.06), FVector(0.1)), true);
		}
	}
}

// The windmill: a little house astride the lane with the tunnel through it, a roof and four sails
// turning across the tunnel's mouth
void AOTGMiniGolf::BuildWindmill(int32 Index)
{
	const FMiniHole& H = Holes()[Index];
	const double CX = H.At.X, Z0 = H.At.Y + H.MillZ, Z1 = Z0 + H.MillDepth, ZM = (Z0 + Z1) / 2, Half = H.MillHalf, HT = 1.5;
	const double Y = HeightAt(CX, ZM) - 0.02;
	const FLinearColor Wall = Hex(0x5d8fbf), Trim = Hex(0x7a4a2a), Roof = Hex(0xb8402e), Glass = Hex(0x2d4a66), Cream = Hex(0xf6f3ea);
	auto Block = [&](double X0, double X1, double Y0, double Y1, const FLinearColor& C) {
		Box(W(CX + (X0 + X1) / 2, Y + (Y0 + Y1) / 2, ZM), FVector((X1 - X0) * 100, H.MillDepth * 100, (Y1 - Y0) * 100), FRotator::ZeroRotator, C);
	};
	Block(-0.9, -Half, 0, HT, Wall);
	Block(Half, 0.9, 0, HT, Wall);
	Block(-Half, Half, 0.26, HT, Wall);
	Block(-0.95, 0.95, HT, HT + 0.08, Trim);
	// (the tunnel's mouth framed in wood)
	Box(W(CX, Y + 0.28, Z0 - 0.01), FVector(Half * 200 + 10, 3, 5), FRotator::ZeroRotator, Trim);
	for (const double S : { -1.0, 1.0 }) Box(W(CX + S * (Half + 0.025), Y + 0.14, Z0 - 0.01), FVector(5, 3, 28), FRotator::ZeroRotator, Trim);
	// the roof: a cone, and the windows and a door on the front
	Shape(ConeMesh, W(CX, Y + HT + 0.08 + 0.5, ZM), FVector(215, H.MillDepth * 100 + 30, 100), FRotator::ZeroRotator, Roof);
	for (const double S : { -1.0, 1.0 })
	{
		Box(W(CX + S * 0.5, Y + 0.95, Z0 - 0.005), FVector(28, 2, 32), FRotator::ZeroRotator, Glass);
		Box(W(CX + S * 0.5, Y + 0.95, Z0 - 0.008), FVector(32, 1, 4), FRotator::ZeroRotator, Cream);
		Box(W(CX + S * 0.5, Y + 0.95, Z1 + 0.005), FVector(28, 2, 32), FRotator::ZeroRotator, Glass);
	}
	// the sails: a hub on the front, each sail reaching to just off the lane
	const double HubY = HeightAt(CX, Z0) + 0.985;
	USceneComponent* Pivot = NewObject<USceneComponent>(this);
	Pivot->SetMobility(EComponentMobility::Movable);
	Pivot->SetupAttachment(RootComponent);
	Pivot->SetRelativeLocation(W(CX, HubY, Z0 - 0.1));
	Pivot->RegisterComponent();
	Sails.Add(Pivot);
	for (int32 k = 0; k < 4; k++)
	{
		USceneComponent* Arm = NewObject<USceneComponent>(this);
		Arm->SetMobility(EComponentMobility::Movable);
		Arm->SetupAttachment(Pivot);
		Arm->SetRelativeRotation(FQuat(FVector::YAxisVector, k * HALF_PI));
		Arm->RegisterComponent();
		Box(FVector(0, 0, -47), FVector(15, 2, 78), FRotator::ZeroRotator, Cream, Arm);
		Box(FVector(0, 0, -91), FVector(15, 2.2, 12), FRotator::ZeroRotator, Roof, Arm);
		Box(FVector(0, 0, -48.5), FVector(3, 3, 97), FRotator::ZeroRotator, Trim, Arm);
	}
	Shape(CylinderMesh, FVector(0, -4, 0), FVector(14, 14, 16), FRotator(0, 0, 90), Trim, Pivot);
}

// The tunnel: a stone arch over the lane
void AOTGMiniGolf::BuildTunnel(int32 Index)
{
	const FMiniHole& H = Holes()[Index];
	const double CX = H.At.X, Z0 = H.At.Y + H.TunZ0, Z1 = H.At.Y + H.TunZ1, Outer = 0.82, Inner = 0.72;
	const double Y0 = HeightAt(CX, Z0) - 0.04;
	FMeshOut M;
	const int32 N = 24;
	auto P = [&](double R, int32 k, double Z) { const double A = PI * k / N; return W(CX + R * FMath::Cos(A), Y0 + R * FMath::Sin(A), Z); };
	for (int32 k = 0; k < N; k++)
	{
		const double A = PI * (k + 0.5) / N;
		const FVector Out(FMath::Cos(A), 0, FMath::Sin(A));
		M.Quad(P(Outer, k, Z0), P(Outer, k + 1, Z0), P(Outer, k + 1, Z1), P(Outer, k, Z1), Out);
		M.Quad(P(Inner, k, Z0), P(Inner, k + 1, Z0), P(Inner, k + 1, Z1), P(Inner, k, Z1), -Out);
		// the ends: the arch's face, toward the player and away
		M.Quad(P(Inner, k, Z0), P(Inner, k + 1, Z0), P(Outer, k + 1, Z0), P(Outer, k, Z0), FVector(0, 1, 0));
		M.Quad(P(Inner, k, Z1), P(Inner, k + 1, Z1), P(Outer, k + 1, Z1), P(Outer, k, Z1), FVector(0, -1, 0));
	}
	UProceduralMeshComponent* Proc = NewProc();
	Proc->CreateMeshSection_LinearColor(0, M.V, M.T, M.N, M.UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
	Proc->SetMaterial(0, Mat(Hex(0x8c8780)));
	// (a keystone over the mouth, both ends)
	for (const double Z : { Z0 - 0.02, Z1 + 0.02 }) Box(W(CX, Y0 + Outer - 0.02, Z), FVector(16, 6, 16), FRotator::ZeroRotator, Hex(0x6f6a63));
}

// The fence round the lawn (open at the front for the way in), the welcome sign, the trees and the
// hills beyond
void AOTGMiniGolf::BuildSurroundings()
{
	const FLinearColor Paint = Hex(0xf4f1ea);
	UInstancedStaticMeshComponent* Pickets = Ism(CubeMesh, Paint);
	const P2 Corners[4] = { LawnMin, P2(LawnMax.X, LawnMin.Y), LawnMax, P2(LawnMin.X, LawnMax.Y) };
	const double GateX = -0.75;
	for (int32 s = 0; s < 4; s++)
	{
		const P2 A = Corners[s], B = Corners[(s + 1) % 4];
		const double Len = (B - A).Size();
		const int32 N = FMath::RoundToInt32(Len / 0.22);
		const bool bAlongX = FMath::Abs(B.X - A.X) > FMath::Abs(B.Y - A.Y);
		const FQuat Q = bAlongX ? FQuat::Identity : FQuat(FVector::UpVector, HALF_PI);
		for (int32 k = 0; k <= N; k++)
		{
			const P2 P = A + (B - A) * (double(k) / N);
			if (s == 0 && FMath::Abs(P.X - GateX) < 1.6) continue;   // (the gate)
			Pickets->AddInstance(FTransform(Q, W(P.X, HeightAt(P.X, P.Y) + 0.33, P.Y), FVector(0.07, 0.025, 0.75)), true);
		}
		// two rails behind the pickets
		auto Rail = [&](double X0, double X1) {
			const P2 Q0 = A + (B - A) * X0, Q1 = A + (B - A) * X1, M = (Q0 + Q1) / 2;
			const double L = (Q1 - Q0).Size() * 100;
			for (const double H : { 0.18, 0.52 })
				Pickets->AddInstance(FTransform(Q, W(M.X, HeightAt(M.X, M.Y) + H, M.Y) + (bAlongX ? FVector(0, (s == 0 ? -1 : 1) * 2.5, 0) : FVector((s == 1 ? 1 : -1) * 2.5, 0, 0)),
					FVector(L / 100, 0.025, 0.05)), true);   // (Q turns it along the side)
		};
		if (s == 0)
		{
			const double G0 = (GateX - 1.6 - A.X) / (B.X - A.X), G1 = (GateX + 1.6 - A.X) / (B.X - A.X);
			Rail(0, G0); Rail(G1, 1);
		}
		else Rail(0, 1);
	}
	// gate posts with a sign over the way in
	for (const double S : { -1.0, 1.0 }) Box(W(GateX + S * 1.65, 0.6, LawnMin.Y), FVector(12, 12, 120), FRotator::ZeroRotator, Paint);
	// (and the course's name, big, over the far fence: seen down every hole)
	Sign(GateX, LawnMax.Y + 1.6, GateX, LawnMin.Y, { TEXT("PUTT-PUTT"), TEXT("Nine holes  ·  OpenTeeGolf") }, 4.2, 1.4, 1.4, Hex(0xd84a3a));

	// trees: a ring round the lawn, thicker further out, and woods on the hills; broadleaves (a
	// trunk and a few round crowns) and firs (stacked cones)
	uint32 Seed = 11;
	auto Rnd = [&Seed]() { Seed = Seed * 1664525u + 1013904223u; return (Seed >> 8) / double(1 << 24); };
	const FLinearColor Leaves[] = { Hex(0x3f7a2e), Hex(0x4c8a34), Hex(0x356b2a), Hex(0x5a9440) };
	const FLinearColor Needles[] = { Hex(0x2a5a33), Hex(0x24503a) };
	UInstancedStaticMeshComponent* Trunks = Ism(CylinderMesh, Hex(0x5b4030));
	auto Tree = [&](double X, double Z, double Ht) {
		const double G = HeightAt(X, Z);
		const FVector Base = W(X, G, Z);
		if (Rnd() < 0.3)
		{
			// a fir
			const double R = Ht * 0.24;
			Trunks->AddInstance(FTransform(FQuat::Identity, Base + FVector(0, 0, Ht * 12), FVector(R * 0.3, R * 0.3, Ht * 0.24)), true);
			UInstancedStaticMeshComponent* C = Ism(ConeMesh, Needles[int32(Rnd() * 2) % 2]);
			for (int32 k = 0; k < 3; k++)
			{
				const double S = 1 - k * 0.26, Hc = Ht * 0.42 * S;
				C->AddInstance(FTransform(FQuat(FVector::UpVector, Rnd() * 6), Base + FVector(0, 0, (Ht * (0.22 + k * 0.24) + Hc / 2) * 100), FVector(R * 2 * S, R * 2 * S, Hc)), true);
			}
			return;
		}
		const double R = Ht * 0.3;
		Trunks->AddInstance(FTransform(FQuat::Identity, Base + FVector(0, 0, Ht * 30), FVector(R * 0.18, R * 0.18, Ht * 0.6)), true);
		UInstancedStaticMeshComponent* C = Ism(SphereMesh, Leaves[int32(Rnd() * 4) % 4]);
		C->AddInstance(FTransform(FQuat::Identity, Base + FVector(0, 0, (Ht - R * 0.9) * 100), FVector(R * 2, R * 2, R * 1.8)), true);
		for (int32 k = 0; k < 3; k++)
		{
			const double A = Rnd() * 2 * PI, S = 0.55 + Rnd() * 0.25;
			C->AddInstance(FTransform(FQuat::Identity, Base + FVector(FMath::Cos(A) * R * 55, FMath::Sin(A) * R * 55, (Ht - R * (1.1 + Rnd() * 0.5)) * 100), FVector(R * 2 * S)), true);
		}
	};
	const P2 Centre((LawnMin.X + LawnMax.X) / 2, (LawnMin.Y + LawnMax.Y) / 2);
	auto OutOfLawn = [](double X, double Z) { return FMath::Max(FMath::Max(LawnMin.X - X, X - LawnMax.X), FMath::Max(LawnMin.Y - Z, Z - LawnMax.Y)); };
	// (close round the fence, thinning out; none just in front of the way in, where the cameras are
	// for the first holes)
	for (int32 k = 0; k < 1400; k++)
	{
		const double X = Centre.X + (Rnd() * 2 - 1) * 75, Z = Centre.Y + (Rnd() * 2 - 1) * 80, D = OutOfLawn(X, Z);
		if (D < 3 || (Z < LawnMin.Y && Z > LawnMin.Y - 12 && D < 12)) continue;
		if (Rnd() > (D < 12 ? 0.55 : 0.32 * (1 - Smooth(30, 60, D)))) continue;
		Tree(X, Z, 6 + Rnd() * 7 + FMath::Min(D, 20.0) * 0.25);
	}
	// hills on the horizon
	for (int32 k = 0; k < 14; k++)
	{
		const double A = k / 14.0 * 2 * PI + Rnd() * 0.3, D = 260 + Rnd() * 160, R = 90 + Rnd() * 120;
		const double X = Centre.X + FMath::Cos(A) * D, Z = Centre.Y + FMath::Sin(A) * D;
		Shape(SphereMesh, W(X, -R * 0.15, Z), FVector(R * 200, R * 170, R * 55), FRotator(0, Rnd() * 90, 0), k & 1 ? Hex(0x4b7f3a) : Hex(0x557f42));
	}
}

void AOTGMiniGolf::Tick(float Dt)
{
	Super::Tick(Dt);
	Clock += Dt;
	for (USceneComponent* S : Sails) if (S) S->SetRelativeRotation(FQuat(FVector::YAxisVector, Clock * SailSpeed));
}

// The sails sweeping across the windmill's tunnel (practice.js movers), from now on the clock
void AOTGMiniGolf::AddMovers(otg::ShotOptions& O) const
{
	const double T0 = Clock;
	for (const FMiniHole& H : Holes())
	{
		if (!H.bMill) continue;
		const double GX = H.At.X, GZ = H.At.Y + H.MillZ - 0.1, Half = H.MillHalf;
		O.movers.push_back([GX, GZ, Half, T0](const otg::V3& A, const otg::V3& B, double T, otg::Hit& Out) {
			const double DA = A.z - GZ, DB = B.z - GZ;
			if ((DA < 0) == (DB < 0) || DA == DB) return false;
			const double F = DA / (DA - DB), X = A.x + (B.x - A.x) * F, Y = A.y + (B.y - A.y) * F;
			if (FMath::Abs(X - GX) > Half + 0.03 || Y - HeightAt(X, GZ) > 0.35) return false;
			const double Q = HALF_PI, M = FMath::Fmod(FMath::Fmod((T0 + T) * SailSpeed, Q) + Q, Q);
			if (FMath::Min(M, Q - M) > SailBlock) return false;
			const double Dir = DB > DA ? 1 : -1;
			Out.kind = "blade";
			Out.normal = otg::V3{ 0, 0, -Dir };
			Out.point = otg::V3{ X, Y, GZ - Dir * 0.01 };
			return true;
		});
	}
}
