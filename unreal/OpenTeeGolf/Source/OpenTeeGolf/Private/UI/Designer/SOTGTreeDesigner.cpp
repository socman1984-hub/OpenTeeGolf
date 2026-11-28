// See SOTGTreeDesigner.h.

#include "UI/Designer/SOTGTreeDesigner.h"
#include "UI/Designer/SOTGCourseDesigner.h"

#include "UI/OTGUi.h"
#include "World/OTGRuntimeCourse.h"
#include "World/OTGBiomes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"

using namespace otg::ui;

#include "UI/Designer/SOTGDesignerShared.h"

// ------------------------------------------------------------------ the tree designer

void SOTGTreeDesigner::Construct(const FArguments&)
{
	ChildSlot[SAssignNew(Body, SBox).WidthOverride(520).MaxDesiredHeight(760)];
	// (the picked one marked on the course while this shows: the marks go when it stops asking)
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float) {
		if (bPicked && Course.IsValid() && GetVisibility().IsVisible()) Course->MarkPicked(Picked);
		if (SelForests.Num() && Course.IsValid() && GetVisibility().IsVisible()) Course->MarkForests(SelForests.Array());
		return EActiveTimerReturnType::Continue;
	}));
}

void SOTGTreeDesigner::Open(AOTGRuntimeCourse* InCourse)
{
	Course = InCourse;
	Edit = InCourse ? InCourse->TreeDesign() : FOTGTreeDesign();
	Species = InCourse ? InCourse->TreeSpecies() : TArray<TPair<FString, int32>>();
	Status.Empty();
	bPicked = false;
	SelForests.Reset();
	LibForests = OTGLibrary::Forests();
	// (testing, -designforests=all: every forest picked, outlined)
	if (FString W; FParse::Value(FCommandLine::Get(), TEXT("designforests="), W) && W == TEXT("all")) for (int32 i = 0; i < Edit.Forests.Num(); i++) SelForests.Add(i);
	// (the species to add: the course's own first, then the rest the game knows)
	SpeciesOpts.Reset();
	for (const TPair<FString, int32>& S : Species) SpeciesOpts.Add(MakeShared<FString>(S.Key));
	for (const FString& K : FOTGTreeDesign::AllSpecies())
		if (!Species.ContainsByPredicate([&](const TPair<FString, int32>& S) { return S.Key == K; })) SpeciesOpts.Add(MakeShared<FString>(K));
	if (!AddKind.IsValid() || !SpeciesOpts.ContainsByPredicate([&](const TSharedPtr<FString>& P) { return *P == *AddKind; })) AddKind = SpeciesOpts.Num() ? SpeciesOpts[0] : nullptr;
	Rebuild();
}

void SOTGTreeDesigner::Reselect()
{
	if (!bPicked || !Course.IsValid()) return;
	FOTGPicked P;
	if (Course->Picked(Picked.bRock, Picked.X, Picked.Z, P)) Picked = P;
	else bPicked = false;   // (taken out)
}

bool SOTGTreeDesigner::WorldClick(const FVector& Origin, const FVector& Dir, bool bGround, const FVector& Ground)
{
	if (!Course.IsValid()) return false;
	const double MaxT = bGround ? FVector::Dist(Origin, Ground) : 1e9;
	if (Tool == 0 || Tool == 3)
	{
		FOTGPicked P;
		if (!Course->Pick(Origin, Dir, MaxT, P))
		{
			if (Tool == 0) { bPicked = false; Status = TEXT("Nothing there: click a tree or a rock"); Rebuild(); }
			return true;
		}
		if (Tool == 3)
		{
			// taken out (one added here: gone altogether)
			if (P.bRock) { if (P.bAdded) Edit.ClearRockEdit(P.X, P.Z); else Edit.RockEdit(P.X, P.Z).bRemoved = true; }
			else { if (P.bAdded) Edit.ClearTreeEdit(P.X, P.Z); else Edit.TreeEdit(P.X, P.Z).bRemoved = true; }
			if (bPicked && Picked.bRock == P.bRock && FMath::Abs(Picked.X - P.X) < 0.15 && FMath::Abs(Picked.Z - P.Z) < 0.15) bPicked = false;
			Apply();
			Status = P.bRock ? TEXT("Rock taken out (not saved yet)") : TEXT("Tree taken out (not saved yet)");
			Rebuild();
			return true;
		}
		bPicked = true;
		Picked = P;
		const FOTGTreeEdit* E = P.bRock ? nullptr : Edit.TreeEditAt(P.X, P.Z);
		PickedBaseM = E && !E->bAdded ? P.HeightM / FMath::Max(0.1f, E->Size) : P.HeightM;
		Rebuild();
		return true;
	}
	if (!bGround) return true;
	if (Tool == 1)
	{
		if (!AddKind.IsValid()) return true;
		const FVector2D At = Course->CourseXZ(Ground);
		FOTGTreeEdit& E = Edit.TreeEdit(At.X, At.Y);
		E.bAdded = true; E.bRemoved = false; E.Kind = *AddKind; E.Height = AddHeight; E.Size = 1.f;
		Apply();
		Status = FString::Printf(TEXT("%s added (not saved yet)"), *FOTGTreeDesign::SpeciesLabel(*AddKind));
	}
	else
	{
		const FVector2D At = Course->RockXY(Ground);
		FOTGRockEdit& E = Edit.RockEdit(At.X, At.Y);
		E.bAdded = true; E.bRemoved = false; E.Size = AddRock;
		Apply();
		Status = TEXT("Rock added (not saved yet)");
	}
	Rebuild();
	return true;
}

