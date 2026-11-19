// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "TidewrightSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TidewrightTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	static FTidewrightWaveSettings RoughSea()
	{
		FTidewrightWaveSettings Settings;
		Settings.WindSpeed = 14.0f;
		Settings.Fetch = 300.0f;
		Settings.SwellHeight = 1.0f;
		Settings.SwellWavelength = 120.0f;
		Settings.PatternSize = 900.0f;
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTidewrightFFTTest, "Tidewright.Simulation.InverseFFT", TidewrightTests::Flags)
bool FTidewrightFFTTest::RunTest(const FString& Parameters)
{
	// A single non-zero coefficient at wave number (3, 5) must transform to a plane wave.
	const int32 Size = 32;
	TArray<FVector2f> Data, Scratch;
	Data.SetNumZeroed(Size * Size);
	Scratch.SetNumZeroed(Size);
	Data[5 * Size + 3] = FVector2f(1.0f, 0.0f);
	FTidewrightSimulation::InverseFFT2D(Data.GetData(), Scratch.GetData(), Size);

	float MaxError = 0.0f;
	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const float Phase = UE_TWO_PI * (3.0f * X + 5.0f * Y) / Size;
			MaxError = FMath::Max(MaxError, FMath::Abs(Data[Y * Size + X].X - FMath::Cos(Phase)));
			MaxError = FMath::Max(MaxError, FMath::Abs(Data[Y * Size + X].Y - FMath::Sin(Phase)));
		}
	}
	TestTrue(TEXT("Inverse FFT of one coefficient is a plane wave"), MaxError < 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTidewrightEnergyTest, "Tidewright.Simulation.WaveHeightMatchesSpectrum", TidewrightTests::Flags)
