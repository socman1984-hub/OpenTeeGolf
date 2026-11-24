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

#include "World/OTGRuntimeCourse.h"
#include "Glb/OTGGlb.h"

#include "Course/OTGCourseData.h"
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
#include "Course/OTGTextures.h"
#include "World/OTGGrassFwd.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGWater, Log, All);

namespace
{
	constexpr double LiftCm = 3;            // (the water drawn this far over its level, as the game's own is)
	constexpr double LakeMarginCm = 300;    // (a lake's rectangle round its outline: under the banks)
	constexpr double RiverWiden = 1.15;     // (a river a little wider than its channel: the banks hide the rest)
	constexpr int32 LakesAlive = 2;         // (ponds simulating at once: the nearest)
	constexpr double LakeAliveCm = 60000;   // (...within 600 m)
	const FLinearColor PondScatter(0.0045f, 0.006f, 0.003f, 1.0f);    // (a pond's colour: BuildStillPond)
	const FLinearColor PondAbsorption(1.6f, 1.05f, 2.0f, 1.0f);

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

// The creeks, before the ground is drawn: each piece of a river (cut where it runs through a pond) laid
// out so its water always ends against ground or other water. The river's water fades out over its
// first and last 3 m; a short creek between two ponds (Majestic hole 1's is 10 m) showed its dug bed
// at both ends, dry dirt, and the ponds' sheets ended in mid-air over its channel. At a pond:
// - the pond's level within 60 cm of the creek's: the creek eases to the pond's level over its last
//   20 m and runs on 4 m into the pond, its fading end under the pond's water
// - the pond lower: the creek falls to the pond's level over its last 10 m, then on into it the same
// - the pond higher (Majestic's sits 3.4 m over its creek): the creek keeps its own level and runs into
//   the berm holding the pond (LiftPondBanks), its fading end under the grass; that ground isn't dug
// And the ground under each creek is dug to a bed below its water, its banks eased to at most about
// 30 degrees over 5 m either side (the export's channel walls stood up to 2 m sheer). Only the
// ground's drawing: the ball plays on the course's own heights. -oldcreeks: as the export has them
void FOTGCourseWater::PlanCreeks()
{
	bCreeksPlanned = true;
	Creeks.Reset();
	TArray<FBody> Bodies = ReadBodies(Course->Data->ExportDir);
	// the ponds and their levels (as BuildTidewright lays them: up to the bank's top)
	TArray<const FBody*> Ponds;
	TArray<double> PondZ;
	for (const FBody& B : Bodies)
	{
		if (!B.bLake) continue;
		double Level = B.Pts[0].Z;
		const int32 k = Ponds.Num();
		if (PondLevels.IsValidIndex(k)) Level = FMath::Max(Level, PondLevels[k]);
		else
		{
			TArray<double> Ys;
			for (const FVector3f& V : WaterPos) if (Inside(B.Pts, V.X * 100.0, V.Z * 100.0)) Ys.Add(V.Y * 100.0);
			if (Ys.Num()) { Ys.Sort(); Level = FMath::Max(Level, Ys[Ys.Num() / 2]); }
		}
		Ponds.Add(&B);
		PondZ.Add(Level);
	}
	auto PondAt = [&](const FVector& P, double Reach) -> int32 {
		for (int32 k = 0; k < Ponds.Num(); k++)
			if (Inside(Ponds[k]->Pts, P.X, P.Y) || (Reach > 0 && DistToLine(Ponds[k]->Pts, P.X, P.Y, true) < Reach)) return k;
		return INDEX_NONE;
	};
	// (a pond over a creek, and how far out from it its berm reaches: LiftPondBanks, about 30 degrees down
	// from just over its water - not dug)
	TMap<int32, double> Berms;
	PondHolds.Reset();
	const bool bOld = FParse::Param(FCommandLine::Get(), TEXT("oldcreeks"));
	int32 Joined = 0, Spills = 0;
	for (const FBody& B : Bodies)
	{
		if (B.bLake) continue;
		// (cut where it runs through a pond, so a pond stays a pond - a river laid across one covered it in
		// river water; each piece remembers the ponds at its ends)
		struct FPiece { FOTGCourseWater::FCreek C; int32 PondA = INDEX_NONE, PondB = INDEX_NONE; };
		TArray<FPiece> Pieces;
		FPiece Cur;
		int32 Last = INDEX_NONE;   // (the pond the line came out of)
		for (int32 i = 0; i < B.Pts.Num(); i++)
		{
			const int32 In = PondAt(B.Pts[i], 200);
			if (In != INDEX_NONE)
			{
				Cur.PondB = In;
				if (Cur.C.Pts.Num() >= 2) Pieces.Add(Cur);
				Cur = FPiece();
				Last = In;
				continue;
			}
			if (!Cur.C.Pts.Num()) Cur.PondA = Last;
			Cur.C.Pts.Add(B.Pts[i]);
			Cur.C.W.Add(B.W[i]);
		}
		if (Cur.C.Pts.Num() >= 2) Pieces.Add(Cur);
		for (FPiece& Pc : Pieces)
		{
			TArray<FVector>& P = Pc.C.Pts;
			TArray<double>& W = Pc.C.W;
			// (an end near a pond without the line running into it: that pond too)
			if (Pc.PondA == INDEX_NONE) Pc.PondA = PondAt(P[0], 400);
			if (Pc.PondB == INDEX_NONE) Pc.PondB = PondAt(P.Last(), 400);
			if (!bOld)
				for (int32 End = 0; End < 2; End++)
				{
					const int32 Pond = End == 0 ? Pc.PondA : Pc.PondB;
					if (Pond == INDEX_NONE) continue;
					// (worked from this end: the points in order from it)
					if (End == 1) { Algo::Reverse(P); Algo::Reverse(W); }
					TArray<double> S;
					S.Add(0);
					for (int32 i = 1; i < P.Num(); i++) S.Add(S.Last() + FVector2D::Distance(FVector2D(P[i]), FVector2D(P[i - 1])));
					const double Len = FMath::Max(S.Last(), 1.0);
					const double Zp = PondZ[Pond], Ze = P[0].Z;
					const FVector2D Dir = (FVector2D(P[0]) - FVector2D(P[1])).GetSafeNormal();
					// (how far on to the pond's outline)
					double ToPond = 0;
					while (ToPond < 3000 && !Inside(Ponds[Pond]->Pts, P[0].X + Dir.X * ToPond, P[0].Y + Dir.Y * ToPond)) ToPond += 50;
					if (ToPond >= 3000) ToPond = DistToLine(Ponds[Pond]->Pts, P[0].X, P[0].Y, true);
					if (Zp > Ze + 60)
					{
						// the pond over the creek: held by its berm (HoldPonds), which buries the creek's fading end
						// (run on into it, it reached the pond's deep bed and showed through the pond's water)
						Berms.FindOrAdd(Pond) = FMath::Max(Berms.FindRef(Pond), (Zp + 10.0 - (Ze - 60.0)) / 0.55 + 320.0);
						PondHolds.Add({ Pond, FVector2D(P[0]) });
						Spills++;
					}
					else
					{
						// the creek to the pond's level - over 20 m to one about level with it, 10 m down to a
						// lower one - and on into it under its water
						const double Over = FMath::Min(Zp < Ze - 60 ? 1000.0 : 2000.0, Len * (Pc.PondA != INDEX_NONE && Pc.PondB != INDEX_NONE ? 0.5 : 1.0));
						for (int32 i = 0; i < P.Num(); i++) P[i].Z = FMath::Lerp(Zp, P[i].Z, FMath::SmoothStep(0.0, Over, S[i]));
						P.Insert(FVector(FVector2D(P[0]) + Dir * (ToPond + 400.0), Zp), 0);
						const double W0 = W[0];
						W.Insert(W0, 0);
						Joined++;
					}
					if (End == 1) { Algo::Reverse(P); Algo::Reverse(W); }
				}
			// (upstream first, never running uphill)
			if (P[0].Z < P.Last().Z) { Algo::Reverse(P); Algo::Reverse(W); }
			for (int32 i = 1; i < P.Num(); i++) P[i].Z = FMath::Min(P[i].Z, P[i - 1].Z);
			Creeks.Add(MoveTemp(Pc.C));
		}
	}
	if (bOld) { UE_LOG(LogOTGWater, Display, TEXT("Creeks: %d, as the export has them (-oldcreeks)"), Creeks.Num()); return; }
	const int32 N = Course->Data->HeightSamples;
	if (Course->Ground.GroundHeights.Num() != N * N) return;
	const double Step = Course->Data->MetresPerSample * 100.0;
	const double ZScale = Course->Data->HeightRange * 100.0 / 65535.0;
	auto Box = [&](FBox2D B, int32& X0, int32& Y0, int32& X1, int32& Y1) {
		X0 = FMath::Max(0, FMath::FloorToInt(B.Min.X / Step)); Y0 = FMath::Max(0, FMath::FloorToInt(B.Min.Y / Step));
		X1 = FMath::Min(N - 1, FMath::CeilToInt(B.Max.X / Step)); Y1 = FMath::Min(N - 1, FMath::CeilToInt(B.Max.Y / Step));
	};
	// the ponds over creeks banked (HoldPonds), and that ground left alone
	Course->Ground.HoldPonds();
	auto InBerm = [&](const FVector2D& Q) {
		for (const FOTGCourseWater::FPondHold& H : PondHolds)
			if (FVector2D::Distance(Q, H.At) < 1500.0 && DistToLine(Ponds[H.Pond]->Pts, Q.X, Q.Y, true) < Berms.FindRef(H.Pond)) return true;
		return false;
	};
	// the ground dug to follow them
	constexpr double BankCm = 500.0, BankSlope = 0.55;   // (the banks eased over 5 m, to about 30 degrees)
	int32 Dug = 0;
	int32 Deep = 0;
	for (const FOTGCourseWater::FCreek& C : Creeks)
	{
		const int32 Before = Dug;
		FVector2D Sum(0, 0); double MaxDown = 0;
		FBox2D B(ForceInit);
		double MaxHalf = 0;
		for (int32 i = 0; i < C.Pts.Num(); i++) { B += FVector2D(C.Pts[i]); MaxHalf = FMath::Max(MaxHalf, C.W[i] * 0.5); }
		int32 X0, Y0, X1, Y1;
		Box(B.ExpandBy(MaxHalf + BankCm + Step), X0, Y0, X1, Y1);
		for (int32 y = Y0; y <= Y1; y++)
			for (int32 x = X0; x <= X1; x++)
			{
				const FVector2D Q(x * Step, y * Step);
				// (the nearest point of the line: its water level and half-width there)
				double Best = TNumericLimits<double>::Max(), Zw = 0, Half = 0;
				FVector2D On;
				for (int32 i = 0; i + 1 < C.Pts.Num(); i++)
				{
					const FVector2D A(C.Pts[i]), Bp(C.Pts[i + 1]), AB = Bp - A;
					const double T = FMath::Clamp(FVector2D::DotProduct(Q - A, AB) / FMath::Max(AB.SizeSquared(), 1e-6), 0.0, 1.0);
					const double D = FVector2D::Distance(Q, A + AB * T);
					if (D < Best) { Best = D; On = A + AB * T; Zw = FMath::Lerp(C.Pts[i].Z, C.Pts[i + 1].Z, T); Half = FMath::Lerp(C.W[i], C.W[i + 1], T) * 0.5; }
				}
				if (Best > Half + BankCm) continue;
				// (where the course's own ground on the creek's line stands well over its water - its level in
				// water_bodies.json metres low there (Walden: 5 m) - left as it is: dug to it, a deep dry
				// trench with dark walls)
				if (Course->Data->Heights[FMath::Clamp(FMath::RoundToInt(On.Y / Step), 0, N - 1) * N + FMath::Clamp(FMath::RoundToInt(On.X / Step), 0, N - 1)] * ZScale - Zw > 350.0) { Deep++; continue; }
				if (PondAt(FVector(Q, 0), 0) != INDEX_NONE || InBerm(Q)) continue;   // (a pond's bed is its own: LiftPondBanks)
				uint16& Hv = Course->Ground.GroundHeights[y * N + x];
				const double Z = Hv * ZScale;
				double Want;
				if (Best < Half) Want = Zw - 25.0 - 35.0 * FMath::Clamp(1.0 - Best / FMath::Max(Half, 1.0), 0.0, 1.0);   // (the bed: 25 cm under at the edge, 60 in the middle)
				else
				{
					// (the bank: from just over the water at the edge, rising no steeper than BankSlope; from
					// 3.5 m out, back to the ground as it was)
					const double Out = Best - Half;
					const double Cap = Zw + 4.0 + Out * BankSlope;
					Want = FMath::Lerp(FMath::Min(Z, Cap), Z, FMath::SmoothStep(BankCm * 0.7, BankCm, Out));
				}
				if (Z > Want + 0.5) { Hv = uint16(FMath::Clamp(FMath::FloorToDouble(Want / ZScale), 0.0, 65535.0)); Dug++; Sum += Q; MaxDown = FMath::Max(MaxDown, Z - Want); }
			}
		if (Dug - Before > 50) UE_LOG(LogOTGWater, Display, TEXT("  creek: dug at %d samples round %.0f %.0f, down to %.0f cm"), Dug - Before, Sum.X / (Dug - Before), Sum.Y / (Dug - Before), MaxDown);
	}
	UE_LOG(LogOTGWater, Display, TEXT("Creeks: %d pieces; %d ends joined to their ponds' levels, %d into a pond's berm over them; the ground dug at %d samples (%d left alone: the water metres under the ground there)"),
		Creeks.Num(), Joined, Spills, Dug, Deep);
}

bool FOTGCourseWater::BuildTidewright(TArray<bool>& OutGone)
{
	OutGone.Init(false, WaterPos.Num());
	if (FParse::Param(FCommandLine::Get(), TEXT("oldwater"))) return false;
	UTidewrightPreset* Glass = LoadObject<UTidewrightPreset>(nullptr, TEXT("/Tidewright/Presets/TWP_GlassLake.TWP_GlassLake"));
	if (!Glass) { UE_LOG(LogOTGWater, Warning, TEXT("No Tidewright presets (the plugin's content): the game's own water")); return false; }
	// (the ponds as they are; the rivers as PlanCreeks laid them out - cut where they run through a
	// pond, each running from one pond's level to the next)
	if (!bCreeksPlanned) PlanCreeks();
	TArray<FBody> Bodies;
	for (FBody& B : ReadBodies(Course->Data->ExportDir)) if (B.bLake) Bodies.Add(MoveTemp(B));
	for (const FOTGCourseWater::FCreek& C : Creeks) { FBody B; B.Pts = C.Pts; B.W = C.W; Bodies.Add(MoveTemp(B)); }
	const FVector O = Course->Data->LevelOrigin;
	const bool bSeaCourse = FPaths::FileExists(FPaths::Combine(Course->Data->ExportDir, TEXT("sea.glb")));
	int32 NLakes = 0, NSkipped = 0, NRivers = 0, NLakeSeen = 0;
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
			// (up to the bank's top: LiftPondBanks, which shaped the bank round it at that level)
			const bool bShaped = PondLevels.IsValidIndex(NLakeSeen);
			if (bShaped) Level = FMath::Max(Level, PondLevels[NLakeSeen]);
			const TArray<FVector2D>* Round = PondOutlines.IsValidIndex(NLakeSeen) ? &PondOutlines[NLakeSeen] : nullptr;
			NLakeSeen++;
			if (!bShaped)
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
					if (!Inside(B.Pts, X, Y) && DistToLine(B.Pts, X, Y, true) > LakeMarginCm && Course->Ground.GroundAt(X, Y) < Level + LiftCm + 2) { bSafe = false; break; }
			if (bSeaCourse) bSafe = false;
			if (!bSafe) { if (BuildStillPond(B.Pts, Level, Round)) NSkipped++; continue; }
			FActorSpawnParameters SP;
			SP.bDeferConstruction = true;
			const FVector2D C = Box.GetCenter();
			ATidewrightWater* W = Course->GetWorld()->SpawnActor<ATidewrightWater>(ATidewrightWater::StaticClass(), FTransform(O + FVector(C.X, C.Y, Level + LiftCm)), SP);
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
			W->Look.ScatterColor = PondScatter;   // (as a still pond's: BuildStillPond)
			W->Look.Absorption = PondAbsorption;
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
			ATidewrightRiver* R = Course->GetWorld()->SpawnActor<ATidewrightRiver>(ATidewrightRiver::StaticClass(), FTransform(O + Pts[0] + FVector(0, 0, LiftCm)), SP);
			if (!R) continue;
			R->River.Width = 1.0f;   // (each point's width in its scale)
			// (a golf course's creek or canal: clearer and calmer than the plugin's silty river, a
			// thin line of foam at the banks)
			R->River.FlowSpeed = 0.8f;
			R->River.BankFoamWidth = 0.0f;   // (no white line along the banks: a creek's edge is grass)
			R->River.FoamAmount = 0.2f;
			// (a river's colour: green-brown, not the sea's cyan - blue taken out as much as red - and
			// darkening quickly with depth, so its deep middle isn't as clear as its edges)
			R->River.ScatterColor = FLinearColor(0.005f, 0.007f, 0.004f, 1.0f);
			R->River.Absorption = FLinearColor(1.4f, 0.9f, 1.7f, 1.0f);
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
			RiverAroundRocks(R);
			Rivers.Add(R);
			RiverBoxes.Add(RiverBox(R));
			NRivers++;
			for (int32 v = 0; v < WaterPos.Num(); v++)
			{
				const double X = WaterPos[v].X * 100.0, Y = WaterPos[v].Z * 100.0;
				int32 Seg = 0;
				const double D = DistToLine(Pts, X, Y, false, &Seg);
				const double Half = FMath::Max(W[Seg], W[FMath::Min(Seg + 1, W.Num() - 1)]) * 0.5;
				// (the game's own water along it, whatever its level: the creek's runs from pond to pond, the
				// export's in steps - left, its pieces showed as stairs beside the creek and the ponds)
				if (D < Half + 300 && FMath::Abs(WaterPos[v].Y * 100.0 - Pts[Seg].Z) < 400) OutGone[v] = true;
			}
		}
	}
	UE_LOG(LogOTGWater, Display, TEXT("Tidewright: %d lakes, %d still ponds (ground under their level in a rectangle round them: drawn along their own outline), %d rivers"), NLakes, NSkipped, NRivers);
	return true;
}

