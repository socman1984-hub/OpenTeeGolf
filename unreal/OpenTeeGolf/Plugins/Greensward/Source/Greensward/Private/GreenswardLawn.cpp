// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "GreenswardLawn.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogGreensward, Log, All);

namespace GreenswardLawnPrivate
{
	static bool PointInPolygon(const FVector2D& P, const TArray<FVector2D>& Poly)
	{
		bool bInside = false;
		const int32 Num = Poly.Num();
		for (int32 I = 0, J = Num - 1; I < Num; J = I++)
		{
			const FVector2D& A = Poly[I];
			const FVector2D& B = Poly[J];
			if (((A.Y > P.Y) != (B.Y > P.Y)) &&
				(P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X))
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}
}

AGreenswardLawn::AGreenswardLawn()
{
	PrimaryActorTick.bCanEverTick = false;

	Boundary = CreateDefaultSubobject<USplineComponent>(TEXT("Boundary"));
	SetRootComponent(Boundary);
	Boundary->SetClosedLoop(true);
	Boundary->SetMobility(EComponentMobility::Static);

	// Default outline: a 10 m square.
	Boundary->ClearSplinePoints(false);
	const float H = 500.0f;
	Boundary->AddSplinePoint(FVector(-H, -H, 0), ESplineCoordinateSpace::Local, false);
	Boundary->AddSplinePoint(FVector(H, -H, 0), ESplineCoordinateSpace::Local, false);
	Boundary->AddSplinePoint(FVector(H, H, 0), ESplineCoordinateSpace::Local, false);
	Boundary->AddSplinePoint(FVector(-H, H, 0), ESplineCoordinateSpace::Local, false);
	for (int32 I = 0; I < 4; ++I)
	{
		Boundary->SetSplinePointType(I, ESplinePointType::Linear, false);
	}
	Boundary->UpdateSpline();
}

void AGreenswardLawn::SetRectangle(float SizeX, float SizeY)
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

void AGreenswardLawn::GetOutline(TArray<FVector2D>& OutLocalPoints) const
{
	OutLocalPoints.Reset();
	if (!Boundary || Boundary->GetNumberOfSplinePoints() < 3)
	{
		return;
	}

	const float Length = Boundary->GetSplineLength();
	const int32 Samples = FMath::Clamp(FMath::CeilToInt32(Length / 25.0f), 16, 4096);
	OutLocalPoints.Reserve(Samples);
	for (int32 I = 0; I < Samples; ++I)
	{
		const FVector P = Boundary->GetLocationAtDistanceAlongSpline(Length * I / Samples, ESplineCoordinateSpace::Local);
		OutLocalPoints.Add(FVector2D(P.X, P.Y));
	}
}

bool AGreenswardLawn::IsPointInside(const FVector& WorldPoint) const
{
	TArray<FVector2D> Outline;
	GetOutline(Outline);
	if (Outline.Num() < 3)
	{
		return false;
	}
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldPoint);
	return GreenswardLawnPrivate::PointInPolygon(FVector2D(Local.X, Local.Y), Outline);
}

int32 AGreenswardLawn::GetInstanceCount() const
{
	int32 Count = 0;
	for (const UInstancedStaticMeshComponent* Comp : MatComponents)
	{
		if (Comp)
		{
			Count += Comp->GetInstanceCount();
		}
	}
	return Count;
}

void AGreenswardLawn::Clear()
{
	for (UInstancedStaticMeshComponent* Comp : MatComponents)
	{
		if (Comp)
		{
			RemoveInstanceComponent(Comp);
			Comp->DestroyComponent();
		}
	}
	MatComponents.Reset();
}

