// The round's HUD, in Slate (no assets): the hole and the lie at the top left, the club, the shot
// data along the bottom, the launch monitor's status at the bottom right, who's winning at the top
// right (players together), banners and toasts in the middle, the keys, the scorecard, and the
// pause menu (with the launch monitor's settings).

#pragma once

#include "CoreMinimal.h"
#include "Game/OTGPlayers.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;
class SOTGMinimap;
class SOTGWind;
class SOTGShotPanel;
class SOTGWeatherPanel;
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
	void SetShotData(const FString& Line1, const FString& Line2);
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
	void ShowPerf(bool bShow);
	bool IsPerfShown() const;

private:
	TSharedPtr<STextBlock> Info1, Info2, ClubText, Shot1, Shot2, Status, BannerTitle, BannerSmall, ToastText, Keys;
	TSharedPtr<SBorder> BannerBox, ToastBox, ShotBox, StandBox;
	TSharedPtr<SVerticalBox> StandList;
	TSharedPtr<SBox> CardSlot, PauseSlot;
	float BannerLeft = 0, ToastLeft = 0;
	TSharedPtr<SOTGMinimap> Map;
	TSharedPtr<SOTGWind> WindBox;
	TSharedPtr<SOTGShotPanel> Shots;
	TSharedPtr<SOTGWeatherPanel> WxPanel;
	TSharedPtr<SBorder> PerfBox;
	TSharedPtr<STextBlock> PerfText;
	double PerfShown = 0;
	FString StandKey;
};
