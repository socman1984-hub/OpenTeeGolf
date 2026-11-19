// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "TidewrightShoreField.h"
#include "TidewrightSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TidewrightShoreTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	/** A straight beach: the ground rises 5 cm per metre along +X and crosses the water level at X = 0. 200 m square. */
	static FTidewrightShoreField StraightBeach(int32 Size = 128)
	{
		TArray<float> Heights;
		Heights.SetNumUninitialized(Size * Size);
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const double WorldX = -10000.0 + (X + 0.5) * 20000.0 / Size;
				Heights[Y * Size + X] = static_cast<float>(WorldX * 0.05);
			}
		}
		FTidewrightShoreField Field;
		Field.Build(Heights, Size, FVector2D::ZeroVector, FVector2D(10000.0, 10000.0), 0.0);
		return Field;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTidewrightShoreFieldTest, "Tidewright.Shore.FieldMatchesTerrain", TidewrightShoreTests::Flags)
bool FTidewrightShoreFieldTest::RunTest(const FString& Parameters)
{
	const FTidewrightShoreField Field = TidewrightShoreTests::StraightBeach();
	TestTrue(TEXT("Field builds"), Field.IsValid());
	TestTrue(TEXT("Half of a straight beach is land"), FMath::Abs(Field.GetLandFraction() - 0.5f) < 0.02f);

	// 40 m out to sea: 2 m deep, 40 m from the waterline, and the shore lies towards +X.
	const FTidewrightShorePoint Sea = Field.Sample(FVector2D(-4000.0, 1500.0));
	AddInfo(FString::Printf(TEXT("Sea point: depth %.3f, distance %.3f, to shore (%.3f, %.3f)"), Sea.Depth, Sea.Distance, Sea.ToShore.X, Sea.ToShore.Y));
	TestTrue(TEXT("Depth matches the terrain"), FMath::Abs(Sea.Depth - 2.0f) < 0.05f);
	TestTrue(TEXT("Distance to the waterline is right to within a cell"), FMath::Abs(Sea.Distance - 40.0f) < 1.6f);
	TestTrue(TEXT("Direction points at the shore"), Sea.ToShore.X > 0.99f && FMath::Abs(Sea.ToShore.Y) < 0.05f);

	const FTidewrightShorePoint Land = Field.Sample(FVector2D(3000.0, -2000.0));
	TestTrue(TEXT("Land has negative depth and distance"), Land.Depth < -1.4f && Land.Distance < -28.0f);

	const FTidewrightShorePoint Outside = Field.Sample(FVector2D(-50000.0, 0.0));
	TestTrue(TEXT("Outside the scanned area is deep open water"), Outside.Weight == 0.0f && Outside.Depth > 500.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTidewrightShoreMotionTest, "Tidewright.Shore.SurfAndDamping", TidewrightShoreTests::Flags)
bool FTidewrightShoreMotionTest::RunTest(const FString& Parameters)
{
	const FTidewrightShoreField Field = TidewrightShoreTests::StraightBeach();
	FTidewrightShoreSettings Settings;
	Settings.WaveDampingDepth = 4.0f;
	Settings.SurfHeight = 1.0f;
	Settings.BreakDepth = 1.5f;

	auto Peak = [&](double WorldX, float& OutFoam)
	{
		float Highest = 0.0f;
		OutFoam = 0.0f;
		const FVector2D Position(WorldX, 0.0);
		const FTidewrightShorePoint Point = Field.Sample(Position);
		for (int32 Step = 0; Step < 140; ++Step)
		{
			const FTidewrightShoreMotion Motion = FTidewrightShoreField::Evaluate(Point, Settings, Position, Step * 0.05);
			Highest = FMath::Max(Highest, Motion.Height);
			OutFoam = FMath::Max(OutFoam, Motion.Foam);
		}
		return Highest;
	};

	// Depths along this beach: 80 m out = 4 m, 30 m out = 1.5 m (the break), 4 m out = 0.2 m.
	float FoamDeep, FoamBreak, FoamShallow, FoamEdge;
	const float Deep = Peak(-9000.0, FoamDeep);
	const float Break = Peak(-3000.0, FoamBreak);
	const float Shallow = Peak(-400.0, FoamShallow);
	Peak(-20.0, FoamEdge);
	AddInfo(FString::Printf(TEXT("Peak surf height: deep %.3f, at the break %.3f, shallows %.3f. Foam %.2f / %.2f / %.2f / edge %.2f"),
		Deep, Break, Shallow, FoamDeep, FoamBreak, FoamShallow, FoamEdge));
	TestTrue(TEXT("Surf is tallest where it breaks"), Break > 0.9f && Break > Shallow && Break > Deep + 0.5f);
	TestTrue(TEXT("Surf is negligible in deep water"), Deep < 0.3f && FoamDeep < 0.01f);
	TestTrue(TEXT("Broken surf carries foam to the beach"), FoamShallow > 0.8f);
	TestTrue(TEXT("The water's edge is foamy"), FoamEdge > 0.6f);

	// Open-water waves: full height where deep, gone at the waterline, small bands surviving longest.
	const FTidewrightShoreMotion AtFour = FTidewrightShoreField::Evaluate(Field.Sample(FVector2D(-8500.0, 0.0)), Settings, FVector2D(-8500.0, 0.0), 0.0);
	const FTidewrightShoreMotion AtOne = FTidewrightShoreField::Evaluate(Field.Sample(FVector2D(-2000.0, 0.0)), Settings, FVector2D(-2000.0, 0.0), 0.0);
	const FTidewrightShoreMotion AtZero = FTidewrightShoreField::Evaluate(Field.Sample(FVector2D(100.0, 0.0)), Settings, FVector2D(100.0, 0.0), 0.0);
	TestTrue(TEXT("Deep water keeps its waves"), AtFour.BandScale[0] > 0.99f);
	TestTrue(TEXT("Shallow water flattens long waves more than short ones"), AtOne.BandScale[0] < 0.2f && AtOne.BandScale[3] > 0.99f);
	TestTrue(TEXT("No open-water waves on land"), AtZero.BandScale[0] == 0.0f && AtZero.BandScale[3] == 0.0f);

	// A flattened band no longer moves the queried surface.
	FTidewrightWaveSettings Waves;
	FTidewrightSimulation Simulation;
	Simulation.Configure(Waves, 64, 3);
	Simulation.Update(4.0, 0.1f);
	const float None[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float Height = 1.0f;
	FVector3f Normal;
	Simulation.SampleSurface(FVector2D(12.0, 7.0), Height, Normal, nullptr, None);
	TestTrue(TEXT("Fully damped water is flat"), Height == 0.0f && Normal.Z > 0.9999f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
