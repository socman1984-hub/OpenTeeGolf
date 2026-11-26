// The launch monitor page (in the main menu and the round's pause menu): how shots arrive, the
// device's status, and the Mevo+ setup. (The web version's Mevo+ setup page: mevo-page.js.)

#pragma once

#include "CoreMinimal.h"
#include "LaunchMonitor/OTGLaunchMonitorTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class STextBlock;
class UOTGLaunchMonitorSubsystem;
class UTexture2D;
struct FSlateBrush;

class SOTGLaunchMonitorPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGLaunchMonitorPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	virtual ~SOTGLaunchMonitorPanel(); // (turns the Mevo+ live view off)
	virtual void Tick(const FGeometry& Geometry, const double Time, const float Dt) override;

private:
	void Rebuild();
	void UpdateLiveView(); // the newest Mevo+ camera picture into LiveTex
	UOTGLaunchMonitorSubsystem* LM() const;

	// the Mevo+ live view (the web version's mevo.html camera card)
	TSharedPtr<STextBlock> LiveText;
	TSharedPtr<FSlateBrush> LiveBrush;
	TStrongObjectPtr<UTexture2D> LiveTex;
	TArray64<uint8> LivePixels;
	int32 LiveSerial = 0;
	bool bShownLive = false;

	TSharedPtr<SBox> Body;
	TSharedPtr<STextBlock> StatusText, DetailText;
	FString Address;
	FOTGMevoSetup Draft;
	bool bArmOnTurn = true;
	EOTGConnection Shown = EOTGConnection::FSGolf;
	bool bShownOff = false;
};
