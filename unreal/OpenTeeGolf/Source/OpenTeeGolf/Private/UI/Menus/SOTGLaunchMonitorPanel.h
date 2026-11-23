// The launch monitor page (in the main menu and the round's pause menu): how shots arrive, the
// device's status, and the Mevo+ setup. (The web version's Mevo+ setup page: mevo-page.js.)

#pragma once

#include "CoreMinimal.h"
#include "LaunchMonitor/OTGLaunchMonitorTypes.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class STextBlock;
class UOTGLaunchMonitorSubsystem;

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
