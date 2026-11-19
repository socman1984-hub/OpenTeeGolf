// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "TidewrightRiver.h"

#include "CollisionQueryParams.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TidewrightRiver)

namespace TidewrightRiver
{
	static constexpr int32 WidthSegments = 10;

	struct FSection
	{
		FVector Location;
		FVector Along;
		FVector Right;
		double HalfWidth;
		float Slope;
	};

	static FSection SectionAt(const USplineComponent& Spline, double Distance, float Width)
	{
		FSection Section;
		Section.Location = Spline.GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		const FVector Tangent = Spline.GetDirectionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		const FVector Flat = FVector(Tangent.X, Tangent.Y, 0.0).GetSafeNormal();
		Section.Along = Flat.IsNearlyZero() ? FVector::ForwardVector : Flat;
		Section.Right = FVector(-Section.Along.Y, Section.Along.X, 0.0);
		Section.HalfWidth = 50.0 * Width * FMath::Max(Spline.GetScaleAtDistanceAlongSpline(Distance).Y, 0.01);
		// Drop per unit of horizontal travel; positive when the river runs downhill along the spline.
		const double Horizontal = FMath::Max(FVector2D(Tangent.X, Tangent.Y).Size(), 0.05);
		Section.Slope = static_cast<float>(FMath::Max(-Tangent.Z / Horizontal, 0.0));
		return Section;
	}
}

ATidewrightRiver::ATidewrightRiver()
{
	PrimaryActorTick.bCanEverTick = true;

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	SetRootComponent(Spline);
	Spline->ClearSplinePoints(false);
	Spline->AddSplinePoint(FVector(0.0, 0.0, 0.0), ESplineCoordinateSpace::Local, false);
	Spline->AddSplinePoint(FVector(2000.0, 400.0, -40.0), ESplineCoordinateSpace::Local, false);
	Spline->AddSplinePoint(FVector(4000.0, 0.0, -80.0), ESplineCoordinateSpace::Local, true);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RiverFinder(TEXT("/Tidewright/Materials/M_Tidewright_River.M_Tidewright_River"));
	RiverMaterial = RiverFinder.Object;
}

float ATidewrightRiver::FlowFactor(float Across, float Slope, float RapidsSlope)
{
	// Friction slows the water at the banks; a steeper bed speeds it up.
	const float Profile = 1.0f - 0.75f * FMath::Square(FMath::Clamp(Across, -1.0f, 1.0f));
	const float Steepness = 1.0f + 1.5f * FMath::Clamp(Slope / FMath::Max(RapidsSlope, 0.001f), 0.0f, 2.0f);
	return Profile * Steepness;
}

void ATidewrightRiver::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildRiver();
}

void ATidewrightRiver::BeginPlay()
{
	Super::BeginPlay();
	if (!SurfaceMesh)
	{
		RebuildRiver();
	}
}

