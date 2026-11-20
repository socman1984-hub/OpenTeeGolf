// The course's ponds, lakes and rivers drawn by the Tidewright Water plugin (Plugins/Tidewright)
// in place of the game's own water mesh (AOTGRuntimeCourse::BuildWater, M_Water).
//
// - A pond or lake (water_bodies.json's lakes: outline and level) is a Tidewright lake: a rectangle
//   round its outline, at its level. The ground hides it everywhere but the pond, so a pond is only
//   given one where no ground in that rectangle - outside the pond - lies below the water; the
//   others are still ponds: a Tidewright river with no current, laid along the pond's own outline
//   (BuildStillPond). Glass Lake: ripples, no foam. Only the ponds nearest the
//   camera simulate their waves (each runs its own simulation on the game thread); the others keep
//   their last ones and cost nothing.
// - A river (water_bodies.json's rivers: a centre line, its level and width at each point) is a
//   Tidewright river: a spline down it, running downhill, a little wider than the channel (its banks
//   are where the ground rises through the flat surface). Where it runs through a pond it's cut:
//   the pond is the pond's water.
// - The sea (sea.glb, sea.json) is a Tidewright ocean at the sea's level, Breezy Bay, its swell the
//   export's, with a Tidewright shoreline over the coast for the surf. A shoreline scans the ground
//   with traces, and the ground built at run time has no collision: its scan is filled in from the
//   course's heights instead (BuildTidewrightSea). One shoreline serves every Tidewright water and
//   is rebuilt for each one's level, so on a course with a sea every pond is a still pond.
// - None of the game's own water is drawn (-oldwater: it, as before, for comparison).
//
// -oldwater: the game's own water everywhere, as before.

#include "Game/OTGRuntimeCourse.h"
#include "OTGGlb.h"

#include "OTGCourseData.h"
#include "Components/SplineComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TidewrightPreset.h"
#include "TidewrightRiver.h"
#include "TidewrightShoreline.h"
#include "ProceduralMeshComponent.h"
#include "Components/BoxComponent.h"
#include "UObject/UnrealType.h"
#include "TidewrightWater.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGWater, Log, All);

namespace
{
	constexpr double LiftCm = 3;            // (the water drawn this far over its level, as the game's own is)
	constexpr double LakeMarginCm = 300;    // (a lake's rectangle round its outline: under the banks)
	constexpr double RiverWiden = 1.15;     // (a river a little wider than its channel: the banks hide the rest)
	constexpr int32 LakesAlive = 2;         // (ponds simulating at once: the nearest)
	constexpr double LakeAliveCm = 60000;   // (...within 600 m)
	constexpr double ShoreDryCm = 50;       // (the sea's shallows under this: dry to its surf - BuildTidewrightSea)

