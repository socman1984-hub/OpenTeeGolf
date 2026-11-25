// See OTGGrass.h: the grass streamed round the camera (cells placed off the game thread, added a few a
// frame), and finding a surface or a kind nearby. (The kinds: OTGGrassKinds.cpp.)

#include "World/OTGGrass.h"

#include "Course/OTGCourseData.h"
#include "World/OTGKiteDemo.h"

#include "Async/Async.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"


namespace
{
	constexpr double CellM = 16.0;
	TAutoConsoleVariable<int32> CVarFarGrass(TEXT("OTG.Grass.Far"), 1, TEXT("The grass far off (the rough 80-400 m, the tall grass 150-450 m): 1 on, 0 off"));
	double CellFor(const otg::FGrassKind& K) { return K.CellM > 0 ? K.CellM : CellM; }
	constexpr int32 MaxStartsAFrame = 6, MaxInFlight = 12, MaxAddsAFrame = 4;

	// (deterministic per position: the same grass every time you come back)
	uint32 Hash(int32 A, int32 B, int32 C)
	{
		uint32 H = 2166136261u;
		for (int32 V : { A, B, C }) { H ^= uint32(V); H *= 16777619u; H ^= H >> 13; }
		return H;
	}

	// Smooth value noise 0..1, Size metres across (the meadow's patches: the pack read them from the
	// rock texture's lightness; this has the same scale and spread)
	double Noise(double X, double Y, double Size, int32 Seed)
	{
		const double U = X / Size, V = Y / Size;
		const int32 I = FMath::FloorToInt(U), J = FMath::FloorToInt(V);
		const double FU = U - I, FV = V - J;
		auto R = [&](int32 a, int32 b) { return (Hash(a, b, Seed) & 0xffff) / 65535.0; };
		const double SU = FU * FU * (3 - 2 * FU), SV = FV * FV * (3 - 2 * FV);
		return FMath::Lerp(FMath::Lerp(R(I, J), R(I + 1, J), SU), FMath::Lerp(R(I, J + 1), R(I + 1, J + 1), SU), SV);
	}
}

void otg::FGrass::Init(AActor* InOwner, UOTGCourseData* InData, const TMap<FString, FPicture>& Masks, double LandCm,
	const TArray<FGrassKind>& InKinds, const TMap<FString, TArray<TPair<UStaticMesh*, UMaterialInterface*>>>& KindsHave)
{
	Owner = InOwner;
	Data = InData;
	Pics = Masks;
	Land = LandCm;
	Done = MakeShared<TQueue<TPair<FCellKey, FPlaced>, EQueueMode::Mpsc>>();
	InFlight = MakeShared<FThreadSafeCounter>();
	SetKinds(InKinds, KindsHave);
}

void otg::FGrass::Clear()
{
	Wait();
	TPair<FCellKey, FPlaced> Job;
	while (Done.IsValid() && Done->Dequeue(Job)) {}
	for (auto& C : Cells) for (UInstancedStaticMeshComponent* ISM : C.Value.Comps) if (ISM) ISM->DestroyComponent();
	Cells.Empty();
	Count = 0;
}

void otg::FGrass::SetKinds(const TArray<FGrassKind>& InKinds, const TMap<FString, TArray<TPair<UStaticMesh*, UMaterialInterface*>>>& KindsHave)
{
	Clear();
	KindList = InKinds;
	Meshes.Reset(); Mats.Reset(); KindIndex.Reset();
	for (int32 k = 0; k < KindList.Num(); k++)
	{
		TArray<UStaticMesh*>& M = Meshes.AddDefaulted_GetRef();
		TArray<UMaterialInterface*>& Mt = Mats.AddDefaulted_GetRef();
		if (const TArray<TPair<UStaticMesh*, UMaterialInterface*>>* Have = KindsHave.Find(KindList[k].Name))
			for (const TPair<UStaticMesh*, UMaterialInterface*>& V : *Have)
				if (V.Key) { M.Add(V.Key); Mt.Add(V.Value); }
		if (M.Num() && KindList[k].bOn && KindList[k].Density > 0) KindIndex.Add(k);
	}
	FirstCut = 0;
	for (int32 k : KindIndex) FirstCut = FMath::Max(FirstCut, KindList[k].BandM);
}

