// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "TidewrightBuoyancyComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TidewrightRiver.h"
#include "TidewrightWater.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TidewrightBuoyancyComponent)

UTidewrightBuoyancyComponent::UTidewrightBuoyancyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UTidewrightBuoyancyComponent::BeginPlay()
{
	Super::BeginPlay();

	ActivePoints = FloatPoints;
	const AActor* Owner = GetOwner();
	const UPrimitiveComponent* Body = Owner ? Cast<UPrimitiveComponent>(Owner->GetRootComponent()) : nullptr;
	if (ActivePoints.Num() == 0 && Body)
	{
		// One float at each lower corner of the local bounds, pulled in slightly, plus one in the middle.
		const FBoxSphereBounds Local = Body->CalcBounds(FTransform::Identity);
		const FVector Extent = Local.BoxExtent * 0.8;
		const double Bottom = Local.Origin.Z - Local.BoxExtent.Z * 0.5;
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			ActivePoints.Add(FVector(
				Local.Origin.X + ((Corner & 1) ? Extent.X : -Extent.X),
				Local.Origin.Y + ((Corner & 2) ? Extent.Y : -Extent.Y),
				Bottom));
		}
		ActivePoints.Add(FVector(Local.Origin.X, Local.Origin.Y, Bottom));
	}
	if (ActivePoints.Num() == 0)
	{
		ActivePoints.Add(FVector::ZeroVector);
	}

	if (Water)
	{
		AddTickPrerequisiteActor(Water);
	}
}

ATidewrightWater* UTidewrightBuoyancyComponent::ResolveWater()
{
	if (Water)
	{
		return Water;
	}
	if (!CachedWater)
	{
		CachedWater = ATidewrightWater::GetTidewrightWater(this);
	}
	return CachedWater;
}

void UTidewrightBuoyancyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ATidewrightWater* WaterActor = ResolveWater();
	if (!GetOwner())
	{
		SubmergedFraction = 0.0f;
		return;
	}

	if (Mode == ETidewrightBuoyancyMode::Physics)
	{
		TickPhysics(WaterActor, DeltaTime);
	}
	else
	{
		TickRide(WaterActor, DeltaTime);
	}
}

bool UTidewrightBuoyancyComponent::SampleWater(ATidewrightWater* WaterActor, const FVector& Location, FVector& OutSurface, FVector& OutNormal, FVector& OutVelocity) const
{
	// A river lies above the sea wherever the two overlap, so it is asked first.
	if (ATidewrightRiver::SampleAnyRiver(GetWorld(), Location, OutSurface, OutNormal, OutVelocity))
	{
		return true;
	}
	OutVelocity = FVector::ZeroVector;
	return WaterActor && WaterActor->SampleWaterSurface(Location, OutSurface, OutNormal);
}

void UTidewrightBuoyancyComponent::TickPhysics(ATidewrightWater* WaterActor, float DeltaTime)
{
	UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	if (!Body || !Body->IsSimulatingPhysics())
	{
		SubmergedFraction = 0.0f;
		return;
	}

	const float Mass = Body->GetMass();
	const float GravityStrength = FMath::Abs(GetWorld()->GetGravityZ());
	const float LiftPerPoint = Mass * GravityStrength * Buoyancy / ActivePoints.Num();
	const float DragPerPoint = Mass * WaterDrag / ActivePoints.Num();
	const FTransform& Transform = Body->GetComponentTransform();

	float Submerged = 0.0f;
	for (const FVector& LocalPoint : ActivePoints)
	{
		const FVector Point = Transform.TransformPosition(LocalPoint);
		FVector Surface, Normal, Current;
		if (!SampleWater(WaterActor, Point, Surface, Normal, Current))
		{
			continue;
		}

		const float Depth = static_cast<float>(Surface.Z - Point.Z);
		const float Fraction = FMath::Clamp((Depth + FloatRadius) / (2.0f * FloatRadius), 0.0f, 1.0f);
		if (Fraction <= 0.0f)
		{
			continue;
		}
		Submerged += Fraction;

		// Drag acts on movement relative to the water, so a current carries the body with it.
		const FVector Velocity = Body->GetPhysicsLinearVelocityAtPoint(Point) - Current;
		const FVector Force = FVector(0.0, 0.0, LiftPerPoint * Fraction) - Velocity * (DragPerPoint * Fraction);
		Body->AddForceAtLocation(Force, Point);
	}
	SubmergedFraction = Submerged / ActivePoints.Num();
}

void UTidewrightBuoyancyComponent::TickRide(ATidewrightWater* WaterActor, float DeltaTime)
{
	AActor* Owner = GetOwner();
	const FVector Location = Owner->GetActorLocation();
	FVector Surface, Normal, Flow;
	if (!SampleWater(WaterActor, Location, Surface, Normal, Flow))
	{
		SubmergedFraction = 0.0f;
		return;
	}
	SubmergedFraction = 1.0f;

	const float Alpha = 1.0f - FMath::Exp(-RideResponse * DeltaTime);
	FVector Target = Location + FVector(Flow.X, Flow.Y, 0.0) * DeltaTime;
	Target.Z = FMath::Lerp(Location.Z, Surface.Z + RideHeight, Alpha);

	const FVector Up = FMath::Lerp(FVector::UpVector, Normal, RideTilt).GetSafeNormal();
	const FQuat Current = Owner->GetActorQuat();
	const FQuat Desired = FRotationMatrix::MakeFromZX(Up, Current.GetForwardVector()).ToQuat();
	Owner->SetActorLocationAndRotation(Target, FQuat::Slerp(Current, Desired, Alpha));
}
