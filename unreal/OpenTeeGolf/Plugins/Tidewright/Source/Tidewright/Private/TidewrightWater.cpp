// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "TidewrightWater.h"

#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "RHIUtilities.h"
#include "RenderingThread.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "TextureResource.h"
#include "TidewrightPreset.h"
#include "UObject/ConstructorHelpers.h"
#include "TidewrightShoreline.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TidewrightWater)

namespace TidewrightWater
{
	/** Radius of the innermost ring of the surface mesh, in centimetres. */
	static constexpr double InnerRadius = 20.0;

	/** Vertical padding added to the mesh bounds so tall waves are never culled, in centimetres. */
	static constexpr double BoundsPadding = 4000.0;

	static const FName DisplacementNames[FTidewrightSimulation::NumCascades] = { TEXT("Displacement0"), TEXT("Displacement1"), TEXT("Displacement2"), TEXT("Displacement3") };
	static const FName DerivativeNames[FTidewrightSimulation::NumCascades] = { TEXT("Derivative0"), TEXT("Derivative1"), TEXT("Derivative2"), TEXT("Derivative3") };

	template <typename StructType>
	static bool StructsEqual(const StructType& A, const StructType& B)
	{
		return StructType::StaticStruct()->CompareScriptStruct(&A, &B, PPF_None);
	}

	static UTexture2D* CreateWaveTexture(UObject* Outer, int32 Size)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Size, Size, PF_A32B32G32R32F);
		if (!Texture)
		{
			return nullptr;
		}
		Texture->SRGB = false;
		Texture->CompressionSettings = TC_HDR;
		Texture->Filter = TF_Default;
		Texture->LODGroup = TEXTUREGROUP_World;
		Texture->AddressX = TA_Wrap;
		Texture->AddressY = TA_Wrap;
		Texture->NeverStream = true;

		FTexturePlatformData* PlatformData = Texture->GetPlatformData();
		{
			FTexture2DMipMap& Mip = PlatformData->Mips[0];
			void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memzero(Data, static_cast<SIZE_T>(Size) * Size * sizeof(FVector4f));
			Mip.BulkData.Unlock();
		}
		for (int32 MipSize = Size / 2; MipSize >= 1; MipSize /= 2)
		{
			FTexture2DMipMap* Mip = new FTexture2DMipMap(MipSize, MipSize, 1);
			PlatformData->Mips.Add(Mip);
			Mip->BulkData.Lock(LOCK_READ_WRITE);
			void* Data = Mip->BulkData.Realloc(static_cast<int64>(MipSize) * MipSize * sizeof(FVector4f));
			FMemory::Memzero(Data, static_cast<SIZE_T>(MipSize) * MipSize * sizeof(FVector4f));
			Mip->BulkData.Unlock();
		}
		Texture->UpdateResource();
		return Texture;
	}

	static void UploadMip(UTexture2D* Texture, int32 MipIndex, const TArray<FVector4f>& Texels, int32 Size)
	{
		if (!Texture || !Texture->GetResource())
		{
			return;
		}
		const SIZE_T Bytes = static_cast<SIZE_T>(Texels.Num()) * sizeof(FVector4f);
		uint8* Copy = static_cast<uint8*>(FMemory::Malloc(Bytes));
		FMemory::Memcpy(Copy, Texels.GetData(), Bytes);
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Size, Size);
		Texture->UpdateTextureRegions(MipIndex, 1, Region, Size * sizeof(FVector4f), sizeof(FVector4f), Copy,
			[](uint8* Data, const FUpdateTextureRegion2D* Regions)
			{
				FMemory::Free(Data);
				delete Regions;
			});
	}
}

