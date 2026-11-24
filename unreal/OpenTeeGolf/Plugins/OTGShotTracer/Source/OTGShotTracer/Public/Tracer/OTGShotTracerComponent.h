// A golf ball's flight drawn as a ribbon (the round's tracer): the path so far, thinned to about a
// point a metre, drawn 2 cm above it as a strip facing the camera - wider the farther it is from the
// camera, so it reads from the tee - in the material given. Both faces drawn.
//
// Use: add it to an actor, set RibbonMaterial, call Draw with the path's points (the level's cm, in
// order, up to where the ball is), the ball itself and the camera's position as the ball flies;
// Clear when the next shot starts.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "OTGShotTracerComponent.generated.h"

UCLASS(ClassGroup = (OpenTeeGolf), meta = (BlueprintSpawnableComponent))
class OTGSHOTTRACER_API UOTGShotTracerComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	// (the ribbon's material: set before drawing)
	UPROPERTY() TObjectPtr<UMaterialInterface> RibbonMaterial;

	// The path so far (points in order) and the ball where it is now, seen from CameraPos
	void Draw(const TArray<FVector>& Path, const FVector& Head, const FVector& CameraPos);
	void Clear();

private:
	TArray<FVector> Points;
};