float otg::FGrass::FirstCutM(const TArray<FGrassKind>& Kinds)
{
	float B = 0;
	for (const FGrassKind& K : Kinds) if (K.bOn && K.Density > 0) B = FMath::Max(B, K.BandM);
	return B;
}

float otg::FGrass::MeshHeightCm(int32 Kind) const
{
	if (!Meshes.IsValidIndex(Kind) || !Meshes[Kind].Num() || !Meshes[Kind][0]) return 0;
	const FBox B = Meshes[Kind][0]->GetBoundingBox();
	return float(B.Max.Z - FMath::Min(0.0, B.Min.Z));
}

// A mask's value (0..1: the distance fields, 0.5 on the outline) at a level position (cm)
float otg::FGrass::Mask(const FString& Name, double X, double Y) const
{
	const FPicture* P = Pics.Find(Name);
	if (!P || !P->W) return 0.f;
	const double U = X / Land * P->W - 0.5, V = Y / Land * P->H - 0.5;
	const int32 I = FMath::Clamp(FMath::FloorToInt(U), 0, P->W - 2), J = FMath::Clamp(FMath::FloorToInt(V), 0, P->H - 2);
	const double FU = FMath::Clamp(U - I, 0.0, 1.0), FV = FMath::Clamp(V - J, 0.0, 1.0);
	auto At = [&](int32 a, int32 b) { return P->Pixels[b * P->W + a] / 255.0; };
	return float(FMath::Lerp(FMath::Lerp(At(I, J), At(I + 1, J), FU), FMath::Lerp(At(I, J + 1), At(I + 1, J + 1), FU), FV));
}

// The ground's height (cm) at a level position, and its normal
double otg::FGrass::GroundZ(double X, double Y, FVector* OutNormal) const
{
	const int32 N = Data->HeightSamples;
	const double Step = Data->MetresPerSample * 100.0, ZS = Data->HeightRange * 100.0 / 65535.0;
	const double U = X / Step, V = Y / Step;
	const int32 I = FMath::Clamp(FMath::FloorToInt(U), 0, N - 2), J = FMath::Clamp(FMath::FloorToInt(V), 0, N - 2);
	const double FU = FMath::Clamp(U - I, 0.0, 1.0), FV = FMath::Clamp(V - J, 0.0, 1.0);
	auto H = [&](int32 a, int32 b) { return Data->Heights[b * N + a] * ZS; };
	const double H00 = H(I, J), H10 = H(I + 1, J), H01 = H(I, J + 1), H11 = H(I + 1, J + 1);
	if (OutNormal) *OutNormal = FVector((H00 - H10 + H01 - H11) / (2 * Step), (H00 - H01 + H10 - H11) / (2 * Step), 1.0).GetSafeNormal();
	return FMath::Lerp(FMath::Lerp(H00, H10, FU), FMath::Lerp(H01, H11, FU), FV);
}

void otg::FGrass::SetBiomeMap(TArray<uint8> Map, int32 Size)
{
	BiomeMap = MoveTemp(Map);
	BiomeSize = BiomeMap.Num() == Size * Size ? Size : 0;
	Clear();   // (grown again as the map has it)
}

// (the biome at a level position: its cell, the line between two a little ragged - a metre or two of
// noise - so an area's edge isn't ruled)
int32 otg::FGrass::BiomeAt(double X, double Y) const
{
	if (BiomeSize <= 0 || Land <= 0) return 0;
	const double J = 180.0;
	const double JX = (Noise(X / 100.0, Y / 100.0, 3.0, 811) - 0.5) * 2 * J, JY = (Noise(X / 100.0, Y / 100.0, 3.0, 977) - 0.5) * 2 * J;
	const int32 I = FMath::Clamp(FMath::FloorToInt32((X + JX) / Land * BiomeSize), 0, BiomeSize - 1);
	const int32 K = FMath::Clamp(FMath::FloorToInt32((Y + JY) / Land * BiomeSize), 0, BiomeSize - 1);
	return BiomeMap[K * BiomeSize + I];
}

