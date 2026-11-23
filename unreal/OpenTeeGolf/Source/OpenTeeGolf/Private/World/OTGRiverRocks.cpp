// Water that goes round the rocks in a creek (Tidewright's rivers can't: their current is a flow map
// along the channel - the ripples and foam slide down it in straight lines and ran under a rock).
// A river with a rock breaking its surface gets the plugin's surface mesh finer (rows every half
// metre) and then, round each rock:
// - its texture coordinates (river space, metres: across and along) bent as potential flow bends round
//   a post - the ripples, which the material slides along "along", part in front of the rock, pass it
//   on both sides and close up behind it; they slow where the water piles up against it
// - the water raised a few cm on the upstream side (it banks up against the rock) and a touch lower
//   in the eddy behind it
// (no foam of its own: the river's foams by its banks and rapids alone - BankFoamWidth, off on our
//  creeks - and a ring of it made from the vertex colour lit the shallows along the banks too)
// -nowaterrocks: as the plugin draws it.

#include "World/OTGRuntimeCourse.h"
#include "Course/OTGCourseData.h"

#include "Components/SplineComponent.h"
#include "Misc/CommandLine.h"
#include "ProceduralMeshComponent.h"
#include "TidewrightRiver.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGRiverRocks, Log, All);

namespace
{
	struct FObstacle { double Along = 0, Across = 0, R = 0; };   // (river space, cm)
}

