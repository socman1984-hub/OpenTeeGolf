#include "Course/OTGCourseSource.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"

namespace
{
	const TArray<FString> FeatureKinds = { TEXT("green"), TEXT("fairway"), TEXT("bunker"), TEXT("tee"), TEXT("water") };

	double R2(double V) { return FMath::RoundToDouble(V * 100) / 100; }
	double R3(double V) { return FMath::RoundToDouble(V * 1000) / 1000; }

	bool ReadXZ(const TSharedPtr<FJsonObject>& O, FVector2D& Out)
	{
		double X, Z;
		if (!O.IsValid() || !O->TryGetNumberField(TEXT("x"), X) || !O->TryGetNumberField(TEXT("z"), Z)) return false;
		Out = FVector2D(X, Z);
		return true;
	}
	// [[x, z], ...]
	TArray<FVector2D> ReadPairs(const TArray<TSharedPtr<FJsonValue>>* A)
	{
		TArray<FVector2D> Out;
		if (!A) return Out;
		for (const TSharedPtr<FJsonValue>& V : *A)
		{
			const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
			if (V.IsValid() && V->TryGetArray(P) && P->Num() >= 2) Out.Emplace((*P)[0]->AsNumber(), (*P)[1]->AsNumber());
		}
		return Out;
	}
	TArray<TSharedPtr<FJsonValue>> WritePairs(const TArray<FVector2D>& Pts)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FVector2D& P : Pts) Out.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(R2(P.X)), MakeShared<FJsonValueNumber>(R2(P.Y)) }));
		return Out;
	}
	double PathLen(const TArray<FVector2D>& Pts)
	{
		double L = 0;
		for (int32 i = 1; i < Pts.Num(); i++) L += FVector2D::Distance(Pts[i - 1], Pts[i]);
		return L;
	}
}

const TArray<FString>& FOTGCourseSource::TeesOrder()
{
	static const TArray<FString> T = { TEXT("Black"), TEXT("Blue"), TEXT("White"), TEXT("Green"), TEXT("Yellow"), TEXT("Red"), TEXT("Junior"), TEXT("Par3") };
	return T;
}

const TArray<FString>& FOTGCourseSource::MowPatterns()
{
	static const TArray<FString> M = { TEXT("none"), TEXT("stripes"), TEXT("diagonal"), TEXT("checker"), TEXT("diamond") };
	return M;
}

FString FOTGCourseSource::MowLabel(const FString& Id)
{
	if (Id == TEXT("stripes")) return TEXT("Straight stripes");
	if (Id == TEXT("diagonal")) return TEXT("Diagonal stripes");
	if (Id == TEXT("checker")) return TEXT("Checkerboard");
	if (Id == TEXT("diamond")) return TEXT("Diamonds");
	return TEXT("None (plain)");
}

const TArray<FOTGCourseSource::FPlant>& FOTGCourseSource::Plants()
{
	static const TArray<FPlant> P = {
		{ TEXT("boxwood"), TEXT("Boxwood"), TEXT("Shrubs") }, { TEXT("hedge"), TEXT("Clubhouse hedge"), TEXT("Shrubs") }, { TEXT("azalea"), TEXT("Azalea"), TEXT("Shrubs") },
		{ TEXT("rhododendron"), TEXT("Rhododendron"), TEXT("Shrubs") }, { TEXT("holly"), TEXT("Holly shrub"), TEXT("Shrubs") },
		{ TEXT("bedred"), TEXT("Red flower beds"), TEXT("Flower beds") }, { TEXT("bedyellow"), TEXT("Yellow flower beds"), TEXT("Flower beds") },
		{ TEXT("bedpurple"), TEXT("Purple flower beds"), TEXT("Flower beds") }, { TEXT("bedwhite"), TEXT("White and pink flower beds"), TEXT("Flower beds") },
		{ TEXT("fountaingrass"), TEXT("Fountain grass"), TEXT("Ornamental grasses") }, { TEXT("pampas"), TEXT("Pampas grass"), TEXT("Ornamental grasses") },
		{ TEXT("muhly"), TEXT("Pink muhly grass"), TEXT("Ornamental grasses") }, { TEXT("fern"), TEXT("Fern"), TEXT("Under the trees") },
		{ TEXT("palmetto"), TEXT("Saw palmetto"), TEXT("Under the trees") }, { TEXT("sagebrush"), TEXT("Sagebrush"), TEXT("Desert plants") },
		{ TEXT("creosote"), TEXT("Creosote bush"), TEXT("Desert plants") }, { TEXT("yucca"), TEXT("Yucca"), TEXT("Desert plants") },
		{ TEXT("pricklypear"), TEXT("Prickly pear"), TEXT("Desert plants") },
	};
	return P;
}