void SOTGTreeDesigner::EditPicked(const FString& Field, const FString& Value)
{
	if (!bPicked) { UE_LOG(LogTemp, Warning, TEXT("OTG.Trees.Edit: nothing picked")); return; }
	const double X = Picked.X, Z = Picked.Z;
	const float V = FCString::Atof(*Value);
	if (Picked.bRock)
	{
		if (Field == TEXT("out")) { const FOTGRockEdit* E = Edit.RockEditAt(X, Z); if (E && E->bAdded) Edit.ClearRockEdit(X, Z); else Edit.RockEdit(X, Z).bRemoved = true; bPicked = false; }
		else if (Field == TEXT("reset")) Edit.ClearRockEdit(X, Z);
		else if (Field == TEXT("model")) Edit.RockEdit(X, Z).Model = FOTGTreeDesign::RockModels().Contains(Value) ? Value : FString();
		else Edit.RockEdit(X, Z).Size = FMath::Clamp(V, FOTGRockEdit::SizeLo, FOTGRockEdit::SizeHi);
	}
	else if (Field == TEXT("out")) { const FOTGTreeEdit* E = Edit.TreeEditAt(X, Z); if (E && E->bAdded) Edit.ClearTreeEdit(X, Z); else Edit.TreeEdit(X, Z).bRemoved = true; bPicked = false; }
	else if (Field == TEXT("reset")) Edit.ClearTreeEdit(X, Z);
	else
	{
		FOTGTreeEdit& E = Edit.TreeEdit(X, Z);
		if (Field == TEXT("size")) E.Size = FMath::Clamp(V, FOTGTreeEdit::SizeLo, FOTGTreeEdit::SizeHi);
		else if (Field == TEXT("height")) E.Height = FMath::Clamp(V, FOTGTreeEdit::HeightLo, FOTGTreeEdit::HeightHi);
		else if (Field == TEXT("hue")) E.Hue = FMath::Clamp(V, FOTGTreeEdit::HueLo, FOTGTreeEdit::HueHi);
		else if (Field == TEXT("bright")) E.Bright = FMath::Clamp(V, FOTGTreeEdit::BrightLo, FOTGTreeEdit::BrightHi);
		else if (Field == TEXT("kind")) E.Kind = Value;
		else if (Field == TEXT("model") && (Value.IsEmpty() || FOTGTreeDesign::Models().Contains(Value))) E.Model = Value;
	}
	Apply();
	Reselect();
	Rebuild();
}

