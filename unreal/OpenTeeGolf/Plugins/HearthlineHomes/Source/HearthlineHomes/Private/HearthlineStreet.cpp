// Copyright __PUBLISHER__. All Rights Reserved.
#include "HearthlineStreet.h"
#include "HearthlinePlanting.h"
#include "Components/DynamicMeshComponent.h"
#include "Materials/MaterialInterface.h"

AHearthlineStreet::AHearthlineStreet()
{
	PrimaryActorTick.bCanEverTick = false;
	MeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Mesh"));
	RootComponent = MeshComponent;
	MeshComponent->SetMobility(EComponentMobility::Static);
	MeshComponent->SetVertexColorSpaceTransformMode(EDynamicMeshVertexColorTransformMode::LinearToSRGB);
	MeshComponent->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
	MeshComponent->CollisionType = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	MeshComponent->bEnableComplexCollision = true;
	MeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
	Materials.LoadDefaults();
}

void AHearthlineStreet::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void AHearthlineStreet::BeginPlay()
{
	Super::BeginPlay();
	if (!bBuiltOnce) { Rebuild(); }
}

void AHearthlineStreet::Rebuild()
{
	bBuiltOnce = true;
	if (!MeshComponent) { return; }
	using FB = FHearthlineMeshBuilder;
	FB B;
	const double Hx = Length * 0.5, Hr = RoadWidth * 0.5, Ch = CurbHeight;
	const FVector UpAxis(0, 0, 1);
	const uint8 Slab = FB::All & ~FB::ZN;

	// slots: 0 asphalt, 1 concrete, 2 grass, 3 paint
	B.Set(0, FLinearColor::White);
	B.QuadF(FVector(-Hx, -Hr, 0), FVector(Hx, -Hr, 0), FVector(Hx, Hr, 0), FVector(-Hx, Hr, 0), UpAxis);
	if (bCentreLine)
	{
		B.Set(3, LineColor);
		for (double X = -Hx + 150; X + 300 < Hx; X += 900)
		{
			B.QuadF(FVector(X, -6, 0.4), FVector(X + 300, -6, 0.4), FVector(X + 300, 6, 0.4), FVector(X, 6, 0.4), UpAxis);
		}
	}
	for (int32 i = 0; i < 2; ++i)
	{
		const double S = i == 0 ? 1.0 : -1.0;
		double Y = Hr;
		B.Set(1, FLinearColor(0.72f, 0.72f, 0.7f));
		// curb and gutter pan
		B.Box(FVector(-Hx, S * (Y - 30), 0), FVector(Hx, S * Y, 1.0), Slab);
		B.Box(FVector(-Hx, S * Y, 0), FVector(Hx, S * (Y + 15), Ch), Slab);
		Y += 15;
		if (VergeWidth > 1)
		{
			B.Set(2, FLinearColor::White);
			B.QuadF(FVector(-Hx, S * Y, Ch), FVector(Hx, S * Y, Ch), FVector(Hx, S * (Y + VergeWidth), Ch), FVector(-Hx, S * (Y + VergeWidth), Ch), UpAxis);
			Y += VergeWidth;
		}
		if (SidewalkWidth > 1)
		{
			// separate slabs so the joints read at walking distance
			for (double X = -Hx; X < Hx - 1; X += 150)
			{
				const double Shade = 0.66 + 0.06 * FMath::Frac(FMath::Sin(X * 12.9898 + i * 78.233) * 43758.5453);
				B.Set(1, FLinearColor(Shade, Shade, Shade * 0.98));
				B.Box(FVector(X + 0.6, S * Y, 0), FVector(FMath::Min(X + 150, Hx) - 0.6, S * (Y + SidewalkWidth), Ch + 1.0), Slab);
			}
		}
	}

	if (TreeSpacing > 50 && VergeWidth > 60)
	{
		FRandomStream R(TreeSeed * 131 + 7);
		FHearthlinePlanting::FSlots Slots; Slots.Bark = 4; Slots.Foliage = 5; Slots.Inner = 2; Slots.Tint = FoliageColor;
		for (int32 i = 0; i < 2; ++i)
		{
			const double S = i == 0 ? 1.0 : -1.0;
			for (double X = -Hx + TreeSpacing * (i == 0 ? 0.5 : 0.85); X < Hx - 150; X += TreeSpacing)
			{
				FHearthlinePlanting::Tree(B, R, FVector(X + R.FRandRange(-80.f, 80.f), S * (Hr + 15 + VergeWidth * 0.5), Ch), R.FRandRange(700.f, 980.f), Slots);
			}
		}
	}

	UE::Geometry::FDynamicMesh3& Mesh = B.Mesh;
	MeshComponent->SetMesh(MoveTemp(Mesh));
	TArray<UMaterialInterface*> Mats = { Materials.Asphalt, Materials.Concrete, Materials.Grass, Materials.Trim, Materials.Wood, Materials.Foliage };
	MeshComponent->ConfigureMaterialSet(Mats);
	MeshComponent->UpdateCollision(false);
}