ATidewrightWater::ATidewrightWater()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// Hard references found at construction, so that packaging a game always includes these assets.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> UnderwaterFinder(TEXT("/Tidewright/Materials/M_Tidewright_Underwater.M_Tidewright_Underwater"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WaterFinder(TEXT("/Tidewright/Materials/M_Tidewright_Water.M_Tidewright_Water"));
	static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> Waves0Finder(TEXT("/Tidewright/Shared/RT_Tidewright_Waves0.RT_Tidewright_Waves0"));
	static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> Waves1Finder(TEXT("/Tidewright/Shared/RT_Tidewright_Waves1.RT_Tidewright_Waves1"));
	static ConstructorHelpers::FObjectFinder<UMaterialParameterCollection> CollectionFinder(TEXT("/Tidewright/Shared/MPC_Tidewright.MPC_Tidewright"));
	SharedWaveTargets[0] = Waves0Finder.Object;
	SharedWaveTargets[1] = Waves1Finder.Object;
	SharedCollection = CollectionFinder.Object;
	UnderwaterMaterial = UnderwaterFinder.Object;
	WaterMaterial = WaterFinder.Object;
}

int32 ATidewrightWater::GetResolution() const
{
	switch (SimulationQuality)
	{
	case ETidewrightSimulationQuality::Low: return 64;
	case ETidewrightSimulationQuality::High: return 256;
	default: return 128;
	}
}

void ATidewrightWater::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	EnsureSurface();
}

void ATidewrightWater::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseSurface();
	Super::EndPlay(EndPlayReason);
}

void ATidewrightWater::Destroyed()
{
	ReleaseSurface();
	Super::Destroyed();
}

#if WITH_EDITOR
void ATidewrightWater::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ATidewrightWater, Preset) && Preset)
	{
		Waves = Preset->Waves;
		Look = Preset->Look;
	}
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ATidewrightWater, WaterMaterial))
	{
		SurfaceMaterial = nullptr;
	}
	bMaterialDirty = true;
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void ATidewrightWater::ApplyPreset(UTidewrightPreset* NewPreset, float BlendSeconds)
{
	if (!NewPreset)
	{
		return;
	}
	Preset = NewPreset;
	Look = NewPreset->Look;
	SetWaveSettings(NewPreset->Waves, BlendSeconds);
}

void ATidewrightWater::SetWaveSettings(const FTidewrightWaveSettings& NewWaves, float BlendSeconds)
{
	Waves = NewWaves;
	PendingBlendSeconds = FMath::Max(BlendSeconds, 0.0f);
}

void ATidewrightWater::UpdateShore(const FVector& ViewLocation)
{
	if (!SurfaceMaterial)
	{
		return;
	}

	// One shoreline shapes the water at a time: the scanned one nearest the camera.
	ATidewrightShoreline* Nearest = nullptr;
	double NearestDistance = TNumericLimits<double>::Max();
	for (TActorIterator<ATidewrightShoreline> It(GetWorld()); It; ++It)
	{
		if (It->HasScan())
		{
			const double Distance = It->DistanceToArea(ViewLocation);
			if (Distance < NearestDistance)
			{
				NearestDistance = Distance;
				Nearest = *It;
			}
		}
	}

	const FTidewrightShoreField* Field = Nearest ? Nearest->GetField(GetActorLocation().Z) : nullptr;
	if (!Field)
	{
		if (ActiveShoreline.IsValid() || !bShoreApplied)
		{
			ActiveShoreline = nullptr;
			bShoreApplied = true;
			SurfaceMaterial->SetScalarParameterValue(TEXT("TW_ShoreOn"), 0.0f);
		}
		return;
	}

	if (ActiveShoreline.Get() != Nearest || AppliedShoreVersion != Nearest->GetFieldVersion() || !bShoreApplied || !ShoreTexture)
	{
		const int32 Size = Field->GetResolution();
		if (!ShoreTexture || ShoreTexture->GetSizeX() != Size)
		{
			ShoreTexture = UTexture2D::CreateTransient(Size, Size, PF_A32B32G32R32F);
			ShoreTexture->SRGB = false;
			ShoreTexture->CompressionSettings = TC_HDR;
			ShoreTexture->Filter = TF_Bilinear;
			ShoreTexture->AddressX = TA_Clamp;
			ShoreTexture->AddressY = TA_Clamp;
			ShoreTexture->NeverStream = true;
		}
		{
			FTexture2DMipMap& Mip = ShoreTexture->GetPlatformData()->Mips[0];
			void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(Data, Field->GetTexels().GetData(), static_cast<SIZE_T>(Size) * Size * sizeof(FVector4f));
			Mip.BulkData.Unlock();
		}
		ShoreTexture->UpdateResource();

		ActiveShoreline = Nearest;
		AppliedShoreVersion = Nearest->GetFieldVersion();
		bShoreApplied = true;

		const FVector2D ShoreCentre = Field->GetCentreCm();
		const FVector2D HalfSize = Field->GetHalfSizeCm();
		SurfaceMaterial->SetTextureParameterValue(TEXT("ShoreTexture"), ShoreTexture);
		SurfaceMaterial->SetVectorParameterValue(TEXT("TW_ShoreRect"), FLinearColor(
			static_cast<float>(ShoreCentre.X), static_cast<float>(ShoreCentre.Y),
			static_cast<float>(0.5 / HalfSize.X), static_cast<float>(0.5 / HalfSize.Y)));
		SurfaceMaterial->SetScalarParameterValue(TEXT("TW_ShoreOn"), 1.0f);
	}

	const FTidewrightShoreSettings& Shore = Nearest->Shore;
	SurfaceMaterial->SetVectorParameterValue(TEXT("TW_ShoreA"), FLinearColor(
		FMath::Max(Shore.WaveDampingDepth, 0.05f), Shore.ShoreFoamDepth, Shore.RunUpHeight, FMath::Max(Shore.SurfPeriod, 0.1f)));
	SurfaceMaterial->SetVectorParameterValue(TEXT("TW_ShoreB"), FLinearColor(
		Shore.SurfHeight, FMath::Max(Shore.SurfWavelength, 0.1f), FMath::Max(Shore.BreakDepth, 0.05f), Shore.ShoreFoamAmount));
	SurfaceMaterial->SetScalarParameterValue(TEXT("TW_Time"), FTidewrightShoreField::WrapTime(WaveTime, Shore.SurfPeriod));
}