// A pond a rectangle can't hold (lower ground beside it would be under the water too): a Tidewright
// river with no current laid down its long axis, as wide at each step as the pond there (and a
// little more, under the banks), flat at the pond's level - the pond's own shape, ripples, no foam
bool FOTGCourseWater::BuildStillPond(const TArray<FVector>& Poly, double Level, const TArray<FVector2D>* Round)
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
	const FVector O3 = Course->Data->LevelOrigin;
	FActorSpawnParameters SP;
	SP.bDeferConstruction = true;
	ATidewrightRiver* R = Course->GetWorld()->SpawnActor<ATidewrightRiver>(ATidewrightRiver::StaticClass(), FTransform(O3 + Pts[0] + FVector(0, 0, LiftCm)), SP);
	if (!R) return false;
	R->River.Width = 1.0f;
	R->River.FlowSpeed = 0.0f;           // (still)
	R->River.BankFoamWidth = 0.0f;
	R->River.FoamAmount = 0.0f;
	R->River.RippleStrength = 0.6f;
	// (a golf pond's water: olive-brown, dark within a metre or so - the plugin's clear blue-green
	// showed the whole bed down to the middle, a swimming pool. Blue goes first, so what light comes
	// back is green-brown)
	R->River.ScatterColor = PondScatter;
	R->River.Absorption = PondAbsorption;
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
	// Its surface: not the strip down its long axis (its edges straight runs from one station to the
	// next, cutting across the pond's bays and fading out over its ends: water stopping short of the
	// bank) but a flat sheet a metre a square over the whole rounded outline and 2.5 m on under the
	// bank - LiftPondBanks shaped the bank round that outline, so the water's edge is where the ground
	// rises through it
	if (Round && Round->Num() >= 3 && !FParse::Param(FCommandLine::Get(), TEXT("oldponds")))
		if (UProceduralMeshComponent* PM = R->FindComponentByClass<UProceduralMeshComponent>())
		{
			TArray<FVector> RP;
			for (const FVector2D& P : *Round) RP.Add(FVector(P, 0));
			const double Under = 250, Cell = 100;
			FBox2D Box(*Round);
			Box = Box.ExpandBy(Under + Cell);
			const int32 NX = FMath::CeilToInt(Box.GetSize().X / Cell) + 1, NY = FMath::CeilToInt(Box.GetSize().Y / Cell) + 1;
			const FVector Org = PM->GetComponentLocation();
			const double Z = O3.Z + Level + LiftCm - Org.Z;
			auto OverCreek = [&](const FVector2D& Q) {
				for (const FOTGCourseWater::FCreek& Cr : Creeks)
				{
					int32 Seg = 0;
					const double D = DistToLine(Cr.Pts, Q.X, Q.Y, false, &Seg);
					if (D < FMath::Max(Cr.W[Seg], Cr.W[FMath::Min(Seg + 1, Cr.W.Num() - 1)]) * 0.5 + 100.0) return true;
				}
				return false;
			};
			TArray<FVector> V; TArray<FVector> Nm; TArray<FVector2D> UV; TArray<FLinearColor> Col; TArray<FProcMeshTangent> Tg; TArray<int32> Tri;
			V.Reserve(NX * NY);
			for (int32 j = 0; j < NY; j++)
				for (int32 i = 0; i < NX; i++)
				{
					const FVector2D P = Box.Min + FVector2D(i, j) * Cell;
					V.Add(FVector(O3.X + P.X - Org.X, O3.Y + P.Y - Org.Y, Z));
					Nm.Add(FVector::UpVector);
					UV.Add(P * 0.01);
					Col.Add(FLinearColor(0.5f, 0.f, 0.2f, 1.f));
					Tg.Add(FProcMeshTangent(FVector(1, 0, 0), false));
				}
			for (int32 j = 0; j + 1 < NY; j++)
				for (int32 i = 0; i + 1 < NX; i++)
				{
					const FVector2D Mid = Box.Min + FVector2D(i + 0.5, j + 0.5) * Cell;
					if (!Inside(RP, Mid.X, Mid.Y) && DistToLine(RP, Mid.X, Mid.Y, true) > Under) continue;
					// (nor out over a creek leaving it: the creek's water is there, and the sheet's square edge
					// showed over its channel as stairs)
					if (!Inside(RP, Mid.X, Mid.Y) && OverCreek(Mid)) continue;
					const int32 Ia = j * NX + i, Ib = Ia + 1, Ic = Ia + NX, Id = Ic + 1;
					Tri.Append({ Ia, Ic, Id, Ia, Id, Ib });   // (facing up: A->C is +Y, A->B +X)
				}
			if (Tri.Num())
			{
				UMaterialInterface* Mat = PM->GetMaterial(0);
				PM->ClearAllMeshSections();
				PM->CreateMeshSection_LinearColor(0, V, Tri, Nm, UV, Col, Tg, false);
				if (Mat) PM->SetMaterial(0, Mat);
				UE_LOG(LogOTGWater, Display, TEXT("Still pond: a sheet of %d triangles over its rounded outline"), Tri.Num() / 3);
			}
		}
	Rivers.Add(R);
	RiverBoxes.Add(RiverBox(R));
	return true;
}

