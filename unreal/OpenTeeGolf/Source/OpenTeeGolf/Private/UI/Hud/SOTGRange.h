// The driving range's panel (on the right of the HUD, where the minimap is on a course): the last
// shot's numbers - carry, total, offline, apex, ball speed, launch, direction, spin and its axis -
// and the club in hand's recent shots, newest first, with their averages.

#pragma once

#include "CoreMinimal.h"
#include "Round/OTGRange.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;

class SOTGRangePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGRangePanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	// The shots so far (oldest first) and the club in hand
	void Show(const TArray<FOTGRangeShot>& Shots, const FString& Club, const FString& ClubName);

private:
	TSharedPtr<SVerticalBox> Body;
};