// (0 at the deep rough's outline to 1 its DeepRampM in, ragged with noise: its mats' density and height)
float otg::FGrass::DeepRamp(float W, double X, double Y) const
{
	constexpr float DeepRampM = 2.5f;   // (the masks: 0.5 on the outline, 1/6 a metre in or out)
	const float In = (W - 0.5f) * 6.f / DeepRampM;
	return FMath::SmoothStep(0.f, 1.f, In + (float(Noise(X / 100.0, Y / 100.0, 1.5, 611)) - 0.5f) * 0.9f);
}

// (Place's odds of a patch at a spot, before the random keep: the mask past its edge, not in the
// water or on a path, a road or a mown surface, in its meadow patch)
float otg::FGrass::Chance(const FGrassKind& K, double X, double Y) const
{
	float W = 0;
	for (const FString& M : K.Masks) W = FMath::Max(W, Mask(M, X, Y));
	// (Greensward's mats: on the outline itself, 25 cm either way - our blades reach past it and their
	// material folds them away there)
	float P = K.IsGreensward() ? FMath::Clamp((W - 0.5f) * 24.f + 0.5f, 0.f, 1.f) : FMath::Clamp((W - 0.33f) * 12.f, 0.f, 1.f);
	// (the deep rough's mats thin out towards its edge, raggedly, over its first DeepRampM: at full
	// density from the outline in, it stood out from the rough as a pale patch with a hard rim)
	if (K.IsGreensward() && K.Masks.Contains(TEXT("native")))
		P *= DeepRamp(W, X, Y);
	if (P <= 0) return 0.f;
	// (a grass biome's kind: only in its own biome's areas - OTGBiomes.h)
	if (K.Biome >= 0 && BiomeSize > 0 && BiomeAt(X, Y) != K.Biome) return 0.f;
	P *= FMath::Clamp((0.45f - Mask(TEXT("water"), X, Y)) * 10.f, 0.f, 1.f);
	P *= FMath::Clamp((0.85f - Mask(TEXT("path"), X, Y)) * 2.f, 0.f, 1.f);
	P *= FMath::Clamp((0.85f - Mask(TEXT("road"), X, Y)) * 2.f, 0.f, 1.f);   // (roads and car parks: layer_road.png)
	// (a mown cut's own blades: on its surface - the material folds away any past its outline)
	if (K.bMown) return P;
	// (Greensward's mats aren't cut off by the masks as our blades are, by their material: they're kept
	// clear of the bunkers and streams themselves, 15 cm off)
	if (K.IsGreensward())
		for (const TCHAR* Off : { TEXT("bunker"), TEXT("stream") })
			if (Mask(Off, X, Y) > 0.5f - 0.15f / 6.f) return 0.f;
	// (and never on a mown surface, though the rough's mask reaches into the fringes: tufts
	// up to a few cm over its outline - the material cuts their blades off exactly on it)
	const float Mown = FMath::Max(FMath::Max(Mask(TEXT("fairway"), X, Y), Mask(TEXT("tee"), X, Y)), FMath::Max(Mask(TEXT("fringe"), X, Y), Mask(TEXT("green"), X, Y)));
	// (the masks: 0.5 on the outline, 1/6 a metre in or out)
	if (K.BandM > 0)
	{
		// the first cut: a band round the fairways, tees and collars, a little over either edge
		const float Lo = 0.5f - K.BandM / 6.f;
		return P * FMath::Clamp((Mown - Lo + 0.05f) * 12.f, 0.f, 1.f) * FMath::Clamp((0.56f - Mown) * 12.f, 0.f, 1.f);
	}
	// (the rest kept out of the first cut too, when it grows - but for blades reaching into it - and
	// a wide clump K.Margin inside)
	const float Edge = 0.52f - (K.Masks.Contains(TEXT("rough")) ? FMath::Max(0.f, FirstCut - 0.25f) / 6.f : 0.f);
	P *= 1.f - FMath::Clamp((Mown - Edge + K.Margin / 6.f) * 12.f, 0.f, 1.f);
	if (K.Margin > 0) P *= FMath::Clamp((W - 0.5f - K.Margin / 6.f) * 12.f + 1.f, 0.f, 1.f);
	if (K.PatchM > 0)
	{
		// (value noise bunches round the middle: spread out to the rock texture's range)
		const double N = FMath::Clamp(0.5 + (Noise(X / 100.0, Y / 100.0, K.PatchM, K.BaseName().StartsWith(TEXT("native")) ? 77 : int32(GetTypeHash(K.BaseName().Replace(TEXT("_far"), TEXT(""))) % 997) + 31) - 0.5) * 1.8, 0.0, 1.0);
		const double L = 0.27 + 0.19 * N;
		float F = FMath::SmoothStep(K.PatchLo, K.PatchHi, float(L));
		P *= K.bInvert ? 1.f - F : F;
	}
	return P;
}

