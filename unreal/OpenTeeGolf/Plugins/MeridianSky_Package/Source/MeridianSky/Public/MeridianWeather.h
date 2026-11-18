#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MeridianWeather.generated.h"

/** Built-in weather conditions. */
UENUM(BlueprintType)
enum class EMeridianWeatherType : uint8
{
	Clear,
	PartlyCloudy,
	Overcast,
	Foggy,
	LightRain,
	HeavyRain,
	Thunderstorm,
	LightSnow,
	Blizzard,
};

/** Everything that describes the weather at one moment. Two states can be blended to transition between them. */
USTRUCT(BlueprintType)
struct MERIDIANSKY_API FMeridianWeatherState
{
	GENERATED_BODY()

	/** 0 = clear sky, 1 = overcast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = 0, ClampMax = 1))
	float CloudCoverage = 0.3f;

	/** 0 = fair-weather clouds, 1 = dark storm clouds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = 0, ClampMax = 1))
	float Storminess = 0.f;

	/** 0 = clear air, 1 = thick fog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = 0, ClampMax = 1))
	float Fogginess = 0.f;

	/** 0 = dry, 1 = downpour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = 0, ClampMax = 1))
	float Rain = 0.f;

	/** 0 = none, 1 = heavy snowfall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = 0, ClampMax = 1))
	float Snow = 0.f;

	/** Wind speed in metres per second. 5 is a breeze, 15 is a gale, 30 is a severe storm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = 0, ClampMax = 40))
	float WindSpeed = 2.f;

	/** Average lightning strikes per minute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = 0, ClampMax = 60))
	float LightningRate = 0.f;

	static FMeridianWeatherState Lerp(const FMeridianWeatherState& A, const FMeridianWeatherState& B, float Alpha);
	static FMeridianWeatherState FromType(EMeridianWeatherType Type);
};

/** A reusable weather condition saved as an asset, for weather that the built-in types do not cover. */
UCLASS(BlueprintType)
class MERIDIANSKY_API UMeridianWeatherPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ShowOnlyInnerProperties))
	FMeridianWeatherState State;
};
