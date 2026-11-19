// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "TidewrightSimulation.h"

#include "Async/ParallelFor.h"
#include "HAL/CriticalSection.h"
#include "Math/RandomStream.h"
#include "Misc/ScopeLock.h"

#include <cmath>

namespace TidewrightSim
{
	static constexpr double Gravity = 9.81;
	static constexpr double TwoPi = 6.283185307179586;

	/** First harmonic of a band that the band is trusted with; longer waves belong to the next larger band. */
	static constexpr float BandStartHarmonic = 5.0f;

	struct FFFTTables
	{
		int32 Size = 0;
		TArray<int32> Reverse;
		TArray<float> Cos;
		TArray<float> Sin;
	};

	static const FFFTTables& GetTables(int32 Size)
	{
		static FCriticalSection Lock;
		static TMap<int32, TUniquePtr<FFFTTables>> Cache;

		FScopeLock ScopeLock(&Lock);
		if (TUniquePtr<FFFTTables>* Found = Cache.Find(Size))
		{
			return **Found;
		}

		TUniquePtr<FFFTTables> Tables = MakeUnique<FFFTTables>();
		Tables->Size = Size;
		const int32 Bits = FMath::FloorLog2(Size);
		Tables->Reverse.SetNumUninitialized(Size);
		for (int32 Index = 0; Index < Size; ++Index)
		{
			int32 Reversed = 0;
			for (int32 Bit = 0; Bit < Bits; ++Bit)
			{
				if (Index & (1 << Bit))
				{
					Reversed |= 1 << (Bits - 1 - Bit);
				}
			}
			Tables->Reverse[Index] = Reversed;
		}
		Tables->Cos.SetNumUninitialized(Size / 2);
		Tables->Sin.SetNumUninitialized(Size / 2);
		for (int32 Index = 0; Index < Size / 2; ++Index)
		{
			const double Angle = TwoPi * Index / Size;
			Tables->Cos[Index] = static_cast<float>(std::cos(Angle));
			Tables->Sin[Index] = static_cast<float>(std::sin(Angle));
		}
		return *Cache.Add(Size, MoveTemp(Tables));
	}

	/** Unnormalised inverse transform (positive exponent) of one contiguous row. */
	static void InverseFFT1D(FVector2f* Data, const FFFTTables& Tables)
	{
		const int32 Size = Tables.Size;
		for (int32 Index = 0; Index < Size; ++Index)
		{
			const int32 Other = Tables.Reverse[Index];
			if (Index < Other)
			{
				Swap(Data[Index], Data[Other]);
			}
		}
		for (int32 Length = 2; Length <= Size; Length <<= 1)
		{
			const int32 Half = Length >> 1;
			const int32 Step = Size / Length;
			for (int32 Start = 0; Start < Size; Start += Length)
			{
				for (int32 Offset = 0; Offset < Half; ++Offset)
				{
					const float Wr = Tables.Cos[Offset * Step];
					const float Wi = Tables.Sin[Offset * Step];
					FVector2f& A = Data[Start + Offset];
					FVector2f& B = Data[Start + Offset + Half];
					const float Tr = B.X * Wr - B.Y * Wi;
					const float Ti = B.X * Wi + B.Y * Wr;
					B.X = A.X - Tr;
					B.Y = A.Y - Ti;
					A.X += Tr;
					A.Y += Ti;
				}
			}
		}
	}

	static FORCEINLINE int32 WrapIndex(int64 Value, int32 Size)
	{
		return static_cast<int32>(Value & static_cast<int64>(Size - 1));
	}

