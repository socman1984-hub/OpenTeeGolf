// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TidewrightSettings.generated.h"

/** Whether the water is an endless sea or a bounded body. */
UENUM(BlueprintType)
enum class ETidewrightWaterMode : uint8
{
	/** Endless surface that follows the camera to the horizon. */
	Ocean,
	/** Rectangular body of water centred on the actor. */
	Lake
};

/** Resolution of each simulated wave band. Higher is sharper and costs more CPU. */
UENUM(BlueprintType)
enum class ETidewrightSimulationQuality : uint8
{
	Low UMETA(DisplayName = "Low (64)"),
	Medium UMETA(DisplayName = "Medium (128)"),
	High UMETA(DisplayName = "High (256)")
};

/** Everything that shapes the waves. All lengths are in metres, speeds in metres per second. */
USTRUCT(BlueprintType)
struct TIDEWRIGHT_API FTidewrightWaveSettings
{
	GENERATED_BODY()

	/** Wind speed ten metres above the water. The main control for how rough the water is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Wind", meta = (ClampMin = "0.1", ClampMax = "45.0", Units = "m/s"))
	float WindSpeed = 9.0f;

	/** Compass direction the wind blows towards, in degrees around the world Z axis (0 = +X). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Wind", meta = (ClampMin = "-360.0", ClampMax = "360.0", Units = "deg"))
	float WindDirection = 20.0f;

	/** Distance of open water the wind has blown over. Short fetch gives small steep waves (lakes), long fetch gives long rolling seas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0.05", ClampMax = "2000.0", Units = "km"))
	float Fetch = 150.0f;

	/** How tightly wind waves line up with the wind. 0 = scattered in all directions, 1 = long parallel crests. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Alignment = 0.55f;

	/** Sharpness of the peak of the wave spectrum. 1 = mature sea, 3.3 = young, growing sea, higher = very regular waves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind", AdvancedDisplay, meta = (ClampMin = "1.0", ClampMax = "7.0"))
	float PeakSharpness = 3.3f;

	/** Overall multiplier on wind wave height. 1 is physically derived from wind speed and fetch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Wind", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float HeightScale = 1.0f;

	/** Significant height of the swell: long waves arriving from a distant storm, independent of local wind. 0 disables swell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Swell", meta = (ClampMin = "0.0", ClampMax = "15.0", Units = "m"))
	float SwellHeight = 0.6f;

	/** Distance between swell crests. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Swell", meta = (ClampMin = "2.0", ClampMax = "600.0", Units = "m"))
	float SwellWavelength = 90.0f;

	/** Direction the swell travels towards, in degrees around the world Z axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Swell", meta = (ClampMin = "-360.0", ClampMax = "360.0", Units = "deg"))
	float SwellDirection = -35.0f;

	/** How much crests are pinched and troughs flattened. 0 = smooth sine-like waves, 1 = sharp crests on the edge of breaking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Shape", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float Choppiness = 0.85f;

	/** Water depth used for wave speed. Shallow water slows and shortens long waves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.2", ClampMax = "5000.0", Units = "m"))
	float Depth = 400.0f;

	/** Removes waves shorter than this. Raise it for a glassier surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.0", ClampMax = "5.0", Units = "m"))
	float SmallWaveCutoff = 0.02f;

	/**
	 * Size of the largest simulated wave band. It must be several times longer than the biggest waves.
	 * Three smaller bands are layered on top; none of them share a common multiple, which hides repetition.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "8.0", ClampMax = "8000.0", Units = "m"))
	float PatternSize = 620.0f;

	/** How readily foam forms where the surface folds. 0 = none, 1 = foam on almost every crest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Foam", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FoamCoverage = 0.6f;

	/** Seconds foam takes to fade after the crest that made it has passed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam", meta = (ClampMin = "0.05", ClampMax = "60.0", Units = "s"))
	float FoamLifetime = 5.0f;
};

/** How the water behaves where it meets land. Lengths are in metres. */
USTRUCT(BlueprintType)
struct TIDEWRIGHT_API FTidewrightShoreSettings
{
	GENERATED_BODY()

