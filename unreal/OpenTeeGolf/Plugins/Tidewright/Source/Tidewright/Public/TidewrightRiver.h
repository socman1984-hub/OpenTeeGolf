// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TidewrightRiver.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class USplineComponent;

/** How a river looks and moves. Lengths are in metres. */
USTRUCT(BlueprintType)
struct TIDEWRIGHT_API FTidewrightRiverSettings
{
	GENERATED_BODY()

	/** Width of the river. Each spline point's Y scale multiplies it, so the river can widen and narrow along its length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River", meta = (ClampMin = "0.5", ClampMax = "500.0", Units = "m"))
	float Width = 12.0f;

	/** Speed of the current in mid-stream. The water slows towards the banks and speeds up where the river steepens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "River", meta = (ClampMin = "0.0", ClampMax = "15.0", Units = "m/s"))
	float FlowSpeed = 1.6f;

	/**
	 * How much faster the water runs where the river narrows, and slower where it widens, as the same water squeezes
	 * through. 0 keeps one speed along the whole river; 1 is full continuity (half the width, twice the speed).
	 * Measured against the river's typical width, and limited to between 0.65 and 1.6 times Flow Speed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "River", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NarrowsSpeedup = 0.7f;

	/** Size of the ripple pattern carried by the current. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (ClampMin = "0.2", ClampMax = "50.0", Units = "m"))
	float RippleSize = 5.0f;

	/** Strength of the ripples. Fast water is rougher by itself; this scales all of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Surface", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float RippleStrength = 1.0f;

	/** Roughness of the surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Surface", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Roughness = 0.06f;

	/** Width of the foam along each bank. 0 removes it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Foam", meta = (ClampMin = "0.0", ClampMax = "20.0", Units = "m"))
	float BankFoamWidth = 0.7f;

	/** Downhill slope at which the water turns white, as drop per metre of length. 0.06 is a brisk rapid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Foam", meta = (ClampMin = "0.005", ClampMax = "2.0"))
	float RapidsSlope = 0.06f;

	/** Overall foam strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Foam", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float FoamAmount = 1.0f;

	/** Colour of light scattered back out of the water. Rivers carry silt, so this is greener and brighter than the sea. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Water")
	FLinearColor ScatterColor = FLinearColor(0.012f, 0.022f, 0.012f, 1.0f);

	/** Light absorbed per metre, per colour channel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Water")
	FLinearColor Absorption = FLinearColor(0.7f, 0.3f, 0.45f, 1.0f);
};

/**
 * A river, stream or canal drawn along a spline.
 *
 * Shape the spline down the river bed; the water surface follows it, so unlike the sea a river can run downhill.
 * The current carries ripples and foam with it, slows at the banks, whitens where the bed steepens, and pushes
 * anything with a Tidewright Buoyancy component downstream.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Tidewright River"))
class TIDEWRIGHT_API ATidewrightRiver : public AActor
{
	GENERATED_BODY()

public:
	ATidewrightRiver();

	/** Width, current, ripples, foam and colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Tidewright", meta = (ShowOnlyInnerProperties))
	FTidewrightRiverSettings River;

	/** Height of the water surface above the ground when Snap To Ground is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright", meta = (ClampMin = "0.0", ClampMax = "50.0", Units = "m"))
	float WaterDepth = 0.6f;

	/** Length of each strip of the surface mesh. Shorter follows tight bends more smoothly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright|Quality", meta = (ClampMin = "0.25", ClampMax = "20.0", Units = "m"))
	float SegmentLength = 2.0f;

	/** Material used for the surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright|Advanced")
	TObjectPtr<UMaterialInterface> RiverMaterial;

	/** Drops every spline point onto the ground below it, then lifts it by Water Depth. Lay the spline roughly, then press this. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Tidewright")
	void SnapToGround();

	/** Rebuilds the surface after the spline is changed from Blueprint or code. Editing in the editor does this by itself. */
	UFUNCTION(BlueprintCallable, Category = "Tidewright")
	void RebuildRiver();

	/**
	 * Finds the river surface and current at a world location.
	 * @param OutSurfaceLocation  The point on the surface directly above or below.
	 * @param OutNormal  Surface normal (it leans downhill).
	 * @param OutVelocity  Velocity of the water there, in centimetres per second.
	 * @return false if the location is not over this river.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tidewright")
	bool SampleRiver(const FVector& WorldLocation, FVector& OutSurfaceLocation, FVector& OutNormal, FVector& OutVelocity) const;

	/** Tries every river in the world. Returns false if the location is over none of them. */
	static bool SampleAnyRiver(const UWorld* World, const FVector& WorldLocation, FVector& OutSurfaceLocation, FVector& OutNormal, FVector& OutVelocity);

	USplineComponent* GetSpline() const { return Spline; }

	/** Speed of the current as a fraction of Flow Speed: Across is -1 to 1 bank to bank, Slope is drop per metre. */
	static float FlowFactor(float Across, float Slope, float RapidsSlope);

	//~ AActor
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

private:
	void PushMaterialParameters();

	UPROPERTY(VisibleAnywhere, Category = "Tidewright")
	TObjectPtr<USplineComponent> Spline;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> SurfaceMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SurfaceMaterial;

	/** Speed of the current along the river relative to Flow Speed (Narrows Speedup), one value per mesh row. */
	float WidthSpeedAt(double Distance) const;

	FTidewrightRiverSettings AppliedRiver;
	bool bMaterialDirty = true;
	TArray<float> RowWidthSpeed;
	double RowWidthSpeedLength = 0.0;
};