void ATidewrightWater::ReleaseSurface()
{
	if (SurfaceMesh)
	{
		SurfaceMesh->DestroyComponent();
		SurfaceMesh = nullptr;
	}
	if (UnderwaterPost)
	{
		UnderwaterPost->DestroyComponent();
		UnderwaterPost = nullptr;
	}
	UnderwaterInstance = nullptr;
	bViewUnderwater = false;
	SurfaceMaterial = nullptr;
	bHasCentre = false;
}

void ATidewrightWater::ShareWaves()
{
	if (!SharedCollection || !SharedWaveTargets[0] || !SharedWaveTargets[1])
	{
		return;
	}

	const bool bShare = bShareWavesWithMaterials && DisplacementTextures.Num() == FTidewrightSimulation::NumCascades;
	const int32 Size = Simulation.GetResolution();
	if (bShare)
	{
		// The two largest wave bands are copied on the GPU into render target assets that any material can read.
		for (int32 Index = 0; Index < 2; ++Index)
		{
			UTextureRenderTarget2D* Target = SharedWaveTargets[Index];
			if (Target->SizeX != Size || Target->SizeY != Size || Target->GetFormat() != PF_A32B32G32R32F)
			{
				Target->AddressX = TA_Wrap;
				Target->AddressY = TA_Wrap;
				Target->InitCustomFormat(Size, Size, PF_A32B32G32R32F, true);
				Target->UpdateResourceImmediate(true);
			}
			FTextureResource* Source = DisplacementTextures[Index] ? DisplacementTextures[Index]->GetResource() : nullptr;
			FTextureRenderTargetResource* Destination = Target->GameThread_GetRenderTargetResource();
			if (!Source || !Destination)
			{
				continue;
			}
			ENQUEUE_RENDER_COMMAND(TidewrightShareWaves)([Source, Destination](FRHICommandListImmediate& RHICmdList)
			{
				FRHITexture* From = Source->TextureRHI;
				FRHITexture* To = Destination->GetRenderTargetTexture();
				if (From && To && From->GetSizeXY() == To->GetSizeXY() && From->GetFormat() == To->GetFormat())
				{
					FRHICopyTextureInfo Info;
					Info.NumMips = 1;
					TransitionAndCopyTexture(RHICmdList, From, To, Info);
				}
			});
		}
	}

	const float InvLength0 = 1.0f / (FMath::Max(Simulation.GetCascade(0).LengthM, 0.01f) * 100.0f);
	const float InvLength1 = 1.0f / (FMath::Max(Simulation.GetCascade(1).LengthM, 0.01f) * 100.0f);
	UKismetMaterialLibrary::SetVectorParameterValue(this, SharedCollection, TEXT("TW_FloatWaves"),
		FLinearColor(InvLength0, InvLength1, 1.0f / FMath::Max(Size, 1), bShare ? 1.0f : 0.0f));
	UKismetMaterialLibrary::SetScalarParameterValue(this, SharedCollection, TEXT("TW_FloatLevel"), static_cast<float>(GetActorLocation().Z));
}

