// See OTGGrass.h.

#include "OTGGrass.h"

#include "OTGCourseData.h"

#include "Async/Async.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr double CellM = 16.0;
	TAutoConsoleVariable<int32> CVarFarGrass(TEXT("OTG.Grass.Far"), 1, TEXT("The grass far off (the rough 80-220 m, the tall grass 150-450 m): 1 on, 0 off"));
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

// build_course.py TURF_GRASS, as the game grows it (keep the two the same)
const TArray<otg::FGrassKind>& otg::FGrass::DefaultKinds()
{
	static const TArray<FGrassKind> K = [] {
		TArray<FGrassKind> Out;
		auto Add = [&](const TCHAR* Name, const TCHAR* Label, TArray<FString> Masks, float Density, float From, float To, float Jitter, float SLo, float SHi, float PatchM = 0, float PLo = 0, float PHi = 0, bool bInv = false) -> FGrassKind& {
			FGrassKind G;
			G.Name = Name; G.Label = Label; G.Masks = MoveTemp(Masks); G.Density = Density; G.DrawFrom = From; G.DrawTo = To; G.Jitter = Jitter;
			G.ScaleLo = SLo; G.ScaleHi = SHi; G.PatchM = PatchM; G.PatchLo = PLo; G.PatchHi = PHi; G.bInvert = bInv;
			return Out.Add_GetRef(G);
		};
		// the far-off copy of a kind of tall grass: a fifth as many patches, bigger, from 150 m out
		auto Far = [&](const TCHAR* Of) {
			FGrassKind F = *Out.FindByPredicate([Of](const FGrassKind& X) { return X.Name == Of; });
			F.FarOf = F.Name;
			F.Name += TEXT("_far");
			F.Label += TEXT(" (far off)");
			F.Density /= 5;
			F.DrawFrom = 380; F.DrawTo = 450;
			F.ScaleLo *= 1.3f; F.ScaleHi *= 1.3f;
			F.MinDrawM = 150;
			F.CellM = 64;
			Out.Add(F);
		};
		Add(TEXT("rough"), TEXT("Rough grass"), { TEXT("rough") }, 2400, 70, 100, 1.0f, 0.85f, 1.2f).bEven = true;
		// the rough far off, 80 to 220 m: fewer, wider blades in bigger patches (build_course.py's
		// rough_far), so the rough keeps a grass texture past where its near blades go - it ended in a
		// bare band at 100 m. Off with the other far kinds (OTG.Grass.Far 0)
		{
			FGrassKind& G = Add(TEXT("rough_far"), TEXT("Rough grass (far off)"), { TEXT("rough") }, 200, 170, 220, 1.0f, 1.1f, 1.45f);
			G.MinDrawM = 80;
			G.CellM = 48;
			G.FarOf = TEXT("rough");
			G.bEven = true;
		}
		// (no blades on the mown surfaces - fairways, tees, fringes, greens: mown that short, real turf
		// reads as a smooth surface, and blades on it looked like rough; the ground draws them)
		// tufts through the rough: taller clumps of the same grass in the same colours (build_course.py's
		// rough_clump) - a kept rough is one even green; the Megascans clumps here before came in their
		// own straw and lime. Kept a little inside it, clear of the fairways
		{
			FGrassKind& G = Add(TEXT("rough_clump"), TEXT("Rough tufts"), { TEXT("rough") }, 70, 60, 90, 1.0f, 0.75f, 1.3f);
			G.Margin = 0.3f;
			G.bEven = true;
		}
		// the tall grass, Megascans plants in drifts, kept low and dense as the heathland rough round a
		// links or heath course is - knee high, 30 to 60 cm, not the 1 to 2 m of a hay meadow: fescue
		// (tall grass, its meshes 60-105 cm, at under half size) in its patches, wild and tundra grass
		// in the others, each out to 450 m (their own billboards far off), and our short meadow grass
		// under them as a base. (Wheat grass and rough meadow grass, 1.2-1.7 m with seed heads, are
		// out: too tall however they're scaled.) Their colours: make_game_assets.py's fab_tints
		Add(TEXT("native_short"), TEXT("Short meadow grass"), { TEXT("native") }, 520, 110, 160, 1.0f, 0.75f, 1.25f, 16, 0.31f, 0.39f, true);
		{
			// (out to 60 m: further off its pale tufts ran together into cream-yellow drifts across the
			// tall grass, looking into the sun most of all; the wild grass carries the drifts on)
			FGrassKind& G = Add(TEXT("fescue"), TEXT("Fescue (Megascans)"), { TEXT("native") }, 560, 40, 60, 1.0f, 0.42f, 0.6f, 22, 0.33f, 0.40f, false);
			G.Fab = { TEXT("Tall_Grass_rbEkI"), TEXT("Tall_Grass_rbEkP"), TEXT("Tall_Grass_rbEla") };
			G.Margin = 0.5f;
			G.CellM = 48;
		}
		{
			FGrassKind& G = Add(TEXT("meadow"), TEXT("Wild and tundra grass (Megascans)"), { TEXT("native") }, 480, 250, 400, 1.0f, 0.5f, 0.72f, 22, 0.33f, 0.40f, true);
			G.Fab = { TEXT("Wild_Grass_vczndjqja"), TEXT("Tundra_Grass_wk2oaeyqx"), TEXT("Grass_smdrailr"), TEXT("Grass_slBm1") };
			G.Margin = 0.6f;
			G.CellM = 48;
		}
		Far(TEXT("native_short"));
		Add(TEXT("flowers_yellow"), TEXT("Yellow wildflowers"), { TEXT("native") }, 70, 70, 100, 1.0f, 0.85f, 1.2f, 9, 0.43f, 0.48f, false);
		Add(TEXT("flowers_white"), TEXT("White wildflowers"), { TEXT("native") }, 70, 70, 100, 1.0f, 0.85f, 1.2f, 11, 0.44f, 0.49f, false);
		Add(TEXT("flowers_purple"), TEXT("Purple wildflowers"), { TEXT("native") }, 60, 70, 100, 1.0f, 0.85f, 1.2f, 7, 0.44f, 0.5f, false);
		// the game's other tall grasses (TURF_GRASS: made, not grown unless the designer picks them)
		Add(TEXT("native"), TEXT("Tall wild grass"), { TEXT("native") }, 320, 140, 200, 1.0f, 0.7f, 1.3f, 16, 0.31f, 0.39f, false).bOn = false;
		Far(TEXT("native"));
		Add(TEXT("fescue_gold"), TEXT("Golden fescue, gone over"), { TEXT("native") }, 280, 140, 200, 1.0f, 0.6f, 1.4f, 22, 0.34f, 0.39f, false).bOn = false;
		Far(TEXT("fescue_gold"));
		Add(TEXT("tussock"), TEXT("Tussocks"), { TEXT("native") }, 90, 110, 160, 1.0f, 0.6f, 1.5f).bOn = false;
		Far(TEXT("tussock"));
		return Out;
	}();
	return K;
}