	static FVector4f Bilinear(const TArray<FVector4f>& Texels, int32 Size, double U, double V)
	{
		const double Fx = FMath::FloorToDouble(U);
		const double Fy = FMath::FloorToDouble(V);
		const float Tx = static_cast<float>(U - Fx);
		const float Ty = static_cast<float>(V - Fy);
		const int32 X0 = WrapIndex(static_cast<int64>(Fx), Size);
		const int32 Y0 = WrapIndex(static_cast<int64>(Fy), Size);
		const int32 X1 = (X0 + 1) & (Size - 1);
		const int32 Y1 = (Y0 + 1) & (Size - 1);
		const FVector4f& A = Texels[Y0 * Size + X0];
		const FVector4f& B = Texels[Y0 * Size + X1];
		const FVector4f& C = Texels[Y1 * Size + X0];
		const FVector4f& D = Texels[Y1 * Size + X1];
		return FMath::Lerp(FMath::Lerp(A, B, Tx), FMath::Lerp(C, D, Tx), Ty);
	}

	/** Normalisation of the cos^2s(theta/2) directional spread so it integrates to one over the circle. */
	static double SpreadNormalisation(double S)
	{
		return std::exp(std::lgamma(S + 1.0) - std::lgamma(S + 0.5)) / (2.0 * std::sqrt(UE_DOUBLE_PI));
	}

	static double Spread(double DeltaAngle, double S)
	{
		const double C = FMath::Abs(std::cos(DeltaAngle * 0.5));
		return SpreadNormalisation(S) * std::pow(C, 2.0 * S);
	}
}

void FTidewrightSimulation::InverseFFT2D(FVector2f* Data, FVector2f* Scratch, int32 Size)
{
	const TidewrightSim::FFFTTables& Tables = TidewrightSim::GetTables(Size);
	for (int32 Row = 0; Row < Size; ++Row)
	{
		TidewrightSim::InverseFFT1D(Data + Row * Size, Tables);
	}
	for (int32 Column = 0; Column < Size; ++Column)
	{
		for (int32 Row = 0; Row < Size; ++Row)
		{
			Scratch[Row] = Data[Row * Size + Column];
		}
		TidewrightSim::InverseFFT1D(Scratch, Tables);
		for (int32 Row = 0; Row < Size; ++Row)
		{
			Data[Row * Size + Column] = Scratch[Row];
		}
	}
}

