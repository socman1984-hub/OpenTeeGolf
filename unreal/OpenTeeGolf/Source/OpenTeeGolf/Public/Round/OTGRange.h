// The driving range: a practice ground the game makes itself (no download, no export), played with
// the launch monitor or the test shot panel as a round is (AOTGRound in its range mode).
//
//  - otg::range::Ensure writes it as an exported course folder (play.json, heightmap.png,
//    surfaces.png, photo.jpg, layer_*.png, trees.json) under Saved/Range, once per version of the
//    layout, so the course subsystem opens it the same way as any course (L_Course, built by
//    AOTGRuntimeCourse). It's laid out as the web version's (public/js/practice.js drivingRange): a
//    wide mown fan from a long tee line out to a bank at 330 m, a target green with a flag at 50,
//    100, 150, 200 and 250 yards, a couple of bunkers, woods down both sides and valley sides
//    rising beyond.
//  - AOTGRangeProps stands the range's things on it: the hitting mats along the tee line, a flag on
//    every target green but the 150's (the round's own flag and cup are there), and a distance board
//    every 50 yards down both sides, its numbers made of blocks (no fonts or text materials: the same
//    engine shapes as the round's ball and flagstick, so a Shipping build has them all).
//
// Opened with OTG.Range (console), -range (command line) or the menu's Driving range button
// (UOTGCourseSubsystem::PlayRange).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OTGRange.generated.h"

// One shot on the range (yards, feet, mph, degrees, rpm; offline + right of the aim)
struct FOTGRangeShot
{
	FString Club, ClubName;
	double CarryYd = 0, TotalYd = 0, OfflineYd = 0, ApexFt = 0, Mph = 0, Launch = 0, Direction = 0, Spin = 0, Axis = 0;
	double RestX = 0, RestZ = 0;   // (where it stopped: course metres)
	double EarlySpin = -1;         // (Mevo+: the radar's first spin estimate, shown above the final; -1 none)
};

class UOTGCourseData;
class UStaticMesh;
class UInstancedStaticMeshComponent;
class UMaterialInterface;

namespace otg::range
{
	// The range's course id (play.json's id)
	OPENTEEGOLF_API extern const TCHAR* const Id;
	// A target green: its distance (yards from the middle mat), centre and radius (course metres), flag colour
	struct FTarget { int32 Yards = 0; double X = 0, Z = 0, R = 0; FLinearColor Colour; };
	OPENTEEGOLF_API const TArray<FTarget>& Targets();
	// The mown fan's half width (metres) at a distance down the range
	OPENTEEGOLF_API double HalfWidth(double Z);
	// Where the game keeps it, and writing it there if it isn't (or is an older layout)
	OPENTEEGOLF_API FString Folder();
	OPENTEEGOLF_API bool Ensure(FString& OutDir, FString* OutError = nullptr);
	OPENTEEGOLF_API bool IsRange(const UOTGCourseData* D);
}

UCLASS()
class OPENTEEGOLF_API AOTGRangeProps : public AActor
{
	GENERATED_BODY()

public:
	AOTGRangeProps();
	void Build(const UOTGCourseData* D);
	// The flags blown out downwind (Down: the way the wind blows, in the level; S 0 calm .. 1 strong)
	void SetWind(const FVector& Down, double S);

private:
	UInstancedStaticMeshComponent* Ism(UStaticMesh* Mesh, const FLinearColor& Colour);
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMat;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Parts;
	TMap<FString, UInstancedStaticMeshComponent*> ByLook;
	struct FFlag { UInstancedStaticMeshComponent* Comp = nullptr; int32 Index = 0; FVector Top; };
	TArray<FFlag> Flags;
};
