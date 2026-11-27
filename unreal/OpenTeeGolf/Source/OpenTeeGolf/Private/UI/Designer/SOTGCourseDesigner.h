// The course's designers, opened in a round over the course itself, every change seen at once: the
// grass (SOTGGrassDesigner.h), the trees and rocks (SOTGTreeDesigner.h) and the bridges
// (SOTGBridgeDesigner.h), on tabs (R).

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UI/Designer/SOTGGrassDesigner.h"
#include "UI/Designer/SOTGTreeDesigner.h"
#include "UI/Designer/SOTGBridgeDesigner.h"

class AOTGRuntimeCourse;
class SBox;

// Both, on tabs (R)
class SOTGCourseDesigner : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGCourseDesigner) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	void Open(AOTGRuntimeCourse* Course);
	TSharedPtr<SOTGTreeDesigner> Trees() const { return TreePanel; }
	TSharedPtr<SOTGGrassDesigner> GrassTab() const { return GrassPanel; }
	TSharedPtr<SOTGBridgeDesigner> BridgeTab() const { return BridgePanel; }
	// (a click on the course: the trees' or the bridges' tab's to use, when it's showing)
	bool WorldClick(const FVector& Origin, const FVector& Dir, bool bGround, const FVector& Ground)
	{
		if (Current == 2) return BridgePanel.IsValid() && BridgePanel->WorldClick(Origin, Dir, bGround, Ground);
		return Current == 1 && TreePanel.IsValid() && TreePanel->WorldClick(Origin, Dir, bGround, Ground);
	}
	void ShowTab(int32 Tab);   // 0 the grass, 1 the trees and rocks, 2 the bridges

private:
	TSharedPtr<SOTGGrassDesigner> GrassPanel;
	TSharedPtr<SOTGTreeDesigner> TreePanel;
	TSharedPtr<SOTGBridgeDesigner> BridgePanel;
	TSharedPtr<class SWidgetSwitcher> Switch;
	TSharedPtr<SBox> TabRow;
	int32 Current = 0;
};
