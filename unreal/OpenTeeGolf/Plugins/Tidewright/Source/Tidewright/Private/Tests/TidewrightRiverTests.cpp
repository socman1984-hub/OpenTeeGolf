// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

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
