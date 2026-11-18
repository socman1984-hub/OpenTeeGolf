#include "MeridianSkyActor.h"

#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/WindDirectionalSourceComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/DateTime.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace
{
	// Below this sun elevation the sky has no twilight left, so the sun light can be switched off entirely.
	constexpr float SunOffElevation = -18.f;
	// The sun hands shadow casting to the moon once it is this far below the horizon.
	constexpr float SunShadowMinElevation = -2.f;
	// Angular diameter of the sun and moon discs as seen from Earth.
	constexpr float SunDiscAngle = 0.5357f;
	constexpr float MoonDiscAngle = 0.5180f;
	// Precipitation below this intensity is not worth running a particle system for.
	constexpr float MinPrecipitation = 0.01f;
	constexpr float FlashDuration = 0.7f;
	constexpr float SpeedOfSound = 343.f; // m/s
	// The engine's default Earth atmosphere, used as the clear-weather baseline.
	constexpr float ClearRayleighScale = 0.0331f;
	constexpr float ClearMieScale = 0.003996f;
	constexpr float SkyDomeRadius = 10000000.f; // 100 km, in cm

	const FName IntensityParam(TEXT("Intensity"));
	const FName WindParam(TEXT("Wind"));

	float WrapHours(float Hours)
	{
		return Hours - 24.f * FMath::FloorToFloat(Hours / 24.f);
	}

	UDirectionalLightComponent* MakeSkyLight(AActor* Owner, USceneComponent* Parent, const TCHAR* Name, int32 AtmosphereIndex, float DiscAngle)
	{
		UDirectionalLightComponent* Light = Owner->CreateDefaultSubobject<UDirectionalLightComponent>(Name);
		Light->SetupAttachment(Parent);
		Light->SetMobility(EComponentMobility::Movable);
		Light->bAtmosphereSunLight = true;
		Light->AtmosphereSunLightIndex = AtmosphereIndex;
		Light->bCastCloudShadows = true;
		Light->bCastShadowsOnClouds = true;
		// Lets clouds shade the air beneath them, so an overcast sky stops glowing blue near the horizon.
		Light->bCastShadowsOnAtmosphere = true;
		Light->LightSourceAngle = DiscAngle;
		return Light;
	}
}

AMeridianSkyActor::AMeridianSkyActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SunLight = MakeSkyLight(this, SceneRoot, TEXT("SunLight"), 0, SunDiscAngle);
	SunLight->ForwardShadingPriority = 1;
	MoonLight = MakeSkyLight(this, SceneRoot, TEXT("MoonLight"), 1, MoonDiscAngle);

	LightningLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("LightningLight"));
	LightningLight->SetupAttachment(SceneRoot);
	LightningLight->SetMobility(EComponentMobility::Movable);
	LightningLight->Intensity = 0.f;
	LightningLight->CastShadows = false;
	LightningLight->SetVisibleFlag(false);

	SkyAtmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphere"));
	SkyAtmosphere->SetupAttachment(SceneRoot);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(SceneRoot);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;

	VolumetricCloud = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("VolumetricCloud"));
	VolumetricCloud->SetupAttachment(SceneRoot);
	// Cumulus bases sit around 2 km up; the engine default of 5 km makes clouds look small and distant.
	VolumetricCloud->LayerBottomAltitude = 1.8f;
	VolumetricCloud->LayerHeight = 6.f;

	HeightFog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HeightFog"));
	HeightFog->SetupAttachment(SceneRoot);
	// Fog takes its colour from the sky atmosphere, so it follows the time of day without a hand-tuned colour curve.
	HeightFog->FogInscatteringLuminance = FLinearColor::Black;
	HeightFog->DirectionalInscatteringLuminance = FLinearColor::Black;

	// Precipitation is detached from the actor's transform: it follows the camera instead.
	RainFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("RainFX"));
	RainFX->SetupAttachment(SceneRoot);
	RainFX->SetUsingAbsoluteLocation(true);
	RainFX->SetUsingAbsoluteRotation(true);
	RainFX->bAutoActivate = false;

	SnowFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("SnowFX"));
	SnowFX->SetupAttachment(SceneRoot);
	SnowFX->SetUsingAbsoluteLocation(true);
	SnowFX->SetUsingAbsoluteRotation(true);
	SnowFX->bAutoActivate = false;

	WindSource = CreateDefaultSubobject<UWindDirectionalSourceComponent>(TEXT("WindSource"));
	WindSource->SetupAttachment(SceneRoot);
	WindSource->SetUsingAbsoluteRotation(true);

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(SceneRoot);
	PostProcess->bUnbound = true;
	// Low priority so a project's own post process volumes win.
	PostProcess->Priority = -1.f;

	RainAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("RainAudio"));
	RainAudio->SetupAttachment(SceneRoot);
	RainAudio->bAutoActivate = false;
	RainAudio->bAllowSpatialization = false;

	WindAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("WindAudio"));
	WindAudio->SetupAttachment(SceneRoot);
	WindAudio->bAutoActivate = false;
	WindAudio->bAllowSpatialization = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/EngineSky/SM_SkySphere.SM_SkySphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));

	auto MakeVisualMesh = [this](const TCHAR* Name, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Component->SetupAttachment(SceneRoot);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCastShadow(false);
		Component->bAffectDistanceFieldLighting = false;
		Component->bAffectDynamicIndirectLighting = false;
		Component->SetUsingAbsoluteLocation(true);
		Component->SetUsingAbsoluteRotation(true);
		Component->SetUsingAbsoluteScale(true);
		return Component;
	};

	SkyDome = MakeVisualMesh(TEXT("SkyDome"), SphereMesh.Object);
	if (SphereMesh.Object)
	{
		// Far enough to sit behind any scenery; the sky material ignores distance anyway.
		const float Radius = FMath::Max(SphereMesh.Object->GetBounds().SphereRadius, 1.f);
		SkyDome->SetWorldScale3D(FVector(SkyDomeRadius / Radius));
	}

	LightningBolt = MakeVisualMesh(TEXT("LightningBolt"), PlaneMesh.Object);
	LightningBolt->SetVisibleFlag(false);

	SkyMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/MeridianSky/Materials/M_MeridianSky.M_MeridianSky")));
	LightningMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/MeridianSky/Materials/M_MeridianLightning.M_MeridianLightning")));

	Weather = FMeridianWeatherState::FromType(WeatherType);
	VisualWeather = Weather;

	CloudMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/MeridianSky/Materials/M_MeridianClouds.M_MeridianClouds")));
	RainSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/MeridianSky/FX/NS_MeridianRain.NS_MeridianRain")));
	SnowSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/MeridianSky/FX/NS_MeridianSnow.NS_MeridianSnow")));
	WeatherParameters = TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(TEXT("/MeridianSky/Materials/MPC_MeridianWeather.MPC_MeridianWeather")));
}

void AMeridianSkyActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMeridianSkyActor, TimeOfDay);
	DOREPLIFETIME(AMeridianSkyActor, bAnimateTimeOfDay);
	DOREPLIFETIME(AMeridianSkyActor, DayLengthMinutes);
	DOREPLIFETIME(AMeridianSkyActor, NightLengthMinutes);
	DOREPLIFETIME(AMeridianSkyActor, bRealWorldSimulation);
	DOREPLIFETIME(AMeridianSkyActor, Latitude);
	DOREPLIFETIME(AMeridianSkyActor, Longitude);
	DOREPLIFETIME(AMeridianSkyActor, UtcOffsetHours);
	DOREPLIFETIME(AMeridianSkyActor, Year);
	DOREPLIFETIME(AMeridianSkyActor, Month);
	DOREPLIFETIME(AMeridianSkyActor, Day);
	DOREPLIFETIME(AMeridianSkyActor, SimpleMoonPhase);
	DOREPLIFETIME(AMeridianSkyActor, Weather);
	DOREPLIFETIME(AMeridianSkyActor, WindDirection);
	DOREPLIFETIME(AMeridianSkyActor, Wetness);
	DOREPLIFETIME(AMeridianSkyActor, SnowCover);
	DOREPLIFETIME(AMeridianSkyActor, AuroraStrength);
}

#if WITH_EDITOR
void AMeridianSkyActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AMeridianSkyActor, WeatherType))
	{
		Weather = FMeridianWeatherState::FromType(WeatherType);
		bTransitioning = false;
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void AMeridianSkyActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	bHasPreviousState = false;
	bRainSystemMissing = false;
	bSnowSystemMissing = false;
	bWeatherParametersMissing = false;
	MissingMaterials.Reset();
	VisualWeather = Weather;
	UpdateSky();
}

void AMeridianSkyActor::BeginPlay()
{
	Super::BeginPlay();
	bHasPreviousState = false;
	VisualWeather = Weather;
	UpdateSky();
}

bool AMeridianSkyActor::IsInGameWorld() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld();
}

bool AMeridianSkyActor::ShouldTickIfViewportsOnly() const
{
	return bPreviewWeatherInEditor || (bAnimateTimeOfDay && bAnimateInEditor);
}

void AMeridianSkyActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const bool bGame = IsInGameWorld();

	// Clients advance time locally between server updates so the sky moves smoothly.
	if (bAnimateTimeOfDay && (bGame || bAnimateInEditor))
	{
		AdvanceTime(DeltaSeconds);
	}

	if (bGame && HasAuthority())
	{
		TickWeatherTransition(DeltaSeconds);
		TickSurfaces(DeltaSeconds);
	}

	UpdateVisualWeather(DeltaSeconds);
	if (bGame || bPreviewWeatherInEditor)
	{
		// Clouds drift with the wind. Each machine accumulates its own offset, so cloud positions are not
		// identical across a multiplayer session.
		CloudOffset += GetWindVelocity() * CloudDriftScale * DeltaSeconds;
	}
	TickLightning(DeltaSeconds);
	UpdateSky();
}

// ---------------------------------------------------------------------------------------------------------------------
// Time

void AMeridianSkyActor::AdvanceTime(float DeltaSeconds)
{
	const float RealMinutesPerHalfDay = IsDaytime() ? DayLengthMinutes : NightLengthMinutes;
	const float HoursPerSecond = 12.f / (FMath::Max(RealMinutesPerHalfDay, 0.05f) * 60.f);
	TimeOfDay += HoursPerSecond * DeltaSeconds;

	while (TimeOfDay >= 24.f)
	{
		TimeOfDay -= 24.f;
		if (HasAuthority())
		{
			AdvanceDate();
		}
		OnDayChanged.Broadcast();
	}
}