bool FTidewrightEnergyTest::RunTest(const FString& Parameters)
{
	FTidewrightSimulation Simulation;
	Simulation.Configure(TidewrightTests::RoughSea(), 128, 3);
	Simulation.Update(12.5, 0.016f);

	// Measure the variance of the simulated height field, band by band.
	double Measured = 0.0;
	for (int32 Index = 0; Index < FTidewrightSimulation::NumCascades; ++Index)
	{
		const TArray<FVector4f>& Texels = Simulation.GetCascade(Index).DisplacementMips[0];
		double Sum = 0.0, SumSquares = 0.0;
		for (const FVector4f& Texel : Texels)
		{
			Sum += Texel.Z;
			SumSquares += static_cast<double>(Texel.Z) * Texel.Z;
		}
		const double Mean = Sum / Texels.Num();
		Measured += SumSquares / Texels.Num() - Mean * Mean;
	}

	const double Predicted = FMath::Square(Simulation.GetSignificantWaveHeight() / 4.0);
	AddInfo(FString::Printf(TEXT("Significant wave height %.2f m, measured variance %.4f, predicted %.4f"), Simulation.GetSignificantWaveHeight(), Measured, Predicted));
	TestTrue(TEXT("Sea state is in a plausible range for 14 m/s wind"), Simulation.GetSignificantWaveHeight() > 1.0f && Simulation.GetSignificantWaveHeight() < 8.0f);
	TestTrue(TEXT("Simulated height variance matches the spectrum within 40%"), Measured > Predicted * 0.6 && Measured < Predicted * 1.4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTidewrightDeterminismTest, "Tidewright.Simulation.Deterministic", TidewrightTests::Flags)
bool FTidewrightDeterminismTest::RunTest(const FString& Parameters)
{
	FTidewrightSimulation A, B, C;
	A.Configure(TidewrightTests::RoughSea(), 64, 7);
	B.Configure(TidewrightTests::RoughSea(), 64, 7);
	C.Configure(TidewrightTests::RoughSea(), 64, 8);
	A.Update(40.0, 0.016f);
	// B reaches the same time by a different route; the surface must not depend on history.
	B.Update(3.0, 0.016f);
	B.Update(40.0, 0.016f);
	C.Update(40.0, 0.016f);

	const FVector2D Position(13.7, -251.3);
	const FVector3f Da = A.SampleDisplacement(Position);
	const FVector3f Db = B.SampleDisplacement(Position);
	const FVector3f Dc = C.SampleDisplacement(Position);
	TestTrue(TEXT("Same seed and time give the same surface"), Da.Equals(Db, 1e-4f));
	TestFalse(TEXT("A different seed gives a different surface"), Da.Equals(Dc, 1e-3f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTidewrightSurfaceQueryTest, "Tidewright.Simulation.SurfaceQuery", TidewrightTests::Flags)
bool FTidewrightSurfaceQueryTest::RunTest(const FString& Parameters)
{
	FTidewrightSimulation Simulation;
	Simulation.Configure(TidewrightTests::RoughSea(), 128, 11);

	float Height = 0.0f;
	FVector3f Normal;
	TestFalse(TEXT("No surface before the first update"), Simulation.SampleSurface(FVector2D::ZeroVector, Height, Normal));

	Simulation.Update(21.0, 0.016f);

	float WorstResidual = 0.0f;
	float MaxHeight = 0.0f;
	for (int32 Index = 0; Index < 200; ++Index)
	{
		const FVector2D Position(Index * 7.31 - 400.0, Index * -3.77 + 150.0);
		FVector2f Offset;
		TestTrue(TEXT("Surface query succeeds"), Simulation.SampleSurface(Position, Height, Normal, &Offset));

		// The rest position found by the query, pushed through the displacement, must land back on the query position.
		const FVector2D Rest = Position + FVector2D(Offset.X, Offset.Y);
		const FVector3f Displacement = Simulation.SampleDisplacement(Rest);
		const FVector2D Landed = Rest + FVector2D(Displacement.X, Displacement.Y);
		WorstResidual = FMath::Max(WorstResidual, static_cast<float>((Landed - Position).Size()));
		MaxHeight = FMath::Max(MaxHeight, FMath::Abs(Height));
		TestTrue(TEXT("Normal is unit length and points up"), FMath::IsNearlyEqual(Normal.Size(), 1.0f, 1e-3f) && Normal.Z > 0.2f);
	}
	AddInfo(FString::Printf(TEXT("Worst horizontal residual %.4f m, largest height %.2f m"), WorstResidual, MaxHeight));
	TestTrue(TEXT("Query lands within 5 cm of the requested position"), WorstResidual < 0.05f);
	TestTrue(TEXT("Waves have height"), MaxHeight > 0.3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTidewrightBlendTest, "Tidewright.Simulation.PresetBlendIsContinuous", TidewrightTests::Flags)
bool FTidewrightBlendTest::RunTest(const FString& Parameters)
{
	FTidewrightSimulation Simulation;
	FTidewrightWaveSettings Calm;
	Calm.WindSpeed = 3.0f;
	Calm.SwellHeight = 0.0f;
	Calm.PatternSize = TidewrightTests::RoughSea().PatternSize;
	Simulation.Configure(Calm, 64, 5);
	Simulation.Update(10.0, 0.016f);

	const FVector2D Position(40.0, 25.0);
	const float Before = Simulation.SampleDisplacement(Position).Z;
	const float CalmHeight = Simulation.GetSignificantWaveHeight();

	Simulation.SetSettings(TidewrightTests::RoughSea(), 10.0f);
	Simulation.Update(10.0, 0.0f);
	const float JustAfter = Simulation.SampleDisplacement(Position).Z;
	TestTrue(TEXT("Starting a blend does not move the surface"), FMath::IsNearlyEqual(Before, JustAfter, 1e-3f));

	// Run the blend to completion at a fixed wave time, then compare with a simulation that started rough.
	for (int32 Step = 0; Step < 120; ++Step)
	{
		Simulation.Update(10.0, 0.1f);
	}
	FTidewrightSimulation Reference;
	Reference.Configure(TidewrightTests::RoughSea(), 64, 5);
	Reference.Update(10.0, 0.016f);
	TestTrue(TEXT("A finished blend equals the target sea state"), FMath::IsNearlyEqual(Simulation.SampleDisplacement(Position).Z, Reference.SampleDisplacement(Position).Z, 1e-3f));
	TestTrue(TEXT("The rough sea is rougher than the calm one"), Simulation.GetSignificantWaveHeight() > CalmHeight * 2.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTidewrightFoamTest, "Tidewright.Simulation.FoamFollowsRoughness", TidewrightTests::Flags)
bool FTidewrightFoamTest::RunTest(const FString& Parameters)
{
	auto MeanFoam = [](const FTidewrightWaveSettings& Settings)
	{
		FTidewrightSimulation Simulation;
		Simulation.Configure(Settings, 64, 2);
		for (int32 Step = 0; Step < 30; ++Step)
		{
			Simulation.Update(Step * 0.1, 0.1f);
		}
		double Sum = 0.0;
		int32 Count = 0;
		for (int32 Index = 0; Index < FTidewrightSimulation::NumCascades; ++Index)
		{
			for (const FVector4f& Texel : Simulation.GetCascade(Index).DisplacementMips[0])
			{
				Sum += Texel.W;
				++Count;
			}
		}
		return Sum / Count;
	};

	FTidewrightWaveSettings Calm;
	Calm.WindSpeed = 2.0f;
	Calm.SwellHeight = 0.0f;
	Calm.Choppiness = 0.3f;
	Calm.FoamCoverage = 0.9f;
	FTidewrightWaveSettings Storm = TidewrightTests::RoughSea();
	Storm.WindSpeed = 24.0f;
	Storm.Choppiness = 1.2f;
	Storm.FoamCoverage = 0.9f;
	FTidewrightWaveSettings NoFoam = Storm;
	NoFoam.FoamCoverage = 0.0f;

	const double CalmFoam = MeanFoam(Calm);
	const double StormFoam = MeanFoam(Storm);
	AddInfo(FString::Printf(TEXT("Mean foam: calm %.4f, storm %.4f"), CalmFoam, StormFoam));
	TestTrue(TEXT("A storm makes more foam than a calm"), StormFoam > CalmFoam + 0.001);
	TestTrue(TEXT("Zero coverage makes no foam"), MeanFoam(NoFoam) == 0.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
