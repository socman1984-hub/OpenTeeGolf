// The course's designers, opened in a round over the course itself, every change seen at once: the
// grass (SOTGGrassDesigner.h) and the trees and rocks (SOTGTreeDesigner.h), on tabs (R).

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UI/Designer/SOTGGrassDesigner.h"
#include "UI/Designer/SOTGTreeDesigner.h"

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
	// (a click on the course: the trees tab's to use, when it's showing)
	bool WorldClick(const FVector& Origin, const FVector& Dir, bool bGround, const FVector& Ground)
	{
		return Current == 1 && TreePanel.IsValid() && TreePanel->WorldClick(Origin, Dir, bGround, Ground);
	}
	void ShowTab(int32 Tab);   // 0 the grass, 1 the trees and rocks

private:
	TSharedPtr<SOTGGrassDesigner> GrassPanel;
	TSharedPtr<SOTGTreeDesigner> TreePanel;
	TSharedPtr<class SWidgetSwitcher> Switch;
	TSharedPtr<SBox> TabRow;
	int32 Current = 0;
};
