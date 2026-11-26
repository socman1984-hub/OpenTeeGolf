// The game's menus, in Slate:
//
//  - SOTGMainMenu (the start level): Play (the courses - searched by name or place, sorted by name,
//    recently played or newest - and for the one picked the tees, pins, where to start, 18 or 9
//    holes, the weather, who's playing and the game), Practice (driving range, putt-putt), Course builder (find a real course on the map and build it:
//    SOTGCourseBuilder), Players (the profiles: name, colour, handicap, hand, tees), Launch monitor
//    (SOTGLaunchMonitorPanel.h), Settings and What's new (each version's changes); and a button to
//    join the Discord
//
// The pages are in files of their own: SOTGMainMenu.cpp (the frame, the tabs, the update bar),
// SOTGMainMenuPlay.cpp (the courses and the round's setup), SOTGMainMenuPractice.cpp (Practice and
// Players), SOTGMainMenuInfo.cpp (Settings and What's new); their shared look: SOTGMenuStyle.h.

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Round/OTGPlayers.h"
#include "LaunchMonitor/OTGLaunchMonitorTypes.h"
#include "Course/OTGCourseSubsystem.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class STextBlock;
class UOTGLaunchMonitorSubsystem;
class UOTGCourseSubsystem;

class SOTGMainMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGMainMenu) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	void Start();      // the round as set up
	virtual bool SupportsKeyboardFocus() const override { return true; }
	void ShowPage(const FString& Name);   // play, practice, build, players, lm, settings, news
	virtual void Tick(const FGeometry& Geometry, const double Time, const float Dt) override;

	// The Practice tab's entries (Driving range, Putt-putt): each shows "Coming soon" until it's given
	// a way to start it here
	struct FPracticeMode { FString Name, About; TFunction<void()> Open; };
	void SetPracticeMode(const FString& Name, TFunction<void()> Open);

private:
	enum class EPage : uint8 { Play, Practice, Build, Edit, Studio, Players, LaunchMonitor, Settings, WhatsNew };
	void Rebuild();
	// What's new: each version's changes (WHATS_NEW.md, next to OpenTeeGolf.exe: unreal/release in the repo)
	TSharedRef<SWidget> WhatsNewPage();
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
	TSharedPtr<class SOTGStudio> Studio;           // (the grass biomes and forests, with its preview plot: made each time it's opened)
	bool bStudioForests = false;
	void OpenStudio(bool bForests);
	void OpenEditor(const FString& Id);
	TSharedRef<SWidget> EditLink(const struct FOTGCourseInfo* Info);
public:
	void EditCourse(const FString& Id) { OpenEditor(Id); }
private:
	EPage Page = EPage::Play;
	EPage EditBack = EPage::Play;   // (the page the editor was opened from, and goes back to)
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