void AMeridianSkyActor::AdvanceDate()
{
	const int32 SafeMonth = FMath::Clamp(Month, 1, 12);
	const int32 SafeDay = FMath::Clamp(Day, 1, FDateTime::DaysInMonth(Year, SafeMonth));
	const FDateTime Next = FDateTime(Year, SafeMonth, SafeDay) + FTimespan::FromDays(1.0);
	Year = Next.GetYear();
	Month = Next.GetMonth();
	Day = Next.GetDay();
}

void AMeridianSkyActor::SetTimeOfDay(float NewTimeOfDay)
{
	TimeOfDay = WrapHours(NewTimeOfDay);
	// A jump is not the sun rising or setting, so do not fire those events for it.
	bHasPreviousState = false;
	UpdateSky();
}

void AMeridianSkyActor::SetDate(int32 NewYear, int32 NewMonth, int32 NewDay)
{
	Year = FMath::Clamp(NewYear, 1, 9999);
	Month = FMath::Clamp(NewMonth, 1, 12);
	Day = FMath::Clamp(NewDay, 1, FDateTime::DaysInMonth(Year, Month));
	bHasPreviousState = false;
	UpdateSky();
}

void AMeridianSkyActor::OnRep_TimeOfDay()
{
	UpdateSky();
}

// ---------------------------------------------------------------------------------------------------------------------
// Weather state

void AMeridianSkyActor::ChangeWeather(EMeridianWeatherType NewWeather, float TransitionSeconds)
{
	WeatherType = NewWeather;
	ChangeWeatherToState(FMeridianWeatherState::FromType(NewWeather), TransitionSeconds);
}

void AMeridianSkyActor::ChangeWeatherToPreset(const UMeridianWeatherPreset* Preset, float TransitionSeconds)
{
	if (Preset)
	{
		ChangeWeatherToState(Preset->State, TransitionSeconds);
	}
}

void AMeridianSkyActor::ChangeWeatherToState(const FMeridianWeatherState& NewWeather, float TransitionSeconds)
{
	if (TransitionSeconds <= KINDA_SMALL_NUMBER || !IsInGameWorld())
	{
		Weather = NewWeather;
		bTransitioning = false;
		return;
	}
	TransitionFrom = Weather;
	TransitionTo = NewWeather;
	TransitionDuration = TransitionSeconds;
	TransitionElapsed = 0.f;
	bTransitioning = true;
}

void AMeridianSkyActor::SetCloudCoverage(float NewCoverage)
{
	Weather.CloudCoverage = FMath::Clamp(NewCoverage, 0.f, 1.f);
	TransitionTo.CloudCoverage = Weather.CloudCoverage;
	TransitionFrom.CloudCoverage = Weather.CloudCoverage;
}

void AMeridianSkyActor::SetAuroraStrength(float NewStrength)
{
	AuroraStrength = FMath::Clamp(NewStrength, 0.f, 1.f);
}

void AMeridianSkyActor::SetFogginess(float NewFogginess)
{
	Weather.Fogginess = FMath::Clamp(NewFogginess, 0.f, 1.f);
	TransitionTo.Fogginess = Weather.Fogginess;
	TransitionFrom.Fogginess = Weather.Fogginess;
}

void AMeridianSkyActor::TickWeatherTransition(float DeltaSeconds)
{
	if (!bTransitioning)
	{
		return;
	}
	TransitionElapsed += DeltaSeconds;
	const float Alpha = FMath::Clamp(TransitionElapsed / FMath::Max(TransitionDuration, KINDA_SMALL_NUMBER), 0.f, 1.f);
	Weather = FMeridianWeatherState::Lerp(TransitionFrom, TransitionTo, FMath::SmoothStep(0.f, 1.f, Alpha));
	bTransitioning = Alpha < 1.f;
}

void AMeridianSkyActor::TickSurfaces(float DeltaSeconds)
{
	if (Weather.Rain > MinPrecipitation)
	{
		Wetness += Weather.Rain * DeltaSeconds / FMath::Max(WetnessBuildSeconds, 1.f);
	}
	else
	{
		Wetness -= DeltaSeconds / FMath::Max(DrySeconds, 1.f);
	}

	if (Weather.Snow > MinPrecipitation)
	{
		SnowCover += Weather.Snow * DeltaSeconds / FMath::Max(SnowBuildSeconds, 1.f);
	}
	else
	{
		const float MeltRate = 1.f + 3.f * Weather.Rain;
		SnowCover -= MeltRate * DeltaSeconds / FMath::Max(SnowMeltSeconds, 1.f);
	}

	Wetness = FMath::Clamp(Wetness, 0.f, 1.f);
	SnowCover = FMath::Clamp(SnowCover, 0.f, 1.f);
}

void AMeridianSkyActor::UpdateVisualWeather(float DeltaSeconds)
{
	// The server's weather arrives a couple of times a second; ease towards it so clients never see it step.
	if (IsInGameWorld() && !HasAuthority())
	{
		const float Alpha = 1.f - FMath::Exp(-DeltaSeconds * 3.f);
		VisualWeather = FMeridianWeatherState::Lerp(VisualWeather, Weather, Alpha);
	}
	else
	{
		VisualWeather = Weather;
	}
}

