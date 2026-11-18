#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MeridianAstronomy.h"
#include "MeridianWeather.h"
#include "MeridianSkyActor.generated.h"

class UDirectionalLightComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UVolumetricCloudComponent;
class UExponentialHeightFogComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UWindDirectionalSourceComponent;
class UPostProcessComponent;
class UAudioComponent;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMeridianSkyEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMeridianHourEvent, int32, Hour);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMeridianLightningEvent, FVector, Location, float, DistanceMeters);

/**
 * Drop one of these in a level for a complete sky and weather system: sun, moon, atmosphere, clouds, sky light,
 * fog, rain, snow, wind and lightning, all driven from Time Of Day and Weather.
 */
UCLASS(meta = (DisplayName = "Meridian Sky"))
class MERIDIANSKY_API AMeridianSkyActor : public AActor
{
	GENERATED_BODY()

public:
	AMeridianSkyActor();

	// ---- Time ----

	/** Local time in hours, 0 to 24. Everything in the sky follows this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_TimeOfDay, Interp, Category = "Time", meta = (ClampMin = 0, ClampMax = 24, UIMin = 0, UIMax = 24))
	float TimeOfDay = 10.f;

	/** Advance Time Of Day automatically during play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Time")
	bool bAnimateTimeOfDay = false;

	/** Also advance time in the editor viewport, for previewing the cycle without pressing Play. */
	UPROPERTY(EditAnywhere, Category = "Time", meta = (EditCondition = "bAnimateTimeOfDay"))
	bool bAnimateInEditor = false;

	/** Real minutes that twelve hours of daytime take. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Time", meta = (ClampMin = 0.05, EditCondition = "bAnimateTimeOfDay"))
	float DayLengthMinutes = 10.f;

	/** Real minutes that twelve hours of night take. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Time", meta = (ClampMin = 0.05, EditCondition = "bAnimateTimeOfDay"))
	float NightLengthMinutes = 5.f;

	// ---- Sun and moon path ----

	/** Place the sun and moon where they really are for a location and date. When off, a simple adjustable arc is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Sun and Moon")
	bool bRealWorldSimulation = false;

	/** Degrees, positive north. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Sun and Moon|Simulation", meta = (ClampMin = -90, ClampMax = 90, EditCondition = "bRealWorldSimulation"))
	float Latitude = 40.7f;

	/** Degrees, positive east. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Sun and Moon|Simulation", meta = (ClampMin = -180, ClampMax = 180, EditCondition = "bRealWorldSimulation"))
	float Longitude = -74.f;

	/** Hours that local time is ahead of UTC. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Sun and Moon|Simulation", meta = (ClampMin = -12, ClampMax = 14, EditCondition = "bRealWorldSimulation"))
	float UtcOffsetHours = -5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Sun and Moon|Simulation", meta = (ClampMin = 1, ClampMax = 9999, EditCondition = "bRealWorldSimulation"))
	int32 Year = 2026;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Sun and Moon|Simulation", meta = (ClampMin = 1, ClampMax = 12, EditCondition = "bRealWorldSimulation"))
	int32 Month = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Sun and Moon|Simulation", meta = (ClampMin = 1, ClampMax = 31, EditCondition = "bRealWorldSimulation"))
	int32 Day = 21;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon|Simple", meta = (ClampMin = 0, ClampMax = 12, EditCondition = "!bRealWorldSimulation"))
	float SunriseTime = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon|Simple", meta = (ClampMin = 12, ClampMax = 24, EditCondition = "!bRealWorldSimulation"))
	float SunsetTime = 18.f;

	/** How far the sun's arc leans away from straight overhead, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon|Simple", meta = (ClampMin = 0, ClampMax = 85, EditCondition = "!bRealWorldSimulation"))
	float SunPathTilt = 35.f;

	/** 0 = new moon, 0.5 = full moon. Also sets where the moon sits relative to the sun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Sun and Moon|Simple", meta = (ClampMin = 0, ClampMax = 1, EditCondition = "!bRealWorldSimulation"))
	float SimpleMoonPhase = 0.5f;

	/** Yaw of true north in the level. At 0, north is +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon")
	float NorthYaw = 0.f;

	// ---- Lighting ----

	/** Sun brightness in lux, before the atmosphere dims it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (ClampMin = 0))
	float SunIntensity = 10.f;

	/** Brightness of a full moon in lux. Scales down with the moon's phase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (ClampMin = 0))
	float MoonIntensity = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
	FLinearColor MoonColor = FLinearColor(0.72f, 0.80f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (ClampMin = 0))
	float SkyLightIntensity = 1.f;

	/** Extra sky light under full cloud cover, as a multiplier. Raise it if overcast and storm scenes look too dark on the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting", meta = (ClampMin = 1, ClampMax = 8))
	float CloudySkyLightBoost = 2.f;

	// ---- Weather ----

	/** Pick a built-in weather condition. Changing this in the editor applies it immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	EMeridianWeatherType WeatherType = EMeridianWeatherType::PartlyCloudy;

	/** The current weather. Edit the values directly for a custom condition. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Interp, Category = "Weather")
	FMeridianWeatherState Weather;

	/** Direction the wind blows towards, as a yaw in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Weather", meta = (ClampMin = -180, ClampMax = 360))
	float WindDirection = 30.f;

	/** Run weather effects (precipitation, lightning) in the editor viewport without pressing Play. */
	UPROPERTY(EditAnywhere, Category = "Weather")
	bool bPreviewWeatherInEditor = true;

