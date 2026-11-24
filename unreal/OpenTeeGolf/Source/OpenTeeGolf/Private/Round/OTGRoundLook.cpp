// The round's look and weather on the scene (see OTGRound.h): a clear summer day as golf games show one
// (the OTG.Look.* console variables), the weather set up for the round, and the weather on the sky,
// the sun, the clouds and the fog (OTG.Weather.*); the wind for each hole.

#include "Round/OTGRound.h"
#include "World/OTGRuntimeCourse.h"
#include "App/OTGHelperSubsystem.h"
#include "Round/OTGMiniGolf.h"

#include "Camera/CameraActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Texture2D.h"
#include "Minimap/SOTGMinimap.h"
#include "UI/Hud/SOTGPanels.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Course/OTGCourseData.h"
#include "OTGLaunchMonitorSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UI/Hud/SOTGHud.h"
#include "UI/Hud/SOTGRange.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGRound, Log, All);

#include "Round/OTGRoundShared.h"

// The course's look: a clear summer day, rich warm greens and a deep blue sky (tunable live)
static TAutoConsoleVariable<float> CVarLookExposure(TEXT("OTG.Look.Exposure"), -0.45f, TEXT("Exposure bias (stops; - darker)"));
static TAutoConsoleVariable<float> CVarLookSaturation(TEXT("OTG.Look.Saturation"), 1.1f, TEXT("Colour saturation (1: as is)"));
static TAutoConsoleVariable<float> CVarLookContrast(TEXT("OTG.Look.Contrast"), 1.1f, TEXT("Contrast (1: as is)"));
static TAutoConsoleVariable<float> CVarLookTemp(TEXT("OTG.Look.Temp"), 6900.f, TEXT("White balance (K; 6500 neutral, higher warmer)"));
static TAutoConsoleVariable<float> CVarLookSun(TEXT("OTG.Look.Sun"), 1.35f, TEXT("Sunlight strength (x the level's)"));
static TAutoConsoleVariable<float> CVarLookSunWarm(TEXT("OTG.Look.SunWarm"), 0.35f, TEXT("Sunlight warmth (0 white .. 1 golden)"));
static TAutoConsoleVariable<float> CVarLookSunPitch(TEXT("OTG.Look.SunPitch"), -42.f, TEXT("Sun height (degrees below the horizon's pitch; 999: the level's)"));
static TAutoConsoleVariable<float> CVarLookSunSide(TEXT("OTG.Look.SunSide"), 40.f, TEXT("The sun from behind the tee, this many degrees to the left (999: the level's own direction)"));
static TAutoConsoleVariable<float> CVarLookHaze(TEXT("OTG.Look.Haze"), 0.35f, TEXT("Haze in the air (x the level's; lower: a deeper blue sky)"));
static TAutoConsoleVariable<float> CVarLookFog(TEXT("OTG.Look.Fog"), 0.5f, TEXT("Height fog (x the level's)"));
static TAutoConsoleVariable<float> CVarLookSunSoft(TEXT("OTG.Look.SunSoft"), 1.2f, TEXT("The sun's size in the sky (degrees: softer shadow edges; the real sun is 0.53)"));
static TAutoConsoleVariable<float> CVarLookSlopeBias(TEXT("OTG.Look.SlopeBias"), 2.0f, TEXT("Shadow slope bias (smooths the hills' own shadow edges in a low sun)"));
static TAutoConsoleVariable<float> CVarLookTreeLift(TEXT("OTG.Look.TreeLift"), 0.25f, TEXT("Sky light let into shade (Lumen skylight leaking: lighter tree crowns; 0 none)"));
static TAutoConsoleVariable<float> CVarLookShadows(TEXT("OTG.Look.Shadows"), 1.18f, TEXT("The darkest tones lifted (gain; 1: as is)"));
static TAutoConsoleVariable<float> CVarLookSky(TEXT("OTG.Look.Sky"), 1.3f, TEXT("Sky light (x the level's)"));
static TAutoConsoleVariable<float> CVarLookVignette(TEXT("OTG.Look.Vignette"), 0.3f, TEXT("Vignette"));
// the weather's sky, at full cloud (a clear sky: the level's own)
static TAutoConsoleVariable<float> CVarWxCover(TEXT("OTG.Weather.Cover"), 0.7f, TEXT("Cloud coverage added at full cloud (the cloud material's offset)"));
static TAutoConsoleVariable<float> CVarWxDensity(TEXT("OTG.Weather.Density"), 4.f, TEXT("Cloud density at full cloud (x the level's)"));
static TAutoConsoleVariable<float> CVarWxGrey(TEXT("OTG.Weather.Grey"), 0.85f, TEXT("How grey an overcast sky goes (0: blue as ever, 1: all grey)"));
static TAutoConsoleVariable<float> CVarWxSun(TEXT("OTG.Weather.Sun"), 0.06f, TEXT("Sunlight left under full cloud (x a clear day's)"));
static TAutoConsoleVariable<float> CVarWxMaskAt(TEXT("OTG.Weather.MaskAt"), 0.75f, TEXT("Cloud from which the clouds cover the whole sky (not the cloud material's patches)"));
static TAutoConsoleVariable<float> CVarWxRainDark(TEXT("OTG.Weather.RainDark"), 0.6f, TEXT("How dark the clouds go in rain (a storm: all the way)"));
static TAutoConsoleVariable<int32> CVarWxOwnClouds(TEXT("OTG.Weather.OwnClouds"), 1, TEXT("1: the game's own clouds (M_Clouds); 0: the engine's sample clouds"));
static TAutoConsoleVariable<float> CVarWxCloudFill(TEXT("OTG.Weather.CloudFill"), 0.f, TEXT("A test of the clouds: 1 the noise as it comes, 2 the layer solid"));
static TAutoConsoleVariable<int32> CVarWxCloudLayer(TEXT("OTG.Weather.CloudLayer"), 1, TEXT("1: our clouds' layer 1.5 km up, 3 km thick; 0: the level's own"));
static TAutoConsoleVariable<float> CVarWxSunOwn(TEXT("OTG.Weather.SunOwn"), 0.4f, TEXT("Sunlight left under full cloud with our own clouds (they shade the ground themselves)"));
static TAutoConsoleVariable<float> CVarWxSoft(TEXT("OTG.Weather.Soft"), 20.f, TEXT("The sun's size under full cloud (degrees: soft, faint shadows)"));