otg::FGrassKind otg::FGrass::PlantKind(const FString& Folder)
{
	FGrassKind G;
	G.Name = TEXT("fab_") + Folder;
	// (Wild_Grass_vlkhcbxia: "Wild grass")
	FString L = Folder;
	int32 Us;
	if (L.FindLastChar(TEXT('_'), Us) && L.Len() - Us > 6) L = L.Left(Us);
	L = L.Replace(TEXT("_"), TEXT(" ")).ToLower();
	if (L.Len()) L[0] = FChar::ToUpper(L[0]);
	G.Label = L + TEXT(" (Megascans)");
	G.Masks = { TEXT("native") };
	G.Density = 200; G.DrawFrom = 250; G.DrawTo = 400;
	G.ScaleLo = 0.45f; G.ScaleHi = 0.65f;
	G.PatchM = 22; G.PatchLo = 0.33f; G.PatchHi = 0.40f;
	G.Margin = 0.6f;
	G.CellM = 48;
	G.Fab = { Folder };
	G.bAdded = true;
	return G;
}

void otg::FGrass::Derive(TArray<FGrassKind>& Kinds)
{
	const TArray<FGrassKind>& Def = DefaultKinds();
	auto DefOf = [&](const FString& N) { return Def.FindByPredicate([&](const FGrassKind& X) { return X.Name == N; }); };
	for (FGrassKind& F : Kinds)
	{
		if (F.FarOf.IsEmpty()) continue;
		const FGrassKind* P = Kinds.FindByPredicate([&](const FGrassKind& X) { return X.Name == F.FarOf; });
		const FGrassKind* DF = DefOf(F.Name);
		const FGrassKind* DP = DefOf(F.FarOf);
		if (!P || !DF || !DP) continue;
		// (as many, as tall, as its near kind is against the game's own, and starting where it ends)
		F.bOn = P->bOn;
		F.Masks = P->Masks;
		F.Density = DF->Density * P->Density / FMath::Max(1.f, DP->Density);
		const float S = (P->ScaleLo + P->ScaleHi) / FMath::Max(0.01f, DP->ScaleLo + DP->ScaleHi);
		F.ScaleLo = DF->ScaleLo * S; F.ScaleHi = DF->ScaleHi * S;
		F.MinDrawM = DF->MinDrawM * P->DrawTo / FMath::Max(1.f, DP->DrawTo);
		F.DrawTo = FMath::Max(DF->DrawTo, F.MinDrawM + 60);
		F.DrawFrom = F.DrawTo - (DF->DrawTo - DF->DrawFrom);
		F.PatchM = P->PatchM; F.PatchLo = P->PatchLo; F.PatchHi = P->PatchHi; F.bInvert = P->bInvert;
	}
}