// A cell's patches: a grid as dense as the kind, jittered, each kept as likely as the pack's grass
// map would have it (the mask past its edge, not in the water or on a path, in its meadow patch)
otg::FGrass::FPlaced otg::FGrass::Place(int32 KindI, int32 CX, int32 CY) const
{
	const FGrassKind& K = KindList[KindI]; const double Cell = CellFor(K);
	const int32 NV = FMath::Max(1, Meshes.IsValidIndex(KindI) ? Meshes[KindI].Num() : 1);
	FPlaced Out;
	Out.SetNum(NV);
	const double Spacing = 1000.0 / FMath::Sqrt(K.Density);   // cm (Density a 10 x 10 m)
	const int32 Per = FMath::Max(1, FMath::RoundToInt(Cell * 100.0 / Spacing));
	const double S = Cell * 100.0 / Per;
	FRandomStream R(int32(Hash(KindI, CX, CY)));
	// (the mown cuts' square patches tile: on the grid exactly, turned by quarter turns, only their
	// height varied)
	const bool bTiled = K.bMown || K.BandM > 0 || K.TileCm > 0;
	const double Tile = K.TileCm > 0 ? K.TileCm : 20.0;
	const float Jit = bTiled ? 0.f : K.Jitter;
	// Greensward's mats: a whole tile where all of it's in; one on an edge split in quarters, and
	// those again (to 12.5 cm), the last kept where their middle is in, a little oversized so they
	// overlap and cut a little shorter - the deep rough meets the rough without 50 cm steps
	if (K.IsGreensward())
	{
		auto Add = [&](double X, double Y, double Size, double Yaw, float ZScale)
		{
			FVector Nrm;
			const double Z = GroundZ(X, Y, &Nrm);
			const FQuat Tilt = FQuat::FindBetweenNormals(FVector::UpVector, Nrm);
			const double XY = Size / Tile;
			Out[NV > 1 ? R.RandRange(0, NV - 1) : 0].Add(FTransform(Tilt * FQuat(FVector::UpVector, Yaw), FVector(X, Y, Z) + Data->LevelOrigin, FVector(XY, XY, ZScale)));
		};
		TFunction<void(double, double, double, int32)> Fill = [&](double X, double Y, double Size, int32 Level)
		{
			const double C = Size * 0.48;
			const float Ps[5] = { Chance(K, X, Y), Chance(K, X - C, Y - C), Chance(K, X + C, Y - C), Chance(K, X + C, Y + C), Chance(K, X - C, Y + C) };
			float Lo = 1, Hi = 0;
			for (const float P : Ps) { Lo = FMath::Min(Lo, P); Hi = FMath::Max(Hi, P); }
			const float Sc = FMath::Lerp(K.ScaleLo, K.ScaleHi, R.FRand());
			if (Lo >= 0.999f) { Add(X, Y, Size, R.RandRange(0, 3) * 0.5 * PI, Sc); return; }
			if (Hi <= 0.001f) return;
			if (Level < 2)
			{
				const double Q = Size * 0.25;
				for (const FVector2D& O : { FVector2D(-Q, -Q), FVector2D(Q, -Q), FVector2D(Q, Q), FVector2D(-Q, Q) }) Fill(X + O.X, Y + O.Y, Size * 0.5, Level + 1);
				return;
			}
			// (kept only where its middle is in, a little shorter: a fine, even edge, not a torn one)
			if (Ps[0] < 0.5f) return;
			const FVector2D Nudge(R.FRandRange(-0.1f, 0.1f) * Size, R.FRandRange(-0.1f, 0.1f) * Size);
			Add(X + Nudge.X, Y + Nudge.Y, Size * 1.25, R.RandRange(0, 3) * 0.5 * PI, Sc * R.FRandRange(0.8f, 1.f));
		};
		for (int32 b = 0; b < Per; b++)
			for (int32 a = 0; a < Per; a++)
			{
				const double X = CX * Cell * 100.0 + (a + 0.5) * S, Y = CY * Cell * 100.0 + (b + 0.5) * S;
				if (X < 0 || Y < 0 || X > Land || Y > Land) continue;
				Fill(X, Y, S, 0);
			}
		return Out;
	}
	for (int32 b = 0; b < Per; b++)
		for (int32 a = 0; a < Per; a++)
		{
			const double X = CX * Cell * 100.0 + (a + 0.5 + (R.FRand() - 0.5) * Jit) * S;
			const double Y = CY * Cell * 100.0 + (b + 0.5 + (R.FRand() - 0.5) * Jit) * S;
			const float Keep = R.FRand();
			if (X < 0 || Y < 0 || X > Land || Y > Land) continue;
			const float P = Chance(K, X, Y);
			if (Keep >= P) continue;
			FVector Nrm;
			const double Z = GroundZ(X, Y, &Nrm);
			FQuat Tilt = FQuat::FindBetweenNormals(FVector::UpVector, Nrm);
			const FQuat Turn(FVector::UpVector, bTiled ? R.RandRange(0, 3) * 0.5 * PI : R.FRand() * 2 * PI);
			float Sc = FMath::Lerp(K.ScaleLo, K.ScaleHi, R.FRand());
			// (and lower there: the deep rough rises out of the rough rather than starting as a step)
			if (K.IsGreensward() && K.Masks.Contains(TEXT("native")))
				Sc *= FMath::Lerp(0.8f, 1.f, DeepRamp(Mask(TEXT("native"), X, Y), X, Y));
			if (K.Masks.Contains(TEXT("native")))
			{
				// (left to grow: taller here, lower there over a few metres, and each clump leaning its own way)
				const float Vigor = FMath::SmoothStep(0.2f, 0.8f, float(Noise(X / 100.0, Y / 100.0, 7.0, 501)));
				Sc *= K.Fab.Num() ? FMath::Lerp(0.8f, 1.2f, Vigor) : FMath::Lerp(0.55f, 1.35f, Vigor);   // (the Megascans clumps vary in size already)
				const FVector Lean = FVector(R.FRandRange(-1, 1), R.FRandRange(-1, 1), 0).GetSafeNormal();
				Tilt = FQuat(Lean ^ FVector::UpVector, FMath::DegreesToRadians(R.FRandRange(0.f, 10.f))) * Tilt;
			}
			Out[NV > 1 ? R.RandRange(0, NV - 1) : 0].Add(FTransform(Tilt * Turn, FVector(X, Y, Z) + Data->LevelOrigin, bTiled ? FVector(S / Tile, S / Tile, Sc) : FVector(Sc)));
		}
	return Out;
}