void FOTGCourseWater::RiverAroundRocks(ATidewrightRiver* R)
{
	if (!R || FParse::Param(FCommandLine::Get(), TEXT("nowaterrocks"))) return;
	USplineComponent* Sp = R->GetSpline();
	UProceduralMeshComponent* PM = R->FindComponentByClass<UProceduralMeshComponent>();
	if (!Sp || !PM) return;
	const double Length = Sp->GetSplineLength();
	if (Length < 100) return;
	auto Section = [&](double D, FVector& Loc, FVector& Along, FVector& Right, double& Half, float& Slope) {
		Loc = Sp->GetLocationAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
		const FVector T = Sp->GetDirectionAtDistanceAlongSpline(D, ESplineCoordinateSpace::World);
		const FVector Flat = FVector(T.X, T.Y, 0).GetSafeNormal();
		Along = Flat.IsNearlyZero() ? FVector::ForwardVector : Flat;
		Right = FVector(-Along.Y, Along.X, 0);
		Half = 50.0 * R->River.Width * FMath::Max(Sp->GetScaleAtDistanceAlongSpline(D).Y, 0.01);
		const double Horizontal = FMath::Max(FVector2D(T.X, T.Y).Size(), 0.05);
		Slope = float(FMath::Max(-T.Z / Horizontal, 0.0));
	};

	// the rocks standing up through this river's water
	TArray<FObstacle> Obs;
	for (const FOTGCourseTrees::FDrawn& D : Course->Trees.DrawnRocks)
	{
		FVector S, N, V;
		if (!R->SampleRiver(D.Base, S, N, V)) continue;
		const double Top = D.Base.Z + D.H * 0.6;
		if (Top < S.Z + 5) continue;   // (under the water: the ripples go over it)
		const float Key = Sp->FindInputKeyClosestToWorldLocation(D.Base);
		const double Dist = Sp->GetDistanceAlongSplineAtSplineInputKey(Key);
		FVector Loc, Along, Right; double Half; float Slope;
		Section(Dist, Loc, Along, Right, Half, Slope);
		const double Across = FVector::DotProduct(D.Base - Loc, Right);
		// (out in the stream, not a bank stone at its edge, and the bed under it below the water)
		if (FMath::Abs(Across) > Half - D.R * 0.5 || D.Base.Z > S.Z - 10) continue;
		Obs.Add({ Dist + FVector::DotProduct(D.Base - Loc, Along), Across, FMath::Max(30.0, D.R * 0.8) });
		UE_LOG(LogOTGRiverRocks, Display, TEXT("Rock in the water at %s (level %s), %.0f cm across"), *D.Base.ToString(), *(D.Base - Course->Data->LevelOrigin).ToString(), D.R * 2);
	}
	if (!Obs.Num()) return;

	// the plugin's own surface, made finer (rows every half metre: its material, vertex colours and
	// normals as it makes them - a mesh of our own, however like it, drew bright bands across the
	// water), then bent in place round the rocks
	R->SegmentLength = 1.0f;
	R->RebuildRiver();
	if (FParse::Param(FCommandLine::Get(), TEXT("waterrocksseg"))) return;   // (testing: the plugin's own, finer)
	const FProcMeshSection* Sec = PM->GetProcMeshSection(0);
	if (!Sec || !Sec->ProcVertexBuffer.Num()) return;
	const bool bNoUV = FParse::Param(FCommandLine::Get(), TEXT("waterrocksnouv")), bNoLift = FParse::Param(FCommandLine::Get(), TEXT("waterrocksnolift"));   // (testing)
	TArray<FVector> Pos, Nrm; TArray<FVector2D> UV; TArray<FColor> Cols; TArray<FProcMeshTangent> Tans;
	const int32 NV = Sec->ProcVertexBuffer.Num();
	Pos.Reserve(NV); Nrm.Reserve(NV); UV.Reserve(NV); Cols.Reserve(NV); Tans.Reserve(NV);
	int32 Moved = 0;
	// (its slope - the vertex colour's G, which sets the current's speed - smoothed along it over 3 m:
	// read at every half metre, each little step in the river's spline was a row of faster water, a
	// bright band across it)
	constexpr int32 Across = 11;   // (TidewrightRiver.cpp WidthSegments + 1)
	const int32 NRows = NV / Across;
	TArray<float> G;
	G.SetNumZeroed(NRows);
	for (int32 r = 0; r < NRows; r++) G[r] = Sec->ProcVertexBuffer[r * Across].Color.G;
	TArray<uint8> GS;
	GS.SetNumZeroed(NRows);
	for (int32 r = 0; r < NRows; r++)
	{
		float Sum = 0; int32 N = 0;
		for (int32 k = FMath::Max(0, r - 6); k <= FMath::Min(NRows - 1, r + 6); k++) { Sum += G[k]; N++; }
		GS[r] = uint8(FMath::RoundToInt(Sum / FMath::Max(N, 1)));
	}
	// (and its height, each column the same way: the spline steps down where the creek does, and at
	// half-metre rows each step was a little steep face catching the sky)
	TArray<double> ZS;
	ZS.SetNumZeroed(NV);
	for (int32 r = 0; r < NRows; r++)
		for (int32 c = 0; c < Across; c++)
		{
			double Sum = 0; int32 N = 0;
			for (int32 k = FMath::Max(0, r - 6); k <= FMath::Min(NRows - 1, r + 6); k++) { Sum += Sec->ProcVertexBuffer[k * Across + c].Position.Z; N++; }
			ZS[r * Across + c] = Sum / FMath::Max(N, 1);
		}
	int32 Index = 0;
	for (const FProcMeshVertex& V : Sec->ProcVertexBuffer)
	{
		const int32 Row = FMath::Min(Index++ / Across, NRows - 1);
		// (river space: its UV, metres across and along)
		const double X = V.UV0.X * 100.0, D = V.UV0.Y * 100.0;
		double U = X, Vv = D, Lift = 0, GX = 0, GD = 0;
		for (const FObstacle& O : Obs)
		{
			const double Lx = D - O.Along, Ly = X - O.Across;   // (along the flow, across)
			if (FMath::Abs(Lx) > O.R * 8 || FMath::Abs(Ly) > O.R * 8) continue;
			const double Rho2 = FMath::Max(Lx * Lx + Ly * Ly, O.R * O.R);
			const double Rho = FMath::Sqrt(Rho2);
			const double K = O.R * O.R / Rho2;
			const double Fade = FMath::Exp(-FMath::Square(Rho / (O.R * 5.0)));
			// potential flow round a post: phi = x (1 + r2/rho2) along, psi = y (1 - r2/rho2) across
			if (!bNoUV) { Vv += Lx * K * Fade; U -= Ly * K * Fade; }
			// banked up in front, a little low in the eddy behind
			if (!bNoLift)
			{
				auto L = [&](double ax, double ay) {
					const double rr = FMath::Max(FMath::Sqrt(ax * ax + ay * ay), O.R);
					const double Ring = FMath::Exp(-FMath::Square((rr - O.R) / (O.R * 0.7)));
					return ax < 0 ? 6.0 * Ring * (-ax / rr) : -2.0 * Ring * (ax / rr);
				};
				Lift += L(Lx, Ly);
				GD += (L(Lx + 5, Ly) - L(Lx - 5, Ly)) / 10.0;
				GX += (L(Lx, Ly + 5) - L(Lx, Ly - 5)) / 10.0;
			}
		}
		// (its normal: leaning down the smoothed slope - the plugin's, from the spline at each row, leant
		// hard at every step - and tilted by the banking-up; along is the tangent's turn from across)
		const FVector Right = V.Tangent.TangentX.GetSafeNormal2D();
		const FVector Along(Right.Y, -Right.X, 0);
		const double Slope = (NRows > 0 && NV % Across == 0) ? GS[Row] / 255.0 : V.Color.G / 255.0;
		const FVector N = (FVector(0, 0, 1) + Along * Slope - Right * GX - Along * GD).GetSafeNormal();
		if (Lift != 0) Moved++;
		FVector P = V.Position;
		if (NV % Across == 0) P.Z = ZS[Index - 1];
		Pos.Add(P + FVector(0, 0, Lift));
		Nrm.Add(N);
		UV.Add(FVector2D(U * 0.01, Vv * 0.01));
		FColor C = V.Color;
		if (NRows > 0 && NV % Across == 0) C.G = GS[Row];
		Cols.Add(C);
		Tans.Add(V.Tangent);
	}
	PM->UpdateMeshSection(0, Pos, Nrm, UV, Cols, Tans);
	PM->SetVisibleInRayTracing(false);
	UE_LOG(LogOTGRiverRocks, Display, TEXT("River round %d rock(s): %d of %d points moved"), Obs.Num(), Moved, NV);
}
