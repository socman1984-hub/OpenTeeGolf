// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DecalActor.h"
#include "GreenswardPitchMarking.generated.h"

class UMaterialInterface;

UENUM(BlueprintType)
enum class EGreenswardMarking : uint8
{
	/** Association football, 105 x 68 m. */
	SoccerFull,
	/** Small-sided football, 40 x 20 m. */
	SoccerSmall,
	/** American football, 120 x 53.3 yd, with yard lines, hash marks and numbers. */
	AmericanFootball,
	/** Rugby union, 100 x 70 m plus 10 m in-goals. */
	RugbyUnion,
	/** Rugby league, 100 x 68 m plus 8 m in-goals. */
	RugbyLeague,
	/** One tennis court, 23.77 x 10.97 m. */
	TennisSingle,
	/** Three tennis courts side by side. */
	TennisTriple,
	/** Field hockey, 91.4 x 55 m. */
	Hockey
};

namespace GreenswardMarkings
{
	/** Size of the painted area in metres (length, width), including a 1 m border around the outer lines. */
	GREENSWARD_API FVector2D GetExtentMetres(EGreenswardMarking Marking);

	/** Asset name suffix, for example "SoccerFull". */
	GREENSWARD_API FString GetAssetSuffix(EGreenswardMarking Marking);
}

/**
 * Line markings for a sports pitch, projected at true size onto the grass and ground below.
 * With the default rotation the length of the pitch runs along world X. Change Yaw to turn it.
 */
UCLASS(meta = (DisplayName = "Greensward Pitch Marking"))
class GREENSWARD_API AGreenswardPitchMarking : public ADecalActor
{
	GENERATED_BODY()

public:
	AGreenswardPitchMarking();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward")
	EGreenswardMarking Marking = EGreenswardMarking::SoccerFull;

	/** 1 = regulation size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward", meta = (ClampMin = "0.05"))
	float Scale = 1.0f;

	/** How far above and below the actor the lines are projected, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward", meta = (ClampMin = "1"))
	float ProjectionDepth = 300.0f;

	/** Use your own instance of the marking material (line colour, wear) instead of the stock one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Greensward")
	TObjectPtr<UMaterialInterface> MaterialOverride;

	/** Apply the settings to the decal. Called automatically when the actor is edited. */
	UFUNCTION(BlueprintCallable, Category = "Greensward")
	void Refresh();

	virtual void OnConstruction(const FTransform& Transform) override;
};
