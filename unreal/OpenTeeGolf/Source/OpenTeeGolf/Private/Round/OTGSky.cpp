// AOTGRound (see OTGRound.h), continued: the sky. One Meridian Sky actor (the MeridianSky plugin)
// draws the sun, the sky, the sky light, the clouds and the lightning, in place of the level's own
// (L_Course's template sun, atmosphere, sky light and clouds: removed when it's spawned). The round still owns the weather - FOTGWeather, live or a preset, changing as the round
// goes on, what the physics plays - and the time of day (SunHour); ApplySky turns them into the
// sky's weather and time each time either changes. Ours still: the weather's sound (UOTGWeatherSound:
// Meridian ships none - its lightning brings our thunder, OnSkyBolt), the rain (its streaks and
// splashes on hard ground), the height fog (the level's light haze: Meridian's is a dark band on a
// clear day), the ground's wetness (MPC_Weather), the trees' and flag's wind (MPC_Wind) and the
// picture's grade (LookVolume, above Meridian's own post process).
//
// The sun: where it really is at the course (Meridian's real-world simulation from its latitude and
// longitude, on midsummer's day, at local sun time), north as the course's (-Y in the level). "Sun
// behind you" (SunHour -1): the afternoon hour the sun stands OTG.Look.SunPitch high, the sky turned
// (Meridian's NorthYaw) so it shines down each hole from behind the tee, OTG.Look.SunSide to the left.
//
// -oldsky on the command line: the level's own sky as before (comparing looks and frame times).

#include "Round/OTGRound.h"

#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeridianAstronomy.h"
#include "MeridianSkyActor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Course/OTGCourseData.h"
#include "Round/OTGWeatherSound.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGSky, Log, All);

static TAutoConsoleVariable<float> CVarSkyFog(TEXT("OTG.Sky.Fog"), 0.4f, TEXT("Meridian's own fog, when the level has none of its own (x the level's fog as the look has it)"));
static TAutoConsoleVariable<float> CVarSkyBoost(TEXT("OTG.Sky.CloudBoost"), 1.5f, TEXT("The Meridian sky's sky light under full cloud (x)"));

namespace
{
	// (the round's look and weather settings: OTGRound.cpp's console variables)
	float Cv(const TCHAR* Name, float Default)
	{
		const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
		return V ? V->GetFloat() : Default;
	}

	// (the sky's components by name: Meridian keeps them to itself)
	template <typename T>
	T* Part(AActor* A, const TCHAR* Name)
	{
		TInlineComponentArray<T*> All(A);
		for (T* C : All) if (C->GetFName() == Name) return C;
		return nullptr;
	}
}

