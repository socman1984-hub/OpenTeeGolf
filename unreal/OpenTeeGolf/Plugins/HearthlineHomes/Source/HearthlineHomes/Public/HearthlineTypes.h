// Copyright __PUBLISHER__. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "HearthlineTypes.generated.h"

class UMaterialInterface;

UENUM(BlueprintType)
enum class EHearthlineRoof : uint8
{
	Gable,
	Hip,
	Shed,
	Flat
};

/** Wall finishes. The order matches the first six material slots. */
UENUM(BlueprintType)
enum class EHearthlineCladding : uint8
{
	LapSiding,
	BoardAndBatten,
	Brick,
	Stucco,
	FieldStone,
	Concrete
};

/** Sides of a volume. Front faces the actor's +X axis. */
UENUM(BlueprintType)
enum class EHearthlineSide : uint8
{
	Front,
	Right,
	Back,
	Left
};

UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EHearthlineSideMask : uint8
{
	None = 0 UMETA(Hidden),
	Front = 1,
	Right = 2,
	Back = 4,
	Left = 8
};
ENUM_CLASS_FLAGS(EHearthlineSideMask);

UENUM(BlueprintType)
enum class EHearthlineOpening : uint8
{
	Window,
	Door,
	GarageDoor,
	/** Reserves the space so that no automatic window is placed there. */
	Blank
};

UENUM(BlueprintType)
enum class EHearthlineDoorStyle : uint8
{
	SixPanel,
	HalfGlass,
	Craftsman,
	Modern
};

UENUM(BlueprintType)
enum class EHearthlineAccent : uint8
{
	/** One cladding everywhere. */
	None,
	/** Gable ends and dormers use the accent cladding. */
	Gables,
	/** Everything above the ground storey uses the accent cladding. */
	UpperStoreys
};

UENUM(BlueprintType)
enum class EHearthlineFence : uint8
{
	/** Low painted pickets with pointed tops. */
	Picket,
	/** Tall closed board fence. */
	Privacy
};

UENUM(BlueprintType)
enum class EHearthlineStyle : uint8
{
	Ranch,
	Colonial,
	CapeCod,
	Craftsman,
	Farmhouse,
	Modern,
	Cottage,
	SplitLevel
};

/** One box-shaped part of a house with its own roof. A house is one or more overlapping volumes. */
USTRUCT(BlueprintType)
struct FHearthlineVolume
{
	GENERATED_BODY()

	/** Centre of the volume on the ground, relative to the actor (X forward, Y right), in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume")
	FVector2D Offset = FVector2D::ZeroVector;

	/** Size across the front (along Y), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "200", UIMax = "3000"))
	float Width = 1200.f;

	/** Size front to back (along X), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "200", UIMax = "3000"))
	float Depth = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "1", ClampMax = "4"))
	int32 Storeys = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume", meta = (ClampMin = "200", ClampMax = "500"))
	float StoreyHeight = 285.f;

	/** Raises this volume's floor above the foundation top, for split levels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume")
	float FloorOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof")
	EHearthlineRoof Roof = EHearthlineRoof::Gable;

	/** True: the ridge runs left to right, eaves face front and back. False: the gable faces the front. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof")
	bool bRidgeAlongWidth = true;

	/** Roof pitch in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof", meta = (ClampMin = "2", ClampMax = "60"))
	float Pitch = 30.f;

	/** Eave and rake overhang, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof", meta = (ClampMin = "0", ClampMax = "150"))
	float Overhang = 40.f;

	/** Shed roofs only: flips which side is the high side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof")
	bool bFlipShed = false;

	/** Dormers on the front-facing (or right-facing) slope of a gable roof. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof", meta = (ClampMin = "0", ClampMax = "8"))
	int32 DormersFront = 0;

	/** Dormers on the opposite slope. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof", meta = (ClampMin = "0", ClampMax = "8"))
	int32 DormersBack = 0;

	/** Sides that receive automatically spaced windows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Openings", meta = (Bitmask, BitmaskEnum = "/Script/HearthlineHomes.EHearthlineSideMask"))
	int32 WindowSides = 15;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finish", meta = (InlineEditConditionToggle))
	bool bOverrideCladding = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finish", meta = (EditCondition = "bOverrideCladding"))
	EHearthlineCladding Cladding = EHearthlineCladding::LapSiding;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finish", meta = (InlineEditConditionToggle))
	bool bOverrideColor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Finish", meta = (EditCondition = "bOverrideColor"))
	FLinearColor WallColor = FLinearColor(0.8f, 0.8f, 0.78f);
};

/** A hand-placed opening. Automatic windows keep clear of it. */
USTRUCT(BlueprintType)
struct FHearthlineOpeningSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Opening")
	int32 Volume = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Opening")
	EHearthlineSide Side = EHearthlineSide::Front;

