// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TidewrightSettings.h"

/** One band of the wave spectrum, simulated on a periodic square patch. */
struct TIDEWRIGHT_API FTidewrightCascade
{
	/** Texels per side. Power of two. */
	int32 Resolution = 0;
	/** Patch side length in metres. */
	float LengthM = 0.0f;
	/** Wave numbers (rad/m) this band is responsible for: [KMin, KMax). */
	float KMin = 0.0f;
	float KMax = 0.0f;

	/** Variance of surface slope contributed by this band (dimensionless). */
	float SlopeVariance = 0.0f;
	/** Variance of surface height contributed by this band (m^2). */
	float HeightVariance = 0.0f;

	/** Mip chain. RGBA = (X displacement, Y displacement, height, foam), metres. Index 0 is full resolution. */
	TArray<TArray<FVector4f>> DisplacementMips;
	/** Mip chain. RGBA = (dHeight/dX, dHeight/dY, dDispX/dX, dDispY/dY). */
	TArray<TArray<FVector4f>> DerivativeMips;

	// Internal state.
	TArray<FVector2f> Noise;
	TArray<float> Amplitude;
	TArray<float> AmplitudeFrom;
	TArray<float> AmplitudeTo;
	TArray<float> Omega;
	TArray<FVector2f> Work[4];
	TArray<FVector2f> Scratch;
	TArray<float> Foam;
};

/**
 * Spectral wave simulation. Plain C++ with no engine object dependencies so it can be unit tested
 * and queried from gameplay code (buoyancy) with exactly the data the renderer displays.
 */
class TIDEWRIGHT_API FTidewrightSimulation
{
public:
	static constexpr int32 NumCascades = 4;

	/** Ratio between the patch lengths of consecutive bands. Deliberately not an integer. */
	static constexpr float CascadeRatio = 5.3f;

	/** Rebuilds everything. Call when resolution or seed change. */
	void Configure(const FTidewrightWaveSettings& InSettings, int32 InResolution, int32 InSeed);

	/** Moves to new wave settings, cross-fading wave heights over BlendSeconds so the surface never pops. */
	void SetSettings(const FTidewrightWaveSettings& InSettings, float BlendSeconds);

	/** Advances to the given absolute time. DeltaSeconds only drives foam build-up and preset blending. */
	void Update(double TimeSeconds, float DeltaSeconds);

	bool IsConfigured() const { return Resolution > 0; }
	int32 GetResolution() const { return Resolution; }
	const FTidewrightWaveSettings& GetSettings() const { return Settings; }
	const FTidewrightCascade& GetCascade(int32 Index) const { return Cascades[Index]; }

	/** Four times the standard deviation of surface height, the usual measure of sea state, in metres. */
	float GetSignificantWaveHeight() const;

	/**
	 * Samples the displaced surface above a horizontal position (metres, world axes).
	 * OutHeight is metres above the rest level. OutNormal is unit length. Returns false before the first update.
	 * BandScale, if given, is one multiplier per wave band (NumCascades of them), used where a shore flattens the waves.
	 */
	bool SampleSurface(const FVector2D& PositionM, float& OutHeight, FVector3f& OutNormal, FVector2f* OutUndisplacedOffset = nullptr, const float* BandScale = nullptr) const;

	/** Raw displacement (metres) of the surface point whose rest position is PositionM. */
	FVector3f SampleDisplacement(const FVector2D& PositionM, const float* BandScale = nullptr) const;

	/** Jacobian value below which foam forms for the current settings. Shared with the surface material. */
	float GetFoamThreshold() const;

	/** How quickly foam reaches full strength below the threshold. Shared with the surface material. */
	float GetFoamGain() const;

	/** Standard deviation of the displacement Jacobian for the current settings. */
	float GetJacobianDeviation() const;

	/** In-place inverse FFT of a Resolution x Resolution complex grid. Exposed for tests. */
	static void InverseFFT2D(FVector2f* Data, FVector2f* Scratch, int32 Size);

	/** Analytic spectral density of the given settings at a wave vector (m^4). Exposed for tests. */
	static double SpectrumDensity(const FTidewrightWaveSettings& InSettings, double Kx, double Ky);

private:
	void BuildAmplitudes(const FTidewrightWaveSettings& InSettings, int32 CascadeIndex, TArray<float>& OutAmplitude) const;
	void UpdateStatistics(FTidewrightCascade& Cascade) const;
	void UpdateCascade(FTidewrightCascade& Cascade, double TimeSeconds, float DeltaSeconds, float BlendAlpha, bool bBlending);
	void AccumulateFoam(int32 CascadeIndex, float DeltaSeconds);
	static void BuildMips(TArray<TArray<FVector4f>>& Mips, int32 Size);

	FTidewrightWaveSettings Settings;
	FTidewrightCascade Cascades[NumCascades];
	int32 Resolution = 0;
	int32 Seed = 0;
	float BlendDuration = 0.0f;
	float BlendElapsed = 0.0f;
	bool bBlendActive = false;
	bool bHasOutput = false;
};
