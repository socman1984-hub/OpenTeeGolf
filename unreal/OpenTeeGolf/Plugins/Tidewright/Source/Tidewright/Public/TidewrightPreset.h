// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TidewrightSettings.h"
#include "TidewrightPreset.generated.h"

/**
 * A saved sea state: wave and look settings that can be applied to a Tidewright Water actor.
 * Create your own from the Content Browser: Miscellaneous > Data Asset > Tidewright Preset.
 */
UCLASS(BlueprintType)
class TIDEWRIGHT_API UTidewrightPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Short description shown to people choosing a preset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset", meta = (MultiLine = true))
	FText Description;

	/** Wave settings applied by this preset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset")
	FTidewrightWaveSettings Waves;

	/** Shading settings applied by this preset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset")
	FTidewrightLookSettings Look;
};