// ------------------------------------------------------------------ the look

// (every look and weather setting, the hole and the time of day, hashed: changed, the look's applied again)
uint32 FOTGRoundScene::LookStampNow() const
{
	uint32 Stamp = 0;
	for (const auto* V : { &CVarLookExposure, &CVarLookSaturation, &CVarLookContrast, &CVarLookTemp, &CVarLookSun, &CVarLookSunWarm, &CVarLookSunPitch, &CVarLookSunSide, &CVarLookHaze, &CVarLookFog, &CVarLookVignette, &CVarLookTreeLift, &CVarLookShadows, &CVarLookSky, &CVarLookSunSoft, &CVarLookSlopeBias, &CVarWxCover, &CVarWxDensity, &CVarWxGrey, &CVarWxSun, &CVarWxSoft, &CVarWxMaskAt, &CVarWxRainDark, &CVarWxCloudFill, &CVarWxSunOwn })
		Stamp = HashCombine(Stamp, GetTypeHash(V->GetValueOnGameThread()));
	return HashCombine(HashCombine(HashCombine(Stamp, GetTypeHash(CVarWxOwnClouds.GetValueOnGameThread())), GetTypeHash(Round->HoleIndex)), GetTypeHash(Round->SunHour));
}

// A clear summer day as golf games show one: the level's sun stronger, warmer and higher, less haze
// (a deeper blue), and the picture graded richer - more saturated and contrasty, a little warm,
// exposed a touch darker so the greens aren't washed out. The weather then works from this.
void FOTGRoundScene::ApplyLook()
{
	LookStamp = LookStampNow();
	// (the time of day: the sun's height and bearing then, and how much day there is - the sun and
	// sky dimming through dawn and dusk)
	double Elev = 45, Az = 180;
	const bool bTimed = Round->SunHour >= 0;
	if (bTimed) Round->SunAt(Round->SunHour, Elev, Az);
	const float SunDay = bTimed ? float(FMath::SmoothStep(-3.0, 8.0, Elev)) : 1.f;
	const float SkyDay = bTimed ? FMath::Max(0.04f, float(FMath::SmoothStep(-10.0, 10.0, Elev))) : 1.f;
	if (!LookVolume)
	{
		LookVolume = Round->GetWorld()->SpawnActor<APostProcessVolume>();
		LookVolume->bUnbound = true;
		LookVolume->Priority = 10;
	}
	FPostProcessSettings& P = LookVolume->Settings;
	P.bOverride_AutoExposureBias = true;
	P.AutoExposureBias = CVarLookExposure.GetValueOnGameThread();
	const float Sat = CVarLookSaturation.GetValueOnGameThread(), Con = CVarLookContrast.GetValueOnGameThread();
	P.bOverride_ColorSaturation = true;
	P.ColorSaturation = FVector4(Sat, Sat, Sat, 1);
	P.bOverride_ColorContrast = true;
	P.ColorContrast = FVector4(Con, Con, Con, 1);
	P.bOverride_WhiteTemp = true;
	P.WhiteTemp = CVarLookTemp.GetValueOnGameThread();
	P.bOverride_VignetteIntensity = true;
	P.VignetteIntensity = CVarLookVignette.GetValueOnGameThread();
	// (lighter trees: their crowns' insides get some of the sky's light, and the darkest tones lift)
	P.bOverride_LumenSkylightLeaking = true;
	P.LumenSkylightLeaking = CVarLookTreeLift.GetValueOnGameThread();
	const float Sh = CVarLookShadows.GetValueOnGameThread();
	P.bOverride_ColorGainShadows = true;
	P.ColorGainShadows = FVector4(Sh, Sh, Sh, 1);
	// (the Meridian sky: the sun, sky, fog and clouds are its - OTGSky.cpp)
	if (EnsureSky())
	{
		ApplySky();
		return;
	}
	for (TActorIterator<ADirectionalLight> It(Round->GetWorld()); It; ++It)
	{
		UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		if (!L) continue;
		if (OrigSun < 0) { OrigSun = L->Intensity; OrigSunRot = It->GetActorRotation(); OrigSunColour = L->GetLightColor(); }
		L->SetIntensity(OrigSun * CVarLookSun.GetValueOnGameThread() * SunDay);
		// (smooth shadow edges on the hills: the ground is a mesh on a 1 m grid, and a low sun along
		// it shadows its facets in a comb of teeth - a slightly softer sun and more slope bias)
		L->SetLightSourceAngle(CVarLookSunSoft.GetValueOnGameThread());
		L->SetShadowSlopeBias(CVarLookSlopeBias.GetValueOnGameThread());
		L->SetLightColor(FMath::Lerp(OrigSunColour, FLinearColor(1.f, 0.86f, 0.68f), CVarLookSunWarm.GetValueOnGameThread()));
		// (lit from behind the player on every hole, a little to one side, as golf games light them:
		// looking into the sun the trees are dark shapes)
		if (bTimed)
		{
			// (towards the sun: X east, Y south; the light travels the other way)
			const double E = FMath::DegreesToRadians(Elev), A = FMath::DegreesToRadians(Az);
			const FVector ToSun(FMath::Sin(A) * FMath::Cos(E), -FMath::Cos(A) * FMath::Cos(E), FMath::Sin(E));
			It->SetActorRotation((-ToSun).Rotation());
			break;
		}
		const float Pitch = CVarLookSunPitch.GetValueOnGameThread(), Side = CVarLookSunSide.GetValueOnGameThread();
		double Yaw = OrigSunRot.Yaw;
		if (Side < 900 && Round->Data && Round->Data->Holes.IsValidIndex(Round->HoleIndex))
		{
			const FOTGHole& H = Round->Hole();
			const otg::V3 T = Round->TeeOf(H, Round->TeeName);
			const double Hd = HeadingTo(T, H.GreenCenter.X, H.GreenCenter.Z);
			Yaw = FMath::RadiansToDegrees(FMath::Atan2(-FMath::Cos(Hd), FMath::Sin(Hd))) + Side; // (the light travelling down the hole)
		}
		It->SetActorRotation(FRotator(Pitch > 900 ? OrigSunRot.Pitch : Pitch, Yaw, OrigSunRot.Roll));
		break;
	}
	for (TObjectIterator<USkyAtmosphereComponent> It; It; ++It)
	{
		if (It->GetWorld() != Round->GetWorld()) continue;
		if (OrigMie < 0) { OrigMie = It->MieScatteringScale; OrigRayleigh = It->RayleighScatteringScale; }
		It->SetMieScatteringScale(OrigMie * CVarLookHaze.GetValueOnGameThread());
		// (the level's own blue back: the weather greys it from this, not from what it greyed last
		// time - each new time of day under cloud would grey it again, to a black sky)
		It->SetRayleighScatteringScale(OrigRayleigh);
		break;
	}
	for (TActorIterator<ASkyLight> It(Round->GetWorld()); It; ++It)
	{
		USkyLightComponent* SL = It->GetLightComponent();
		if (!SL) continue;
		if (OrigSky < 0) OrigSky = SL->Intensity;
		SL->SetIntensity(OrigSky * CVarLookSky.GetValueOnGameThread() * SkyDay);
		if (!SL->bRealTimeCapture) SL->RecaptureSky();   // (the sky's own colour at this hour)
		break;
	}
	for (TActorIterator<AExponentialHeightFog> It(Round->GetWorld()); It; ++It)
	{
		UExponentialHeightFogComponent* F = It->GetComponent();
		if (!F) continue;
		if (OrigFog < 0) OrigFog = F->FogDensity;
		F->SetFogDensity(OrigFog * CVarLookFog.GetValueOnGameThread());
		break;
	}
	// (the weather works from these now)
	BaseSun = BaseFogDensity = BaseRayleigh = BaseMie = BaseSkyLight = -1;
}

