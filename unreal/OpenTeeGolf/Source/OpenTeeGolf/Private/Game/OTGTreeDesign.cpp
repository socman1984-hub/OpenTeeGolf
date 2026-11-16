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
	constexpr double Near = 0.15;   // m: an edit's tree found again where it stands
	const TCHAR* SpeciesKeys[] = { TEXT("oak"), TEXT("maple"), TEXT("redmaple"), TEXT("tuliptree"), TEXT("beech"), TEXT("elm"), TEXT("dogwood"), TEXT("crapemyrtle"),
		TEXT("holly"), TEXT("ash"), TEXT("birch"), TEXT("aspen"), TEXT("poplar"), TEXT("willow"), TEXT("pine"), TEXT("loblolly"), TEXT("redcedar"), TEXT("douglasfir"),
		TEXT("ponderosa"), TEXT("spruce"), TEXT("hemlock"), TEXT("juniper"), TEXT("palm"), TEXT("liveoak"), TEXT("magnolia"), TEXT("sycamore"), TEXT("cypress"),
		TEXT("baldcypress"), TEXT("olive"), TEXT("mesquite"), TEXT("paloverde"), TEXT("saguaro") };
}

FLinearColor FOTGTreeEdit::LeafTint() const
{
	if (!HasColour()) return FLinearColor(0, 0, 0, 0);
	// (a leaf green turned in HSV; the multiplier that takes it there, kept within reason)
	const FLinearColor Ref(0.07f, 0.11f, 0.03f);
	FLinearColor H = Ref.LinearRGBToHSV();
	H.R = FMath::Fmod(H.R + FMath::Clamp(Hue, HueLo, HueHi) + 360.f, 360.f);
	H.B *= FMath::Clamp(Bright, BrightLo, BrightHi);
	const FLinearColor To = H.HSVToLinearRGB();
	auto Ch = [](float T, float R) { return FMath::Clamp(T / R, 0.15f, 4.f) - 1.f; };
	return FLinearColor(Ch(To.R, Ref.R), Ch(To.G, Ref.G), Ch(To.B, Ref.B), 0.f);
}

const FOTGTreeEdit* FOTGTreeDesign::TreeEditAt(double X, double Z) const
{
	return Trees.FindByPredicate([&](const FOTGTreeEdit& E) { return FMath::Abs(E.X - X) < Near && FMath::Abs(E.Z - Z) < Near; });
}

FOTGTreeEdit& FOTGTreeDesign::TreeEdit(double X, double Z)
{
	if (FOTGTreeEdit* E = const_cast<FOTGTreeEdit*>(TreeEditAt(X, Z))) return *E;
	FOTGTreeEdit& E = Trees.AddDefaulted_GetRef();
	E.X = X; E.Z = Z;
	return E;
}

const FOTGRockEdit* FOTGTreeDesign::RockEditAt(double X, double Z) const
{
	return Rocks.FindByPredicate([&](const FOTGRockEdit& E) { return FMath::Abs(E.X - X) < Near && FMath::Abs(E.Z - Z) < Near; });
}

FOTGRockEdit& FOTGTreeDesign::RockEdit(double X, double Z)
{
	if (FOTGRockEdit* E = const_cast<FOTGRockEdit*>(RockEditAt(X, Z))) return *E;
	FOTGRockEdit& E = Rocks.AddDefaulted_GetRef();
	E.X = X; E.Z = Z;
	return E;
}

void FOTGTreeDesign::ClearTreeEdit(double X, double Z)
{
	Trees.RemoveAll([&](const FOTGTreeEdit& E) { return FMath::Abs(E.X - X) < Near && FMath::Abs(E.Z - Z) < Near; });
}

void FOTGTreeDesign::ClearRockEdit(double X, double Z)
{
	Rocks.RemoveAll([&](const FOTGRockEdit& E) { return FMath::Abs(E.X - X) < Near && FMath::Abs(E.Z - Z) < Near; });
}

const TArray<FString>& FOTGTreeDesign::AllSpecies()
{
	static const TArray<FString> A = [] { TArray<FString> Out; for (const TCHAR* K : SpeciesKeys) Out.Add(K); return Out; }();
	return A;
}