void otg::FGrass::Update(const FVector& Camera)
{
	AActor* O = Owner.Get();
	if (!O || !Data) return;
	const FVector C = Camera - Data->LevelOrigin;
	// finished cells: their instances
	TPair<FCellKey, FPlaced> Job;
	for (int32 n = 0; n < MaxAddsAFrame && Done->Dequeue(Job); n++)
	{
		FCell* Cell = Cells.Find(Job.Key);
		if (!Cell) continue;   // (dropped meanwhile)
		Cell->bPending = false;
		const FGrassKind& K = KindList[Job.Key.Kind];
		for (int32 v = 0; v < Job.Value.Num(); v++)
		{
			if (!Job.Value[v].Num() || !Meshes[Job.Key.Kind].IsValidIndex(v)) continue;
			// (the Megascans plants hierarchically: each clump to its billboard as it goes off)
			UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(O);
			ISM->SetStaticMesh(Meshes[Job.Key.Kind][v]);
			if (UMaterialInterface* Mat = Mats[Job.Key.Kind][v])
			{
				// (every slot: a Greensward mat's blades and its turf base)
				const int32 Slots = FMath::Max(1, Meshes[Job.Key.Kind][v]->GetStaticMaterials().Num());
				for (int32 m = 0; m < Slots; m++) ISM->SetMaterial(m, Mat);
			}
			else if (Swaps.Num())
			{
				// (a Megascans plant: its own materials, the tinted copies where the course made them)
				const TArray<FStaticMaterial>& SM = Meshes[Job.Key.Kind][v]->GetStaticMaterials();
				for (int32 m = 0; m < SM.Num(); m++)
					if (UMaterialInterface* const* To = Swaps.Find(SM[m].MaterialInterface)) ISM->SetMaterial(m, *To);
			}
			ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			ISM->SetCastShadow(false);
			ISM->bVisibleInRayTracing = false;   // (rebuilt into the ray-tracing scene each frame, for nothing: ~1 ms)
			ISM->SetMobility(EComponentMobility::Movable);
			ISM->InstanceStartCullDistance = int32(K.DrawFrom * 100);
			ISM->InstanceEndCullDistance = int32(K.DrawTo * 100);
			ISM->MinDrawDistance = K.MinDrawM * 100;
			ISM->SetupAttachment(O->GetRootComponent());
			ISM->RegisterComponent();
			ISM->AddInstances(Job.Value[v], false, true);
			Cell->Comps.Add(ISM);
			Count += Job.Value[v].Num();
		}
	}
	// far cells: dropped
	for (auto It = Cells.CreateIterator(); It; ++It)
	{
		const FGrassKind& K = KindList[It->Key.Kind]; const double Cell = CellFor(K);
		const bool bOff = K.MinDrawM > 0 && !CVarFarGrass.GetValueOnGameThread();
		const FVector2D Mid((It->Key.X + 0.5) * Cell * 100, (It->Key.Y + 0.5) * Cell * 100);
		if (bOff || FVector2D::Distance(Mid, FVector2D(C)) > (K.DrawTo + Cell) * 100)
		{
			for (UInstancedStaticMeshComponent* ISM : It->Value.Comps) if (ISM) { Count -= ISM->GetInstanceCount(); ISM->DestroyComponent(); }
			It.RemoveCurrent();
		}
	}
	// near cells not there yet: placed on worker threads, nearest first
	int32 Started = 0;
	for (int32 KindI : KindIndex)
	{
		const FGrassKind& K = KindList[KindI]; const double Cell = CellFor(K);
		if (K.MinDrawM > 0 && !CVarFarGrass.GetValueOnGameThread()) continue;
		const int32 Rad = FMath::CeilToInt(K.DrawTo / Cell);
		const int32 CX0 = FMath::FloorToInt(C.X / (Cell * 100)), CY0 = FMath::FloorToInt(C.Y / (Cell * 100));
		TArray<FIntPoint> Want;
		for (int32 dy = -Rad; dy <= Rad; dy++)
			for (int32 dx = -Rad; dx <= Rad; dx++)
			{
				const FVector2D Mid((CX0 + dx + 0.5) * Cell * 100, (CY0 + dy + 0.5) * Cell * 100);
				if (FVector2D::Distance(Mid, FVector2D(C)) > (K.DrawTo + Cell * 0.71) * 100) continue;
				if (Mid.X < -Cell * 100 || Mid.Y < -Cell * 100 || Mid.X > Land + Cell * 100 || Mid.Y > Land + Cell * 100) continue;
				if (!Cells.Contains(FCellKey{ KindI, CX0 + dx, CY0 + dy })) Want.Add(FIntPoint(CX0 + dx, CY0 + dy));
			}
		Want.Sort([&](const FIntPoint& A, const FIntPoint& B) { return FVector2D(A - FIntPoint(CX0, CY0)).SizeSquared() < FVector2D(B - FIntPoint(CX0, CY0)).SizeSquared(); });
		for (const FIntPoint& P : Want)
		{
			if (Started >= MaxStartsAFrame || InFlight->GetValue() >= MaxInFlight) return;
			const FCellKey Key{ KindI, P.X, P.Y };
			Cells.Add(Key, FCell{ {}, true });
			InFlight->Increment();
			Started++;
			auto DoneQ = Done;
			auto Flight = InFlight;
			const FGrass* Self = this;
			Async(EAsyncExecution::ThreadPool, [Self, Key, DoneQ, Flight]() {
				DoneQ->Enqueue(TPair<FCellKey, FPlaced>(Key, Self->Place(Key.Kind, Key.X, Key.Y)));
				Flight->Decrement();
			});
		}
	}
}

