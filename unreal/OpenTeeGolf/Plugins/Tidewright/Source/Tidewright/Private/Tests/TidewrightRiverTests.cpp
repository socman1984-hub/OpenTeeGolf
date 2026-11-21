// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "NiagaraDataInterfaceTidewrightWater.h"
#include "TidewrightRiver.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTidewrightRiverFlowTest, "Tidewright.River.CurrentProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTidewrightRiverFlowTest::RunTest(const FString& Parameters)
{
	const float Rapids = 0.06f;
	const float Mid = ATidewrightRiver::FlowFactor(0.0f, 0.0f, Rapids);
	const float Bank = ATidewrightRiver::FlowFactor(1.0f, 0.0f, Rapids);
	const float OtherBank = ATidewrightRiver::FlowFactor(-1.0f, 0.0f, Rapids);
	const float Steep = ATidewrightRiver::FlowFactor(0.0f, Rapids, Rapids);
	const float VerySteep = ATidewrightRiver::FlowFactor(0.0f, 10.0f, Rapids);
	TestTrue(TEXT("Mid-stream on a level bed flows at the set speed"), FMath::IsNearlyEqual(Mid, 1.0f));
	TestTrue(TEXT("The banks are slower, and equally so"), Bank < 0.5f && Bank > 0.0f && FMath::IsNearlyEqual(Bank, OtherBank));
	TestTrue(TEXT("A steeper bed flows faster"), Steep > Mid * 2.0f);
	TestTrue(TEXT("Speed is bounded on a waterfall"), VerySteep <= 4.0f + KINDA_SMALL_NUMBER);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTidewrightRiverNarrowsTest, "Tidewright.River.FasterInNarrows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTidewrightRiverNarrowsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("A world to put the river in"), World))
	{
		return false;
	}
	ATidewrightRiver* River = World->SpawnActor<ATidewrightRiver>();
	// A level river 4 m wide, 80 m long, squeezed to half its width 40 m down.
	USplineComponent* Spline = River->GetSpline();
	Spline->ClearSplinePoints(false);
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Spline->AddSplinePoint(FVector(2000.0 * Index, 0.0, 0.0), ESplineCoordinateSpace::World, false);
		Spline->SetScaleAtSplinePoint(Index, FVector(1.0, Index == 2 ? 0.5 : 1.0, 1.0), false);
	}
	Spline->UpdateSpline();
	River->River.Width = 4.0f;
	River->RebuildRiver();

	auto SpeedAt = [River](double X) {
		FVector Surface, Normal, Velocity;
		return River->SampleRiver(FVector(X, 0.0, 0.0), Surface, Normal, Velocity) ? Velocity.Size() : -1.0;
	};
	const double Wide = SpeedAt(500.0), Narrow = SpeedAt(4000.0);
	TestTrue(TEXT("The water is found in both places"), Wide > 0.0 && Narrow > 0.0);
	TestTrue(TEXT("The narrows run faster than the wide water"), Narrow > Wide * 1.3);
	TestTrue(TEXT("No faster than the limit"), Narrow <= River->River.FlowSpeed * 100.0 * 1.6 + 1.0);

	River->River.NarrowsSpeedup = 0.0f;
	River->RebuildRiver();
	TestTrue(TEXT("Narrows Speedup 0 keeps one speed"), FMath::IsNearlyEqual(SpeedAt(500.0), SpeedAt(4000.0), 1.0));

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTidewrightNiagaraQueryTest, "Tidewright.Niagara.NoWaterIsInvalid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTidewrightNiagaraQueryTest::RunTest(const FString& Parameters)
{
	FVector Surface, Normal, Velocity(1.0, 2.0, 3.0);
	const bool bFound = UNiagaraDataInterfaceTidewrightWater::SampleWater(nullptr, TConstArrayView<TWeakObjectPtr<ATidewrightRiver>>(), FVector(10.0, 20.0, 30.0), Surface, Normal, Velocity);
	TestFalse(TEXT("With no water and no rivers the query reports nothing found"), bFound);
	TestTrue(TEXT("Velocity is cleared"), Velocity.IsZero());
	TestTrue(TEXT("The data interface runs on CPU emitters only"),
		GetDefault<UNiagaraDataInterfaceTidewrightWater>()->CanExecuteOnTarget(ENiagaraSimTarget::CPUSim)
		&& !GetDefault<UNiagaraDataInterfaceTidewrightWater>()->CanExecuteOnTarget(ENiagaraSimTarget::GPUComputeSim));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