double FTidewrightSimulation::SpectrumDensity(const FTidewrightWaveSettings& InSettings, double Kx, double Ky)
{
	using namespace TidewrightSim;

	const double K = std::sqrt(Kx * Kx + Ky * Ky);
	if (K < 1e-9)
	{
		return 0.0;
	}

	const double Depth = FMath::Max<double>(InSettings.Depth, 0.2);
	const double Kd = FMath::Min(K * Depth, 40.0);
	const double Th = std::tanh(Kd);
	const double Omega = std::sqrt(Gravity * K * Th);
	const double GroupFactor = Gravity * (Th + Kd * (1.0 - Th * Th)) / (2.0 * Omega);
	const double Theta = std::atan2(Ky, Kx);

	double Energy = 0.0;

	// Wind sea: JONSWAP frequency spectrum with a frequency dependent directional spread.
	const double U = FMath::Max<double>(InSettings.WindSpeed, 0.1);
	if (InSettings.HeightScale > 0.0f)
	{
		const double FetchM = FMath::Max<double>(InSettings.Fetch, 0.05) * 1000.0;
		const double PeakOmega = FMath::Max(22.0 * std::cbrt(Gravity * Gravity / (U * FetchM)), 0.87 * Gravity / U);
		// The spectrum is scaled so its total energy matches measured seas: fetch-limited growth
		// (JONSWAP field data), capped at the fully developed height for this wind (Pierson-Moskowitz).
		const double Gamma = FMath::Max<double>(InSettings.PeakSharpness, 1.0);
		const double FetchVariance = 1.6e-7 * (Gravity * FetchM / (U * U)) * FMath::Square(U * U / Gravity);
		const double MatureVariance = FMath::Square(0.21 * U * U / Gravity / 4.0);
		const double TargetVariance = FMath::Min(FetchVariance, MatureVariance);
		const double Alpha = 5.0 * FMath::Square(PeakOmega * PeakOmega) * TargetVariance * FMath::Max(1.0 - 0.287 * std::log(Gamma), 0.2) / (Gravity * Gravity);
		const double Sigma = Omega <= PeakOmega ? 0.07 : 0.09;
		const double R = std::exp(-FMath::Square(Omega - PeakOmega) / (2.0 * Sigma * Sigma * PeakOmega * PeakOmega));
		const double Frequency = Alpha * Gravity * Gravity / std::pow(Omega, 5.0)
			* std::exp(-1.25 * std::pow(PeakOmega / Omega, 4.0))
			* std::pow(static_cast<double>(InSettings.PeakSharpness), R);

		const double PeakSpread = FMath::Lerp(1.5, 26.0, FMath::Square(static_cast<double>(InSettings.Alignment)));
		const double Ratio = Omega / PeakOmega;
		const double S = FMath::Clamp(PeakSpread * (Ratio <= 1.0 ? std::pow(Ratio, 5.0) : std::pow(Ratio, -2.5)), 0.75, 80.0);
		const double Direction = FMath::DegreesToRadians(static_cast<double>(InSettings.WindDirection));

		Energy += Frequency * Spread(Theta - Direction, S) * FMath::Square(static_cast<double>(InSettings.HeightScale));
	}

	// Swell: a narrow band of long waves from one direction.
	if (InSettings.SwellHeight > 0.0f)
	{
		const double Ks = TwoPi / FMath::Max<double>(InSettings.SwellWavelength, 1.0);
		const double SwellOmega = std::sqrt(Gravity * Ks * std::tanh(FMath::Min(Ks * Depth, 40.0)));
		const double Width = 0.07 * SwellOmega;
		const double Variance = FMath::Square(static_cast<double>(InSettings.SwellHeight)) / 16.0;
		const double Frequency = Variance / (Width * std::sqrt(TwoPi)) * std::exp(-FMath::Square(Omega - SwellOmega) / (2.0 * Width * Width));
		const double Direction = FMath::DegreesToRadians(static_cast<double>(InSettings.SwellDirection));
		Energy += Frequency * Spread(Theta - Direction, 32.0);
	}

	const double Cutoff = InSettings.SmallWaveCutoff / TwoPi;
	const double Suppression = std::exp(-FMath::Square(K * Cutoff));

	return Energy * GroupFactor / K * Suppression;
}

void FTidewrightSimulation::BuildAmplitudes(const FTidewrightWaveSettings& InSettings, int32 CascadeIndex, TArray<float>& OutAmplitude) const
{
	const FTidewrightCascade& Cascade = Cascades[CascadeIndex];
	const int32 Size = Resolution;
	const int32 Half = Size / 2;
	const double DeltaK = TidewrightSim::TwoPi / Cascade.LengthM;

	OutAmplitude.SetNumUninitialized(Size * Size);
	ParallelFor(Size, [&](int32 Y)
	{
		const double Ky = (Y < Half ? Y : Y - Size) * DeltaK;
		for (int32 X = 0; X < Size; ++X)
		{
			const double Kx = (X < Half ? X : X - Size) * DeltaK;
			const double K = std::sqrt(Kx * Kx + Ky * Ky);
			float Value = 0.0f;
			if (X != Half && Y != Half && K >= Cascade.KMin && K < Cascade.KMax && K > 1e-9)
			{
				Value = static_cast<float>(0.5 * std::sqrt(SpectrumDensity(InSettings, Kx, Ky)) * DeltaK);
			}
			OutAmplitude[Y * Size + X] = Value;
		}
	});
}

void FTidewrightSimulation::UpdateStatistics(FTidewrightCascade& Cascade) const
{
	const int32 Size = Resolution;
	const int32 Half = Size / 2;
	const double DeltaK = TidewrightSim::TwoPi / Cascade.LengthM;
	double Height = 0.0;
	double Slope = 0.0;
	for (int32 Y = 0; Y < Size; ++Y)
	{
		const double Ky = (Y < Half ? Y : Y - Size) * DeltaK;
		for (int32 X = 0; X < Size; ++X)
		{
			const double Kx = (X < Half ? X : X - Size) * DeltaK;
			const double A = Cascade.AmplitudeTo[Y * Size + X];
			const double Power = 4.0 * A * A;
			Height += Power;
			Slope += Power * (Kx * Kx + Ky * Ky);
		}
	}
	Cascade.HeightVariance = static_cast<float>(Height);
	Cascade.SlopeVariance = static_cast<float>(Slope);
}

