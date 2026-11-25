// See OTGBiomes.h.

#include "World/OTGBiomes.h"

#include "Course/OTGCourseData.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGBiomes, Log, All);

namespace
{
	TSharedPtr<FJsonObject> ReadObject(const FString& File)
	{
		FString Text;
		TSharedPtr<FJsonObject> J;
		if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J)) return nullptr;
		return J;
	}

	bool WriteObject(const FString& File, const TSharedRef<FJsonObject>& J)
	{
		FString Text;
		FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Text));
		return FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	// (a name as a file's: letters, digits, spaces, dashes)
	FString FileOf(const FString& Name)
	{
		FString Out;
		for (TCHAR C : Name) Out.AppendChar(FChar::IsAlnum(C) || C == TEXT(' ') || C == TEXT('-') || C == TEXT('_') ? C : TEXT('_'));
		Out.TrimStartAndEndInline();
		return Out.IsEmpty() ? FString(TEXT("Unnamed")) : Out.Left(80);
	}

	TArray<TSharedPtr<FJsonValue>> PointsJson(const TArray<FVector2D>& Pts)
	{
		TArray<TSharedPtr<FJsonValue>> L;
		for (const FVector2D& P : Pts)
			L.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(FMath::RoundToDouble(P.X * 100) / 100), MakeShared<FJsonValueNumber>(FMath::RoundToDouble(P.Y * 100) / 100) }));
		return L;
	}

	TArray<FVector2D> PointsOf(const TSharedPtr<FJsonObject>& O)
	{
		TArray<FVector2D> Out;
		const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
		if (O.IsValid() && O->TryGetArrayField(TEXT("pts"), L))
			for (const TSharedPtr<FJsonValue>& P : *L)
			{
				const TArray<TSharedPtr<FJsonValue>>* XZ = nullptr;
				if (P.IsValid() && P->TryGetArray(XZ) && XZ->Num() >= 2) Out.Add(FVector2D((*XZ)[0]->AsNumber(), (*XZ)[1]->AsNumber()));
			}
		return Out;
	}

	bool InPolygon(const TArray<FVector2D>& Pts, double X, double Z)
	{
		if (Pts.Num() < 3) return false;
		bool bIn = false;
		for (int32 i = 0, j = Pts.Num() - 1; i < Pts.Num(); j = i++)
			if ((Pts[i].Y > Z) != (Pts[j].Y > Z) && X < (Pts[j].X - Pts[i].X) * (Z - Pts[i].Y) / (Pts[j].Y - Pts[i].Y) + Pts[i].X) bIn = !bIn;
		return bIn;
	}

	// ---- the library's first biomes and forests
	otg::FGrassKind* KindIn(FOTGBiome& B, const FString& Name) { return B.Kinds.FindByPredicate([&](const otg::FGrassKind& K) { return K.Name == Name; }); }
	void Set(FOTGBiome& B, const TCHAR* Name, bool bOn, float Density = -1, float PatchLo = -1, float PatchHi = -1)
	{
		if (otg::FGrassKind* K = KindIn(B, Name))
		{
			K->bOn = bOn;
			if (Density >= 0) K->Density = Density;
			if (PatchLo >= 0) { K->PatchLo = PatchLo; K->PatchHi = PatchHi; }
		}
	}
	void AddPlant(FOTGBiome& B, const TCHAR* Plant, float Density = -1, float PatchLo = -1, float PatchHi = -1)
	{
		otg::FGrassKind K = otg::FGrass::PlantKind(Plant);
		if (Density >= 0) K.Density = Density;
		if (PatchLo >= 0) { K.PatchLo = PatchLo; K.PatchHi = PatchHi; }
		B.Kinds.Add(K);
	}

	TArray<FOTGBiome> StarterBiomes()
	{
		TArray<FOTGBiome> Out;
		Out.Add(FOTGBiome::TallGrass(TEXT("The game's tall grass")));
		{
			FOTGBiome B = FOTGBiome::TallGrass(TEXT("Heathland"));
			Set(B, TEXT("heather"), true, 240, 0.31f, 0.42f);
			Set(B, TEXT("flowers_yellow"), false); Set(B, TEXT("flowers_white"), false);
			Set(B, TEXT("flowers_purple"), true, 40);
			Set(B, TEXT("meadow"), true, 300);
			Out.Add(B);
		}
		{
			FOTGBiome B = FOTGBiome::TallGrass(TEXT("Wildflower meadow"));
			Set(B, TEXT("flowers_yellow"), true, 180, 0.36f, 0.44f);
			Set(B, TEXT("flowers_white"), true, 160, 0.37f, 0.45f);
			Set(B, TEXT("flowers_purple"), true, 140, 0.37f, 0.45f);
			AddPlant(B, TEXT("kite_buttercup"), 80, 0.36f, 0.45f);
			AddPlant(B, TEXT("kite_scabious"), 80, 0.36f, 0.45f);
			AddPlant(B, TEXT("kite_yarrow"), 90, 0.37f, 0.46f);
			Out.Add(B);
		}
		{
			FOTGBiome B = FOTGBiome::TallGrass(TEXT("Links dunes"));
			Set(B, TEXT("fescue_gold"), true, 320);
			Set(B, TEXT("tussock"), true, 90);
			Set(B, TEXT("flowers_yellow"), false); Set(B, TEXT("flowers_white"), false); Set(B, TEXT("flowers_purple"), false);
			Out.Add(B);
		}
		{
			FOTGBiome B = FOTGBiome::TallGrass(TEXT("Woodland floor"));
			Set(B, TEXT("fescue"), false);
			Set(B, TEXT("meadow"), true, 160);
			Set(B, TEXT("flowers_yellow"), false); Set(B, TEXT("flowers_purple"), false);
			Set(B, TEXT("flowers_white"), true, 40);
			AddPlant(B, TEXT("kite_ferns"), 90, 0.33f, 0.44f);
			AddPlant(B, TEXT("kite_leaves"), 120, 0.30f, 0.40f);
			AddPlant(B, TEXT("kite_bogmyrtle"), 25, 0.40f, 0.47f);
			Out.Add(B);
		}
		for (FOTGBiome& B : Out) otg::FGrass::Derive(B.Kinds);
		return Out;
	}

	TArray<FOTGForestPreset> StarterForests()
	{
		auto P = [](const TCHAR* Name, TArray<FOTGForestSpecies> S, float Lo, float Hi, float PerHa, const TCHAR* Under) {
			FOTGForestPreset F;
			F.Name = Name; F.Species = MoveTemp(S); F.HeightLo = Lo; F.HeightHi = Hi; F.PerHa = PerHa; F.Under = Under;
			return F;
		};
		return {
			P(TEXT("Oak and beech woodland"), { { TEXT("oak"), 3.f, FString() }, { TEXT("beech"), 2.f, FString() }, { TEXT("maple"), 1.f, FString() } }, 14, 24, 250, TEXT("Woodland floor")),
			P(TEXT("Scots pine heath"), { { TEXT("pine"), 1.f, TEXT("scotspine") } }, 10, 20, 140, TEXT("Heathland")),
			P(TEXT("Birch copse"), { { TEXT("birch"), 3.f, FString() }, { TEXT("aspen"), 1.f, FString() } }, 8, 16, 400, TEXT("")),
			P(TEXT("Mixed conifers"), { { TEXT("spruce"), 2.f, FString() }, { TEXT("pine"), 1.f, FString() }, { TEXT("hemlock"), 1.f, FString() } }, 15, 28, 350, TEXT("")),
		};
	}

	FString BiomeDir() { return FPaths::Combine(OTGLibrary::Dir(), TEXT("Biomes")); }
	FString ForestDir() { return FPaths::Combine(OTGLibrary::Dir(), TEXT("Forests")); }
}