void AGreenswardLawn::ApplyAppearance()
{
	for (UInstancedStaticMeshComponent* Comp : MatComponents)
	{
		if (!Comp)
		{
			continue;
		}
		if (bOverrideAppearance)
		{
			Comp->SetDefaultCustomPrimitiveDataFloat(0, static_cast<float>(static_cast<uint8>(StripeMode)));
			Comp->SetDefaultCustomPrimitiveDataFloat(1, StripeWidth);
			Comp->SetDefaultCustomPrimitiveDataFloat(2, StripeAngle);
			Comp->SetDefaultCustomPrimitiveDataFloat(3, StripeContrast);
			// Stored as an offset from white, so mats with no custom data (all zeros) keep their natural colour.
			Comp->SetDefaultCustomPrimitiveDataVector4(4, FVector4(Tint.R - 1.0f, Tint.G - 1.0f, Tint.B - 1.0f, 0.0f));
		}
		Comp->SetCastShadow(bCastShadow);
		Comp->SetCullDistances(0, FMath::RoundToInt32(CullDistance));
		Comp->SetWorldPositionOffsetDisableDistance(FMath::RoundToInt32(WindDisableDistance));
	}
}

void AGreenswardLawn::Rebuild()
{
	using namespace GreenswardLawnPrivate;

	Clear();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Usable mats and their cumulative weights.
	TArray<UStaticMesh*> Meshes;
	TArray<float> Cumulative;
	float TotalWeight = 0.0f;
	for (const FGreenswardMatEntry& Entry : Mats)
	{
		if (Entry.Mesh && Entry.Weight > 0.0f)
		{
			TotalWeight += Entry.Weight;
			Meshes.Add(Entry.Mesh);
			Cumulative.Add(TotalWeight);
		}
	}
	if (Meshes.IsEmpty())
	{
		return;
	}

	TArray<FVector2D> Outline;
	GetOutline(Outline);
	if (Outline.Num() < 3)
	{
		return;
	}

	const FTransform ActorTM = GetActorTransform();

	// Cut-outs, expressed in this lawn's local space.
	TArray<TArray<FVector2D>> CutOuts;
	for (const AGreenswardLawn* Other : CutOutLawns)
	{
		if (!Other || Other == this)
		{
			continue;
		}
		TArray<FVector2D> OtherLocal;
		Other->GetOutline(OtherLocal);
		if (OtherLocal.Num() < 3)
		{
			continue;
		}
		const FTransform OtherTM = Other->GetActorTransform();
		TArray<FVector2D>& Converted = CutOuts.AddDefaulted_GetRef();
		Converted.Reserve(OtherLocal.Num());
		for (const FVector2D& P : OtherLocal)
		{
			const FVector L = ActorTM.InverseTransformPosition(OtherTM.TransformPosition(FVector(P.X, P.Y, 0.0)));
			Converted.Add(FVector2D(L.X, L.Y));
		}
	}

	FBox2D Bounds(ForceInit);
	for (const FVector2D& P : Outline)
	{
		Bounds += P;
	}

	const float Cell = FMath::Max(MatSize, 5.0f);
	const int32 MinX = FMath::FloorToInt32(Bounds.Min.X / Cell);
	const int32 MaxX = FMath::CeilToInt32(Bounds.Max.X / Cell);
	const int32 MinY = FMath::FloorToInt32(Bounds.Min.Y / Cell);
	const int32 MaxY = FMath::CeilToInt32(Bounds.Max.Y / Cell);

	TArray<TArray<FTransform>> Transforms;
	Transforms.SetNum(Meshes.Num());

	FRandomStream Rng(Seed);
	const FVector Up = ActorTM.GetUnitAxis(EAxis::Z);
	const FVector Forward = ActorTM.GetUnitAxis(EAxis::X);
	const FVector Right = ActorTM.GetUnitAxis(EAxis::Y);

	FCollisionQueryParams Query(SCENE_QUERY_STAT(GreenswardLawn), true, this);
	for (const AGreenswardLawn* Other : CutOutLawns)
	{
		if (Other)
		{
			Query.AddIgnoredActor(Other);
		}
	}

	auto IsInside = [&](const FVector2D& P)
	{
		if (!PointInPolygon(P, Outline))
		{
			return false;
		}
		for (const TArray<FVector2D>& CutOut : CutOuts)
		{
			if (PointInPolygon(P, CutOut))
			{
				return false;
			}
		}
		return true;
	};

	int32 Placed = 0;
	bool bHitLimit = false;

	// Places one mat, scaled to cover a square of the given size centred on Centre.
	auto PlaceMat = [&](const FVector2D& Centre, float Size, int32 Quarter, float Pick, float Jitter, float HeightScale, float YawDegrees = 0.0f, float Spread = 1.0f)
	{
		FVector Position = ActorTM.TransformPosition(FVector(Centre.X, Centre.Y, 0.0));
		FVector Normal = Up;

		if (bSnapToGround)
		{
			FHitResult Hit;
			const FVector Start = Position + Up * TraceDistance;
			const FVector End = Position - Up * TraceDistance;
			if (World->LineTraceSingleByChannel(Hit, Start, End, TraceChannel, Query))
			{
				Position = Hit.ImpactPoint;
				Normal = Hit.ImpactNormal;
			}
			else if (bRequireGround)
			{
				return;
			}
		}

		FVector XAxis = Forward;
		if (bRandomQuarterTurns)
		{
			switch (Quarter)
			{
			case 1: XAxis = Right; break;
			case 2: XAxis = -Forward; break;
			case 3: XAxis = -Right; break;
			default: break;
			}
		}

		if (YawDegrees != 0.0f)
		{
			XAxis = XAxis.RotateAngleAxis(YawDegrees, Up);
		}
		const FQuat Rotation = FRotationMatrix::MakeFromZX(Normal, XAxis).ToQuat();
		FVector FinalScale = ActorTM.GetScale3D();
		FinalScale.X *= Size / Cell * Spread;
		FinalScale.Y *= Size / Cell * Spread;
		FinalScale.Z *= (1.0f + Jitter * HeightJitter) * HeightScale;

		int32 MeshIndex = 0;
		while (MeshIndex < Cumulative.Num() - 1 && Pick > Cumulative[MeshIndex])
		{
			++MeshIndex;
		}

		Transforms[MeshIndex].Emplace(Rotation, Position + Normal * SurfaceOffset, FinalScale);
		if (++Placed >= MaxInstances)
		{
			bHitLimit = true;
		}
	};

	// Cells cut by the outline are split into quarters, and those again, so the edge follows the spline closely.
	const int32 MaxLevel = FMath::Clamp(EdgeSubdivisions, 0, 3);
	TFunction<void(const FVector2D&, float, int32, int32, float, float)> FillCell;
	FillCell = [&](const FVector2D& Centre, float Size, int32 Level, int32 Quarter, float Pick, float Jitter)
	{
		if (bHitLimit)
		{
			return;
		}
		const float H = Size * 0.5f;
		// Test just inside the corners so an outline that runs along the cell grid counts as fully covered.
		const float C = H * 0.96f;
		const bool bCentre = IsInside(Centre);
		int32 Corners = 0;
		Corners += IsInside(Centre + FVector2D(-C, -C)) ? 1 : 0;
		Corners += IsInside(Centre + FVector2D(C, -C)) ? 1 : 0;
		Corners += IsInside(Centre + FVector2D(C, C)) ? 1 : 0;
		Corners += IsInside(Centre + FVector2D(-C, C)) ? 1 : 0;

		if (bCentre && Corners == 4)
		{
			PlaceMat(Centre, Size, Quarter, Pick, Jitter, 1.0f);
			return;
		}
		if (!bCentre && Corners == 0)
		{
			return;
		}
		if (Level >= MaxLevel)
		{
			// On the outline itself: scatter and shorten the last mats so the edge is ragged, not a staircase.
			const uint32 Hash = HashCombine(GetTypeHash(FMath::RoundToInt32(Centre.X * 8.0)), GetTypeHash(FMath::RoundToInt32(Centre.Y * 8.0)));
			const float R1 = (Hash & 0xFFFF) / 65535.0f;
			const float R2 = ((Hash >> 16) & 0xFFFF) / 65535.0f;
			if (EdgeRaggedness <= 0.0f)
			{
				if (bCentre)
				{
					PlaceMat(Centre, Size, Quarter, Pick, Jitter, 1.0f);
				}
				return;
			}
			const FVector2D Scatter = FVector2D(R1 - 0.5f, R2 - 0.5f) * Size * 2.0f * EdgeRaggedness;
			if (bCentre || IsInside(Centre + Scatter))
			{
				// Turned and nudged off the grid, slightly oversized so the tufts overlap, and cut shorter.
				const FVector2D Nudge = Scatter * 0.35f;
				const float Height = FMath::Lerp(1.0f, FMath::Lerp(0.5f, 1.0f, R2), EdgeRaggedness);
				PlaceMat(Centre + Nudge, Size, Quarter, Pick, Jitter, Height, 5.0f + R1 * 80.0f, 1.0f + 0.9f * EdgeRaggedness);
			}
			return;
		}

		const float Q = Size * 0.25f;
		const FVector2D Offsets[4] = { FVector2D(-Q, -Q), FVector2D(Q, -Q), FVector2D(Q, Q), FVector2D(-Q, Q) };
		for (int32 I = 0; I < 4; ++I)
		{
			// Vary the quarters without touching the main random stream.
			FillCell(Centre + Offsets[I], H, Level + 1, (Quarter + I * 3 + Level) & 3, FMath::Fmod(Pick + TotalWeight * 0.37f * (I + 1), TotalWeight), Jitter);
		}
	};

	for (int32 Y = MinY; Y < MaxY && !bHitLimit; ++Y)
	{
		for (int32 X = MinX; X < MaxX && !bHitLimit; ++X)
		{
			// Draw the random numbers for every cell so editing the outline does not reshuffle the lawn.
			const int32 Quarter = Rng.RandRange(0, 3);
			const float Pick = Rng.FRand() * TotalWeight;
			const float Jitter = Rng.FRandRange(-1.0f, 1.0f);

			FillCell(FVector2D((X + 0.5f) * Cell, (Y + 0.5f) * Cell), Cell, 0, Quarter, Pick, Jitter);
		}
	}

	if (bHitLimit)
	{
		UE_LOG(LogGreensward, Warning, TEXT("%s: reached MaxInstances (%d). The lawn is only partly filled."), *GetName(), MaxInstances);
	}

	for (int32 I = 0; I < Meshes.Num(); ++I)
	{
		if (Transforms[I].IsEmpty())
		{
			continue;
		}

		UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transactional);
		Comp->SetMobility(EComponentMobility::Static);
		Comp->SetStaticMesh(Meshes[I]);
		if (OverrideMaterial)
		{
			const int32 NumMaterials = Meshes[I]->GetStaticMaterials().Num();
			for (int32 M = 0; M < NumMaterials; ++M)
			{
				Comp->SetMaterial(M, OverrideMaterial);
			}
		}
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCanEverAffectNavigation(false);
		Comp->bAffectDistanceFieldLighting = false;
		Comp->bReceivesDecals = true;
		Comp->SetupAttachment(Boundary);
		AddInstanceComponent(Comp);
		Comp->RegisterComponent();
		Comp->AddInstances(Transforms[I], false, true, false);
		MatComponents.Add(Comp);
	}

	ApplyAppearance();
}

void AGreenswardLawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

#if WITH_EDITOR
	const UWorld* World = GetWorld();
	if (bAutoRebuild && World && !World->IsGameWorld() && !IsTemplate())
	{
		Rebuild();
	}
#endif
}
