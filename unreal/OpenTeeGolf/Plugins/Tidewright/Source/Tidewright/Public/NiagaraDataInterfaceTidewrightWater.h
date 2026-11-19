// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceTidewrightWater.generated.h"

class ATidewrightWater;

/**
 * Lets Niagara particles find the water.
 *
 * Add it to an emitter as a user or emitter parameter and call Sample Water Surface from a CPU script: it returns
 * the surface point above or below a position, the surface normal, the water's velocity (rivers have a current)
 * and how deep the position is. With those a particle can float, drift, splash or die at the surface.
 * CPU emitters only.
 */
UCLASS(EditInlineNew, Category = "Tidewright", meta = (DisplayName = "Tidewright Water"))
class TIDEWRIGHT_API UNiagaraDataInterfaceTidewrightWater : public UNiagaraDataInterface
{
	GENERATED_BODY()

public:
	/** Water to read. Leave empty to use the first Tidewright Water in the level. Rivers are always included. */
	UPROPERTY(EditAnywhere, Category = "Tidewright")
	TSoftObjectPtr<ATidewrightWater> Water;

	/** Include Tidewright Rivers, which lie above the sea where the two overlap. */
	UPROPERTY(EditAnywhere, Category = "Tidewright")
	bool bIncludeRivers = true;

	/**
	 * The query behind the Niagara function, usable from C++ as well.
	 * @return false if there is no water at the position.
	 */
	static bool SampleWater(const ATidewrightWater* WaterActor, TConstArrayView<TWeakObjectPtr<class ATidewrightRiver>> Rivers, const FVector& WorldPosition,
		FVector& OutSurface, FVector& OutNormal, FVector& OutVelocity);

	//~ UObject
	virtual void PostInitProperties() override;

	//~ UNiagaraDataInterface
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::CPUSim; }
	virtual int32 PerInstanceDataSize() const override;
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual bool HasPreSimulateTick() const override { return true; }
	// The water simulates before physics; particles must read it afterwards, never while it is being written.
	virtual bool HasTickGroupPrereqs() const override { return true; }
	virtual ETickingGroup CalculateTickGroup(const void* PerInstanceData) const override { return TG_DuringPhysics; }
	virtual bool PostSimulateCanOverlapFrames() const override { return false; }

protected:
#if WITH_EDITORONLY_DATA
	virtual void GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const override;
#endif
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};