bool AOTGRound::EnsureSky()
{
	if (Sky) return true;
	if (bSkyTried) return false;
	bSkyTried = true;
	if (FParse::Param(FCommandLine::Get(), TEXT("oldsky")))
	{
		UE_LOG(LogOTGSky, Display, TEXT("Sky: the level's own (-oldsky)"));
		return false;
	}
	UWorld* W = GetWorld();
	if (!W) return false;
	// (the level's: its strengths kept - the look multiplies them, as it did - and its shadows' and
	// sky capture's settings, so the new sky costs what it did; then it goes)
	bool bDfShadows = false, bRealTime = true;
	float ShadowDist = -1, DfDist = -1;
	int32 Cascades = -1;
	for (TActorIterator<ADirectionalLight> It(W); It; ++It)
	{
		if (UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
			if (OrigSun < 0)
			{
				OrigSun = L->Intensity; OrigSunRot = It->GetActorRotation(); OrigSunColour = L->GetLightColor();
				bDfShadows = L->bUseRayTracedDistanceFieldShadows;
				ShadowDist = L->DynamicShadowDistanceMovableLight;
				DfDist = L->DistanceFieldShadowDistance;
				Cascades = L->DynamicShadowCascades;
			}
		It->Destroy();
	}
	for (TActorIterator<ASkyLight> It(W); It; ++It)
	{
		if (USkyLightComponent* SL = It->GetLightComponent()) if (OrigSky < 0) { OrigSky = SL->Intensity; bRealTime = SL->bRealTimeCapture; }
		It->Destroy();
	}
	// (the level's height fog stays: Meridian's takes the sky's colour on a clear day, a dark navy band
	// along the sea's horizon - ours is the light haze the game had, thickened by the weather as before)
	for (TActorIterator<AExponentialHeightFog> It(W); It; ++It)
		if (UExponentialHeightFogComponent* F = It->GetComponent())
		{
			if (OrigFog < 0) OrigFog = F->FogDensity;
			LevelFog = F;
			break;
		}
	for (TActorIterator<AActor> It(W); It; ++It)
		if (It->FindComponentByClass<USkyAtmosphereComponent>() || It->FindComponentByClass<UVolumetricCloudComponent>())
			if (!It->IsA<AMeridianSkyActor>()) It->Destroy();
	if (OrigSun < 0) { OrigSun = 10.f; OrigSunColour = FLinearColor::White; }
	if (OrigSky < 0) OrigSky = 1.f;
	if (OrigFog < 0) OrigFog = 0.02f;

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Sky = W->SpawnActor<AMeridianSkyActor>(AMeridianSkyActor::StaticClass(), FTransform::Identity, SP);
	if (!Sky)
	{
		UE_LOG(LogOTGSky, Warning, TEXT("Sky: Meridian Sky couldn't be spawned"));
		return false;
	}
	Sky->bAnimateTimeOfDay = false;         // (the round sets the hour)
	Sky->bLimitExposure = false;            // (no night: the game's own exposure, as before)
	Sky->bRealWorldSimulation = true;
	const bool bPlace = Data && (Data->Latitude != 0 || Data->Longitude != 0);
	Sky->Latitude = bPlace ? float(Data->Latitude) : 40.f;
	Sky->Longitude = bPlace ? float(Data->Longitude) : 0.f;
	Sky->UtcOffsetHours = Sky->Longitude / 15.f;   // (the clock is the sun's time there: SunHour 12 is noon)
	Sky->SetDate(FDateTime::Now().GetYear(), 6, 21);   // (midsummer, as SunAt has it)
	Sky->LightningDistanceRange = FVector2D(600.f, 9000.f);
	// (the rain's ours: grey streaks tinted to the sky, the fog thicker far off, splashes - Meridian's own
	// fell as bright white rods, and darkened the ground in streaks in a storm)
	Sky->RainSystem.Reset();
	Sky->CloudySkyLightBoost = 1.5f;
	Sky->OnLightningStrike.AddDynamic(this, &AOTGRound::OnSkyBolt);
	if (LevelFog.IsValid())
		if (UExponentialHeightFogComponent* MF = Part<UExponentialHeightFogComponent>(Sky, TEXT("HeightFog"))) MF->SetVisibility(false);
	// (what it costs, kept to what the level's sky cost: no moonlight - the game has no night, and a
	// second atmosphere light is lit through every cloud; no cloud shadows on the air; our clouds'
	// layer, 1.5 km up and 3 km thick, not 6)
	if (UDirectionalLightComponent* Moon = Part<UDirectionalLightComponent>(Sky, TEXT("MoonLight")))
	{
		Moon->bCastCloudShadows = false;
		Moon->bAtmosphereSunLight = false;
		Moon->bAffectsWorld = false;
		Moon->MarkRenderStateDirty();
	}
	if (UDirectionalLightComponent* Sun = Part<UDirectionalLightComponent>(Sky, TEXT("SunLight")))
	{
		Sun->bCastShadowsOnAtmosphere = false;
		// (the clouds' shadows 30 km round the camera, as ours were - not 150)
		Sun->CloudShadowExtent = 30.f;
		Sun->CloudShadowOnSurfaceStrength = 0.85f;
		Sun->CloudShadowMapResolutionScale = 0.5f;   // (soft shadows anyway: a quarter of the cost)
		Sun->bUseRayTracedDistanceFieldShadows = bDfShadows;
		if (ShadowDist > 0) Sun->DynamicShadowDistanceMovableLight = ShadowDist;
		if (DfDist > 0) Sun->DistanceFieldShadowDistance = DfDist;
		if (Cascades >= 0) Sun->DynamicShadowCascades = Cascades;
		Sun->MarkRenderStateDirty();
	}
	if (USkyLightComponent* SL = Part<USkyLightComponent>(Sky, TEXT("SkyLight")))
		if (!bRealTime)
		{
			SL->bRealTimeCapture = false;   // (captured as the sky changes: ApplySky)
			SL->MarkRenderStateDirty();
		}
	if (UVolumetricCloudComponent* VC = Part<UVolumetricCloudComponent>(Sky, TEXT("VolumetricCloud")))
	{
		VC->SetLayerBottomAltitude(1.5f);
		VC->SetLayerHeight(3.f);
	}
	UE_LOG(LogOTGSky, Display, TEXT("Sky: Meridian Sky at %.3f, %.3f (the level's sun %.1f lux, distance field shadows %d, sky light %.2f %s, fog %.3f)"),
		Sky->Latitude, Sky->Longitude, OrigSun, bDfShadows ? 1 : 0, OrigSky, bRealTime ? TEXT("real time") : TEXT("captured"), OrigFog);
	return true;
}

void AOTGRound::ApplySky()
{
	if (!Sky) return;
	// ---- the time of day
	const float Pitch = Cv(TEXT("OTG.Look.SunPitch"), -42.f), Side = Cv(TEXT("OTG.Look.SunSide"), 40.f);
	if (SunHour >= 0)
	{
		Sky->NorthYaw = -90.f;   // (north: the level's -Y)
		Sky->SetTimeOfDay(float(SunHour));
	}
	else
	{
		// (the afternoon hour the sun's that high - or noon, where it never gets so high)
		const double Want = Pitch > 900 ? 42.0 : -Pitch;
		const double Day0 = FDateTime(Sky->Year, Sky->Month, Sky->Day).GetJulianDay();
		auto At = [&](double H) { return MeridianAstronomy::Compute(Day0 + (H - Sky->UtcOffsetHours) / 24.0, Sky->Latitude, Sky->Longitude); };
		double Hour = 12;
		for (double H = 12; H <= 19.5; H += 0.05)
			if (At(H).SunElevation <= Want) { Hour = H; break; }
		const FMeridianCelestialState C = At(Hour);
		// (the light travelling down the hole, a little from one side, as ApplyLook had it)
		double Yaw = OrigSunRot.Yaw;
		if (Side < 900 && Data && Data->Holes.IsValidIndex(HoleIndex))
		{
			const FOTGHole& H = Hole();
			const otg::V3 T = TeeOf(H, TeeName);
			const double Hd = FMath::Atan2(H.GreenCenter.X - T.x, H.GreenCenter.Z - T.z);
			Yaw = FMath::RadiansToDegrees(FMath::Atan2(-FMath::Cos(Hd), FMath::Sin(Hd))) + Side;
		}
		Sky->NorthYaw = float(FMath::Fmod(Yaw + 180.0 - C.SunAzimuth + 720.0, 360.0));   // (the sky's yaw towards the sun: azimuth + NorthYaw)
		Sky->SetTimeOfDay(float(Hour));
	}

	// ---- the weather (as ApplyWeatherToScene had it: rain is overcast whatever the cloud says)
	const double Mm = Weather.RainMm;
	const double Cloudy = Mm > 0.05 ? FMath::Max(Weather.Cloud, 0.92) : Weather.Cloud;
	const float Storm = float(FMath::Clamp((Mm - 0.05) / 6.0, 0.0, 1.0));
	const float Over = float(FMath::SmoothStep(0.45, 0.95, Cloudy));
	const float Dim = float(FMath::SmoothStep(0.3, 0.95, Cloudy));
	FMeridianWeatherState S;
	S.CloudCoverage = float(Cloudy);
	S.Storminess = FMath::Max(Storm, 0.2f * Over);
	// (rain thickens the haze, a storm's most: the far rain, greying a hole's far end smoothly with
	// distance - no sheet of it standing at one distance)
	const double RainHaze = Mm > 0.05 ? 0.06 + 0.1 * Storm : 0.0;
	// (Meridian squares its fogginess: the root keeps our fog as thick as it was)
	S.Fogginess = FMath::Sqrt(float(FMath::Min(1.0, Weather.Fog + RainHaze)));
	S.Rain = Mm > 0.05 ? float(FMath::Clamp(0.2 + Mm / 7.0, 0.0, 1.0)) : 0.f;
	S.Snow = 0.f;
	double Mps = 0, FromDeg = 0;
	HoleWind(Mps, FromDeg);
	S.WindSpeed = float(FMath::Max(Mps, 0.5));
	S.LightningRate = Mm >= 5.0 ? 4.f : 0.f;   // (a storm's: one every 6 to 22 s, as ours were)
	// (where it blows to, in the level: X east, Y south)
	const double F = FMath::DegreesToRadians(FromDeg);
	Sky->WindDirection = float(FMath::RadiansToDegrees(FMath::Atan2(FMath::Cos(F), -FMath::Sin(F))));
	Sky->ChangeWeatherToState(S, 0.f);   // (the round blends its weather itself: UpdateChanging)

	// ---- the light: the look's strengths on the level's, dimmed under cloud as before
	Sky->SunIntensity = OrigSun * Cv(TEXT("OTG.Look.Sun"), 1.35f) * FMath::Lerp(1.f, Cv(TEXT("OTG.Weather.SunOwn"), 0.4f), Dim);
	Sky->SkyLightIntensity = OrigSky * Cv(TEXT("OTG.Look.Sky"), 1.3f) * FMath::Lerp(1.f, 0.85f, Over);
	Sky->FogDensityRange.X = OrigFog * Cv(TEXT("OTG.Look.Fog"), 0.5f) * CVarSkyFog.GetValueOnGameThread();
	if (UExponentialHeightFogComponent* LF = LevelFog.Get())
		LF->SetFogDensity(FMath::Lerp(OrigFog * Cv(TEXT("OTG.Look.Fog"), 0.5f), 0.25f, float(FMath::Min(1.0, Weather.Fog + RainHaze))));
	Sky->CloudySkyLightBoost = CVarSkyBoost.GetValueOnGameThread();
	if (UDirectionalLightComponent* Sun = Part<UDirectionalLightComponent>(Sky, TEXT("SunLight")))
	{
		// (soft shadow edges on the hills, more under cloud; warmer than white)
		Sun->SetLightSourceAngle(FMath::Lerp(Cv(TEXT("OTG.Look.SunSoft"), 1.2f), Cv(TEXT("OTG.Weather.Soft"), 20.f), Dim));
		Sun->SetShadowSlopeBias(Cv(TEXT("OTG.Look.SlopeBias"), 2.f));
		Sun->SetLightColor(FMath::Lerp(OrigSunColour, FLinearColor(1.f, 0.86f, 0.68f), Cv(TEXT("OTG.Look.SunWarm"), 0.35f)));
	}
	Sky->UpdateSky();
	if (USkyLightComponent* SL = Part<USkyLightComponent>(Sky, TEXT("SkyLight"))) if (!SL->bRealTimeCapture) SL->RecaptureSky();
	// (the rain's streaks far off grey under a dark sky; rain and storms are darker days)
	if (RainMat) RainMat->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(FLinearColor(0.75f, 0.78f, 0.82f), FLinearColor(0.58f, 0.61f, 0.66f), Dim));
	if (LookVolume)
	{
		const float Gloom = float(FMath::Clamp((Mm - 0.05) / 7.0, 0.0, 1.0));
		LookVolume->Settings.AutoExposureBias = Cv(TEXT("OTG.Look.Exposure"), -0.45f) - (Mm > 0.05 ? 0.3f + 0.9f * Gloom : 0.f) - 0.25f * Over;
	}
	SkyNow = Sky->SkyLightIntensity;
}

void AOTGRound::OnSkyBolt(FVector Location, float DistanceMeters)
{
	// (the thunder: a second for every 343 m, louder the nearer)
	const float Km = FMath::Max(DistanceMeters / 1000.f, 0.05f);
	if (WxSound) WxSound->Thunder(DistanceMeters / 343.f, FMath::Clamp(1.6f / Km, 0.15f, 1.f));
}
