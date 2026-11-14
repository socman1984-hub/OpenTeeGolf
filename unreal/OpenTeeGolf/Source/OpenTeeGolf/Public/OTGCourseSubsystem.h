// The courses the game has, and opening one. Two kinds:
//  - exported courses: a folder each in the courses folder (Documents\OpenTeeGolf\Courses by
//    default; -courses=<folder> or the Settings page for another), written by the web version's
//    Export to Unreal. Built when opened, in the template level L_Course (AOTGRuntimeCourse).
//  - course packs (the old way: built by the Unreal Editor, make_pack.ps1): UOTGCourseData assets
//    found through the asset manager as primary assets of type OTGCourse.
// An exported course is used over a pack of the same course (-packs: the pack).
//
// Console: OTG.Courses lists them, OTG.Play <id> opens one. Command line: -course=<id> opens one at
// start.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Game/OTGPlayers.h"
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
	// an exported course's folder (empty: a course pack)
	UPROPERTY(BlueprintReadOnly) FString ExportDir;
};

UCLASS()
class OPENTEEGOLF_API UOTGCourseSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf")
	TArray<FOTGCourseInfo> ListCourses() const;

	// Where exported courses are looked for (and the setting that moves it: empty for the default)
	FString CoursesFolder() const;
	static FString DefaultCoursesFolder();
	void SetCoursesFolder(const FString& Folder);

	// The course's data (loaded), or null
	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf")
	UOTGCourseData* LoadCourse(const FString& Id) const;

	// Open the course's level; the course is then Current
	UFUNCTION(BlueprintCallable, Category = "OpenTeeGolf")
	bool PlayCourse(const FString& Id);

	UPROPERTY(BlueprintReadOnly, Category = "OpenTeeGolf")
	TObjectPtr<UOTGCourseData> Current;

	// What the menu set up for the round (the round reads it when the course's level opens)
	UPROPERTY() FOTGRoundSetup PendingSetup;
	// (the exported course read last: the menu and the round use it again rather than reading it twice)
	UPROPERTY() mutable TObjectPtr<UOTGCourseData> LastExport;
	bool bHasPendingSetup = false;
};