void ATidewrightWater::UpdateUnderwater()
{
	UWorld* World = GetWorld();
	if (SurfaceMaterial)
	{
		SurfaceMaterial->SetVectorParameterValue(TEXT("TW_UndersideColor"), FLinearColor(0.55f, 0.82f, 1.0f, 1.0f) * (Underwater.bEnabled ? Underwater.SkyGlow : 0.0f));
	}
	bViewUnderwater = false;
	ViewDepth = -1000.0f;

	bool bActive = false;
	FVector Surface = FVector::ZeroVector, Normal = FVector::UpVector;
	FVector View = FVector::ZeroVector;
	if (World && Underwater.bEnabled && World->ViewLocationsRenderedLastFrame.Num() > 0)
	{
		View = World->ViewLocationsRenderedLastFrame[0];
		if (SampleWaterSurface(View, Surface, Normal))
		{
			ViewDepth = static_cast<float>((Surface.Z - View.Z) * 0.01);
			bViewUnderwater = ViewDepth > 0.0f;
			// The effect is only needed once the lens is close enough to the surface to touch it.
			bActive = ViewDepth > -1.0f;
		}
	}

	if (bActive && !UnderwaterPost)
	{
		if (UMaterialInterface* Base = UnderwaterMaterial)
		{
			UnderwaterInstance = UMaterialInstanceDynamic::Create(Base, this);
			UnderwaterPost = NewObject<UPostProcessComponent>(this, NAME_None, RF_Transient);
			UnderwaterPost->SetupAttachment(SceneRoot);
			UnderwaterPost->bUnbound = true;
			UnderwaterPost->Priority = 1000.0f;
			UnderwaterPost->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, UnderwaterInstance));
			UnderwaterPost->RegisterComponent();
		}
	}
	if (!UnderwaterPost)
	{
		return;
	}

	UnderwaterPost->bEnabled = bActive;
	UnderwaterPost->BlendWeight = bActive ? 1.0f : 0.0f;
	if (!bActive || !UnderwaterInstance)
	{
		return;
	}

	// The surface near the lens is described as a plane through the point above or below the camera.
	const float SignedDistance = static_cast<float>((View.Z - Surface.Z) * Normal.Z);
	UnderwaterInstance->SetVectorParameterValue(TEXT("TW_UnderPlane"), FLinearColor(
		static_cast<float>(Normal.X), static_cast<float>(Normal.Y), static_cast<float>(Normal.Z), SignedDistance));

	const float Falloff = FMath::Exp(-FMath::Max(ViewDepth, 0.0f) / FMath::Max(Underwater.LightFalloffDepth, 1.0f) * 1.1f);
	UnderwaterInstance->SetVectorParameterValue(TEXT("TW_Absorption"), Look.Absorption);
	UnderwaterInstance->SetVectorParameterValue(TEXT("TW_Scatter"), Look.ScatterColor * Look.ScatterDensity);
	UnderwaterInstance->SetVectorParameterValue(TEXT("TW_UnderParams"), FLinearColor(
		Underwater.FogDensity, Underwater.Brightness * Falloff, Underwater.WaterlineStrength, 0.0f));
}

