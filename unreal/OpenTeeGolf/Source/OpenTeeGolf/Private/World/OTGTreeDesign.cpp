// See OTGTreeDesign.h.

#include "World/OTGTreeDesign.h"

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
	static const TArray<FString> M = { TEXT("auto"), TEXT("beech"), TEXT("hornbeam"), TEXT("spruce"), TEXT("pine"), TEXT("scotspine"), TEXT("hilltree") };
	return M;
}

const TArray<FString>& FOTGTreeDesign::RockModels()
{
	static const TArray<FString> M = { FString(), TEXT("mossy"), TEXT("grey"), TEXT("river"), TEXT("field"), TEXT("plains"), TEXT("volcanic") };
	return M;
}

FString FOTGTreeDesign::RockModelLabel(const FString& Model)
{
	if (Model == TEXT("mossy")) return TEXT("Mossy boulder");
	if (Model == TEXT("grey")) return TEXT("Grey boulder");
	if (Model == TEXT("river")) return TEXT("River stone");
	if (Model == TEXT("field")) return TEXT("Field stone");
	if (Model == TEXT("plains")) return TEXT("Big weathered boulder");
	if (Model == TEXT("volcanic")) return TEXT("Dark volcanic rock");
	return TEXT("The game's stones");
}

FString FOTGTreeDesign::ModelLabel(const FString& Model)
{
	if (Model == TEXT("beech")) return TEXT("European beech");
	if (Model == TEXT("hornbeam")) return TEXT("Hornbeam");
	if (Model == TEXT("spruce")) return TEXT("Norway spruce");
	if (Model == TEXT("pine")) return TEXT("Baltic pine");
	if (Model == TEXT("scotspine")) return TEXT("Scots pine (KiteDemo)");
	if (Model == TEXT("hilltree")) return TEXT("Hill oak (KiteDemo)");
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
		if ((*R)->TryGetStringField(TEXT("model"), D.RockModel) && !RockModels().Contains(D.RockModel)) D.RockModel.Empty();
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
			if (O->TryGetStringField(TEXT("model"), E.Model) && !RockModels().Contains(E.Model)) E.Model.Empty();
			D.Rocks.Add(E);
		}
	if (J->TryGetArrayField(TEXT("forests"), List))
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Pts = nullptr;
			if (!O.IsValid() || !O->TryGetArrayField(TEXT("pts"), Pts)) continue;
			FOTGForest F;
			for (const TSharedPtr<FJsonValue>& P : *Pts)
			{
				const TArray<TSharedPtr<FJsonValue>>* XZ = nullptr;
				if (P.IsValid() && P->TryGetArray(XZ) && XZ->Num() >= 2) F.Pts.Add(FVector2D((*XZ)[0]->AsNumber(), (*XZ)[1]->AsNumber()));
			}
			if (F.Pts.Num() < 3) continue;
			O->TryGetStringField(TEXT("name"), F.Name);
			const TSharedPtr<FJsonObject>* P = nullptr;
			if (O->TryGetObjectField(TEXT("preset"), P)) FOTGForestPreset::FromJson(*P, F.Preset);
			Num(O, TEXT("amount"), F.Amount, FOTGForest::AmountLo, FOTGForest::AmountHi);
			Num(O, TEXT("size"), F.Size, FOTGForest::SizeLo, FOTGForest::SizeHi);
			O->TryGetBoolField(TEXT("fill"), F.bFill);
			D.Forests.Add(MoveTemp(F));
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
	if (!RockModel.IsEmpty()) R->SetStringField(TEXT("model"), RockModel);
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
			if (!E.Model.IsEmpty()) O->SetStringField(TEXT("model"), E.Model);
			List.Add(MakeShared<FJsonValueObject>(O));
		}
		J->SetArrayField(TEXT("rockEdits"), List);
	}
	if (Forests.Num())
	{
		TArray<TSharedPtr<FJsonValue>> List;
		for (const FOTGForest& F : Forests)
		{
			const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), F.Name);
			TArray<TSharedPtr<FJsonValue>> Pts;
			for (const FVector2D& P : F.Pts)
				Pts.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(FMath::RoundToDouble(P.X * 100) / 100), MakeShared<FJsonValueNumber>(FMath::RoundToDouble(P.Y * 100) / 100) }));
			O->SetArrayField(TEXT("pts"), Pts);
			O->SetObjectField(TEXT("preset"), F.Preset.ToJson());
			O->SetNumberField(TEXT("amount"), F.Amount);
			O->SetNumberField(TEXT("size"), F.Size);
			O->SetBoolField(TEXT("fill"), F.bFill);
			List.Add(MakeShared<FJsonValueObject>(O));
		}
		J->SetArrayField(TEXT("forests"), List);
	}
	FString Text;
	FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Text));
	return FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Dir, File), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FOTGTreeDesign::IsDefault() const
{
	if (AllSize != 1.f || AllDensity != 1.f || !bRocks || RockSize != 1.f || RockDensity != 1.f || !RockModel.IsEmpty() || Trees.Num() || Rocks.Num() || Forests.Num()) return false;
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

int32 FOTGTreeDesign::Copies(const FString& Kind, double X, double Z, float Times) const
{
	const float D = FMath::Max(0.f, AllDensity * For(Kind).Density * Times);
	const int32 Whole = FMath::FloorToInt32(D);
	return Whole + (Hash01(X, Z, 17) < D - Whole ? 1 : 0);
}

FVector2D FOTGTreeDesign::CopyAt(double X, double Z, int32 I, double R)
{
	const double A = Hash01(X, Z, 31 + I) * 2 * PI;
	const double D = FMath::Max(3.0, R) * (1.1 + 0.9 * Hash01(X, Z, 57 + I));
	return FVector2D(X + FMath::Cos(A) * D, Z + FMath::Sin(A) * D);
}

const FOTGForest* FOTGTreeDesign::ForestAt(double X, double Z) const
{
	for (int32 i = Forests.Num() - 1; i >= 0; i--) if (Forests[i].Contains(X, Z)) return &Forests[i];
	return nullptr;
}

bool FOTGForest::Contains(double X, double Z) const
{
	if (Pts.Num() < 3) return false;
	bool bIn = false;
	for (int32 i = 0, j = Pts.Num() - 1; i < Pts.Num(); j = i++)
		if ((Pts[i].Y > Z) != (Pts[j].Y > Z) && X < (Pts[j].X - Pts[i].X) * (Z - Pts[i].Y) / (Pts[j].Y - Pts[i].Y) + Pts[i].X) bIn = !bIn;
	return bIn;
}

FBox2D FOTGForest::Bounds() const
{
	FBox2D B(ForceInit);
	for (const FVector2D& P : Pts) B += P;
	return B;
}

double FOTGForest::AreaM2() const
{
	double A = 0;
	for (int32 i = 0, j = Pts.Num() - 1; i < Pts.Num(); j = i++) A += (Pts[j].X + Pts[i].X) * (Pts[j].Y - Pts[i].Y);
	return FMath::Abs(A) * 0.5;
}

const FOTGForestSpecies* FOTGForestPreset::PickAt(double X, double Z) const
{
	float Total = 0;
	for (const FOTGForestSpecies& S : Species) if (!S.Kind.IsEmpty()) Total += FMath::Max(0.f, S.Share);
	if (Total <= 0) return nullptr;
	float R = float(FOTGTreeDesign::Hash01(X, Z, 73)) * Total;
	for (const FOTGForestSpecies& S : Species)
	{
		if (S.Kind.IsEmpty() || S.Share <= 0) continue;
		R -= S.Share;
		if (R <= 0) return &S;
	}
	return Species.FindByPredicate([](const FOTGForestSpecies& S) { return !S.Kind.IsEmpty() && S.Share > 0; });
}

float FOTGForestPreset::HeightAt(double X, double Z) const
{
	return FMath::Lerp(FMath::Min(HeightLo, HeightHi), FMath::Max(HeightLo, HeightHi), float(FOTGTreeDesign::Hash01(X, Z, 79)));
}

TSharedRef<FJsonObject> FOTGForestPreset::ToJson() const
{
	const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("name"), Name);
	TArray<TSharedPtr<FJsonValue>> L;
	for (const FOTGForestSpecies& S : Species)
	{
		const TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("kind"), S.Kind);
		E->SetNumberField(TEXT("share"), S.Share);
		if (!S.Model.IsEmpty() && S.Model != TEXT("auto")) E->SetStringField(TEXT("model"), S.Model);
		L.Add(MakeShared<FJsonValueObject>(E));
	}
	O->SetArrayField(TEXT("species"), L);
	O->SetNumberField(TEXT("heightLo"), HeightLo);
	O->SetNumberField(TEXT("heightHi"), HeightHi);
	O->SetNumberField(TEXT("perHa"), PerHa);
	if (!Under.IsEmpty()) O->SetStringField(TEXT("under"), Under);
	return O;
}

