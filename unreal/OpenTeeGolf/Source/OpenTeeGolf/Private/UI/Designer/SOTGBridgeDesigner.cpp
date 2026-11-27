// See SOTGBridgeDesigner.h.

#include "UI/Designer/SOTGBridgeDesigner.h"

#include "UI/OTGUi.h"
#include "World/OTGRuntimeCourse.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

using namespace otg::ui;

namespace
{
	constexpr double MinSpanM = 2, MaxSpanM = 150;
	constexpr double EndLiftM = 0.03;   // (the deck's top at an end: just over the ground, so the ball rolls on)
}

void SOTGBridgeDesigner::Construct(const FArguments&)
{
	ChildSlot[SAssignNew(Body, SBox).WidthOverride(520).MaxDesiredHeight(760)];
	// (the first end, and the picked bridge's ends, marked on the course while this shows)
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float) {
		if (!Course.IsValid() || !GetVisibility().IsVisible()) return EActiveTimerReturnType::Continue;
		TArray<FVector> Pts;
		if (bFirst) Pts.Add(Course->CourseToWorldAt(FirstX, Course->GroundHeightAt(FirstX, FirstZ), FirstZ));
		if (Edit.Bridges.IsValidIndex(Sel))
		{
			const FOTGBridge& B = Edit.Bridges[Sel];
			Pts.Add(Course->CourseToWorldAt(B.AX, B.AY, B.AZ));
			Pts.Add(Course->CourseToWorldAt(B.BX, B.BY, B.BZ));
		}
		if (Pts.Num()) Course->MarkPoints(Pts);
		return EActiveTimerReturnType::Continue;
	}));
}

void SOTGBridgeDesigner::Open(AOTGRuntimeCourse* InCourse)
{
	Course = InCourse;
	Edit = InCourse ? InCourse->BridgeDesign() : FOTGBridgeDesign();
	Status.Empty();
	bFirst = false;
	Sel = -1;
	bNew = false;
	Rebuild();
}

void SOTGBridgeDesigner::SetTool(int32 InTool)
{
	Tool = FMath::Clamp(InTool, 0, 1);
	bFirst = false;
	Rebuild();
}

void SOTGBridgeDesigner::Apply()
{
	if (!Course.IsValid()) return;
	Course->SetBridges(Edit);
	if (OnChanged) OnChanged();
}

FString SOTGBridgeDesigner::Describe(const FOTGBridge& B) const
{
	return FString::Printf(TEXT("%d m  ·  %s %s, %s"), FMath::RoundToInt(B.Span()), B.bArch ? TEXT("arched") : TEXT("flat"),
		*FOTGBridge::LookLabel(B.Look).ToLower(), B.bRails ? TEXT("rails") : TEXT("no rails"));
}

void SOTGBridgeDesigner::Begin(double X, double Z)
{
	bFirst = true;
	FirstX = X; FirstZ = Z;
	if (bNew && Edit.Bridges.IsValidIndex(Sel)) { Edit.Bridges.RemoveAt(Sel); Apply(); }   // (one not built yet: let go)
	Sel = -1;
	bNew = false;
	Status = TEXT("One end set: now click where it ends");
}

void SOTGBridgeDesigner::Finish(double X, double Z)
{
	const double Span = FVector2D(X - FirstX, Z - FirstZ).Size();
	if (Span < MinSpanM) { Status = TEXT("Too short: click further from the first end"); return; }
	if (Span > MaxSpanM) { Status = FString::Printf(TEXT("Too long (%d m): no more than %d m"), FMath::RoundToInt(Span), int32(MaxSpanM)); return; }
	FOTGBridge B = Style;
	B.AX = FirstX; B.AZ = FirstZ; B.BX = X; B.BZ = Z;
	B.AY = Course->GroundHeightAt(B.AX, B.AZ) + EndLiftM;
	B.BY = Course->GroundHeightAt(B.BX, B.BZ) + EndLiftM;
	B.Rise = FOTGBridge::RiseFor(Span);
	Edit.Bridges.Add(B);
	Sel = Edit.Bridges.Num() - 1;
	bNew = true;
	bFirst = false;
	Apply();
	Status = TEXT("Choose its style, then Build");
	UE_LOG(LogTemp, Display, TEXT("Bridge from %.1f %.1f to %.1f %.1f (course m): %s"), B.AX, B.AZ, B.BX, B.BZ, *Describe(B));
}

