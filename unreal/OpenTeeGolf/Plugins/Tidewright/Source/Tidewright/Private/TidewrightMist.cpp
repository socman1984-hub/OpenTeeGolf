// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "TidewrightMist.h"

#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "TextureResource.h"
#include "TidewrightRiver.h"
#include "TidewrightWater.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TidewrightMist)

ATidewrightMist::ATidewrightMist()
{
	PrimaryActorTick.bCanEverTick = true;

	Area = CreateDefaultSubobject<UBoxComponent>(TEXT("Area"));
	Area->SetBoxExtent(FVector(10000.0, 10000.0, 1500.0));
	Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Area->SetGenerateOverlapEvents(false);
	Area->SetCanEverAffectNavigation(false);
	Area->ShapeColor = FColor(200, 210, 220);
	SetRootComponent(Area);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MistFinder(TEXT("/Tidewright/Materials/M_Tidewright_Mist.M_Tidewright_Mist"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	MistMaterial = MistFinder.Object;
	VolumeMesh = CubeFinder.Object;
}

void ATidewrightMist::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	EnsureVolume();
}

void ATidewrightMist::BeginPlay()
{
	Super::BeginPlay();
	if (bRebuildOnBeginPlay)
	{
		RebuildMist();
	}
}

void ATidewrightMist::EnsureVolume()
{
	UWorld* World = GetWorld();
	if (!World || IsTemplate() || IsRunningDedicatedServer() || !IsValid(this) || IsActorBeingDestroyed())
	{
		return;
	}
	if (World->WorldType == EWorldType::Inactive || World->WorldType == EWorldType::None)
	{
		return;
	}
	if (!Volume)
	{
		UStaticMesh* Cube = VolumeMesh;
		if (!Cube)
		{
			return;
		}
		Volume = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Volume->SetMobility(EComponentMobility::Movable);
		Volume->SetupAttachment(Area);
		Volume->SetUsingAbsoluteLocation(true);
		Volume->SetUsingAbsoluteRotation(true);
		Volume->SetUsingAbsoluteScale(true);
		Volume->SetStaticMesh(Cube);
		Volume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Volume->SetGenerateOverlapEvents(false);
		Volume->SetCastShadow(false);
		Volume->SetCanEverAffectNavigation(false);
		Volume->bAffectDistanceFieldLighting = false;
		Volume->RegisterComponent();
		VolumeMaterial = nullptr;
	}
	if (!VolumeMaterial)
	{
		if (UMaterialInterface* Base = MistMaterial)
		{
			VolumeMaterial = UMaterialInstanceDynamic::Create(Base, this);
			Volume->SetMaterial(0, VolumeMaterial);
			bSurfaceDirty = true;
		}
	}
}

void ATidewrightMist::RebuildMist()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
#if WITH_EDITOR
	Modify();
#endif

	// A rotated box scans the upright box around it.
	const FBox Bounds = Area->Bounds.GetBox();
	const int32 Size = FMath::Clamp(ScanResolution, 16, 512);
	ScanSize = Size;
	ScanCentre = Bounds.GetCenter();
	ScanExtent = Bounds.GetExtent();
	SurfaceHeights.SetNumUninitialized(Size * Size);

	FCollisionQueryParams Query(SCENE_QUERY_STAT(TidewrightMistScan), true);
	Query.AddIgnoredActor(this);
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);

	// Still water inside the box counts as surface too. Waves are ignored: mist lies on the rest level.
	const ATidewrightWater* Water = ATidewrightWater::GetTidewrightWater(this);
	const double WaterLevel = Water ? Water->GetActorLocation().Z : -TNumericLimits<double>::Max();

	int32 GroundHits = 0, WaterCells = 0;
	for (int32 Y = 0; Y < Size; ++Y)
	{
		const double WorldY = Bounds.Min.Y + (Y + 0.5) * (Bounds.Max.Y - Bounds.Min.Y) / Size;
		for (int32 X = 0; X < Size; ++X)
		{
			const double WorldX = Bounds.Min.X + (X + 0.5) * (Bounds.Max.X - Bounds.Min.X) / Size;
			double Height = Bounds.Min.Z;
			FHitResult Hit;
			if (World->LineTraceSingleByObjectType(Hit, FVector(WorldX, WorldY, Bounds.Max.Z), FVector(WorldX, WorldY, Bounds.Min.Z), Objects, Query))
			{
				Height = Hit.ImpactPoint.Z;
				++GroundHits;
			}
			bool bWater = false;
			if (Water && WaterLevel > Height && WaterLevel < Bounds.Max.Z)
			{
				FVector Surface, Normal;
				if (Water->Mode == ETidewrightWaterMode::Ocean || Water->SampleWaterSurface(FVector(WorldX, WorldY, 0.0), Surface, Normal))
				{
					Height = WaterLevel;
					bWater = true;
				}
			}
			FVector RiverSurface, RiverNormal, RiverVelocity;
			if (ATidewrightRiver::SampleAnyRiver(World, FVector(WorldX, WorldY, Height), RiverSurface, RiverNormal, RiverVelocity) && RiverSurface.Z > Height && RiverSurface.Z < Bounds.Max.Z)
			{
				Height = RiverSurface.Z;
				bWater = true;
			}
			WaterCells += bWater ? 1 : 0;
			SurfaceHeights[Y * Size + X] = static_cast<float>((Height - Bounds.Min.Z) * 0.01);
		}
	}

	// Soften the scan so the mist does not show the steps between samples.
	TArray<float> Blurred;
	Blurred.SetNumUninitialized(Size * Size);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				float Sum = 0.0f;
				for (int32 Dy = -1; Dy <= 1; ++Dy)
				{
					for (int32 Dx = -1; Dx <= 1; ++Dx)
					{
						Sum += SurfaceHeights[FMath::Clamp(Y + Dy, 0, Size - 1) * Size + FMath::Clamp(X + Dx, 0, Size - 1)];
					}
				}
				Blurred[Y * Size + X] = Sum / 9.0f;
			}
		}
		Swap(SurfaceHeights, Blurred);
	}

	LowestSurface = FMath::Min(SurfaceHeights);
	ScanStatus = FString::Printf(TEXT("Scanned %d x %d samples over %.0f x %.0f m. Ground under %d%%, water under %d%%."),
		Size, Size, ScanExtent.X * 0.02, ScanExtent.Y * 0.02,
		FMath::RoundToInt32(100.0f * GroundHits / (Size * Size)), FMath::RoundToInt32(100.0f * WaterCells / (Size * Size)));
	bSurfaceDirty = true;
