// The game's menus, in Slate:
//
//  - SOTGMainMenu (the start level): Play (the course, tees, pins, where to start, 18 or 9 holes,
//    who's playing and the game), Players (the profiles: name, colour, handicap, hand, tees) and
//    Launch monitor
//  - SOTGLaunchMonitorPanel: how shots arrive, the device's status, and the Mevo+ setup (also in the
//    round's pause menu)

#pragma once

#include "CoreMinimal.h"
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
	void ShowPage(const FString& Name);   // play, players, lm

private:
	enum class EPage : uint8 { Play, Players, LaunchMonitor };
	void Rebuild();
	TSharedRef<SWidget> PlayPage();
	TSharedRef<SWidget> PlayersPage();
	UOTGCourseSubsystem* Courses() const;
	void Remember() { FOTGPlayers::SaveSetup(Setup); }

	TSharedPtr<SBox> Body;
	EPage Page = EPage::Play;
	TArray<FOTGCourseInfo> CourseList;
	FOTGRoundSetup Setup;
	TArray<FOTGPlayerProfile> Profiles, Draft;
	int32 ConfirmRemove = -1;
};
