// See OTGGrass.h: the kinds of grass - the game's own, the Megascans plants', derived far kinds -
// their colours, and loading and saving a course's (grass.json).

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
		// (off unless a course's designer turns it on, as the fairways: a green is the ground's smooth
		// turf texture - its 5 mm blades read as dark specks standing up over it)
		Mown(TEXT("green"), TEXT("Greens (5 mm)"), { TEXT("green") }, 2500, 14, 24).bOn = false;
		Mown(TEXT("fringe"), TEXT("Collars (1 cm)"), { TEXT("fringe") }, 2500, 18, 30);
		// (off unless a course's designer turns it on: fairways and tees are the ground's turf texture -
		// a mown fairway reads as a smooth carpet, its separate blades as stubble)
		Mown(TEXT("fairway"), TEXT("Fairways and tees (1.1 cm)"), { TEXT("fairway"), TEXT("tee") }, 2500, 24, 38).bOn = false;
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
		// the rough far off, 80 to 400 m: fewer, wider blades in bigger patches (make_turf.py's
		// rough_far), so the rough keeps a grass texture past where its near blades go - it ended in a
		// bare band at 100 m, and at 220 m the rough round a long hole's green was the ground's flat,
		// paler texture. Off with the other far kinds (OTG.Grass.Far 0)
		{
			FGrassKind& G = Add(TEXT("rough_far"), TEXT("Rough grass (far off)"), { TEXT("rough") }, 200, 330, 400, 1.0f, 1.1f, 1.45f);
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
		// (the wildflowers only near: out to 70-100 m their patches read from afar as yellow and white
		// blotches across the tall grass)
		Add(TEXT("flowers_yellow"), TEXT("Yellow wildflowers"), { TEXT("native") }, 70, 30, 45, 1.0f, 0.85f, 1.2f, 9, 0.43f, 0.48f, false);
		Add(TEXT("flowers_white"), TEXT("White wildflowers"), { TEXT("native") }, 70, 30, 45, 1.0f, 0.85f, 1.2f, 11, 0.44f, 0.49f, false);
		Add(TEXT("flowers_purple"), TEXT("Purple wildflowers"), { TEXT("native") }, 60, 30, 45, 1.0f, 0.85f, 1.2f, 7, 0.44f, 0.5f, false);
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

	// (a biome's kind under its own name: "fescue@2" saved as "fescue")
	otg::FGrassKind BaseKind(const otg::FGrassKind& K)
	{
		otg::FGrassKind B = K;
		B.Name = K.BaseName();
		if (!B.FarOf.IsEmpty()) { int32 At; if (B.FarOf.FindChar(TEXT('@'), At)) B.FarOf = B.FarOf.Left(At); }
		B.Biome = -1;
		return B;
	}
}

TSharedRef<FJsonObject> otg::FGrass::KindToJson(const FGrassKind& K)
{
	return KindJson(BaseKind(K));
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
	for (const TSharedPtr<FJsonValue>& V : *List) ApplyKindJson(Out, V.IsValid() ? V->AsObject() : nullptr, Version);
	Derive(Out);
	return Out;
}

bool otg::FGrass::ApplyKindJson(TArray<FGrassKind>& Out, const TSharedPtr<FJsonObject>& O, double Version)
{
	{
		FString Name;
		if (!O.IsValid() || !O->TryGetStringField(TEXT("name"), Name)) return false;
		FGrassKind* K = Out.FindByPredicate([&](const FGrassKind& X) { return X.Name == Name; });
		if (!K)
		{
			// (a Megascans plant added in the designer)
			const TArray<TSharedPtr<FJsonValue>>* Fab = nullptr;
			if (!Name.StartsWith(TEXT("fab_")) || !O->TryGetArrayField(TEXT("fab"), Fab) || !Fab->Num()) return false;
			FGrassKind A = PlantKind((*Fab)[0]->AsString());
			A.Name = Name;
			O->TryGetStringField(TEXT("label"), A.Label);
			K = &Out.Add_GetRef(A);
		}
		if (!K->FarOf.IsEmpty()) return true;   // (follows its near kind)
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
		if (Name.StartsWith(TEXT("flowers_")) && FMath::IsNearlyEqual(K->DrawTo, 100.f)) K->DrawTo = DefTo;   // (blotches far off)
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
	return true;
}

bool otg::FGrass::SaveKinds(const FString& Dir, const TArray<FGrassKind>& Kinds)
{
	const TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("version"), 2);
	TArray<TSharedPtr<FJsonValue>> List;
	for (const FGrassKind& K : Kinds) if (K.FarOf.IsEmpty() && K.Biome <= 0) List.Add(MakeShared<FJsonValueObject>(KindJson(BaseKind(K))));
	J->SetArrayField(TEXT("kinds"), List);
	FString Text;
	FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Text));
	return FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Dir, TEXT("grass.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