void FTidewrightSimulation::Configure(const FTidewrightWaveSettings& InSettings, int32 InResolution, int32 InSeed)
{
	Resolution = FMath::RoundUpToPowerOfTwo(FMath::Clamp(InResolution, 16, 1024));
	Seed = InSeed;
	bHasOutput = false;
	bBlendActive = false;

	TidewrightSim::GetTables(Resolution);

	const int32 Count = Resolution * Resolution;
	for (int32 Index = 0; Index < NumCascades; ++Index)
	{
		FTidewrightCascade& Cascade = Cascades[Index];
		Cascade.Resolution = Resolution;

		FRandomStream Random(Seed * 7919 + Index * 104729 + 17);
		Cascade.Noise.SetNumUninitialized(Count);
		for (int32 Texel = 0; Texel < Count; ++Texel)
		{
			// Box-Muller: two independent standard normal values.
			const float U1 = FMath::Max(Random.GetFraction(), 1e-7f);
			const float U2 = Random.GetFraction();
			const float Radius = FMath::Sqrt(-2.0f * FMath::Loge(U1));
			Cascade.Noise[Texel] = FVector2f(Radius * FMath::Cos(UE_TWO_PI * U2), Radius * FMath::Sin(UE_TWO_PI * U2));
		}

		Cascade.Omega.SetNumZeroed(Count);
		Cascade.Amplitude.SetNumZeroed(Count);
		Cascade.AmplitudeFrom.SetNumZeroed(Count);
		Cascade.AmplitudeTo.SetNumZeroed(Count);
		Cascade.Foam.SetNumZeroed(Count);
		Cascade.Scratch.SetNumZeroed(Resolution);
		for (TArray<FVector2f>& Buffer : Cascade.Work)
		{
			Buffer.SetNumZeroed(Count);
		}

		const int32 MipCount = FMath::FloorLog2(Resolution) + 1;
		Cascade.DisplacementMips.SetNum(MipCount);
		Cascade.DerivativeMips.SetNum(MipCount);
		for (int32 Mip = 0; Mip < MipCount; ++Mip)
		{
			const int32 MipSize = Resolution >> Mip;
			Cascade.DisplacementMips[Mip].SetNumZeroed(MipSize * MipSize);
			Cascade.DerivativeMips[Mip].SetNumZeroed(MipSize * MipSize);
		}
	}

	SetSettings(InSettings, 0.0f);
}

void FTidewrightSimulation::SetSettings(const FTidewrightWaveSettings& InSettings, float BlendSeconds)
{
	if (Resolution <= 0)
	{
		return;
	}

	Settings = InSettings;

	const int32 Size = Resolution;
	const int32 Half = Size / 2;
	const double Depth = FMath::Max<double>(Settings.Depth, 0.2);

	for (int32 Index = 0; Index < NumCascades; ++Index)
	{
		FTidewrightCascade& Cascade = Cascades[Index];
		Cascade.LengthM = FMath::Max(Settings.PatternSize, 1.0f) / FMath::Pow(CascadeRatio, static_cast<float>(Index));
	}
	for (int32 Index = 0; Index < NumCascades; ++Index)
	{
		FTidewrightCascade& Cascade = Cascades[Index];
		Cascade.KMin = Index == 0 ? 0.0f : Cascades[Index - 1].KMax;
		Cascade.KMax = Index == NumCascades - 1
			? 0.9f * UE_PI * Size / Cascade.LengthM
			: UE_TWO_PI * TidewrightSim::BandStartHarmonic / Cascades[Index + 1].LengthM;

		const double DeltaK = TidewrightSim::TwoPi / Cascade.LengthM;
		for (int32 Y = 0; Y < Size; ++Y)
		{
			const double Ky = (Y < Half ? Y : Y - Size) * DeltaK;
			for (int32 X = 0; X < Size; ++X)
			{
				const double Kx = (X < Half ? X : X - Size) * DeltaK;
				const double K = std::sqrt(Kx * Kx + Ky * Ky);
				Cascade.Omega[Y * Size + X] = static_cast<float>(std::sqrt(TidewrightSim::Gravity * K * std::tanh(FMath::Min(K * Depth, 40.0))));
			}
		}

		Cascade.AmplitudeFrom = Cascade.Amplitude;
		BuildAmplitudes(Settings, Index, Cascade.AmplitudeTo);
		UpdateStatistics(Cascade);

		if (BlendSeconds <= 0.0f || !bHasOutput)
		{
			Cascade.Amplitude = Cascade.AmplitudeTo;
		}
	}

	bBlendActive = BlendSeconds > 0.0f && bHasOutput;
	BlendDuration = BlendSeconds;
	BlendElapsed = 0.0f;
}