	// (a river's footprint, its widest half-width round it: WaterTopAt's quick test)
	FBox2D RiverBox(const ATidewrightRiver* R)
	{
		FBox2D B(ForceInit);
		const USplineComponent* Sp = R->GetSpline();
		double W = 0;
		for (int32 i = 0; i < Sp->GetNumberOfSplinePoints(); i++)
		{
			B += FVector2D(Sp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World));
			W = FMath::Max(W, 50.0 * R->River.Width * Sp->GetScaleAtSplinePoint(i).Y);
		}
		return B.ExpandBy(W + 500.0);
	}

	struct FBody { bool bLake = false; TArray<FVector> Pts; TArray<double> W; };

	TArray<FBody> ReadBodies(const FString& Dir)
	{
		TArray<FBody> Out;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Dir, TEXT("water_bodies.json")))) return Out;
		TArray<TSharedPtr<FJsonValue>> List;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), List)) return Out;
		for (const TSharedPtr<FJsonValue>& V : List)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* Pts = nullptr;
			if (!O.IsValid() || !O->TryGetArrayField(TEXT("points"), Pts) || Pts->Num() < 2) continue;
			FBody B;
			B.bLake = O->GetStringField(TEXT("type")) == TEXT("lake");
			for (const TSharedPtr<FJsonValue>& P : *Pts)
			{
				const TSharedPtr<FJsonObject> Q = P->AsObject();
				B.Pts.Add(FVector(Q->GetNumberField(TEXT("x")), Q->GetNumberField(TEXT("y")), Q->GetNumberField(TEXT("z"))));
				double W = 0;
				Q->TryGetNumberField(TEXT("w"), W);
				B.W.Add(W);
			}
			Out.Add(MoveTemp(B));
		}
		return Out;
	}

	bool Inside(const TArray<FVector>& Poly, double X, double Y)
	{
		bool bIn = false;
		for (int32 a = 0, b = Poly.Num() - 1; a < Poly.Num(); b = a++)
			if ((Poly[a].Y > Y) != (Poly[b].Y > Y) && X < (Poly[b].X - Poly[a].X) * (Y - Poly[a].Y) / (Poly[b].Y - Poly[a].Y) + Poly[a].X) bIn = !bIn;
		return bIn;
	}

	double DistToLine(const TArray<FVector>& L, double X, double Y, bool bClosed, int32* OutSeg = nullptr)
	{
		double Best = TNumericLimits<double>::Max();
		const int32 N = L.Num();
		for (int32 i = 0; i + (bClosed ? 0 : 1) < N; i++)
		{
			const FVector2D A(L[i]), B(L[(i + 1) % N]), P(X, Y);
			const FVector2D AB = B - A;
			const double T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1e-6), 0.0, 1.0);
			const double D = FVector2D::Distance(P, A + AB * T);
			if (D < Best) { Best = D; if (OutSeg) *OutSeg = i; }
		}
		return Best;
	}
}