bool otg::FGrass::FindSurface(const FString& Name, const FVector& From, FVector& Out, double InsideM, double MaxM) const
{
	if (!Data || Land <= 0 || !Pics.Contains(Name)) return false;
	const FVector C = From - Data->LevelOrigin;
	static const TCHAR* Others[] = { TEXT("fairway"), TEXT("tee"), TEXT("green"), TEXT("fringe"), TEXT("bunker"), TEXT("water"), TEXT("stream"), TEXT("native"), TEXT("path"), TEXT("road") };
	auto In = [&](double X, double Y) {
		if (X < 0 || Y < 0 || X > Land || Y > Land || Mask(Name, X, Y) < 0.5f + float(InsideM) / 6.f) return false;
		for (const TCHAR* O : Others)
			if (Name != O && !(Name == TEXT("fairway") && FCString::Strcmp(O, TEXT("tee")) == 0) && Pics.Contains(O) && Mask(O, X, Y) > 0.45f) return false;
		return true;
	};
	// (a step as fine as the surface is thin: a collar's a metre wide)
	const double Step = InsideM < 1 ? 70 : 300;
	for (double R = 1000; R <= MaxM * 100; R += Step)
	{
		const int32 N = FMath::Max(8, FMath::RoundToInt(2 * PI * R / Step));
		for (int32 i = 0; i < N; i++)
		{
			const double A = 2 * PI * i / N;
			const double X = C.X + FMath::Cos(A) * R, Y = C.Y + FMath::Sin(A) * R;
			if (!In(X, Y)) continue;
			Out = FVector(X, Y, GroundZ(X, Y, nullptr)) + Data->LevelOrigin;
			return true;
		}
	}
	return false;
}