void ATidewrightWater::EnsureSurface()
{
	UWorld* World = GetWorld();
	if (!World || IsTemplate() || IsRunningDedicatedServer() || !IsValid(this) || IsActorBeingDestroyed())
	{
		return;
	}
	if (World->WorldType == EWorldType::Inactive || World->WorldType == EWorldType::None)
	{
		return;
	}

	if (!SurfaceMesh)
	{
		SurfaceMesh = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
		SurfaceMesh->SetMobility(EComponentMobility::Movable);
		SurfaceMesh->SetupAttachment(SceneRoot);
		SurfaceMesh->SetUsingAbsoluteLocation(true);
		SurfaceMesh->SetUsingAbsoluteRotation(true);
		SurfaceMesh->SetUsingAbsoluteScale(true);
		SurfaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SurfaceMesh->SetGenerateOverlapEvents(false);
		SurfaceMesh->SetCastShadow(false);
		SurfaceMesh->SetCanEverAffectNavigation(false);
		SurfaceMesh->bUseAsyncCooking = false;
		SurfaceMesh->bAffectDistanceFieldLighting = false;
		SurfaceMesh->RegisterComponent();
		SurfaceMesh->SetWorldLocationAndRotation(GetActorLocation(), FQuat::Identity);
		BuiltSegments = 0;
		SurfaceMaterial = nullptr;
	}

	if (BuiltSegments != SurfaceSegments || BuiltViewDistance != ViewDistance)
	{
		BuildMesh();
		bMaterialDirty = true;
	}

	if (!SurfaceMaterial)
	{
		if (UMaterialInterface* Base = WaterMaterial)
		{
			SurfaceMaterial = UMaterialInstanceDynamic::Create(Base, this);
			SurfaceMesh->SetMaterial(0, SurfaceMaterial);
			bMaterialDirty = true;
		}
	}
}

void ATidewrightWater::BuildMesh()
{
	const int32 Segments = FMath::Clamp(SurfaceSegments, 48, 512);
	const double Growth = 1.0 + UE_DOUBLE_TWO_PI / Segments;
	const double MaxRadius = FMath::Max<double>(ViewDistance, 0.2) * 100000.0;

	TArray<double> Radii;
	for (double Radius = TidewrightWater::InnerRadius; ; Radius *= Growth)
	{
		Radii.Add(FMath::Min(Radius, MaxRadius));
		if (Radius >= MaxRadius)
		{
			break;
		}
	}

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	Vertices.Reserve(Radii.Num() * Segments + 3);
	Triangles.Reserve(Radii.Num() * Segments * 6);

	Vertices.Add(FVector::ZeroVector);
	for (const double Radius : Radii)
	{
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const double Angle = UE_DOUBLE_TWO_PI * Segment / Segments;
			Vertices.Add(FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.0));
		}
	}

	// Triangles are emitted so that their face normal, (C - A) x (B - A), points up.
	auto AddTriangle = [&](int32 A, int32 B, int32 C)
	{
		const FVector Normal = FVector::CrossProduct(Vertices[C] - Vertices[A], Vertices[B] - Vertices[A]);
		Triangles.Add(A);
		if (Normal.Z >= 0.0)
		{
			Triangles.Add(B);
			Triangles.Add(C);
		}
		else
		{
			Triangles.Add(C);
			Triangles.Add(B);
		}
	};

	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		AddTriangle(0, 1 + Segment, 1 + (Segment + 1) % Segments);
	}
	for (int32 Ring = 0; Ring + 1 < Radii.Num(); ++Ring)
	{
		const int32 Inner = 1 + Ring * Segments;
		const int32 Outer = Inner + Segments;
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const int32 Next = (Segment + 1) % Segments;
			AddTriangle(Inner + Segment, Inner + Next, Outer + Next);
			AddTriangle(Inner + Segment, Outer + Next, Outer + Segment);
		}
	}

	// Two loose vertices give the bounds vertical thickness for the displaced waves.
	Vertices.Add(FVector(0.0, 0.0, TidewrightWater::BoundsPadding));
	Vertices.Add(FVector(0.0, 0.0, -TidewrightWater::BoundsPadding));

	TArray<FVector> Normals;
	Normals.Init(FVector::UpVector, Vertices.Num());
	TArray<FProcMeshTangent> Tangents;
	Tangents.Init(FProcMeshTangent(1.0f, 0.0f, 0.0f), Vertices.Num());

	SurfaceMesh->ClearAllMeshSections();
	SurfaceMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, TArray<FVector2D>(), TArray<FLinearColor>(), Tangents, false);
	if (SurfaceMaterial)
	{
		SurfaceMesh->SetMaterial(0, SurfaceMaterial);
	}

	BuiltSegments = SurfaceSegments;
	BuiltViewDistance = ViewDistance;
}

