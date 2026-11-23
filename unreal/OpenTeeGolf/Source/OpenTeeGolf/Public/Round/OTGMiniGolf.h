// The putt-putt: a nine-hole mini golf course the game makes itself (no course folder), after the
// web version's practice.js putt-putt and three holes more. Felt lanes edged with boards the ball
// bounces off, posts, a corner block, a ramp, a windmill whose sails sweep across its tunnel, a
// stone tunnel and a volcano, a zig-zag, camel humps, a pond with a narrow bridge and a banked
// U-turn, in a fenced lawn ringed with trees.
//
//  - MakeCourse: the play data (UOTGCourseData, Generated "minigolf"): the holes, a fine
//    heightmap (5 cm) and surfaces (2.5 cm: felt is green, the pond is water, the lawn is tee -
//    anywhere off the felt is out), the boards, posts and blocks as solids of kind "bumper"
//  - AOTGMiniGolf: draws it in L_Course (procedural meshes and the engine's basic shapes, coloured
//    with its basic material), turns the windmill and tells the ball where its sails are
//    (AddMovers: otg::ShotOptions::movers)
//
// AOTGRound plays it as any course, every shot a putt (OTGRound.cpp: bMini). Open it with
// -minigolf (or -course=minigolf) on the command line, OTG.MiniGolf in the console, or the menu's
// Putt-putt button.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OTGMiniGolf.generated.h"

class UOTGCourseData;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;
namespace otg { struct ShotOptions; }

UCLASS()
class OPENTEEGOLF_API AOTGMiniGolf : public AActor
{
	GENERATED_BODY()

public:
	AOTGMiniGolf();

	static const TCHAR* CourseId;   // "minigolf"
	// The putt-putt's play data (made once a game: kept by Outer)
	static UOTGCourseData* MakeCourse(UObject* Outer);
	static AOTGMiniGolf* In(UWorld* W);

	// Draw the course (its data from MakeCourse)
	void Build(UOTGCourseData* D);
	virtual void Tick(float Dt) override;

	// The windmill's sails, for a shot struck now
	void AddMovers(otg::ShotOptions& O) const;

private:
	FVector W(double X, double Y, double Z) const;   // course metres to the level's cm
	UMaterialInstanceDynamic* Mat(const FLinearColor& C);
	UInstancedStaticMeshComponent* Ism(UStaticMesh* Mesh, const FLinearColor& C, bool bShadow = true);
	void Box(const FVector& CentreW, const FVector& SizeCm, const FRotator& Rot, const FLinearColor& C, USceneComponent* Parent = nullptr);
	UStaticMeshComponent* Shape(UStaticMesh* Mesh, const FVector& CentreW, const FVector& SizeCm, const FRotator& Rot, const FLinearColor& C, USceneComponent* Parent = nullptr);
	UProceduralMeshComponent* NewProc();
	void Sign(double X, double Z, double FaceX, double FaceZ, const TArray<FString>& Lines, double Wm, double Hm, double Lift, const FLinearColor& Stripe);

	void BuildLawn();
	void BuildLanes();
	void BuildBoards();
	void BuildFeatures();
	void BuildWindmill(int32 Hole);
	void BuildTunnel(int32 Hole);
	void BuildSurroundings();

	UPROPERTY() TObjectPtr<UOTGCourseData> Data;
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Mats;
	TMap<uint32, int32> MatIndex;
	UPROPERTY() TMap<FString, TObjectPtr<UInstancedStaticMeshComponent>> Isms;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Sails;   // (each windmill's sails, turned about its hub)
	double Clock = 0;   // (seconds since the course was built: the sails turn by it)
};