bool otg::FGrass::FindNear(const FString& Name, const FVector& From, FVector& Out, double MaxM) const
{
	const int32 KI = KindList.IndexOfByPredicate([&](const FGrassKind& X) { return X.Name == Name; });
	if (KI == INDEX_NONE || !Data || Land <= 0) return false;
	const FGrassKind& K = KindList[KI];
	if (!K.Masks.Num()) return false;
	const FVector C = From - Data->LevelOrigin;
	// rings out from where you are, 4 m apart: the first spot where it grows thickly, and round it too
	// (a patch of it, not a stray clump) - not nearer than 8 m, so there's room to see it
	auto Thick = [&](double X, double Y) {
		if (X < 0 || Y < 0 || X > Land || Y > Land || Chance(K, X, Y) < 0.75f) return false;
		for (const FVector2D D : { FVector2D(250, 0), FVector2D(-250, 0), FVector2D(0, 250), FVector2D(0, -250) })
			if (Chance(K, X + D.X, Y + D.Y) < 0.5f) return false;
		return true;
	};
	for (double R = 800; R <= MaxM * 100; R += 400)
	{
		const int32 N = FMath::Max(8, FMath::RoundToInt(2 * PI * R / 400));
		for (int32 i = 0; i < N; i++)
		{
			const double A = 2 * PI * i / N;
			const double X = C.X + FMath::Cos(A) * R, Y = C.Y + FMath::Sin(A) * R;
			if (!Thick(X, Y)) continue;
			Out = FVector(X, Y, GroundZ(X, Y, nullptr)) + Data->LevelOrigin;
			return true;
		}
	}
	return false;
}

TArray<UPrimitiveComponent*> otg::FGrass::ComponentsNear(const FVector& At, double R) const
{
	TArray<UPrimitiveComponent*> Out;
	for (const auto& C : Cells)
		for (UInstancedStaticMeshComponent* I : C.Value.Comps)
			if (I && I->IsRegistered() && I->GetInstanceCount() > 0 && I->Bounds.GetBox().ExpandBy(R).IsInsideXY(At)) Out.Add(I);
	return Out;
}