// ------------------------------------------------------------------ a biome

FOTGBiome FOTGBiome::TallGrass(const FString& Name)
{
	FOTGBiome B;
	B.Name = Name;
	for (const otg::FGrassKind& K : otg::FGrass::DefaultKinds())
		if (K.Masks.Contains(TEXT("native"))) B.Kinds.Add(K);
	return B;
}

int32 FOTGBiome::Growing() const
{
	int32 N = 0;
	for (const otg::FGrassKind& K : Kinds) N += K.FarOf.IsEmpty() && K.bOn && K.Density > 0 ? 1 : 0;
	return N;
}

TSharedRef<FJsonObject> FOTGBiome::ToJson() const
{
	const TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("version"), 1);
	J->SetStringField(TEXT("name"), Name);
	TArray<TSharedPtr<FJsonValue>> L;
	for (const otg::FGrassKind& K : Kinds)
		if (K.FarOf.IsEmpty())
		{
			// (an added plant marked as such, so it's added back)
			otg::FGrassKind C = K;
			if (C.Name.StartsWith(TEXT("fab_"))) C.bAdded = true;
			L.Add(MakeShared<FJsonValueObject>(otg::FGrass::KindToJson(C)));
		}
	J->SetArrayField(TEXT("kinds"), L);
	return J;
}

