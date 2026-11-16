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
#include "Game/OTGTreeDesign.h"
#include "OTGCourseData.generated.h"

namespace otg
{
	struct PlayCourse; struct PlayTrees;
	// A greyscale PNG's pixels (8 or 16 bits a pixel)
	OPENTEEGOLF_API bool ReadGreyPng(const FString& File, int32 BitDepth, int32& OutW, int32& OutH, TArray64<uint8>& OutRaw);
}

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
	// The level the pack builder made for the course (a course loaded from its export folder: the
	// game's template level, L_Course, which builds it there)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Course") TSoftObjectPtr<UWorld> Level;
	// A course loaded from its export folder at run time (not a pack): the folder
	UPROPERTY() FString ExportDir;

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

	// The play data from an export folder (play.json, its heightmap and surfaces): false, with the
	// reason in OutError, if it isn't there or doesn't read
	bool LoadFromExport(const FString& Dir, FString* OutError = nullptr);

	// The course as the physics plays it
	void FillPlayCourse(otg::PlayCourse& Out) const;
	void FillTrees(otg::PlayTrees& Out) const;

	// The course's trees and rocks as its tree designer left them (trees_design.json; changed live by
	// the designer), and the trees as they stand with it: each play tree (Trees, the same order as
	// the export's trees.json) taken out, kept, or with others round it - Src the tree it comes from,
	// X Z where it stands (course metres), Ground the ground's height there, Scale its size; Edit
	// its own changes if it has any (a tree picked in the designer). Trees added in the designer
	// come last, Src -1 (no tree they come from: Edit says what they are, Scale 1)
	FOTGTreeDesign TreeDesign;
	void ForEachDesignedTree(TFunctionRef<void(int32 Src, double X, double Z, double Ground, float Scale, const FOTGTreeEdit* Edit)> Fn) const;
	// (an added tree's shape for the ball: from its species and height, standing on Ground)
	static FOTGTreeShape AddedShape(const FOTGTreeEdit& E);
	double GroundAt(double X, double Z) const;          // (course metres; the heightmap's)
	uint8 SurfaceCodeAt(double X, double Z) const;      // (otg::Surface; 0 off the course)
	// Course metres (x east, z north, y up) to the level's centimetres (X east, Y south, Z up), and back
	FVector CourseToWorld(const FVector& P) const;
	FVector WorldToCourse(const FVector& W) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("OTGCourse"), FName(*CourseId)); }
};