bool AOTGRuntimeCourse::BuildTidewright(TArray<bool>& OutGone)
{
	OutGone.Init(false, WaterPos.Num());
	if (FParse::Param(FCommandLine::Get(), TEXT("oldwater"))) return false;
	UTidewrightPreset* Glass = LoadObject<UTidewrightPreset>(nullptr, TEXT("/Tidewright/Presets/TWP_GlassLake.TWP_GlassLake"));
	if (!Glass) { UE_LOG(LogOTGWater, Warning, TEXT("No Tidewright presets (the plugin's content): the game's own water")); return false; }
	TArray<FBody> Bodies = ReadBodies(Data->ExportDir);
	// (a stream's centre line runs on through the ponds it feeds: cut there, so a pond stays a pond -
	// a river laid across one covered it in river water)
	{
		TArray<const FBody*> Ponds;
		for (const FBody& B : Bodies) if (B.bLake) Ponds.Add(&B);
		auto InPond = [&](const FVector& P) {
			for (const FBody* L : Ponds) if (Inside(L->Pts, P.X, P.Y) || DistToLine(L->Pts, P.X, P.Y, true) < 200) return true;
			return false;
		};
		TArray<FBody> Split;
		for (const FBody& B : Bodies)
		{
			if (B.bLake) { Split.Add(B); continue; }
			FBody Cur;
			for (int32 i = 0; i < B.Pts.Num(); i++)
			{
				if (InPond(B.Pts[i])) { if (Cur.Pts.Num() >= 2) Split.Add(Cur); Cur = FBody(); continue; }
				Cur.Pts.Add(B.Pts[i]);
				Cur.W.Add(B.W[i]);
			}
			if (Cur.Pts.Num() >= 2) Split.Add(Cur);
		}
		Bodies = MoveTemp(Split);
	}
	const FVector O = Data->LevelOrigin;
	const bool bSeaCourse = FPaths::FileExists(FPaths::Combine(Data->ExportDir, TEXT("sea.glb")));
	int32 NLakes = 0, NSkipped = 0, NRivers = 0;
	bool bShared = false;
	for (const FBody& B : Bodies)
	{
		if (B.bLake)
		{
			// a pond: its rectangle, and nothing in it outside the pond under the water
			FBox2D Box(ForceInit);
			for (const FVector& P : B.Pts) Box += FVector2D(P);
			Box = Box.ExpandBy(LakeMarginCm);
			// (its level: where the game's own water settled in it - up to the bank. water_bodies.json's
			// is the outline's lowest point, often well under that: the water stopped short of the bank,
			// a strip of bare bed showing all round)
			double Level = B.Pts[0].Z;
			{
				TArray<double> Ys;
				for (const FVector3f& V : WaterPos)
					if (Inside(B.Pts, V.X * 100.0, V.Z * 100.0)) Ys.Add(V.Y * 100.0);
				if (Ys.Num())
				{
					Ys.Sort();
					Level = FMath::Max(Level, Ys[Ys.Num() / 2]);
				}
			}
			bool bSafe = true;
			for (double Y = Box.Min.Y; Y <= Box.Max.Y && bSafe; Y += 100)
				for (double X = Box.Min.X; X <= Box.Max.X; X += 100)
					if (!Inside(B.Pts, X, Y) && DistToLine(B.Pts, X, Y, true) > LakeMarginCm && GroundAt(X, Y) < Level + LiftCm + 2) { bSafe = false; break; }
			if (bSeaCourse) bSafe = false;
			if (!bSafe) { if (BuildStillPond(B.Pts, Level)) NSkipped++; continue; }
			FActorSpawnParameters SP;
			SP.bDeferConstruction = true;
			const FVector2D C = Box.GetCenter();
			ATidewrightWater* W = GetWorld()->SpawnActor<ATidewrightWater>(ATidewrightWater::StaticClass(), FTransform(O + FVector(C.X, C.Y, Level + LiftCm)), SP);
			if (!W) continue;
			W->Mode = ETidewrightWaterMode::Lake;
			W->LakeSize = Box.GetSize() / 100.0;
			W->SimulationQuality = ETidewrightSimulationQuality::Low;
			W->ViewDistance = 2.0f;
			W->bShareWavesWithMaterials = !bShared;   // (one water in a level shares its waves)
			W->Underwater.bEnabled = false;   // (the camera never goes under: AOTGRound::UpdateCamera)
			W->Seed = NLakes + 1;
			bShared = true;
			W->FinishSpawning(FTransform(O + FVector(C.X, C.Y, Level + LiftCm)));
			W->ApplyPreset(Glass, 0.f);
			Lakes.Add(W);
			NLakes++;
			// (the game's own water under it: gone)
			for (int32 v = 0; v < WaterPos.Num(); v++)
			{
				const double X = WaterPos[v].X * 100.0, Y = WaterPos[v].Z * 100.0;
				if (Box.IsInside(FVector2D(X, Y)) && FMath::Abs(WaterPos[v].Y * 100.0 - Level) < 20) OutGone[v] = true;
			}
		}
		else
		{
			// a river: upstream first, never running uphill
			TArray<FVector> Pts = B.Pts;
			TArray<double> W = B.W;
			if (Pts[0].Z < Pts.Last().Z) { Algo::Reverse(Pts); Algo::Reverse(W); }
			for (int32 i = 1; i < Pts.Num(); i++) Pts[i].Z = FMath::Min(Pts[i].Z, Pts[i - 1].Z);
			FActorSpawnParameters SP;
			SP.bDeferConstruction = true;
			ATidewrightRiver* R = GetWorld()->SpawnActor<ATidewrightRiver>(ATidewrightRiver::StaticClass(), FTransform(O + Pts[0] + FVector(0, 0, LiftCm)), SP);
			if (!R) continue;
			R->River.Width = 1.0f;   // (each point's width in its scale)
			// (a golf course's creek or canal: clearer and calmer than the plugin's silty river, a
			// thin line of foam at the banks)
			R->River.FlowSpeed = 0.8f;
			R->River.BankFoamWidth = 0.0f;   // (no white line along the banks: a creek's edge is grass)
			R->River.FoamAmount = 0.2f;
			// (a river's colour: green-brown, not the sea's cyan - blue taken out as much as red - and
			// darkening quickly with depth, so its deep middle isn't as clear as its edges)
			R->River.ScatterColor = FLinearColor(0.006f, 0.008f, 0.005f, 1.0f);
			R->River.Absorption = FLinearColor(1.1f, 0.65f, 1.2f, 1.0f);
			R->FinishSpawning(FTransform(O + Pts[0] + FVector(0, 0, LiftCm)));
			USplineComponent* S = R->GetSpline();
			if (!S) { R->Destroy(); continue; }
			S->ClearSplinePoints(false);
			for (int32 i = 0; i < Pts.Num(); i++)
			{
				S->AddSplinePoint(O + Pts[i] + FVector(0, 0, LiftCm), ESplineCoordinateSpace::World, false);
				S->SetScaleAtSplinePoint(i, FVector(1, FMath::Max(1.0, W[i] / 100.0 * RiverWiden), 1), false);
			}
			S->UpdateSpline();
			R->RebuildRiver();
			Rivers.Add(R);
			RiverBoxes.Add(RiverBox(R));
			NRivers++;
			for (int32 v = 0; v < WaterPos.Num(); v++)
			{
				const double X = WaterPos[v].X * 100.0, Y = WaterPos[v].Z * 100.0;
				int32 Seg = 0;
				const double D = DistToLine(Pts, X, Y, false, &Seg);
				const double Half = FMath::Max(W[Seg], W[FMath::Min(Seg + 1, W.Num() - 1)]) * 0.5;
				if (D < Half + 300 && FMath::Abs(WaterPos[v].Y * 100.0 - Pts[Seg].Z) < 80) OutGone[v] = true;
			}
		}
	}
	UE_LOG(LogOTGWater, Display, TEXT("Tidewright: %d lakes, %d still ponds (ground under their level in a rectangle round them: drawn along their own outline), %d rivers"), NLakes, NSkipped, NRivers);
	return true;
}

