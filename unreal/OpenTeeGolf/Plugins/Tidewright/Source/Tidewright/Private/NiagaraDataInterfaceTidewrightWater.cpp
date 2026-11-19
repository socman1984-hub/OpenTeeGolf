// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "NiagaraDataInterfaceTidewrightWater.h"

#include "EngineUtils.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraTypes.h"
#include "TidewrightRiver.h"
#include "TidewrightWater.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraDataInterfaceTidewrightWater)

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceTidewrightWater"

namespace TidewrightNiagara
{
	static const FName SampleWaterSurfaceName(TEXT("SampleWaterSurface"));
	static const FName GetWaterHeightName(TEXT("GetWaterHeight"));
	static const FName GetWaterVelocityName(TEXT("GetWaterVelocity"));

	struct FInstanceData
	{
		TWeakObjectPtr<ATidewrightWater> Water;
		TArray<TWeakObjectPtr<ATidewrightRiver>> Rivers;
		FNiagaraLWCConverter LWCConverter;
		float RefreshCountdown = 0.0f;
	};

	static void VMSampleWaterSurface(FVectorVMExternalFunctionContext& Context)
	{
		VectorVM::FUserPtrHandler<FInstanceData> Instance(Context);
		FNDIInputParam<FNiagaraPosition> InPosition(Context);

		FNDIOutputParam<FNiagaraPosition> OutSurface(Context);
		FNDIOutputParam<FVector3f> OutNormal(Context);
		FNDIOutputParam<FVector3f> OutVelocity(Context);
		FNDIOutputParam<float> OutDepth(Context);
		FNDIOutputParam<bool> OutValid(Context);

		const ATidewrightWater* Water = Instance->Water.Get();
		for (int32 Index = 0; Index < Context.GetNumInstances(); ++Index)
		{
			const FVector3f SimulationPosition = InPosition.GetAndAdvance();
			const FVector WorldPosition = Instance->LWCConverter.ConvertSimulationPositionToWorld(SimulationPosition);
			FVector Surface, Normal, Velocity;
			if (UNiagaraDataInterfaceTidewrightWater::SampleWater(Water, Instance->Rivers, WorldPosition, Surface, Normal, Velocity))
			{
				OutSurface.SetAndAdvance(Instance->LWCConverter.ConvertWorldToSimulationPosition(Surface));
				OutNormal.SetAndAdvance(FVector3f(Normal));
				OutVelocity.SetAndAdvance(FVector3f(Velocity));
				OutDepth.SetAndAdvance(static_cast<float>(Surface.Z - WorldPosition.Z));
				OutValid.SetAndAdvance(true);
			}
			else
			{
				OutSurface.SetAndAdvance(SimulationPosition);
				OutNormal.SetAndAdvance(FVector3f::UpVector);
				OutVelocity.SetAndAdvance(FVector3f::ZeroVector);
				OutDepth.SetAndAdvance(0.0f);
				OutValid.SetAndAdvance(false);
			}
		}
	}
}

namespace TidewrightNiagara
{
	static void VMGetWaterHeight(FVectorVMExternalFunctionContext& Context)
	{
		VectorVM::FUserPtrHandler<FInstanceData> Instance(Context);
		FNDIInputParam<FNiagaraPosition> InPosition(Context);
		FNDIOutputParam<float> OutHeight(Context);
		const ATidewrightWater* Water = Instance->Water.Get();
		for (int32 Index = 0; Index < Context.GetNumInstances(); ++Index)
		{
			const FVector3f SimulationPosition = InPosition.GetAndAdvance();
			FVector Surface, Normal, Velocity;
			const bool bFound = UNiagaraDataInterfaceTidewrightWater::SampleWater(Water, Instance->Rivers, Instance->LWCConverter.ConvertSimulationPositionToWorld(SimulationPosition), Surface, Normal, Velocity);
			OutHeight.SetAndAdvance(bFound ? Instance->LWCConverter.ConvertWorldToSimulationPosition(Surface).Z : SimulationPosition.Z);
		}
	}

	static void VMGetWaterVelocity(FVectorVMExternalFunctionContext& Context)
	{
		VectorVM::FUserPtrHandler<FInstanceData> Instance(Context);
		FNDIInputParam<FNiagaraPosition> InPosition(Context);
		FNDIOutputParam<FVector3f> OutVelocity(Context);
		const ATidewrightWater* Water = Instance->Water.Get();
		for (int32 Index = 0; Index < Context.GetNumInstances(); ++Index)
		{
			FVector Surface, Normal, Velocity;
			UNiagaraDataInterfaceTidewrightWater::SampleWater(Water, Instance->Rivers, Instance->LWCConverter.ConvertSimulationPositionToWorld(InPosition.GetAndAdvance()), Surface, Normal, Velocity);
			OutVelocity.SetAndAdvance(FVector3f(Velocity));
		}
	}
}

bool UNiagaraDataInterfaceTidewrightWater::SampleWater(const ATidewrightWater* WaterActor, TConstArrayView<TWeakObjectPtr<ATidewrightRiver>> Rivers, const FVector& WorldPosition,
	FVector& OutSurface, FVector& OutNormal, FVector& OutVelocity)
{
	for (const TWeakObjectPtr<ATidewrightRiver>& River : Rivers)
	{
		const ATidewrightRiver* RiverActor = River.Get();
		if (RiverActor && RiverActor->SampleRiver(WorldPosition, OutSurface, OutNormal, OutVelocity))
		{
			return true;
		}
	}
	OutVelocity = FVector::ZeroVector;
	return WaterActor && WaterActor->SampleWaterSurface(WorldPosition, OutSurface, OutNormal);
}