TSharedRef<SWidget> SOTGTreeDesigner::PickPanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	const double X = Picked.X, Z = Picked.Z;
	auto Row = [&](const FString& Label, TSharedRef<SWidget> W) {
		V->AddSlot().AutoHeight().Padding(18, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(112)[Text(Label, 12, Soft)]]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[W]
		];
	};
	// (a slider on a number of the picked one's edit: made when first moved, drawn again when let go)
	auto Slider = [&](const FString& Label, float Lo, float Hi, TFunction<float()> Get, TFunction<void(float)> Set, TFunction<FString()> Fmt) {
		Row(Label,
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([Lo, Hi, Get]() { return FMath::Clamp((Get() - Lo) / (Hi - Lo), 0.f, 1.f); })
				.OnValueChanged_Lambda([Lo, Hi, Set](float F) { Set(Lo + (Hi - Lo) * F); })
				.OnMouseCaptureEnd_Lambda([this]() { Apply(); Reselect(); })
				.OnControllerCaptureEnd_Lambda([this]() { Apply(); Reselect(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(130)[SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Ink).Text_Lambda([Fmt]() { return FText::FromString(Fmt()); })]
			]);
	};
	if (Picked.bRock)
	{
		V->AddSlot().AutoHeight().Padding(0, 4, 0, 2)[Text(Picked.bAdded ? TEXT("A rock you added") : TEXT("A rock"), 14, Ink, "Bold")];
		if (Picked.bAdded)
			Slider(TEXT("Size"), FOTGRockEdit::SizeLo, FOTGRockEdit::SizeHi,
				[this, X, Z]() { const FOTGRockEdit* E = Edit.RockEditAt(X, Z); return E ? E->Size : 1.f; },
				[this, X, Z](float F) { Edit.RockEdit(X, Z).Size = FMath::RoundToFloat(F * 10) / 10; },
				[this, X, Z]() { const FOTGRockEdit* E = Edit.RockEditAt(X, Z); return FString::Printf(TEXT("%.1f m across"), E ? E->Size : 1.f); });
		else
			Slider(TEXT("Size"), FOTGRockEdit::SizeLo, FOTGRockEdit::SizeHi,
				[this, X, Z]() { const FOTGRockEdit* E = Edit.RockEditAt(X, Z); return E ? E->Size : 1.f; },
				[this, X, Z](float F) { Edit.RockEdit(X, Z).Size = FMath::RoundToFloat(F * 20) / 20; },
				[this, X, Z]() { const FOTGRockEdit* E = Edit.RockEditAt(X, Z); const float S = E ? E->Size : 1.f; return FMath::IsNearlyEqual(S, 1.f) ? FString(TEXT("as mapped")) : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(S * 100)); });
		// its look, from a list (as the course's rocks first)
		{
			const FOTGRockEdit* E0 = Edit.RockEditAt(X, Z);
			TArray<FString> M = FOTGTreeDesign::RockModels();
			M.Remove(FString());
			M.Insert(FString(), 0);
			Row(TEXT("Looks like"), Dropdown(M, E0 ? E0->Model : FString(),
				[](const FString& K) { return K.IsEmpty() ? FString(TEXT("As the course's rocks")) : FOTGTreeDesign::RockModelLabel(K); },
				[this, X, Z](const FString& K) { Edit.RockEdit(X, Z).Model = K; Apply(); Reselect(); Rebuild(); }));
		}
		V->AddSlot().AutoHeight().Padding(130, 6, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Take it out"), [this, X, Z]() {
				const FOTGRockEdit* E = Edit.RockEditAt(X, Z);
				if (E && E->bAdded) Edit.ClearRockEdit(X, Z); else Edit.RockEdit(X, Z).bRemoved = true;
				bPicked = false; Apply(); Status = TEXT("Rock taken out (not saved yet)"); Rebuild();
			}, 11)]
			+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("As it was"), [this, X, Z]() {
				const FOTGRockEdit* E = Edit.RockEditAt(X, Z);
				if (E && E->bAdded) { Edit.RockEdit(X, Z).Size = AddRock; Edit.RockEdit(X, Z).Model.Empty(); } else Edit.ClearRockEdit(X, Z);
				Apply(); Reselect(); Rebuild();
			}, 11)]
		];
		return V;
	}

	const FOTGTreeEdit* E0 = Edit.TreeEditAt(X, Z);
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[Text(FOTGTreeDesign::SpeciesLabel(Picked.Kind), 14, Ink, "Bold")]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(6, 0, 0, 1)
			[Text(FString::Printf(TEXT("%d m tall%s%s"), FMath::RoundToInt(Picked.HeightM), Picked.bAdded ? TEXT("  ·  added") : TEXT(""), E0 && !Picked.bAdded ? TEXT("  ·  changed") : TEXT("")), 11, Soft)]
	];
	// the forest (or forests) it grows in: a button picks it, in Forests above
	{
		TArray<int32> In;
		for (int32 i = 0; i < Edit.Forests.Num(); i++) if (Edit.Forests[i].Contains(X, Z)) In.Add(i);
		TSharedRef<SWrapBox> W = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4));
		W->AddSlot().VAlign(VAlign_Center)[Text(In.Num() ? TEXT("In") : TEXT("Not in a forest"), 12, Soft)];
		for (int32 i : In)
			W->AddSlot()[Button(SelForests.Num() == 1 && SelForests.Contains(i) ? Edit.Forests[i].Name + TEXT("  (picked)") : TEXT("Pick ") + Edit.Forests[i].Name, [this, i]() {
				SelForests.Reset();
				SelForests.Add(i);
				Status = FString::Printf(TEXT("%s picked: outlined on the course, its settings in Forests"), *Edit.Forests[i].Name);
				Rebuild();
			}, 11, SelForests.Contains(i))];
		V->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[W];
	}
	// its species (the model family it's drawn from follows)
	Row(TEXT("Species"),
		SNew(SComboBox<TSharedPtr<FString>>)
		.OptionsSource(&SpeciesOpts)
		.OnGenerateWidget_Lambda([](TSharedPtr<FString> P) { return StaticCastSharedRef<SWidget>(Text(FOTGTreeDesign::SpeciesLabel(*P), 12)); })
		.OnSelectionChanged_Lambda([this, X, Z](TSharedPtr<FString> P, ESelectInfo::Type How) {
			if (!P.IsValid() || How == ESelectInfo::Direct) return;
			FOTGTreeEdit& E = Edit.TreeEdit(X, Z);
			E.Kind = *P;
			Apply(); Reselect(); Rebuild();
		})
		[SNew(STextBlock).Font(Font(12)).Text(FText::FromString(FOTGTreeDesign::SpeciesLabel(Picked.Kind)))]);
	// its model, from a list (its species' own first)
	{
		TArray<FString> M = FOTGTreeDesign::Models();
		M.Remove(FString());
		M.Insert(FString(), 0);
		Row(TEXT("Drawn as"), Dropdown(M, E0 ? E0->Model : FString(),
			[](const FString& K) { return K.IsEmpty() ? FString(TEXT("As its species")) : FOTGTreeDesign::ModelLabel(K); },
			[this, X, Z](const FString& K) { Edit.TreeEdit(X, Z).Model = K; Apply(); Reselect(); Rebuild(); }));
	}
	if (Picked.bAdded)
		Slider(TEXT("Height"), FOTGTreeEdit::HeightLo, FOTGTreeEdit::HeightHi,
			[this, X, Z]() { const FOTGTreeEdit* E = Edit.TreeEditAt(X, Z); return E ? E->Height : 12.f; },
			[this, X, Z](float F) { Edit.TreeEdit(X, Z).Height = FMath::RoundToFloat(F * 2) / 2; },
			[this, X, Z]() { const FOTGTreeEdit* E = Edit.TreeEditAt(X, Z); return FString::Printf(TEXT("%.1f m"), E ? E->Height : 12.f); });
	else
	{
		const double Base = PickedBaseM;
		Slider(TEXT("Height"), FOTGTreeEdit::SizeLo, FOTGTreeEdit::SizeHi,
			[this, X, Z]() { const FOTGTreeEdit* E = Edit.TreeEditAt(X, Z); return E ? E->Size : 1.f; },
			[this, X, Z](float F) { Edit.TreeEdit(X, Z).Size = FMath::RoundToFloat(F * 20) / 20; },
			[this, X, Z, Base]() { const FOTGTreeEdit* E = Edit.TreeEditAt(X, Z); const float S = E ? E->Size : 1.f; return FString::Printf(TEXT("%.1f m (%d%%)"), Base * S, FMath::RoundToInt(S * 100)); });
	}
	if (!Picked.bColourable)
		Row(TEXT("Leaf colour"), Text(TEXT("this model keeps its own colour"), 11, Soft));
	else
	{
		Slider(TEXT("Leaf colour"), FOTGTreeEdit::HueLo, FOTGTreeEdit::HueHi,
			[this, X, Z]() { const FOTGTreeEdit* E = Edit.TreeEditAt(X, Z); return E ? E->Hue : 0.f; },
			[this, X, Z](float F) { float H = FMath::RoundToFloat(F); if (FMath::Abs(H) < 2) H = 0; Edit.TreeEdit(X, Z).Hue = H; },
			[this, X, Z]() {
				const FOTGTreeEdit* E = Edit.TreeEditAt(X, Z);
				const int32 H = E ? FMath::RoundToInt(E->Hue) : 0;
				return H == 0 ? FString(TEXT("as it grows")) : H < -45 ? FString(TEXT("autumn red")) : H < -20 ? FString(TEXT("autumn gold")) : H < 0 ? FString(TEXT("yellower")) : FString(TEXT("bluer"));
			});
		Slider(TEXT("Leaf brightness"), FOTGTreeEdit::BrightLo, FOTGTreeEdit::BrightHi,
			[this, X, Z]() { const FOTGTreeEdit* E = Edit.TreeEditAt(X, Z); return E ? E->Bright : 1.f; },
			[this, X, Z](float F) { float B = FMath::RoundToFloat(F * 20) / 20; Edit.TreeEdit(X, Z).Bright = B; },
			[this, X, Z]() { const FOTGTreeEdit* E = Edit.TreeEditAt(X, Z); const float B = E ? E->Bright : 1.f; return FMath::IsNearlyEqual(B, 1.f) ? FString(TEXT("as it grows")) : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(B * 100)); });
	}
	V->AddSlot().AutoHeight().Padding(130, 6, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Take it out"), [this, X, Z]() {
			const FOTGTreeEdit* E = Edit.TreeEditAt(X, Z);
			if (E && E->bAdded) Edit.ClearTreeEdit(X, Z); else Edit.TreeEdit(X, Z).bRemoved = true;
			bPicked = false; Apply(); Status = TEXT("Tree taken out (not saved yet)"); Rebuild();
		}, 11)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(Picked.bAdded ? TEXT("As added") : TEXT("As it was"), [this, X, Z]() {
			const FOTGTreeEdit* E = Edit.TreeEditAt(X, Z);
			if (E && E->bAdded) { FOTGTreeEdit& M = Edit.TreeEdit(X, Z); M.Model.Empty(); M.Size = 1; M.Hue = 0; M.Bright = 1; }
			else Edit.ClearTreeEdit(X, Z);
			Apply(); Reselect();
			if (bPicked && !Picked.bRock) PickedBaseM = Picked.HeightM;
			Rebuild();
		}, 11)]
	];
	return V;
}

