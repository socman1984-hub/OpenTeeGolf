// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "GreenswardEditorLibrary.h"
#include "GreenswardEditorPrivate.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInterface.h"

namespace GreenswardLandscapePrivate
{
	/** 0 outside, 1 inside, with a soft edge. Distances in cm. */
	static float PatchMask(const FGreenswardLandscapePatch& Patch, const FVector2D& World, float Feather, float Noise)
	{
		const FVector2D Delta = World - Patch.Centre;
		const float Angle = FMath::DegreesToRadians(Patch.RotationDegrees);
		const float C = FMath::Cos(Angle);
		const float S = FMath::Sin(Angle);
		const FVector2D Local(Delta.X * C + Delta.Y * S, -Delta.X * S + Delta.Y * C);
		const FVector2D Half(FMath::Max(Patch.HalfSize.X, 1.0), FMath::Max(Patch.HalfSize.Y, 1.0));

		float Distance;
		if (Patch.bEllipse)
		{
			const float R = FMath::Sqrt(FMath::Square(Local.X / Half.X) + FMath::Square(Local.Y / Half.Y));
			Distance = (R - 1.0f) * FMath::Min(Half.X, Half.Y);
		}
		else
		{
			Distance = FMath::Max(FMath::Abs(Local.X) - Half.X, FMath::Abs(Local.Y) - Half.Y);
		}
		Distance += (Noise - 0.5f) * Patch.EdgeBreakup * Feather * 6.0f;
		return FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(0.5f - Distance / FMath::Max(Feather, 1.0f), 0.0f, 1.0f));
	}
}

