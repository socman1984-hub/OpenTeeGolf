// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "GreenswardWindController.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

AGreenswardWindController::AGreenswardWindController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

#if WITH_EDITORONLY_DATA
	Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("WindDirection"));
	if (Arrow)
	{
		Arrow->SetupAttachment(Root);
		Arrow->ArrowColor = FColor(120, 220, 120);
		Arrow->ArrowSize = 3.0f;
		Arrow->bIsScreenSizeScaled = true;
	}
#endif

	ParameterCollection = TSoftObjectPtr<UMaterialParameterCollection>(
		FSoftObjectPath(TEXT("/Greensward/Materials/MPC_Greensward.MPC_Greensward")));
}

void AGreenswardWindController::Apply()
{
	UWorld* World = GetWorld();
	UMaterialParameterCollection* Collection = ParameterCollection.LoadSynchronous();
	if (!World || !Collection)
	{
		return;
	}

	UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(Collection);
	if (!Instance)
	{
		return;
	}

	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
	const FVector2D Dir = Forward.IsNearlyZero() ? FVector2D(1.0, 0.0) : FVector2D(Forward.X, Forward.Y);

	Instance->SetVectorParameterValue(TEXT("Wind"), FLinearColor(Dir.X, Dir.Y, WindStrength, WindSpeed));
	Instance->SetVectorParameterValue(TEXT("Environment"), FLinearColor(Season, Dryness, Snow, Wetness));
}

void AGreenswardWindController::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Apply();
}

void AGreenswardWindController::BeginPlay()
{
	Super::BeginPlay();
	Apply();
}

void AGreenswardWindController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Apply();
}