// The course editor's forests: picked (one, several or all), outlined on the course, and changed
// together - how many trees, their size, the trees they're planted with
TSharedRef<SWidget> SOTGTreeDesigner::ForestPanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight()[Text(TEXT("Forests"), 15, Gold, "Bold")];
	if (!Edit.Forests.Num())
	{
		V->AddSlot().AutoHeight()[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(470).Text(FText::FromString(
			TEXT("None yet. Mark a group of trees as a forest in the course editor (Play page: Edit this course, then Trees), and it's here to change in the game.")))];
		return V;
	}
	for (auto It = SelForests.CreateIterator(); It; ++It) if (!Edit.Forests.IsValidIndex(*It)) It.RemoveCurrent();
	V->AddSlot().AutoHeight()[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(470).Text(FText::FromString(
		TEXT("Pick one or more: they're outlined on the course, and what you change here changes them all.")))];
	TSharedRef<SWrapBox> Chips = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4));
	for (int32 i = 0; i < Edit.Forests.Num(); i++)
		Chips->AddSlot()[Button(Edit.Forests[i].Name, [this, i]() { if (!SelForests.Remove(i)) SelForests.Add(i); Rebuild(); }, 12, SelForests.Contains(i))];
	Chips->AddSlot()[Button(TEXT("All"), [this]() { for (int32 i = 0; i < Edit.Forests.Num(); i++) SelForests.Add(i); Rebuild(); }, 11)];
	Chips->AddSlot()[Button(TEXT("None"), [this]() { SelForests.Reset(); Rebuild(); }, 11)];
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Chips];
	if (!SelForests.Num()) return V;
	TArray<int32> Sel = SelForests.Array();
	Sel.Sort();
	const FOTGForest& F0 = Edit.Forests[Sel[0]];
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(Sel.Num() == 1 ? FString::Printf(TEXT("%s: %.1f ha"), *F0.Name, F0.AreaM2() / 10000.0)
			: FString::Printf(TEXT("%d forests together"), Sel.Num()), 13, Ink, "Bold")]
		// (the free view flown back to see it - or them - whole)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Button(Sel.Num() == 1 ? TEXT("Go to it") : TEXT("Go to them"), [this, Sel]() {
			TArray<FVector2D> Pts;
			for (int32 i : Sel) if (Edit.Forests.IsValidIndex(i)) Pts.Append(Edit.Forests[i].Pts);
			if (OnShowArea && Pts.Num()) OnShowArea(Pts);
		}, 11)]
	];
	// (a slider on all of them: it shows the first's, and sets every one's)
	auto Slide = [&](const FString& Label, float Lo, float Hi, float FOTGForest::* Field, TFunction<FString(float)> Fmt) {
		V->AddSlot().AutoHeight().Padding(18, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(112)[Text(Label, 12, Soft)]]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([this, Sel, Lo, Hi, Field]() { return Edit.Forests.IsValidIndex(Sel[0]) ? FMath::Clamp((Edit.Forests[Sel[0]].*Field - Lo) / (Hi - Lo), 0.f, 1.f) : 0.f; })
				.OnValueChanged_Lambda([this, Sel, Lo, Hi, Field](float X) { for (int32 i : Sel) if (Edit.Forests.IsValidIndex(i)) Edit.Forests[i].*Field = FMath::RoundToFloat((Lo + (Hi - Lo) * X) * 20) / 20; })
				.OnMouseCaptureEnd_Lambda([this]() { Apply(); })
				.OnControllerCaptureEnd_Lambda([this]() { Apply(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(140)[SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Ink).Text_Lambda([this, Sel, Field, Fmt]() { return FText::FromString(Edit.Forests.IsValidIndex(Sel[0]) ? Fmt(Edit.Forests[Sel[0]].*Field) : FString()); })]
			]
		];
	};
	Slide(TEXT("How many"), FOTGForest::AmountLo, FOTGForest::AmountHi, &FOTGForest::Amount,
		[](float X) { return FMath::IsNearlyEqual(X, 1.f) ? FString(TEXT("as mapped")) : X <= 0 ? FString(TEXT("none")) : FString::Printf(TEXT("x %g"), FMath::RoundToFloat(X * 10) / 10); });
	Slide(TEXT("Their size"), FOTGForest::SizeLo, FOTGForest::SizeHi, &FOTGForest::Size, [](float X) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(X * 100)); });
	// their trees: their own, or one of the Course builder's forests
	TArray<FString> Presets = { FString() };
	for (const FOTGForestPreset& P : LibForests) Presets.Add(P.Name);
	V->AddSlot().AutoHeight().Padding(18, 2, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(112)[Text(TEXT("Its trees"), 12, Soft)]]
		+ SHorizontalBox::Slot().FillWidth(1)[Dropdown(Presets, F0.Preset.IsSet() ? F0.Preset.Name : FString(),
			[](const FString& K) { return K.IsEmpty() ? FString(TEXT("Their own (as mapped)")) : K; },
			[this, Sel](const FString& K) {
				const FOTGForestPreset* P = LibForests.FindByPredicate([&](const FOTGForestPreset& X) { return X.Name == K; });
				for (int32 i : Sel) if (Edit.Forests.IsValidIndex(i)) { Edit.Forests[i].Preset = P ? *P : FOTGForestPreset(); if (P) Edit.Forests[i].bFill = true; }
				Apply(); Rebuild();
			})]
	];
	if (F0.Preset.IsSet())
		V->AddSlot().AutoHeight().Padding(18, 4, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(112)[Text(TEXT("Planted out"), 12, Soft)]]
			+ SHorizontalBox::Slot().AutoWidth()[Button(F0.bFill ? TEXT("Yes: to its spacing") : TEXT("No: the trees there only"), [this, Sel]() {
				const bool bTo = !Edit.Forests[Sel[0]].bFill;
				for (int32 i : Sel) if (Edit.Forests.IsValidIndex(i)) Edit.Forests[i].bFill = bTo;
				Apply(); Rebuild();
			}, 12, F0.bFill)]
		];
	return V;
}

