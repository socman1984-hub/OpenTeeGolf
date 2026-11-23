// See SOTGCourseDesigner.h.

#include "UI/Designer/SOTGCourseDesigner.h"

#include "UI/OTGUi.h"
#include "World/OTGRuntimeCourse.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"

using namespace otg::ui;

#include "UI/Designer/SOTGDesignerShared.h"

// ------------------------------------------------------------------ both, on tabs

void SOTGCourseDesigner::Construct(const FArguments&)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)[SAssignNew(TabRow, SBox)]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(Switch, SWidgetSwitcher)
			+ SWidgetSwitcher::Slot()[SAssignNew(GrassPanel, SOTGGrassDesigner)]
			+ SWidgetSwitcher::Slot()[SAssignNew(TreePanel, SOTGTreeDesigner)]
		]
	];
	ShowTab(0);
}

void SOTGCourseDesigner::ShowTab(int32 Tab)
{
	Current = Tab;
	Switch->SetActiveWidgetIndex(Tab);
	TabRow->SetContent(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)[Button(TEXT("Grass"), [this]() { ShowTab(0); }, 13, Current == 0)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Trees and rocks"), [this]() { ShowTab(1); }, 13, Current == 1)]);
}

void SOTGCourseDesigner::Open(AOTGRuntimeCourse* Course)
{
	GrassPanel->Open(Course);
	TreePanel->Open(Course);
	ShowTab(Current);
}