void UNiagaraDataInterfaceTidewrightWater::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		const ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceTidewrightWater::GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const
{
	FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
	Sig.Name = TidewrightNiagara::SampleWaterSurfaceName;
	Sig.Inputs.Emplace(FNiagaraTypeDefinition(GetClass()), TEXT("Water"));
	Sig.Inputs.Emplace(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Position"));
	Sig.Outputs.Emplace(FNiagaraTypeDefinition::GetPositionDef(), TEXT("SurfacePosition"));
	Sig.Outputs.Emplace(FNiagaraTypeDefinition::GetVec3Def(), TEXT("SurfaceNormal"));
	Sig.Outputs.Emplace(FNiagaraTypeDefinition::GetVec3Def(), TEXT("WaterVelocity"));
	Sig.Outputs.Emplace(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Depth"));
	Sig.Outputs.Emplace(FNiagaraTypeDefinition::GetBoolDef(), TEXT("IsValid"));
	Sig.bMemberFunction = true;
	{
		FNiagaraFunctionSignature& Height = OutFunctions.AddDefaulted_GetRef();
		Height.Name = TidewrightNiagara::GetWaterHeightName;
		Height.Inputs.Emplace(FNiagaraTypeDefinition(GetClass()), TEXT("Water"));
		Height.Inputs.Emplace(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Position"));
		Height.Outputs.Emplace(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Height"));
		Height.bMemberFunction = true;
		Height.SetDescription(LOCTEXT("GetWaterHeightDescription", "Z of the Tidewright water surface above or below a position. Returns the position's own Z where there is no water."));

		FNiagaraFunctionSignature& Velocity = OutFunctions.AddDefaulted_GetRef();
		Velocity.Name = TidewrightNiagara::GetWaterVelocityName;
		Velocity.Inputs.Emplace(FNiagaraTypeDefinition(GetClass()), TEXT("Water"));
		Velocity.Inputs.Emplace(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Position"));
		Velocity.Outputs.Emplace(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Velocity"));
		Velocity.bMemberFunction = true;
		Velocity.SetDescription(LOCTEXT("GetWaterVelocityDescription", "Velocity of the water at a position in centimetres per second: the current in a river, zero elsewhere."));
	}
	Sig.SetDescription(LOCTEXT("SampleWaterSurfaceDescription",
		"Finds the Tidewright water surface above or below a position. Depth is how far the position is below the surface in centimetres (negative above it). Water Velocity is the current, in centimetres per second; it is zero on the sea and lakes."));
}
#endif

void UNiagaraDataInterfaceTidewrightWater::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
	if (BindingInfo.Name == TidewrightNiagara::SampleWaterSurfaceName)
	{
		OutFunc = FVMExternalFunction::CreateStatic(&TidewrightNiagara::VMSampleWaterSurface);
	}
	else if (BindingInfo.Name == TidewrightNiagara::GetWaterHeightName)
	{
		OutFunc = FVMExternalFunction::CreateStatic(&TidewrightNiagara::VMGetWaterHeight);
	}
	else if (BindingInfo.Name == TidewrightNiagara::GetWaterVelocityName)
	{
		OutFunc = FVMExternalFunction::CreateStatic(&TidewrightNiagara::VMGetWaterVelocity);
	}
}

bool UNiagaraDataInterfaceTidewrightWater::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceTidewrightWater* Typed = CastChecked<const UNiagaraDataInterfaceTidewrightWater>(Other);
	return Typed->Water == Water && Typed->bIncludeRivers == bIncludeRivers;
}

bool UNiagaraDataInterfaceTidewrightWater::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	UNiagaraDataInterfaceTidewrightWater* Typed = CastChecked<UNiagaraDataInterfaceTidewrightWater>(Destination);
	Typed->Water = Water;
	Typed->bIncludeRivers = bIncludeRivers;
	return true;
}

int32 UNiagaraDataInterfaceTidewrightWater::PerInstanceDataSize() const
{
	return sizeof(TidewrightNiagara::FInstanceData);
}

bool UNiagaraDataInterfaceTidewrightWater::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	TidewrightNiagara::FInstanceData* Instance = new (PerInstanceData) TidewrightNiagara::FInstanceData();
	Instance->LWCConverter = SystemInstance->GetLWCConverter();
	return true;
}

void UNiagaraDataInterfaceTidewrightWater::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	static_cast<TidewrightNiagara::FInstanceData*>(PerInstanceData)->~FInstanceData();
}

bool UNiagaraDataInterfaceTidewrightWater::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	// Runs on the game thread before the particles simulate: the only place actors may be looked up.
	TidewrightNiagara::FInstanceData* Instance = static_cast<TidewrightNiagara::FInstanceData*>(PerInstanceData);
	Instance->LWCConverter = SystemInstance->GetLWCConverter();
	Instance->RefreshCountdown -= DeltaSeconds;
	if (Instance->RefreshCountdown <= 0.0f || !Instance->Water.IsValid())
	{
		Instance->RefreshCountdown = 1.0f;
		UWorld* World = SystemInstance->GetWorld();
		ATidewrightWater* Chosen = Water.Get();
		if (!Chosen || Chosen->GetWorld() != World)
		{
			Chosen = ATidewrightWater::GetTidewrightWater(World);
		}
		Instance->Water = Chosen;
		Instance->Rivers.Reset();
		if (bIncludeRivers && World)
		{
			for (TActorIterator<ATidewrightRiver> It(World); It; ++It)
			{
				Instance->Rivers.Add(*It);
			}
		}
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