void SOTGBridgeDesigner::AddAt(double AX, double AZ, double BX, double BZ)
{
	if (!Course.IsValid()) return;
	Begin(AX, AZ);
	Finish(BX, BZ);
	Rebuild();
}

bool SOTGBridgeDesigner::WorldClick(const FVector& Origin, const FVector& Dir, bool bGround, const FVector& Ground)
{
	if (!Course.IsValid()) return false;
	if (Tool == 0)
	{
		const double MaxT = bGround ? FVector::Dist(Origin, Ground) : 1e9;
		const int32 I = Course->PickBridge(Origin, Dir, MaxT);
		if (bNew && I != Sel && Edit.Bridges.IsValidIndex(Sel)) { Edit.Bridges.RemoveAt(Sel); Apply(); }   // (one not built: let go)
		bNew = false;
		Sel = Edit.Bridges.IsValidIndex(I) ? I : -1;
		Status = Sel >= 0 ? FString() : FString(TEXT("Nothing there: click a bridge"));
		Rebuild();
		return true;
	}
	if (!bGround) return true;
	const FVector2D At = Course->CourseXZ(Ground);
	if (!bFirst) Begin(At.X, At.Y);
	else Finish(At.X, At.Y);
	Rebuild();
	return true;
}

void SOTGBridgeDesigner::EditPicked(const FString& Field, const FString& Value)
{
	if (!Edit.Bridges.IsValidIndex(Sel)) { UE_LOG(LogTemp, Warning, TEXT("OTG.Bridges.Edit: no bridge picked")); return; }
	FOTGBridge& B = Edit.Bridges[Sel];
	const float V = FCString::Atof(*Value);
	if (Field == TEXT("shape")) B.bArch = Value == TEXT("arch");
	else if (Field == TEXT("rails")) B.bRails = Value != TEXT("off");
	else if (Field == TEXT("look") && FOTGBridge::Looks().Contains(Value)) B.Look = Value;
	else if (Field == TEXT("width")) B.Width = FMath::Clamp(V, FOTGBridge::WidthLo, FOTGBridge::WidthHi);
	else if (Field == TEXT("rise")) B.Rise = FMath::Clamp(V, FOTGBridge::RiseLo, FOTGBridge::RiseHi);
	else if (Field == TEXT("build")) { bNew = false; Sel = -1; Status = TEXT("Built (not saved yet)"); Rebuild(); return; }
	else if (Field == TEXT("cancel") || Field == TEXT("out"))
	{
		Edit.Bridges.RemoveAt(Sel);
		Status = bNew ? TEXT("Let go") : TEXT("Taken out (not saved yet)");
		Sel = -1; bNew = false;
		Apply(); Rebuild();
		return;
	}
	if (bNew) { Style = B; }
	Apply();
	Rebuild();
}

