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
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Internationalization/Regex.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGPack, Log, All);

using otg::ReadGreyPng;

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
	UPackage* Package = CreatePackage(*FPaths::Combine(PackagePath, AssetName));
	UOTGCourseData* D = NewObject<UOTGCourseData>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!D->LoadFromExport(ExportDir)) return nullptr;
	D->Level = TSoftObjectPtr<UWorld>(FSoftObjectPath(LevelPath));
	D->LevelOrigin = LevelOrigin;

	FAssetRegistryModule::AssetCreated(D);
	D->MarkPackageDirty();
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	if (!UPackage::SavePackage(Package, D, *File, Args)) { UE_LOG(LogOTGPack, Error, TEXT("Couldn't save %s"), *File); return nullptr; }
	UE_LOG(LogOTGPack, Display, TEXT("Course data: %s, %d holes, %d trees, heights %d^2, surfaces %dx%d"), *D->CourseName, D->Holes.Num(), D->Trees.Num(), D->HeightSamples, D->SurfaceW, D->SurfaceH);
	return D;
}

int32 UOTGPackTools::GuardRayTracingLod(const FString& Folder)
{
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	AR.ScanPathsSynchronous({ Folder }, true);
	TArray<FAssetData> Assets;
	AR.GetAssetsByPath(FName(*Folder), Assets, true);
	int32 Changed = 0;
	for (const FAssetData& A : Assets)
	{
		if (!A.IsInstanceOf(UMaterial::StaticClass())) continue;
		UMaterial* M = Cast<UMaterial>(A.GetAsset());
		if (!M) continue;
		bool bMat = false;
		for (UMaterialExpression* E : M->GetExpressions())
		{
			UMaterialExpressionCustom* C = Cast<UMaterialExpressionCustom>(E);
			if (!C || !C->Code.Contains(TEXT("CalculateLevelOfDetail")) || C->Code.Contains(TEXT("RAYHITGROUPSHADER"))) continue;
			// each "float x = T.CalculateLevelOfDetail(...);" line: level 0 in ray-tracing shaders
			TArray<FString> Lines;
			C->Code.ParseIntoArrayLines(Lines, false);
			for (FString& L : Lines)
				if (L.Contains(TEXT("CalculateLevelOfDetail")))
				{
					const FString Trim = L.TrimStart();
					const FString Indent = L.Left(L.Len() - Trim.Len());
					int32 Eq = INDEX_NONE;
					if (!Trim.FindChar(TEXT('='), Eq)) continue;
					FString Decl = Trim.Left(Eq).TrimEnd();
					L = FString::Printf(TEXT("#if RAYHITGROUPSHADER\n%s%s = 0;\n#else\n%s%s\n#endif"), *Indent, *Decl, *Indent, *Trim);
				}
			C->Modify();
			C->Code = FString::Join(Lines, TEXT("\n"));
			bMat = true;
			Changed++;
		}
		if (bMat)
		{
			M->PreEditChange(nullptr);
			M->PostEditChange();
			M->MarkPackageDirty();
			UPackage* Pkg = M->GetOutermost();
			FSavePackageArgs Args;
			Args.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Pkg, M, *FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension()), Args);
			UE_LOG(LogTemp, Display, TEXT("Ray-tracing guard: %s"), *M->GetPathName());
		}
	}
	return Changed;
}