// ------------------------------------------------------------------ the weather

void AOTGRound::SetupWeather()
{
	FString Mode = Setup_.Weather.IsEmpty() ? FString(TEXT("live")) : Setup_.Weather;
	if (bRange && Mode == TEXT("live")) Mode = TEXT("calm");   // (the range is nowhere: calm, until the weather panel changes it)
	// (-weather=<key> on the command line: for the round's start only, so the weather panel can change it)
	if (!bWeatherFromCommandLine)
	{
		bWeatherFromCommandLine = true;
		if (FParse::Value(FCommandLine::Get(), TEXT("weather="), Mode)) Setup_.Weather = Mode;
	}
	WindRel.Reset();
	for (int32 i = 0; i < Data->Holes.Num(); i++) WindRel.Add(Setup_.WindDir == TEXT("rand") ? FMath::FRand() * 360 : FCString::Atod(*Setup_.WindDir));
	if (Mode == TEXT("custom")) Weather = FOTGWeather::Custom(Setup_.WindMph);
	else Weather = FOTGWeather::Preset(Mode);
	if (Mode == TEXT("live"))
	{
		// (breezy until the course's real weather arrives, or if it can't)
		Weather = FOTGWeather::Preset(TEXT("breezy"));
		if (Data->Latitude == 0 && Data->Longitude == 0) HudView.Toast(TEXT("This course has no location: breezy weather"), 4.f);
		else
		{
			TWeakObjectPtr<AOTGRound> Self(this);
			FOTGWeather::FetchLive(Data->Latitude, Data->Longitude, [Self](bool bOk, const FOTGWeather& W, const FString& Why) {
				AOTGRound* R = Self.Get();
				if (!R) return;
				if (!bOk)
				{
					R->HudView.Toast(FString::Printf(TEXT("Live weather unavailable (%s): breezy"), *Why), 4.f);
					return;
				}
				R->Weather = W;
				R->Scene.ApplyWeatherToScene();
				R->HudView.UpdateHudInfo();
				R->HudView.Toast(FString::Printf(TEXT("Weather at the course: %s, %.0f\u00B0C, wind %.0f mph"), *W.Label, W.TempC, W.WindMph), 4.f);
			});
		}
	}
	Scene.ApplyWeatherToScene();
	UE_LOG(LogOTGRound, Display, TEXT("Weather: %s, wind %.0f mph (gusts %.0f), %.0f C"), *Weather.Label, Weather.WindMph, Weather.GustMph, Weather.TempC);
}