const TArray<FString>& FOTGCourseSource::Species()
{
	static const TArray<FString> S = { TEXT("oak"), TEXT("maple"), TEXT("redmaple"), TEXT("tuliptree"), TEXT("beech"), TEXT("elm"), TEXT("dogwood"), TEXT("crapemyrtle"),
		TEXT("holly"), TEXT("ash"), TEXT("birch"), TEXT("aspen"), TEXT("poplar"), TEXT("willow"), TEXT("pine"), TEXT("loblolly"), TEXT("redcedar"), TEXT("douglasfir"),
		TEXT("ponderosa"), TEXT("spruce"), TEXT("hemlock"), TEXT("juniper"), TEXT("palm"), TEXT("liveoak"), TEXT("magnolia"), TEXT("sycamore"), TEXT("cypress"),
		TEXT("baldcypress"), TEXT("olive"), TEXT("mesquite"), TEXT("paloverde"), TEXT("saguaro") };
	return S;
}

const TArray<FString>& FOTGCourseSource::Mixes()
{
	static const TArray<FString> M = { TEXT("auto"), TEXT("broadleaf"), TEXT("conifer"), TEXT("desert"), TEXT("ornamental") };
	return M;
}

FString FOTGCourseSource::MixLabel(const FString& Id)
{
	if (Id == TEXT("auto")) return TEXT("Automatic (from the photo)");
	if (Id == TEXT("broadleaf")) return TEXT("Mixed broadleaf");
	if (Id == TEXT("conifer")) return TEXT("Mixed conifers");
	if (Id == TEXT("desert")) return TEXT("Desert mix");
	if (Id == TEXT("ornamental")) return TEXT("Ornamental mix");
	return Id;
}

const TArray<FString>& FOTGCourseSource::PinDays()
{
	static const TArray<FString> D = { TEXT("Thursday"), TEXT("Friday"), TEXT("Saturday"), TEXT("Sunday") };
	return D;
}