bool FOTGForestPreset::FromJson(const TSharedPtr<FJsonObject>& O, FOTGForestPreset& Out)
{
	if (!O.IsValid()) return false;
	FOTGForestPreset P;
	O->TryGetStringField(TEXT("name"), P.Name);
	const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
	if (O->TryGetArrayField(TEXT("species"), L))
		for (const TSharedPtr<FJsonValue>& V : *L)
		{
			const TSharedPtr<FJsonObject> E = V.IsValid() ? V->AsObject() : nullptr;
			FOTGForestSpecies S;
			if (!E.IsValid() || !E->TryGetStringField(TEXT("kind"), S.Kind) || !FOTGTreeDesign::AllSpecies().Contains(S.Kind)) continue;
			double D;
			if (E->TryGetNumberField(TEXT("share"), D) && FMath::IsFinite(D)) S.Share = FMath::Clamp(float(D), 0.f, 10.f);
			if (E->TryGetStringField(TEXT("model"), S.Model) && !FOTGTreeDesign::Models().Contains(S.Model)) S.Model.Empty();
			P.Species.Add(S);
		}
	auto Num = [&](const TCHAR* F, float& Val, float Lo, float Hi) { double D; if (O->TryGetNumberField(F, D) && FMath::IsFinite(D)) Val = FMath::Clamp(float(D), Lo, Hi); };
	Num(TEXT("heightLo"), P.HeightLo, HeightMin, HeightMax);
	Num(TEXT("heightHi"), P.HeightHi, HeightMin, HeightMax);
	Num(TEXT("perHa"), P.PerHa, PerHaLo, PerHaHi);
	O->TryGetStringField(TEXT("under"), P.Under);
	Out = MoveTemp(P);
	return true;
}

bool FOTGTreeDesign::KeepRock(double X, double Z) const
{
	if (const FOTGRockEdit* E = RockEditAt(X, Z)) if (E->bRemoved) return false;
	return bRocks && Hash01(X, Z, 91) < RockDensity;
}