TSharedRef<SWidget> SOTGBridgeDesigner::PickPanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	const FOTGBridge& B = Edit.Bridges[Sel];
	V->AddSlot().AutoHeight().Padding(0, 8, 0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[Text(bNew ? TEXT("New bridge") : FString::Printf(TEXT("Bridge %d"), Sel + 1), 15, Gold, "Bold")]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(8, 0, 0, 2)[Text(FString::Printf(TEXT("%d m across"), FMath::RoundToInt(B.Span())), 12, Soft)]
	];
	auto Row = [&](const FString& Label, TSharedRef<SWidget> W) {
		V->AddSlot().AutoHeight().Padding(18, 3, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(90)[Text(Label, 12, Soft)]]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[W]
		];
	};
	// (a choice changed: drawn again at once; a new bridge's choices are the next one's too)
	auto Set = [this](TFunction<void(FOTGBridge&)> Fn) {
		return [this, Fn]() {
			if (!Edit.Bridges.IsValidIndex(Sel)) return;
			Fn(Edit.Bridges[Sel]);
			if (bNew) Style = Edit.Bridges[Sel];
			Apply();
			Rebuild();
		};
	};
	auto Choices = [&](TArray<TPair<FString, bool>> Opts, TArray<TFunction<void()>> Do) {
		TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
		for (int32 i = 0; i < Opts.Num(); i++) H->AddSlot().AutoWidth().Padding(0, 0, 4, 0)[Button(Opts[i].Key, Do[i], 12, Opts[i].Value)];
		return H;
	};
	Row(TEXT("Shape"), Choices({ { TEXT("Flat"), !B.bArch }, { TEXT("Arched"), B.bArch } },
		{ Set([](FOTGBridge& X) { X.bArch = false; }), Set([](FOTGBridge& X) { X.bArch = true; }) }));
	Row(TEXT("Rails"), Choices({ { TEXT("With rails"), B.bRails }, { TEXT("No rails"), !B.bRails } },
		{ Set([](FOTGBridge& X) { X.bRails = true; }), Set([](FOTGBridge& X) { X.bRails = false; }) }));
	{
		TArray<TPair<FString, bool>> Opts;
		TArray<TFunction<void()>> Do;
		for (const FString& L : FOTGBridge::Looks())
		{
			Opts.Add({ FOTGBridge::LookLabel(L), B.Look == L });
			Do.Add(Set([L](FOTGBridge& X) { X.Look = L; }));
		}
		Row(TEXT("Made of"), Choices(Opts, Do));
	}
	// (a slider: the number changes as it moves, the bridge drawn again when it's let go)
	auto Slider = [&](const FString& Label, float Lo, float Hi, float FOTGBridge::* Field) {
		Row(Label,
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([this, Lo, Hi, Field]() { return Edit.Bridges.IsValidIndex(Sel) ? FMath::Clamp((Edit.Bridges[Sel].*Field - Lo) / (Hi - Lo), 0.f, 1.f) : 0.f; })
				.OnValueChanged_Lambda([this, Lo, Hi, Field](float F) {
					if (!Edit.Bridges.IsValidIndex(Sel)) return;
					Edit.Bridges[Sel].*Field = FMath::RoundToFloat((Lo + (Hi - Lo) * F) * 10) / 10;
					if (bNew) Style.*Field = Edit.Bridges[Sel].*Field;
				})
				.OnMouseCaptureEnd_Lambda([this]() { Apply(); })
				.OnControllerCaptureEnd_Lambda([this]() { Apply(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(70)[SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Ink).Text_Lambda([this, Field]() {
					return FText::FromString(Edit.Bridges.IsValidIndex(Sel) ? FString::Printf(TEXT("%.1f m"), Edit.Bridges[Sel].*Field) : FString());
				})]
			]);
	};
	Slider(TEXT("Width"), FOTGBridge::WidthLo, FOTGBridge::WidthHi, &FOTGBridge::Width);
	if (B.bArch) Slider(TEXT("Arch rise"), FOTGBridge::RiseLo, FOTGBridge::RiseHi, &FOTGBridge::Rise);
	V->AddSlot().AutoHeight().Padding(18, 8, 0, 0)
	[
		bNew
		? Choices({ { TEXT("Build"), true }, { TEXT("Cancel"), false } }, {
			[this]() { bNew = false; Sel = -1; Status = TEXT("Built (not saved yet)"); Rebuild(); },
			[this]() { if (Edit.Bridges.IsValidIndex(Sel)) Edit.Bridges.RemoveAt(Sel); Sel = -1; bNew = false; Status = TEXT("Let go"); Apply(); Rebuild(); } })
		: Choices({ { TEXT("Done"), true }, { TEXT("Take out"), false } }, {
			[this]() { Sel = -1; Rebuild(); },
			[this]() { if (Edit.Bridges.IsValidIndex(Sel)) Edit.Bridges.RemoveAt(Sel); Sel = -1; Status = TEXT("Taken out (not saved yet)"); Apply(); Rebuild(); } })
	];
	return V;
}

