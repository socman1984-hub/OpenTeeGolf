// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "TidewrightShoreField.h"

#include <cmath>

namespace TidewrightShore
{
	static constexpr double Far = 1.0e18;
	static constexpr float DeepWater = 1000.0f;

	/** One dimensional squared distance transform (Felzenszwalb and Huttenlocher). */
	static void Transform1D(const double* F, int32 Count, double Spacing, double* Out, int32* V, double* Z)
	{
		const double S2 = Spacing * Spacing;
		int32 K = 0;
		V[0] = 0;
		Z[0] = -Far;
		Z[1] = Far;
		for (int32 Q = 1; Q < Count; ++Q)
		{
			double S;
			for (;;)
			{
				const int32 P = V[K];
				S = ((F[Q] + S2 * Q * Q) - (F[P] + S2 * P * P)) / (2.0 * S2 * (Q - P));
				if (S <= Z[K] && K > 0)
				{
					--K;
					continue;
				}
				break;
			}
			if (S <= Z[K])
			{
				// Replaces the very first parabola.
				V[K] = Q;
				Z[K + 1] = Far;
				continue;
			}
			++K;
			V[K] = Q;
			Z[K] = S;
			Z[K + 1] = Far;
		}
		K = 0;
		for (int32 Q = 0; Q < Count; ++Q)
		{
			while (Z[K + 1] < Q)
			{
				++K;
			}
			const int32 P = V[K];
			Out[Q] = S2 * (Q - P) * (Q - P) + F[P];
		}
	}

	/** Distance from every cell to the nearest cell where Inside is true, in centimetres. */
	static void DistanceTo(const TArray<bool>& Inside, bool bTarget, int32 Size, double SpacingX, double SpacingY, TArray<double>& OutDistance)
	{
		OutDistance.SetNumUninitialized(Size * Size);
		TArray<double> Line, Result, Z;
		TArray<int32> V;
		Line.SetNumUninitialized(Size);
		Result.SetNumUninitialized(Size);
		Z.SetNumUninitialized(Size + 1);
		V.SetNumUninitialized(Size);

		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				Line[X] = Inside[Y * Size + X] == bTarget ? 0.0 : Far;
			}
			Transform1D(Line.GetData(), Size, SpacingX, Result.GetData(), V.GetData(), Z.GetData());
			for (int32 X = 0; X < Size; ++X)
			{
				OutDistance[Y * Size + X] = Result[X];
			}
		}
		for (int32 X = 0; X < Size; ++X)
		{
			for (int32 Y = 0; Y < Size; ++Y)
			{
				Line[Y] = OutDistance[Y * Size + X];
			}
			Transform1D(Line.GetData(), Size, SpacingY, Result.GetData(), V.GetData(), Z.GetData());
			for (int32 Y = 0; Y < Size; ++Y)
			{
				OutDistance[Y * Size + X] = FMath::Sqrt(FMath::Min(Result[Y], Far));
			}
		}
	}

	static FORCEINLINE float Smooth(float Value)
	{
		return Value * Value * (3.0f - 2.0f * Value);
	}

	static FORCEINLINE float SmoothStep(float A, float B, float X)
	{
		return Smooth(FMath::Clamp((X - A) / (B - A), 0.0f, 1.0f));
	}

	static FORCEINLINE float Frac(float Value)
	{
		return Value - FMath::FloorToFloat(Value);
	}
}

void FTidewrightShoreField::Reset()
{
	Texels.Reset();
	Resolution = 0;
	LandFraction = 0.0f;
}

