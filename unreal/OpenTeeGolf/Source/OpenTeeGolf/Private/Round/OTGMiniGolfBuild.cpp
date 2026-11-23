// Drawing the putt-putt (see OTGMiniGolf.h): the lawn, the lanes, the boards, the signs, the
// features (windmill, tunnel) and the surroundings.

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

#include "Round/OTGMiniGolfLayout.h"

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