void SOTGBridgeDesigner::Rebuild()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[Text(TEXT("Bridges"), 18, Ink, "Bold")]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(8, 0, 0, 2)[Text(TEXT("seen at once  ·  R: close"), 12, Soft)]
	];
	if (!Course.IsValid())
	{
		V->AddSlot().AutoHeight()[Text(TEXT("No course to build on (still loading?)"), 12, Soft)];
		Body->SetContent(Box(V, FMargin(14, 10), Solid));
		return;
	}
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Text(TEXT("Click on the course to"), 15, Gold, "Bold")];
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)[Button(TEXT("Pick one"), [this]() { SetTool(0); }, 12, Tool == 0)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Add a bridge"), [this]() { SetTool(1); }, 12, Tool == 1)]
	];
	V->AddSlot().AutoHeight().Padding(18, 4, 0, 0)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(450).Text(FText::FromString(
		Tool == 0 ? TEXT("Click a bridge to change its style or take it out. Fly with F (W A S D, right mouse to look) to get close.")
		: bFirst ? TEXT("Now click where it ends - across the water, say. Its deck sits on the ground at both ends.")
		: TEXT("Click where the bridge starts - say a cart path's end on one bank - then where it ends. The ball rolls over it too.")))];
	if (Edit.Bridges.IsValidIndex(Sel)) V->AddSlot().AutoHeight()[PickPanel()];

	// the course's bridges, each to pick
	V->AddSlot().AutoHeight().Padding(0, 10, 0, 2)[Text(TEXT("On this course"), 15, Gold, "Bold")];
	if (!Edit.Bridges.Num()) V->AddSlot().AutoHeight().Padding(18, 0, 0, 0)[Text(TEXT("None yet"), 12, Soft)];
	for (int32 i = 0; i < Edit.Bridges.Num(); i++)
	{
		if (bNew && i == Sel) continue;
		V->AddSlot().AutoHeight().Padding(18, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Button(FString::Printf(TEXT("Bridge %d"), i + 1), [this, i]() {
				if (bNew && Edit.Bridges.IsValidIndex(Sel)) { Edit.Bridges.RemoveAt(Sel); Apply(); }
				bNew = false;
				Sel = Edit.Bridges.IsValidIndex(i) ? i : -1;
				Rebuild();
			}, 12, i == Sel)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Text(Describe(Edit.Bridges[i]), 12, Soft)]
		];
	}

	V->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Save for this course"), [this]() {
			if (bNew) { bNew = false; Sel = -1; }   // (saving builds the one being made)
			FString Where;
			const bool bOk = Course.IsValid() && Course->SaveBridges(&Where);
			Status = bOk ? FString(TEXT("Saved: the course has these from now on")) : FString(TEXT("Couldn't save ")) + Where;
			Rebuild();
		}, 13, true)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("As saved"), [this]() {
			if (!Course.IsValid()) return;
			Edit = Course->SavedBridges();
			Sel = -1; bNew = false; bFirst = false;
			Apply();
			Status = TEXT("Back as they were saved");
			Rebuild();
		}, 12)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("None"), [this]() {
			Edit.Bridges.Reset();
			Sel = -1; bNew = false; bFirst = false;
			Apply();
			Status = TEXT("No bridges (not saved yet)");
			Rebuild();
		}, 12)]
	];
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
	[
		SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).Text_Lambda([this]() {
			const int32 N = Edit.Bridges.Num() - (bNew ? 1 : 0);
			return FText::FromString(FString::Printf(TEXT("%s%s%d bridge%s"), *Status, Status.IsEmpty() ? TEXT("") : TEXT("  ·  "), N, N == 1 ? TEXT("") : TEXT("s")));
		})
	];
	Body->SetContent(Box(V, FMargin(14, 10), Solid));
}
