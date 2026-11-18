// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GreenswardEditorLibrary.generated.h"

class UStaticMesh;
class UMaterialInterface;
class ALandscape;

/** Recipe for one procedurally generated grass mat. All lengths in cm. */
USTRUCT(BlueprintType)
struct FGreenswardMatSettings
{
	GENERATED_BODY()

	/** Edge length of the square mat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mat")
	float MatSize = 50.0f;

	/** Number of grass blades on the mat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mat", meta = (ClampMin = "1"))
	int32 BladeCount = 2600;

	/** Grass height. For mown grass this is the cutting height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blades", meta = (ClampMin = "0.2"))
	float Height = 4.0f;

	/** Mown: blades are cut flat at Height. Unmown: blades taper to a point and vary in height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blades")
	bool bMown = true;

	/** Fractional height variation between blades (unmown) or how much shorter some blades are than the cut (mown). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blades", meta = (ClampMin = "0", ClampMax = "0.9"))
	float HeightVariation = 0.3f;

	/** Blade width at the base. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blades", meta = (ClampMin = "0.02"))
	float BladeWidth = 0.45f;

	/** Maximum lean of a blade at its root, in degrees from vertical. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blades", meta = (ClampMin = "0", ClampMax = "70"))
	float Lean = 22.0f;

	/** How far a blade curls over along its length, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blades", meta = (ClampMin = "0", ClampMax = "120"))
	float Bend = 30.0f;

	/** Segments along each blade. 0 = choose from the height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blades", meta = (ClampMin = "0", ClampMax = "8"))
	int32 Segments = 0;

	/** Fraction of blades that are dead straw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blades", meta = (ClampMin = "0", ClampMax = "1"))
	float DryFraction = 0.0f;

	/** 0 = blades spread evenly, 1 = blades gathered into tufts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mat", meta = (ClampMin = "0", ClampMax = "1"))
	float Clumping = 0.35f;

	/** Seed stalks for meadows: thin stems that stand above the blades and carry a seed head. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stalks", meta = (ClampMin = "0"))
	int32 StalkCount = 0;

	/** Stalk height as a multiple of Height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stalks", meta = (ClampMin = "1"))
	float StalkHeightScale = 1.6f;

	/** Add a flat layer of turf under the blades so the ground never shows through. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mat")
	bool bGroundCover = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mat")
	int32 Seed = 1;

	/** Build the mesh as Nanite geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mat")
	bool bNanite = true;
};

/** An area of a generated landscape to paint with a layer and, optionally, level. Positions in world cm. */
USTRUCT(BlueprintType)
struct FGreenswardLandscapePatch
{
	GENERATED_BODY()

	/** Layer to paint: Grass, DryGrass, Soil, Mud, Sand, Gravel or Clay. None = do not paint (level only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	FName Layer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	FVector2D Centre = FVector2D::ZeroVector;

	/** Half width and half length of the patch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	FVector2D HalfSize = FVector2D(500.0, 500.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	float RotationDegrees = 0.0f;

	/** Ellipse rather than rectangle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	bool bEllipse = false;

	/** Width of the soft edge in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch", meta = (ClampMin = "1"))
	float Feather = 60.0f;

	/** 0-1: how ragged the edge is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch", meta = (ClampMin = "0", ClampMax = "1"))
	float EdgeBreakup = 0.0f;

	/** 0-1: how completely the layer replaces what was there. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch", meta = (ClampMin = "0", ClampMax = "1"))
	float Strength = 1.0f;

	/** Level the ground under the patch to the height at its centre, with a gentle bank around it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	bool bLevel = false;

	/** Raise (+) or sink (-) the patch, in cm. Use a negative value for a bunker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patch")
	float HeightOffset = 0.0f;
};

/** Editor tools: grass mat generation and stock asset authoring. */
UCLASS()
class GREENSWARDEDITOR_API UGreenswardEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Generate one grass mat static mesh and save it.
	 * @param PackagePath  Content folder, for example /Game/MyGrass
	 * @param AssetName    Asset name, for example SM_MyLawn_4cm
	 * @param Material     Material for the blades. Null = the stock Greensward grass material.
	 */
	UFUNCTION(BlueprintCallable, Category = "Greensward|Editor")
	static UStaticMesh* GenerateGrassMat(const FGreenswardMatSettings& Settings, const FString& PackagePath, const FString& AssetName, UMaterialInterface* Material);

	/** Generate the stock mat library into /Greensward/Mats. Returns the number of meshes written. MaxMats > 0 limits the run. */
	UFUNCTION(BlueprintCallable, Category = "Greensward|Editor")
	static int32 GenerateMatLibrary(int32 MaxMats = 0);

	/** (Re)author the stock parameter collection, noise texture, master material and instances. */
	UFUNCTION(BlueprintCallable, Category = "Greensward|Editor")
	static bool BuildCoreAssets();

	/**
	 * Create a rolling landscape centred on the origin, using the seven stock Greensward layers.
	 *  Material            Landscape material. Null = the stock layered material.
	 *  ComponentsPerSide   Landscape components along each edge (63 quads each).
	 *  QuadSize            Size of one landscape quad in cm.
	 *  HillHeight          Height of the rolling terrain in cm.
	 *  NaturalVariation    0-1: how much dry grass and bare soil is scattered over the base grass.
	 *  Patches             Painted and levelled areas, applied in order.
	 */
	UFUNCTION(BlueprintCallable, Category = "Greensward|Editor", meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "Patches"))
	static ALandscape* CreateLandscape(UObject* WorldContextObject, UMaterialInterface* Material, const TArray<FGreenswardLandscapePatch>& Patches, int32 ComponentsPerSide = 6, float QuadSize = 50.0f, float HillHeight = 90.0f, float NaturalVariation = 0.5f, int32 Seed = 7);
};
