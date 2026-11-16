// The game's menus, in Slate:
//
//  - SOTGMainMenu (the start level): Play (the courses - searched by name or place, sorted by name,
//    recently played or newest - and for the one picked the tees, pins, where to start, 18 or 9
//    holes, the weather, who's playing and the game), Practice (driving range, putt-putt), New course (find a real course on the map and build it:
//    SOTGCourseBuilder), Players (the profiles: name, colour, handicap, hand, tees), Launch monitor
//    and Settings
//  - SOTGLaunchMonitorPanel: how shots arrive, the device's status, and the Mevo+ setup (also in the
//    round's pause menu)

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/OTGPlayers.h"
#include "LaunchMonitor/OTGLaunchMonitorTypes.h"
#include "OTGCourseSubsystem.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class STextBlock;
class UOTGLaunchMonitorSubsystem;
class UOTGCourseSubsystem;

class SOTGLaunchMonitorPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGLaunchMonitorPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	virtual void Tick(const FGeometry& Geometry, const double Time, const float Dt) override;

private:
	void Rebuild();
	UOTGLaunchMonitorSubsystem* LM() const;

	TSharedPtr<SBox> Body;
	TSharedPtr<STextBlock> StatusText, DetailText;
	FString Address;
	FOTGMevoSetup Draft;
	bool bArmOnTurn = true;
	EOTGConnection Shown = EOTGConnection::FSGolf;
	bool bShownOff = false;
};

class SOTGMainMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGMainMenu) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	void Start();      // the round as set up
	virtual bool SupportsKeyboardFocus() const override { return true; }
	void ShowPage(const FString& Name);   // play, practice, build, players, lm, settings
	virtual void Tick(const FGeometry& Geometry, const double Time, const float Dt) override;

	// The Practice tab's entries (Driving range, Putt-putt): each shows "Coming soon" until it's given
	// a way to start it here
	struct FPracticeMode { FString Name, About; TFunction<void()> Open; };
	void SetPracticeMode(const FString& Name, TFunction<void()> Open);

private:
	enum class EPage : uint8 { Play, Practice, Build, Edit, Players, LaunchMonitor, Settings };
	void Rebuild();
	TSharedRef<SWidget> PlayPage();
	TSharedRef<SWidget> PracticePage();
	TArray<FPracticeMode> PracticeModes;
	// the Play page's course list: what's typed in the search, the order, and the rows shown
	enum class ESort : uint8 { Name, Recent, Added };
	ESort Sort = ESort::Name;
	FString Search;
	TArray<int32> ShownCourses() const;   // (indexes into CourseList: matching the search, in order)
	void FillCourseList();
	FDateTime LastPlayedOf(const FOTGCourseInfo& Info) const;
	FDateTime AddedOf(const FOTGCourseInfo& Info) const;
	mutable TMap<FString, FDateTime> AddedAt;
	TSharedPtr<class SVerticalBox> CourseRows;
	TSharedPtr<STextBlock> CourseCount;
	TSharedPtr<class SScrollBox> CourseScroll, OptionsScroll;
	// (course thumbnails load one a frame, as their rows come into view)
	const FSlateBrush* ThumbOf(const FString& Id);
	TArray<FString> ThumbsWanted;
	TSharedRef<SWidget> PlayersPage();
	TSharedRef<SWidget> SettingsPage();
	TSharedRef<SWidget> UpdateBar();
	class UOTGUpdateSubsystem* Updater() const;
	bool bUpdateLater = false;   // (the update bar put off until the game starts again)
	UOTGCourseSubsystem* Courses() const;
	void Remember() { FOTGPlayers::SaveSetup(Setup); }

	TSharedPtr<SBox> Body;
	TSharedPtr<class SOTGCourseBuilder> Builder;   // (made once: it keeps its map and searches)
	TSharedPtr<class SOTGCourseEditor> Editor;     // (the course being edited)
	void OpenEditor(const FString& Id);
public:
	void EditCourse(const FString& Id) { OpenEditor(Id); }
private:
	EPage Page = EPage::Play;
	TArray<FOTGCourseInfo> CourseList;
	FOTGRoundSetup Setup;
	TArray<FOTGPlayerProfile> Profiles, Draft;
	// each course's aerial photo: its row's thumbnail, the picked course's header, and the backdrop
	enum class EPhoto : uint8 { Thumb, Header, Backdrop };
	const FSlateBrush* PhotoOf(const FString& Id, EPhoto Kind);
	TMap<FString, TSharedPtr<FSlateBrush>> Photos;
	TMap<FString, TStrongObjectPtr<class UTexture2D>> FolderPhotos;   // (exported courses' photos, loaded from their folders)
	TSharedPtr<class SImage> Backdrop;
	TSharedPtr<SBox> TabBar;
	int32 ConfirmRemove = -1;
};
