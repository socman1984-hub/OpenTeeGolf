// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "TidewrightFloatingProp.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "TidewrightBuoyancyComponent.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TidewrightFloatingProp)

ATidewrightFloatingProp::ATidewrightFloatingProp()
{
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetSimulatePhysics(true);
	Mesh->SetLinearDamping(0.05f);
	Mesh->SetAngularDamping(0.4f);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Mesh->SetStaticMesh(Cube.Object);
	}

	Buoyancy = CreateDefaultSubobject<UTidewrightBuoyancyComponent>(TEXT("Buoyancy"));
}
