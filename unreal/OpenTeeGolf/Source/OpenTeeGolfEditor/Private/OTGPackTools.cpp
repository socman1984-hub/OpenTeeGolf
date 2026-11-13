// See OTGPackTools.h.

#include "OTGPackTools.h"

#include "OTGCourseData.h"

#include "Editor.h"
#include "Engine/World.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGPack, Log, All);

namespace
{
	// A greyscale PNG's pixels (8 or 16 bits)
	bool ReadGreyPng(const FString& File, int32 BitDepth, int32& OutW, int32& OutH, TArray64<uint8>& OutRaw)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *File)) { UE_LOG(LogOTGPack, Error, TEXT("Can't read %s"), *File); return false; }
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Png.IsValid() || !Png->SetCompressed(Bytes.GetData(), Bytes.Num()) || !Png->GetRaw(ERGBFormat::Gray, BitDepth, OutRaw))
		{
			UE_LOG(LogOTGPack, Error, TEXT("Can't decode %s"), *File);
			return false;
		}
		OutW = Png->GetWidth();
		OutH = Png->GetHeight();
		return true;
	}

	FVector Point(const TSharedPtr<FJsonObject>& O)
	{
		return O.IsValid() ? FVector(O->GetNumberField(TEXT("x")), O->GetNumberField(TEXT("y")), O->GetNumberField(TEXT("z"))) : FVector::ZeroVector;
	}
}

ALandscape* UOTGPackTools::CreateLandscape(const FString& HeightmapPng, FVector Corner, FVector Scale)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { UE_LOG(LogOTGPack, Error, TEXT("No editor world")); return nullptr; }
	int32 W = 0, H = 0;
	TArray64<uint8> Raw;
	if (!ReadGreyPng(HeightmapPng, 16, W, H, Raw)) return nullptr;
	if (W != H) { UE_LOG(LogOTGPack, Error, TEXT("Heightmap isn't square (%dx%d)"), W, H); return nullptr; }
	// Components: the first layout that fits the samples exactly (505 .. 8129: 63 quads a section
	// up to 4033, then 127)
	const int32 Quads = W - 1;
	int32 SectionQuads = 0, Sections = 0;
	for (const FIntPoint& Option : { FIntPoint(63, 1), FIntPoint(127, 1), FIntPoint(63, 2), FIntPoint(255, 1), FIntPoint(127, 2), FIntPoint(255, 2) })
	{
		if (Quads % (Option.X * Option.Y) == 0) { SectionQuads = Option.X; Sections = Option.Y; break; }
	}
	if (!SectionQuads) { UE_LOG(LogOTGPack, Error, TEXT("No landscape layout fits %d samples"), W); return nullptr; }

	TArray<uint16> Heights;
	Heights.SetNumUninitialized(W * H);
	FMemory::Memcpy(Heights.GetData(), Raw.GetData(), Heights.Num() * sizeof(uint16));
	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	HeightDataPerLayers.Add(FGuid(), MoveTemp(Heights));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
	MaterialLayerDataPerLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	ALandscape* Landscape = World->SpawnActor<ALandscape>(Corner, FRotator::ZeroRotator);
	Landscape->SetActorRelativeScale3D(Scale);
	Landscape->StaticLightingLOD = FMath::DivideAndRoundUp(FMath::CeilLogTwo((W * H) / (2048 * 2048) + 1), (uint32)2);
	Landscape->Import(FGuid::NewGuid(), 0, 0, W - 1, H - 1, Sections, SectionQuads, HeightDataPerLayers, *HeightmapPng, MaterialLayerDataPerLayers, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());
	if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo()) Info->UpdateLayerInfoMap(Landscape);
	Landscape->SetActorLabel(TEXT("Landscape"));
	UE_LOG(LogOTGPack, Display, TEXT("Landscape: %d x %d samples, %d x %d components"), W, H, Quads / (SectionQuads * Sections), Quads / (SectionQuads * Sections));
	return Landscape;
}

