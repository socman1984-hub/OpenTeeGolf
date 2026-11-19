// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TidewrightFloatingProp.generated.h"

class UStaticMeshComponent;
class UTidewrightBuoyancyComponent;

/**
 * A ready-made floating object: a physics mesh with a Tidewright Buoyancy component.
 * Drop it on the water, swap the mesh for a crate, barrel or buoy, and it floats.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Tidewright Floating Prop"))
class TIDEWRIGHT_API ATidewrightFloatingProp : public AActor
{
	GENERATED_BODY()

public:
	ATidewrightFloatingProp();

	/** The floating body. Simulates physics. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tidewright")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Keeps the body afloat on Tidewright Water. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tidewright")
	TObjectPtr<UTidewrightBuoyancyComponent> Buoyancy;
};
