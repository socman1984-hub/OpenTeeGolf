// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TidewrightSettings.h"
#include "TidewrightShoreField.h"
#include "TidewrightShoreline.generated.h"

class UBoxComponent;

/**
 * Makes the water react to a stretch of coast.
 *
 * Place the box over the coast and press Rebuild Shoreline. The ground inside the box is scanned from above,
 * and from that the water gets shallow-water wave damping, surf that rolls in and breaks, run-up on the beach
 * and foam along the water's edge. Works with landscapes and any mesh that has collision.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Tidewright Shoreline"))
class TIDEWRIGHT_API ATidewrightShoreline : public AActor
{
	GENERATED_BODY()

public:
	ATidewrightShoreline();

	/** How the water behaves along this coast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Tidewright", meta = (ShowOnlyInnerProperties))
	FTidewrightShoreSettings Shore;

	/** Number of ground samples along each side of the box. More samples follow a detailed coast more closely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright|Scan", meta = (ClampMin = "32", ClampMax = "1024"))
	int32 ScanResolution = 256;

	/** Scan again when play begins. Turn on if the terrain is generated or changed at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright|Scan")
	bool bRebuildOnBeginPlay = false;

	/** Result of the last scan. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tidewright|Scan")
	FString ScanStatus = TEXT("Not scanned yet. Press Rebuild Shoreline.");

	/** Scans the ground inside the box. Press again after moving the box or editing the terrain. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Tidewright")
	void RebuildShoreline();

	/** True once the ground has been scanned. */
	UFUNCTION(BlueprintPure, Category = "Tidewright")
	bool HasScan() const { return ScanSize > 0 && TerrainHeights.Num() == ScanSize * ScanSize; }

	/** Horizontal distance from a world location to the scanned rectangle, 0 inside it. */
	double DistanceToArea(const FVector& WorldLocation) const;

	/** The depth field for the given water level, built on demand. Null before the first scan. */
	const FTidewrightShoreField* GetField(double WaterLevelCm);

	/** Changes whenever the field's contents change. */
	uint32 GetFieldVersion() const { return FieldVersion; }

	UBoxComponent* GetArea() const { return Area; }

	//~ AActor
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Tidewright")
	TObjectPtr<UBoxComponent> Area;

	/** Scanned ground heights (world Z, centimetres), saved with the level. */
	UPROPERTY()
	TArray<float> TerrainHeights;

	UPROPERTY()
	int32 ScanSize = 0;

	UPROPERTY()
	FVector2D ScanCentre = FVector2D::ZeroVector;

	UPROPERTY()
	FVector2D ScanHalfSize = FVector2D::ZeroVector;

	FTidewrightShoreField Field;
	uint32 FieldVersion = 0;
};