#if WITH_EDITOR
	MarkPackageDirty();
#endif
}

float ATidewrightMist::GetSurfaceHeightAt(const FVector& WorldLocation) const
{
	const FBox Bounds = Area->Bounds.GetBox();
	if (!HasScan())
	{
		return static_cast<float>(Bounds.Min.Z);
	}
	const double U = (WorldLocation.X - ScanCentre.X) / (2.0 * FMath::Max(ScanExtent.X, 1.0)) + 0.5;
	const double V = (WorldLocation.Y - ScanCentre.Y) / (2.0 * FMath::Max(ScanExtent.Y, 1.0)) + 0.5;
	const int32 X = FMath::Clamp(FMath::FloorToInt32(U * ScanSize), 0, ScanSize - 1);
	const int32 Y = FMath::Clamp(FMath::FloorToInt32(V * ScanSize), 0, ScanSize - 1);
	return static_cast<float>(ScanCentre.Z - ScanExtent.Z + SurfaceHeights[Y * ScanSize + X] * 100.0);
}

void ATidewrightMist::UploadSurface()
{
	bSurfaceDirty = false;
	if (!VolumeMaterial)
	{
		return;
	}
	if (!HasScan())
	{
		VolumeMaterial->SetScalarParameterValue(TEXT("TW_MistScan"), 0.0f);
		return;
	}
	if (!SurfaceTexture || SurfaceTexture->GetSizeX() != ScanSize)
	{
		SurfaceTexture = UTexture2D::CreateTransient(ScanSize, ScanSize, PF_A32B32G32R32F);
		SurfaceTexture->SRGB = false;
		SurfaceTexture->CompressionSettings = TC_HDR;
		SurfaceTexture->Filter = TF_Bilinear;
		SurfaceTexture->AddressX = TA_Clamp;
		SurfaceTexture->AddressY = TA_Clamp;
		SurfaceTexture->NeverStream = true;
	}
	{
		FTexture2DMipMap& Mip = SurfaceTexture->GetPlatformData()->Mips[0];
		FVector4f* Data = static_cast<FVector4f*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Index = 0; Index < SurfaceHeights.Num(); ++Index)
		{
			Data[Index] = FVector4f(SurfaceHeights[Index], 0.0f, 0.0f, 1.0f);
		}
		Mip.BulkData.Unlock();
	}
	SurfaceTexture->UpdateResource();
	VolumeMaterial->SetTextureParameterValue(TEXT("MistSurface"), SurfaceTexture);
	VolumeMaterial->SetScalarParameterValue(TEXT("TW_MistScan"), 1.0f);
}