bool FOTGCourseSource::Load(const FString& InId, const TSharedPtr<FJsonObject>& Course, const TSharedPtr<FJsonObject>& Real, FString* Why)
{
	auto Fail = [Why](const TCHAR* W) { if (Why) *Why = W; return false; };
	const TArray<TSharedPtr<FJsonValue>>* HoleList = nullptr;
	if (!Course.IsValid() || !Course->TryGetArrayField(TEXT("Holes"), HoleList)) return Fail(TEXT("not a course file (no Holes)"));
	*this = FOTGCourseSource();
	Id = InId;
	CourseJson = Course;
	RealJson = Real;
	Course->TryGetStringField(TEXT("CourseName"), Name);
	Course->TryGetStringField(TEXT("Location"), Location);
	if (!Course->TryGetNumberField(TEXT("DefaultStimp"), Stimp) || Stimp <= 0) Stimp = 10;

	// ---- the course's looks (mowing.js courseSurface, courseMowing, courseGrass; coursePlants)
	FString Sf;
	const TSharedPtr<FJsonObject>* SfO = nullptr;
	if (Course->TryGetStringField(TEXT("Surface"), Sf) && Sf == TEXT("photo")) bPhotoFairway = bPhotoGreen = bPhotoRough = true;
	else if (Course->TryGetObjectField(TEXT("Surface"), SfO))
	{
		auto Photo = [&](const TCHAR* K) { FString V; return (*SfO)->TryGetStringField(K, V) && V == TEXT("photo"); };
		bPhotoFairway = Photo(TEXT("fairway")); bPhotoGreen = Photo(TEXT("green")); bPhotoRough = Photo(TEXT("rough"));
	}
	const TSharedPtr<FJsonObject>* Mow = nullptr;
	if (Course->TryGetObjectField(TEXT("Mowing"), Mow))
	{
		FString V;
		if ((*Mow)->TryGetStringField(TEXT("fairway"), V) && MowPatterns().Contains(V)) MowFairway = V;
		if ((*Mow)->TryGetStringField(TEXT("green"), V) && MowPatterns().Contains(V)) MowGreen = V;
	}
	const TSharedPtr<FJsonObject>* Gr = nullptr;
	if (Course->TryGetObjectField(TEXT("Grass"), Gr))
	{
		auto Scale = [&](const TCHAR* Key, const TCHAR* Preset, float Lo, float Hi, TMap<FString, float> Presets, float& Out) {
			double V;
			FString P;
			if ((*Gr)->TryGetNumberField(Key, V) && FMath::IsFinite(V)) Out = FMath::Clamp(float(V), Lo, Hi);
			else if ((*Gr)->TryGetStringField(Preset, P) && Presets.Contains(P)) Out = Presets[P];
		};
		Scale(TEXT("roughScale"), TEXT("rough"), 0.3f, 1.6f, { { TEXT("short"), 0.55f }, { TEXT("medium"), 0.8f }, { TEXT("long"), 1.1f } }, RoughScale);
		Scale(TEXT("nativeScale"), TEXT("native"), 0.4f, 1.8f, { { TEXT("short"), 0.7f }, { TEXT("medium"), 1.f }, { TEXT("tall"), 1.3f } }, NativeScale);
	}
	const TArray<TSharedPtr<FJsonValue>>* Off = nullptr;
	if (Course->TryGetArrayField(TEXT("TreesOff"), Off)) for (const TSharedPtr<FJsonValue>& V : *Off) TreesOff.Add(V->AsString());
	const TSharedPtr<FJsonObject>* Pl = nullptr;
	if (Course->TryGetObjectField(TEXT("Plants"), Pl))
	{
		const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
		if ((*Pl)->TryGetArrayField(TEXT("off"), L)) for (const TSharedPtr<FJsonValue>& V : *L) PlantsOff.Add(V->AsString());
		if ((*Pl)->TryGetArrayField(TEXT("extra"), L)) for (const TSharedPtr<FJsonValue>& V : *L) PlantsExtra.Add(V->AsString());
		double A;
		if ((*Pl)->TryGetNumberField(TEXT("amount"), A) && FMath::IsFinite(A)) PlantAmount = FMath::Clamp(float(A), 0.f, 2.f);
	}

	// ---- holes (editor.js load(): tees, aim points, the green's centre, pins)
	for (const TSharedPtr<FJsonValue>& HV : *HoleList)
	{
		const TSharedPtr<FJsonObject> H = HV.IsValid() ? HV->AsObject() : nullptr;
		if (!H.IsValid()) continue;
		FOTGSourceHole Ho;
		Ho.Number = H->GetIntegerField(TEXT("HoleNumber"));
		Ho.Par = H->GetIntegerField(TEXT("Par"));
		H->TryGetNumberField(TEXT("Index"), Ho.Index);
		bool bGreen = false;
		TArray<FVector2D> Aims; Aims.SetNum(2);
		TArray<bool> HasAim = { false, false };
		const TArray<TSharedPtr<FJsonValue>>* Tees = nullptr;
		if (H->TryGetArrayField(TEXT("Tees"), Tees))
			for (const TSharedPtr<FJsonValue>& TV : *Tees)
			{
				const TSharedPtr<FJsonObject> T = TV->AsObject();
				FVector2D P;
				const TSharedPtr<FJsonObject>* Pos = nullptr;
				if (!T.IsValid() || !T->TryGetObjectField(TEXT("Position"), Pos) || !ReadXZ(*Pos, P)) continue;
				const FString Type = T->GetStringField(TEXT("TeeType"));
				if (Type.StartsWith(TEXT("AimPoint"))) { const int32 K = FCString::Atoi(*Type.Mid(8)) - 1; if (K == 0 || K == 1) { Aims[K] = P; HasAim[K] = true; } }
				else if (Type == TEXT("GreenCenterPoint")) { Ho.Green = P; bGreen = true; }
				else Ho.Tees.Add(Type, P);
			}
		for (int32 k = 0; k < 2; k++) if (HasAim[k]) Ho.Aims.Add(Aims[k]);
		const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
		if (H->TryGetArrayField(TEXT("Pins"), Pins))
			for (const TSharedPtr<FJsonValue>& PV : *Pins)
			{
				const TSharedPtr<FJsonObject> Pn = PV->AsObject();
				FVector2D P;
				const TSharedPtr<FJsonObject>* Pos = nullptr;
				if (Pn.IsValid() && Pn->TryGetObjectField(TEXT("Position"), Pos) && ReadXZ(*Pos, P)) Ho.Pins.Add(Pn->GetStringField(TEXT("Day")), P);
			}
		if (!bGreen && Ho.Pins.Num())
		{
			FVector2D S = FVector2D::ZeroVector;
			for (const auto& P : Ho.Pins) S += P.Value;
			Ho.Green = S / Ho.Pins.Num();
		}
		Holes.Add(MoveTemp(Ho));
	}

	// ---- the mapped shapes, then tree areas, penalty areas, the boundary (in the editor's order)
	const TSharedPtr<FJsonObject>* Features = nullptr;
	if (Real.IsValid() && Real->TryGetObjectField(TEXT("features"), Features))
		for (const auto& KV : (*Features)->Values)
		{
			const FString Key(KV.Key);
			if (!FeatureKinds.Contains(Key)) { Passthrough.Add(Key, KV.Value); continue; }
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			if (!KV.Value->TryGetArray(List)) continue;
			for (const TSharedPtr<FJsonValue>& FV : *List)
			{
				const TSharedPtr<FJsonObject> F = FV->AsObject();
				if (!F.IsValid()) continue;
				FOTGSourceShape S;
				S.Kind = Key;
				const TArray<TSharedPtr<FJsonValue>>* Pts = nullptr;
				F->TryGetArrayField(TEXT("pts"), Pts);
				S.Pts = ReadPairs(Pts);
				F->TryGetBoolField(TEXT("hole"), S.bInner);
				F->TryGetBoolField(TEXT("flow"), S.bFlow);
				F->TryGetBoolField(TEXT("banks"), S.bBanks);
				if (const TSharedPtr<FJsonValue> Fit = F->TryGetField(TEXT("fit")))
					S.Fit = Fit->Type == EJson::Number ? FString::FromInt(int32(Fit->AsNumber())) : Fit->AsString();
				S.Osm = F->TryGetField(TEXT("osm"));
				if (S.Pts.Num() >= 3) Shapes.Add(MoveTemp(S));
			}
		}
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Real.IsValid() && Real->TryGetArrayField(TEXT("tallGrass"), Arr))
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> A = V->AsObject();
			double R = 0;
			if (!A.IsValid() || !A->TryGetNumberField(TEXT("r"), R) || R <= 0) continue;
			FOTGGrassStroke G;
			const TArray<TSharedPtr<FJsonValue>>* Pts = nullptr;
			A->TryGetArrayField(TEXT("pts"), Pts);
			G.Pts = ReadPairs(Pts);
			G.R = R;
			A->TryGetBoolField(TEXT("clear"), G.bErase);
			TallGrass.Add(MoveTemp(G));
		}
	if (Real.IsValid() && Real->TryGetArrayField(TEXT("treeAreas"), Arr))
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> A = V->AsObject();
			if (!A.IsValid()) continue;
			FOTGSourceShape S;
			S.Kind = TEXT("trees");
			const TArray<TSharedPtr<FJsonValue>>* Pts = nullptr;
			A->TryGetArrayField(TEXT("pts"), Pts);
			S.Pts = ReadPairs(Pts);
			A->TryGetStringField(TEXT("density"), S.Density);
			A->TryGetStringField(TEXT("kind"), S.Species);
			A->TryGetNumberField(TEXT("hMin"), S.HMin);
			A->TryGetNumberField(TEXT("hMax"), S.HMax);
			if (S.Pts.Num() >= 3) Shapes.Add(MoveTemp(S));
		}
	if (Real.IsValid() && Real->TryGetArrayField(TEXT("userTrees"), Arr))
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> T = V->AsObject();
			FOTGSourceTree Tr;
			if (!ReadXZ(T, Tr.P)) continue;
			T->TryGetStringField(TEXT("size"), Tr.Size);
			T->TryGetStringField(TEXT("kind"), Tr.Kind);
			UserTrees.Add(Tr);
		}
	if (Real.IsValid() && Real->TryGetArrayField(TEXT("removedTrees"), Arr)) RemovedTrees = ReadPairs(Arr);
	const TArray<TSharedPtr<FJsonValue>>* Hazards = nullptr;
	if (Course->TryGetArrayField(TEXT("Hazards"), Hazards))
		for (const TSharedPtr<FJsonValue>& V : *Hazards)
		{
			const TSharedPtr<FJsonObject> Hz = V->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* C = nullptr;
			if (!Hz.IsValid() || !Hz->TryGetArrayField(TEXT("coords"), C)) continue;
			FOTGSourceShape S;
			S.Kind = TEXT("penalty");
			for (const TSharedPtr<FJsonValue>& P : *C) { FVector2D Q; if (ReadXZ(P->AsObject(), Q)) S.Pts.Add(Q); }
			Hz->TryGetBoolField(TEXT("innerOOB"), S.bInnerOOB);
			Shapes.Add(MoveTemp(S));
		}
	const TSharedPtr<FJsonObject>* Oob = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* OobC = nullptr;
	if (Course->TryGetObjectField(TEXT("pOOB"), Oob) && (*Oob)->TryGetArrayField(TEXT("coords"), OobC) && OobC->Num() > 2)
	{
		FOTGSourceShape S;
		S.Kind = TEXT("oob");
		for (const TSharedPtr<FJsonValue>& P : *OobC) { FVector2D Q; if (ReadXZ(P->AsObject(), Q)) S.Pts.Add(Q); }
		Shapes.Add(MoveTemp(S));
	}
	const TSharedPtr<FJsonObject>* Bt = nullptr;
	if (Real.IsValid() && Real->TryGetObjectField(TEXT("buildingTypes"), Bt))
		for (const auto& KV : (*Bt)->Values) BuildingTypes.Add(FString(KV.Key), KV.Value->AsString());

	// ---- the ground's heights (real.grid: Float32 heights in base64, row by row from minZ)
	const TSharedPtr<FJsonObject>* Grid = nullptr;
	FString B64;
	if (Real.IsValid() && Real->TryGetObjectField(TEXT("grid"), Grid) && (*Grid)->TryGetStringField(TEXT("heights"), B64))
	{
		GridMinX = (*Grid)->GetNumberField(TEXT("minX"));
		GridMinZ = (*Grid)->GetNumberField(TEXT("minZ"));
		GridCell = (*Grid)->GetNumberField(TEXT("cell"));
		Nx = (*Grid)->GetIntegerField(TEXT("nx"));
		Nz = (*Grid)->GetIntegerField(TEXT("nz"));
		TArray<uint8> Bytes;
		if (FBase64::Decode(B64, Bytes) && Nx > 1 && Nz > 1 && Bytes.Num() >= Nx * Nz * 4 && GridCell > 0)
		{
			Heights.SetNumUninitialized(Nx * Nz);
			FMemory::Memcpy(Heights.GetData(), Bytes.GetData(), Nx * Nz * sizeof(float));
		}
	}
	return true;
}

