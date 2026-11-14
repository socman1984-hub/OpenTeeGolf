// See OTGGrass.h.

#include "OTGGrass.h"

#include "OTGCourseData.h"

#include "Async/Async.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

namespace
{
	constexpr double CellM = 16.0;
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

// build_course.py TURF_GRASS, as the game grows it (keep the two the same)
const TArray<otg::FGrassKind>& otg::FGrass::Kinds()
{
	static const TArray<FGrassKind> K = [] {
		TArray<FGrassKind> Out;
		auto Add = [&](const TCHAR* Name, TArray<FString> Masks, float Density, float From, float To, float Jitter, float SLo, float SHi, float PatchM = 0, float PLo = 0, float PHi = 0, bool bInv = false) {
			FGrassKind G;
			G.Name = Name; G.Masks = MoveTemp(Masks); G.Density = Density; G.DrawFrom = From; G.DrawTo = To; G.Jitter = Jitter;
			G.ScaleLo = SLo; G.ScaleHi = SHi; G.PatchM = PatchM; G.PatchLo = PLo; G.PatchHi = PHi; G.bInvert = bInv;
			Out.Add(G);
		};
		Add(TEXT("rough"), { TEXT("rough") }, 2400, 70, 100, 1.0f, 0.85f, 1.2f);
		Add(TEXT("fairway"), { TEXT("fairway"), TEXT("tee") }, 2600, 30, 42, 0.3f, 0.95f, 1.05f);
		Add(TEXT("native"), { TEXT("native") }, 320, 140, 200, 1.0f, 0.7f, 1.3f, 16, 0.31f, 0.39f, false);
		Add(TEXT("native_short"), { TEXT("native") }, 520, 110, 160, 1.0f, 0.75f, 1.25f, 16, 0.31f, 0.39f, true);
		Add(TEXT("flowers_yellow"), { TEXT("native") }, 70, 70, 100, 1.0f, 0.85f, 1.2f, 9, 0.43f, 0.48f, false);
		Add(TEXT("flowers_white"), { TEXT("native") }, 70, 70, 100, 1.0f, 0.85f, 1.2f, 11, 0.44f, 0.49f, false);
		Add(TEXT("flowers_purple"), { TEXT("native") }, 60, 70, 100, 1.0f, 0.85f, 1.2f, 7, 0.44f, 0.5f, false);
		return Out;
	}();
	return K;
}

void otg::FGrass::Init(AActor* InOwner, UOTGCourseData* InData, const TMap<FString, FPicture>& Masks, double LandCm,
	const TMap<FString, TPair<UStaticMesh*, UMaterialInterface*>>& KindsHave)
{
	Owner = InOwner;
	Data = InData;
	Pics = Masks;
	Land = LandCm;
	Done = MakeShared<TQueue<TPair<FCellKey, TArray<FTransform>>, EQueueMode::Mpsc>>();
	InFlight = MakeShared<FThreadSafeCounter>();
	for (int32 k = 0; k < Kinds().Num(); k++)
	{
		const TPair<UStaticMesh*, UMaterialInterface*>* Have = KindsHave.Find(Kinds()[k].Name);
		Meshes.Add(Have ? Have->Key : nullptr);
		Mats.Add(Have ? Have->Value : nullptr);
		if (Have && Have->Key) KindIndex.Add(k);
	}
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

// A cell's patches: a grid as dense as the kind, jittered, each kept as likely as the pack's grass
// map would have it (the mask past its edge, not in the water or on a path, in its meadow patch)
TArray<FTransform> otg::FGrass::Place(int32 KindI, int32 CX, int32 CY) const
{
	const FGrassKind& K = Kinds()[KindI];
	TArray<FTransform> Out;
	const double Spacing = 1000.0 / FMath::Sqrt(K.Density);   // cm (Density a 10 x 10 m)
	const int32 Per = FMath::Max(1, FMath::RoundToInt(CellM * 100.0 / Spacing));
	const double S = CellM * 100.0 / Per;
	FRandomStream R(int32(Hash(KindI, CX, CY)));
	for (int32 b = 0; b < Per; b++)
		for (int32 a = 0; a < Per; a++)
		{
			const double X = CX * CellM * 100.0 + (a + 0.5 + (R.FRand() - 0.5) * K.Jitter) * S;
			const double Y = CY * CellM * 100.0 + (b + 0.5 + (R.FRand() - 0.5) * K.Jitter) * S;
			const float Keep = R.FRand();
			if (X < 0 || Y < 0 || X > Land || Y > Land) continue;
			float W = 0;
			for (const FString& M : K.Masks) W = FMath::Max(W, Mask(M, X, Y));
			float P = FMath::Clamp((W - 0.33f) * 12.f, 0.f, 1.f);
			if (P <= 0) continue;
			P *= FMath::Clamp((0.45f - Mask(TEXT("water"), X, Y)) * 10.f, 0.f, 1.f);
			P *= FMath::Clamp((0.85f - Mask(TEXT("path"), X, Y)) * 2.f, 0.f, 1.f);
			if (K.PatchM > 0)
			{
				// (value noise bunches round the middle: spread out to the rock texture's range)
				const double N = FMath::Clamp(0.5 + (Noise(X / 100.0, Y / 100.0, K.PatchM, KindI < 4 ? 77 : KindI * 31) - 0.5) * 1.8, 0.0, 1.0);
				const double L = 0.27 + 0.19 * N;
				float F = FMath::SmoothStep(K.PatchLo, K.PatchHi, float(L));
				P *= K.bInvert ? 1.f - F : F;
			}
			if (Keep >= P) continue;
			FVector Nrm;
			const double Z = GroundZ(X, Y, &Nrm);
			const FQuat Tilt = FQuat::FindBetweenNormals(FVector::UpVector, Nrm);
			const FQuat Turn(FVector::UpVector, R.FRand() * 2 * PI);
			const float Sc = FMath::Lerp(K.ScaleLo, K.ScaleHi, R.FRand());
			Out.Add(FTransform(Tilt * Turn, FVector(X, Y, Z) + Data->LevelOrigin, FVector(Sc)));
		}
	return Out;
}

void otg::FGrass::Update(const FVector& Camera)
{
	AActor* O = Owner.Get();
	if (!O || !Data) return;
	const FVector C = Camera - Data->LevelOrigin;
	// finished cells: their instances
	TPair<FCellKey, TArray<FTransform>> Job;
	for (int32 n = 0; n < MaxAddsAFrame && Done->Dequeue(Job); n++)
	{
		FCell* Cell = Cells.Find(Job.Key);
		if (!Cell) continue;   // (dropped meanwhile)
		Cell->bPending = false;
		if (!Job.Value.Num()) continue;
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(O);
		ISM->SetStaticMesh(Meshes[Job.Key.Kind]);
		if (Mats[Job.Key.Kind]) ISM->SetMaterial(0, Mats[Job.Key.Kind]);
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->SetCastShadow(false);
		ISM->SetMobility(EComponentMobility::Movable);
		const FGrassKind& K = Kinds()[Job.Key.Kind];
		ISM->InstanceStartCullDistance = int32(K.DrawFrom * 100);
		ISM->InstanceEndCullDistance = int32(K.DrawTo * 100);
		ISM->SetupAttachment(O->GetRootComponent());
		ISM->RegisterComponent();
		ISM->AddInstances(Job.Value, false, true);
		Cell->Comp = ISM;
		Count += Job.Value.Num();
	}
	// far cells: dropped
	for (auto It = Cells.CreateIterator(); It; ++It)
	{
		const FGrassKind& K = Kinds()[It->Key.Kind];
		const FVector2D Mid((It->Key.X + 0.5) * CellM * 100, (It->Key.Y + 0.5) * CellM * 100);
		if (FVector2D::Distance(Mid, FVector2D(C)) > (K.DrawTo + CellM) * 100)
		{
			if (UInstancedStaticMeshComponent* ISM = It->Value.Comp) { Count -= ISM->GetInstanceCount(); ISM->DestroyComponent(); }
			It.RemoveCurrent();
		}
	}
	// near cells not there yet: placed on worker threads, nearest first
	int32 Started = 0;
	for (int32 KindI : KindIndex)
	{
		const FGrassKind& K = Kinds()[KindI];
		const int32 Rad = FMath::CeilToInt(K.DrawTo / CellM);
		const int32 CX0 = FMath::FloorToInt(C.X / (CellM * 100)), CY0 = FMath::FloorToInt(C.Y / (CellM * 100));
		TArray<FIntPoint> Want;
		for (int32 dy = -Rad; dy <= Rad; dy++)
			for (int32 dx = -Rad; dx <= Rad; dx++)
			{
				const FVector2D Mid((CX0 + dx + 0.5) * CellM * 100, (CY0 + dy + 0.5) * CellM * 100);
				if (FVector2D::Distance(Mid, FVector2D(C)) > (K.DrawTo + CellM * 0.71) * 100) continue;
				if (Mid.X < -CellM * 100 || Mid.Y < -CellM * 100 || Mid.X > Land + CellM * 100 || Mid.Y > Land + CellM * 100) continue;
				if (!Cells.Contains(FCellKey{ KindI, CX0 + dx, CY0 + dy })) Want.Add(FIntPoint(CX0 + dx, CY0 + dy));
			}
		Want.Sort([&](const FIntPoint& A, const FIntPoint& B) { return FVector2D(A - FIntPoint(CX0, CY0)).SizeSquared() < FVector2D(B - FIntPoint(CX0, CY0)).SizeSquared(); });
		for (const FIntPoint& P : Want)
		{
			if (Started >= MaxStartsAFrame || InFlight->GetValue() >= MaxInFlight) return;
			const FCellKey Key{ KindI, P.X, P.Y };
			Cells.Add(Key, FCell{ nullptr, true });
			InFlight->Increment();
			Started++;
			auto DoneQ = Done;
			auto Flight = InFlight;
			const FGrass* Self = this;
			Async(EAsyncExecution::ThreadPool, [Self, Key, DoneQ, Flight]() {
				DoneQ->Enqueue(TPair<FCellKey, TArray<FTransform>>(Key, Self->Place(Key.Kind, Key.X, Key.Y)));
				Flight->Decrement();
			});
		}
	}
}
