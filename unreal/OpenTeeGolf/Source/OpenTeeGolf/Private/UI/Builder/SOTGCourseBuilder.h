// Making a course in the game (the menu's "New course" page), as the web version's course builder
// does it, through the course helper (UOTGHelperSubsystem):
//
//  - SOTGWorldMap: a map of the world to find a course on - OpenStreetMap's map or the aerial
//    photos (tiles fetched and kept by the helper), dragged and zoomed with the mouse, every golf
//    course mapped in OpenStreetMap a dot (gold: built already), a click picks one
//  - SOTGCourseBuilder: the map, a search, and the course picked: what's mapped there (holes, greens,
//    fairways ...; which set of holes on a club with more than one), its name, then Build - the map
//    data, heights and aerial photos downloaded and the course built, then exported to the game's
//    courses folder - with how far it is; then Play. Below: the courses built, to export again
//    (after editing), bring up to date from OpenStreetMap, or delete.
//
// ragma once

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "UI/Builder/SOTGWorldMap.h"

class SBox;
class STextBlock;
class SEditableTextBox;
class UOTGHelperSubsystem;
class FJsonObject;

class SOTGCourseBuilder : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGCourseBuilder) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	// (each frame, before the layout - not in Tick, which runs as it's drawn: panels made there
	// are drawn once unmeasured, piled on each other)
	EActiveTimerReturnType Pulse(double Time, float Dt);
	TFunction<void(const FString& CourseId)> OnReady;   // (a course built and exported: play it)
	TFunction<void(const FString& CourseId)> OnEdit;    // (a built course to edit)
	void Refresh() { LoadBuilt(); }

private:
	UOTGHelperSubsystem* Helper() const;
	void Rebuild();
	TSharedRef<SWidget> PickedCard();
	TSharedRef<SWidget> JobCard();
	TSharedRef<SWidget> BuiltList();
	void Pick(int32 I, bool bFly);
	// (one of the map key's switches: its dot and name, ticked when shown; a click turns it off or on)
	TSharedRef<SWidget> KeySwitch(const FString& Label, const FSlateBrush* Dot, float DotSize, bool SOTGWorldMap::* Flag);
	void LoadWorld();
	void LoadBuilt();
	void Search();
	void Check();
	void StartBuild();
	void JobDone(const struct FOTGHelperJob& Job);

	TSharedPtr<SOTGWorldMap> Map;
	TSharedPtr<SBox> Side, Lower;
	FString NameDraft;
	TSharedPtr<SEditableTextBox> SearchBox, NameBox;
	TArray<FOTGMapCourse> Courses;        // (the world's, then what a search found)
	TMap<int64, int32> ById;              // (osm id -> index in Courses)
	TArray<int32> Found;                  // (the last search's)
	int32 Picked = INDEX_NONE;
	int32 WorldFrom = 0;
	bool bWorldDone = false, bWorldAsking = false;
	double NextWorld = 0;
	float WorldProgress = 0;
	int32 WorldSeeded = 0;   // (courses from the list that comes with the game)
	FString Status;
	// the course picked: what's mapped there
	TSharedPtr<FJsonObject> Checked;
	bool bChecking = false, bCheckFailed = false;
	FString Group;
	// the courses built (the helper's), and the ones the game has
	struct FBuilt { FString Id, Name, Location; int64 OsmId = 0; bool bInGame = false; };
	TArray<FBuilt> Built;
	FString ConfirmDelete;
	// a build going on: export it when it's done
	FString ReadyId;
	FDelegateHandle JobHandle;
	FString JobShown;
	// (-buildertest=<words>: search, pick the first, a screenshot; -buildertestbuild: build it too,
	// and quit when it's in the game - testing without the mouse)
	FString TestQuery;
	int32 TestStep = 0;
	double TestAt = 0;
	bool bTestBuild = false;
};