	/** Depth at which open-water waves reach their full height. In shallower water they are progressively flattened, long waves first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Shore", meta = (ClampMin = "0.1", ClampMax = "100.0", Units = "m"))
	float WaveDampingDepth = 5.0f;

	/** Height of the surf: the rolling waves that travel in to the beach and break. 0 disables surf. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Surf", meta = (ClampMin = "0.0", ClampMax = "6.0", Units = "m"))
	float SurfHeight = 0.7f;

	/** Distance between one line of surf and the next. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surf", meta = (ClampMin = "3.0", ClampMax = "200.0", Units = "m"))
	float SurfWavelength = 24.0f;

	/** Seconds between one surf wave reaching the beach and the next. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surf", meta = (ClampMin = "1.5", ClampMax = "30.0", Units = "s"))
	float SurfPeriod = 7.0f;

	/** Water depth at which surf waves break and turn to foam. Roughly one and a half to two times Surf Height looks natural. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Surf", meta = (ClampMin = "0.1", ClampMax = "12.0", Units = "m"))
	float BreakDepth = 1.3f;

	/** How far the water level rises at the beach after each surf wave arrives. On a gentle beach a small rise sends the waterline a long way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Surf", meta = (ClampMin = "0.0", ClampMax = "3.0", Units = "m"))
	float RunUpHeight = 0.22f;

	/** Foam lines the water's edge wherever the water is shallower than this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Foam", meta = (ClampMin = "0.0", ClampMax = "5.0", Units = "m"))
	float ShoreFoamDepth = 0.35f;

	/** Strength of shore and surf foam. 0 removes it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Foam", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ShoreFoamAmount = 1.0f;
};

/** The view from below the surface. */
USTRUCT(BlueprintType)
struct TIDEWRIGHT_API FTidewrightUnderwaterSettings
{
	GENERATED_BODY()

	/** Fog, colour and a waterline across the screen when the camera goes below the surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Underwater")
	bool bEnabled = true;

	/** How quickly things fade into the water with distance. 1 uses the water's own Absorption and Scatter; lower sees further. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Underwater", meta = (ClampMin = "0.05", ClampMax = "10.0"))
	float FogDensity = 1.0f;

	/** Brightness of the water itself when looking through it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Underwater", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float Brightness = 3.0f;

	/** Depth over which the underwater light fades to a third. Larger keeps deep water brighter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Underwater", meta = (ClampMin = "1.0", ClampMax = "500.0", Units = "m"))
	float LightFalloffDepth = 25.0f;

	/** Brightness of the sky seen through the surface when looking up from below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Underwater", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float SkyGlow = 3.5f;

	/** Darkness of the thin line where the surface crosses the lens. 0 removes it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Underwater", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WaterlineStrength = 0.6f;
};

/** Everything that shapes how the water is shaded. */
USTRUCT(BlueprintType)
struct TIDEWRIGHT_API FTidewrightLookSettings
{
	GENERATED_BODY()

	/** Colour of light scattered back out of the water body. This is the colour of deep water. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Water")
	FLinearColor ScatterColor = FLinearColor(0.0018f, 0.006f, 0.0085f, 1.0f);

	/** Amount of scattering per metre. Higher values make the water murkier and brighter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Water", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float ScatterDensity = 1.0f;

	/** Light absorbed per metre, per colour channel. Real water absorbs red fastest, which is why depth looks blue-green. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Water")
	FLinearColor Absorption = FLinearColor(0.45f, 0.09f, 0.06f, 1.0f);

	/** Forward bias of scattering. Higher values brighten wave crests lit from behind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "-0.9", ClampMax = "0.9"))
	float ScatterAnisotropy = 0.6f;

	/** Roughness of the surface up close. Distant water is roughened automatically from the waves it can no longer resolve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Surface", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Roughness = 0.04f;

	/** Strength of the automatic distance roughening. 1 is physically derived; lower gives a tighter sun glitter path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float DistanceRoughening = 1.0f;

	/** Multiplier on the fine surface normals. Does not move geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Surface", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float NormalStrength = 1.0f;

	/** Colour of foam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Foam")
	FLinearColor FoamColor = FLinearColor(0.82f, 0.86f, 0.88f, 1.0f);

	/** Overall foam amount multiplier applied in shading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Foam", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float FoamAmount = 1.0f;

	/** Size of the foam bubble pattern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foam", meta = (ClampMin = "0.2", ClampMax = "50.0", Units = "m"))
	float FoamPatternSize = 6.0f;
};
