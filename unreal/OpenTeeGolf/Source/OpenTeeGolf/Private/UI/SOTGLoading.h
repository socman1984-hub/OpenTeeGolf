// The loading screen while an exported course is built (AOTGRuntimeCourse::BuildInSteps): the
// course's name, a bar, and the step it's on.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SOTGLoading : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGLoading) {}
		SLATE_ARGUMENT(FString, Title)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	void Set(float Fraction, const FString& What);

private:
	float Frac = 0.f;
	FString Step;
};