ALandscape* UGreenswardEditorLibrary::CreateLandscape(UObject* WorldContextObject, UMaterialInterface* Material, const TArray<FGreenswardLandscapePatch>& Patches, int32 ComponentsPerSide, float QuadSize, float HillHeight, float NaturalVariation, int32 Seed)
{
	using namespace GreenswardLandscapePrivate;

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}
	if (!Material)
	{
		Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Greensward/Materials/MI_Greensward_Landscape.MI_Greensward_Landscape"));
	}

	ComponentsPerSide = FMath::Clamp(ComponentsPerSide, 1, 32);
	QuadSize = FMath::Clamp(QuadSize, 10.0f, 1000.0f);
	const int32 QuadsPerSection = 63;
	const int32 SectionsPerComponent = 1;
	const int32 Size = ComponentsPerSide * QuadsPerSection * SectionsPerComponent + 1;
	const float ScaleZ = 100.0f;
	const float HalfExtent = (Size - 1) * QuadSize * 0.5f;
	const float Extent = HalfExtent * 2.0f;

	const TArray<FName>& LayerNames = GreenswardEditor::GetLandscapeLayerNames();
	const int32 NumLayers = LayerNames.Num();
	const int32 DryIndex = LayerNames.IndexOfByKey(FName(TEXT("DryGrass")));
	const int32 SoilIndex = LayerNames.IndexOfByKey(FName(TEXT("Soil")));

	auto WorldAt = [&](int32 X, int32 Y)
	{
		return FVector2D(-HalfExtent + X * QuadSize, -HalfExtent + Y * QuadSize);
	};
	// Terrain shape in cm, as a function of world position, so features keep their size when the landscape grows.
	auto BaseHeight = [&](const FVector2D& P)
	{
		const float U = P.X / 20000.0f + 37.0f;
		const float V = P.Y / 20000.0f + 53.0f;
		const float Broad = GreenswardEditor::Fbm(U, V, 2, 3, Seed, false) - 0.5f;
		const float Fine = GreenswardEditor::Fbm(U, V, 9, 3, Seed + 17, false) - 0.5f;
		return HillHeight * (Broad * 2.0f + Fine * 0.25f);
	};

	TArray<float> HeightsCm;
	HeightsCm.SetNumUninitialized(Size * Size);
	TArray<float> Weights;
	Weights.SetNumZeroed(Size * Size * NumLayers);

	NaturalVariation = FMath::Clamp(NaturalVariation, 0.0f, 1.0f);
	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const FVector2D P = WorldAt(X, Y);
			const int32 Index = Y * Size + X;
			float Height = BaseHeight(P);

			float* W = &Weights[Index * NumLayers];
			W[0] = 1.0f;

			// Scattered dry grass and bare soil over the base grass.
			const float NU = P.X / 6000.0f + 11.0f;
			const float NV = P.Y / 6000.0f + 23.0f;
			const float DryNoise = GreenswardEditor::Fbm(NU, NV, 3, 5, Seed + 31, false);
			const float SoilNoise = GreenswardEditor::Fbm(NU * 1.7f, NV * 1.7f, 3, 5, Seed + 47, false);
			const float Dry = FMath::SmoothStep(0.52f, 0.7f, DryNoise) * 0.75f * NaturalVariation;
			const float Soil = FMath::SmoothStep(0.62f, 0.76f, SoilNoise) * 0.7f * NaturalVariation;
			if (DryIndex != INDEX_NONE)
			{
				W[0] *= 1.0f - Dry;
				W[DryIndex] += Dry;
			}
			if (SoilIndex != INDEX_NONE)
			{
				for (int32 L = 0; L < NumLayers; ++L)
				{
					W[L] *= 1.0f - Soil;
				}
				W[SoilIndex] += Soil;
			}

			const float EdgeNoise = GreenswardEditor::Fbm(P.X / 700.0f + 5.0f, P.Y / 700.0f + 9.0f, 2, 4, Seed + 61, false);
			for (const FGreenswardLandscapePatch& Patch : Patches)
			{
				if (Patch.bLevel)
				{
					// A wide, smooth bank so the level area meets the hills without a step.
					const float Bank = PatchMask(Patch, P, Patch.Feather * 4.0f + 400.0f, 0.5f);
					Height = FMath::Lerp(Height, BaseHeight(Patch.Centre), Bank);
				}

				const float Mask = PatchMask(Patch, P, Patch.Feather, EdgeNoise);
				if (Mask <= 0.0f)
				{
					continue;
				}
				Height += Patch.HeightOffset * Mask;

				const int32 LayerIndex = LayerNames.IndexOfByKey(Patch.Layer);
				if (LayerIndex != INDEX_NONE)
				{
					const float Amount = Mask * FMath::Clamp(Patch.Strength, 0.0f, 1.0f);
					for (int32 L = 0; L < NumLayers; ++L)
					{
						W[L] *= 1.0f - Amount;
					}
					W[LayerIndex] += Amount;
				}
			}
			HeightsCm[Index] = Height;
		}
	}

	TArray<uint16> Heights;
	Heights.SetNumUninitialized(Size * Size);
	const float UnitsPerCm = 128.0f / ScaleZ;
	for (int32 I = 0; I < Heights.Num(); ++I)
	{
		Heights[I] = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt32(32768.0f + HeightsCm[I] * UnitsPerCm), 0, 65535));
	}

	TArray<FLandscapeImportLayerInfo> ImportLayers;
	TArray<ULandscapeLayerInfoObject*> LayerInfos;
	for (int32 L = 0; L < NumLayers; ++L)
	{
		const FString Path = FString::Printf(TEXT("/Greensward/Landscape/LI_GS_%s.LI_GS_%s"), *LayerNames[L].ToString(), *LayerNames[L].ToString());
		ULandscapeLayerInfoObject* Info = LoadObject<ULandscapeLayerInfoObject>(nullptr, *Path);
		LayerInfos.Add(Info);
		if (!Info)
		{
			UE_LOG(LogGreenswardEditor, Warning, TEXT("CreateLandscape: missing layer info %s. Run BuildCoreAssets first."), *Path);
			continue;
		}

		FLandscapeImportLayerInfo& Import = ImportLayers.Emplace_GetRef(LayerNames[L]);
		Import.LayerInfo = Info;
		Import.LayerData.SetNumUninitialized(Size * Size);
	}
	// Normalise so the layers of each vertex sum to 255.
	for (int32 I = 0; I < Size * Size; ++I)
	{
		const float* W = &Weights[I * NumLayers];
		float Sum = 0.0f;
		for (int32 L = 0; L < NumLayers; ++L)
		{
			Sum += LayerInfos[L] ? W[L] : 0.0f;
		}
		int32 Remaining = 255;
		int32 Slot = 0;
		for (int32 L = 0; L < NumLayers; ++L)
		{
			if (!LayerInfos[L])
			{
				continue;
			}
			const bool bLast = Slot == ImportLayers.Num() - 1;
			const int32 Value = bLast ? Remaining : FMath::Clamp(FMath::RoundToInt32(255.0f * W[L] / FMath::Max(Sum, 1e-4f)), 0, Remaining);
			ImportLayers[Slot].LayerData[I] = static_cast<uint8>(Value);
			Remaining -= Value;
			++Slot;
		}
	}

	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
	HeightDataPerLayers.Add(FGuid(), MoveTemp(Heights));
	MaterialLayerDataPerLayers.Add(FGuid(), ImportLayers);

	ALandscape* Landscape = World->SpawnActor<ALandscape>(FVector(-HalfExtent, -HalfExtent, 0.0f), FRotator::ZeroRotator);
	if (!Landscape)
	{
		return nullptr;
	}
	Landscape->LandscapeMaterial = Material;
	Landscape->SetActorRelativeScale3D(FVector(QuadSize, QuadSize, ScaleZ));
	Landscape->StaticLightingLOD = 0;

	Landscape->Import(FGuid::NewGuid(), 0, 0, Size - 1, Size - 1, SectionsPerComponent, QuadsPerSection,
		HeightDataPerLayers, TEXT(""), MaterialLayerDataPerLayers, ELandscapeImportAlphamapType::Additive,
		TArrayView<const FLandscapeLayer>());

	if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
	{
		Info->UpdateLayerInfoMap(Landscape);
	}
	for (ULandscapeLayerInfoObject* Info : LayerInfos)
	{
		if (Info)
		{
			Landscape->AddTargetLayer(Info->GetLayerName(), FLandscapeTargetLayerSettings(Info));
		}
	}

	Landscape->SetActorLabel(TEXT("GreenswardLandscape"));
	Landscape->PostEditChange();
	UE_LOG(LogGreenswardEditor, Log, TEXT("Created landscape: %d x %d vertices, %.0f m across."), Size, Size, Extent / 100.0f);
	return Landscape;
}