FVector AMeridianSkyActor::GetWindVelocity() const
{
	return FRotator(0.f, WindDirection, 0.f).Vector() * VisualWeather.WindSpeed * 100.f;
}

// ---------------------------------------------------------------------------------------------------------------------
// Lightning

void AMeridianSkyActor::TickLightning(float DeltaSeconds)
{
	const bool bRunScheduler = HasAuthority() && (IsInGameWorld() || bPreviewWeatherInEditor);
	if (bRunScheduler && Weather.LightningRate > KINDA_SMALL_NUMBER)
	{
		const float MeanGap = 60.f / Weather.LightningRate;
		if (SecondsToNextStrike < 0.f)
		{
			SecondsToNextStrike = FMath::FRandRange(0.2f, 1.f) * MeanGap;
		}
		SecondsToNextStrike -= DeltaSeconds;
		if (SecondsToNextStrike <= 0.f)
		{
			TriggerLightningStrike();
			// Exponential gaps give the irregular rhythm of a real storm.
			SecondsToNextStrike = FMath::Max(0.3f, -FMath::Loge(FMath::Max(FMath::FRand(), 1.e-4f)) * MeanGap);
		}
	}
	else
	{
		SecondsToNextStrike = -1.f;
	}

	if (bFlashing)
	{
		FlashElapsed += DeltaSeconds;
		if (FlashElapsed >= FlashDuration)
		{
			bFlashing = false;
			LightningLight->SetIntensity(0.f);
			LightningLight->SetVisibility(false);
			LightningBolt->SetVisibility(false);
		}
		else
		{
			// A fast decay with a flicker on top, like the return strokes of a real bolt.
			const float Flicker = FMath::Square(0.55f + 0.45f * FMath::Sin(FlashElapsed * 42.f + FlashPhase));
			const float Envelope = FMath::Exp(-FlashElapsed * 5.f) * Flicker;
			LightningLight->SetIntensity(LightningFlashIntensity * FlashStrength * Envelope);
			if (BoltMID)
			{
				BoltMID->SetScalarParameterValue(TEXT("Intensity"), BoltBrightness * Envelope);
			}
		}
	}
}

void AMeridianSkyActor::TriggerLightningStrike()
{
	if (!HasAuthority())
	{
		return;
	}
	const float Distance = FMath::FRandRange((float)LightningDistanceRange.X, FMath::Max((float)LightningDistanceRange.Y, (float)LightningDistanceRange.X));
	MulticastLightningStrike(FMath::FRandRange(0.f, 360.f), Distance, FMath::Rand());
}

void AMeridianSkyActor::MulticastLightningStrike_Implementation(float YawDegrees, float DistanceMeters, int32 Seed)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const float MaxDistance = FMath::Max((float)LightningDistanceRange.Y, 1.f);
	FlashStrength = FMath::Lerp(1.f, 0.15f, FMath::Clamp(DistanceMeters / MaxDistance, 0.f, 1.f));
	FlashPhase = FRandomStream(Seed).FRandRange(0.f, 2.f * PI);
	FlashElapsed = 0.f;
	bFlashing = true;

	// The flash comes from the direction of the strike, angled down from the cloud base.
	LightningLight->SetWorldRotation(FRotator(-45.f, YawDegrees + 180.f, 0.f));
	LightningLight->SetLightColor(LightningColor);
	LightningLight->SetVisibility(true);

	const FVector Origin = ResolveViewLocation();
	const FVector StrikeLocation = Origin + FRotator(0.f, YawDegrees, 0.f).Vector() * DistanceMeters * 100.f;
	OnLightningStrike.Broadcast(StrikeLocation, DistanceMeters);

	// The bolt is a tall card from the ground to the cloud base, turned to face the viewer.
	if (UMaterialInstanceDynamic* Bolt = EnsureMID(LightningBolt, LightningMaterial, BoltMID))
	{
		const float Height = VolumetricCloud->LayerBottomAltitude * 100000.f; // km to cm
		const float Width = Height * 0.5f;
		const FVector ToViewer = (Origin - StrikeLocation).GetSafeNormal2D();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, ToViewer).GetSafeNormal();
		const float GroundZ = GetActorLocation().Z;
		// The plane mesh is 100 units square; its Y axis maps to the texture's V, which runs from cloud to ground.
		LightningBolt->SetWorldTransform(FTransform(
			FRotationMatrix::MakeFromXY(Right, FVector::DownVector).ToQuat(),
			FVector(StrikeLocation.X, StrikeLocation.Y, GroundZ + Height * 0.5f),
			FVector(Width / 100.f, Height / 100.f, 1.f)));
		Bolt->SetScalarParameterValue(TEXT("Seed"), (float)(Seed % 1000) + 1.f);
		Bolt->SetScalarParameterValue(TEXT("Intensity"), BoltBrightness);
		Bolt->SetVectorParameterValue(TEXT("Colour"), LightningColor);
		LightningBolt->SetVisibility(true);
	}

	if (IsInGameWorld() && !ThunderSound.IsNull())
	{
		const float Volume = FMath::Lerp(1.f, 0.25f, FMath::Clamp(DistanceMeters / MaxDistance, 0.f, 1.f));
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Volume]()
		{
			if (USoundBase* Sound = ThunderSound.LoadSynchronous())
			{
				UGameplayStatics::PlaySound2D(this, Sound, Volume);
			}
		}), FMath::Max(DistanceMeters / SpeedOfSound, 0.01f), false);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Sun and moon