bool FOTGBiome::FromJson(const TSharedPtr<FJsonObject>& O, FOTGBiome& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
	if (!O.IsValid() || !O->TryGetArrayField(TEXT("kinds"), L)) return false;
	FString Name;
	O->TryGetStringField(TEXT("name"), Name);
	FOTGBiome B = TallGrass(Name);
	for (const TSharedPtr<FJsonValue>& V : *L) otg::FGrass::ApplyKindJson(B.Kinds, V.IsValid() ? V->AsObject() : nullptr, 2);
	// (the tall grass only: a kind of the rough's named here grows there)
	for (otg::FGrassKind& K : B.Kinds) K.Masks = { TEXT("native") };
	otg::FGrass::Derive(B.Kinds);
	Out = MoveTemp(B);
	return true;
}

// ------------------------------------------------------------------ the library

FString OTGLibrary::Dir()
{
	return FPaths::Combine(FPlatformProcess::UserDir(), TEXT("OpenTeeGolf"), TEXT("Library"));
}

FString OTGLibrary::FreeName(const FString& Base, const TArray<FString>& Taken)
{
	if (!Taken.Contains(Base)) return Base;
	for (int32 n = 2;; n++)
	{
		const FString N = FString::Printf(TEXT("%s %d"), *Base, n);
		if (!Taken.Contains(N)) return N;
	}
}

const FOTGBiome* OTGLibrary::FindIn(const TArray<FOTGBiome>& L, const FString& Name)
{
	return L.FindByPredicate([&](const FOTGBiome& B) { return B.Name == Name; });
}

TArray<FOTGBiome> OTGLibrary::Biomes()
{
	IFileManager& FM = IFileManager::Get();
	if (!FM.DirectoryExists(*BiomeDir()))
	{
		FM.MakeDirectory(*BiomeDir(), true);
		for (const FOTGBiome& B : StarterBiomes()) SaveBiome(B);
	}
	TArray<FString> Files;
	FM.FindFiles(Files, *FPaths::Combine(BiomeDir(), TEXT("*.json")), true, false);
	TArray<FOTGBiome> Out;
	for (const FString& F : Files)
	{
		FOTGBiome B;
		if (!FOTGBiome::FromJson(ReadObject(FPaths::Combine(BiomeDir(), F)), B)) { UE_LOG(LogOTGBiomes, Warning, TEXT("Biome %s doesn't read"), *F); continue; }
		if (B.Name.IsEmpty()) B.Name = FPaths::GetBaseFilename(F);
		if (!FindIn(Out, B.Name)) Out.Add(MoveTemp(B));
	}
	Out.Sort([](const FOTGBiome& A, const FOTGBiome& B) { return A.Name < B.Name; });
	return Out;
}

bool OTGLibrary::SaveBiome(const FOTGBiome& B)
{
	IFileManager::Get().MakeDirectory(*BiomeDir(), true);
	return WriteObject(FPaths::Combine(BiomeDir(), FileOf(B.Name) + TEXT(".json")), B.ToJson());
}

bool OTGLibrary::DeleteBiome(const FString& Name)
{
	return IFileManager::Get().Delete(*FPaths::Combine(BiomeDir(), FileOf(Name) + TEXT(".json")));
}