// The water's meshes kept out of ray tracing, and the ponds simulating: the nearest few to the
// camera; the rest keep their last waves
void FOTGCourseWater::UpdateTidewright()
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
	const APlayerController* PC = Course->GetWorld() ? Course->GetWorld()->GetFirstPlayerController() : nullptr;
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
bool FOTGCourseWater::BuildTidewrightSea(const otg::FGlbMesh& Mesh, const FBox& Box, const FVector2D& Swell, double LevelM)
{
	if (FParse::Param(FCommandLine::Get(), TEXT("oldwater"))) return false;
	UTidewrightPreset* Bay = LoadObject<UTidewrightPreset>(nullptr, TEXT("/Tidewright/Presets/TWP_BreezyBay.TWP_BreezyBay"));
	if (!Bay) { UE_LOG(LogOTGWater, Warning, TEXT("No Tidewright presets (the plugin's content): the game's own sea")); return false; }
	const FVector O = Course->Data->LevelOrigin;
	const double Level = LevelM * 100.0;
	const FVector Centre = O + FVector((Box.Min.X + Box.Max.X) * 50.0, (Box.Min.Z + Box.Max.Z) * 50.0, Level + LiftCm);

	// the shoreline first (the ocean finds it when it ticks): the sea's box and 150 m round it
	const FVector2D Half((Box.Max.X - Box.Min.X) * 50.0 + 15000.0, (Box.Max.Z - Box.Min.Z) * 50.0 + 15000.0);
	FActorSpawnParameters SP;
	SP.bDeferConstruction = true;
	ATidewrightShoreline* Shore = Course->GetWorld()->SpawnActor<ATidewrightShoreline>(ATidewrightShoreline::StaticClass(), FTransform(Centre), SP);
	if (Shore)
	{
		Shore->bRebuildOnBeginPlay = false;
		// (the surf's foam: a faint, narrow band - the foam that moves is the surf's; a stronger band at the
		// water's edge stood still, a white line along the coast)
		Shore->Shore.ShoreFoamAmount = 0.15f;
		Shore->Shore.ShoreFoamDepth = 0.1f;
		// (no run-up, a lower surf: the water rising over the shore drew it over the grass)
		Shore->Shore.RunUpHeight = 0.f;
		Shore->Shore.SurfHeight = 0.35f;
		Shore->GetArea()->SetBoxExtent(FVector(Half.X, Half.Y, 5000.0));
		// (cells of about a metre and a half, up to 2048 a side: at 512 over a long coast they were 4 m
		// and more, and the water's edge followed them in steps, a saw of foam across the grass)
		const int32 Size = FMath::Clamp(FMath::CeilToInt(2.0 * FMath::Max(Half.X, Half.Y) / 150.0), 512, 2048);
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
		// (each cell's distance, metres, to the nearest cell set in In: two chamfer passes)
		auto DistTo = [&](const TArray<uint8>& In, TArray<float>& D) {
			const float Ax = float(CellX / 100.0), Ay = float(CellY / 100.0), Ad = FMath::Sqrt(Ax * Ax + Ay * Ay);
			D.Init(1e9f, Size * Size);
			for (int32 i = 0; i < Size * Size; i++) if (In[i]) D[i] = 0;
			for (int32 y = 0; y < Size; y++)
				for (int32 x = 0; x < Size; x++)
				{
					float& V = D[y * Size + x];
					if (x > 0) V = FMath::Min(V, D[y * Size + x - 1] + Ax);
					if (y > 0) { V = FMath::Min(V, D[(y - 1) * Size + x] + Ay); if (x > 0) V = FMath::Min(V, D[(y - 1) * Size + x - 1] + Ad); if (x < Size - 1) V = FMath::Min(V, D[(y - 1) * Size + x + 1] + Ad); }
				}
			for (int32 y = Size - 1; y >= 0; y--)
				for (int32 x = Size - 1; x >= 0; x--)
				{
					float& V = D[y * Size + x];
					if (x < Size - 1) V = FMath::Min(V, D[y * Size + x + 1] + Ax);
					if (y < Size - 1) { V = FMath::Min(V, D[(y + 1) * Size + x] + Ay); if (x < Size - 1) V = FMath::Min(V, D[(y + 1) * Size + x + 1] + Ad); if (x > 0) V = FMath::Min(V, D[(y + 1) * Size + x - 1] + Ad); }
				}
		};
		TArray<double> Gs;
		Gs.SetNumUninitialized(Size * Size);
		TArray<uint8> Wet;
		Wet.Init(0, Size * Size);
		for (int32 y = 0; y < Size; y++)
			for (int32 x = 0; x < Size; x++)
			{
				const int32 i = y * Size + x;
				Gs[i] = Course->Ground.GroundAt(C.X - Half.X + (x + 0.5) * CellX, C.Y - Half.Y + (y + 0.5) * CellY);
				Wet[i] = InSea[i] && Gs[i] < Level;
			}
		TArray<float> FromSea, FromWater;
		DistTo(InSea, FromSea);
		DistTo(Wet, FromWater);
		// the heights: the ground as it's drawn by the sea, a whisker lower (the water's edge is then
		// where the ground meets the water, under the land, never a line drawn on the grass). Further
		// from the export's sea - 15 to 30 m - low ground (a bunker, a links hollow at the sea's level)
		// rises to dry land a metre up, or the surf and its foam showed through the ground there
		TArray<float> Hts;
		Hts.SetNumUninitialized(Size * Size);
		for (int32 i = 0; i < Size * Size; i++)
		{
			const double Near = Gs[i] - 10.0, Far = FMath::Max(Gs[i], Level + 100.0);
			const double A = FMath::SmoothStep(15.0, 30.0, double(FromSea[i]));
			Hts[i] = float(O.Z + FMath::Lerp(Near, Far, A));
		}
		AddBeach(FromWater, Gs, Size, C - Half, FVector2D(CellX, CellY), Level);
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

	ATidewrightWater* W = Course->GetWorld()->SpawnActor<ATidewrightWater>(ATidewrightWater::StaticClass(), FTransform(Centre), SP);
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

// A sand beach along the sea: the bunkers' mask (the ground's sand; no grass grows on it) widened
// by a band about 12 m wide inland from the water's edge - wavering, and only on low ground: where
// the coast rises over 2 m above the sea it fades out, a cliff stays grass - and under the shallows.
// FromWater: metres to the sea's water, on the shoreline's grid (Size a side from Min, Cell cm a cell);
// Gs: the ground there (cm).
void FOTGCourseWater::AddBeach(const TArray<float>& FromWater, const TArray<double>& Gs, int32 Size, const FVector2D& Min, const FVector2D& Cell, double Level)
{
	if (FParse::Param(FCommandLine::Get(), TEXT("nobeach")) || !Course->GroundMat) return;
	otg::FPicture* P = Course->MaskPixels.Find(TEXT("bunker"));
	if (!P || !P->W)
	{
		otg::FPicture Blank;
		Blank.W = Blank.H = 2048;
		Blank.Pixels.Init(0, Blank.W * Blank.H);
		P = &Course->MaskPixels.Add(TEXT("bunker"), MoveTemp(Blank));
	}
	auto Sample = [&](const auto& A, double Gx, double Gy) {
		const int32 I = FMath::Clamp(FMath::FloorToInt(Gx), 0, Size - 2), J = FMath::Clamp(FMath::FloorToInt(Gy), 0, Size - 2);
		const double U = FMath::Clamp(Gx - I, 0.0, 1.0), V = FMath::Clamp(Gy - J, 0.0, 1.0);
		return FMath::Lerp(FMath::Lerp(double(A[J * Size + I]), double(A[J * Size + I + 1]), U), FMath::Lerp(double(A[(J + 1) * Size + I]), double(A[(J + 1) * Size + I + 1]), U), V);
	};
	int32 Sand = 0;
	ParallelFor(P->H, [&](int32 j)
	{
		for (int32 i = 0; i < P->W; i++)
		{
			const double X = (i + 0.5) / P->W * Course->LandCm, Y = (j + 0.5) / P->H * Course->LandCm;
			const double Gx = (X - Min.X) / Cell.X - 0.5, Gy = (Y - Min.Y) / Cell.Y - 0.5;
			if (Gx < 0 || Gy < 0 || Gx > Size - 1 || Gy > Size - 1) continue;
			const double D = Sample(FromWater, Gx, Gy);
			if (D > 60) continue;
			const double G = Sample(Gs, Gx, Gy);
			const double Width = 12.0 + 5.0 * FMath::PerlinNoise2D(FVector2D(X / 2500.0, Y / 2500.0));
			// (metres inside the beach: its width from the water, less 3 a metre over 2 m up)
			const double In = FMath::Min(Width - D, (Level + 200.0 - G) / 100.0 * 3.0);
			const uint8 V = uint8(FMath::Clamp(0.5 + In / 6.0, 0.0, 1.0) * 255.0);
			uint8& Px = P->Pixels[j * P->W + i];
			if (V > Px) Px = V;
		}
	});
	if (UTexture2D* T = otg::MakeTexture(*P, otg::ETexKind::Mask, Course))
	{
		Course->Textures.Add(TEXT("Mask_bunker"), T);
		Course->GroundMat->SetTextureParameterValue(TEXT("Mask_bunker"), T);
	}
	UE_LOG(LogOTGWater, Display, TEXT("Sea: a beach along it (the bunkers' mask, %d x %d)"), P->W, P->H);
}

// The highest water surface over a point (world cm), or -infinity: the sea, a pond's level, a
// river's surface. The camera stays over it (AOTGRound): nothing in the game is seen from under water
double FOTGCourseWater::WaterTopAt(const FVector& P) const
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

double FOTGCourseWater::OpenWaterAt(const FVector& P) const
{
	const double Top = WaterTopAt(P);
	if (Top < -1e9 || !Course->Data) return Top;
	const FVector O = Course->Data->LevelOrigin;
	return Top > O.Z + Course->Ground.GroundAt(P.X - O.X, P.Y - O.Y) + 3.0 ? Top : -UE_BIG_NUMBER;
}

void FOTGCourseWater::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(Lakes);
	Collector.AddReferencedObjects(Rivers);
	Collector.AddReferencedObject(Sea);
}
