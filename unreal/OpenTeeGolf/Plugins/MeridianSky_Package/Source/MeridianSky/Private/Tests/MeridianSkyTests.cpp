#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MeridianAstronomy.h"
#include "MeridianSkyActor.h"
#include "MeridianWeather.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"

namespace
{
	const EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	double JulianDayUtc(int32 Year, int32 Month, int32 Day, double HoursUtc)
	{
		return FDateTime(Year, Month, Day).GetJulianDay() + HoursUtc / 24.0;
	}

	float SeparationDegrees(float Az1, float El1, float Az2, float El2)
	{
		const FVector A = MeridianAstronomy::DirectionFromAzimuthElevation(Az1, El1, 0.f);
		const FVector B = MeridianAstronomy::DirectionFromAzimuthElevation(Az2, El2, 0.f);
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp((float)FVector::DotProduct(A, B), -1.f, 1.f)));
	}

	/** A throwaway game world with one sky actor in it, torn down when it goes out of scope. */
	struct FSkyTestWorld
	{
		UWorld* World = nullptr;
		AMeridianSkyActor* Sky = nullptr;

		FSkyTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			Sky = World->SpawnActor<AMeridianSkyActor>();
		}

		~FSkyTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		void Advance(float Seconds, float Step = 0.5f)
		{
			for (float Elapsed = 0.f; Elapsed < Seconds - KINDA_SMALL_NUMBER; Elapsed += Step)
			{
				Sky->Tick(Step);
			}
		}
	};
}

