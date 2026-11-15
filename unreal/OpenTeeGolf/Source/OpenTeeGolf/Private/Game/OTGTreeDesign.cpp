// See OTGTreeDesign.h.

#include "Game/OTGTreeDesign.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	const TCHAR* File = TEXT("trees_design.json");
}

const TArray<FString>& FOTGTreeDesign::Models()
{
	static const TArray<FString> M = { TEXT("auto"), TEXT("beech"), TEXT("hornbeam"), TEXT("spruce"), TEXT("pine") };
	return M;
}

FString FOTGTreeDesign::ModelLabel(const FString& Model)
{
	if (Model == TEXT("beech")) return TEXT("European beech");
	if (Model == TEXT("hornbeam")) return TEXT("Hornbeam");
	if (Model == TEXT("spruce")) return TEXT("Norway spruce");
	if (Model == TEXT("pine")) return TEXT("Baltic pine");
	return TEXT("The game's choice");
}

FString FOTGTreeDesign::SpeciesLabel(const FString& Kind)
{
	// (public/js/treeplace.js TREE_SPECIES)
	static const TMap<FString, FString> L = {
		{ TEXT("oak"), TEXT("Oak") }, { TEXT("maple"), TEXT("Maple") }, { TEXT("redmaple"), TEXT("Red maple") }, { TEXT("tuliptree"), TEXT("Tulip tree") },
		{ TEXT("beech"), TEXT("American beech") }, { TEXT("elm"), TEXT("American elm") }, { TEXT("dogwood"), TEXT("Flowering dogwood") },
		{ TEXT("crapemyrtle"), TEXT("Crape myrtle") }, { TEXT("holly"), TEXT("American holly") }, { TEXT("ash"), TEXT("Ash") }, { TEXT("birch"), TEXT("Birch") },
		{ TEXT("aspen"), TEXT("Aspen") }, { TEXT("poplar"), TEXT("Lombardy poplar") }, { TEXT("willow"), TEXT("Weeping willow") }, { TEXT("pine"), TEXT("Pine") },
		{ TEXT("loblolly"), TEXT("Loblolly pine") }, { TEXT("redcedar"), TEXT("Eastern redcedar") }, { TEXT("douglasfir"), TEXT("Douglas-fir") },
		{ TEXT("ponderosa"), TEXT("Ponderosa pine") }, { TEXT("spruce"), TEXT("Spruce") }, { TEXT("hemlock"), TEXT("Eastern hemlock") }, { TEXT("juniper"), TEXT("Juniper") },
		{ TEXT("palm"), TEXT("Palm") }, { TEXT("liveoak"), TEXT("Live oak") }, { TEXT("magnolia"), TEXT("Southern magnolia") }, { TEXT("sycamore"), TEXT("Sycamore") },
		{ TEXT("cypress"), TEXT("Monterey cypress") }, { TEXT("baldcypress"), TEXT("Bald cypress") }, { TEXT("olive"), TEXT("Olive") }, { TEXT("mesquite"), TEXT("Mesquite") },
		{ TEXT("paloverde"), TEXT("Palo verde") }, { TEXT("saguaro"), TEXT("Saguaro cactus") },
	};
	if (const FString* S = L.Find(Kind)) return *S;
	FString S = Kind.IsEmpty() ? FString(TEXT("Other trees")) : Kind;
	S[0] = FChar::ToUpper(S[0]);
	return S;
}

