// A course in a course pack: what the game needs to play it, from the web version's export
// (play.json, heightmap.png, surfaces.png; public/js/export-unreal.js), and its level. Made by the
// pack builder (unreal/pack_course.py, UOTGPackTools::CreateCourseData) as
// /Game/Courses/<id>/CourseData.
//
// Positions are in the game's course metres (x east, z north, y up), as the physics works in them;
// CourseToWorld turns them into the level's Unreal centimetres.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OTGCourseData.generated.h"

namespace otg { struct PlayCourse; struct PlayTrees; }

USTRUCT(BlueprintType)
struct FOTGHole
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Number = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Par = 4;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Index = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float GreenRadius = 0;
	// by tee colour (White, Blue ...) and by pin day; course metres
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TMap<FString, FVector> Tees;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TMap<FString, FVector> Pins;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector GreenCenter = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FVector> Aims;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FVector> Path;
};

// A tree's shape for the ball (otg::Tree): a trunk, and a cone (A base, B height, C radius) or an
// ellipsoid crown (A centre height, B radius across, C half height)
USTRUCT()
struct FOTGTreeShape
{
	GENERATED_BODY()
	UPROPERTY() float X = 0;
	UPROPERTY() float Z = 0;
	UPROPERTY() float TrunkTop = 0;
	UPROPERTY() float TrunkR = 0;
	UPROPERTY() FString Kind;
	UPROPERTY() bool bCone = false;
	UPROPERTY() float A = 0;
	UPROPERTY() float B = 0;
	UPROPERTY() float C = 0;
};

// A building or parked car: its footprint and height above the ground
USTRUCT()
struct FOTGSolid
{
	GENERATED_BODY()
	UPROPERTY() float Height = 0;
	UPROPERTY() FString Kind;
	UPROPERTY() TArray<FVector2D> Footprint;
};

USTRUCT()
struct FOTGDropZone
{
	GENERATED_BODY()
	UPROPERTY() FVector Position = FVector::ZeroVector;
	// the water's outline's box (min x, min z, max x, max z); none if bHasBox is false
	UPROPERTY() bool bHasBox = false;
	UPROPERTY() FVector4 Box = FVector4(0, 0, 0, 0);
};

UCLASS(BlueprintType)
class OPENTEEGOLF_API UOTGCourseData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Course") FString CourseId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Course") FString CourseName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Course") FString Location;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Course") double Latitude = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Course") double Longitude = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Course") double AltitudeM = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Course") float DefaultStimp = 10;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Course") TArray<FOTGHole> Holes;
	// The level the pack builder made for the course
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Course") TSoftObjectPtr<UWorld> Level;

	// Where the course is (course metres): outside, the ball is out
	UPROPERTY() double MinX = 0;
	UPROPERTY() double MaxX = 0;
	UPROPERTY() double MinZ = 0;
	UPROPERTY() double MaxZ = 0;
	// The export's heightmap (the level's landscape): Samples x Samples, sample (i, j) at
	// HeightX0 + i * MetresPerSample, HeightZ1 - j * MetresPerSample, value v at HeightBase + v / 65535 * HeightRange
	UPROPERTY() int32 HeightSamples = 0;
	UPROPERTY() double HeightX0 = 0;
	UPROPERTY() double HeightZ1 = 0;
	UPROPERTY() double MetresPerSample = 1;
	UPROPERTY() double HeightBase = 0;
	UPROPERTY() double HeightRange = 1;
	UPROPERTY() TArray<uint16> Heights;
	// The surfaces (otg::Surface codes), every SurfaceRes metres from SurfaceMinX, SurfaceMaxZ
	UPROPERTY() int32 SurfaceW = 0;
	UPROPERTY() int32 SurfaceH = 0;
	UPROPERTY() double SurfaceMinX = 0;
	UPROPERTY() double SurfaceMaxZ = 0;
	UPROPERTY() double SurfaceRes = 0.5;
	UPROPERTY() TArray<uint8> Surfaces;
	UPROPERTY() TArray<FOTGTreeShape> Trees;
	UPROPERTY() TArray<FOTGSolid> Solids;
	UPROPERTY() TArray<FOTGDropZone> DropZones;
	// Where the export's origin (the landscape's north-west corner, at HeightBase) is in the level
	UPROPERTY() FVector LevelOrigin = FVector::ZeroVector;

	// The course as the physics plays it
	void FillPlayCourse(otg::PlayCourse& Out) const;
	void FillTrees(otg::PlayTrees& Out) const;
	// Course metres (x east, z north, y up) to the level's centimetres (X east, Y south, Z up), and back
	FVector CourseToWorld(const FVector& P) const;
	FVector WorldToCourse(const FVector& W) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("OTGCourse"), FName(*CourseId)); }
};