namespace
{
	TSharedRef<FJsonObject> KindJson(const otg::FGrassKind& K)
	{
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), K.Name);
		O->SetBoolField(TEXT("on"), K.bOn);
		O->SetStringField(TEXT("surface"), K.Masks.Num() ? K.Masks[0] : FString(TEXT("native")));
		O->SetNumberField(TEXT("density"), K.Density);
		O->SetNumberField(TEXT("scaleLo"), K.ScaleLo);
		O->SetNumberField(TEXT("scaleHi"), K.ScaleHi);
		O->SetNumberField(TEXT("drawTo"), K.DrawTo);
		O->SetNumberField(TEXT("patchM"), K.PatchM);
		O->SetNumberField(TEXT("patchLo"), K.PatchLo);
		O->SetNumberField(TEXT("patchHi"), K.PatchHi);
		O->SetBoolField(TEXT("invert"), K.bInvert);
		O->SetNumberField(TEXT("margin"), K.Margin);
		if (K.bAdded)
		{
			TArray<TSharedPtr<FJsonValue>> F;
			for (const FString& P : K.Fab) F.Add(MakeShared<FJsonValueString>(P));
			O->SetArrayField(TEXT("fab"), F);
			O->SetStringField(TEXT("label"), K.Label);
		}
		return O;
	}
}

TArray<otg::FGrassKind> otg::FGrass::LoadKinds(const FString& Dir)
{
	TArray<FGrassKind> Out = DefaultKinds();
	FString Text;
	TSharedPtr<FJsonObject> J;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Dir, TEXT("grass.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J)
		|| !J.IsValid() || !J->TryGetArrayField(TEXT("kinds"), List))
		return Out;
	for (const TSharedPtr<FJsonValue>& V : *List)
	{
		const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
		FString Name;
		if (!O.IsValid() || !O->TryGetStringField(TEXT("name"), Name)) continue;
		FGrassKind* K = Out.FindByPredicate([&](const FGrassKind& X) { return X.Name == Name; });
		if (!K)
		{
			// (a Megascans plant added in the designer)
			const TArray<TSharedPtr<FJsonValue>>* Fab = nullptr;
			if (!Name.StartsWith(TEXT("fab_")) || !O->TryGetArrayField(TEXT("fab"), Fab) || !Fab->Num()) continue;
			FGrassKind A = PlantKind((*Fab)[0]->AsString());
			A.Name = Name;
			O->TryGetStringField(TEXT("label"), A.Label);
			K = &Out.Add_GetRef(A);
		}
		if (!K->FarOf.IsEmpty()) continue;   // (follows its near kind)
		O->TryGetBoolField(TEXT("on"), K->bOn);
		FString Surface;
		// (the rough stays one even colour: only kinds in its colours grow on it)
		if (O->TryGetStringField(TEXT("surface"), Surface) && (Surface == TEXT("native") || (Surface == TEXT("rough") && K->bEven))) K->Masks = { Surface };
		auto Num = [&](const TCHAR* F, float& Val, float Lo, float Hi) { double D; if (O->TryGetNumberField(F, D) && FMath::IsFinite(D)) Val = FMath::Clamp(float(D), Lo, Hi); };
		const float From = K->DrawTo > 0 ? K->DrawFrom / K->DrawTo : 0.7f;
		Num(TEXT("density"), K->Density, 1, 20000);
		Num(TEXT("scaleLo"), K->ScaleLo, 0.05f, 5);
		Num(TEXT("scaleHi"), K->ScaleHi, 0.05f, 5);
		// (a distance saved as an old default - saved with the rest, not chosen - becomes the new one:
		// the fescue's 450 m drew it as cream-yellow drifts far off)
		const float DefTo = K->DrawTo;
		Num(TEXT("drawTo"), K->DrawTo, 10, 800);
		if (Name == TEXT("fescue") && FMath::IsNearlyEqual(K->DrawTo, 450.f)) K->DrawTo = DefTo;
		K->DrawFrom = K->DrawTo * From;
		Num(TEXT("patchM"), K->PatchM, 0, 200);
		Num(TEXT("patchLo"), K->PatchLo, 0, 1);
		Num(TEXT("patchHi"), K->PatchHi, 0, 1);
		Num(TEXT("margin"), K->Margin, 0, 5);
		O->TryGetBoolField(TEXT("invert"), K->bInvert);
		if (K->ScaleHi < K->ScaleLo) Swap(K->ScaleLo, K->ScaleHi);
		if (K->PatchM > 0 && K->PatchHi < K->PatchLo + 0.01f) K->PatchHi = K->PatchLo + 0.01f;
	}
	Derive(Out);
	return Out;
}