float FTidewrightSimulation::GetSignificantWaveHeight() const
{
	float Variance = 0.0f;
	for (const FTidewrightCascade& Cascade : Cascades)
	{
		Variance += Cascade.HeightVariance;
	}
	return 4.0f * FMath::Sqrt(Variance);
}

void FTidewrightSimulation::BuildMips(TArray<TArray<FVector4f>>& Mips, int32 Size)
{
	for (int32 Mip = 1; Mip < Mips.Num(); ++Mip)
	{
		const int32 SourceSize = Size >> (Mip - 1);
		const int32 TargetSize = Size >> Mip;
		const TArray<FVector4f>& Source = Mips[Mip - 1];
		TArray<FVector4f>& Target = Mips[Mip];
		for (int32 Y = 0; Y < TargetSize; ++Y)
		{
			const FVector4f* Row0 = &Source[(Y * 2) * SourceSize];
			const FVector4f* Row1 = &Source[(Y * 2 + 1) * SourceSize];
			FVector4f* Out = &Target[Y * TargetSize];
			for (int32 X = 0; X < TargetSize; ++X)
			{
				Out[X] = (Row0[X * 2] + Row0[X * 2 + 1] + Row1[X * 2] + Row1[X * 2 + 1]) * 0.25f;
			}
		}
	}
}

