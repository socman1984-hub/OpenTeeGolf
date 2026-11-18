// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#include "GreenswardPitchMarking.h"

#include "Components/DecalComponent.h"
#include "Materials/MaterialInterface.h"

FVector2D GreenswardMarkings::GetExtentMetres(EGreenswardMarking Marking)
{
	switch (Marking)
	{
	case EGreenswardMarking::SoccerFull: return FVector2D(107.0, 70.0);
	case EGreenswardMarking::SoccerSmall: return FVector2D(42.0, 22.0);
	case EGreenswardMarking::AmericanFootball: return FVector2D(111.73, 50.77);
	case EGreenswardMarking::RugbyUnion: return FVector2D(122.0, 72.0);
	case EGreenswardMarking::RugbyLeague: return FVector2D(118.0, 70.0);
	case EGreenswardMarking::TennisSingle: return FVector2D(25.77, 12.97);
	case EGreenswardMarking::TennisTriple: return FVector2D(25.77, 46.97);
	case EGreenswardMarking::Hockey: return FVector2D(93.4, 57.0);
	default: return FVector2D(100.0, 60.0);
	}
}

FString GreenswardMarkings::GetAssetSuffix(EGreenswardMarking Marking)
{
	switch (Marking)
	{
	case EGreenswardMarking::SoccerFull: return TEXT("SoccerFull");
	case EGreenswardMarking::SoccerSmall: return TEXT("SoccerSmall");
	case EGreenswardMarking::AmericanFootball: return TEXT("AmericanFootball");
	case EGreenswardMarking::RugbyUnion: return TEXT("RugbyUnion");
	case EGreenswardMarking::RugbyLeague: return TEXT("RugbyLeague");
	case EGreenswardMarking::TennisSingle: return TEXT("TennisSingle");
	case EGreenswardMarking::TennisTriple: return TEXT("TennisTriple");
	case EGreenswardMarking::Hockey: return TEXT("Hockey");
	default: return TEXT("SoccerFull");
	}
}

AGreenswardPitchMarking::AGreenswardPitchMarking()
{
	if (UDecalComponent* DecalComponent = GetDecal())
	{
		// Markings are large and should stay visible from far away.
		DecalComponent->FadeScreenSize = 0.0f;
	}
}

void AGreenswardPitchMarking::Refresh()
{
	UDecalComponent* DecalComponent = GetDecal();
	if (!DecalComponent)
	{
		return;
	}

	UMaterialInterface* Material = MaterialOverride;
	if (!Material)
	{
		const FString Suffix = GreenswardMarkings::GetAssetSuffix(Marking);
		const FString Path = FString::Printf(TEXT("/Greensward/Markings/MI_GS_Marking_%s.MI_GS_Marking_%s"), *Suffix, *Suffix);
		Material = LoadObject<UMaterialInterface>(nullptr, *Path);
	}
	if (Material)
	{
		DecalComponent->SetDecalMaterial(Material);
	}

	// The decal projects along its X axis. Facing down, its Z axis lies along world X (pitch length) and Y along world Y (width).
	const FVector2D Extent = GreenswardMarkings::GetExtentMetres(Marking) * 100.0 * FMath::Max(Scale, 0.05f);
	DecalComponent->DecalSize = FVector(ProjectionDepth, Extent.Y * 0.5, Extent.X * 0.5);
	DecalComponent->MarkRenderStateDirty();
}

void AGreenswardPitchMarking::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Refresh();
}