UOTGCourseData* UOTGPackTools::CreateCourseData(const FString& ExportDir, const FString& PackagePath, const FString& AssetName, const FString& LevelPath, FVector LevelOrigin)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(ExportDir, TEXT("play.json")))) { UE_LOG(LogOTGPack, Error, TEXT("No play.json in %s (export the course again)"), *ExportDir); return nullptr; }
	TSharedPtr<FJsonObject> Play;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Play) || !Play.IsValid()) { UE_LOG(LogOTGPack, Error, TEXT("play.json doesn't read")); return nullptr; }

	UPackage* Package = CreatePackage(*FPaths::Combine(PackagePath, AssetName));
	UOTGCourseData* D = NewObject<UOTGCourseData>(Package, *AssetName, RF_Public | RF_Standalone);
	D->CourseId = Play->GetStringField(TEXT("id"));
	D->CourseName = Play->GetStringField(TEXT("name"));
	Play->TryGetStringField(TEXT("location"), D->Location);
	Play->TryGetNumberField(TEXT("lat"), D->Latitude);
	Play->TryGetNumberField(TEXT("lon"), D->Longitude);
	Play->TryGetNumberField(TEXT("altitudeM"), D->AltitudeM);
	double Stimp = 10;
	Play->TryGetNumberField(TEXT("defaultStimp"), Stimp);
	D->DefaultStimp = Stimp;
	D->Level = TSoftObjectPtr<UWorld>(FSoftObjectPath(LevelPath));
	D->LevelOrigin = LevelOrigin;

	const TSharedPtr<FJsonObject> B = Play->GetObjectField(TEXT("bounds"));
	D->MinX = B->GetNumberField(TEXT("minX")); D->MaxX = B->GetNumberField(TEXT("maxX"));
	D->MinZ = B->GetNumberField(TEXT("minZ")); D->MaxZ = B->GetNumberField(TEXT("maxZ"));

	// the heightmap
	const TSharedPtr<FJsonObject> Hm = Play->GetObjectField(TEXT("heightmap"));
	D->HeightX0 = Hm->GetNumberField(TEXT("x0")); D->HeightZ1 = Hm->GetNumberField(TEXT("z1"));
	D->MetresPerSample = Hm->GetNumberField(TEXT("metresPerSample"));
	D->HeightBase = Hm->GetNumberField(TEXT("heightBaseMetres")); D->HeightRange = Hm->GetNumberField(TEXT("heightRangeMetres"));
	{
		int32 W = 0, H = 0;
		TArray64<uint8> Raw;
		if (!ReadGreyPng(FPaths::Combine(ExportDir, Hm->GetStringField(TEXT("file"))), 16, W, H, Raw)) return nullptr;
		D->HeightSamples = W;
		D->Heights.SetNumUninitialized(W * H);
		FMemory::Memcpy(D->Heights.GetData(), Raw.GetData(), D->Heights.Num() * sizeof(uint16));
	}
	// the surfaces
	const TSharedPtr<FJsonObject> Sf = Play->GetObjectField(TEXT("surfaces"));
	D->SurfaceMinX = Sf->GetNumberField(TEXT("minX")); D->SurfaceMaxZ = Sf->GetNumberField(TEXT("maxZ")); D->SurfaceRes = Sf->GetNumberField(TEXT("res"));
	{
		int32 W = 0, H = 0;
		TArray64<uint8> Raw;
		if (!ReadGreyPng(FPaths::Combine(ExportDir, Sf->GetStringField(TEXT("file"))), 8, W, H, Raw)) return nullptr;
		D->SurfaceW = W; D->SurfaceH = H;
		D->Surfaces = TArray<uint8>(Raw.GetData(), static_cast<int32>(Raw.Num()));
	}
	// holes
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
		D->Holes.Add(Hole);
	}
	// trees: [x, z, trunkTop, trunkR, kind, cone, a, b, c]
	for (const TSharedPtr<FJsonValue>& V : Play->GetArrayField(TEXT("trees")))
	{
		const TArray<TSharedPtr<FJsonValue>>& T = V->AsArray();
		if (T.Num() < 9) continue;
		FOTGTreeShape S;
		S.X = T[0]->AsNumber(); S.Z = T[1]->AsNumber(); S.TrunkTop = T[2]->AsNumber(); S.TrunkR = T[3]->AsNumber();
		S.Kind = T[4]->AsString(); S.bCone = T[5]->AsNumber() != 0;
		S.A = T[6]->AsNumber(); S.B = T[7]->AsNumber(); S.C = T[8]->AsNumber();
		D->Trees.Add(S);
	}
	for (const TSharedPtr<FJsonValue>& V : Play->GetArrayField(TEXT("solids")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FOTGSolid S;
		S.Height = O->GetNumberField(TEXT("h"));
		S.Kind = O->GetStringField(TEXT("kind"));
		for (const TSharedPtr<FJsonValue>& P : O->GetArrayField(TEXT("pts"))) { const auto& XZ = P->AsArray(); S.Footprint.Add(FVector2D(XZ[0]->AsNumber(), XZ[1]->AsNumber())); }
		D->Solids.Add(S);
	}
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
		D->DropZones.Add(Z);
	}

	FAssetRegistryModule::AssetCreated(D);
	D->MarkPackageDirty();
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	if (!UPackage::SavePackage(Package, D, *File, Args)) { UE_LOG(LogOTGPack, Error, TEXT("Couldn't save %s"), *File); return nullptr; }
	UE_LOG(LogOTGPack, Display, TEXT("Course data: %s, %d holes, %d trees, heights %d^2, surfaces %dx%d"), *D->CourseName, D->Holes.Num(), D->Trees.Num(), D->HeightSamples, D->SurfaceW, D->SurfaceH);
	return D;
}