void ATidewrightRiver::RebuildRiver()
{
	UWorld* World = GetWorld();
	if (!World || IsTemplate() || IsRunningDedicatedServer())
	{
		return;
	}

	if (!SurfaceMesh)
	{
		SurfaceMesh = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
		SurfaceMesh->SetMobility(EComponentMobility::Movable);
		SurfaceMesh->SetupAttachment(Spline);
		SurfaceMesh->SetUsingAbsoluteLocation(true);
		SurfaceMesh->SetUsingAbsoluteRotation(true);
		SurfaceMesh->SetUsingAbsoluteScale(true);
		SurfaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SurfaceMesh->SetGenerateOverlapEvents(false);
		SurfaceMesh->SetCastShadow(false);
		SurfaceMesh->SetCanEverAffectNavigation(false);
		SurfaceMesh->bAffectDistanceFieldLighting = false;
		SurfaceMesh->RegisterComponent();
		SurfaceMaterial = nullptr;
	}

	const double Length = Spline->GetSplineLength();
	const int32 Rows = FMath::Clamp(FMath::CeilToInt32(Length / (FMath::Max(SegmentLength, 0.25f) * 100.0)), 1, 20000) + 1;
	const int32 Columns = TidewrightRiver::WidthSegments + 1;
	const FVector Origin = GetActorLocation();
	SurfaceMesh->SetWorldLocationAndRotation(Origin, FQuat::Identity);

	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> Triangles;
	Vertices.Reserve(Rows * Columns);
	Triangles.Reserve((Rows - 1) * (Columns - 1) * 6);

	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const double Distance = Length * Row / (Rows - 1);
		const TidewrightRiver::FSection Section = TidewrightRiver::SectionAt(*Spline, Distance, River.Width);
		// The surface is level from bank to bank and leans downhill along the flow.
		const FVector Normal = FVector(Section.Along.X * Section.Slope, Section.Along.Y * Section.Slope, 1.0).GetSafeNormal();
		// The mesh fades out over its first and last few metres so it can run into a lake or the sea.
		const float EndFade = static_cast<float>(FMath::Clamp(FMath::Min(Distance, Length - Distance) / 300.0, 0.0, 1.0));
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			const double Across = 2.0 * Column / (Columns - 1) - 1.0;
			Vertices.Add(Section.Location + Section.Right * (Across * Section.HalfWidth) - Origin);
			Normals.Add(Normal);
			Tangents.Add(FProcMeshTangent(Section.Right, false));
			// River space, in metres: X across from the centre line, Y along the flow.
			UV0.Add(FVector2D(Across * Section.HalfWidth * 0.01, Distance * 0.01));
			Colors.Add(FLinearColor(static_cast<float>(FMath::Abs(Across)), FMath::Min(Section.Slope, 1.0f), static_cast<float>(Section.HalfWidth * 0.01 / 100.0), EndFade));
		}
	}
	for (int32 Row = 0; Row + 1 < Rows; ++Row)
	{
		for (int32 Column = 0; Column + 1 < Columns; ++Column)
		{
			const int32 A = Row * Columns + Column;
			const int32 B = A + 1;
			const int32 C = A + Columns;
			const int32 D = C + 1;
			// Wound so the face normal, (C - A) x (B - A), points up: A->B is across (+Right), A->C is along.
			Triangles.Append({ A, B, D, A, D, C });
		}
	}
	// Up = Right x Along for this basis is negative Z, so flip if the first face points down.
	if (Triangles.Num() >= 3)
	{
		const FVector FaceNormal = FVector::CrossProduct(Vertices[Triangles[2]] - Vertices[Triangles[0]], Vertices[Triangles[1]] - Vertices[Triangles[0]]);
		if (FaceNormal.Z < 0.0)
		{
			for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
			{
				Swap(Triangles[Index + 1], Triangles[Index + 2]);
			}
		}
	}

	SurfaceMesh->ClearAllMeshSections();
	SurfaceMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV0, Colors, Tangents, false);

	if (!SurfaceMaterial)
	{
		if (UMaterialInterface* Base = RiverMaterial)
		{
			SurfaceMaterial = UMaterialInstanceDynamic::Create(Base, this);
		}
	}
	if (SurfaceMaterial)
	{
		SurfaceMesh->SetMaterial(0, SurfaceMaterial);
	}
	bMaterialDirty = true;
	PushMaterialParameters();
}

void ATidewrightRiver::PushMaterialParameters()
{
	if (!SurfaceMaterial)
	{
		return;
	}
	if (!bMaterialDirty && FTidewrightRiverSettings::StaticStruct()->CompareScriptStruct(&River, &AppliedRiver, PPF_None))
	{
		return;
	}
	AppliedRiver = River;
	bMaterialDirty = false;

	SurfaceMaterial->SetVectorParameterValue(TEXT("TW_RiverA"), FLinearColor(River.FlowSpeed, FMath::Max(River.RippleSize, 0.05f), River.RippleStrength, River.Roughness));
	SurfaceMaterial->SetVectorParameterValue(TEXT("TW_RiverB"), FLinearColor(River.BankFoamWidth, FMath::Max(River.RapidsSlope, 0.001f), River.FoamAmount, 0.0f));
	SurfaceMaterial->SetVectorParameterValue(TEXT("TW_ScatterColor"), River.ScatterColor);
	SurfaceMaterial->SetVectorParameterValue(TEXT("TW_Absorption"), River.Absorption);
}

