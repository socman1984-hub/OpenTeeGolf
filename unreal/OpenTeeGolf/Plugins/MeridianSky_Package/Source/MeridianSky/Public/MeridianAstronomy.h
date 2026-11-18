#pragma once

#include "CoreMinimal.h"
#include "MeridianAstronomy.generated.h"

/** Where the sun and moon are in the sky, as seen from the ground. Angles are in degrees. */
USTRUCT(BlueprintType)
struct MERIDIANSKY_API FMeridianCelestialState
{
	GENERATED_BODY()

	/** Compass bearing of the sun, clockwise from north. */
	UPROPERTY(BlueprintReadOnly, Category = "Meridian Sky")
	float SunAzimuth = 180.f;

	/** Height of the sun above the horizon. Negative when it has set. */
	UPROPERTY(BlueprintReadOnly, Category = "Meridian Sky")
	float SunElevation = 45.f;

	UPROPERTY(BlueprintReadOnly, Category = "Meridian Sky")
	float MoonAzimuth = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Meridian Sky")
	float MoonElevation = -45.f;

	/** Position in the lunar cycle: 0 = new moon, 0.5 = full moon, back to new at 1. */
	UPROPERTY(BlueprintReadOnly, Category = "Meridian Sky")
	float MoonPhase = 0.5f;

	/** Fraction of the moon's disc that is lit, 0 to 1. */
	UPROPERTY(BlueprintReadOnly, Category = "Meridian Sky")
	float MoonIllumination = 1.f;

	/** Local sidereal angle, used to rotate the star field. */
	UPROPERTY(BlueprintReadOnly, Category = "Meridian Sky")
	float SiderealAngle = 0.f;
};

/** Low-precision solar and lunar ephemeris (Astronomical Almanac series; about 0.01 deg for the sun, 0.3 deg for the moon). */
namespace MeridianAstronomy
{
	/**
	 * @param JulianDay   Julian day in UTC, including the fractional day.
	 * @param Latitude    Degrees, positive north.
	 * @param Longitude   Degrees, positive east.
	 */
	MERIDIANSKY_API FMeridianCelestialState Compute(double JulianDay, double Latitude, double Longitude);

	/** Unit vector pointing at a body, with +X as north (before NorthYaw), +Y east and +Z up. */
	MERIDIANSKY_API FVector DirectionFromAzimuthElevation(float AzimuthDeg, float ElevationDeg, float NorthYawDeg);

	/** Inverse of DirectionFromAzimuthElevation. */
	MERIDIANSKY_API void AzimuthElevationFromDirection(const FVector& Direction, float NorthYawDeg, float& OutAzimuthDeg, float& OutElevationDeg);
}
