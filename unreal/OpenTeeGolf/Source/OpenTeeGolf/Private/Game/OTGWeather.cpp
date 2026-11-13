// See OTGWeather.h (public/js/weather.js).

#include "Game/OTGWeather.h"

#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// Open-Meteo's weather codes
	const TPair<int32, const TCHAR*> Codes[] = {
		{ 0, TEXT("Clear") }, { 1, TEXT("Mostly clear") }, { 2, TEXT("Partly cloudy") }, { 3, TEXT("Overcast") }, { 45, TEXT("Fog") }, { 48, TEXT("Freezing fog") },
		{ 51, TEXT("Light drizzle") }, { 53, TEXT("Drizzle") }, { 55, TEXT("Heavy drizzle") }, { 61, TEXT("Light rain") }, { 63, TEXT("Rain") }, { 65, TEXT("Heavy rain") },
		{ 71, TEXT("Light snow") }, { 73, TEXT("Snow") }, { 75, TEXT("Heavy snow") }, { 80, TEXT("Showers") }, { 81, TEXT("Showers") }, { 82, TEXT("Heavy showers") },
		{ 95, TEXT("Thunderstorm") }, { 96, TEXT("Thunderstorm") }, { 99, TEXT("Thunderstorm") },
	};
}

TArray<TPair<FString, FString>> FOTGWeather::Choices()
{
	return {
		{ TEXT("live"), TEXT("Live (the course's weather now)") }, { TEXT("calm"), TEXT("Calm and sunny") }, { TEXT("breezy"), TEXT("Breezy") },
		{ TEXT("windy"), TEXT("Windy") }, { TEXT("overcast"), TEXT("Overcast") }, { TEXT("fog"), TEXT("Fog") }, { TEXT("rain"), TEXT("Rain") },
		{ TEXT("storm"), TEXT("Storm") }, { TEXT("custom"), TEXT("Custom wind") },
	};
}

FOTGWeather FOTGWeather::Preset(const FString& Key)
{
	FOTGWeather W;
	auto Set = [&](const TCHAR* L, double Wind, double Gust, double T, double H, double Rain, double Cl, double Fg) {
		W.Label = L; W.WindMph = Wind; W.GustMph = Gust; W.TempC = T; W.Humidity = H; W.RainMm = Rain; W.Cloud = Cl; W.Fog = Fg;
	};
	if (Key == TEXT("calm")) Set(TEXT("Calm and sunny"), 2, 4, 24, 0.45, 0, 0.1, 0);
	else if (Key == TEXT("windy")) Set(TEXT("Windy"), 18, 30, 16, 0.6, 0, 0.6, 0);
	else if (Key == TEXT("overcast")) Set(TEXT("Overcast"), 6, 10, 15, 0.75, 0, 0.95, 0);
	else if (Key == TEXT("fog")) Set(TEXT("Fog"), 1, 3, 11, 1, 0, 0.85, 1);
	else if (Key == TEXT("rain")) Set(TEXT("Rain"), 11, 20, 13, 0.95, 2.5, 1, 0);
	else if (Key == TEXT("storm")) Set(TEXT("Storm"), 22, 38, 12, 1, 7, 1, 0.3);
	else Set(TEXT("Breezy"), 9, 15, 21, 0.55, 0, 0.35, 0);
	W.Key = Key == TEXT("calm") || Key == TEXT("windy") || Key == TEXT("overcast") || Key == TEXT("fog") || Key == TEXT("rain") || Key == TEXT("storm") ? Key : FString(TEXT("breezy"));
	W.WindFromMet = FMath::FRand() * 360;
	return W;
}

FOTGWeather FOTGWeather::Custom(double WindMph)
{
	FOTGWeather W;
	W.Key = TEXT("custom");
	W.Label = TEXT("Custom");
	W.WindMph = WindMph;
	W.GustMph = WindMph * 1.35;
	W.TempC = 20; W.Humidity = 0.5; W.RainMm = 0; W.Cloud = 0.3;
	W.bRelative = true;
	return W;
}

void FOTGWeather::Gust(double& OutMph, double& OutWobbleDeg) const
{
	const double R = FMath::FRand();
	OutMph = WindMph + (GustMph - WindMph) * R * R;
	OutWobbleDeg = (FMath::FRand() - 0.5) * (8 + (GustMph - WindMph) * 1.2);
}

void FOTGWeather::FetchLive(double Lat, double Lon, TFunction<void(bool, const FOTGWeather&, const FString&)> OnDone)
{
	const FString Url = FString::Printf(TEXT("https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,precipitation,cloud_cover,surface_pressure,weather_code,wind_speed_10m,wind_direction_10m,wind_gusts_10m&wind_speed_unit=mph"), Lat, Lon);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(Url);
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(10);
	Req->OnProcessRequestComplete().BindLambda([OnDone](FHttpRequestPtr, FHttpResponsePtr Res, bool bOk) {
		FOTGWeather W = Preset(TEXT("breezy"));
		if (!bOk || !Res.IsValid() || Res->GetResponseCode() != 200)
			return OnDone(false, W, Res.IsValid() ? FString::Printf(TEXT("weather service %d"), Res->GetResponseCode()) : FString(TEXT("no connection")));
		TSharedPtr<FJsonObject> J;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Res->GetContentAsString()), J) || !J || !J->HasTypedField<EJson::Object>(TEXT("current")))
			return OnDone(false, W, TEXT("unreadable answer"));
		const TSharedPtr<FJsonObject> C = J->GetObjectField(TEXT("current"));
		auto Num = [&](const TCHAR* K, double D) { double V; return C->TryGetNumberField(K, V) ? V : D; };
		const int32 Code = int32(Num(TEXT("weather_code"), 0));
		W.Key = TEXT("live");
		W.Label = TEXT("Current conditions");
		for (const auto& P : Codes) if (P.Key == Code) W.Label = P.Value;
		W.WindMph = Num(TEXT("wind_speed_10m"), 0);
		W.GustMph = FMath::Max(Num(TEXT("wind_gusts_10m"), 0), W.WindMph);
		W.WindFromMet = Num(TEXT("wind_direction_10m"), 0);
		W.TempC = Num(TEXT("temperature_2m"), 20);
		W.Humidity = Num(TEXT("relative_humidity_2m"), 50) / 100;
		W.PressureHpa = Num(TEXT("surface_pressure"), 0);
		W.RainMm = Num(TEXT("precipitation"), 0);
		W.Cloud = Num(TEXT("cloud_cover"), 30) / 100;
		W.Fog = Code == 45 || Code == 48 ? 1 : Code >= 95 ? 0.3 : 0;
		W.bLive = true;
		OnDone(true, W, FString());
	});
	Req->ProcessRequest();
}