bool otg::FGrass::SaveKinds(const FString& Dir, const TArray<FGrassKind>& Kinds)
{
	const TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("version"), 1);
	TArray<TSharedPtr<FJsonValue>> List;
	for (const FGrassKind& K : Kinds) if (K.FarOf.IsEmpty()) List.Add(MakeShared<FJsonValueObject>(KindJson(K)));
	J->SetArrayField(TEXT("kinds"), List);
	FString Text;
	FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Text));
	return FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Dir, TEXT("grass.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
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
	for (int32 b = 0; b < Per; b++)
		for (int32 a = 0; a < Per; a++)
		{
			const double X = CX * Cell * 100.0 + (a + 0.5 + (R.FRand() - 0.5) * K.Jitter) * S;
			const double Y = CY * Cell * 100.0 + (b + 0.5 + (R.FRand() - 0.5) * K.Jitter) * S;
			const float Keep = R.FRand();
			if (X < 0 || Y < 0 || X > Land || Y > Land) continue;
			float W = 0;
			for (const FString& M : K.Masks) W = FMath::Max(W, Mask(M, X, Y));
			float P = FMath::Clamp((W - 0.33f) * 12.f, 0.f, 1.f);
			if (P <= 0) continue;
			P *= FMath::Clamp((0.45f - Mask(TEXT("water"), X, Y)) * 10.f, 0.f, 1.f);
			P *= FMath::Clamp((0.85f - Mask(TEXT("path"), X, Y)) * 2.f, 0.f, 1.f);
			// (and never on a mown surface, though the rough's mask reaches into the fringes: tufts
			// up to a few cm over its outline - the material cuts their blades off exactly on it)
			const float Mown = FMath::Max(FMath::Max(Mask(TEXT("fairway"), X, Y), Mask(TEXT("tee"), X, Y)), FMath::Max(Mask(TEXT("fringe"), X, Y), Mask(TEXT("green"), X, Y)));
			// (the masks: 0.5 on the outline, 1/6 a metre in or out - a wide clump kept K.Margin inside)
			P *= 1.f - FMath::Clamp((Mown - 0.52f + K.Margin / 6.f) * 12.f, 0.f, 1.f);
			if (K.Margin > 0) P *= FMath::Clamp((W - 0.5f - K.Margin / 6.f) * 12.f + 1.f, 0.f, 1.f);
			if (K.PatchM > 0)
			{
				// (value noise bunches round the middle: spread out to the rock texture's range)
				const double N = FMath::Clamp(0.5 + (Noise(X / 100.0, Y / 100.0, K.PatchM, K.Name.StartsWith(TEXT("native")) ? 77 : int32(GetTypeHash(K.Name.Replace(TEXT("_far"), TEXT(""))) % 997) + 31) - 0.5) * 1.8, 0.0, 1.0);
				const double L = 0.27 + 0.19 * N;
				float F = FMath::SmoothStep(K.PatchLo, K.PatchHi, float(L));
				P *= K.bInvert ? 1.f - F : F;
			}
			if (Keep >= P) continue;
			FVector Nrm;
			const double Z = GroundZ(X, Y, &Nrm);
			FQuat Tilt = FQuat::FindBetweenNormals(FVector::UpVector, Nrm);
			const FQuat Turn(FVector::UpVector, R.FRand() * 2 * PI);
			float Sc = FMath::Lerp(K.ScaleLo, K.ScaleHi, R.FRand());
			if (K.Masks.Contains(TEXT("native")))
			{
				// (left to grow: taller here, lower there over a few metres, and each clump leaning its own way)
				const float Vigor = FMath::SmoothStep(0.2f, 0.8f, float(Noise(X / 100.0, Y / 100.0, 7.0, 501)));
				Sc *= K.Fab.Num() ? FMath::Lerp(0.8f, 1.2f, Vigor) : FMath::Lerp(0.55f, 1.35f, Vigor);   // (the Megascans clumps vary in size already)
				const FVector Lean = FVector(R.FRandRange(-1, 1), R.FRandRange(-1, 1), 0).GetSafeNormal();
				Tilt = FQuat(Lean ^ FVector::UpVector, FMath::DegreesToRadians(R.FRandRange(0.f, 10.f))) * Tilt;
			}
			Out[NV > 1 ? R.RandRange(0, NV - 1) : 0].Add(FTransform(Tilt * Turn, FVector(X, Y, Z) + Data->LevelOrigin, FVector(Sc)));
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
			if (Mats[Job.Key.Kind][v]) ISM->SetMaterial(0, Mats[Job.Key.Kind][v]);
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
