// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TidewrightSettings.h"

/** What the shore field knows about one point of the water. */
struct TIDEWRIGHT_API FTidewrightShorePoint
{
	/** Water depth at rest in metres. Negative on land. Very large away from any scanned shore. */
	float Depth = 1000.0f;
	/** Distance to the waterline in metres. Negative on land. */
	float Distance = 1000.0f;
	/** Unit direction towards the nearest waterline, or zero. */
	FVector2f ToShore = FVector2f::ZeroVector;
	/** 1 inside the scanned area, fading to 0 at its edge. */
	float Weight = 0.0f;
};

/** Shore motion at one point and time. Mirrors the surface material exactly. */
struct TIDEWRIGHT_API FTidewrightShoreMotion
{
	/** Multiplier on each open-water wave band, largest band first. */
	float BandScale[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	/** Height added by surf and run-up, metres. */
	float Height = 0.0f;
	/** Sideways push of the surf, metres. */
	FVector2f Horizontal = FVector2f::ZeroVector;
	/** Surface slope added by the surf. */
	FVector2f Slope = FVector2f::ZeroVector;
	/** Shore and surf foam, 0 to 1. */
	float Foam = 0.0f;
};

/**
 * Depth and distance-to-shore over a rectangle of the world, built from scanned terrain heights.
 * Plain C++ so it can be unit tested and shared between the surface material and buoyancy.
 */
class TIDEWRIGHT_API FTidewrightShoreField
{
public:
	/**
	 * @param TerrainHeightCm  Resolution x Resolution world heights of the ground, row by row along +Y, each row along +X.
	 * @param CentreCm, HalfSizeCm  The rectangle the grid covers.
	 * @param WaterLevelCm  Rest level of the water.
	 */
	void Build(const TArray<float>& TerrainHeightCm, int32 InResolution, const FVector2D& InCentreCm, const FVector2D& InHalfSizeCm, double WaterLevelCm);

	void Reset();
	bool IsValid() const { return Resolution > 0; }
	int32 GetResolution() const { return Resolution; }
	const FVector2D& GetCentreCm() const { return CentreCm; }
	const FVector2D& GetHalfSizeCm() const { return HalfSizeCm; }
	double GetWaterLevelCm() const { return WaterLevelCm; }

	/** RGBA = (depth m, distance to shore m, direction to shore x, y). */
	const TArray<FVector4f>& GetTexels() const { return Texels; }

	/** Fraction of the scanned area that is land. */
	float GetLandFraction() const { return LandFraction; }

	FTidewrightShorePoint Sample(const FVector2D& WorldCm) const;

	/** Surf, run-up, damping and foam for a point. Time is in seconds. */
	static FTidewrightShoreMotion Evaluate(const FTidewrightShorePoint& Point, const FTidewrightShoreSettings& Settings, const FVector2D& WorldCm, double TimeSeconds);

	/** Time wrapped so that single precision keeps the surf smooth, without a visible jump at the wrap. */
	static float WrapTime(double TimeSeconds, float SurfPeriod);

private:
	TArray<FVector4f> Texels;
	FVector2D CentreCm = FVector2D::ZeroVector;
	FVector2D HalfSizeCm = FVector2D(1.0, 1.0);
	double WaterLevelCm = 0.0;
	int32 Resolution = 0;
	float LandFraction = 0.0f;
};