void FTidewrightSimulation::UpdateCascade(FTidewrightCascade& Cascade, double TimeSeconds, float DeltaSeconds, float BlendAlpha, bool bBlending)
{
	const int32 Size = Resolution;
	const int32 Half = Size / 2;
	const int32 Mask = Size - 1;
	const float DeltaK = UE_TWO_PI / Cascade.LengthM;

	if (bBlending)
	{
		const int32 Count = Size * Size;
		for (int32 Texel = 0; Texel < Count; ++Texel)
		{
			Cascade.Amplitude[Texel] = FMath::Lerp(Cascade.AmplitudeFrom[Texel], Cascade.AmplitudeTo[Texel], BlendAlpha);
		}
	}

	FVector2f* B0 = Cascade.Work[0].GetData();
	FVector2f* B1 = Cascade.Work[1].GetData();
	FVector2f* B2 = Cascade.Work[2].GetData();
	FVector2f* B3 = Cascade.Work[3].GetData();

	for (int32 Y = 0; Y < Size; ++Y)
	{
		const float Ky = (Y < Half ? Y : Y - Size) * DeltaK;
		const int32 MirrorY = (Size - Y) & Mask;
		for (int32 X = 0; X < Size; ++X)
		{
			const int32 Texel = Y * Size + X;
			const float A = Cascade.Amplitude[Texel];
			const int32 Mirror = MirrorY * Size + ((Size - X) & Mask);
			const float Am = Cascade.Amplitude[Mirror];
			if (A == 0.0f && Am == 0.0f)
			{
				B0[Texel] = B1[Texel] = B2[Texel] = B3[Texel] = FVector2f::ZeroVector;
				continue;
			}

			const float Kx = (X < Half ? X : X - Size) * DeltaK;
			const float K = FMath::Sqrt(Kx * Kx + Ky * Ky);
			const float InvK = K > 1e-9f ? 1.0f / K : 0.0f;

			const double Phase = FMath::Fmod(static_cast<double>(Cascade.Omega[Texel]) * TimeSeconds, TidewrightSim::TwoPi);
			float S, C;
			FMath::SinCos(&S, &C, static_cast<float>(Phase));

			// h(k,t) = h0(k) e^{-iwt} + conj(h0(-k)) e^{+iwt}
			const FVector2f Hk = Cascade.Noise[Texel] * A;
			const FVector2f Hm = FVector2f(Cascade.Noise[Mirror].X, -Cascade.Noise[Mirror].Y) * Am;
			const float Hr = Hk.X * C + Hk.Y * S + Hm.X * C - Hm.Y * S;
			const float Hi = Hk.Y * C - Hk.X * S + Hm.Y * C + Hm.X * S;

			// Each buffer packs two real fields as real and imaginary parts of one transform.
			// B0: height, displacement X.            factor (1 - kx/k)
			// B1: displacement Y, dHeight/dX.        factor (-kx + i ky/k)
			// B2: dHeight/dY, dDispX/dX.             factor i (ky - kx^2/k)
			// B3: dDispY/dY, dDispX/dY.              factor (-ky^2/k - i kx ky/k)
			const float F0 = 1.0f - Kx * InvK;
			B0[Texel] = FVector2f(Hr * F0, Hi * F0);

			const float F1r = -Kx;
			const float F1i = Ky * InvK;
			B1[Texel] = FVector2f(Hr * F1r - Hi * F1i, Hr * F1i + Hi * F1r);

			const float F2i = Ky - Kx * Kx * InvK;
			B2[Texel] = FVector2f(-Hi * F2i, Hr * F2i);

			const float F3r = -Ky * Ky * InvK;
			const float F3i = -Kx * Ky * InvK;
			B3[Texel] = FVector2f(Hr * F3r - Hi * F3i, Hr * F3i + Hi * F3r);
		}
	}

	FVector2f* Scratch = Cascade.Scratch.GetData();
	InverseFFT2D(B0, Scratch, Size);
	InverseFFT2D(B1, Scratch, Size);
	InverseFFT2D(B2, Scratch, Size);
	InverseFFT2D(B3, Scratch, Size);

	const float Chop = Settings.Choppiness;
	FVector4f* Displacement = Cascade.DisplacementMips[0].GetData();
	FVector4f* Derivative = Cascade.DerivativeMips[0].GetData();
	const int32 Count = Size * Size;
	for (int32 Texel = 0; Texel < Count; ++Texel)
	{
		Displacement[Texel] = FVector4f(Chop * B0[Texel].Y, Chop * B1[Texel].X, B0[Texel].X, 0.0f);
		Derivative[Texel] = FVector4f(B1[Texel].Y, B2[Texel].X, Chop * B2[Texel].Y, Chop * B3[Texel].X);
	}

	BuildMips(Cascade.DerivativeMips, Size);
}

float FTidewrightSimulation::GetJacobianDeviation() const
{
	// The Jacobian is close to 1 + dDx/dx + dDy/dy, whose variance is choppiness squared times the slope variance.
	// The finest band is left out: its ripples fold constantly but are far too small to make foam.
	float SlopeVariance = 0.0f;
	for (int32 Index = 0; Index < NumCascades - 1; ++Index)
	{
		SlopeVariance += Cascades[Index].SlopeVariance;
	}
	return FMath::Max(Settings.Choppiness * FMath::Sqrt(SlopeVariance), 1e-4f);
}