	/**
	 * Collection that materials read weather from. Scalars: Wetness, SnowCover, Rain, Snow, WindSpeed, CloudCoverage,
	 * Fogginess, TimeOfDay, SunElevation. Vector: WindDirection.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	TSoftObjectPtr<UMaterialParameterCollection> WeatherParameters;

	/** How wet surfaces are, 0 to 1. Builds up in rain and dries afterwards. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Weather|Surfaces", meta = (ClampMin = 0, ClampMax = 1))
	float Wetness = 0.f;

	/** How much snow has settled, 0 to 1. Builds up in snowfall and melts afterwards. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Weather|Surfaces", meta = (ClampMin = 0, ClampMax = 1))
	float SnowCover = 0.f;

	/** Seconds of heavy rain needed to soak surfaces fully. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Surfaces", meta = (ClampMin = 1))
	float WetnessBuildSeconds = 40.f;

	/** Seconds for soaked surfaces to dry once the rain stops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Surfaces", meta = (ClampMin = 1))
	float DrySeconds = 240.f;

	/** Seconds of heavy snowfall needed for full snow cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Surfaces", meta = (ClampMin = 1))
	float SnowBuildSeconds = 180.f;

	/** Seconds for full snow cover to melt once the snow stops. Rain melts it four times faster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Surfaces", meta = (ClampMin = 1))
	float SnowMeltSeconds = 400.f;

	// ---- Precipitation ----

	/** Particle system for rain. It receives a float user parameter "Intensity" (0-1) and a vector "Wind" (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precipitation")
	TSoftObjectPtr<UNiagaraSystem> RainSystem;

	/** Particle system for snow. Same user parameters as rain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precipitation")
	TSoftObjectPtr<UNiagaraSystem> SnowSystem;

	// ---- Lightning ----

	/** Peak brightness of a nearby lightning flash, in lux. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning", meta = (ClampMin = 0))
	float LightningFlashIntensity = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning")
	FLinearColor LightningColor = FLinearColor(0.78f, 0.85f, 1.f);

	/** Nearest and farthest a strike can land from the viewer, in metres. Farther strikes are dimmer and their thunder arrives later. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning")
	FVector2D LightningDistanceRange = FVector2D(300.f, 6000.f);

	/** Played after each strike, delayed by the time sound takes to travel the strike distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning")
	TSoftObjectPtr<USoundBase> ThunderSound;

	/** Material for the visible bolt. It receives scalars "Seed" and "Intensity" and a vector "Colour". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning")
	TSoftObjectPtr<UMaterialInterface> LightningMaterial;

	/** Brightness of the visible bolt. High values bloom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning", meta = (ClampMin = 0))
	float BoltBrightness = 40.f;

	// ---- Night sky ----

	/** Material for the sky dome: atmosphere, sun disc, stars and moon. Clear it to fall back to the plain engine sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Night Sky")
	TSoftObjectPtr<UMaterialInterface> SkyMaterial;

	/** Brightness of the stars on a clear night. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Night Sky", meta = (ClampMin = 0))
	float StarBrightness = 3.5f;

	/** Brightness of the moon's disc. This is the look of the moon itself, separate from the light it casts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Night Sky", meta = (ClampMin = 0))
	float MoonBrightness = 0.45f;

	/** Width of the moon in the sky, in degrees. The real moon is about 0.5; games usually draw it larger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Night Sky", meta = (ClampMin = 0.1, ClampMax = 20))
	float MoonAngularDiameter = 2.f;

	/** How strong the aurora is, 0 to 1. It only shows at night. Call Set Aurora Strength on the server in multiplayer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Interp, Category = "Night Sky", meta = (ClampMin = 0, ClampMax = 1))
	float AuroraStrength = 0.f;

	/** Brightness of the aurora at full strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Night Sky", meta = (ClampMin = 0))
	float AuroraBrightness = 1.f;

	// ---- Sound ----

	/** Looping rain ambience. Volume follows rain intensity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	TSoftObjectPtr<USoundBase> RainLoopSound;

	/** Looping wind ambience. Volume follows wind speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	TSoftObjectPtr<USoundBase> WindLoopSound;

	// ---- Exposure ----

	/** Stop auto exposure from brightening the night into day. Applies to the whole level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure")
	bool bLimitExposure = true;

	/** Darkest and brightest scene the camera will adapt to, in EV100. Raise the first value for darker nights. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (EditCondition = "bLimitExposure"))
	FVector2D ExposureRangeEV = FVector2D(0.f, 6.f);

	// ---- Clouds and fog ----

	/** Material used for the volumetric cloud layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds")
	TSoftObjectPtr<UMaterialInterface> CloudMaterial;

	/** Scalar parameter on the cloud material that cloud coverage drives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", AdvancedDisplay)
	FName CoverageParameterName = TEXT("Coverage");

	/** Material parameter values written at cloud coverage 0 and 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", AdvancedDisplay)
	FVector2D CoverageParameterRange = FVector2D(0.f, 1.f);

	/** Cloud colour in fair weather. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds")
	FLinearColor CloudColor = FLinearColor(0.95f, 0.95f, 0.95f, 1.f);

	/** Cloud colour at full storminess. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds")
	FLinearColor StormCloudColor = FLinearColor(0.62f, 0.64f, 0.68f, 1.f);

	/** Vector parameter on the cloud material that receives the cloud colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", AdvancedDisplay)
	FName CloudColorParameterName = TEXT("CloudColor");

	/** Scalar parameter on the cloud material that receives the cloud density. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", AdvancedDisplay)
	FName DensityParameterName = TEXT("Density");

	/** Cloud density written at storminess 0 and 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", AdvancedDisplay)
	FVector2D CloudDensityRange = FVector2D(0.01f, 0.012f);

	/** Scalar parameter on the cloud material that receives storminess (0-1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", AdvancedDisplay)
	FName StormParameterName = TEXT("Storminess");

	/** Vector parameter on the cloud material that receives the accumulated wind drift, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", AdvancedDisplay)
	FName WindOffsetParameterName = TEXT("WindOffset");

	/** How fast clouds drift relative to the ground wind. Wind is stronger at cloud height, so values above 1 look natural. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = 0))
	float CloudDriftScale = 3.f;

	/** Colour of fog and haze under cloud, rain or snow. In clear weather fog takes the sky's colour instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog")
	FLinearColor BadWeatherFogColor = FLinearColor(0.62f, 0.66f, 0.72f);

	/** Brightness of bad-weather fog in full daylight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (ClampMin = 0))
	float FogBrightness = 1.f;

	/** Height fog density at fogginess 0 and 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", AdvancedDisplay)
	FVector2D FogDensityRange = FVector2D(0.005f, 0.25f);

	// ---- Events ----

	UPROPERTY(BlueprintAssignable, Category = "Meridian Sky")
	FMeridianSkyEvent OnSunrise;

	UPROPERTY(BlueprintAssignable, Category = "Meridian Sky")
	FMeridianSkyEvent OnSunset;

	/** Fires when midnight passes and the date moves on. */
	UPROPERTY(BlueprintAssignable, Category = "Meridian Sky")
	FMeridianSkyEvent OnDayChanged;