// A pond a rectangle can't hold (lower ground beside it would be under the water too): a Tidewright
// river with no current laid down its long axis, as wide at each step as the pond there (and a
// little more, under the banks), flat at the pond's level - the pond's own shape, ripples, no foam
bool AOTGRuntimeCourse::BuildStillPond(const TArray<FVector>& Poly, double Level)
{
	// (its long axis: the outline's principal direction)
	FVector2D C(0, 0);
	for (const FVector& P : Poly) C += FVector2D(P);
	C /= Poly.Num();
	double Sxx = 0, Syy = 0, Sxy = 0;
	for (const FVector& P : Poly) { const FVector2D D = FVector2D(P) - C; Sxx += D.X * D.X; Syy += D.Y * D.Y; Sxy += D.X * D.Y; }
	const double Ang = 0.5 * FMath::Atan2(2 * Sxy, Sxx - Syy);
	const FVector2D Ax(FMath::Cos(Ang), FMath::Sin(Ang)), Ac(-Ax.Y, Ax.X);
	double Lo = TNumericLimits<double>::Max(), Hi = -Lo;
	for (const FVector& P : Poly) { const double T = FVector2D::DotProduct(FVector2D(P) - C, Ax); Lo = FMath::Min(Lo, T); Hi = FMath::Max(Hi, T); }
	// stations every 2 m along it: where the line across meets the outline, its middle and span
	TArray<FVector> Pts;
	TArray<double> W;
	const double Step = 200;
	for (double T = Lo + 20; T <= Hi - 20 + 1; T += Step)
	{
		const FVector2D O = C + Ax * T;
		double A = TNumericLimits<double>::Max(), B = -A;
		for (int32 i = 0, j = Poly.Num() - 1; i < Poly.Num(); j = i++)
		{
			const FVector2D P(Poly[i]), Q(Poly[j]);
			const double Ti = FVector2D::DotProduct(P - C, Ax) - T, Tj = FVector2D::DotProduct(Q - C, Ax) - T;
			if ((Ti > 0) == (Tj > 0)) continue;
			const FVector2D X = P + (Q - P) * (Ti / (Ti - Tj));
			const double U = FVector2D::DotProduct(X - O, Ac);
			A = FMath::Min(A, U); B = FMath::Max(B, U);
		}
		if (A > B) continue;
		const FVector2D Mid = O + Ac * (0.5 * (A + B));
		Pts.Add(FVector(Mid.X, Mid.Y, Level));
		W.Add((B - A) + 2 * 150);
	}
	if (Pts.Num() < 2) return false;
	const FVector O3 = Data->LevelOrigin;
	FActorSpawnParameters SP;
	SP.bDeferConstruction = true;
	ATidewrightRiver* R = GetWorld()->SpawnActor<ATidewrightRiver>(ATidewrightRiver::StaticClass(), FTransform(O3 + Pts[0] + FVector(0, 0, LiftCm)), SP);
	if (!R) return false;
	R->River.Width = 1.0f;
	R->River.FlowSpeed = 0.0f;           // (still)
	R->River.BankFoamWidth = 0.0f;
	R->River.FoamAmount = 0.0f;
	R->River.RippleStrength = 0.6f;
	R->River.ScatterColor = FLinearColor(0.006f, 0.013f, 0.016f, 1.0f);
	R->River.Absorption = FLinearColor(0.45f, 0.16f, 0.13f, 1.0f);
	R->SegmentLength = 1.0f;
	R->FinishSpawning(FTransform(O3 + Pts[0] + FVector(0, 0, LiftCm)));
	USplineComponent* S = R->GetSpline();
	if (!S) { R->Destroy(); return false; }
	S->ClearSplinePoints(false);
	for (int32 i = 0; i < Pts.Num(); i++)
	{
		S->AddSplinePoint(O3 + Pts[i] + FVector(0, 0, LiftCm), ESplineCoordinateSpace::World, false);
		S->SetScaleAtSplinePoint(i, FVector(1, FMath::Max(1.0, W[i] / 100.0), 1), false);
	}
	S->UpdateSpline();
	R->RebuildRiver();
	Rivers.Add(R);
	RiverBoxes.Add(RiverBox(R));
	return true;
}