float FTidewrightSimulation::GetFoamThreshold() const
{
	if (Settings.FoamCoverage <= 0.0f || Resolution <= 0)
	{
		return -100.0f;
	}
	// Foam forms where the surface is squeezed further than usual. "Usual" is partly this sea's own spread of
	// squeezing and partly a fixed reference, so a rougher sea foams more at the same setting, yet a moderate
	// sea still shows whitecaps on its steepest crests.
	const float ReferenceDeviation = 0.15f;
	const float Deviations = FMath::Lerp(3.0f, 0.6f, FMath::Clamp(Settings.FoamCoverage, 0.0f, 1.0f));
	return 1.0f - Deviations * FMath::Sqrt(GetJacobianDeviation() * ReferenceDeviation);
}

float FTidewrightSimulation::GetFoamGain() const
{
	return 7.0f;
}

void FTidewrightSimulation::AccumulateFoam(int32 CascadeIndex, float DeltaSeconds)
{
	FTidewrightCascade& Cascade = Cascades[CascadeIndex];
	const int32 Size = Resolution;
	const float Chop = Settings.Choppiness;
	const bool bFoam = Settings.FoamCoverage > 0.0f && CascadeIndex < NumCascades - 1;
	const float FoamThreshold = GetFoamThreshold();
	const float FoamGain = GetFoamGain();
	const float FoamDecay = FMath::Exp(-FMath::Clamp(DeltaSeconds, 0.0f, 0.25f) / FMath::Max(Settings.FoamLifetime, 0.05f));
	const float TexelM = Cascade.LengthM / Size;

	// Smaller bands steepen the crests of this one. Read them at the mip that matches this band's texel size.
	struct FFiner
	{
		const FVector4f* Texels;
		int32 MipSize;
		float Scale;
	};
	TArray<FFiner, TInlineAllocator<NumCascades>> Finer;
	for (int32 Other = CascadeIndex + 1; Other < NumCascades - 1; ++Other)
	{
		const FTidewrightCascade& Small = Cascades[Other];
		const float Ratio = TexelM / (Small.LengthM / Size);
		const int32 Mip = FMath::Clamp(FMath::RoundToInt(FMath::Log2(Ratio)), 0, Small.DerivativeMips.Num() - 1);
		const int32 MipSize = Size >> Mip;
		Finer.Add({ Small.DerivativeMips[Mip].GetData(), MipSize, MipSize / Small.LengthM });
	}

	const FVector2f* B3 = Cascade.Work[3].GetData();
	const FVector4f* Derivative = Cascade.DerivativeMips[0].GetData();
	FVector4f* Displacement = Cascade.DisplacementMips[0].GetData();
	float* Foam = Cascade.Foam.GetData();

	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const int32 Texel = Y * Size + X;
			float FoamValue = 0.0f;
			if (bFoam)
			{
				float Dxx = Derivative[Texel].Z;
				float Dyy = Derivative[Texel].W;
				const float Dxy = Chop * B3[Texel].Y;
				for (const FFiner& Small : Finer)
				{
					const int32 Sx = FMath::FloorToInt32(X * TexelM * Small.Scale) & (Small.MipSize - 1);
					const int32 Sy = FMath::FloorToInt32(Y * TexelM * Small.Scale) & (Small.MipSize - 1);
					const FVector4f& Value = Small.Texels[Sy * Small.MipSize + Sx];
					Dxx += Value.Z;
					Dyy += Value.W;
				}

				// The Jacobian of the horizontal displacement falls below one where the surface is squeezed
				// together and goes negative where it folds over itself, which is where real waves break.
				const float Jacobian = (1.0f + Dxx) * (1.0f + Dyy) - Dxy * Dxy;
				const float Fresh = FMath::Clamp((FoamThreshold - Jacobian) * FoamGain, 0.0f, 1.0f);
				FoamValue = FMath::Max(Foam[Texel] * FoamDecay, Fresh);
			}
			Foam[Texel] = FoamValue;
			Displacement[Texel].W = FoamValue;
		}
	}

	BuildMips(Cascade.DisplacementMips, Size);
}

