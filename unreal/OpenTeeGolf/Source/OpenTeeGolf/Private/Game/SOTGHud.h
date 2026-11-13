// The round's HUD, in Slate (no assets): the hole and the lie at the top left, the club, the shot
// data along the bottom, the launch monitor's status at the bottom right, banners and toasts in the
// middle, the keys, and the scorecard.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;
class SBox;
class SBorder;

struct FOTGScoreRow
{
	int32 Number = 0, Par = 0, Strokes = -1; // (-1: not played yet)
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
	void Banner(const FString& Title, const FString& Small, float Seconds);
	void Toast(const FString& Text, float Seconds);
	void ShowScorecard(const FString& Course, const TArray<FOTGScoreRow>& Rows, bool bFinal);
	void HideScorecard();

private:
	TSharedPtr<STextBlock> Info1, Info2, ClubText, Shot1, Shot2, Status, BannerTitle, BannerSmall, ToastText, Keys;
	TSharedPtr<SBorder> BannerBox, ToastBox, ShotBox;
	TSharedPtr<SBox> CardSlot;
	float BannerLeft = 0, ToastLeft = 0;
};
