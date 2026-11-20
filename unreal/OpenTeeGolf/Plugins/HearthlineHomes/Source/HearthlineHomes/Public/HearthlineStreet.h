// Copyright __PUBLISHER__. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HearthlineTypes.h"
#include "HearthlineStreet.generated.h"

class UDynamicMeshComponent;

/** A straight residential street running along the actor's X axis: carriageway, curbs, grass verges and sidewalks. */
UCLASS(Blueprintable, meta = (DisplayName = "Hearthline Street"))
class HEARTHLINEHOMES_API AHearthlineStreet : public AActor
{
	GENERATED_BODY()

public:
	AHearthlineStreet();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Street", meta = (ClampMin = "200"))
	float Length = 10000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Street", meta = (ClampMin = "200"))
	float RoadWidth = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Street", meta = (ClampMin = "0"))
	float CurbHeight = 13.f;

	/** Grass strip between curb and sidewalk, cm. 0 removes it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Street", meta = (ClampMin = "0"))
	float VergeWidth = 130.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Street", meta = (ClampMin = "0"))
	float SidewalkWidth = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Street")
	bool bCentreLine = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Street")
	FLinearColor LineColor = FLinearColor(0.75f, 0.55f, 0.08f);

	/** Distance between street trees in the verges, cm. 0 plants none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Street", meta = (ClampMin = "0"))
	float TreeSpacing = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Street")
	int32 TreeSeed = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Street")
	FLinearColor FoliageColor = FLinearColor(0.85f, 0.9f, 0.75f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Materials")
	FHearthlineMaterialSet Materials;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Hearthline|Street")
	void Rebuild();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hearthline")
	TObjectPtr<UDynamicMeshComponent> MeshComponent;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

private:
	bool bBuiltOnce = false;
};
