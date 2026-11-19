// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TidewrightBuoyancyComponent.generated.h"

class ATidewrightWater;
class UPrimitiveComponent;

/** How a Tidewright Buoyancy component moves its owner. */
UENUM(BlueprintType)
enum class ETidewrightBuoyancyMode : uint8
{
	/** Pushes a physics-simulated body up at each float point. The body rocks, drifts and collides naturally. */
	Physics,
	/** Moves the actor directly so it sits on the surface and tilts with the waves. No physics needed; ideal for props and debris. */
	Ride
};

/**
 * Makes an actor float on Tidewright Water and Tidewright Rivers. On a river it is carried along by the current.
 *
 * Add it to any actor. In Physics mode the root component must simulate physics. Float points are
 * generated from the actor's bounds unless you supply your own.
 */
UCLASS(ClassGroup = (Tidewright), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "Tidewright Buoyancy"))
class TIDEWRIGHT_API UTidewrightBuoyancyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTidewrightBuoyancyComponent();

	/** Physics forces, or direct placement on the surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy")
	ETidewrightBuoyancyMode Mode = ETidewrightBuoyancyMode::Physics;

	/** Water to float on. Leave empty to use the first Tidewright Water in the level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy")
	TObjectPtr<ATidewrightWater> Water;

	/** Float points in the local space of the root component. Leave empty to place one at each lower corner of the bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy|Physics", meta = (MakeEditWidget = true))
	TArray<FVector> FloatPoints;

	/** Radius of each float point. A point is fully submerged when the water is this far above it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy|Physics", meta = (ClampMin = "1.0", Units = "cm"))
	float FloatRadius = 40.0f;

	/** Total lift when fully submerged, as a multiple of the body's weight. 1 barely floats, 2 floats half out of the water. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy|Physics", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float Buoyancy = 2.0f;

	/** Resistance of the water to movement. Higher values settle faster and bob less. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy|Physics", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float WaterDrag = 1.5f;

	/** Height of the actor's origin above the surface. Ride mode only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy|Ride", meta = (Units = "cm"))
	float RideHeight = 0.0f;

	/** How much the actor tilts to follow the wave slope. 0 stays upright. Ride mode only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy|Ride", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RideTilt = 1.0f;

	/** How quickly the actor follows the surface. Lower values feel heavier. Ride mode only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buoyancy|Ride", meta = (ClampMin = "0.1", ClampMax = "60.0"))
	float RideResponse = 8.0f;

	/** Fraction of the float points currently under water, 0 to 1. */
	UFUNCTION(BlueprintPure, Category = "Buoyancy")
	float GetSubmergedFraction() const { return SubmergedFraction; }

	/** True if any float point is under water. */
	UFUNCTION(BlueprintPure, Category = "Buoyancy")
	bool IsInWater() const { return SubmergedFraction > 0.0f; }

	//~ UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	ATidewrightWater* ResolveWater();
	bool SampleWater(ATidewrightWater* WaterActor, const FVector& Location, FVector& OutSurface, FVector& OutNormal, FVector& OutVelocity) const;
	void TickPhysics(ATidewrightWater* WaterActor, float DeltaTime);
	void TickRide(ATidewrightWater* WaterActor, float DeltaTime);

	UPROPERTY(Transient)
	TObjectPtr<ATidewrightWater> CachedWater;

	TArray<FVector> ActivePoints;
	float SubmergedFraction = 0.0f;
};