bool FOTGTreeDesign::IsConifer(const FString& Kind)
{
	static const TSet<FString> C = { TEXT("pine"), TEXT("loblolly"), TEXT("redcedar"), TEXT("douglasfir"), TEXT("ponderosa"), TEXT("spruce"), TEXT("cypress"), TEXT("baldcypress"), TEXT("hemlock"), TEXT("juniper"), TEXT("holly") };
	return C.Contains(Kind);
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
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (J->TryGetArrayField(TEXT("trees"), List))
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			double X, Z;
			if (!O.IsValid() || !O->TryGetNumberField(TEXT("x"), X) || !O->TryGetNumberField(TEXT("z"), Z) || !FMath::IsFinite(X) || !FMath::IsFinite(Z)) continue;
			FOTGTreeEdit E;
			E.X = X; E.Z = Z;
			O->TryGetBoolField(TEXT("added"), E.bAdded);
			O->TryGetBoolField(TEXT("removed"), E.bRemoved);
			O->TryGetStringField(TEXT("kind"), E.Kind);
			if (O->TryGetStringField(TEXT("model"), E.Model) && !Models().Contains(E.Model)) E.Model.Empty();
			Num(O, TEXT("size"), E.Size, FOTGTreeEdit::SizeLo, FOTGTreeEdit::SizeHi);
			Num(O, TEXT("height"), E.Height, FOTGTreeEdit::HeightLo, FOTGTreeEdit::HeightHi);
			Num(O, TEXT("hue"), E.Hue, FOTGTreeEdit::HueLo, FOTGTreeEdit::HueHi);
			Num(O, TEXT("brightness"), E.Bright, FOTGTreeEdit::BrightLo, FOTGTreeEdit::BrightHi);
			if (E.bAdded && E.Kind.IsEmpty()) continue;
			D.Trees.Add(E);
		}
	if (J->TryGetArrayField(TEXT("rockEdits"), List))
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			double X, Z;
			if (!O.IsValid() || !O->TryGetNumberField(TEXT("x"), X) || !O->TryGetNumberField(TEXT("z"), Z) || !FMath::IsFinite(X) || !FMath::IsFinite(Z)) continue;
			FOTGRockEdit E;
			E.X = X; E.Z = Z;
			O->TryGetBoolField(TEXT("added"), E.bAdded);
			O->TryGetBoolField(TEXT("removed"), E.bRemoved);
			Num(O, TEXT("size"), E.Size, FOTGRockEdit::SizeLo, FOTGRockEdit::SizeHi);
			D.Rocks.Add(E);
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
	if (Trees.Num())
	{
		TArray<TSharedPtr<FJsonValue>> List;
		for (const FOTGTreeEdit& E : Trees)
		{
			const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("x"), E.X);
			O->SetNumberField(TEXT("z"), E.Z);
			if (E.bAdded) O->SetBoolField(TEXT("added"), true);
			if (E.bRemoved) O->SetBoolField(TEXT("removed"), true);
			if (!E.Kind.IsEmpty()) O->SetStringField(TEXT("kind"), E.Kind);
			if (!E.Model.IsEmpty()) O->SetStringField(TEXT("model"), E.Model);
			O->SetNumberField(TEXT("size"), E.Size);
			if (E.bAdded) O->SetNumberField(TEXT("height"), E.Height);
			if (E.HasColour()) { O->SetNumberField(TEXT("hue"), E.Hue); O->SetNumberField(TEXT("brightness"), E.Bright); }
			List.Add(MakeShared<FJsonValueObject>(O));
		}
		J->SetArrayField(TEXT("trees"), List);
	}
	if (Rocks.Num())
	{
		TArray<TSharedPtr<FJsonValue>> List;
		for (const FOTGRockEdit& E : Rocks)
		{
			const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("x"), E.X);
			O->SetNumberField(TEXT("z"), E.Z);
			if (E.bAdded) O->SetBoolField(TEXT("added"), true);
			if (E.bRemoved) O->SetBoolField(TEXT("removed"), true);
			O->SetNumberField(TEXT("size"), E.Size);
			List.Add(MakeShared<FJsonValueObject>(O));
		}
		J->SetArrayField(TEXT("rockEdits"), List);
	}
	FString Text;
	FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Text));
	return FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Dir, File), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FOTGTreeDesign::IsDefault() const
{
	if (AllSize != 1.f || AllDensity != 1.f || !bRocks || RockSize != 1.f || RockDensity != 1.f || Trees.Num() || Rocks.Num()) return false;
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
	if (const FOTGRockEdit* E = RockEditAt(X, Z)) if (E->bRemoved) return false;
	return bRocks && Hash01(X, Z, 91) < RockDensity;
}
