// Weather for a round, as the web version's weather.js and main.js have it: presets (calm, breezy,
// windy, overcast, fog, rain, storm), a custom wind relative to each hole, or live conditions at the
// course from Open-Meteo (free, no key). The physics plays the wind (a gust for each shot), the air
// (temperature, humidity, pressure) and how wet the ground is.
//
// Wind directions are meteorological: where the wind blows FROM (0 = north). The course's z is north.

#pragma once

#include "CoreMinimal.h"

struct OPENTEEGOLF_API FOTGWeather
{
	FString Key = TEXT("breezy"), Label = TEXT("Breezy");
	double WindMph = 9, GustMph = 15, WindFromMet = 0;
	double TempC = 21, Humidity = 0.55, PressureHpa = 0, RainMm = 0, Cloud = 0.35, Fog = 0;
	bool bRelative = false;   // (custom: the wind's direction relative to each hole)
	bool bLive = false;

	static TArray<TPair<FString, FString>> Choices();      // key, label (live, presets, custom)
	static FOTGWeather Preset(const FString& Key);         // (an unknown key: breezy)
	static FOTGWeather Custom(double WindMph);
	double Wetness() const { return RainMm > 0.05 ? FMath::Min(1.0, 0.35 + RainMm / 3) : 0.0; }
	// The mist rain brings (added to Fog for the height fog): a light haze in any rain, much more in
	// heavy rain and a storm - the air thick with spray, the far end of a hole greyed out
	double RainMist() const
	{
		if (RainMm <= 0.05) return 0.0;
		const double Storm = FMath::Clamp((RainMm - 0.05) / 6.0, 0.0, 1.0);
		return 0.06 + 0.1 * Storm + 0.5 * FMath::SmoothStep(0.3, 1.0, Storm);
	}
	// A gust for one shot: between the steady wind and the gust speed, its direction wobbling a bit
	void Gust(double& OutMph, double& OutWobbleDeg) const;

	// Live conditions at a place; OnDone on the game thread (false: unavailable, with why)
	static void FetchLive(double Lat, double Lon, TFunction<void(bool, const FOTGWeather&, const FString&)> OnDone);
};