void ATidewrightWater::CreateTextures()
{
	const int32 Size = Simulation.GetResolution();
	DisplacementTextures.Reset();
	DerivativeTextures.Reset();
	for (int32 Index = 0; Index < FTidewrightSimulation::NumCascades; ++Index)
	{
		DisplacementTextures.Add(TidewrightWater::CreateWaveTexture(this, Size));
		DerivativeTextures.Add(TidewrightWater::CreateWaveTexture(this, Size));
	}
	bMaterialDirty = true;
}

void ATidewrightWater::UploadTextures()
{
	if (DisplacementTextures.Num() != FTidewrightSimulation::NumCascades)
	{
		return;
	}
	const int32 Size = Simulation.GetResolution();
	for (int32 Index = 0; Index < FTidewrightSimulation::NumCascades; ++Index)
	{
		const FTidewrightCascade& Cascade = Simulation.GetCascade(Index);
		for (int32 Mip = 0; Mip < Cascade.DisplacementMips.Num(); ++Mip)
		{
			TidewrightWater::UploadMip(DisplacementTextures[Index], Mip, Cascade.DisplacementMips[Mip], Size >> Mip);
			TidewrightWater::UploadMip(DerivativeTextures[Index], Mip, Cascade.DerivativeMips[Mip], Size >> Mip);
		}
	}
}

void ATidewrightWater::PushMaterialParameters(bool bForce)
{
	if (!SurfaceMaterial)
	{
		return;
	}
	if (!bForce && !bMaterialDirty && TidewrightWater::StructsEqual(Look, AppliedLook))
	{
		return;
	}
	AppliedLook = Look;
	bMaterialDirty = false;

	const int32 Size = FMath::Max(Simulation.GetResolution(), 1);
	FLinearColor InvLength, Texel, SlopeVariance;
	float* InvLengthValues = &InvLength.R;
	float* TexelValues = &Texel.R;
	float* SlopeValues = &SlopeVariance.R;
	for (int32 Index = 0; Index < FTidewrightSimulation::NumCascades; ++Index)
	{
		const FTidewrightCascade& Cascade = Simulation.GetCascade(Index);
		const float LengthCm = FMath::Max(Cascade.LengthM, 0.01f) * 100.0f;
		InvLengthValues[Index] = 1.0f / LengthCm;
		TexelValues[Index] = LengthCm / Size;
		SlopeValues[Index] = Cascade.SlopeVariance;

		if (DisplacementTextures.IsValidIndex(Index))
		{
			SurfaceMaterial->SetTextureParameterValue(TidewrightWater::DisplacementNames[Index], DisplacementTextures[Index]);
			SurfaceMaterial->SetTextureParameterValue(TidewrightWater::DerivativeNames[Index], DerivativeTextures[Index]);
		}
	}

	SurfaceMaterial->SetVectorParameterValue(TEXT("TW_InvLength"), InvLength);
	SurfaceMaterial->SetVectorParameterValue(TEXT("TW_Texel"), Texel);
	SurfaceMaterial->SetVectorParameterValue(TEXT("TW_SlopeVariance"), SlopeVariance);
	SurfaceMaterial->SetScalarParameterValue(TEXT("TW_InvResolution"), 1.0f / Size);
	SurfaceMaterial->SetScalarParameterValue(TEXT("TW_MaxLod"), static_cast<float>(FMath::FloorLog2(Size)) - 2.0f);
	SurfaceMaterial->SetScalarParameterValue(TEXT("TW_Spacing"), UE_TWO_PI / FMath::Clamp(SurfaceSegments, 48, 512));
	SurfaceMaterial->SetScalarParameterValue(TEXT("TW_FoamThreshold"), Simulation.GetFoamThreshold());
	SurfaceMaterial->SetScalarParameterValue(TEXT("TW_FoamGain"), Simulation.GetFoamGain());
	SurfaceMaterial->SetScalarParameterValue(TEXT("TW_InnerRadius"), static_cast<float>(TidewrightWater::InnerRadius));
	{
		const float WindRadians = FMath::DegreesToRadians(Waves.WindDirection);
		SurfaceMaterial->SetVectorParameterValue(TEXT("TW_Wind"), FLinearColor(FMath::Cos(WindRadians), FMath::Sin(WindRadians), 0.0f, 0.0f));
	}
	bShoreApplied = false;

	const FVector Location = GetActorLocation();
	if (Mode == ETidewrightWaterMode::Lake)
	{
		SurfaceMaterial->SetVectorParameterValue(TEXT("TW_Lake"), FLinearColor(
			static_cast<float>(Location.X), static_cast<float>(Location.Y),
			static_cast<float>(LakeSize.X * 50.0), static_cast<float>(LakeSize.Y * 50.0)));
	}
	else
	{
		SurfaceMaterial->SetVectorParameterValue(TEXT("TW_Lake"), FLinearColor(0.0f, 0.0f, 1.0e12f, 1.0e12f));
	}

	SurfaceMaterial->SetVectorParameterValue(TEXT("TW_ScatterColor"), Look.ScatterColor);
	SurfaceMaterial->SetScalarParameterValue(TEXT("TW_ScatterDensity"), Look.ScatterDensity);
	SurfaceMaterial->SetVectorParameterValue(TEXT("TW_Absorption"), Look.Absorption);
	SurfaceMaterial->SetScalarParameterValue(TEXT("TW_PhaseG"), Look.ScatterAnisotropy);
	SurfaceMaterial->SetScalarParameterValue(TEXT("TW_Roughness"), Look.Roughness);
	SurfaceMaterial->SetScalarParameterValue(TEXT("TW_DistanceRoughening"), Look.DistanceRoughening);
	SurfaceMaterial->SetScalarParameterValue(TEXT("TW_NormalStrength"), Look.NormalStrength);
	SurfaceMaterial->SetVectorParameterValue(TEXT("TW_FoamColor"), Look.FoamColor);
	SurfaceMaterial->SetScalarParameterValue(TEXT("TW_FoamAmount"), Look.FoamAmount);
	SurfaceMaterial->SetScalarParameterValue(TEXT("TW_FoamPatternSize"), FMath::Max(Look.FoamPatternSize, 0.01f) * 100.0f);
}

