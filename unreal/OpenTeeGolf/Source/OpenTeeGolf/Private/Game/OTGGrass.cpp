// See OTGGrass.h.

#include "OTGGrass.h"

#include "OTGCourseData.h"
#include "OTGKiteDemo.h"

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
		// the mown cuts, each its own blades at its own height (make_turf.py TURF_CUTS): the greens
		// (4-5 mm), the collars round them (1 cm), the fairways and tees (1.5 cm), and the first cut
		// beside them (3 cm, a band 1.5 m wide). Dense and drawn near only: further off the ground,
		// in the same colours and stripes, carries them on. Each grows on its own surface and a
		// little past it; its material folds away any blade not in its cut, so the lines between the
		// cuts are as crisp as the masks
		auto Mown = [&](const TCHAR* Name, const TCHAR* Label, TArray<FString> Masks, float Density, float From, float To) -> FGrassKind& {
			FGrassKind& G = Add(Name, Label, MoveTemp(Masks), Density, From, To, 1.0f, 0.92f, 1.08f);
			G.bMown = true;
			return G;
		};
		// (2500 a 100 m2: one 20 cm square patch to each square of a 20 cm grid - they tile)
		Mown(TEXT("green"), TEXT("Greens (5 mm)"), { TEXT("green") }, 2500, 14, 24);
		Mown(TEXT("fringe"), TEXT("Collars (1 cm)"), { TEXT("fringe") }, 2500, 18, 30);
		Mown(TEXT("fairway"), TEXT("Fairways and tees (1.1 cm)"), { TEXT("fairway"), TEXT("tee") }, 2500, 24, 38);
		{
			FGrassKind& G = Add(TEXT("cut1"), TEXT("First cut (3 cm)"), { TEXT("rough") }, 2500, 40, 60, 1.0f, 0.9f, 1.1f);
			G.BandM = 1.5f;
		}
		Add(TEXT("rough"), TEXT("Rough grass"), { TEXT("rough") }, 2400, 70, 100, 1.0f, 0.85f, 1.2f).bEven = true;
		// (Greensward's mats grow only in the deep rough: the rough is our own blades, whose material
		// folds them away exactly on its outline - Greensward's 50 cm mats, cut down to 12.5 cm pieces
		// at an edge, left a staircase round the tees and bunkers, and read darker or lighter than the
		// mown cuts beside them by the angle they were seen from)
		const bool bGs = !FParse::Param(FCommandLine::Get(), TEXT("oldgrass"));
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
		Add(TEXT("native_short"), TEXT("Short meadow grass"), { TEXT("native") }, 520, 110, 160, 1.0f, 0.75f, 1.25f, 16, 0.31f, 0.39f, true).bOn = !bGs;
		// the deep rough (Greensward's, as DZ Grasses' showcase has it): its rough mats over all the
		// tall grass's ground, 15 to 30 cm, so the rough meets it in a clear step up in height - the
		// plants above grow through it. Its own greyer green with paler tips, varying across the
		// ground (variety belongs in the deep rough); in place of our short meadow grass
		if (bGs)
		{
			FGrassKind& G = Add(TEXT("deep_rough"), TEXT("Deep rough (Greensward)"), { TEXT("native") }, 400, 65, 90, 1.0f, 0.9f, 1.15f);
			// (out to 90 m: as far as its step up from the rough shows; the ground and the tall plants
			// carry it on - 140 m cost a millisecond on a course with a lot of it)
			G.GsMats = { TEXT("Rough_20cm_Dense_Dry10"), TEXT("Rough_20cm_Medium_Dry00"), TEXT("Rough_30cm_Medium_Dry10"), TEXT("Rough_15cm_Dense_Dry10") };
			G.TileCm = 50;
			G.CellM = 32;
		}
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
		// heather in its own patches through the tall grass, thinning out at their edges (KiteDemo's: knee
		// high, its own colour; none where the pack isn't in the game). Off unless the grass designer
		// turns it on: its pink and purple drifts under the trees looked out of place on most courses
		{
			FGrassKind& G = Add(TEXT("heather"), TEXT("Heather"), { TEXT("native") }, 180, 105, 150, 1.0f, 0.8f, 1.25f, 18, 0.34f, 0.46f, false);
			G.Fab = { TEXT("kite_heather") };
			G.Margin = 0.6f;
			G.bOn = false;
		}
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
	if (const otgkite::FPlant* P = otgkite::Plant(Folder))
	{
		// (a KiteDemo plant: its own numbers - they're real sizes, and some are thousands of triangles)
		G.Label = FString(P->Label) + TEXT(" (KiteDemo)");
		G.Masks = { TEXT("native") };
		G.Density = P->Density; G.DrawTo = P->DrawTo; G.DrawFrom = P->DrawTo * 0.7f;
		G.ScaleLo = P->ScaleLo; G.ScaleHi = P->ScaleHi;
		G.PatchM = P->PatchM; G.PatchLo = P->PatchLo; G.PatchHi = P->PatchHi;
		G.Margin = 0.6f;
		G.Fab = { Folder };
		G.bAdded = true;
		return G;
	}
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
		F.Hue = P->Hue; F.Sat = P->Sat; F.Bright = P->Bright;
	}
}

