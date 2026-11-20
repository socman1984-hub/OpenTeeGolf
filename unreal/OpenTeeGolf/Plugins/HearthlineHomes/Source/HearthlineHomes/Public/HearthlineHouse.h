// Copyright __PUBLISHER__. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HearthlineTypes.h"
#include "HearthlineHouse.generated.h"

class UDynamicMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class FHearthlineMeshBuilder;

/**
 * A procedural house exterior. Change any property and the shell is rebuilt.
 * Use Randomize for a new house in the chosen style, and Bake To Static Mesh to turn the result into a
 * regular static mesh asset (Nanite, distance fields, Lumen) once you are happy with it.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Hearthline House"))
class HEARTHLINEHOMES_API AHearthlineHouse : public AActor
{
	GENERATED_BODY()

public:
	AHearthlineHouse();

	// ---- Style ----------------------------------------------------------------------------------------------

	/** Style used by Randomize. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Style")
	EHearthlineStyle Style = EHearthlineStyle::Ranch;

	/** Seed used by Randomize and for small per-window variation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Style")
	int32 Seed = 1;

	/** Replaces the whole configuration with a new house in the current style, from the current seed. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Hearthline|Style")
	void ApplyStyle();

	/** Picks a new seed, then applies the style. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Hearthline|Style")
	void Randomize();

	/** Keeps the shape, picks new colours and finishes. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Hearthline|Style")
	void RandomizeColors();

	// ---- Shape ----------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Shape", meta = (TitleProperty = "Roof"))
	TArray<FHearthlineVolume> Volumes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Shape")
	TArray<FHearthlinePorch> Porches;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Shape")
	FHearthlineChimney Chimney;

	/** Colour of porch decks and wooden steps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Shape")
	FLinearColor DeckColor = FLinearColor(0.62f, 0.6f, 0.57f);

	/** Lantern beside the front door and garage doors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Shape")
	bool bWallLights = true;

	// ---- Walls ----------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Walls")
	EHearthlineCladding Cladding = EHearthlineCladding::LapSiding;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Walls")
	FLinearColor WallColor = FLinearColor(0.56f, 0.6f, 0.55f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Walls")
	EHearthlineAccent Accent = EHearthlineAccent::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Walls", meta = (EditCondition = "Accent != EHearthlineAccent::None"))
	EHearthlineCladding AccentCladding = EHearthlineCladding::BoardAndBatten;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Walls", meta = (EditCondition = "Accent != EHearthlineAccent::None"))
	FLinearColor AccentColor = FLinearColor(0.85f, 0.85f, 0.82f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Walls")
	FLinearColor TrimColor = FLinearColor(0.9f, 0.9f, 0.88f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Walls")
	bool bCornerBoards = true;

	/** Horizontal trim board where storeys meet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Walls")
	bool bBandBoards = false;

	/** Louvred vent in gable ends that are tall enough. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Walls")
	bool bGableVents = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Foundation", meta = (ClampMin = "0", ClampMax = "300"))
	float FoundationHeight = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Foundation")
	EHearthlineCladding FoundationCladding = EHearthlineCladding::Concrete;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Foundation")
	FLinearColor FoundationColor = FLinearColor(0.75f, 0.75f, 0.75f);

	// ---- Roof -----------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Roof")
	FLinearColor RoofColor = FLinearColor(0.07f, 0.065f, 0.06f);

	/** Height of the fascia board at the roof edge, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Roof", meta = (ClampMin = "4", ClampMax = "60"))
	float FasciaDepth = 16.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Roof")
	bool bGutters = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Roof")
	FLinearColor GutterColor = FLinearColor(0.85f, 0.85f, 0.85f);

	/** Height of the wall above a flat roof, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Roof", meta = (ClampMin = "0", ClampMax = "200"))
	float ParapetHeight = 45.f;

	// ---- Windows --------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Windows", meta = (ClampMin = "30", ClampMax = "600"))
	float WindowWidth = 95.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Windows", meta = (ClampMin = "30", ClampMax = "300"))
	float WindowHeight = 145.f;

	/** Height of the sill above the storey floor, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Windows", meta = (ClampMin = "0", ClampMax = "250"))
	float SillHeight = 85.f;

	/** Target distance between window centres for automatic placement, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Windows", meta = (ClampMin = "80", ClampMax = "1500"))
	float WindowSpacing = 300.f;

	/** Vertical glazing bars per sash (panes across = this + 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Windows", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MuntinColumns = 1;

	/** Horizontal glazing bars per sash. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Windows", meta = (ClampMin = "0", ClampMax = "8"))
	int32 MuntinRows = 1;

	/** Splits the window into an upper and lower sash. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Windows")
	bool bDoubleHung = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Windows")
	bool bShutters = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Windows", meta = (EditCondition = "bShutters"))
	FLinearColor ShutterColor = FLinearColor(0.05f, 0.07f, 0.09f);

	/** Width of the trim around windows and doors, cm. 0 removes it (modern look). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Windows", meta = (ClampMin = "0", ClampMax = "30"))
	float CasingWidth = 10.f;

	/** Colour of sashes and glazing bars. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Windows")
	FLinearColor SashColor = FLinearColor(0.9f, 0.9f, 0.88f);

	/** Share of windows that glow as if a lamp is on inside. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Windows", meta = (ClampMin = "0", ClampMax = "1"))
	float LitWindowFraction = 0.f;

	// ---- Doors ----------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Front Door")
	bool bFrontDoor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Front Door", meta = (EditCondition = "bFrontDoor"))
	int32 DoorVolume = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Front Door", meta = (EditCondition = "bFrontDoor"))
	EHearthlineSide DoorSide = EHearthlineSide::Front;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Front Door", meta = (EditCondition = "bFrontDoor", ClampMin = "0", ClampMax = "1"))
	float DoorPosition = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Front Door", meta = (EditCondition = "bFrontDoor"))
	EHearthlineDoorStyle DoorStyle = EHearthlineDoorStyle::SixPanel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Front Door", meta = (EditCondition = "bFrontDoor"))
	FLinearColor DoorColor = FLinearColor(0.12f, 0.015f, 0.012f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Front Door", meta = (EditCondition = "bFrontDoor", ClampMin = "60", ClampMax = "200"))
	float DoorWidth = 95.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Front Door", meta = (EditCondition = "bFrontDoor", ClampMin = "150", ClampMax = "300"))
	float DoorHeight = 207.f;

	/** Narrow glazed panels either side of the door. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Front Door", meta = (EditCondition = "bFrontDoor"))
	bool bSidelights = false;

	// ---- Garage ---------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Garage")
	bool bGarage = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Garage", meta = (EditCondition = "bGarage"))
	int32 GarageVolume = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Garage", meta = (EditCondition = "bGarage"))
	EHearthlineSide GarageSide = EHearthlineSide::Front;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Garage", meta = (EditCondition = "bGarage", ClampMin = "0", ClampMax = "1"))
	float GaragePosition = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Garage", meta = (EditCondition = "bGarage", ClampMin = "1", ClampMax = "3"))
	int32 GarageDoors = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Garage", meta = (EditCondition = "bGarage", ClampMin = "200", ClampMax = "600"))
	float GarageDoorWidth = 490.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Garage", meta = (EditCondition = "bGarage", ClampMin = "180", ClampMax = "350"))
	float GarageDoorHeight = 215.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Garage", meta = (EditCondition = "bGarage"))
	FLinearColor GarageDoorColor = FLinearColor(0.88f, 0.88f, 0.86f);

	/** Row of small windows in the top section of the garage door. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Garage", meta = (EditCondition = "bGarage"))
	bool bGarageWindows = true;

	// ---- Extra openings -------------------------------------------------------------------------------------

	/** Hand-placed windows, doors, garage doors and blank zones. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Openings")
	TArray<FHearthlineOpeningSpec> Openings;

	// ---- Lot ------------------------------------------------------------------------------------------------

	/** Adds a lawn, a driveway to the garage and a path to the front door. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Lot")
	bool bLot = false;

	/** Lot size (X = front to back, Y = across), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Lot", meta = (EditCondition = "bLot"))
	FVector2D LotSize = FVector2D(3000.f, 2200.f);

	/** Distance from the actor origin to the front edge of the lot, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Lot", meta = (EditCondition = "bLot"))
	float FrontYard = 1300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Lot", meta = (EditCondition = "bLot"))
	bool bDriveway = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Lot", meta = (EditCondition = "bLot"))
	bool bWalkway = true;

	// ---- Planting and fences ------------------------------------------------------------------------------

	/** Shrubs along the front walls, clear of doors, garage doors and porches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Planting")
	bool bFoundationShrubs = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Planting", meta = (EditCondition = "bFoundationShrubs", ClampMin = "0", ClampMax = "1"))
	float ShrubDensity = 0.7f;

	/** Trees scattered over the lot (needs Lot). They keep clear of the house, driveway and path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Planting", meta = (EditCondition = "bLot", ClampMin = "0", ClampMax = "12"))
	int32 LotTrees = 0;

	/** Shortest and tallest tree, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Planting", meta = (EditCondition = "bLot"))
	FVector2D TreeHeight = FVector2D(650.f, 1050.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Planting")
	FLinearColor FoliageColor = FLinearColor(0.85f, 0.9f, 0.75f);

	/** Changes plant positions and shapes without touching the house. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Planting")
	int32 PlantSeed = 1;

	/** Fence around the back yard (needs Lot). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Fence", meta = (EditCondition = "bLot"))
	bool bFence = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Fence", meta = (EditCondition = "bFence"))
	EHearthlineFence FenceStyle = EHearthlineFence::Picket;

	/** Lot edges that get a fence. Turn one side off where a neighbour already has a fence on the shared line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Fence", meta = (EditCondition = "bFence", Bitmask, BitmaskEnum = "/Script/HearthlineHomes.EHearthlineSideMask"))
	int32 FenceSides = 14;

	/** 0 uses the usual height for the style (picket 105, privacy 180). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Fence", meta = (EditCondition = "bFence", ClampMin = "0", ClampMax = "300"))
	float FenceHeight = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Fence", meta = (EditCondition = "bFence"))
	FLinearColor FenceColor = FLinearColor(0.9f, 0.9f, 0.88f);

	// ---- Materials and output -------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Materials")
	FHearthlineMaterialSet Materials;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Output")
	bool bGenerateCollision = true;

	/** When set, the actor shows this static mesh instead of generating geometry. Filled in by Bake To Static Mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Output")
	TObjectPtr<UStaticMesh> BakedMesh;

	/** Content folder that receives baked meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Output")
	FString BakeFolder = TEXT("/Game/HearthlineBaked");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hearthline|Output")
	bool bBakeWithNanite = true;

	/** Rebuilds the shell from the current properties. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Hearthline|Output")
	void Rebuild();

	/** Editor only: writes the current shell to a static mesh asset and displays that instead. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Hearthline|Output")
	void BakeToStaticMesh();

	/** Returns to live generation. The baked asset is left in the content folder. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Hearthline|Output")
	void ClearBake();

	/** Number of triangles in the last generated shell. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hearthline|Output")
	int32 TriangleCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hearthline")
	TObjectPtr<UDynamicMeshComponent> ShellComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hearthline")
	TObjectPtr<UStaticMeshComponent> BakedComponent;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

private:
	friend struct FHearthlineHouseGen;
	bool bBuiltOnce = false;
	/** Surface slots present in the last generated shell, in material order. */
	TArray<int32> UsedSlots;
};