// The water's meshes kept out of ray tracing, and the ponds simulating: the nearest few to the
// camera; the rest keep their last waves
void AOTGRuntimeCourse::UpdateTidewright()
{
	// (Tidewright's surfaces are procedural meshes, made when each first builds or ticks: in the
	// hardware ray tracing scene they cost the render thread ~100 ms a frame on a course with a dozen
	// rivers - Majestic, 19 fps against 60. Out of it they're still reflected by Lumen's screen traces)
	auto NoRayTracing = [](AActor* A)
	{
		if (!A) return;
		TInlineComponentArray<UProceduralMeshComponent*> Meshes(A);
		for (UProceduralMeshComponent* M : Meshes)
			if (M->bVisibleInRayTracing) M->SetVisibleInRayTracing(false);
	};
	for (ATidewrightWater* W : Lakes) NoRayTracing(W);
	for (ATidewrightRiver* R : Rivers) NoRayTracing(R);
	NoRayTracing(Sea);
	if (!Lakes.Num()) return;
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager) return;
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	TArray<TPair<double, ATidewrightWater*>> Near;
	for (ATidewrightWater* W : Lakes)
		if (W) Near.Add({ FVector::Dist2D(Cam, W->GetActorLocation()) - (W->LakeSize.GetMax() * 50.0), W });
	Near.Sort([](const auto& A, const auto& B) { return A.Key < B.Key; });
	for (int32 i = 0; i < Near.Num(); i++)
		// (one that hasn't simulated yet runs until it has: paused before then it has no waves at all)
		Near[i].Value->SetActorTickEnabled((i < LakesAlive && Near[i].Key < LakeAliveCm) || Near[i].Value->GetWaveTime() < 1.f);
}