void FTidewrightShoreField::Build(const TArray<float>& TerrainHeightCm, int32 InResolution, const FVector2D& InCentreCm, const FVector2D& InHalfSizeCm, double InWaterLevelCm)
{
	Reset();
	if (InResolution < 4 || TerrainHeightCm.Num() != InResolution * InResolution)
	{
		return;
	}

	const int32 Size = InResolution;
	const int32 Count = Size * Size;
	Resolution = Size;
	CentreCm = InCentreCm;
	HalfSizeCm = FVector2D(FMath::Max(InHalfSizeCm.X, 1.0), FMath::Max(InHalfSizeCm.Y, 1.0));
	WaterLevelCm = InWaterLevelCm;

	const double SpacingX = 2.0 * HalfSizeCm.X / Size;
	const double SpacingY = 2.0 * HalfSizeCm.Y / Size;

	TArray<float> Depth;
	TArray<bool> Water;
	Depth.SetNumUninitialized(Count);
	Water.SetNumUninitialized(Count);
	int32 LandCells = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Depth[Index] = FMath::Clamp(static_cast<float>((InWaterLevelCm - TerrainHeightCm[Index]) * 0.01), -50.0f, 200.0f);
		Water[Index] = Depth[Index] > 0.0f;
		LandCells += Water[Index] ? 0 : 1;
	}
	LandFraction = static_cast<float>(LandCells) / Count;

	// Signed distance to the waterline: positive in water, negative on land.
	TArray<float> Distance;
	Distance.Init(TidewrightShore::DeepWater, Count);
	if (LandCells > 0 && LandCells < Count)
	{
		TArray<double> ToLand, ToWater;
		TidewrightShore::DistanceTo(Water, false, Size, SpacingX, SpacingY, ToLand);
		TidewrightShore::DistanceTo(Water, true, Size, SpacingX, SpacingY, ToWater);
		const double HalfCell = 0.25 * (SpacingX + SpacingY);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Signed = Water[Index] ? ToLand[Index] - HalfCell : -(ToWater[Index] - HalfCell);
			Distance[Index] = static_cast<float>(Signed * 0.01);
		}

		// The transform measures to cell centres, which leaves stair steps along a diagonal coast. Soften them.
		TArray<float> Blurred;
		Blurred.SetNumUninitialized(Count);
		for (int32 Pass = 0; Pass < 3; ++Pass)
		{
			for (int32 Y = 0; Y < Size; ++Y)
			{
				for (int32 X = 0; X < Size; ++X)
				{
					float Sum = 0.0f;
					for (int32 Dy = -1; Dy <= 1; ++Dy)
					{
						for (int32 Dx = -1; Dx <= 1; ++Dx)
						{
							Sum += Distance[FMath::Clamp(Y + Dy, 0, Size - 1) * Size + FMath::Clamp(X + Dx, 0, Size - 1)];
						}
					}
					Blurred[Y * Size + X] = Sum / 9.0f;
				}
			}
			Swap(Distance, Blurred);
		}
	}
	else if (LandCells == Count)
	{
		Distance.Init(-TidewrightShore::DeepWater, Count);
	}

	Texels.SetNumUninitialized(Count);
	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const int32 X0 = FMath::Max(X - 1, 0), X1 = FMath::Min(X + 1, Size - 1);
			const int32 Y0 = FMath::Max(Y - 1, 0), Y1 = FMath::Min(Y + 1, Size - 1);
			// Distance grows away from the shore, so the shore is down its gradient.
			FVector2f Gradient(
				(Distance[Y * Size + X1] - Distance[Y * Size + X0]) / static_cast<float>((X1 - X0) * SpacingX),
				(Distance[Y1 * Size + X] - Distance[Y0 * Size + X]) / static_cast<float>((Y1 - Y0) * SpacingY));
			const FVector2f Direction = -Gradient.GetSafeNormal();
			Texels[Y * Size + X] = FVector4f(Depth[Y * Size + X], Distance[Y * Size + X], Direction.X, Direction.Y);
		}
	}
}

FTidewrightShorePoint FTidewrightShoreField::Sample(const FVector2D& WorldCm) const
{
	FTidewrightShorePoint Point;
	if (Resolution <= 0)
	{
		return Point;
	}

	const double U = (WorldCm.X - CentreCm.X) / (2.0 * HalfSizeCm.X) + 0.5;
	const double V = (WorldCm.Y - CentreCm.Y) / (2.0 * HalfSizeCm.Y) + 0.5;
	const float EdgeU = FMath::Clamp(static_cast<float>(0.5 - FMath::Abs(U - 0.5)) * 12.0f, 0.0f, 1.0f);
	const float EdgeV = FMath::Clamp(static_cast<float>(0.5 - FMath::Abs(V - 0.5)) * 12.0f, 0.0f, 1.0f);
	Point.Weight = EdgeU * EdgeV;
	if (Point.Weight <= 0.0f)
	{
		return Point;
	}

	// Same footprint as a clamped bilinear texture fetch.
	const double Fx = FMath::Clamp(U * Resolution - 0.5, 0.0, Resolution - 1.0);
	const double Fy = FMath::Clamp(V * Resolution - 0.5, 0.0, Resolution - 1.0);
	const int32 X0 = FMath::Min(FMath::FloorToInt32(Fx), Resolution - 1);
	const int32 Y0 = FMath::Min(FMath::FloorToInt32(Fy), Resolution - 1);
	const int32 X1 = FMath::Min(X0 + 1, Resolution - 1);
	const int32 Y1 = FMath::Min(Y0 + 1, Resolution - 1);
	const float Tx = static_cast<float>(Fx - X0);
	const float Ty = static_cast<float>(Fy - Y0);
	const FVector4f Value = FMath::Lerp(
		FMath::Lerp(Texels[Y0 * Resolution + X0], Texels[Y0 * Resolution + X1], Tx),
		FMath::Lerp(Texels[Y1 * Resolution + X0], Texels[Y1 * Resolution + X1], Tx), Ty);

	Point.Depth = FMath::Lerp(TidewrightShore::DeepWater, Value.X, Point.Weight);
	Point.Distance = Value.Y;
	Point.ToShore = FVector2f(Value.Z, Value.W);
	return Point;
}

