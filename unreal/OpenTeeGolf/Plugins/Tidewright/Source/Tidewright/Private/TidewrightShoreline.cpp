// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "TidewrightShoreline.h"

#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TidewrightWater.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TidewrightShoreline)

ATidewrightShoreline::ATidewrightShoreline()
{
	PrimaryActorTick.bCanEverTick = false;

	Area = CreateDefaultSubobject<UBoxComponent>(TEXT("Area"));
	Area->SetBoxExtent(FVector(15000.0, 15000.0, 3000.0));
	Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Area->SetGenerateOverlapEvents(false);
	Area->SetCanEverAffectNavigation(false);
	Area->ShapeColor = FColor(60, 200, 230);
	SetRootComponent(Area);
}

void ATidewrightShoreline::BeginPlay()
{
	Super::BeginPlay();
	if (bRebuildOnBeginPlay || !HasScan())
	{
		RebuildShoreline();
	}
}

void ATidewrightShoreline::RebuildShoreline()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

#if WITH_EDITOR
	Modify();
#endif

	// The scan covers the box's footprint. A rotated box scans the axis-aligned rectangle around it.
	const FBox Bounds = Area->Bounds.GetBox();
	const int32 Size = FMath::Clamp(ScanResolution, 32, 1024);
	ScanSize = Size;
	ScanCentre = FVector2D(Bounds.GetCenter());
	ScanHalfSize = FVector2D(Bounds.GetExtent());
	TerrainHeights.SetNumUninitialized(Size * Size);

	FCollisionQueryParams Query(SCENE_QUERY_STAT(TidewrightShoreScan), true);
	Query.AddIgnoredActor(this);
	for (TActorIterator<ATidewrightWater> It(World); It; ++It)
	{
		Query.AddIgnoredActor(*It);
	}
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);

	const double Top = Bounds.Max.Z;
	const double Bottom = Bounds.Min.Z;
	int32 Hits = 0;
	for (int32 Y = 0; Y < Size; ++Y)
	{
		const double WorldY = Bounds.Min.Y + (Y + 0.5) * (Bounds.Max.Y - Bounds.Min.Y) / Size;
		for (int32 X = 0; X < Size; ++X)
		{
			const double WorldX = Bounds.Min.X + (X + 0.5) * (Bounds.Max.X - Bounds.Min.X) / Size;
			FHitResult Hit;
			float Height = static_cast<float>(Bottom);
			if (World->LineTraceSingleByObjectType(Hit, FVector(WorldX, WorldY, Top), FVector(WorldX, WorldY, Bottom), Objects, Query))
			{
				Height = static_cast<float>(Hit.ImpactPoint.Z);
				++Hits;
			}
			TerrainHeights[Y * Size + X] = Height;
		}
	}

	Field.Reset();
	++FieldVersion;

	if (Hits == 0)
	{
		ScanStatus = TEXT("Scanned, but found no ground inside the box. The ground needs collision, and the box must reach above and below the waterline.");
	}
	else
	{
		ScanStatus = FString::Printf(TEXT("Scanned %d x %d samples over %.0f x %.0f m. Ground found under %d%% of them."),
			Size, Size, ScanHalfSize.X * 0.02, ScanHalfSize.Y * 0.02, FMath::RoundToInt32(100.0f * Hits / (Size * Size)));
	}
#if WITH_EDITOR
	MarkPackageDirty();
#endif
}

double ATidewrightShoreline::DistanceToArea(const FVector& WorldLocation) const
{
	const double Dx = FMath::Max(FMath::Abs(WorldLocation.X - ScanCentre.X) - ScanHalfSize.X, 0.0);
	const double Dy = FMath::Max(FMath::Abs(WorldLocation.Y - ScanCentre.Y) - ScanHalfSize.Y, 0.0);
	return FMath::Sqrt(Dx * Dx + Dy * Dy);
}

const FTidewrightShoreField* ATidewrightShoreline::GetField(double WaterLevelCm)
{
	if (!HasScan())
	{
		return nullptr;
	}
	if (!Field.IsValid() || !FMath::IsNearlyEqual(Field.GetWaterLevelCm(), WaterLevelCm, 0.01))
	{
		Field.Build(TerrainHeights, ScanSize, ScanCentre, ScanHalfSize, WaterLevelCm);
		++FieldVersion;
	}
	return Field.IsValid() ? &Field : nullptr;
}