TArray<FOTGForestPreset> OTGLibrary::Forests()
{
	IFileManager& FM = IFileManager::Get();
	if (!FM.DirectoryExists(*ForestDir()))
	{
		FM.MakeDirectory(*ForestDir(), true);
		for (const FOTGForestPreset& P : StarterForests()) SaveForest(P);
	}
	TArray<FString> Files;
	FM.FindFiles(Files, *FPaths::Combine(ForestDir(), TEXT("*.json")), true, false);
	TArray<FOTGForestPreset> Out;
	for (const FString& F : Files)
	{
		FOTGForestPreset P;
		if (!FOTGForestPreset::FromJson(ReadObject(FPaths::Combine(ForestDir(), F)), P)) continue;
		if (P.Name.IsEmpty()) P.Name = FPaths::GetBaseFilename(F);
		if (!Out.ContainsByPredicate([&](const FOTGForestPreset& X) { return X.Name == P.Name; })) Out.Add(MoveTemp(P));
	}
	Out.Sort([](const FOTGForestPreset& A, const FOTGForestPreset& B) { return A.Name < B.Name; });
	return Out;
}

bool OTGLibrary::SaveForest(const FOTGForestPreset& P)
{
	IFileManager::Get().MakeDirectory(*ForestDir(), true);
	const TSharedRef<FJsonObject> J = P.ToJson();
	J->SetNumberField(TEXT("version"), 1);
	return WriteObject(FPaths::Combine(ForestDir(), FileOf(P.Name) + TEXT(".json")), J);
}

bool OTGLibrary::DeleteForest(const FString& Name)
{
	return IFileManager::Get().Delete(*FPaths::Combine(ForestDir(), FileOf(Name) + TEXT(".json")));
}

// ------------------------------------------------------------------ a course's biomes

bool FOTGBiomeArea::Contains(double X, double Z) const { return InPolygon(Pts, X, Z); }

FOTGCourseBiomes FOTGCourseBiomes::Load(const FString& Dir)
{
	FOTGCourseBiomes Out;
	const TSharedPtr<FJsonObject> J = ReadObject(FPaths::Combine(Dir, TEXT("biomes.json")));
	if (!J.IsValid()) return Out;
	J->TryGetStringField(TEXT("default"), Out.Default);
	const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
	if (J->TryGetArrayField(TEXT("biomes"), L))
		for (const TSharedPtr<FJsonValue>& V : *L)
		{
			FOTGBiome B;
			if (V.IsValid() && FOTGBiome::FromJson(V->AsObject(), B) && !B.Name.IsEmpty() && !Out.Find(B.Name)) Out.Biomes.Add(MoveTemp(B));
		}
	if (J->TryGetArrayField(TEXT("areas"), L))
		for (const TSharedPtr<FJsonValue>& V : *L)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			FOTGBiomeArea A;
			if (!O.IsValid() || !O->TryGetStringField(TEXT("biome"), A.Biome)) continue;
			A.Pts = PointsOf(O);
			if (A.Pts.Num() >= 3 && Out.Find(A.Biome)) Out.Areas.Add(MoveTemp(A));
		}
	if (!Out.Default.IsEmpty() && !Out.Find(Out.Default)) Out.Default.Empty();
	return Out;
}

bool FOTGCourseBiomes::Save(const FString& Dir) const
{
	const FString File = FPaths::Combine(Dir, TEXT("biomes.json"));
	if (IsEmpty()) { IFileManager::Get().Delete(*File); return true; }
	const TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("version"), 1);
	J->SetStringField(TEXT("default"), Default);
	TArray<TSharedPtr<FJsonValue>> L;
	for (const FOTGBiome& B : Biomes) L.Add(MakeShared<FJsonValueObject>(B.ToJson()));
	J->SetArrayField(TEXT("biomes"), L);
	TArray<TSharedPtr<FJsonValue>> A;
	for (const FOTGBiomeArea& Ar : Areas)
	{
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("biome"), Ar.Biome);
		O->SetArrayField(TEXT("pts"), PointsJson(Ar.Pts));
		A.Add(MakeShared<FJsonValueObject>(O));
	}
	J->SetArrayField(TEXT("areas"), A);
	return WriteObject(File, J);
}

