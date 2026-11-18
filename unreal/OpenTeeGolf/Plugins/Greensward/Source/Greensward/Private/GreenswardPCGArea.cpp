// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "GreenswardPCGArea.h"

#include "Components/SplineComponent.h"
#include "PCGComponent.h"

AGreenswardPCGArea::AGreenswardPCGArea()
{
	PrimaryActorTick.bCanEverTick = false;

	Boundary = CreateDefaultSubobject<USplineComponent>(TEXT("Boundary"));
	SetRootComponent(Boundary);
	Boundary->SetMobility(EComponentMobility::Static);
	SetRectangle(1000.0f, 1000.0f);

	PCG = CreateDefaultSubobject<UPCGComponent>(TEXT("PCG"));
}

void AGreenswardPCGArea::SetRectangle(float SizeX, float SizeY)
{
	const float HX = FMath::Max(SizeX, 1.0f) * 0.5f;
	const float HY = FMath::Max(SizeY, 1.0f) * 0.5f;
	Boundary->ClearSplinePoints(false);
	Boundary->AddSplinePoint(FVector(-HX, -HY, 0), ESplineCoordinateSpace::Local, false);
	Boundary->AddSplinePoint(FVector(HX, -HY, 0), ESplineCoordinateSpace::Local, false);
	Boundary->AddSplinePoint(FVector(HX, HY, 0), ESplineCoordinateSpace::Local, false);
	Boundary->AddSplinePoint(FVector(-HX, HY, 0), ESplineCoordinateSpace::Local, false);
	for (int32 I = 0; I < 4; ++I)
	{
		Boundary->SetSplinePointType(I, ESplinePointType::Linear, false);
	}
	Boundary->SetClosedLoop(true, false);
	Boundary->UpdateSpline();
}