FVector AMeridianSkyActor::GetSunDirection() const
{
	return MeridianAstronomy::DirectionFromAzimuthElevation(Celestial.SunAzimuth, Celestial.SunElevation, NorthYaw);
}

FVector AMeridianSkyActor::GetMoonDirection() const
{
	return MeridianAstronomy::DirectionFromAzimuthElevation(Celestial.MoonAzimuth, Celestial.MoonElevation, NorthYaw);
}

FMeridianCelestialState AMeridianSkyActor::ComputeCelestial() const
{
	const float Time = WrapHours(TimeOfDay);

	if (bRealWorldSimulation)
	{
		const int32 SafeMonth = FMath::Clamp(Month, 1, 12);
		const int32 SafeYear = FMath::Clamp(Year, 1, 9999);
		const int32 SafeDay = FMath::Clamp(Day, 1, FDateTime::DaysInMonth(SafeYear, SafeMonth));
		const double JulianDay = FDateTime(SafeYear, SafeMonth, SafeDay).GetJulianDay() + (double)(Time - UtcOffsetHours) / 24.0;
		return MeridianAstronomy::Compute(JulianDay, Latitude, Longitude);
	}

	// Simple mode: the sun rides a tilted arc from east to west between sunrise and sunset,
	// then completes the circle below the horizon overnight.
	const float DayHours = FMath::Max(SunsetTime - SunriseTime, 0.1f);
	float SunAngle;
	if (Time >= SunriseTime && Time < SunsetTime)
	{
		SunAngle = 180.f * (Time - SunriseTime) / DayHours;
	}
	else
	{
		const float SinceSunset = WrapHours(Time - SunsetTime);
		SunAngle = 180.f + 180.f * SinceSunset / FMath::Max(24.f - DayHours, 0.1f);
	}

	auto ArcDirection = [](float AngleDeg, float TiltDeg)
	{
		const float A = FMath::DegreesToRadians(AngleDeg);
		const float T = FMath::DegreesToRadians(TiltDeg);
		// +Y is east and +X is north, so the arc leans south like the sun does in the northern hemisphere.
		return FVector(-FMath::Sin(A) * FMath::Sin(T), FMath::Cos(A), FMath::Sin(A) * FMath::Cos(T));
	};

	FMeridianCelestialState Out;
	MeridianAstronomy::AzimuthElevationFromDirection(ArcDirection(SunAngle, SunPathTilt), 0.f, Out.SunAzimuth, Out.SunElevation);

	// The moon trails the sun by its phase: a full moon sits opposite the sun and rises at sunset.
	const float MoonAngle = SunAngle - 360.f * SimpleMoonPhase;
	MeridianAstronomy::AzimuthElevationFromDirection(ArcDirection(MoonAngle, SunPathTilt + 5.f), 0.f, Out.MoonAzimuth, Out.MoonElevation);

	Out.MoonPhase = SimpleMoonPhase;
	Out.MoonIllumination = 0.5f * (1.f - FMath::Cos(2.f * PI * SimpleMoonPhase));
	Out.SiderealAngle = Time * 15.f;
	return Out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Applying state to the scene

void AMeridianSkyActor::UpdateSky()
{
	TimeOfDay = FMath::Clamp(TimeOfDay, 0.f, 24.f);
	Celestial = ComputeCelestial();

	ApplyLights();
	ApplyClouds();
	ApplyFog();
	ApplyWind();
	ApplyPrecipitation();
	ApplyExposure();
	ApplySound();
	ApplyMaterialParameters();
	ApplySkyDome();

	const bool bIsDaytime = IsDaytime();
	const int32 Hour = FMath::FloorToInt32(WrapHours(TimeOfDay));
	if (bHasPreviousState && IsInGameWorld())
	{
		if (bIsDaytime != bWasDaytime)
		{
			(bIsDaytime ? OnSunrise : OnSunset).Broadcast();
		}
		if (Hour != LastHour)
		{
			OnHourChanged.Broadcast(Hour);
		}
	}
	bWasDaytime = bIsDaytime;
	LastHour = Hour;
	bHasPreviousState = true;
}

void AMeridianSkyActor::ApplyLights()
{
	// The atmosphere dims and reddens the sun near the horizon and blocks it below, so its intensity stays constant
	// here; that keeps twilight in the sky after sunset. The light is only switched off once twilight has ended.
	const bool bSunOn = Celestial.SunElevation > SunOffElevation;
	const bool bSunCastsShadows = Celestial.SunElevation > SunShadowMinElevation;

	SunLight->SetWorldRotation((-GetSunDirection()).Rotation());
	SunLight->SetIntensity(SunIntensity);
	SunLight->SetVisibility(bSunOn);
	SunLight->SetCastShadows(bSunCastsShadows);

	const bool bMoonOn = Celestial.MoonElevation > -5.f && Celestial.MoonIllumination > 0.01f;
	MoonLight->SetWorldRotation((-GetMoonDirection()).Rotation());
	MoonLight->SetIntensity(MoonIntensity * Celestial.MoonIllumination);
	MoonLight->SetLightColor(MoonColor);
	MoonLight->SetVisibility(bMoonOn);
	// Only one directional light casts shadows at a time; two sets of cascades cost more than they add.
	MoonLight->SetCastShadows(bMoonOn && !bSunCastsShadows);

	// Under full cloud the engine's captured sky lights the ground far less than real overcast daylight does
	// (it misses the light bouncing between ground and cloud), so lift the sky light as the sky closes in.
	const float Covered = FMath::Max(FMath::SmoothStep(0.6f, 0.95f, VisualWeather.CloudCoverage), VisualWeather.Storminess * VisualWeather.CloudCoverage);
	SkyLight->SetIntensity(SkyLightIntensity * FMath::Lerp(1.f, CloudySkyLightBoost, FMath::Clamp(Covered, 0.f, 1.f)));
}

void AMeridianSkyActor::ApplyClouds()
{
	UMaterialInterface* Parent = CloudMaterial.LoadSynchronous();
	if (!Parent)
	{
		return;
	}

	if (!CloudMID || CloudMIDParent != Parent)
	{
		// The renderer asserts if the cloud layer is given anything but a volume material flagged for cloud use,
		// so refuse such a material here and keep whatever was already applied.
		const UMaterial* BaseMaterial = Parent->GetMaterial();
		if (!BaseMaterial || BaseMaterial->MaterialDomain != MD_Volume || !BaseMaterial->GetUsageByFlag(MATUSAGE_VolumetricCloud))
		{
			if (RejectedCloudMaterial != Parent)
			{
				RejectedCloudMaterial = Parent;
				UE_LOG(LogTemp, Warning, TEXT("Meridian Sky: cloud material '%s' was ignored. It must use the Volume material domain and have 'Used with Volumetric Cloud' enabled."), *Parent->GetPathName());
			}
			return;
		}
		CloudMID = UMaterialInstanceDynamic::Create(Parent, this);
		CloudMIDParent = Parent;
		VolumetricCloud->SetMaterial(CloudMID);
	}

	if (!CoverageParameterName.IsNone())
	{
		CloudMID->SetScalarParameterValue(CoverageParameterName,
			FMath::Lerp((float)CoverageParameterRange.X, (float)CoverageParameterRange.Y, VisualWeather.CloudCoverage));
	}
	// Storm clouds look dark because they are thick: raise the density and darken the scattering colour together.
	if (!DensityParameterName.IsNone())
	{
		CloudMID->SetScalarParameterValue(DensityParameterName,
			FMath::Lerp((float)CloudDensityRange.X, (float)CloudDensityRange.Y, VisualWeather.Storminess));
	}
	if (!CloudColorParameterName.IsNone())
	{
		CloudMID->SetVectorParameterValue(CloudColorParameterName,
			FMath::Lerp(CloudColor, StormCloudColor, VisualWeather.Storminess));
	}
	if (!StormParameterName.IsNone())
	{
		CloudMID->SetScalarParameterValue(StormParameterName, VisualWeather.Storminess);
	}
	if (!WindOffsetParameterName.IsNone())
	{
		CloudMID->SetVectorParameterValue(WindOffsetParameterName, FLinearColor(CloudOffset));
	}
}

void AMeridianSkyActor::ApplyFog()
{
	// Squared so the low end of the slider gives fine control over light haze.
	const float Alpha = FMath::Square(FMath::Clamp(VisualWeather.Fogginess, 0.f, 1.f));
	HeightFog->SetFogDensity(FMath::Lerp((float)FogDensityRange.X, (float)FogDensityRange.Y, Alpha));

	// In fair weather the fog is lit by the blue sky, so it takes the atmosphere's colour. Under cloud, rain or
	// snow the light is diffuse and grey, so fade the atmosphere's tint out and light the fog with a grey that
	// follows the daylight instead.
	// A fully covered sky is grey even when nothing is falling from it.
	const float Overcast = FMath::SmoothStep(0.6f, 0.95f, VisualWeather.CloudCoverage);
	const float Grey = FMath::Clamp(FMath::Max(
		FMath::Max3(VisualWeather.Fogginess, VisualWeather.Rain, VisualWeather.Snow),
		FMath::Max(VisualWeather.Storminess * VisualWeather.CloudCoverage, Overcast)), 0.f, 1.f);
	const float Daylight = FMath::SmoothStep(-6.f, 8.f, Celestial.SunElevation);
	const float Brightness = FogBrightness * FMath::Lerp(0.004f, 1.f, Daylight) * FMath::Lerp(1.f, 0.45f, VisualWeather.Storminess);
	HeightFog->SetFogInscatteringColor(BadWeatherFogColor * (Brightness * Grey));
	HeightFog->SetSkyAtmosphereAmbientContributionColorScale(FMath::Lerp(FLinearColor::White, FLinearColor(0.08f, 0.08f, 0.08f), Grey));

	// Bad weather fills the air with water droplets, which scatter all colours equally: less blue (Rayleigh),
	// more white haze (Mie). Without this an overcast sky keeps a clear-day blue cast.
	const float Haze = Grey * VisualWeather.CloudCoverage;
	SkyAtmosphere->SetRayleighScatteringScale(FMath::Lerp(ClearRayleighScale, ClearRayleighScale * 0.25f, Haze));
	// The white haze comes from water in the air below the cloud, so it follows rain, snow and fog. Dry overcast
	// gets only a little, which keeps the texture of the cloud base readable.
	const float WetAir = FMath::Clamp(FMath::Max3(VisualWeather.Fogginess, VisualWeather.Rain, VisualWeather.Snow), 0.f, 1.f);
	SkyAtmosphere->SetMieScatteringScale(ClearMieScale * FMath::Lerp(1.f, 4.f, FMath::Max(WetAir, Overcast * 0.35f)));

	// Thick storm cloud lets less sky light reach its underside, but enough must remain for its structure to show.
	VolumetricCloud->SetSkyLightCloudBottomOcclusion(FMath::Lerp(0.55f, 0.72f, VisualWeather.Storminess));
}

void AMeridianSkyActor::ApplyWind()
{
	WindSource->SetWorldRotation(FRotator(0.f, WindDirection, 0.f));
	// Engine wind strength is unitless; 0.5 is already a strong wind for foliage and cloth.
	const float Strength = VisualWeather.WindSpeed / 30.f;
	WindSource->SetStrength(Strength);
	WindSource->SetSpeed(Strength);
	WindSource->SetMinimumGustAmount(0.1f * Strength);
	WindSource->SetMaximumGustAmount(0.4f * Strength);
}

bool AMeridianSkyActor::GetViewLocation(FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	if (World->IsGameWorld())
	{
		const APlayerController* Controller = World->GetFirstPlayerController();
		if (Controller && Controller->PlayerCameraManager)
		{
			OutLocation = Controller->PlayerCameraManager->GetCameraLocation();
			return true;
		}
	}
	if (World->ViewLocationsRenderedLastFrame.Num() > 0)
	{
		OutLocation = World->ViewLocationsRenderedLastFrame[0];
		return true;
	}
	return false;
}

FVector AMeridianSkyActor::ResolveViewLocation()
{
	// An editor viewport that is not in realtime mode only reports its camera on the frames it redraws,
	// so remember the last known position rather than snapping back to the actor in between.
	FVector Location;
	if (GetViewLocation(Location))
	{
		LastViewLocation = Location;
		bHasViewLocation = true;
	}
	return bHasViewLocation ? LastViewLocation : GetActorLocation();
}

void AMeridianSkyActor::ApplyPrecipitation()
{
	const bool bAllowed = GetNetMode() != NM_DedicatedServer && (IsInGameWorld() || bPreviewWeatherInEditor);
	const FVector ViewLocation = ResolveViewLocation();
	const FVector Wind = GetWindVelocity();

	auto Drive = [&](UNiagaraComponent* FX, TSoftObjectPtr<UNiagaraSystem>& System, bool& bMissing, float Intensity)
	{
		if (!bAllowed || Intensity <= MinPrecipitation)
		{
			if (FX->IsActive())
			{
				FX->Deactivate();
			}
			return;
		}
		if (!FX->GetAsset() && !bMissing)
		{
			UNiagaraSystem* Loaded = System.LoadSynchronous();
			bMissing = Loaded == nullptr;
			if (Loaded)
			{
				FX->SetAsset(Loaded);
			}
		}
		if (!FX->GetAsset())
		{
			return;
		}
		FX->SetWorldLocation(ViewLocation);
		FX->SetFloatParameter(IntensityParam, Intensity);
		FX->SetVectorParameter(WindParam, Wind);
		if (!FX->IsActive())
		{
			FX->Activate();
		}
	};

	Drive(RainFX, RainSystem, bRainSystemMissing, VisualWeather.Rain);
	Drive(SnowFX, SnowSystem, bSnowSystemMissing, VisualWeather.Snow);
}

void AMeridianSkyActor::ApplyExposure()
{
	FPostProcessSettings& Settings = PostProcess->Settings;
	Settings.bOverride_AutoExposureMinBrightness = bLimitExposure;
	Settings.bOverride_AutoExposureMaxBrightness = bLimitExposure;
	Settings.AutoExposureMinBrightness = (float)FMath::Min(ExposureRangeEV.X, ExposureRangeEV.Y);
	Settings.AutoExposureMaxBrightness = (float)FMath::Max(ExposureRangeEV.X, ExposureRangeEV.Y);
}

void AMeridianSkyActor::ApplySound()
{
	const bool bAllowed = IsInGameWorld() && GetNetMode() != NM_DedicatedServer;

	auto Drive = [bAllowed](UAudioComponent* Audio, TSoftObjectPtr<USoundBase>& Sound, float Volume)
	{
		if (!bAllowed || Volume <= 0.01f || Sound.IsNull())
		{
			if (Audio->IsPlaying())
			{
				Audio->Stop();
			}
			return;
		}
		if (!Audio->Sound)
		{
			Audio->SetSound(Sound.LoadSynchronous());
		}
		Audio->SetVolumeMultiplier(Volume);
		if (Audio->Sound && !Audio->IsPlaying())
		{
			Audio->Play();
		}
	};

	Drive(RainAudio, RainLoopSound, VisualWeather.Rain);
	// Wind is inaudible in a light breeze and reaches full volume in a gale.
	Drive(WindAudio, WindLoopSound, FMath::Clamp((VisualWeather.WindSpeed - 3.f) / 15.f, 0.f, 1.f));
}

UMaterialInstanceDynamic* AMeridianSkyActor::EnsureMID(UStaticMeshComponent* Component, TSoftObjectPtr<UMaterialInterface>& Material, TObjectPtr<UMaterialInstanceDynamic>& MID)
{
	if (Material.IsNull())
	{
		MID = nullptr;
		return nullptr;
	}
	UMaterialInterface* Parent = Material.Get();
	if (MID && MID->Parent == Parent)
	{
		return MID;
	}
	if (!Parent && !MissingMaterials.Contains(Material.ToSoftObjectPath()))
	{
		Parent = Material.LoadSynchronous();
	}
	if (!Parent)
	{
		// Remember the miss rather than retrying a synchronous load every frame. The property itself is left alone.
		MissingMaterials.Add(Material.ToSoftObjectPath());
		MID = nullptr;
		return nullptr;
	}
	MID = UMaterialInstanceDynamic::Create(Parent, this);
	Component->SetMaterial(0, MID);
	return MID;
}

void AMeridianSkyActor::ApplySkyDome()
{
	UMaterialInstanceDynamic* Sky = GetNetMode() != NM_DedicatedServer ? EnsureMID(SkyDome, SkyMaterial, SkyMID) : nullptr;
	SkyDome->SetVisibility(Sky != nullptr);
	if (!Sky)
	{
		return;
	}

	// Keep the dome centred on the viewer so it always encloses the scene, and turn it about the celestial pole
	// by the sidereal angle so the stars wheel overhead through the night.
	const float PoleElevation = bRealWorldSimulation ? Latitude : SunPathTilt;
	const FVector Pole = MeridianAstronomy::DirectionFromAzimuthElevation(0.f, PoleElevation, NorthYaw);
	const FQuat Tilt = FQuat::FindBetweenNormals(FVector::UpVector, Pole);
	const FQuat Spin(Pole, -FMath::DegreesToRadians(Celestial.SiderealAngle));
	SkyDome->SetWorldLocationAndRotation(ResolveViewLocation(), Spin * Tilt);

	// Stars come out through dusk and are gone by the time the sun is up.
	const float Night = 1.f - FMath::SmoothStep(-12.f, -3.f, Celestial.SunElevation);
	const float MoonUp = FMath::SmoothStep(-2.f, 1.f, Celestial.MoonElevation);
	Sky->SetScalarParameterValue(TEXT("StarBrightness"), StarBrightness * Night);
	Sky->SetScalarParameterValue(TEXT("MoonBrightness"), MoonBrightness * MoonUp);
	Sky->SetScalarParameterValue(TEXT("AuroraIntensity"), FMath::Clamp(AuroraStrength, 0.f, 1.f) * AuroraBrightness * Night);
	Sky->SetScalarParameterValue(TEXT("MoonSize"), FMath::DegreesToRadians(MoonAngularDiameter * 0.5f));
	Sky->SetVectorParameterValue(TEXT("MoonDirection"), FLinearColor(GetMoonDirection()));
	Sky->SetVectorParameterValue(TEXT("SunDirection"), FLinearColor(GetSunDirection()));
}

void AMeridianSkyActor::ApplyMaterialParameters()
{
	UWorld* World = GetWorld();
	if (!World || WeatherParameters.IsNull() || bWeatherParametersMissing)
	{
		return;
	}
	UMaterialParameterCollection* Collection = WeatherParameters.Get();
	if (!Collection)
	{
		// Try the load once; retrying a missing asset every frame would stall the game thread.
		Collection = WeatherParameters.LoadSynchronous();
		if (!Collection)
		{
			bWeatherParametersMissing = true;
			return;
		}
	}
	UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(Collection);
	if (!Instance)
	{
		return;
	}
	Instance->SetScalarParameterValue(TEXT("Wetness"), Wetness);
	Instance->SetScalarParameterValue(TEXT("SnowCover"), SnowCover);
	Instance->SetScalarParameterValue(TEXT("Rain"), VisualWeather.Rain);
	Instance->SetScalarParameterValue(TEXT("Snow"), VisualWeather.Snow);
	Instance->SetScalarParameterValue(TEXT("WindSpeed"), VisualWeather.WindSpeed);
	Instance->SetScalarParameterValue(TEXT("CloudCoverage"), VisualWeather.CloudCoverage);
	Instance->SetScalarParameterValue(TEXT("Fogginess"), VisualWeather.Fogginess);
	Instance->SetScalarParameterValue(TEXT("TimeOfDay"), TimeOfDay);
	Instance->SetScalarParameterValue(TEXT("SunElevation"), Celestial.SunElevation);
	Instance->SetVectorParameterValue(TEXT("WindDirection"), FLinearColor(FRotator(0.f, WindDirection, 0.f).Vector()));
}