// The sea: a Tidewright ocean, and a shoreline over the coast whose scan is the course's heights
// (Box: the sea's surface, the export's frame in metres - x east, z south; Swell: x east, y south)
bool AOTGRuntimeCourse::BuildTidewrightSea(const otg::FGlbMesh& Mesh, const FBox& Box, const FVector2D& Swell, double LevelM)
{
	if (FParse::Param(FCommandLine::Get(), TEXT("oldwater"))) return false;
	UTidewrightPreset* Bay = LoadObject<UTidewrightPreset>(nullptr, TEXT("/Tidewright/Presets/TWP_BreezyBay.TWP_BreezyBay"));
	if (!Bay) { UE_LOG(LogOTGWater, Warning, TEXT("No Tidewright presets (the plugin's content): the game's own sea")); return false; }
	const FVector O = Data->LevelOrigin;
	const double Level = LevelM * 100.0;
	const FVector Centre = O + FVector((Box.Min.X + Box.Max.X) * 50.0, (Box.Min.Z + Box.Max.Z) * 50.0, Level + LiftCm);

	// the shoreline first (the ocean finds it when it ticks): the sea's box and 150 m round it
	const FVector2D Half((Box.Max.X - Box.Min.X) * 50.0 + 15000.0, (Box.Max.Z - Box.Min.Z) * 50.0 + 15000.0);
	FActorSpawnParameters SP;
	SP.bDeferConstruction = true;
	ATidewrightShoreline* Shore = GetWorld()->SpawnActor<ATidewrightShoreline>(ATidewrightShoreline::StaticClass(), FTransform(Centre), SP);
	if (Shore)
	{
		Shore->bRebuildOnBeginPlay = false;
		// (the surf's foam: a narrower, broken band - at full strength the whole coast was a white stripe)
		Shore->Shore.ShoreFoamAmount = 0.5f;
		Shore->Shore.ShoreFoamDepth = 0.18f;
		// (no run-up, a lower surf: the water rising over the shore drew it over the grass)
		Shore->Shore.RunUpHeight = 0.f;
		Shore->Shore.SurfHeight = 0.35f;
		Shore->GetArea()->SetBoxExtent(FVector(Half.X, Half.Y, 5000.0));
		const int32 Size = 512;
		const FVector2D C(Centre.X - O.X, Centre.Y - O.Y);
		const double CellX = 2.0 * Half.X / Size, CellY = 2.0 * Half.Y / Size;
		// (the sea is where the export's sea is: its mesh, laid on the grid. Low ground anywhere else -
		// a bunker or a links hollow near the sea's level - read as shallows took the surf, its run-up
		// and foam, which showed through the ground there: it's dry land a metre up)
		TArray<uint8> InSea;
		InSea.Init(0, Size * Size);
		for (int32 t = 0; t + 2 < Mesh.Indices.Num(); t += 3)
		{
			FVector2D P[3];
			for (int32 k = 0; k < 3; k++)
			{
				const FVector3f& V = Mesh.Positions[Mesh.Indices[t + k]];
				P[k] = FVector2D((V.X * 100.0 - (C.X - Half.X)) / CellX - 0.5, (V.Z * 100.0 - (C.Y - Half.Y)) / CellY - 0.5);
			}
			const double D = (P[1].X - P[0].X) * (P[2].Y - P[0].Y) - (P[2].X - P[0].X) * (P[1].Y - P[0].Y);
			if (FMath::Abs(D) < 1e-9) continue;
			const int32 X0 = FMath::Max(0, FMath::FloorToInt(FMath::Min3(P[0].X, P[1].X, P[2].X))), X1 = FMath::Min(Size - 1, FMath::CeilToInt(FMath::Max3(P[0].X, P[1].X, P[2].X)));
			const int32 Y0 = FMath::Max(0, FMath::FloorToInt(FMath::Min3(P[0].Y, P[1].Y, P[2].Y))), Y1 = FMath::Min(Size - 1, FMath::CeilToInt(FMath::Max3(P[0].Y, P[1].Y, P[2].Y)));
			for (int32 y = Y0; y <= Y1; y++)
				for (int32 x = X0; x <= X1; x++)
				{
					const double A = ((P[1].X - x) * (P[2].Y - y) - (P[2].X - x) * (P[1].Y - y)) / D;
					const double B = ((P[2].X - x) * (P[0].Y - y) - (P[0].X - x) * (P[2].Y - y)) / D;
					if (A >= -0.02 && B >= -0.02 && A + B <= 1.02) InSea[y * Size + x] = 1;
				}
		}
		TArray<float> Hts;
		Hts.SetNumUninitialized(Size * Size);
		for (int32 y = 0; y < Size; y++)
			for (int32 x = 0; x < Size; x++)
			{
				const double G = GroundAt(C.X - Half.X + (x + 0.5) * CellX, C.Y - Half.Y + (y + 0.5) * CellY);
				// (and, in the sea, the shallows under half a metre: the surf and its foam start where the
				// water's that deep. Over a wide flat the sea barely covers, they spread across it, the grass
				// showing through a net of white)
				const bool bWet = InSea[y * Size + x] && G < Level - ShoreDryCm;
				Hts[y * Size + x] = float(O.Z + (bWet ? G : FMath::Max(G, Level + 100.0)));
			}
		// (its scan's fields aren't public: set as a saved scan would be)
		UClass* K = ATidewrightShoreline::StaticClass();
		auto Field = [&](const TCHAR* Name) { FProperty* P = K->FindPropertyByName(Name); return P ? P->ContainerPtrToValuePtr<void>(Shore) : nullptr; };
		void* PH = Field(TEXT("TerrainHeights")); void* PS = Field(TEXT("ScanSize")); void* PC = Field(TEXT("ScanCentre")); void* PE = Field(TEXT("ScanHalfSize"));
		if (PH && PS && PC && PE)
		{
			*static_cast<TArray<float>*>(PH) = MoveTemp(Hts);
			*static_cast<int32*>(PS) = Size;
			*static_cast<FVector2D*>(PC) = FVector2D(Centre);
			*static_cast<FVector2D*>(PE) = Half;
		}
		else UE_LOG(LogOTGWater, Warning, TEXT("Sea: the shoreline's scan fields have changed - no surf"));
		Shore->FinishSpawning(FTransform(Centre));
	}

	ATidewrightWater* W = GetWorld()->SpawnActor<ATidewrightWater>(ATidewrightWater::StaticClass(), FTransform(Centre), SP);
	if (!W) return false;
	W->Mode = ETidewrightWaterMode::Ocean;
	W->SimulationQuality = ETidewrightSimulationQuality::Medium;
	W->ViewDistance = 40.0f;
	W->bShareWavesWithMaterials = Lakes.Num() == 0;
	W->Underwater.bEnabled = false;   // (the camera never goes under: AOTGRound::UpdateCamera)
	W->FinishSpawning(FTransform(Centre));
	W->ApplyPreset(Bay, 0.f);
	if (!Swell.IsNearlyZero())
	{
		FTidewrightWaveSettings Waves = W->Waves;
		Waves.SwellDirection = float(FMath::RadiansToDegrees(FMath::Atan2(Swell.Y, Swell.X)));
		W->SetWaveSettings(Waves, 0.f);
	}
	Sea = W;
	UE_LOG(LogOTGWater, Display, TEXT("Tidewright sea at %.2f m, shoreline %.0f x %.0f m"), LevelM, Half.X / 50.0, Half.Y / 50.0);
	return true;
}

// The highest water surface over a point (world cm), or -infinity: the sea, a pond's level, a
// river's surface. The camera stays over it (AOTGRound): nothing in the game is seen from under water
double AOTGRuntimeCourse::WaterTopAt(const FVector& P) const
{
	double Top = -UE_BIG_NUMBER;
	if (Sea) Top = Sea->GetActorLocation().Z;
	for (const ATidewrightWater* W : Lakes)
		if (W && FMath::Abs(P.X - W->GetActorLocation().X) <= W->LakeSize.X * 50.0 && FMath::Abs(P.Y - W->GetActorLocation().Y) <= W->LakeSize.Y * 50.0) Top = FMath::Max(Top, W->GetActorLocation().Z);
	// (a river only where it's near: its nearest point is a search along its whole spline)
	FVector S, N, V;
	for (int32 i = 0; i < Rivers.Num(); i++)
		if (Rivers[i] && (!RiverBoxes.IsValidIndex(i) || RiverBoxes[i].IsInside(FVector2D(P))) && Rivers[i]->SampleRiver(P, S, N, V))
			Top = FMath::Max(Top, S.Z);
	return Top;
}