double FOTGCourseSource::HeightAt(double X, double Z) const
{
	if (!Heights.Num()) return 0;
	const double Fx = FMath::Clamp((X - GridMinX) / GridCell, 0.0, Nx - 1.001);
	const double Fz = FMath::Clamp((Z - GridMinZ) / GridCell, 0.0, Nz - 1.001);
	const int32 I = FMath::FloorToInt32(Fx), J = FMath::FloorToInt32(Fz);
	const double U = Fx - I, V = Fz - J;
	const int32 K = J * Nx + I;
	return Heights[K] * (1 - U) * (1 - V) + Heights[K + 1] * U * (1 - V) + Heights[K + Nx] * (1 - U) * V + Heights[K + Nx + 1] * U * V;
}

TSharedRef<FJsonObject> FOTGCourseSource::SaveBody() const
{
	// A point as the course file has it: x, z to the centimetre, y the ground's height to the mm
	auto P = [this](const FVector2D& Q) {
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), R2(Q.X));
		O->SetNumberField(TEXT("y"), R3(HeightAt(Q.X, Q.Y)));
		O->SetNumberField(TEXT("z"), R2(Q.Y));
		return O;
	};
	auto Rounded = [](const FVector2D& Q) { return FVector2D(R2(Q.X), R2(Q.Y)); };

	// ---- the course file (coursefile.js makeCourseData)
	const TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();
	C->SetNumberField(TEXT("gkversion"), 3.0);
	C->SetStringField(TEXT("CourseName"), Name);
	C->SetStringField(TEXT("Location"), Location);
	FString Designer;
	if (!CourseJson.IsValid() || !CourseJson->TryGetStringField(TEXT("Designer"), Designer) || Designer.IsEmpty()) Designer = TEXT("OpenTeeGolf course builder");
	C->SetStringField(TEXT("Designer"), Designer);
	double Lat = 0, Lon = 0;
	if (CourseJson.IsValid()) { CourseJson->TryGetNumberField(TEXT("CourseLat"), Lat); CourseJson->TryGetNumberField(TEXT("CourseLong"), Lon); }
	C->SetNumberField(TEXT("CourseLat"), Lat);
	C->SetNumberField(TEXT("CourseLong"), Lon);
	C->SetNumberField(TEXT("DefaultStimp"), Stimp);
	// (the course's looks, as makeCourseData writes them)
	{
		const TSharedRef<FJsonObject> Mow = MakeShared<FJsonObject>();
		Mow->SetStringField(TEXT("fairway"), MowFairway);
		Mow->SetStringField(TEXT("green"), MowGreen);
		C->SetObjectField(TEXT("Mowing"), Mow);
		// (the nearest preset too, for versions before the sliders)
		auto Nearest = [](float S, TArray<TPair<FString, float>> P) { FString Best; float D = 1e9f; for (const auto& X : P) if (FMath::Abs(X.Value - S) < D) { D = FMath::Abs(X.Value - S); Best = X.Key; } return Best; };
		const TSharedRef<FJsonObject> Gr = MakeShared<FJsonObject>();
		Gr->SetStringField(TEXT("rough"), Nearest(RoughScale, { { TEXT("short"), 0.55f }, { TEXT("medium"), 0.8f }, { TEXT("long"), 1.1f } }));
		Gr->SetStringField(TEXT("native"), Nearest(NativeScale, { { TEXT("short"), 0.7f }, { TEXT("medium"), 1.f }, { TEXT("tall"), 1.3f } }));
		Gr->SetNumberField(TEXT("roughScale"), FMath::RoundToDouble(RoughScale * 100) / 100);
		Gr->SetNumberField(TEXT("nativeScale"), FMath::RoundToDouble(NativeScale * 100) / 100);
		C->SetObjectField(TEXT("Grass"), Gr);
		if (TreesOff.Num())
		{
			TArray<TSharedPtr<FJsonValue>> L;
			for (const FString& S : TreesOff) L.Add(MakeShared<FJsonValueString>(S));
			C->SetArrayField(TEXT("TreesOff"), L);
		}
		if (PlantsOff.Num() || PlantsExtra.Num() || PlantAmount != 1.f)
		{
			const TSharedRef<FJsonObject> Pl = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> A, B;
			for (const FString& S : PlantsOff) A.Add(MakeShared<FJsonValueString>(S));
			for (const FString& S : PlantsExtra) B.Add(MakeShared<FJsonValueString>(S));
			Pl->SetArrayField(TEXT("off"), A);
			Pl->SetArrayField(TEXT("extra"), B);
			Pl->SetNumberField(TEXT("amount"), FMath::RoundToDouble(PlantAmount * 100) / 100);
			C->SetObjectField(TEXT("Plants"), Pl);
		}
		if (bPhotoFairway && bPhotoGreen && bPhotoRough) C->SetStringField(TEXT("Surface"), TEXT("photo"));
		else if (bPhotoFairway || bPhotoGreen || bPhotoRough)
		{
			const TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("fairway"), bPhotoFairway ? TEXT("photo") : TEXT("game"));
			S->SetStringField(TEXT("green"), bPhotoGreen ? TEXT("photo") : TEXT("game"));
			S->SetStringField(TEXT("rough"), bPhotoRough ? TEXT("photo") : TEXT("game"));
			C->SetObjectField(TEXT("Surface"), S);
		}
	}
	double Alt = 0;
	if (CourseJson.IsValid()) CourseJson->TryGetNumberField(TEXT("altitudeV2"), Alt);
	C->SetNumberField(TEXT("altitudeV2"), Alt);
	TArray<TSharedPtr<FJsonValue>> OobCoords;
	for (const FOTGSourceShape& S : Shapes) if (S.Kind == TEXT("oob")) { for (const FVector2D& Q : S.Pts) OobCoords.Add(MakeShared<FJsonValueObject>(P(Q))); break; }
	const TSharedRef<FJsonObject> Oob = MakeShared<FJsonObject>();
	Oob->SetNumberField(TEXT("pointCount"), OobCoords.Num());
	Oob->SetArrayField(TEXT("coords"), OobCoords);
	C->SetObjectField(TEXT("pOOB"), Oob);

	TArray<FOTGSourceHole> Sorted = Holes;
	Sorted.StableSort([](const FOTGSourceHole& A, const FOTGSourceHole& B) { return A.Number < B.Number; });
	TMap<FString, double> Totals;
	TArray<TSharedPtr<FJsonValue>> HoleOut;
	for (const FOTGSourceHole& H : Sorted)
	{
		TArray<FVector2D> Aims;
		for (const FVector2D& A : H.Aims) Aims.Add(Rounded(A));
		TArray<TSharedPtr<FJsonValue>> Tees;
		auto Tee = [&Tees](const FString& Type, bool bEnabled, double Dist, TSharedPtr<FJsonObject> Pos) {
			const TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
			T->SetStringField(TEXT("TeeType"), Type);
			T->SetBoolField(TEXT("Enabled"), bEnabled);
			T->SetNumberField(TEXT("Distance"), Dist);
			if (Pos.IsValid()) T->SetObjectField(TEXT("Position"), Pos); else T->SetField(TEXT("Position"), MakeShared<FJsonValueNull>());
			Tees.Add(MakeShared<FJsonValueObject>(T));
		};
		for (const FString& T : TeesOrder())
		{
			const FVector2D* Q = H.Tees.Find(T);
			double D = 0;
			if (Q)
			{
				TArray<FVector2D> Path = { Rounded(*Q) };
				Path.Append(Aims);
				Path.Add(Rounded(H.Green));
				D = PathLen(Path);
			}
			Totals.FindOrAdd(T) += D;
			Tee(T, true, R3(D), Q ? TSharedPtr<FJsonObject>(P(*Q)) : nullptr);
		}
		for (int32 k = 0; k < 2; k++) Tee(FString::Printf(TEXT("AimPoint%d"), k + 1), true, 0, H.Aims.IsValidIndex(k) ? TSharedPtr<FJsonObject>(P(H.Aims[k])) : nullptr);
		Tee(TEXT("GreenCenterPoint"), false, 0, P(H.Green));
		TArray<TSharedPtr<FJsonValue>> Pins;
		for (const FString& Day : PinDays())
		{
			const TSharedRef<FJsonObject> Pn = MakeShared<FJsonObject>();
			Pn->SetStringField(TEXT("Day"), Day);
			const FVector2D* Q = H.Pins.Find(Day);
			Pn->SetObjectField(TEXT("Position"), P(Q ? *Q : H.Green));
			Pins.Add(MakeShared<FJsonValueObject>(Pn));
		}
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("Enabled"), true);
		O->SetNumberField(TEXT("HoleNumber"), H.Number);
		O->SetNumberField(TEXT("Par"), H.Par);
		if (H.Index > 0) O->SetNumberField(TEXT("Index"), H.Index);
		O->SetArrayField(TEXT("Tees"), Tees);
		O->SetArrayField(TEXT("Pins"), Pins);
		HoleOut.Add(MakeShared<FJsonValueObject>(O));
	}
	C->SetArrayField(TEXT("Holes"), HoleOut);
	TArray<TSharedPtr<FJsonValue>> HazOut;
	for (const FOTGSourceShape& S : Shapes)
	{
		if (S.Kind != TEXT("penalty")) continue;
		TArray<TSharedPtr<FJsonValue>> Coords;
		for (const FVector2D& Q : S.Pts) Coords.Add(MakeShared<FJsonValueObject>(P(Q)));
		const TSharedRef<FJsonObject> Hz = MakeShared<FJsonObject>();
		Hz->SetNumberField(TEXT("pointCount"), Coords.Num());
		Hz->SetArrayField(TEXT("coords"), Coords);
		const TSharedRef<FJsonObject> Dz = MakeShared<FJsonObject>();
		Dz->SetNumberField(TEXT("x"), 0); Dz->SetNumberField(TEXT("y"), 0); Dz->SetNumberField(TEXT("z"), 0);
		Hz->SetObjectField(TEXT("DZpos"), Dz);
		Hz->SetBoolField(TEXT("freeDrop"), false);
		Hz->SetBoolField(TEXT("innerOOB"), S.bInnerOOB);
		Hz->SetBoolField(TEXT("noAIL"), false);
		Hz->SetBoolField(TEXT("hasDZ"), false);
		Hz->SetBoolField(TEXT("islandGreen"), false);
		HazOut.Add(MakeShared<FJsonValueObject>(Hz));
	}
	C->SetArrayField(TEXT("Hazards"), HazOut);
	TArray<TSharedPtr<FJsonValue>> TotOut;
	for (const FString& T : TeesOrder())
	{
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("TeeType"), T);
		O->SetBoolField(TEXT("Enabled"), true);
		O->SetNumberField(TEXT("Distance"), R3(Totals.FindRef(T)));
		O->SetField(TEXT("Position"), MakeShared<FJsonValueNull>());
		TotOut.Add(MakeShared<FJsonValueObject>(O));
	}
	C->SetArrayField(TEXT("TeeTypeTotalDistance"), TotOut);
	const TSharedRef<FJsonObject> Builder = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject>* Was = nullptr;
	if (CourseJson.IsValid() && CourseJson->TryGetObjectField(TEXT("GolfSimBuilder"), Was)) Builder->Values = (*Was)->Values;
	Builder->SetBoolField(TEXT("needsReview"), false);
	Builder->SetStringField(TEXT("edited"), FDateTime::UtcNow().ToIso8601());
	C->SetObjectField(TEXT("GolfSimBuilder"), Builder);

	// ---- the rest (the editor's save(): features, the player's trees, areas, tall grass ...)
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("course"), C);
	const TSharedRef<FJsonObject> Features = MakeShared<FJsonObject>();
	for (const auto& KV : Passthrough) Features->SetField(KV.Key, KV.Value);
	for (const FString& K : FeatureKinds)
	{
		TArray<TSharedPtr<FJsonValue>> List;
		for (const FOTGSourceShape& S : Shapes)
		{
			if (S.Kind != K) continue;
			const TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
			F->SetBoolField(TEXT("hole"), S.bInner);
			F->SetArrayField(TEXT("pts"), WritePairs(S.Pts));
			if (K == TEXT("water") && S.bFlow) { F->SetBoolField(TEXT("flow"), true); if (S.bBanks) F->SetBoolField(TEXT("banks"), true); }
			if (K == TEXT("bunker"))
			{
				// (a bunker moved in the editor is the player's: the game never moves it again; the
				// others keep how they were fitted - the web editor marks every bunker it saves)
				if (S.bEdited && !S.bInner) F->SetStringField(TEXT("fit"), TEXT("user"));
				else if (S.Fit == TEXT("2")) F->SetNumberField(TEXT("fit"), 2);
				else if (!S.Fit.IsEmpty()) F->SetStringField(TEXT("fit"), S.Fit);
				if (S.Osm.IsValid() && !S.bEdited) F->SetField(TEXT("osm"), S.Osm);
			}
			List.Add(MakeShared<FJsonValueObject>(F));
		}
		Features->SetArrayField(K, List);
	}
	Body->SetObjectField(TEXT("features"), Features);
	TArray<TSharedPtr<FJsonValue>> Trees;
	for (const FOTGSourceTree& T : UserTrees)
	{
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), R2(T.P.X));
		O->SetNumberField(TEXT("z"), R2(T.P.Y));
		O->SetStringField(TEXT("size"), T.Size);
		O->SetStringField(TEXT("kind"), T.Kind);
		Trees.Add(MakeShared<FJsonValueObject>(O));
	}
	Body->SetArrayField(TEXT("userTrees"), Trees);
	TArray<TSharedPtr<FJsonValue>> Areas;
	for (const FOTGSourceShape& S : Shapes)
	{
		if (S.Kind != TEXT("trees")) continue;
		const TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetArrayField(TEXT("pts"), WritePairs(S.Pts));
		A->SetStringField(TEXT("density"), S.Density.IsEmpty() ? FString(TEXT("medium")) : S.Density);
		if (!S.Species.IsEmpty() && S.Species != TEXT("auto")) A->SetStringField(TEXT("kind"), S.Species);
		if (FMath::IsFinite(S.HMin) && FMath::IsFinite(S.HMax)) { A->SetNumberField(TEXT("hMin"), S.HMin); A->SetNumberField(TEXT("hMax"), S.HMax); }
		Areas.Add(MakeShared<FJsonValueObject>(A));
	}
	Body->SetArrayField(TEXT("treeAreas"), Areas);
	TArray<TSharedPtr<FJsonValue>> Grass;
	for (const FOTGGrassStroke& G : TallGrass)
	{
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetArrayField(TEXT("pts"), WritePairs(G.Pts));
		O->SetNumberField(TEXT("r"), G.R);
		if (G.bErase) O->SetBoolField(TEXT("clear"), true);
		Grass.Add(MakeShared<FJsonValueObject>(O));
	}
	Body->SetArrayField(TEXT("tallGrass"), Grass);
	Body->SetArrayField(TEXT("removedTrees"), WritePairs(RemovedTrees));
	const TSharedRef<FJsonObject> Bt = MakeShared<FJsonObject>();
	for (const auto& KV : BuildingTypes) Bt->SetStringField(KV.Key, KV.Value);
	Body->SetObjectField(TEXT("buildingTypes"), Bt);
	return Body;
}

FString FOTGCourseSource::Summary() const
{
	TMap<FString, int32> Kinds;
	for (const FOTGSourceShape& S : Shapes) Kinds.FindOrAdd(S.Kind)++;
	FString K;
	for (const auto& KV : Kinds) K += FString::Printf(TEXT(" %s %d"), *KV.Key, KV.Value);
	return FString::Printf(TEXT("%s (%s): %s, %d holes, stimp %.1f; shapes:%s; %d own trees, %d removed, %d tall grass strokes, %d building types; heights %dx%d"),
		*Name, *Id, *Location, Holes.Num(), Stimp, *K, UserTrees.Num(), RemovedTrees.Num(), TallGrass.Num(), BuildingTypes.Num(), Nx, Nz);
}