void SOTGTreeDesigner::Apply()
{
	if (!Course.IsValid()) return;
	Course->SetTreeDesign(Edit);
	if (OnChanged) OnChanged();
	Status = TEXT("Not saved yet");
}

void SOTGTreeDesigner::Rebuild()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[Text(TEXT("Trees and rocks"), 18, Ink, "Bold")]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(8, 0, 0, 2)[Text(TEXT("seen at once  ·  R: close"), 12, Soft)]
	];
	if (!Course.IsValid() || !Species.Num())
	{
		V->AddSlot().AutoHeight()[Text(TEXT("This course has no trees to design (a course pack, or still loading)."), 12, Soft)];
		Body->SetContent(Box(V, FMargin(14, 10), Solid));
		return;
	}
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	// a slider on a number in Edit (Get gives its address), the trees drawn again when it's let go
	auto Slider = [this](TSharedRef<SVerticalBox> To, const FString& Label, float Lo, float Hi, TFunction<float*(FOTGTreeDesign&)> Get, TFunction<FString(float)> Fmt) {
		To->AddSlot().AutoHeight().Padding(18, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(112)[Text(Label, 12, Soft)]]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([this, Lo, Hi, Get]() { return FMath::Clamp((*Get(Edit) - Lo) / (Hi - Lo), 0.f, 1.f); })
				.OnValueChanged_Lambda([this, Lo, Hi, Get](float X) { *Get(Edit) = FMath::RoundToFloat((Lo + (Hi - Lo) * X) * 20) / 20; })
				.OnMouseCaptureEnd_Lambda([this]() { Apply(); Reselect(); })
				.OnControllerCaptureEnd_Lambda([this]() { Apply(); Reselect(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(140)[SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Ink).Text_Lambda([this, Get, Fmt]() { return FText::FromString(Fmt(*Get(Edit))); })]
			]
		];
	};
	auto Times = [](float F) { return F < 0.025f ? FString(TEXT("none")) : FMath::IsNearlyEqual(F, 1.f) ? FString(TEXT("as mapped")) : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(F * 100)); };
	auto SizeFmt = [](float F) { return FMath::IsNearlyEqual(F, 1.f) ? FString(TEXT("as mapped")) : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(F * 100)); };

	// one at a time: what a click on the course does
	List->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Text(TEXT("Click on the course to"), 15, Gold, "Bold")];
	{
		TSharedRef<SHorizontalBox> Tools = SNew(SHorizontalBox);
		const TCHAR* Names[] = { TEXT("Pick one"), TEXT("Add a tree"), TEXT("Add a rock"), TEXT("Take out") };
		for (int32 t = 0; t < 4; t++)
			Tools->AddSlot().AutoWidth().Padding(0, 0, 4, 0)[Button(Names[t], [this, t]() { Tool = t; Rebuild(); }, 12, Tool == t)];
		List->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Tools];
	}
	if (Tool == 1)
	{
		List->AddSlot().AutoHeight().Padding(18, 4, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(112)[Text(TEXT("Species"), 12, Soft)]]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&SpeciesOpts)
				.InitiallySelectedItem(AddKind)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> P) { return StaticCastSharedRef<SWidget>(Text(FOTGTreeDesign::SpeciesLabel(*P), 12)); })
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> P, ESelectInfo::Type) { if (P.IsValid()) AddKind = P; })
				[SNew(STextBlock).Font(Font(12)).Text_Lambda([this]() { return FText::FromString(AddKind.IsValid() ? FOTGTreeDesign::SpeciesLabel(*AddKind) : FString()); })]
			]
		];
		Slider(List, TEXT("Height"), FOTGTreeEdit::HeightLo, FOTGTreeEdit::HeightHi, [this](FOTGTreeDesign&) { return &AddHeight; }, [](float F) { return FString::Printf(TEXT("%.1f m"), F); });
	}
	else if (Tool == 2)
		Slider(List, TEXT("Size"), FOTGRockEdit::SizeLo, FOTGRockEdit::SizeHi, [this](FOTGTreeDesign&) { return &AddRock; }, [](float F) { return FString::Printf(TEXT("%.1f m across"), F); });
	List->AddSlot().AutoHeight().Padding(18, 2, 0, 0)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(450).Text(FText::FromString(
		Tool == 0 ? TEXT("Click a tree or a rock to change it. Fly with F (W A S D, right mouse to look) to get close.")
		: Tool == 1 ? TEXT("Click the ground where it should stand (anywhere you like). The ball meets it too.")
		: Tool == 2 ? TEXT("Click the ground where it should lie.")
		: TEXT("Click a tree or a rock to take it out.")))];
	if (bPicked) List->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[PickPanel()];

	List->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[Text(TEXT("All trees"), 15, Gold, "Bold")];
	Slider(List, TEXT("Size"), 0.5f, 1.8f, [](FOTGTreeDesign& D) { return &D.AllSize; }, SizeFmt);
	Slider(List, TEXT("How many"), 0.f, 2.5f, [](FOTGTreeDesign& D) { return &D.AllDensity; }, Times);

	List->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[ForestPanel()];

	List->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[Text(TEXT("Species on this course"), 15, Gold, "Bold")];
	List->AddSlot().AutoHeight()[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(470)
		.Text(FText::FromString(TEXT("More than as mapped grows others round them - never on the fairways, greens, tees, sand, paths or water. The ball meets the trees as you see them.")))];
	for (const TPair<FString, int32>& Sp : Species)
	{
		const FString Kind = Sp.Key;
		FOTGSpeciesLook& L = Edit.Species.FindOrAdd(Kind);
		TSharedRef<SVerticalBox> Row = SNew(SVerticalBox);
		Row->AddSlot().AutoHeight().Padding(0, 8, 0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[Text(FOTGTreeDesign::SpeciesLabel(Kind), 14, Ink, "Bold")]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(6, 0, 0, 1)[Text(FString::Printf(TEXT("%d trees"), Sp.Value), 11, Soft)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)[Button(TEXT("Show me"), [this, Kind]() { if (OnShow) OnShow(Kind); }, 11)]
		];
		// the model it's drawn with: the next one at each click
		Row->AddSlot().AutoHeight().Padding(18, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(112)[Text(TEXT("Drawn as"), 12, Soft)]]
			+ SHorizontalBox::Slot().FillWidth(1)[Dropdown(FOTGTreeDesign::Models(), L.Model,
				[](const FString& K) { return FOTGTreeDesign::ModelLabel(K); },
				[this, Kind](const FString& K) { Edit.Species.FindOrAdd(Kind).Model = K; Apply(); Rebuild(); })]
		];
		Slider(Row, TEXT("Size"), 0.4f, 2.f, [Kind](FOTGTreeDesign& D) { return &D.Species.FindOrAdd(Kind).Size; }, SizeFmt);
		Slider(Row, TEXT("How many"), 0.f, 3.f, [Kind](FOTGTreeDesign& D) { return &D.Species.FindOrAdd(Kind).Density; }, Times);
		List->AddSlot().AutoHeight()[Row];
	}

	List->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(SCheckBox).IsChecked(Edit.bRocks ? ECheckBoxState::Checked : ECheckBoxState::Unchecked).OnCheckStateChanged_Lambda([this](ECheckBoxState St) { Edit.bRocks = St == ECheckBoxState::Checked; Apply(); Rebuild(); })]
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(TEXT("Rocks"), 15, Gold, "Bold")]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)[Button(TEXT("Show me"), [this]() { if (OnShow) OnShow(TEXT("rock")); }, 11)]
	];
	if (Edit.bRocks)
	{
		// their look: the next at each click
		List->AddSlot().AutoHeight().Padding(18, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(112)[Text(TEXT("Look like"), 12, Soft)]]
			+ SHorizontalBox::Slot().FillWidth(1)[Dropdown(FOTGTreeDesign::RockModels(), Edit.RockModel,
				[](const FString& K) { return FOTGTreeDesign::RockModelLabel(K); },
				[this](const FString& K) { Edit.RockModel = K; Apply(); Rebuild(); })]
		];
		Slider(List, TEXT("Size"), 0.4f, 2.5f, [](FOTGTreeDesign& D) { return &D.RockSize; }, SizeFmt);
		Slider(List, TEXT("How many"), 0.f, 1.f, [](FOTGTreeDesign& D) { return &D.RockDensity; }, Times);
	}
	// (the list's scroll kept: a change rebuilds it, which would start it at the top again)
	const float ScrolledTo = Scroll.IsValid() ? Scroll->GetScrollOffset() : 0.f;
	V->AddSlot().AutoHeight()[SNew(SBox).MaxDesiredHeight(600)[SAssignNew(Scroll, SScrollBox) + SScrollBox::Slot()[List]]];
	Scroll->SetScrollOffset(ScrolledTo);

	V->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Save for this course"), [this]() {
			FString Where;
			const bool bOk = Course.IsValid() && Course->SaveTreeDesign(&Where);
			Status = bOk ? FString(TEXT("Saved: the course looks like this from now on")) : FString(TEXT("Couldn't save ")) + Where;
		}, 13, true)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("As saved"), [this]() {
			if (!Course.IsValid()) return;
			Edit = Course->SavedTreeDesign();
			Apply();
			Reselect();
			Status = TEXT("Back as it was saved");
			Rebuild();
		}, 12)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("As mapped"), [this]() {
			Edit = FOTGTreeDesign();
			Apply();
			Reselect();
			Status = TEXT("As mapped (not saved yet)");
			Rebuild();
		}, 12)]
	];
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
	[
		SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).Text_Lambda([this]() {
			const int32 T = Course.IsValid() ? Course->TreesDrawn() : 0, R = Course.IsValid() ? Course->RocksDrawn() : 0;
			return FText::FromString(FString::Printf(TEXT("%s%s%d trees, %d rocks"), *Status, Status.IsEmpty() ? TEXT("") : TEXT("  ·  "), T, R));
		})
	];
	Body->SetContent(Box(V, FMargin(14, 10), Solid));
}