void FOTGCourseBiomes::Use(const FOTGBiome& B)
{
	const int32 I = Biomes.IndexOfByPredicate([&](const FOTGBiome& X) { return X.Name == B.Name; });
	if (I == INDEX_NONE) Biomes.Add(B); else Biomes[I] = B;
}

void FOTGCourseBiomes::Tidy()
{
	Biomes.RemoveAll([&](const FOTGBiome& B) { return B.Name != Default && !Areas.ContainsByPredicate([&](const FOTGBiomeArea& A) { return A.Biome == B.Name; }); });
	Areas.RemoveAll([&](const FOTGBiomeArea& A) { return A.Pts.Num() < 3 || !Find(A.Biome); });
}

TArray<FString> FOTGCourseBiomes::AreaBiomes() const
{
	TArray<FString> Out;
	for (const FOTGBiomeArea& A : Areas) if (Find(A.Biome)) Out.AddUnique(A.Biome);
	return Out;
}

TArray<otg::FGrassKind> FOTGCourseBiomes::Grow(const TArray<otg::FGrassKind>& Own) const
{
	const TArray<FString> InAreas = AreaBiomes();
	const bool bMap = InAreas.Num() > 0;
	TArray<otg::FGrassKind> Out;
	const FOTGBiome* Def = Default.IsEmpty() ? nullptr : Find(Default);
	for (otg::FGrassKind K : Own)
	{
		const bool bTall = K.Masks.Contains(TEXT("native"));
		if (bTall && Def) continue;   // (the default biome's instead)
		K.Biome = bTall && bMap ? 0 : -1;
		Out.Add(K);
	}
	if (Def)
		for (otg::FGrassKind K : Def->Kinds) { K.Biome = bMap ? 0 : -1; Out.Add(K); }
	for (int32 n = 0; n < InAreas.Num(); n++)
		if (const FOTGBiome* B = Find(InAreas[n]))
			for (otg::FGrassKind K : B->Kinds)
			{
				if (!K.bOn) continue;
				const FString Tag = FString::Printf(TEXT("@%d"), n + 1);
				K.Name += Tag;
				if (!K.FarOf.IsEmpty()) K.FarOf += Tag;
				K.Biome = n + 1;
				Out.Add(K);
			}
	return Out;
}

TArray<uint8> FOTGCourseBiomes::Raster(const UOTGCourseData* D, double LandCm, int32 Size) const
{
	TArray<uint8> Map;
	const TArray<FString> InAreas = AreaBiomes();
	if (!D || !InAreas.Num() || Size <= 0 || LandCm <= 0) return Map;
	Map.SetNumZeroed(Size * Size);
	const double Cell = LandCm / Size;
	for (int32 a = 0; a < Areas.Num(); a++)
	{
		const FOTGBiomeArea& A = Areas[a];
		const int32 Idx = InAreas.IndexOfByKey(A.Biome) + 1;
		if (Idx <= 0) continue;
		// (the area's box in cells, then each cell's middle tested: the level's X east = course X, Y south)
		FBox2D B(ForceInit);
		for (const FVector2D& P : A.Pts) B += P;
		const int32 I0 = FMath::Clamp(FMath::FloorToInt32((B.Min.X - D->HeightX0) * 100.0 / Cell), 0, Size - 1), I1 = FMath::Clamp(FMath::CeilToInt32((B.Max.X - D->HeightX0) * 100.0 / Cell), 0, Size - 1);
		const int32 J0 = FMath::Clamp(FMath::FloorToInt32((D->HeightZ1 - B.Max.Y) * 100.0 / Cell), 0, Size - 1), J1 = FMath::Clamp(FMath::CeilToInt32((D->HeightZ1 - B.Min.Y) * 100.0 / Cell), 0, Size - 1);
		for (int32 j = J0; j <= J1; j++)
			for (int32 i = I0; i <= I1; i++)
			{
				const double X = D->HeightX0 + (i + 0.5) * Cell / 100.0, Z = D->HeightZ1 - (j + 0.5) * Cell / 100.0;
				if (A.Contains(X, Z)) Map[j * Size + i] = uint8(FMath::Min(Idx, 255));
			}
	}
	return Map;
}