FOTGTreeDesign FOTGTreeDesign::Load(const FString& Dir)
{
	FOTGTreeDesign D;
	FString Text;
	TSharedPtr<FJsonObject> J;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Dir, File)) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) || !J.IsValid()) return D;
	auto Num = [](const TSharedPtr<FJsonObject>& O, const TCHAR* F, float& V, float Lo, float Hi) { double X; if (O->TryGetNumberField(F, X) && FMath::IsFinite(X)) V = FMath::Clamp(float(X), Lo, Hi); };
	Num(J, TEXT("size"), D.AllSize, 0.3f, 2.5f);
	Num(J, TEXT("density"), D.AllDensity, 0.f, 3.f);
	const TSharedPtr<FJsonObject>* Sp = nullptr;
	if (J->TryGetObjectField(TEXT("species"), Sp))
		for (const auto& KV : (*Sp)->Values)
		{
			const TSharedPtr<FJsonObject> O = KV.Value->AsObject();
			if (!O.IsValid()) continue;
			FOTGSpeciesLook L;
			FString M;
			if (O->TryGetStringField(TEXT("model"), M) && Models().Contains(M)) L.Model = M;
			Num(O, TEXT("size"), L.Size, 0.3f, 2.5f);
			Num(O, TEXT("density"), L.Density, 0.f, 3.f);
			D.Species.Add(FString(KV.Key), L);
		}
	const TSharedPtr<FJsonObject>* R = nullptr;
	if (J->TryGetObjectField(TEXT("rocks"), R))
	{
		(*R)->TryGetBoolField(TEXT("on"), D.bRocks);
		Num(*R, TEXT("size"), D.RockSize, 0.3f, 3.f);
		Num(*R, TEXT("density"), D.RockDensity, 0.f, 1.f);
	}
	return D;
}

bool FOTGTreeDesign::Save(const FString& Dir) const
{
	const TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("version"), 1);
	J->SetNumberField(TEXT("size"), AllSize);
	J->SetNumberField(TEXT("density"), AllDensity);
	const TSharedRef<FJsonObject> Sp = MakeShared<FJsonObject>();
	for (const auto& KV : Species)
	{
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("model"), KV.Value.Model);
		O->SetNumberField(TEXT("size"), KV.Value.Size);
		O->SetNumberField(TEXT("density"), KV.Value.Density);
		Sp->SetObjectField(KV.Key, O);
	}
	J->SetObjectField(TEXT("species"), Sp);
	const TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("on"), bRocks);
	R->SetNumberField(TEXT("size"), RockSize);
	R->SetNumberField(TEXT("density"), RockDensity);
	J->SetObjectField(TEXT("rocks"), R);
	FString Text;
	FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Text));
	return FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Dir, File), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FOTGTreeDesign::IsDefault() const
{
	if (AllSize != 1.f || AllDensity != 1.f || !bRocks || RockSize != 1.f || RockDensity != 1.f) return false;
	for (const auto& KV : Species) if (KV.Value.Model != TEXT("auto") || KV.Value.Size != 1.f || KV.Value.Density != 1.f) return false;
	return true;
}

FOTGSpeciesLook FOTGTreeDesign::For(const FString& Kind) const
{
	const FOTGSpeciesLook* L = Species.Find(Kind);
	return L ? *L : FOTGSpeciesLook();
}

double FOTGTreeDesign::Hash01(double X, double Z, int32 Salt)
{
	// (on the position to the decimetre: the drawn tree and the ball's tree agree)
	uint32 H = 2166136261u;
	for (int32 V : { int32(FMath::RoundToDouble(X * 10)), int32(FMath::RoundToDouble(Z * 10)), Salt }) { H ^= uint32(V); H *= 16777619u; H ^= H >> 15; H *= 0x2c1b3c6du; H ^= H >> 12; }
	return (H & 0xffffff) / double(0x1000000);
}

int32 FOTGTreeDesign::Copies(const FString& Kind, double X, double Z) const
{
	const float D = FMath::Max(0.f, AllDensity * For(Kind).Density);
	const int32 Whole = FMath::FloorToInt32(D);
	return Whole + (Hash01(X, Z, 17) < D - Whole ? 1 : 0);
}

FVector2D FOTGTreeDesign::CopyAt(double X, double Z, int32 I, double R)
{
	const double A = Hash01(X, Z, 31 + I) * 2 * PI;
	const double D = FMath::Max(3.0, R) * (1.1 + 0.9 * Hash01(X, Z, 57 + I));
	return FVector2D(X + FMath::Cos(A) * D, Z + FMath::Sin(A) * D);
}

bool FOTGTreeDesign::KeepRock(double X, double Z) const
{
	return bRocks && Hash01(X, Z, 91) < RockDensity;
}
