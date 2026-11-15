// See OTGCourseData.h.

#include "OTGCourseData.h"

#include "Physics/OTGPlayCourse.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGCourseData, Log, All);

bool otg::ReadGreyPng(const FString& File, int32 BitDepth, int32& OutW, int32& OutH, TArray64<uint8>& OutRaw)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *File)) { UE_LOG(LogOTGCourseData, Error, TEXT("Can't read %s"), *File); return false; }
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
	if (!Png.IsValid() || !Png->SetCompressed(Bytes.GetData(), Bytes.Num()) || !Png->GetRaw(ERGBFormat::Gray, BitDepth, OutRaw))
	{
		UE_LOG(LogOTGCourseData, Error, TEXT("Can't decode %s"), *File);
		return false;
	}
	OutW = Png->GetWidth();
	OutH = Png->GetHeight();
	return true;
}

namespace
{
	FVector Point(const TSharedPtr<FJsonObject>& O)
	{
		return O.IsValid() ? FVector(O->GetNumberField(TEXT("x")), O->GetNumberField(TEXT("y")), O->GetNumberField(TEXT("z"))) : FVector::ZeroVector;
	}
}

bool UOTGCourseData::LoadFromExport(const FString& Dir, FString* OutError)
{
	auto Fail = [&](const FString& Why) { if (OutError) *OutError = Why; UE_LOG(LogOTGCourseData, Error, TEXT("%s: %s"), *Dir, *Why); return false; };
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Dir, TEXT("play.json")))) return Fail(TEXT("no play.json (export the course again)"));
	TSharedPtr<FJsonObject> Play;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Play) || !Play.IsValid()) return Fail(TEXT("play.json doesn't read"));

	TreeDesign = FOTGTreeDesign::Load(Dir);
	CourseId = Play->GetStringField(TEXT("id"));
	CourseName = Play->GetStringField(TEXT("name"));
	Play->TryGetStringField(TEXT("location"), Location);
	Play->TryGetNumberField(TEXT("lat"), Latitude);
	Play->TryGetNumberField(TEXT("lon"), Longitude);
	Play->TryGetNumberField(TEXT("altitudeM"), AltitudeM);
	double Stimp = 10;
	Play->TryGetNumberField(TEXT("defaultStimp"), Stimp);
	DefaultStimp = Stimp;

	const TSharedPtr<FJsonObject> B = Play->GetObjectField(TEXT("bounds"));
	MinX = B->GetNumberField(TEXT("minX")); MaxX = B->GetNumberField(TEXT("maxX"));
	MinZ = B->GetNumberField(TEXT("minZ")); MaxZ = B->GetNumberField(TEXT("maxZ"));

	// the heightmap
	const TSharedPtr<FJsonObject> Hm = Play->GetObjectField(TEXT("heightmap"));
	HeightX0 = Hm->GetNumberField(TEXT("x0")); HeightZ1 = Hm->GetNumberField(TEXT("z1"));
	MetresPerSample = Hm->GetNumberField(TEXT("metresPerSample"));
	HeightBase = Hm->GetNumberField(TEXT("heightBaseMetres")); HeightRange = Hm->GetNumberField(TEXT("heightRangeMetres"));
	{
		int32 W = 0, H = 0;
		TArray64<uint8> Raw;
		if (!otg::ReadGreyPng(FPaths::Combine(Dir, Hm->GetStringField(TEXT("file"))), 16, W, H, Raw)) return Fail(TEXT("the heightmap doesn't read"));
		HeightSamples = W;
		Heights.SetNumUninitialized(W * H);
		FMemory::Memcpy(Heights.GetData(), Raw.GetData(), Heights.Num() * sizeof(uint16));
	}
	// the surfaces
	const TSharedPtr<FJsonObject> Sf = Play->GetObjectField(TEXT("surfaces"));
	SurfaceMinX = Sf->GetNumberField(TEXT("minX")); SurfaceMaxZ = Sf->GetNumberField(TEXT("maxZ")); SurfaceRes = Sf->GetNumberField(TEXT("res"));
	{
		int32 W = 0, H = 0;
		TArray64<uint8> Raw;
		if (!otg::ReadGreyPng(FPaths::Combine(Dir, Sf->GetStringField(TEXT("file"))), 8, W, H, Raw)) return Fail(TEXT("the surfaces don't read"));
		SurfaceW = W; SurfaceH = H;
		Surfaces = TArray<uint8>(Raw.GetData(), static_cast<int32>(Raw.Num()));
	}
	// holes
	Holes.Reset();
	for (const TSharedPtr<FJsonValue>& V : Play->GetArrayField(TEXT("holes")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FOTGHole Hole;
		Hole.Number = O->GetIntegerField(TEXT("number"));
		Hole.Par = O->GetIntegerField(TEXT("par"));
		O->TryGetNumberField(TEXT("index"), Hole.Index);
		double GreenR = 0;
		O->TryGetNumberField(TEXT("greenR"), GreenR);
		Hole.GreenRadius = GreenR;
		for (const auto& T : O->GetObjectField(TEXT("tees"))->Values) if (T.Value->Type == EJson::Object) Hole.Tees.Add(FString(T.Key), Point(T.Value->AsObject()));
		for (const auto& P : O->GetObjectField(TEXT("pins"))->Values) if (P.Value->Type == EJson::Object) Hole.Pins.Add(FString(P.Key), Point(P.Value->AsObject()));
		const TSharedPtr<FJsonObject>* G = nullptr;
		if (O->TryGetObjectField(TEXT("greenCenter"), G)) Hole.GreenCenter = Point(*G);
		for (const TSharedPtr<FJsonValue>& A : O->GetArrayField(TEXT("aims"))) if (A->Type == EJson::Object) Hole.Aims.Add(Point(A->AsObject()));
		for (const TSharedPtr<FJsonValue>& A : O->GetArrayField(TEXT("path"))) if (A->Type == EJson::Object) Hole.Path.Add(Point(A->AsObject()));
		Holes.Add(Hole);
	}
	// trees: [x, z, trunkTop, trunkR, kind, cone, a, b, c]
	Trees.Reset();
	for (const TSharedPtr<FJsonValue>& V : Play->GetArrayField(TEXT("trees")))
	{
		const TArray<TSharedPtr<FJsonValue>>& T = V->AsArray();
		if (T.Num() < 9) continue;
		FOTGTreeShape S;
		S.X = T[0]->AsNumber(); S.Z = T[1]->AsNumber(); S.TrunkTop = T[2]->AsNumber(); S.TrunkR = T[3]->AsNumber();
		S.Kind = T[4]->AsString(); S.bCone = T[5]->AsNumber() != 0;
		S.A = T[6]->AsNumber(); S.B = T[7]->AsNumber(); S.C = T[8]->AsNumber();
		Trees.Add(S);
	}
	Solids.Reset();
	for (const TSharedPtr<FJsonValue>& V : Play->GetArrayField(TEXT("solids")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FOTGSolid S;
		S.Height = O->GetNumberField(TEXT("h"));
		S.Kind = O->GetStringField(TEXT("kind"));
		for (const TSharedPtr<FJsonValue>& P : O->GetArrayField(TEXT("pts"))) { const auto& XZ = P->AsArray(); S.Footprint.Add(FVector2D(XZ[0]->AsNumber(), XZ[1]->AsNumber())); }
		Solids.Add(S);
	}
	DropZones.Reset();
	for (const TSharedPtr<FJsonValue>& V : Play->GetArrayField(TEXT("dropZones")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FOTGDropZone Z;
		Z.Position = Point(O);
		const TArray<TSharedPtr<FJsonValue>>* Box = nullptr;
		if (O->TryGetArrayField(TEXT("bbox"), Box) && Box->Num() == 4)
		{
			Z.bHasBox = true;
			Z.Box = FVector4((*Box)[0]->AsNumber(), (*Box)[1]->AsNumber(), (*Box)[2]->AsNumber(), (*Box)[3]->AsNumber());
		}
		DropZones.Add(Z);
	}
	UE_LOG(LogOTGCourseData, Display, TEXT("Course data: %s, %d holes, %d trees, heights %d^2, surfaces %dx%d"), *CourseName, Holes.Num(), Trees.Num(), HeightSamples, SurfaceW, SurfaceH);
	return true;
}

void UOTGCourseData::FillPlayCourse(otg::PlayCourse& Out) const
{
	Out.N = HeightSamples;
	Out.x0 = HeightX0; Out.z1 = HeightZ1; Out.step = MetresPerSample; Out.base = HeightBase; Out.range = HeightRange;
	Out.heights.assign(Heights.GetData(), Heights.GetData() + Heights.Num());
	Out.w = SurfaceW; Out.h = SurfaceH; Out.sMinX = SurfaceMinX; Out.sMaxZ = SurfaceMaxZ; Out.res = SurfaceRes;
	Out.surfaces.assign(Surfaces.GetData(), Surfaces.GetData() + Surfaces.Num());
	Out.minX = MinX; Out.maxX = MaxX; Out.minZ = MinZ; Out.maxZ = MaxZ;
}

void UOTGCourseData::FillTrees(otg::PlayTrees& Out) const
{
	Out.all.clear();
	Out.all.reserve(Trees.Num());
	// (as the tree designer has them: the drawn trees go the same way - AOTGRuntimeCourse::BuildTrees)
	ForEachDesignedTree([&](int32 Src, double X, double Z, double Ground, float Scale) {
		const FOTGTreeShape& S = Trees[Src];
		const double G0 = GroundAt(S.X, S.Z);
		// (heights above the tree's own ground, scaled, onto the ground where it stands)
		auto Up = [&](double Y) { return Ground + (Y - G0) * Scale; };
		otg::Tree T;
		T.x = X; T.z = Z; T.trunkTop = Up(S.TrunkTop); T.trunkR = S.TrunkR * Scale; T.kind = TCHAR_TO_UTF8(*S.Kind);
		T.cone = S.bCone;
		if (S.bCone) { T.coneBase = Up(S.A); T.coneH = S.B * Scale; T.coneR = S.C * Scale; }
		else { T.cy = Up(S.A); T.rh = S.B * Scale; T.rv = S.C * Scale; }
		Out.all.push_back(T);
	});
	Out.Build();
}

double UOTGCourseData::GroundAt(double X, double Z) const
{
	const int32 N = HeightSamples;
	if (N < 2 || Heights.Num() < N * N) return 0;
	const double U = FMath::Clamp((X - HeightX0) / MetresPerSample, 0.0, N - 1.001), V = FMath::Clamp((HeightZ1 - Z) / MetresPerSample, 0.0, N - 1.001);
	const int32 I = FMath::FloorToInt32(U), J = FMath::FloorToInt32(V);
	const double FU = U - I, FV = V - J;
	auto H = [&](int32 A, int32 B) { return double(Heights[B * N + A]); };
	const double Raw = FMath::Lerp(FMath::Lerp(H(I, J), H(I + 1, J), FU), FMath::Lerp(H(I, J + 1), H(I + 1, J + 1), FU), FV);
	return HeightBase + Raw * HeightRange / 65535.0;
}

uint8 UOTGCourseData::SurfaceCodeAt(double X, double Z) const
{
	const int32 I = FMath::FloorToInt32((X - SurfaceMinX) / SurfaceRes), J = FMath::FloorToInt32((SurfaceMaxZ - Z) / SurfaceRes);
	if (I < 0 || J < 0 || I >= SurfaceW || J >= SurfaceH || !Surfaces.Num()) return 0;
	return Surfaces[J * SurfaceW + I];
}

void UOTGCourseData::ForEachDesignedTree(TFunctionRef<void(int32, double, double, double, float)> Fn) const
{
	// (never on water, sand, a path or the mown grass: on the rough, the tall grass, in the woods)
	auto Free = [this](double X, double Z) {
		using S = otg::Surface;
		const S Sf = S(SurfaceCodeAt(X, Z));
		return Sf != S::Water && Sf != S::Green && Sf != S::Fringe && Sf != S::Fairway && Sf != S::Tee && Sf != S::FirstCut && Sf != S::Path && Sf != S::Sand;
	};
	for (int32 i = 0; i < Trees.Num(); i++)
	{
		const FOTGTreeShape& S = Trees[i];
		const int32 N = TreeDesign.Copies(S.Kind, S.X, S.Z);
		const float Size = TreeDesign.SizeOf(S.Kind);
		if (N >= 1) Fn(i, S.X, S.Z, GroundAt(S.X, S.Z), Size);
		// (more: grown round it, off the mown grass, sand, paths and water, a little smaller or larger)
		const double R = S.bCone ? S.C : S.B;
		for (int32 k = 1; k < N; k++)
		{
			const FVector2D P = FOTGTreeDesign::CopyAt(S.X, S.Z, k, R * Size);
			if (!Free(P.X, P.Y)) continue;
			Fn(i, P.X, P.Y, GroundAt(P.X, P.Y), Size * float(0.8 + 0.35 * FOTGTreeDesign::Hash01(P.X, P.Y, 5)));
		}
	}
}

FVector UOTGCourseData::CourseToWorld(const FVector& P) const
{
	return LevelOrigin + FVector((P.X - HeightX0) * 100.0, (HeightZ1 - P.Z) * 100.0, (P.Y - HeightBase) * 100.0);
}

FVector UOTGCourseData::WorldToCourse(const FVector& W) const
{
	const FVector L = W - LevelOrigin;
	return FVector(HeightX0 + L.X / 100.0, HeightBase + L.Z / 100.0, HeightZ1 - L.Y / 100.0);
}