	UPROPERTY(BlueprintAssignable, Category = "Meridian Sky")
	FMeridianHourEvent OnHourChanged;

	/** Fires on every machine when lightning strikes. */
	UPROPERTY(BlueprintAssignable, Category = "Meridian Sky")
	FMeridianLightningEvent OnLightningStrike;

	// ---- API ----

	/** Jump to a time of day. Call on the server in multiplayer. */
	UFUNCTION(BlueprintCallable, Category = "Meridian Sky")
	void SetTimeOfDay(float NewTimeOfDay);

	UFUNCTION(BlueprintCallable, Category = "Meridian Sky")
	void SetDate(int32 NewYear, int32 NewMonth, int32 NewDay);

	/** Blend to a built-in weather condition over a number of seconds. Call on the server in multiplayer. */
	UFUNCTION(BlueprintCallable, Category = "Meridian Sky|Weather")
	void ChangeWeather(EMeridianWeatherType NewWeather, float TransitionSeconds = 30.f);

	/** Blend to custom weather values over a number of seconds. */
	UFUNCTION(BlueprintCallable, Category = "Meridian Sky|Weather")
	void ChangeWeatherToState(const FMeridianWeatherState& NewWeather, float TransitionSeconds = 30.f);

	/** Blend to a weather preset asset over a number of seconds. */
	UFUNCTION(BlueprintCallable, Category = "Meridian Sky|Weather")
	void ChangeWeatherToPreset(const UMeridianWeatherPreset* Preset, float TransitionSeconds = 30.f);

