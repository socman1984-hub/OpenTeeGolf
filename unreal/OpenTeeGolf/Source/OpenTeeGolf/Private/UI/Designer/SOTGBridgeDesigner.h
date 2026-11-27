// The bridge designer (the course designer's Bridges tab): bridges built where you click - one end,
// then the other (say a cart path's ends on either bank of a pond) - then shown at once with their
// style to choose (flat or arched, rails or none, wood, stone or white boards, how wide, how high
// an arch) and built or let go. A bridge clicked afterwards is picked to change or take out. The
// ball meets them too (OnChanged: AOTGRound::RefreshBridges). Saved for the course (bridges.json:
// AOTGRuntimeCourse::SaveBridges, OTGBridges.h).

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "World/OTGBridges.h"

class AOTGRuntimeCourse;
class SBox;

class SOTGBridgeDesigner : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGBridgeDesigner) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	void Open(AOTGRuntimeCourse* Course);
	TFunction<void()> OnChanged;   // (the bridges changed: the ball's too)
	// A click on the course (as SOTGTreeDesigner::WorldClick): true if the designer used it
	bool WorldClick(const FVector& Origin, const FVector& Dir, bool bGround, const FVector& Ground);
	// (what a click does: 0 pick a bridge, 1 add one)
	void SetTool(int32 InTool);
	// (testing: the picked or new bridge changed as its panel would - shape flat|arch, rails on|off,
	// look wood|stone|white, width <m>, rise <m>, build, cancel, out)
	void EditPicked(const FString& Field, const FString& Value);
	// (testing: a new bridge from one point to another, course metres, as two clicks would make it)
	void AddAt(double AX, double AZ, double BX, double BZ);

private:
	void Rebuild();
	void Apply();
	void Begin(double X, double Z);
	void Finish(double X, double Z);
	TSharedRef<SWidget> PickPanel();
	FString Describe(const FOTGBridge& B) const;
	TWeakObjectPtr<AOTGRuntimeCourse> Course;
	FOTGBridgeDesign Edit;
	int32 Tool = 0;
	bool bFirst = false;             // (the first end clicked, the second still to come)
	double FirstX = 0, FirstZ = 0;
	int32 Sel = -1;                  // (the picked bridge, or the one just made)
	bool bNew = false;               // (just made: Build or Cancel)
	FOTGBridge Style;                // (the style a new one takes: the last chosen)
	TSharedPtr<SBox> Body;
	FString Status;
};