void FTidewrightSimulation::Update(double TimeSeconds, float DeltaSeconds)
{
	if (Resolution <= 0)
	{
		return;
	}

	float BlendAlpha = 1.0f;
	const bool bBlending = bBlendActive;
	if (bBlendActive)
	{
		BlendElapsed += FMath::Max(DeltaSeconds, 0.0f);
		BlendAlpha = FMath::SmoothStep(0.0f, 1.0f, BlendDuration > 0.0f ? BlendElapsed / BlendDuration : 1.0f);
		if (BlendElapsed >= BlendDuration)
		{
			BlendAlpha = 1.0f;
			bBlendActive = false;
		}
	}

	ParallelFor(NumCascades, [&](int32 Index)
	{
		UpdateCascade(Cascades[Index], TimeSeconds, DeltaSeconds, BlendAlpha, bBlending);
	});
	// Foam needs every band's derivatives, so it runs as a second pass.
	ParallelFor(NumCascades, [&](int32 Index)
	{
		AccumulateFoam(Index, DeltaSeconds);
	});

	bHasOutput = true;
}

FVector3f FTidewrightSimulation::SampleDisplacement(const FVector2D& PositionM, const float* BandScale) const
{
	FVector3f Sum = FVector3f::ZeroVector;
	if (!bHasOutput)
	{
		return Sum;
	}
	for (int32 Index = 0; Index < NumCascades; ++Index)
	{
		const FTidewrightCascade& Cascade = Cascades[Index];
		const float Weight = BandScale ? BandScale[Index] : 1.0f;
		if (Weight <= 0.0f)
		{
			continue;
		}
		const double Scale = Cascade.Resolution / static_cast<double>(Cascade.LengthM);
		const FVector4f Value = TidewrightSim::Bilinear(Cascade.DisplacementMips[0], Cascade.Resolution, PositionM.X * Scale, PositionM.Y * Scale);
		Sum += FVector3f(Value.X, Value.Y, Value.Z) * Weight;
	}
	return Sum;
}

bool FTidewrightSimulation::SampleSurface(const FVector2D& PositionM, float& OutHeight, FVector3f& OutNormal, FVector2f* OutUndisplacedOffset, const float* BandScale) const
{
	OutHeight = 0.0f;
	OutNormal = FVector3f::UpVector;
	if (!bHasOutput)
	{
		return false;
	}

	// The surface point above PositionM started somewhere else: waves move water sideways as well as up.
	// Fixed point iteration finds the rest position Q with Q + Displacement(Q).xy == PositionM.
	FVector2D Rest = PositionM;
	FVector3f Displacement = FVector3f::ZeroVector;
	for (int32 Iteration = 0; Iteration < 4; ++Iteration)
	{
		Displacement = SampleDisplacement(Rest, BandScale);
		Rest = PositionM - FVector2D(Displacement.X, Displacement.Y);
	}
	Displacement = SampleDisplacement(Rest, BandScale);
	OutHeight = Displacement.Z;

	float SlopeX = 0.0f, SlopeY = 0.0f, Dxx = 0.0f, Dyy = 0.0f;
	for (int32 Index = 0; Index < NumCascades; ++Index)
	{
		const FTidewrightCascade& Cascade = Cascades[Index];
		const float Weight = BandScale ? BandScale[Index] : 1.0f;
		const double Scale = Cascade.Resolution / static_cast<double>(Cascade.LengthM);
		const FVector4f Value = TidewrightSim::Bilinear(Cascade.DerivativeMips[0], Cascade.Resolution, Rest.X * Scale, Rest.Y * Scale);
		SlopeX += Value.X * Weight;
		SlopeY += Value.Y * Weight;
		Dxx += Value.Z * Weight;
		Dyy += Value.W * Weight;
	}
	SlopeX /= FMath::Max(1.0f + Dxx, 0.25f);
	SlopeY /= FMath::Max(1.0f + Dyy, 0.25f);
	OutNormal = FVector3f(-SlopeX, -SlopeY, 1.0f).GetSafeNormal();

	if (OutUndisplacedOffset)
	{
		*OutUndisplacedOffset = FVector2f(static_cast<float>(Rest.X - PositionM.X), static_cast<float>(Rest.Y - PositionM.Y));
	}
	return true;
}