	UFUNCTION(BlueprintCallable, Category = "Meridian Sky|Weather")
	void SetCloudCoverage(float NewCoverage);

	UFUNCTION(BlueprintCallable, Category = "Meridian Sky|Weather")
	void SetFogginess(float NewFogginess);

	UFUNCTION(BlueprintCallable, Category = "Meridian Sky")
	void SetAuroraStrength(float NewStrength);

	/** Strike lightning now, regardless of the weather. Call on the server in multiplayer. */
	UFUNCTION(BlueprintCallable, Category = "Meridian Sky|Weather")
	void TriggerLightningStrike();

	/** The weather as currently displayed on this machine (smoothed on clients). */
	UFUNCTION(BlueprintPure, Category = "Meridian Sky|Weather")
	const FMeridianWeatherState& GetDisplayedWeather() const { return VisualWeather; }

	/** Velocity of the wind in cm/s. */
	UFUNCTION(BlueprintPure, Category = "Meridian Sky|Weather")
	FVector GetWindVelocity() const;

	UFUNCTION(BlueprintPure, Category = "Meridian Sky")
	const FMeridianCelestialState& GetCelestialState() const { return Celestial; }

	UFUNCTION(BlueprintPure, Category = "Meridian Sky")
	bool IsDaytime() const { return Celestial.SunElevation > 0.f; }

	/** Unit vector from the level towards the sun. */
	UFUNCTION(BlueprintPure, Category = "Meridian Sky")
	FVector GetSunDirection() const;

	UFUNCTION(BlueprintPure, Category = "Meridian Sky")
	FVector GetMoonDirection() const;

	/** Recompute and apply the whole sky from the current settings. */
	UFUNCTION(BlueprintCallable, Category = "Meridian Sky")
	void UpdateSky();

	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> SunLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> MoonLight;

	/** Lights the scene during a lightning flash. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> LightningLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkyAtmosphereComponent> SkyAtmosphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UVolumetricCloudComponent> VolumetricCloud;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UExponentialHeightFogComponent> HeightFog;

	/** Inward-facing sphere that draws the atmosphere, stars and moon. It follows the camera and turns with the stars. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SkyDome;

	/** Camera-facing card that shows the bolt during a lightning strike. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> LightningBolt;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> RainFX;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> SnowFX;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWindDirectionalSourceComponent> WindSource;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UAudioComponent> RainAudio;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UAudioComponent> WindAudio;

private:
	UFUNCTION()
	void OnRep_TimeOfDay();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastLightningStrike(float YawDegrees, float DistanceMeters, int32 Seed);

	void AdvanceTime(float DeltaSeconds);
	void AdvanceDate();
	FMeridianCelestialState ComputeCelestial() const;

	void ApplyLights();
	void ApplyClouds();
	void ApplyFog();
	void ApplyPrecipitation();
	void ApplyWind();
	void ApplyExposure();
	void ApplySound();
	void ApplyMaterialParameters();
	void ApplySkyDome();
	UMaterialInstanceDynamic* EnsureMID(UStaticMeshComponent* Component, TSoftObjectPtr<UMaterialInterface>& Material, TObjectPtr<UMaterialInstanceDynamic>& MID);

	void TickWeatherTransition(float DeltaSeconds);
	void TickSurfaces(float DeltaSeconds);
	void TickLightning(float DeltaSeconds);
	void UpdateVisualWeather(float DeltaSeconds);
	bool GetViewLocation(FVector& OutLocation) const;
	FVector ResolveViewLocation();
	bool IsInGameWorld() const;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CloudMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CloudMIDParent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SkyMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BoltMID;

	/** Last cloud material that failed validation, so the warning is logged once rather than every frame. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> RejectedCloudMaterial;

	/** Weather that everything on this machine is drawn from. */
	FMeridianWeatherState VisualWeather;

	FMeridianWeatherState TransitionFrom;
	FMeridianWeatherState TransitionTo;
	float TransitionDuration = 0.f;
	float TransitionElapsed = 0.f;
	bool bTransitioning = false;

	float SecondsToNextStrike = -1.f;
	float FlashElapsed = 0.f;
	float FlashStrength = 0.f;
	float FlashPhase = 0.f;
	bool bFlashing = false;

	bool bRainSystemMissing = false;
	bool bSnowSystemMissing = false;
	bool bWeatherParametersMissing = false;

	/** Material paths that failed to load, so a missing asset is not retried every frame. */
	TSet<FSoftObjectPath> MissingMaterials;

	FVector CloudOffset = FVector::ZeroVector;
	FVector LastViewLocation = FVector::ZeroVector;
	bool bHasViewLocation = false;

	FMeridianCelestialState Celestial;
	bool bHasPreviousState = false;
	bool bWasDaytime = true;
	int32 LastHour = -1;
};
