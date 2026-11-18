// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGreenswardEditor, Log, All);

namespace GreenswardEditor
{
	/** Load the asset if its package exists, otherwise create it. */
	template <typename T>
	T* FindOrCreateAsset(const FString& PackagePath, const FString& AssetName)
	{
		const FString PackageName = PackagePath / AssetName;
		if (FPackageName::DoesPackageExist(PackageName))
		{
			if (T* Existing = LoadObject<T>(nullptr, *(PackageName + TEXT(".") + AssetName)))
			{
				return Existing;
			}
		}

		UPackage* Package = CreatePackage(*PackageName);
		if (T* InMemory = FindObject<T>(Package, *AssetName))
		{
			return InMemory;
		}
		T* Created = NewObject<T>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Created);
		return Created;
	}

	inline bool SaveAsset(UObject* Asset)
	{
		if (!Asset)
		{
			return false;
		}
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, Asset, *Filename, Args);
	}

	inline float Hash2D(int32 X, int32 Y, int32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u + static_cast<uint32>(Y) * 668265263u + static_cast<uint32>(Seed) * 2246822519u;
		H = (H ^ (H >> 13)) * 1274126177u;
		H ^= H >> 16;
		return static_cast<float>(H & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
	}

	/** Value noise in [0,1]. U,V in lattice units. Period > 0 makes it tile. */
	inline float ValueNoise(float U, float V, int32 Period, int32 Seed)
	{
		const int32 X0 = FMath::FloorToInt32(U);
		const int32 Y0 = FMath::FloorToInt32(V);
		float Fx = U - X0;
		float Fy = V - Y0;
		Fx = Fx * Fx * (3.0f - 2.0f * Fx);
		Fy = Fy * Fy * (3.0f - 2.0f * Fy);
		auto Wrap = [Period](int32 I) { return Period > 0 ? ((I % Period) + Period) % Period : I; };
		const float A = Hash2D(Wrap(X0), Wrap(Y0), Seed);
		const float B = Hash2D(Wrap(X0 + 1), Wrap(Y0), Seed);
		const float C = Hash2D(Wrap(X0), Wrap(Y0 + 1), Seed);
		const float D = Hash2D(Wrap(X0 + 1), Wrap(Y0 + 1), Seed);
		return FMath::Lerp(FMath::Lerp(A, B, Fx), FMath::Lerp(C, D, Fx), Fy);
	}

	/** Fractal noise in [0,1]. U,V in [0,1] when tiling. */
	inline float Fbm(float U, float V, int32 BasePeriod, int32 Octaves, int32 Seed, bool bTile)
	{
		float Sum = 0.0f;
		float Amp = 0.5f;
		float Norm = 0.0f;
		int32 Period = BasePeriod;
		for (int32 O = 0; O < Octaves; ++O)
		{
			Sum += Amp * ValueNoise(U * Period, V * Period, bTile ? Period : 0, Seed + O * 101);
			Norm += Amp;
			Amp *= 0.5f;
			Period *= 2;
		}
		return Sum / Norm;
	}
}

class UMaterialParameterCollection;
class UTexture2D;

namespace GreenswardEditor
{
	/** Names of the seven stock landscape layers, in blend order. */
	const TArray<FName>& GetLandscapeLayerNames();

	/** Author the layer textures, layer info assets and the layered landscape material. */
	void BuildLandscapeAssets(UMaterialParameterCollection* Collection, UTexture2D* Noise);

	/** Author the sports line-marking textures, decal material and instances. */
	void BuildMarkingAssets(UTexture2D* Noise);

	/** Tiling cellular noise. U,V in [0,1]. Returns distance to nearest (F1) and second nearest (F2) feature point in cell units, and a per-cell random. */
	void Cellular(float U, float V, int32 Period, int32 Seed, float& OutF1, float& OutF2, float& OutCellRandom);
}