	/** 0 is the ground storey. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Opening", meta = (ClampMin = "0"))
	int32 Storey = 0;

	/** Centre of the opening along the wall, 0 = left end and 1 = right end as seen from outside. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Opening", meta = (ClampMin = "0", ClampMax = "1"))
	float Position = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Opening")
	EHearthlineOpening Type = EHearthlineOpening::Window;

	/** cm. 0 uses the house default for the type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Opening", meta = (ClampMin = "0"))
	float Width = 0.f;

	/** cm. 0 uses the house default for the type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Opening", meta = (ClampMin = "0"))
	float Height = 0.f;
};

USTRUCT(BlueprintType)
struct FHearthlinePorch
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porch")
	int32 Volume = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porch")
	EHearthlineSide Side = EHearthlineSide::Front;

	/** Extent along the wall as fractions, left to right as seen from outside. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porch", meta = (ClampMin = "0", ClampMax = "1"))
	float Start = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porch", meta = (ClampMin = "0", ClampMax = "1"))
	float End = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porch", meta = (ClampMin = "80", ClampMax = "600"))
	float Depth = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porch")
	bool bRoof = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porch", meta = (ClampMin = "2", ClampMax = "35"))
	float RoofPitch = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porch")
	bool bRailing = true;

	/** Square post size, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porch", meta = (ClampMin = "6", ClampMax = "60"))
	float PostSize = 14.f;

	/** Adds a wider masonry base under each post (craftsman look). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porch")
	bool bPostPiers = false;

	/** Where the steps meet the porch, as a fraction of its length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porch", meta = (ClampMin = "0", ClampMax = "1"))
	float StepsPosition = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porch", meta = (ClampMin = "60", ClampMax = "600"))
	float StepsWidth = 160.f;
};

USTRUCT(BlueprintType)
struct FHearthlineChimney
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chimney")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chimney")
	int32 Volume = 0;

	/** Position across the width of the volume, 0 = left wall, 1 = right wall. Values outside 0..1 stand the stack against the outside of the wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chimney", meta = (UIMin = "-0.05", UIMax = "1.05"))
	float AcrossWidth = 1.03f;

	/** Position front to back, 0 = back wall, 1 = front wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chimney", meta = (UIMin = "-0.05", UIMax = "1.05"))
	float AlongDepth = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chimney", meta = (ClampMin = "30", ClampMax = "400"))
	FVector2D Size = FVector2D(70.f, 110.f);

	/** How far the stack rises above the highest point of its volume's roof, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chimney")
	float HeightAboveRoof = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chimney")
	EHearthlineCladding Cladding = EHearthlineCladding::Brick;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chimney")
	FLinearColor Color = FLinearColor::White;
};

/** Materials used by the generators. Replace any entry to use your own tileable material; vertex colour carries the tint. */
USTRUCT(BlueprintType)
struct FHearthlineMaterialSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials") TObjectPtr<UMaterialInterface> LapSiding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials") TObjectPtr<UMaterialInterface> BoardAndBatten;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials") TObjectPtr<UMaterialInterface> Brick;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials") TObjectPtr<UMaterialInterface> Stucco;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials") TObjectPtr<UMaterialInterface> FieldStone;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials") TObjectPtr<UMaterialInterface> Concrete;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials") TObjectPtr<UMaterialInterface> Shingles;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials") TObjectPtr<UMaterialInterface> Trim;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials") TObjectPtr<UMaterialInterface> Wood;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials") TObjectPtr<UMaterialInterface> Glass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials") TObjectPtr<UMaterialInterface> Metal;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials") TObjectPtr<UMaterialInterface> Asphalt;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials") TObjectPtr<UMaterialInterface> Grass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials") TObjectPtr<UMaterialInterface> Foliage;

	static constexpr int32 NumSlots = 14;
	UMaterialInterface* Get(int32 Slot) const;
	static FName SlotName(int32 Slot);
	void LoadDefaults();
};

namespace HearthlineSlot
{
	enum Type : int32 { LapSiding, BoardAndBatten, Brick, Stucco, FieldStone, Concrete, Shingles, Trim, Wood, Glass, Metal, Asphalt, Grass, Foliage };
}