// Fog thickens the level's height fog (rain a little too); cloud dims the sun and covers the sky;
// a storm darkens the clouds
void FOTGRoundScene::ApplyWeatherToScene()
{
	if (EnsureSky())
	{
		ApplySky();
		return;
	}
	for (TActorIterator<AVolumetricCloud> It(Round->GetWorld()); It; ++It)
	{
		UVolumetricCloudComponent* VC = It->FindComponentByClass<UVolumetricCloudComponent>();
		if (!VC) continue;
		// our own clouds (M_Clouds: make_sky.py): the weather's cover, the kind of cloud, carried by
		// the wind, darker in rain
		if (CVarWxOwnClouds.GetValueOnGameThread() > 0)
		{
			if (!OwnCloudMat)
				if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenTeeGolf/Game/M_Clouds.M_Clouds")))
				{
					OwnCloudMat = UMaterialInstanceDynamic::Create(M, Round);
					UE_LOG(LogOTGRound, Display, TEXT("Clouds: our own (the layer %.1f km up, %.1f km thick)"), VC->LayerBottomAltitude, VC->LayerHeight);
				}
			if (OwnCloudMat)
			{
				if (VC->GetMaterial() != OwnCloudMat) VC->SetMaterial(OwnCloudMat);
				const double Cloudy = Round->Weather.RainMm > 0.05 ? FMath::Max(Round->Weather.Cloud, 0.92) : Round->Weather.Cloud;
				const float Storm = float(FMath::Clamp((Round->Weather.RainMm - 0.05) / 6.0, 0.0, 1.0));
				// (the layer: the material works in the same metres)
				if (CVarWxCloudLayer.GetValueOnGameThread() > 0)
				{
					VC->SetLayerBottomAltitude(1.5f);
					VC->SetLayerHeight(3.f);
				}
				OwnCloudMat->SetScalarParameterValue(TEXT("Bottom"), VC->LayerBottomAltitude * 1000.f);
				OwnCloudMat->SetScalarParameterValue(TEXT("Thick"), VC->LayerHeight * 1000.f);
				OwnCloudMat->SetScalarParameterValue(TEXT("Fill"), CVarWxCloudFill.GetValueOnGameThread());
				OwnCloudMat->SetScalarParameterValue(TEXT("Cover"), float(Cloudy));
				OwnCloudMat->SetScalarParameterValue(TEXT("Stratus"), float(FMath::SmoothStep(0.75, 1.0, Cloudy)));
				OwnCloudMat->SetScalarParameterValue(TEXT("Density"), FMath::Lerp(0.035f, 0.05f, float(Cloudy)) * (1.f + Storm * 2.f) * CVarWxDensity.GetValueOnGameThread() / 4.f);
				OwnCloudMat->SetVectorParameterValue(TEXT("Albedo"), FMath::Lerp(FLinearColor(0.93f, 0.93f, 0.95f), FLinearColor(0.32f, 0.34f, 0.38f), Storm > 0 ? 0.45f + 0.55f * Storm : 0.f));
				// (the wind up there: the hole's wind, in the world, faster aloft - and never quite still)
				double Mps = 0, FromDeg = 0;
				Round->HoleWind(Mps, FromDeg);
				const double F = FMath::DegreesToRadians(FromDeg), Aloft = FMath::Max(3.0, Mps * 2.5);
				OwnCloudMat->SetScalarParameterValue(TEXT("WindX"), float(-FMath::Sin(F) * Aloft));
				OwnCloudMat->SetScalarParameterValue(TEXT("WindY"), float(FMath::Cos(F) * Aloft));
				break;
			}
		}
		if (!CloudMat)
		{
			UMaterialInterface* Base = VC->GetMaterial();
			if (!Base) break;
			Base->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Cloud_GlobalCoverage")), BaseCoverage);
			Base->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Cloud_GlobalDensity")), BaseDensity);
			CloudMat = UMaterialInstanceDynamic::Create(Base, Round);
			VC->SetMaterial(CloudMat);
			UE_LOG(LogOTGRound, Display, TEXT("Clouds: coverage %.2f, density %.2f in the level"), BaseCoverage, BaseDensity);
		}
		const float Cl = float(Round->Weather.Cloud);
		// (coverage is an offset: the level's own about right for a fair day's few clouds)
		CloudMat->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), BaseCoverage + FMath::Lerp(-0.25f, CVarWxCover.GetValueOnGameThread(), Cl));
		CloudMat->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"), BaseDensity * FMath::Lerp(0.8f, CVarWxDensity.GetValueOnGameThread(), Cl));
		// (the engine's clouds are kept to the patches of a mask: an overcast sky clouds over everywhere)
		{
			static UTexture* Mask0 = nullptr;
			static TWeakObjectPtr<UTexture> White;
			if (!White.IsValid()) White = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
			if (!Mask0) CloudMat->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("Layout_GlobalCloudMask")), Mask0);
			const bool bOvercast = Cl >= CVarWxMaskAt.GetValueOnGameThread();
			if (UTexture* T = bOvercast ? White.Get() : Mask0) CloudMat->SetTextureParameterValue(TEXT("Layout_GlobalCloudMask"), T);
		}
		// (rain: darker clouds - their colour; the material's own storm clouds thin the deck out to a
		// few streaks, so not those)
		{
			static FLinearColor Albedo0(-1, -1, -1);
			if (Albedo0.R < 0 && !CloudMat->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Cloud_AlbedoColor")), Albedo0)) Albedo0 = FLinearColor::White;
			const float Dark = Round->Weather.RainMm >= 5 ? 1.f : Round->Weather.RainMm > 0.05 ? CVarWxRainDark.GetValueOnGameThread() : 0.f;
			CloudMat->SetVectorParameterValue(TEXT("Cloud_AlbedoColor"), FMath::Lerp(Albedo0, FLinearColor(0.3f, 0.32f, 0.35f), Dark));
			CloudMat->SetScalarParameterValue(TEXT("StormClouds"), 0.f);
		}
		break;
	}
	for (TActorIterator<AExponentialHeightFog> It(Round->GetWorld()); It; ++It)
	{
		UExponentialHeightFogComponent* F = It->GetComponent();
		if (!F) continue;
		if (BaseFogDensity < 0) BaseFogDensity = F->FogDensity;
		F->SetFogDensity(FMath::Lerp(BaseFogDensity, 0.25f, float(FMath::Min(1.0, Round->Weather.Fog + (Round->Weather.RainMm > 0.05 ? 0.06 : 0.0)))));
		break;
	}
	// an overcast sky: grey rather than blue - more haze (white), the blue taken out of what the sky
	// gives - and the sun behind the cloud: hardly any direct light, its shadows faint and soft, the
	// sky's light doing the lighting (rain: overcast whatever the cloud says)
	const double Cloudy = Round->Weather.RainMm > 0.05 ? FMath::Max(Round->Weather.Cloud, 0.9) : Round->Weather.Cloud;
	const float Over = float(FMath::SmoothStep(0.45, 0.95, Cloudy));
	const float Dim = float(FMath::SmoothStep(0.3, 0.95, Cloudy));
	const float Grey = Over * CVarWxGrey.GetValueOnGameThread();
	for (TObjectIterator<USkyAtmosphereComponent> It; It; ++It)
	{
		if (It->GetWorld() != Round->GetWorld()) continue;
		if (BaseRayleigh < 0) { BaseRayleigh = It->RayleighScatteringScale; BaseMie = It->MieScatteringScale; }
		if (BaseSkyLum.R < 0) BaseSkyLum = It->SkyLuminanceFactor;
		It->SetRayleighScatteringScale(BaseRayleigh * FMath::Lerp(1.f, 0.6f, Over));
		It->SetMieScatteringScale(BaseMie * FMath::Lerp(1.f, 10.f, Over));
		// (a blue sky's light is mostly blue: less of the blue, and of the green, leaves it grey)
		It->SetSkyLuminanceFactor(BaseSkyLum * FMath::Lerp(FLinearColor::White, FLinearColor(0.95f, 0.72f, 0.42f), Grey));
		break;
	}
	for (TActorIterator<ASkyLight> It(Round->GetWorld()); It; ++It)
	{
		USkyLightComponent* SL = It->GetLightComponent();
		if (!SL) continue;
		if (BaseSkyLight < 0) BaseSkyLight = SL->Intensity;
		SkyNow = BaseSkyLight * FMath::Lerp(1.f, 0.85f, Over);
		SL->SetIntensity(SkyNow);
		break;
	}
	for (TActorIterator<ADirectionalLight> It(Round->GetWorld()); It; ++It)
	{
		UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		if (!L) continue;
		if (BaseSun < 0) BaseSun = L->Intensity;
		// (our own clouds shade the ground themselves, and are lit by this sun: it's dimmed less)
		L->SetIntensity(BaseSun * FMath::Lerp(1.f, OwnCloudMat ? CVarWxSunOwn.GetValueOnGameThread() : CVarWxSun.GetValueOnGameThread(), Dim));
		L->SetLightSourceAngle(FMath::Lerp(CVarLookSunSoft.GetValueOnGameThread(), CVarWxSoft.GetValueOnGameThread(), Dim));
		// (the clouds' shadows on the course, drifting with them)
		if (OwnCloudMat && !L->bCastCloudShadows)
		{
			L->bCastCloudShadows = true;
			L->CloudShadowOnSurfaceStrength = 0.85f;
			L->CloudShadowExtent = 30.f;   // (km round the camera)
			L->MarkRenderStateDirty();
		}
		break;
	}
	// (the rain's streaks: grey-white under a dark sky, not glowing)
	if (Round->RainFx.RainMat) Round->RainFx.RainMat->SetVectorParameterValue(TEXT("Tint"), FMath::Lerp(FLinearColor(0.75f, 0.78f, 0.82f), FLinearColor(0.58f, 0.61f, 0.66f), Dim));
	// (rain and storms are darker days: the eye's exposure doesn't lift them back to a bright one)
	if (LookVolume)
	{
		const float Gloom = float(FMath::Clamp((Round->Weather.RainMm - 0.05) / 7.0, 0.0, 1.0));
		LookVolume->Settings.AutoExposureBias = CVarLookExposure.GetValueOnGameThread() - (Round->Weather.RainMm > 0.05 ? 0.3f + 0.9f * Gloom : 0.f) - 0.25f * Over;
	}
}