void ATidewrightMist::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	EnsureVolume();
	if (!Volume || !VolumeMaterial)
	{
		return;
	}
	if (bSurfaceDirty)
	{
		UploadSurface();
	}

	// With a scan the mist stays where it was scanned; without one it simply fills the box.
	const FBox Bounds = Area->Bounds.GetBox();
	const FVector Centre = HasScan() ? ScanCentre : Bounds.GetCenter();
	const FVector Extent = HasScan() ? ScanExtent : Bounds.GetExtent();
	Volume->SetWorldLocationAndRotation(Centre, FQuat::Identity);
	Volume->SetWorldScale3D(Extent / 50.0);

	const float Radians = FMath::DegreesToRadians(Mist.DriftDirection);
	Drift += FVector2D(FMath::Cos(Radians), FMath::Sin(Radians)) * (Mist.DriftSpeed * DeltaSeconds);
	// Keep the offset small so single precision stays smooth over long sessions; the pattern repeats anyway.
	const double Wrap = FMath::Max(Mist.PatchSize, 2.0f) * 64.0;
	Drift.X = FMath::Fmod(Drift.X, Wrap);
	Drift.Y = FMath::Fmod(Drift.Y, Wrap);

	VolumeMaterial->SetVectorParameterValue(TEXT("TW_MistCentre"), FLinearColor(static_cast<float>(Centre.X), static_cast<float>(Centre.Y), static_cast<float>(Centre.Z), 0.0f));
	VolumeMaterial->SetVectorParameterValue(TEXT("TW_MistExtent"), FLinearColor(static_cast<float>(Extent.X), static_cast<float>(Extent.Y), static_cast<float>(Extent.Z), 0.0f));
	VolumeMaterial->SetVectorParameterValue(TEXT("TW_MistA"), FLinearColor(Mist.Density, FMath::Max(Mist.Thickness, 0.05f), Mist.Patchiness, FMath::Max(Mist.PatchSize, 1.0f)));
	VolumeMaterial->SetVectorParameterValue(TEXT("TW_MistDrift"), FLinearColor(static_cast<float>(Drift.X), static_cast<float>(Drift.Y), 0.0f, 0.0f));
	VolumeMaterial->SetVectorParameterValue(TEXT("TW_MistB"), FLinearColor(LowestSurface, HasScan() ? Mist.PoolingHeight : 0.0f, 0.0f, 0.0f));
	VolumeMaterial->SetVectorParameterValue(TEXT("TW_MistColor"), Mist.Color * Mist.Brightness);
}
