// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GreenswardWindController.generated.h"

class UMaterialParameterCollection;
class UArrowComponent;

/**
 * Drives wind, season, dryness, snow and wetness for every Greensward material in the level.
 * Place one in the level. The actor's forward (X) axis is the wind direction.
 */
UCLASS(meta = (DisplayName = "Greensward Wind & Weather Controller"))
class GREENSWARD_API AGreenswardWindController : public AActor
{
	GENERATED_BODY()

public:
	AGreenswardWindController();

	/** Overall wind strength. 0 = still air, 0.3 = breeze, 1 = strong wind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Greensward|Wind", meta = (ClampMin = "0", UIMax = "1.5"))
	float WindStrength = 0.3f;

	/** How fast gusts travel across the grass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Greensward|Wind", meta = (ClampMin = "0", UIMax = "3"))
	float WindSpeed = 1.0f;

	/** -1 = fresh spring growth, 0 = summer, +1 = autumn yellowing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Greensward|Weather", meta = (ClampMin = "-1", ClampMax = "1"))
	float Season = 0.0f;

	/** Fraction of blades that turn to straw, on top of the dry blades baked into each mat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Greensward|Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float Dryness = 0.0f;

	/** Snow cover. Tips whiten first; at 1 the grass is buried. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Greensward|Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float Snow = 0.0f;

	/** Rain or dew: darker, glossier blades. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Greensward|Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float Wetness = 0.0f;

	/** Parameter collection read by the Greensward materials. Leave at the default unless you duplicated it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward")
	TSoftObjectPtr<UMaterialParameterCollection> ParameterCollection;

	/** Pushes the current values to the material parameter collection. Called automatically every frame and on edit. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Greensward")
	void Apply();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Greensward")
	TObjectPtr<USceneComponent> Root;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UArrowComponent> Arrow;
#endif
};