// ---------------------------------------------------------------------------------------------------------------------
// Sun and moon positions, checked against published almanac values.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeridianSunPositionTest, "MeridianSky.Astronomy.SunPosition", TestFlags)
bool FMeridianSunPositionTest::RunTest(const FString& Parameters)
{
	// Greenwich at local solar noon on the June solstice: elevation is 90 - latitude + axial tilt.
	FMeridianCelestialState S = MeridianAstronomy::Compute(JulianDayUtc(2026, 6, 21, 12.0), 51.48, 0.0);
	TestEqual(TEXT("Greenwich solstice noon elevation"), S.SunElevation, 61.96f, 0.5f);
	TestEqual(TEXT("Greenwich solstice noon azimuth is due south"), S.SunAzimuth, 180.f, 3.f);

	// The equator at the March equinox: the sun passes almost directly overhead.
	S = MeridianAstronomy::Compute(JulianDayUtc(2026, 3, 20, 12.0), 0.0, 0.0);
	TestTrue(TEXT("Equator equinox noon sun is near the zenith"), S.SunElevation > 87.f);

	// New York at sunrise on the June solstice (05:25 EDT = 09:25 UTC), rising in the north-east.
	S = MeridianAstronomy::Compute(JulianDayUtc(2026, 6, 21, 9.0 + 25.0 / 60.0), 40.71, -74.01);
	TestEqual(TEXT("New York solstice sunrise elevation"), S.SunElevation, -0.8f, 1.5f);
	TestEqual(TEXT("New York solstice sunrise azimuth"), S.SunAzimuth, 58.f, 3.f);

	// Svalbard: midnight sun in June, polar night in December.
	S = MeridianAstronomy::Compute(JulianDayUtc(2026, 6, 21, 23.0), 78.2, 15.6);
	TestTrue(TEXT("Svalbard has midnight sun in June"), S.SunElevation > 0.f);
	S = MeridianAstronomy::Compute(JulianDayUtc(2026, 12, 21, 11.0), 78.2, 15.6);
	TestTrue(TEXT("Svalbard has polar night in December"), S.SunElevation < 0.f);

	// Southern hemisphere: the noon sun is in the north.
	S = MeridianAstronomy::Compute(JulianDayUtc(2026, 6, 21, 2.0), -33.87, 151.21); // Sydney, about noon local
	TestTrue(TEXT("Sydney noon sun is in the northern sky"), S.SunAzimuth < 45.f || S.SunAzimuth > 315.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeridianMoonTest, "MeridianSky.Astronomy.Moon", TestFlags)
bool FMeridianMoonTest::RunTest(const FString& Parameters)
{
	// Total solar eclipse of 8 April 2024, greatest eclipse near Nazas, Mexico at 18:17 UTC:
	// the moon must sit on top of the sun and be new. The model is geocentric, so allow for lunar parallax.
	FMeridianCelestialState S = MeridianAstronomy::Compute(JulianDayUtc(2024, 4, 8, 18.0 + 17.0 / 60.0), 25.29, -104.14);
	TestTrue(TEXT("Eclipse: moon within 1.5 degrees of the sun"),
		SeparationDegrees(S.SunAzimuth, S.SunElevation, S.MoonAzimuth, S.MoonElevation) < 1.5f);
	TestTrue(TEXT("Eclipse: moon is new"), S.MoonIllumination < 0.01f);
	TestTrue(TEXT("Eclipse: sun is high in the sky"), S.SunElevation > 60.f);

	// Full moon of 23 April 2024, 23:49 UTC.
	S = MeridianAstronomy::Compute(JulianDayUtc(2024, 4, 23, 23.0 + 49.0 / 60.0), 51.48, 0.0);
	TestTrue(TEXT("Full moon is fully lit"), S.MoonIllumination > 0.99f);
	TestEqual(TEXT("Full moon phase is half way through the cycle"), S.MoonPhase, 0.5f, 0.02f);

	// First quarter of 15 April 2024, 19:13 UTC.
	S = MeridianAstronomy::Compute(JulianDayUtc(2024, 4, 15, 19.0 + 13.0 / 60.0), 51.48, 0.0);
	TestEqual(TEXT("First quarter is half lit"), S.MoonIllumination, 0.5f, 0.03f);
	TestEqual(TEXT("First quarter phase"), S.MoonPhase, 0.25f, 0.02f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeridianDirectionTest, "MeridianSky.Astronomy.Directions", TestFlags)
bool FMeridianDirectionTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("North is +X"), MeridianAstronomy::DirectionFromAzimuthElevation(0.f, 0.f, 0.f).Equals(FVector(1, 0, 0), 1.e-4));
	TestTrue(TEXT("East is +Y"), MeridianAstronomy::DirectionFromAzimuthElevation(90.f, 0.f, 0.f).Equals(FVector(0, 1, 0), 1.e-4));
	TestTrue(TEXT("Zenith is +Z"), MeridianAstronomy::DirectionFromAzimuthElevation(0.f, 90.f, 0.f).Equals(FVector(0, 0, 1), 1.e-4));
	TestTrue(TEXT("North yaw turns north"), MeridianAstronomy::DirectionFromAzimuthElevation(0.f, 0.f, 90.f).Equals(FVector(0, 1, 0), 1.e-4));

	float Azimuth = 0.f, Elevation = 0.f;
	MeridianAstronomy::AzimuthElevationFromDirection(MeridianAstronomy::DirectionFromAzimuthElevation(213.f, 37.f, 25.f), 25.f, Azimuth, Elevation);
	TestEqual(TEXT("Azimuth round trip"), Azimuth, 213.f, 0.01f);
	TestEqual(TEXT("Elevation round trip"), Elevation, 37.f, 0.01f);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Weather state

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeridianWeatherStateTest, "MeridianSky.Weather.State", TestFlags)
bool FMeridianWeatherStateTest::RunTest(const FString& Parameters)
{
	const FMeridianWeatherState Clear = FMeridianWeatherState::FromType(EMeridianWeatherType::Clear);
	const FMeridianWeatherState Storm = FMeridianWeatherState::FromType(EMeridianWeatherType::Thunderstorm);
	TestEqual(TEXT("Clear weather has no rain"), Clear.Rain, 0.f);
	TestTrue(TEXT("Thunderstorm has lightning"), Storm.LightningRate > 0.f);
	TestTrue(TEXT("Blizzard snows"), FMeridianWeatherState::FromType(EMeridianWeatherType::Blizzard).Snow > 0.9f);

	const FMeridianWeatherState Mid = FMeridianWeatherState::Lerp(Clear, Storm, 0.5f);
	TestEqual(TEXT("Blend rain is half way"), Mid.Rain, 0.5f * (Clear.Rain + Storm.Rain), 1.e-4f);
	TestEqual(TEXT("Blend wind is half way"), Mid.WindSpeed, 0.5f * (Clear.WindSpeed + Storm.WindSpeed), 1.e-4f);
	TestEqual(TEXT("Blend at 0 is the start"), FMeridianWeatherState::Lerp(Clear, Storm, 0.f).CloudCoverage, Clear.CloudCoverage);
	TestEqual(TEXT("Blend at 1 is the end"), FMeridianWeatherState::Lerp(Clear, Storm, 1.f).LightningRate, Storm.LightningRate);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// The sky actor running in a game world

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeridianTransitionTest, "MeridianSky.Actor.WeatherTransition", TestFlags)
bool FMeridianTransitionTest::RunTest(const FString& Parameters)
{
	FSkyTestWorld Test;
	if (!TestNotNull(TEXT("Sky actor spawned"), Test.Sky))
	{
		return false;
	}
	AMeridianSkyActor* Sky = Test.Sky;

	Sky->ChangeWeather(EMeridianWeatherType::Clear, 0.f);
	TestEqual(TEXT("Instant change applies immediately"), Sky->Weather.CloudCoverage, 0.05f, 1.e-4f);

	Sky->ChangeWeather(EMeridianWeatherType::Thunderstorm, 10.f);
	TestEqual(TEXT("A timed change does not jump"), Sky->Weather.Rain, 0.f, 1.e-4f);

	Test.Advance(5.f);
	TestTrue(TEXT("Half way through, rain is partly on"), Sky->Weather.Rain > 0.2f && Sky->Weather.Rain < 0.8f);
	TestTrue(TEXT("Half way through, displayed weather follows"), Sky->GetDisplayedWeather().Rain > 0.2f);

	Test.Advance(6.f);
	TestEqual(TEXT("Transition ends on the target rain"), Sky->Weather.Rain, 1.f, 1.e-3f);
	TestEqual(TEXT("Transition ends on the target cloud cover"), Sky->Weather.CloudCoverage, 1.f, 1.e-3f);
	TestTrue(TEXT("Transition ends with lightning"), Sky->Weather.LightningRate > 5.f);

	// A second change part-way through another must start from wherever the weather is, without a jump.
	Sky->ChangeWeather(EMeridianWeatherType::Clear, 10.f);
	Test.Advance(4.f);
	const float RainMidway = Sky->Weather.Rain;
	Sky->ChangeWeather(EMeridianWeatherType::HeavyRain, 10.f);
	TestEqual(TEXT("Redirecting a transition does not jump"), Sky->Weather.Rain, RainMidway, 1.e-4f);
	Test.Advance(11.f);
	TestEqual(TEXT("Redirected transition reaches its new target"), Sky->Weather.Rain, 0.85f, 1.e-3f);

	// Lightning must be safe to trigger by hand in any weather.
	Sky->TriggerLightningStrike();
	Test.Advance(1.f, 0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeridianSurfacesTest, "MeridianSky.Actor.Surfaces", TestFlags)
bool FMeridianSurfacesTest::RunTest(const FString& Parameters)
{
	FSkyTestWorld Test;
	if (!TestNotNull(TEXT("Sky actor spawned"), Test.Sky))
	{
		return false;
	}
	AMeridianSkyActor* Sky = Test.Sky;
	Sky->WetnessBuildSeconds = 10.f;
	Sky->DrySeconds = 20.f;
	Sky->SnowBuildSeconds = 10.f;
	Sky->SnowMeltSeconds = 20.f;

	Sky->ChangeWeather(EMeridianWeatherType::Thunderstorm, 0.f);
	Test.Advance(5.f);
	TestEqual(TEXT("Five seconds of full rain half soaks the ground"), Sky->Wetness, 0.5f, 0.05f);
	Test.Advance(10.f);
	TestEqual(TEXT("Wetness saturates at 1"), Sky->Wetness, 1.f, 1.e-3f);

	Sky->ChangeWeather(EMeridianWeatherType::Clear, 0.f);
	Test.Advance(10.f);
	TestEqual(TEXT("Ground is half dry after half the drying time"), Sky->Wetness, 0.5f, 0.05f);
	Test.Advance(15.f);
	TestEqual(TEXT("Ground dries completely"), Sky->Wetness, 0.f, 1.e-3f);

	Sky->ChangeWeather(EMeridianWeatherType::Blizzard, 0.f);
	Test.Advance(12.f);
	TestEqual(TEXT("Snow settles fully in a blizzard"), Sky->SnowCover, 1.f, 1.e-3f);
	TestEqual(TEXT("Snow does not wet the ground"), Sky->Wetness, 0.f, 1.e-3f);

	Sky->ChangeWeather(EMeridianWeatherType::Clear, 0.f);
	Test.Advance(10.f);
	const float MeltedInClear = 1.f - Sky->SnowCover;
	Sky->SnowCover = 1.f;
	Sky->ChangeWeather(EMeridianWeatherType::Thunderstorm, 0.f);
	Test.Advance(2.5f);
	TestTrue(TEXT("Rain melts snow faster than clear weather"), (1.f - Sky->SnowCover) > MeltedInClear * 0.9f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMeridianTimeTest, "MeridianSky.Actor.TimeOfDay", TestFlags)
bool FMeridianTimeTest::RunTest(const FString& Parameters)
{
	FSkyTestWorld Test;
	if (!TestNotNull(TEXT("Sky actor spawned"), Test.Sky))
	{
		return false;
	}
	AMeridianSkyActor* Sky = Test.Sky;

	// Simple mode: sun up between sunrise and sunset, down otherwise, highest at midday.
	Sky->SetTimeOfDay(12.f);
	TestTrue(TEXT("Noon is daytime"), Sky->IsDaytime());
	const float NoonElevation = Sky->GetCelestialState().SunElevation;
	TestEqual(TEXT("Noon elevation matches the path tilt"), NoonElevation, 90.f - Sky->SunPathTilt, 0.5f);
	Sky->SetTimeOfDay(9.f);
	TestTrue(TEXT("Morning sun is lower than noon and in the east"),
		Sky->GetCelestialState().SunElevation < NoonElevation && Sky->GetSunDirection().Y > 0.f);
	Sky->SetTimeOfDay(15.f);
	TestTrue(TEXT("Afternoon sun is in the west"), Sky->GetSunDirection().Y < 0.f);
	Sky->SetTimeOfDay(0.f);
	TestFalse(TEXT("Midnight is night"), Sky->IsDaytime());
	TestTrue(TEXT("A full moon is up at midnight"), Sky->GetCelestialState().MoonElevation > 0.f);
	Sky->SetTimeOfDay(5.9f);
	TestFalse(TEXT("Just before sunrise is night"), Sky->IsDaytime());
	Sky->SetTimeOfDay(6.1f);
	TestTrue(TEXT("Just after sunrise is day"), Sky->IsDaytime());
	Sky->SetTimeOfDay(26.f);
	TestEqual(TEXT("Time wraps past midnight"), Sky->TimeOfDay, 2.f, 1.e-3f);

	// Animated time: twelve daytime hours take DayLengthMinutes of real time.
	Sky->bAnimateTimeOfDay = true;
	Sky->DayLengthMinutes = 1.f;
	Sky->NightLengthMinutes = 0.5f;
	Sky->SetTimeOfDay(10.f);
	Test.Advance(10.f);
	TestEqual(TEXT("Ten seconds of a one-minute day is two hours"), Sky->TimeOfDay, 12.f, 0.02f);
	Sky->SetTimeOfDay(20.f);
	Test.Advance(5.f);
	TestEqual(TEXT("Night runs at its own speed"), Sky->TimeOfDay, 22.f, 0.02f);

	// Midnight rolls the date over, including across a year end.
	Sky->SetDate(2026, 12, 31);
	Sky->SetTimeOfDay(23.5f);
	Test.Advance(5.f);
	TestTrue(TEXT("Time wrapped into the small hours"), Sky->TimeOfDay < 3.f);
	TestEqual(TEXT("Year rolled over"), Sky->Year, 2027);
	TestEqual(TEXT("Month rolled over"), Sky->Month, 1);
	TestEqual(TEXT("Day rolled over"), Sky->Day, 1);

	// Leap day handling.
	Sky->SetDate(2028, 2, 30);
	TestEqual(TEXT("An impossible date is clamped to the month's last day"), Sky->Day, 29);

	// Real-world mode drives the same actor from latitude, longitude and date.
	Sky->bAnimateTimeOfDay = false;
	Sky->bRealWorldSimulation = true;
	Sky->Latitude = 51.48f;
	Sky->Longitude = 0.f;
	Sky->UtcOffsetHours = 0.f;
	Sky->SetDate(2026, 6, 21);
	Sky->SetTimeOfDay(12.f);
	TestEqual(TEXT("Real-world Greenwich solstice noon"), Sky->GetCelestialState().SunElevation, 61.96f, 0.5f);
	Sky->UtcOffsetHours = 1.f;
	Sky->SetTimeOfDay(13.f);
	TestEqual(TEXT("UTC offset shifts local time"), Sky->GetCelestialState().SunElevation, 61.96f, 0.5f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
