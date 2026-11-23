// The round's HUD, in Slate (no assets): the hole and the lie at the top left, the club, the shot
// data along the bottom, the launch monitor's status at the bottom right, who's winning at the top
// right (players together), banners and toasts in the middle, the keys, the scorecard, and the
// pause menu (with the launch monitor's settings).

#pragma once

#include "CoreMinimal.h"
#include "Round/OTGPlayers.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"

class STextBlock;
class SOTGMinimap;
class SOTGWind;
class SOTGShotPanel;
class SOTGWeatherPanel;
class SOTGCourseDesigner;
class SBox;
class SBorder;
class SVerticalBox;

struct FOTGScoreRow
{
	int32 Number = 0, Par = 0;
};

struct FOTGCard
{
	FString Course, Format, Summary;
	bool bFinal = false;
	TArray<FOTGScoreRow> Holes;
	struct FSide { FString Name; FLinearColor Colour; TArray<int32> Scores; };
	TArray<FSide> Sides;
	TArray<FOTGStanding> Standings;
};

class SOTGHud : public SCompoundWidget
{

public:
	SLATE_BEGIN_ARGS(SOTGHud) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	virtual void Tick(const FGeometry& Geometry, const double Time, const float Dt) override;

	void SetInfo(const FString& Line1, const FString& Line2, const FString& ClubLine);
	void SetCourseName(const FString& Name);
	// What the ball lies on, its colour's dot beside it (empty: hidden)
	void SetLie(const FString& Lie, const FLinearColor& Colour);
	// The pin's yardage and how far above or below it is, over the flag: Screen in the HUD's own units
	// (bShow false: hidden - the flag off screen or behind the camera)
	void SetPinTag(bool bShow, const FVector2D& Screen, const FString& Yards, const FString& Elevation, const FLinearColor& ElevColour);
	void SetShotData(const FString& Line1, const FString& Line2);

	// The shot data, as tiles (a launch monitor's numbers: ball speed, club speed, launch, spin...):
	// which show and where on the screen are the player's (D: ShowShotSettings), kept in the
	// game's settings. Values: by id (ShotItems), "" for one not measured; empty: hidden
	struct FShotItem { const TCHAR* Id; const TCHAR* Label; bool bDefault; };
	static const TArray<FShotItem>& ShotItems();
	static const TArray<FString>& ShotPlaces();   // bottom top left right bottomleft bottomright
	void SetShotValues(const TMap<FString, FString>& Values);
	void ShowShotSettings(bool bShow);
	bool IsShotSettingsShown() const;
	void SetStatus(const FString& Text, bool bReady);
	void SetStandings(const FString& Title, const TArray<FOTGStanding>& Rows, const FString& Summary);
	void Banner(const FString& Title, const FString& Small, float Seconds);
	void Toast(const FString& Text, float Seconds);
	void ShowScorecard(const FOTGCard& Card);
	void HideScorecard();
	void ShowPause(FSimpleDelegate OnResume, FSimpleDelegate OnScorecard, FSimpleDelegate OnQuit);
	void HidePause();
	TSharedPtr<SOTGMinimap> Minimap() const { return Map; }
	TSharedPtr<SOTGWind> Wind() const { return WindBox; }
	TSharedPtr<SOTGShotPanel> ShotPanel() const { return Shots; }
	void ShowShotPanel(bool bShow);
	bool IsShotPanelShown() const;
	TSharedPtr<SOTGWeatherPanel> WeatherPanel() const { return WxPanel; }
	void ShowWeatherPanel(bool bShow);
	bool IsWeatherPanelShown() const;
	TSharedPtr<SOTGCourseDesigner> Designer() const { return DesignPanel; }
	void ShowDesigner(bool bShow);
	bool IsDesignerShown() const;
	void ShowPerf(bool bShow);
	bool IsPerfShown() const;
	// The driving range: its panel where the minimap is, and its own keys along the bottom
	void SetPractice(const TSharedRef<SWidget>& Content, const FString& KeysText);

private:
	TSharedPtr<STextBlock> Info1, Info2, ClubText, Shot1, Shot2, Status, BannerTitle, BannerSmall, ToastText, Keys;
	TSharedPtr<SBorder> BannerBox, ToastBox, ShotBox, StandBox;
	TSharedPtr<SVerticalBox> StandList;
	TSharedPtr<SBox> CardSlot, PauseSlot, MapBox;
	float BannerLeft = 0, ToastLeft = 0;
	TSharedPtr<SOTGMinimap> Map;
	TSharedPtr<SOTGWind> WindBox;
	TSharedPtr<SOTGShotPanel> Shots;
	TSharedPtr<SOTGWeatherPanel> WxPanel;
	TSharedPtr<SOTGCourseDesigner> DesignPanel;
	TSharedPtr<SBorder> PerfBox;
	TSharedPtr<STextBlock> PerfText;
	double PerfShown = 0;
	FString StandKey;
	// the course's name, the lie, the pin's tag
	TSharedPtr<STextBlock> CourseText, LieText, PinYards, PinElev;
	TSharedPtr<SBorder> LieDot;
	TSharedPtr<SBox> LieRow, PinTag;
	// the shot data's tiles and their settings
	TSharedPtr<SBox> TileSlot, ShotSettingsSlot, SideSlot;
	SOverlay::FOverlaySlot* TilePlace = nullptr;
	TMap<FString, FString> ShotValues;
	TSet<FString> ShotShown;
	FString ShotWhere = TEXT("bottom");
	bool bSample = false;   // (the settings' sample numbers, until a shot)
	void LoadShotSettings();
	void SaveShotSettings() const;
	void BuildTiles();
	void BuildShotSettings();
};
