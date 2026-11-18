// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GreenswardPCGArea.generated.h"

class USplineComponent;
class UPCGComponent;

/**
 * A closed spline with a PCG component: assign one of the Greensward PCG graphs and the outline is filled with grass.
 * Use this instead of a Greensward Lawn when you want the area to be part of a PCG workflow.
 */
UCLASS(meta = (DisplayName = "Greensward PCG Area"))
class GREENSWARD_API AGreenswardPCGArea : public AActor
{
	GENERATED_BODY()

public:
	AGreenswardPCGArea();

	/** Closed outline of the area. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Greensward")
	TObjectPtr<USplineComponent> Boundary;

	/** Set its graph to one of the graphs in /Greensward/PCG. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Greensward")
	TObjectPtr<UPCGComponent> PCG;

	/** Make the outline a rectangle centred on the actor. Sizes in cm. */
	UFUNCTION(BlueprintCallable, Category = "Greensward")
	void SetRectangle(float SizeX, float SizeY);
};
