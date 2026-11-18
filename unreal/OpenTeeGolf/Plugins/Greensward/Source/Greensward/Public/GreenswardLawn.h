// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GreenswardLawn.generated.h"

class USplineComponent;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EGreenswardStripeMode : uint8
{
	/** No mowing pattern. */
	None,
	/** Parallel stripes, alternating mowing direction. */
	Single,
	/** Two perpendicular passes: a checkerboard. */
	Double
};

/** One grass mat in a lawn's mix. */
USTRUCT(BlueprintType)
struct FGreenswardMatEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward")
	TObjectPtr<UStaticMesh> Mesh;

	/** Relative chance of this mat being picked for a cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward", meta = (ClampMin = "0"))
	float Weight = 1.0f;
};

/**
 * A lawn, pitch, green or meadow: a closed spline filled with grass mats that follow the ground.
 * Edit the spline, pick the mats, press Rebuild.
 */
UCLASS(meta = (DisplayName = "Greensward Lawn"))
class GREENSWARD_API AGreenswardLawn : public AActor
{
	GENERATED_BODY()

public:
	AGreenswardLawn();

	/** Closed outline of the lawn. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Greensward")
	TObjectPtr<USplineComponent> Boundary;

	/** Mats to scatter. Mixing two or three similar mats hides repetition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Mats")
	TArray<FGreenswardMatEntry> Mats;

	/** Edge length of one mat in cm. The stock mats are 50. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Mats", meta = (ClampMin = "5"))
	float MatSize = 50.0f;

	/** Optional material applied to every mat instead of the mesh's own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Mats")
	TObjectPtr<UMaterialInterface> OverrideMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Mats")
	int32 Seed = 1337;

	/** Rotate each mat by a random quarter turn so the same mat never repeats visibly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Mats")
	bool bRandomQuarterTurns = true;

	/** Random vertical scale variation, as a fraction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Mats", meta = (ClampMin = "0", ClampMax = "0.5"))
	float HeightJitter = 0.06f;

	/** How many times cells on the outline are split into quarters. 0 = 50 cm steps, 2 = 12.5 cm steps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Mats", meta = (ClampMin = "0", ClampMax = "3"))
	int32 EdgeSubdivisions = 2;

	/** 0 = a crisp edge that follows the outline exactly. 1 = a loose, uneven edge with shorter grass, as on rough or meadow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Mats", meta = (ClampMin = "0", ClampMax = "1"))
	float EdgeRaggedness = 0.5f;

	/** Safety limit on the number of mats one lawn may place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Mats", meta = (ClampMin = "1"))
	int32 MaxInstances = 250000;

	/** Other lawns whose area is cut out of this one (a green inside a fairway, a pitch inside a park). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Mats")
	TArray<TObjectPtr<AGreenswardLawn>> CutOutLawns;

	/** Drop each mat onto the ground below and tilt it to the slope. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Ground")
	bool bSnapToGround = true;

	/** Skip cells where no ground was found. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Ground", meta = (EditCondition = "bSnapToGround"))
	bool bRequireGround = true;

	/** How far above and below the spline plane to look for ground, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Ground", meta = (EditCondition = "bSnapToGround", ClampMin = "1"))
	float TraceDistance = 20000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Ground", meta = (EditCondition = "bSnapToGround"))
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_WorldStatic;

	/** Lift each mat this far off the ground, in cm, so uneven terrain does not poke through the turf. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Ground")
	float SurfaceOffset = 1.0f;

	/** Send this lawn's mowing pattern and tint to the material. Off = use the material instance's own values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Appearance")
	bool bOverrideAppearance = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Appearance", meta = (EditCondition = "bOverrideAppearance"))
	EGreenswardStripeMode StripeMode = EGreenswardStripeMode::None;

	/** Width of one mower pass in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Appearance", meta = (EditCondition = "bOverrideAppearance", ClampMin = "10"))
	float StripeWidth = 200.0f;

	/** Mowing direction in degrees, world space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Appearance", meta = (EditCondition = "bOverrideAppearance"))
	float StripeAngle = 0.0f;

	/** Light/dark difference between passes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Appearance", meta = (EditCondition = "bOverrideAppearance", ClampMin = "0", ClampMax = "1"))
	float StripeContrast = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Appearance", meta = (EditCondition = "bOverrideAppearance"))
	FLinearColor Tint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Rendering")
	bool bCastShadow = true;

	/** Mats beyond this distance are not drawn. 0 = never cull. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Rendering", meta = (ClampMin = "0"))
	float CullDistance = 0.0f;

	/** Wind animation stops beyond this distance, which is much cheaper. 0 = always animate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward|Rendering", meta = (ClampMin = "0"))
	float WindDisableDistance = 4000.0f;

	/** Rebuild automatically when the actor is edited or moved in the editor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward")
	bool bAutoRebuild = true;

	/** Refill the outline with mats. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Greensward")
	void Rebuild();

	/** Remove every mat. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Greensward")
	void Clear();

	/** Push stripe and tint settings to the existing mats without rebuilding. */
	UFUNCTION(BlueprintCallable, Category = "Greensward")
	void ApplyAppearance();

	/** Number of mats currently placed. */
	UFUNCTION(BlueprintPure, Category = "Greensward")
	int32 GetInstanceCount() const;

	/** Make the outline a rectangle centred on the actor. Sizes in cm. */
	UFUNCTION(BlueprintCallable, Category = "Greensward")
	void SetRectangle(float SizeX, float SizeY);

	/** True if the world-space point lies inside the outline (ignoring height). */
	UFUNCTION(BlueprintPure, Category = "Greensward")
	bool IsPointInside(const FVector& WorldPoint) const;

	virtual void OnConstruction(const FTransform& Transform) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Greensward")
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> MatComponents;

	void GetOutline(TArray<FVector2D>& OutLocalPoints) const;
};