void ATidewrightWater::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (bUseManualTime)
	{
		WaveTime = ManualTime;
	}
	else
	{
		WaveTime += static_cast<double>(DeltaSeconds) * TimeScale;
	}
	const float SimulationDelta = static_cast<float>(FMath::Abs(WaveTime - LastWaveTime));
	LastWaveTime = WaveTime;

	if (!Simulation.IsConfigured() || Simulation.GetResolution() != GetResolution() || BuiltSeed != Seed)
	{
		Simulation.Configure(Waves, GetResolution(), Seed);
		BuiltSeed = Seed;
		AppliedWaves = Waves;
		PendingBlendSeconds = 0.0f;
		DisplacementTextures.Reset();
		bMaterialDirty = true;
	}
	else if (!TidewrightWater::StructsEqual(Waves, AppliedWaves))
	{
		Simulation.SetSettings(Waves, PendingBlendSeconds);
		AppliedWaves = Waves;
		PendingBlendSeconds = 0.0f;
		bMaterialDirty = true;
	}

	const double SimulationStart = FPlatformTime::Seconds();
	Simulation.Update(WaveTime, SimulationDelta);

	EnsureSurface();
	if (!SurfaceMesh)
	{
		return;
	}

	if (DisplacementTextures.Num() != FTidewrightSimulation::NumCascades)
	{
		CreateTextures();
	}
	UploadTextures();
	LastSimulationMs = static_cast<float>((FPlatformTime::Seconds() - SimulationStart) * 1000.0);

	// Keep the mesh under the camera. The material cancels this motion for the velocity buffer so that
	// temporal anti-aliasing treats the water as world-anchored rather than sliding with the view.
	FVector Centre = GetActorLocation();
	if (World->ViewLocationsRenderedLastFrame.Num() > 0)
	{
		Centre.X = World->ViewLocationsRenderedLastFrame[0].X;
		Centre.Y = World->ViewLocationsRenderedLastFrame[0].Y;
	}
	const FVector Delta = bHasCentre ? Centre - MeshCentre : FVector::ZeroVector;
	MeshCentre = Centre;
	bHasCentre = true;
	SurfaceMesh->SetWorldLocationAndRotation(Centre, FQuat::Identity);

	PushMaterialParameters(false);
	ShareWaves();
	UpdateShore(Centre);
	UpdateUnderwater();
	if (SurfaceMaterial)
	{
		SurfaceMaterial->SetVectorParameterValue(TEXT("TW_MeshCentre"), FLinearColor(static_cast<float>(Centre.X), static_cast<float>(Centre.Y), static_cast<float>(Centre.Z), 0.0f));
		SurfaceMaterial->SetVectorParameterValue(TEXT("TW_MeshDelta"), FLinearColor(static_cast<float>(Delta.X), static_cast<float>(Delta.Y), static_cast<float>(Delta.Z), 0.0f));
	}
}