void ATidewrightRiver::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (SurfaceMesh && !FMath::IsNearlyEqual(AppliedRiver.Width, River.Width))
	{
		RebuildRiver();
	}
	PushMaterialParameters();
}

void ATidewrightRiver::SnapToGround()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
#if WITH_EDITOR
	Modify();
	Spline->Modify();
#endif
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TidewrightRiverSnap), true);
	Query.AddIgnoredActor(this);
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	for (int32 Index = 0; Index < Spline->GetNumberOfSplinePoints(); ++Index)
	{
		FVector Point = Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World);
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, Point + FVector(0.0, 0.0, 100000.0), Point - FVector(0.0, 0.0, 100000.0), Objects, Query))
		{
			Point.Z = Hit.ImpactPoint.Z + WaterDepth * 100.0;
			Spline->SetLocationAtSplinePoint(Index, Point, ESplineCoordinateSpace::World, false);
		}
	}
	// Water cannot run uphill: no point may sit higher than the one before it.
	for (int32 Index = 1; Index < Spline->GetNumberOfSplinePoints(); ++Index)
	{
		const FVector Previous = Spline->GetLocationAtSplinePoint(Index - 1, ESplineCoordinateSpace::World);
		FVector Point = Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World);
		if (Point.Z > Previous.Z)
		{
			Point.Z = Previous.Z;
			Spline->SetLocationAtSplinePoint(Index, Point, ESplineCoordinateSpace::World, false);
		}
	}
	Spline->UpdateSpline();
	RebuildRiver();
#if WITH_EDITOR
	MarkPackageDirty();
#endif
}

bool ATidewrightRiver::SampleRiver(const FVector& WorldLocation, FVector& OutSurfaceLocation, FVector& OutNormal, FVector& OutVelocity) const
{
	OutSurfaceLocation = WorldLocation;
	OutNormal = FVector::UpVector;
	OutVelocity = FVector::ZeroVector;

	const float Key = Spline->FindInputKeyClosestToWorldLocation(WorldLocation);
	const double Distance = Spline->GetDistanceAlongSplineAtSplineInputKey(Key);
	const TidewrightRiver::FSection Section = TidewrightRiver::SectionAt(*Spline, Distance, River.Width);

	const FVector Offset = WorldLocation - Section.Location;
	const double Across = FVector::DotProduct(Offset, Section.Right);
	const double Along = FVector::DotProduct(Offset, Section.Along);
	// Past either end of the spline the closest point is the end itself; the river does not continue there.
	if (FMath::Abs(Across) > Section.HalfWidth || FMath::Abs(Along) > FMath::Max(SegmentLength * 100.0, 100.0))
	{
		return false;
	}

	OutSurfaceLocation = FVector(WorldLocation.X, WorldLocation.Y, Section.Location.Z - Along * Section.Slope);
	OutNormal = FVector(Section.Along.X * Section.Slope, Section.Along.Y * Section.Slope, 1.0).GetSafeNormal();
	const float Factor = FlowFactor(static_cast<float>(Across / Section.HalfWidth), Section.Slope, River.RapidsSlope);
	OutVelocity = (Section.Along - FVector(0.0, 0.0, Section.Slope)) * (River.FlowSpeed * 100.0 * Factor);
	return true;
}

bool ATidewrightRiver::SampleAnyRiver(const UWorld* World, const FVector& WorldLocation, FVector& OutSurfaceLocation, FVector& OutNormal, FVector& OutVelocity)
{
	if (World)
	{
		for (TActorIterator<ATidewrightRiver> It(World); It; ++It)
		{
			if (It->SampleRiver(WorldLocation, OutSurfaceLocation, OutNormal, OutVelocity))
			{
				return true;
			}
		}
	}
	return false;
}
