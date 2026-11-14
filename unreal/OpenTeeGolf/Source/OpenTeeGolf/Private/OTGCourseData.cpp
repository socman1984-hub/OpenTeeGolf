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
	for (const FOTGTreeShape& S : Trees)
	{
		otg::Tree T;
		T.x = S.X; T.z = S.Z; T.trunkTop = S.TrunkTop; T.trunkR = S.TrunkR; T.kind = TCHAR_TO_UTF8(*S.Kind);
		T.cone = S.bCone;
		if (S.bCone) { T.coneBase = S.A; T.coneH = S.B; T.coneR = S.C; }
		else { T.cy = S.A; T.rh = S.B; T.rv = S.C; }
		Out.all.push_back(T);
	}
	Out.Build();
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