bool ATidewrightWater::IsInsideWater(const FVector& WorldLocation) const
{
	if (Mode != ETidewrightWaterMode::Lake)
	{
		return true;
	}
	const FVector Location = GetActorLocation();
	return FMath::Abs(WorldLocation.X - Location.X) <= LakeSize.X * 50.0 && FMath::Abs(WorldLocation.Y - Location.Y) <= LakeSize.Y * 50.0;
}

bool ATidewrightWater::SampleWaterSurface(const FVector& WorldLocation, FVector& OutSurfaceLocation, FVector& OutNormal) const
{
	const double RestLevel = GetActorLocation().Z;
	OutSurfaceLocation = FVector(WorldLocation.X, WorldLocation.Y, RestLevel);
	OutNormal = FVector::UpVector;
	if (!IsInsideWater(WorldLocation))
	{
		return false;
	}

	// Near a scanned shore the open-water waves are flattened and surf is added, exactly as drawn.
	FTidewrightShoreMotion Shore;
	bool bShore = false;
	if (ATidewrightShoreline* Shoreline = ActiveShoreline.Get())
	{
		if (const FTidewrightShoreField* Field = Shoreline->GetField(RestLevel))
		{
			const FVector2D Position(WorldLocation.X, WorldLocation.Y);
			const FTidewrightShorePoint Point = Field->Sample(Position);
			if (Point.Weight > 0.0f)
			{
				Shore = FTidewrightShoreField::Evaluate(Point, Shoreline->Shore, Position, WaveTime);
				bShore = true;
			}
		}
	}

	float Height = 0.0f;
	FVector3f Normal;
	if (!Simulation.SampleSurface(FVector2D(WorldLocation.X * 0.01, WorldLocation.Y * 0.01), Height, Normal, nullptr, bShore ? Shore.BandScale : nullptr))
	{
		return false;
	}
	if (bShore)
	{
		Height += Shore.Height;
		const FVector3f Tilted(Normal.X / FMath::Max(Normal.Z, 0.05f) - Shore.Slope.X, Normal.Y / FMath::Max(Normal.Z, 0.05f) - Shore.Slope.Y, 1.0f);
		Normal = Tilted.GetSafeNormal();
	}
	OutSurfaceLocation.Z = RestLevel + Height * 100.0;
	OutNormal = FVector(Normal);
	return true;
}

float ATidewrightWater::GetWaterHeightAtLocation(const FVector& WorldLocation) const
{
	FVector Surface, Normal;
	SampleWaterSurface(WorldLocation, Surface, Normal);
	return static_cast<float>(Surface.Z);
}

bool ATidewrightWater::IsLocationUnderwater(const FVector& WorldLocation) const
{
	FVector Surface, Normal;
	return SampleWaterSurface(WorldLocation, Surface, Normal) && WorldLocation.Z < Surface.Z;
}

float ATidewrightWater::GetSignificantWaveHeight() const
{
	return Simulation.GetSignificantWaveHeight();
}

ATidewrightWater* ATidewrightWater::GetTidewrightWater(const UObject* WorldContextObject)
{
	if (const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr)
	{
		// (the first one: an if, not a loop that returns on its first pass - clang stops on that)
		if (TActorIterator<ATidewrightWater> It(World); It)
		{
			return *It;
		}
	}
	return nullptr;
}
