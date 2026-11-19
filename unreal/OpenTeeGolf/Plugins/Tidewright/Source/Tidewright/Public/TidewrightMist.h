// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TidewrightMist.generated.h"

class UBoxComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTexture2D;

/** How a patch of mist looks and moves. Lengths are in metres. */
USTRUCT(BlueprintType)
struct TIDEWRIGHT_API FTidewrightMistSettings
{
	GENERATED_BODY()

	/** How thick the mist is at the surface. At 1, things 100 metres away through the mist are almost hidden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Mist", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float Density = 1.0f;

	/** Height above the ground or water at which the mist has thinned to about a third. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Mist", meta = (ClampMin = "0.1", ClampMax = "200.0", Units = "m"))
	float Thickness = 2.5f;

	/** Makes the mist gather in low ground. Ground this far above the lowest scanned point holds about a third as much mist. 0 spreads it evenly over all ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Mist", meta = (ClampMin = "0.0", ClampMax = "500.0", Units = "m"))
	float PoolingHeight = 0.0f;

	/** 0 is an even layer; 1 breaks it into drifting banks with clear gaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Mist", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Patchiness = 0.7f;

	/** Size of the banks of mist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mist", meta = (ClampMin = "2.0", ClampMax = "2000.0", Units = "m"))
	float PatchSize = 60.0f;

	/** Speed at which the mist drifts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Drift", meta = (ClampMin = "0.0", ClampMax = "30.0", Units = "m/s"))
	float DriftSpeed = 0.8f;

	/** Direction the mist drifts towards, in degrees around the world Z axis (0 = +X). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Drift", meta = (ClampMin = "-360.0", ClampMax = "360.0", Units = "deg"))
	float DriftDirection = 20.0f;

	/** Colour of the mist. It is not lit by the scene, so set it to suit the time of day. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Mist")
	FLinearColor Color = FLinearColor(0.62f, 0.68f, 0.74f, 1.0f);

	/** Brightness multiplier on the colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Mist", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float Brightness = 1.0f;
};

/**
 * A layer of mist that lies on whatever is beneath it.
 *
 * Place the box and press Rebuild Mist: the ground, the sea and any rivers inside it are scanned, and the mist
 * then follows them, pooling in valleys and over water and thinning with height. It drifts and breaks into banks.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Tidewright Mist"))
class TIDEWRIGHT_API ATidewrightMist : public AActor
{
	GENERATED_BODY()

public:
	ATidewrightMist();

	/** Thickness, patchiness, drift and colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Tidewright", meta = (ShowOnlyInnerProperties))
	FTidewrightMistSettings Mist;

	/** Number of surface samples along each side of the box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright|Scan", meta = (ClampMin = "16", ClampMax = "512"))
	int32 ScanResolution = 128;

	/** Scan again when play begins. Turn on if the terrain is generated or changed at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright|Scan")
	bool bRebuildOnBeginPlay = false;

	/** Result of the last scan. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tidewright|Scan")
	FString ScanStatus = TEXT("Not scanned yet: the mist lies flat on the bottom of the box. Press Rebuild Mist to make it follow the ground.");

	/** Material used to draw the mist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright|Advanced")
	TObjectPtr<UMaterialInterface> MistMaterial;

	/** Scans the ground and water inside the box so the mist can lie on them. Press again after moving the box or editing the terrain. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Tidewright")
	void RebuildMist();

	/** True once the surface under the mist has been scanned. */
	UFUNCTION(BlueprintPure, Category = "Tidewright")
	bool HasScan() const { return ScanSize > 0 && SurfaceHeights.Num() == ScanSize * ScanSize; }

	/** Height of the scanned surface under a world location (world Z), or the bottom of the box where nothing was scanned. */
	UFUNCTION(BlueprintPure, Category = "Tidewright")
	float GetSurfaceHeightAt(const FVector& WorldLocation) const;

	//~ AActor
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
	virtual void PostRegisterAllComponents() override;

private:
	void EnsureVolume();
	void UploadSurface();

	UPROPERTY(VisibleAnywhere, Category = "Tidewright")
	TObjectPtr<UBoxComponent> Area;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Volume;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> VolumeMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> SurfaceTexture;

	UPROPERTY()
	TObjectPtr<class UStaticMesh> VolumeMesh;

	/** Scanned surface heights in metres above the bottom of the scanned box, saved with the level. */
	UPROPERTY()
	TArray<float> SurfaceHeights;

	UPROPERTY()
	int32 ScanSize = 0;

	/** Lowest scanned surface, metres above the bottom of the box. */
	UPROPERTY()
	float LowestSurface = 0.0f;

	UPROPERTY()
	FVector ScanCentre = FVector::ZeroVector;

	UPROPERTY()
	FVector ScanExtent = FVector::ZeroVector;

	FVector2D Drift = FVector2D::ZeroVector;
	bool bSurfaceDirty = true;
};