float FTidewrightShoreField::WrapTime(double TimeSeconds, float SurfPeriod)
{
	const double Wrap = FMath::Max<double>(SurfPeriod, 0.1) * 4096.0;
	return static_cast<float>(FMath::Fmod(TimeSeconds, Wrap));
}

FTidewrightShoreMotion FTidewrightShoreField::Evaluate(const FTidewrightShorePoint& Point, const FTidewrightShoreSettings& Settings, const FVector2D& WorldCm, double TimeSeconds)
{
	using namespace TidewrightShore;

	// Keep this in step with SHORE_CODE in Tools/build_assets.py: the material runs the same arithmetic.
	FTidewrightShoreMotion Motion;
	const float On = Point.Weight;
	if (On <= 0.0f)
	{
		return Motion;
	}

	const float Depth = Point.Depth;
	const float DepthPos = FMath::Max(Depth, 0.0f);
	const float Damp = FMath::Max(Settings.WaveDampingDepth, 0.05f);
	static const float BandDepth[4] = { 1.0f, 0.6f, 0.3f, 0.15f };
	for (int32 Band = 0; Band < 4; ++Band)
	{
		Motion.BandScale[Band] = Smooth(FMath::Clamp(Depth / (Damp * BandDepth[Band]), 0.0f, 1.0f));
	}

	const float Period = FMath::Max(Settings.SurfPeriod, 0.1f);
	const float Wavelength = FMath::Max(Settings.SurfWavelength, 0.1f);
	const float Break = FMath::Max(Settings.BreakDepth, 0.05f);
	const float Time = WrapTime(TimeSeconds, Period);
	const float Px = static_cast<float>(WorldCm.X * 0.01);
	const float Py = static_cast<float>(WorldCm.Y * 0.01);

	// A slow wobble along the coast so that the surf does not arrive everywhere at once.
	const float Wobble = 0.5f * FMath::Sin(Px * 0.071f + Py * 0.043f) + 0.35f * FMath::Sin(Py * 0.113f - Px * 0.029f + 1.7f);
	const float Theta = UE_TWO_PI * (Point.Distance / Wavelength + Time / Period) + Wobble;
	const float Crest = 0.5f + 0.5f * FMath::Cos(Theta);
	const float CrestRoot = FMath::Sqrt(Crest);
	const float Profile = Crest * Crest * CrestRoot;

	// Surf grows as the water shallows, peaks where it breaks, then runs on as a low bore.
	float Envelope = DepthPos >= Break ? 1.0f - SmoothStep(Break, Break * 4.0f, DepthPos) : FMath::Lerp(0.2f, 1.0f, DepthPos / Break);
	Envelope *= On * FMath::Clamp(1.0f + Depth * 2.0f, 0.0f, 1.0f);
	const float Amplitude = Settings.SurfHeight * Envelope;
	const float SurfHeight = Amplitude * Profile;

	// Run-up: the water level at the beach jumps as each crest lands and drains away before the next.
	const float Arrival = Frac((UE_TWO_PI * Time / Period + Wobble) / UE_TWO_PI);
	const float Swash = Arrival < 0.3f ? SmoothStep(0.0f, 0.3f, Arrival) : 1.0f - SmoothStep(0.3f, 1.0f, Arrival);
	const float Level = Settings.RunUpHeight * Swash * FMath::Clamp(1.0f - DepthPos / Break, 0.0f, 1.0f) * On;

	Motion.Height = SurfHeight + Level;
	Motion.Horizontal = Point.ToShore * (Amplitude * 0.7f * FMath::Sin(Theta) * Crest);
	Motion.Slope = Point.ToShore * (Amplitude * 1.25f * Crest * CrestRoot * FMath::Sin(Theta) * UE_TWO_PI / Wavelength);

	const float EffectiveDepth = Depth + Level + SurfHeight;
	const float EdgeFoam = Settings.ShoreFoamDepth > 0.0f ? (1.0f - SmoothStep(0.0f, Settings.ShoreFoamDepth, EffectiveDepth)) * (1.0f - SmoothStep(2.0f, 6.0f, Point.Distance)) : 0.0f;
	const float Broken = 1.0f - SmoothStep(Break * 0.8f, Break * 1.25f, DepthPos);
	const float Behind = Frac(Theta / UE_TWO_PI);
	const float Trail = FMath::Clamp(FMath::Exp(-Behind * 5.0f) + 0.8f * SmoothStep(0.9f, 1.0f, Behind), 0.0f, 1.0f);
	const float SurfFoam = Broken * Trail * FMath::Clamp(Settings.SurfHeight * 4.0f, 0.0f, 1.0f);
	Motion.Foam = FMath::Clamp(FMath::Max(EdgeFoam * 0.7f, SurfFoam) * Settings.ShoreFoamAmount * On, 0.0f, 1.0f);
	return Motion;
}
