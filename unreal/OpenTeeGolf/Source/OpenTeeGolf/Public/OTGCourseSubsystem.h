// The courses the game has: one per course pack (UOTGCourseData, found through the asset manager
// as primary assets of type OTGCourse), and opening one.
//
// Console: OTG.Courses lists them, OTG.Play <id> opens one. Command line: -course=<id> opens one at
// start.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OTGCourseSubsystem.generated.h"

class UOTGCourseData;

USTRUCT(BlueprintType)
struct FOTGCourseInfo
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Id;
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) FString Location;
	UPROPERTY(BlueprintReadOnly) int32 Holes = 0;
};

UCLASS()
class OPENTEEGOLF_API UOTGCourseSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf")
	TArray<FOTGCourseInfo> ListCourses() const;

	// The course's data (loaded), or null
	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf")
	UOTGCourseData* LoadCourse(const FString& Id) const;

	// Open the course's level; the course is then Current
	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf")
	bool PlayCourse(const FString& Id);

	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf")
	TObjectPtr<UOTGCourseData> Current;
};