// The steady wind on this hole, in course space (FromDeg: where it blows from, as the aim's heading)
void AOTGRound::HoleWind(double& Mps, double& FromDeg) const
{
	Mps = 0;
	FromDeg = 0;
	if (Weather.WindMph < 0.5 || bMini) return;   // (the putt-putt's sheltered)
	Mps = Weather.WindMph * 0.44704;
	if (Weather.bRelative)
	{
		const FOTGHole& H = Hole();
		const otg::V3 T = TeeOf(H, TeeName);
		FromDeg = FMath::RadiansToDegrees(HeadingTo(T, H.GreenCenter.X, H.GreenCenter.Z)) + (WindRel.IsValidIndex(HoleIndex) ? WindRel[HoleIndex] : 0);
	}
	else FromDeg = Weather.WindFromMet; // (the course's z is north)
}

// "Wind 9 mph, into you from the left": against the way the shot is aimed
FString AOTGRound::WindText() const
{
	double Mps, FromDeg;
	HoleWind(Mps, FromDeg);
	if (Mps < 0.5 * 0.44704) return TEXT("No wind");
	double Rel = FMath::Fmod(FromDeg - FMath::RadiansToDegrees(Heading) + 540.0, 360.0) - 180.0; // (0: from straight ahead, + from the right)
	const double A = FMath::Abs(Rel);
	const FString SideS = Rel > 0 ? TEXT("right") : TEXT("left");
	FString Where;
	if (A < 22.5) Where = TEXT("into you");
	else if (A < 67.5) Where = TEXT("into you from the ") + SideS;
	else if (A < 112.5) Where = TEXT("from the ") + SideS;
	else if (A < 157.5) Where = TEXT("helping, from the ") + SideS;
	else Where = TEXT("helping");
	return FString::Printf(TEXT("Wind %.0f mph %s"), Weather.WindMph, *Where);
}
