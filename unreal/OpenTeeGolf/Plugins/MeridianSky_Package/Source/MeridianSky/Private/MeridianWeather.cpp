#include "MeridianWeather.h"

FMeridianWeatherState FMeridianWeatherState::Lerp(const FMeridianWeatherState& A, const FMeridianWeatherState& B, float Alpha)
{
	FMeridianWeatherState Out;
	Out.CloudCoverage = FMath::Lerp(A.CloudCoverage, B.CloudCoverage, Alpha);
	Out.Storminess = FMath::Lerp(A.Storminess, B.Storminess, Alpha);
	Out.Fogginess = FMath::Lerp(A.Fogginess, B.Fogginess, Alpha);
	Out.Rain = FMath::Lerp(A.Rain, B.Rain, Alpha);
	Out.Snow = FMath::Lerp(A.Snow, B.Snow, Alpha);
	Out.WindSpeed = FMath::Lerp(A.WindSpeed, B.WindSpeed, Alpha);
	Out.LightningRate = FMath::Lerp(A.LightningRate, B.LightningRate, Alpha);
	return Out;
}

FMeridianWeatherState FMeridianWeatherState::FromType(EMeridianWeatherType Type)
{
	//                           Coverage  Storm  Fog    Rain   Snow   Wind   Lightning
	auto Make = [](float Coverage, float Storm, float Fog, float InRain, float InSnow, float Wind, float Lightning)
	{
		FMeridianWeatherState S;
		S.CloudCoverage = Coverage;
		S.Storminess = Storm;
		S.Fogginess = Fog;
		S.Rain = InRain;
		S.Snow = InSnow;
		S.WindSpeed = Wind;
		S.LightningRate = Lightning;
		return S;
	};

	switch (Type)
	{
	case EMeridianWeatherType::Clear:        return Make(0.05f, 0.f,  0.f,   0.f,   0.f,  2.f,  0.f);
	case EMeridianWeatherType::PartlyCloudy: return Make(0.40f, 0.f,  0.f,   0.f,   0.f,  4.f,  0.f);
	case EMeridianWeatherType::Overcast:     return Make(0.90f, 0.2f, 0.05f, 0.f,   0.f,  5.f,  0.f);
	case EMeridianWeatherType::Foggy:        return Make(0.70f, 0.f,  0.85f, 0.f,   0.f,  1.f,  0.f);
	case EMeridianWeatherType::LightRain:    return Make(0.85f, 0.3f, 0.10f, 0.30f, 0.f,  5.f,  0.f);
	case EMeridianWeatherType::HeavyRain:    return Make(1.00f, 0.7f, 0.15f, 0.85f, 0.f,  10.f, 0.f);
	case EMeridianWeatherType::Thunderstorm: return Make(1.00f, 1.f,  0.15f, 1.00f, 0.f,  16.f, 6.f);
	case EMeridianWeatherType::LightSnow:    return Make(0.85f, 0.1f, 0.25f, 0.f,   0.30f, 3.f, 0.f);
	case EMeridianWeatherType::Blizzard:     return Make(1.00f, 0.5f, 0.70f, 0.f,   1.00f, 20.f, 0.f);
	default:                                 return FMeridianWeatherState();
	}
}