void otg::FGrassKind::ClampColour()
{
	Hue = FMath::IsFinite(Hue) ? FMath::Clamp(Hue, -HueMax, HueMax) : 0.f;
	Sat = FMath::IsFinite(Sat) ? FMath::Clamp(Sat, SatLo, SatHi) : 1.f;
	Bright = FMath::IsFinite(Bright) ? FMath::Clamp(Bright, BrightLo, BrightHi) : 1.f;
}

FLinearColor otg::FGrassKind::TintFor(const FLinearColor& Ref) const
{
	if (IsOwnColour()) return FLinearColor::White;
	// (the reference colour turned in HSV, then what each channel must be multiplied by to get there:
	// kept within reason, so a channel near nothing isn't blown up)
	FLinearColor H = Ref.LinearRGBToHSV();
	H.R = FMath::Fmod(H.R + Hue + 360.f, 360.f);
	H.G = FMath::Clamp(H.G * Sat, 0.f, 1.f);
	H.B *= Bright;
	const FLinearColor To = H.HSVToLinearRGB();
	auto Ch = [](float T, float R) { return FMath::Clamp(T / FMath::Max(R, 0.002f), 0.15f, 4.f); };
	return FLinearColor(Ch(To.R, Ref.R), Ch(To.G, Ref.G), Ch(To.B, Ref.B), 1.f);
}

void otg::FGrass::SetColours(const TArray<FGrassKind>& Kinds)
{
	for (FGrassKind& K : KindList)
		if (const FGrassKind* N = Kinds.FindByPredicate([&](const FGrassKind& X) { return X.Name == K.Name; }))
		{
			K.Hue = N->Hue; K.Sat = N->Sat; K.Bright = N->Bright;
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
		if (!K.IsOwnColour())
		{
			O->SetNumberField(TEXT("hue"), K.Hue);
			O->SetNumberField(TEXT("saturation"), K.Sat);
			O->SetNumberField(TEXT("brightness"), K.Bright);
		}
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
	// (version 1 saved every kind's on/off, chosen or not, when heather grew by default: there it stays
	// off - it grows only where the designer has turned it on since)
	double Version = 1;
	J->TryGetNumberField(TEXT("version"), Version);
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
		if (Version >= 2 || Name != TEXT("heather")) O->TryGetBoolField(TEXT("on"), K->bOn);
		FString Surface;
		// (the rough stays one even colour: only kinds in its colours grow on it)
		if (O->TryGetStringField(TEXT("surface"), Surface) && (Surface == TEXT("native") || (Surface == TEXT("rough") && K->bEven))) K->Masks = { Surface };
		auto Num = [&](const TCHAR* F, float& Val, float Lo, float Hi) { double D; if (O->TryGetNumberField(F, D) && FMath::IsFinite(D)) Val = FMath::Clamp(float(D), Lo, Hi); };
		const float From = K->DrawTo > 0 ? K->DrawFrom / K->DrawTo : 0.7f;
		// (a tiled Greensward kind covers its ground: how many there are is the tile's, not saved - an
		// older grass.json's 2400 for the rough meant our 30 cm patches)
		if (!K->IsGreensward()) Num(TEXT("density"), K->Density, 1, 20000);
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
		Num(TEXT("hue"), K->Hue, -FGrassKind::HueMax, FGrassKind::HueMax);
		Num(TEXT("saturation"), K->Sat, FGrassKind::SatLo, FGrassKind::SatHi);
		Num(TEXT("brightness"), K->Bright, FGrassKind::BrightLo, FGrassKind::BrightHi);
		if (K->ScaleHi < K->ScaleLo) Swap(K->ScaleLo, K->ScaleHi);
		if (K->PatchM > 0 && K->PatchHi < K->PatchLo + 0.01f) K->PatchHi = K->PatchLo + 0.01f;
	}
	Derive(Out);
	return Out;
}

bool otg::FGrass::SaveKinds(const FString& Dir, const TArray<FGrassKind>& Kinds)
{
	const TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("version"), 2);
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

// (Place's odds of a patch at a spot, before the random keep: the mask past its edge, not in the
// water or on a path, a road or a mown surface, in its meadow patch)
float otg::FGrass::Chance(const FGrassKind& K, double X, double Y) const
{
	float W = 0;
	for (const FString& M : K.Masks) W = FMath::Max(W, Mask(M, X, Y));
	// (Greensward's mats: on the outline itself, 25 cm either way - our blades reach past it and their
	// material folds them away there)
	float P = K.IsGreensward() ? FMath::Clamp((W - 0.5f) * 24.f + 0.5f, 0.f, 1.f) : FMath::Clamp((W - 0.33f) * 12.f, 0.f, 1.f);
	if (P <= 0) return 0.f;
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
		const double N = FMath::Clamp(0.5 + (Noise(X / 100.0, Y / 100.0, K.PatchM, K.Name.StartsWith(TEXT("native")) ? 77 : int32(GetTypeHash(K.Name.Replace(TEXT("_far"), TEXT(""))) % 997) + 31) - 0.5) * 1.8, 0.0, 1.0);
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
