// The course editor's panels (see SOTGCourseEditor.h): the top bar, the course, the hole, the shape,
// the selection, trees, plants, grass and buildings.

#include "UI/Editor/SOTGCourseEditor.h"

#include "UI/OTGUi.h"
#include "Course/OTGTextures.h"
#include "Course/OTGCourseData.h"
#include "Course/OTGCourseSubsystem.h"
#include "App/OTGHelperSubsystem.h"
#include "World/OTGTreeDesign.h"
#include "Course/OTGQuickUpdate.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

using namespace otg::ui;

#include "UI/Editor/SOTGEditorShared.h"

void SOTGCourseEditor::Rebuild()
{
	PanelBuilt = PanelKey;
	if (Top.IsValid()) Top->SetContent(TopBar());
	if (!Side.IsValid()) return;
	if (!bLoaded) { Side->SetContent(SNullWidget::NullWidget); return; }
	// the tabs: one section at a time, no scrolling past the others
	static const TCHAR* Names[] = { TEXT("Hole"), TEXT("Shapes"), TEXT("Trees"), TEXT("Tall grass"), TEXT("Course"), TEXT("Plants"), TEXT("Buildings") };
	TSharedRef<SWrapBox> Tabs = SNew(SWrapBox).UseAllottedSize(true);
	for (int32 t = 0; t < UE_ARRAY_COUNT(Names); t++)
		Tabs->AddSlot().Padding(0, 0, 4, 4)[Button(Names[t], [this, t]() { Tab = t; Changed(); }, 13, Tab == t)];
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	auto Add = [&](TSharedRef<SWidget> W) { V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[W]; };
	// (what's picked on the map, over the tab: only while something is)
	const bool bPicked = Src.Shapes.IsValidIndex(SelShape) || Src.UserTrees.IsValidIndex(SelTree) || Found.IsValidIndex(SelFound) || SelMany.Num()
		|| Src.RemovedTrees.IsValidIndex(SelRemoved) || Buildings.IsValidIndex(SelBuilding);
	if (bPicked) Add(SelectionPanel());
	switch (Tab)
	{
	case 0: Add(HolePanel()); break;
	case 1: Add(ShapePanel()); break;
	case 2: Add(ForestPanel()); Add(TreePanel()); break;
	case 3: Add(BiomePanel()); Add(GrassPanel()); break;
	case 4: Add(CoursePanel()); break;
	case 5: Add(PlantPanel()); break;
	default: Add(BuildingPanel()); break;
	}
	if (!bPicked)
		Add(Note(TEXT("Click something on the map to pick it: a shape, a tree, a building. Shift-drag a box round found trees. The mouse wheel zooms; drag (or right-drag) moves the map.")));
	Side->SetContent(SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[Box(Tabs, FMargin(8, 8, 4, 4), Solid)]
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot()[V]]);
}

TSharedRef<SWidget> SOTGCourseEditor::TopBar()
{
	auto Hint = [this]() -> FString {
		switch (Mode)
		{
		case EMode::Place: return FString::Printf(TEXT("Click the map to place: %s %s  (Esc to cancel)"), *ModeKind, *ModeKey);
		case EMode::Draw: return FString::Printf(TEXT("Drawing a %s: click to add points, double-click or Enter to finish, Backspace takes the last off, Esc cancels"), *KindLabel(ModeKind).ToLower());
		case EMode::AddTree: return TEXT("Click the map to add trees, one per click  (Esc when done)");
		case EMode::Cut: return FString::Printf(TEXT("Remove trees: click or drag over them (%.0f m wide)  (Esc when done)"), CutR * 2);
		case EMode::Paint: return FString::Printf(TEXT("Paint tall grass: hold the button and drag (%.0f m wide); right-drag moves the map  (Esc when done)"), BrushR * 2);
		case EMode::Erase: return FString::Printf(TEXT("Rub tall grass out: hold the button and drag (%.0f m wide)  (Esc when done)"), BrushR * 2);
		case EMode::Forest: return TEXT("Click inside a group of trees: they're marked as one forest  (Esc to cancel)");
		default: return Status;
		}
	};
	return Box(SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 16, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Text(bLoaded ? Src.Name : Id, 18, Ink, "Bold")]
			+ SVerticalBox::Slot().AutoHeight()[Text(bDirty ? TEXT("● unsaved changes") : TEXT("Course editor"), 11, bDirty ? Gold : Soft)]
		]
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Mode != EMode::None ? Gold : Soft).AutoWrapText(true).Text(FText::FromString(Hint()))]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[Button(TEXT("Undo"), [this]() { Undo(); }, 12)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[Button(TEXT("Hole"), [this]() { FitHole(); }, 12)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[Button(TEXT("Course"), [this]() { FitAll(); }, 12)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[Button(bShowPhoto && PhotoBrush.IsValid() ? TEXT("Photo") : TEXT("Shading"), [this]() { bShowPhoto = !bShowPhoto; Changed(); }, 12)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 4, 0)[SNew(SBox).ToolTipText(FText::FromString(TEXT("Keep your changes (the course's source). The game doesn't see them until you update it.")))[Button(TEXT("Save"), [this]() { Save(0); }, 14, bDirty)]]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[SNew(SBox).ToolTipText(FText::FromString(TEXT("Save, then patch the course the game plays in a few seconds: shapes, trees, tall grass, the ground. Anything it can't patch is named afterwards: that needs Put in the game (full).")))[Button(bQuicking ? TEXT("Updating the game…") : TEXT("Save and update the game"), [this]() { if (!bQuicking && !bExporting) Save(1); }, 14, !bQuicking && !bExporting)]]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[SNew(SBox).ToolTipText(FText::FromString(TEXT("Save, then rebuild the whole course for the game from scratch (5 to 15 minutes, in the background): needed the first time, and for anything the quick update can't do.")))[Button(bExporting ? TEXT("Putting it in the game…") : TEXT("Put in the game (full)"), [this]() { if (!bExporting && !bQuicking) Save(2); }, 12)]]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)[Button(bDirty && Status == TEXT("Close without saving?") ? TEXT("Yes, close") : TEXT("Close"), [this]() {
			if (bDirty && Status != TEXT("Close without saving?")) { Status = TEXT("Close without saving?"); Changed(); return; }
			if (OnClose) OnClose(bExported);
		}, 13)], FMargin(14, 8), Solid);
}

TSharedRef<SWidget> SOTGCourseEditor::CoursePanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Name"), SNew(SEditableTextBox).Text(FText::FromString(Src.Name)).Font(Font(13))
		.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { if (T.ToString() != Src.Name) { Change(); Src.Name = T.ToString(); Changed(); } }))];
	V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Location"), SNew(SEditableTextBox).Text(FText::FromString(Src.Location)).Font(Font(13))
		.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { if (T.ToString() != Src.Location) { Change(); Src.Location = T.ToString(); Changed(); } }))];
	V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Green speed (stimp)"), SNew(SBox).WidthOverride(100)[SNew(SSpinBox<double>).MinValue(6).MaxValue(14).Delta(0.5).Value(Src.Stimp)
		.OnValueCommitted_Lambda([this](double X, ETextCommit::Type) { if (X != Src.Stimp) { Change(); Src.Stimp = FMath::Clamp(X, 6.0, 14.0); Changed(); } })])];
	auto Look = [this, &V](const FString& Label, bool* Photo) {
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(Label, Button(*Photo ? TEXT("Aerial photo colours") : TEXT("Game colours (even, groomed)"), [this, Photo]() { Change(); *Photo = !*Photo; Changed(); }, 12))];
	};
	Look(TEXT("Fairways & tees"), &Src.bPhotoFairway);
	Look(TEXT("Greens"), &Src.bPhotoGreen);
	Look(TEXT("Rough"), &Src.bPhotoRough);
	auto Slide = [this, &V](const FString& Label, float* Val, float Lo, float Hi, TFunction<FString(float)> Fmt) {
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(Label, SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1)[SNew(SSlider).Value_Lambda([Val, Lo, Hi]() { return (*Val - Lo) / (Hi - Lo); })
				.OnMouseCaptureBegin_Lambda([this]() { Change(); })
				.OnValueChanged_Lambda([Val, Lo, Hi](float X) { *Val = FMath::RoundToFloat((Lo + (Hi - Lo) * X) * 20) / 20; })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)[SNew(SBox).WidthOverride(60)[SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Ink).Text_Lambda([Val, Fmt]() { return FText::FromString(Fmt(*Val)); })]])];
	};
	Slide(TEXT("Rough height"), &Src.RoughScale, 0.3f, 1.6f, [](float S) { return FString::Printf(TEXT("%d cm"), FOTGCourseSource::RoughCm(S)); });
	Slide(TEXT("Tall grass height"), &Src.NativeScale, 0.4f, 1.8f, [](float S) { return FString::Printf(TEXT("%d cm"), FOTGCourseSource::NativeCm(S)); });
	auto Mow = [this, &V](const FString& Label, FString* Id, double WidthM) {
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(Label, SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Button(FOTGCourseSource::MowLabel(*Id), [this, Id]() { Change(); *Id = Next(FOTGCourseSource::MowPatterns(), *Id); Changed(); }, 12)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[SNew(SOTGMowSwatch).Pattern(*Id).WidthM(WidthM)])];
	};
	Mow(TEXT("Fairway mowing"), &Src.MowFairway, 3.5);
	Mow(TEXT("Green mowing"), &Src.MowGreen, 1);
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Note(TEXT("The game's grass designer (R in a round) sets what the rough and tall grass are made of."))];
	return Section(TEXT("Course"), V);
}

TSharedRef<SWidget> SOTGCourseEditor::HolePanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	// the holes: one to edit, a new one, this one gone
	TSharedRef<SWrapBox> Btns = SNew(SWrapBox).UseAllottedSize(true);
	for (int32 i = 0; i < Src.Holes.Num(); i++)
		Btns->AddSlot().Padding(0, 0, 4, 4)[Button(FString::FromInt(Src.Holes[i].Number), [this, i]() { HoleI = i; SetMode(EMode::None); FitHole(); Changed(); }, 12, i == HoleI)];
	V->AddSlot().AutoHeight()[Btns];
	V->AddSlot().AutoHeight().Padding(0, 2, 0, 6)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("+ Add hole"), [this]() {
			Change();
			FOTGSourceHole H;
			for (const FOTGSourceHole& O : Src.Holes) H.Number = FMath::Max(H.Number, O.Number + 1);
			const FVector2D C(Canvas->Cx, Canvas->Cz);
			H.Green = C + FVector2D(0, 120);
			H.Tees.Add(TEXT("White"), C - FVector2D(0, 120));
			AutoPins(H);
			Src.Holes.Add(H);
			HoleI = Src.Holes.Num() - 1;
			Status = TEXT("A new hole in the middle of the map: place its tees, green and pins");
			Changed();
		}, 12)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(Status == TEXT("Delete this hole?") ? TEXT("Yes, delete it") : TEXT("Delete hole"), [this]() {
			if (!Src.Holes.IsValidIndex(HoleI)) return;
			if (Status != TEXT("Delete this hole?")) { Status = TEXT("Delete this hole?"); Changed(); return; }
			Change();
			Src.Holes.RemoveAt(HoleI);
			HoleI = FMath::Clamp(HoleI, 0, FMath::Max(0, Src.Holes.Num() - 1));
			Status.Empty();
			Changed();
		}, 12)]
	];
	if (!Src.Holes.IsValidIndex(HoleI)) { V->AddSlot().AutoHeight()[Note(TEXT("No holes yet: add one."))]; return Section(TEXT("Hole"), V); }
	FOTGSourceHole& H = Src.Holes[HoleI];
	auto Num = [this](const FString& Label, int32 Val, int32 Lo, int32 Hi, TFunction<void(int32)> Set) {
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)[Text(Label, 12, Soft)]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(60)[SNew(SSpinBox<int32>).MinValue(Lo).MaxValue(Hi).Value(Val)
				.OnValueCommitted_Lambda([Set](int32 X, ETextCommit::Type) { Set(X); })]];
	};
	V->AddSlot().AutoHeight().Padding(0, 2, 0, 6)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Num(TEXT("Hole #"), H.Number, 1, 36, [this](int32 X) {
			if (!Src.Holes.IsValidIndex(HoleI) || X == Src.Holes[HoleI].Number) return;
			if (Src.Holes.ContainsByPredicate([X](const FOTGSourceHole& O) { return O.Number == X; })) { Status = FString::Printf(TEXT("Hole %d already exists"), X); Changed(); return; }
			Change(); Src.Holes[HoleI].Number = X; Changed();
		})]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Num(TEXT("Par"), H.Par, 3, 6, [this](int32 X) { if (Src.Holes.IsValidIndex(HoleI) && X != Src.Holes[HoleI].Par) { Change(); Src.Holes[HoleI].Par = X; Changed(); } })]
		+ SHorizontalBox::Slot().AutoWidth()[Num(TEXT("Handicap"), FMath::Max(1, H.Index), 1, 18, [this](int32 X) { if (Src.Holes.IsValidIndex(HoleI) && X != Src.Holes[HoleI].Index) { Change(); Src.Holes[HoleI].Index = X; Changed(); } })]
	];
	// a point: where it is, Place, and a cross to take it away
	auto PointRow = [this, &V](const FLinearColor& Swatch, const FString& Name, const FString& Val, const FString& Kind, const FString& Key, bool bRemovable) {
		const bool bPlacing = Mode == EMode::Place && ModeKind == Kind && ModeKey == Key;
		TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[SNew(SBox).WidthOverride(12).HeightOverride(12)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Swatch)]]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(110)[Text(Name, 12, Ink)]]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(Val, 12, Soft)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 1)[Button(TEXT("Place"), [this, Kind, Key, bPlacing]() { SetMode(bPlacing ? EMode::None : EMode::Place, Kind, Key); }, 11, bPlacing)];
		if (bRemovable)
			R->AddSlot().AutoWidth().Padding(2, 1)[Button(TEXT("✕"), [this, Kind, Key]() {
				if (!Src.Holes.IsValidIndex(HoleI)) return;
				FOTGSourceHole& Ho = Src.Holes[HoleI];
				if (Kind == TEXT("tee") && Ho.Tees.Num() <= 1) { Status = TEXT("A hole needs at least one tee"); Changed(); return; }
				Change();
				if (Kind == TEXT("tee")) Ho.Tees.Remove(Key); else if (Kind == TEXT("aim") && Ho.Aims.IsValidIndex(FCString::Atoi(*Key))) Ho.Aims.RemoveAt(FCString::Atoi(*Key));
				Changed();
			}, 11)];
		V->AddSlot().AutoHeight().Padding(0, 1)[R];
	};
	auto Teal = [](const FString& T) {
		static const TMap<FString, FLinearColor> C = { { TEXT("Black"), FLinearColor(0.02f, 0.02f, 0.02f) }, { TEXT("Blue"), FLinearColor(0.03f, 0.12f, 0.8f) }, { TEXT("White"), FLinearColor::White },
			{ TEXT("Green"), FLinearColor(0.02f, 0.4f, 0.03f) }, { TEXT("Yellow"), FLinearColor(0.85f, 0.6f, 0.02f) }, { TEXT("Red"), FLinearColor(0.72f, 0.02f, 0.02f) },
			{ TEXT("Junior"), FLinearColor(1, 0.25f, 0) }, { TEXT("Par3"), FLinearColor(0.32f, 0.32f, 0.32f) } };
		return C.Contains(T) ? C[T] : FLinearColor::White;
	};
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 2)[Text(TEXT("Tees"), 13, Gold, "Bold")];
	for (const FString& T : FOTGCourseSource::TeesOrder())
	{
		const FVector2D* P = H.Tees.Find(T);
		FString Yd = TEXT("not set");
		if (P) { TArray<FVector2D> L = { *P }; L.Append(H.Aims); L.Add(H.Green); double D = 0; for (int32 k = 1; k < L.Num(); k++) D += FVector2D::Distance(L[k - 1], L[k]); Yd = FString::Printf(TEXT("%d yd"), FMath::RoundToInt(D * MToYd)); }
		PointRow(Teal(T), T, Yd, TEXT("tee"), T, P != nullptr);
	}
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 2)[Text(TEXT("Aim points (doglegs)"), 13, Gold, "Bold")];
	for (int32 k = 0; k < 2; k++) PointRow(FLinearColor(1, 0.7f, 0.07f), FString::Printf(TEXT("Aim %d"), k + 1), H.Aims.IsValidIndex(k) ? TEXT("set") : TEXT("not set"), TEXT("aim"), FString::FromInt(k), H.Aims.IsValidIndex(k));
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 2)[Text(TEXT("Green and pins"), 13, Gold, "Bold")];
	PointRow(FLinearColor(0.6f, 0.6f, 0.6f), TEXT("Green centre"), TEXT("set"), TEXT("green"), TEXT("0"), false);
	for (const FString& D : FOTGCourseSource::PinDays()) PointRow(FLinearColor(0.8f, 0.02f, 0.03f), D + TEXT(" pin"), H.Pins.Contains(D) ? TEXT("set") : TEXT("not set"), TEXT("pin"), D, false);
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Button(TEXT("Spread pins across green"), [this]() { if (Src.Holes.IsValidIndex(HoleI)) { Change(); AutoPins(Src.Holes[HoleI]); Changed(); } }, 12)];
	return Section(FString::Printf(TEXT("Hole %d  ·  par %d"), H.Number, H.Par), V);
}

TSharedRef<SWidget> SOTGCourseEditor::ShapePanel()
{
	TSharedRef<SWrapBox> W = SNew(SWrapBox).UseAllottedSize(true);
	for (const FString& K : DrawKinds)
	{
		const bool bOn = Mode == EMode::Draw && ModeKind == K;
		W->AddSlot().Padding(0, 0, 4, 4)[Button(KindLabel(K), [this, K, bOn]() { SetMode(bOn ? EMode::None : EMode::Draw, K); }, 12, bOn)];
	}
	return Section(TEXT("Draw a shape"), SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[W]
		+ SVerticalBox::Slot().AutoHeight()[Note(TEXT("Click a shape on the map to pick it: drag its corners, drag a corner's middle for a new one, drag inside it to move it."))]);
}

TSharedRef<SWidget> SOTGCourseEditor::SelectionPanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	if (Src.UserTrees.IsValidIndex(SelTree))
	{
		FOTGSourceTree& T = Src.UserTrees[SelTree];
		static const TArray<FString> Sizes = { TEXT("small"), TEXT("medium"), TEXT("large") };
		TArray<FString> Kinds = FOTGCourseSource::Mixes(); Kinds.Append(FOTGCourseSource::Species());
		V->AddSlot().AutoHeight()[Row(TEXT("Size"), Button(T.Size, [this]() { Change(); Src.UserTrees[SelTree].Size = Next(Sizes, Src.UserTrees[SelTree].Size); Changed(); }, 12))];
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Species"), Button(T.Kind == TEXT("auto") || FOTGCourseSource::Mixes().Contains(T.Kind) ? FOTGCourseSource::MixLabel(T.Kind) : FOTGTreeDesign::SpeciesLabel(T.Kind),
			[this, Kinds]() { Change(); Src.UserTrees[SelTree].Kind = Next(Kinds, Src.UserTrees[SelTree].Kind); Changed(); }, 12))];
		V->AddSlot().AutoHeight()[Note(TEXT("Drag it to move it."))];
		V->AddSlot().AutoHeight().Padding(0, 4)[Button(TEXT("Delete tree"), [this]() { DeleteSelected(); }, 12)];
		return Section(TEXT("Your tree"), V);
	}
	if (Buildings.IsValidIndex(SelBuilding))
	{
		const FBuilding& B = Buildings[SelBuilding];
		const FString* T = Src.BuildingTypes.Find(B.Key);
		FString Label = TEXT("The game's guess");
		for (const auto& BT : BuildingTypes()) if (T && BT.Key == *T) Label = BT.Value;
		V->AddSlot().AutoHeight()[Row(TEXT("It's a"), Button(Label, [this]() {
			Change();
			const FString Key = Buildings[SelBuilding].Key;
			const FString* Cur = Src.BuildingTypes.Find(Key);
			int32 I = INDEX_NONE;
			for (int32 k = 0; k < BuildingTypes().Num(); k++) if (Cur && BuildingTypes()[k].Key == *Cur) I = k;
			// (the guess, then each type)
			if (I + 1 >= BuildingTypes().Num()) Src.BuildingTypes.Remove(Key); else Src.BuildingTypes.Add(Key, BuildingTypes()[I + 1].Key);
			Changed();
		}, 12))];
		V->AddSlot().AutoHeight()[Note(FString::Printf(TEXT("%s%s about %d m²%s"), *B.Name, B.Name.IsEmpty() ? TEXT("") : TEXT(" ·"), FMath::RoundToInt(B.Area), B.Kind.IsEmpty() || B.Kind == TEXT("yes") ? TEXT("") : *(TEXT(" · mapped as ") + B.Kind)))];
		return Section(TEXT("Building"), V);
	}
	if (SelMany.Num())
	{
		V->AddSlot().AutoHeight()[Note(FString::Printf(TEXT("%d found trees picked. Shift-click a tree to add or drop it."), SelMany.Num()))];
		V->AddSlot().AutoHeight().Padding(0, 4)[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(FString::Printf(TEXT("Remove %d trees"), SelMany.Num()), [this]() { RemoveMany(); }, 12)]
			+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Clear"), [this]() { ClearPick(); Changed(); }, 12)]];
		return Section(TEXT("Trees"), V);
	}
	if (Found.IsValidIndex(SelFound))
	{
		const FFound& F = Found[SelFound];
		const FString Sp = FOTGTreeDesign::SpeciesLabel(F.Kind).ToLower();
		if (F.Design == 1 || F.Design == 3)
		{
			V->AddSlot().AutoHeight()[Note(F.Design == 1
				? FString::Printf(TEXT("A %s taken out in the game's tree designer (R in a round)."), *Sp)
				: FString::Printf(TEXT("A %s added in the game's tree designer (R in a round), about %d m across."), *Sp, FMath::RoundToInt(F.R * 2)))];
			const int32 I = SelFound;
			V->AddSlot().AutoHeight().Padding(0, 4)[Button(F.Design == 1 ? TEXT("Put it back") : TEXT("Take it out"), [this, I]() { DesignUndo(I); }, 12)];
			return Section(F.Design == 1 ? TEXT("Taken out in the game") : TEXT("Added in the game"), V);
		}
		if (F.Design == 2)
		{
			V->AddSlot().AutoHeight()[Note(FString::Printf(TEXT("A %s taken out by the game's tree designer: fewer of its species (How many, under R in a round)."), *Sp))];
			const int32 I = SelFound;
			V->AddSlot().AutoHeight().Padding(0, 4)[Button(TEXT("All of this species back"), [this, I]() { DesignUndo(I); }, 12)];
			return Section(TEXT("Taken out in the game"), V);
		}
		V->AddSlot().AutoHeight()[Note(FString::Printf(TEXT("A %s the game planted (from the tree-height map, OpenStreetMap or the aerial photo), about %d m across."), *FOTGTreeDesign::SpeciesLabel(F.Kind).ToLower(), FMath::RoundToInt(F.R * 2)))];
		V->AddSlot().AutoHeight().Padding(0, 4)[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Remove tree"), [this]() { RemoveFound(false); }, 12)]
			+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Replace with my own"), [this]() { RemoveFound(true); }, 12)]];
		if (Review.IsValidIndex(ReviewAt))
			V->AddSlot().AutoHeight().Padding(0, 4)[SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Text(FString::Printf(TEXT("In play: %d of %d"), ReviewAt + 1, Review.Num()), 12, Gold)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)[Button(TEXT("Previous"), [this]() { ReviewAt = FMath::Max(0, ReviewAt - 1); ShowReview(); }, 11)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)[Button(TEXT("Keep, next"), [this]() { ReviewAt++; if (!Review.IsValidIndex(ReviewAt)) { ReviewAt = INDEX_NONE; Status = TEXT("Review done"); } ShowReview(); }, 11)]
				+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Remove, next"), [this]() { RemoveFound(false); ReviewAt++; if (!Review.IsValidIndex(ReviewAt)) { ReviewAt = INDEX_NONE; Status = TEXT("Review done"); } ShowReview(); }, 11)]];
		return Section(TEXT("Found tree"), V);
	}
	if (Src.RemovedTrees.IsValidIndex(SelRemoved))
	{
		V->AddSlot().AutoHeight()[Note(TEXT("A tree the game found was taken out here."))];
		V->AddSlot().AutoHeight().Padding(0, 4)[Button(TEXT("Put it back"), [this]() { RestoreRemoved(); }, 12)];
		return Section(TEXT("Removed tree"), V);
	}
	if (!Src.Shapes.IsValidIndex(SelShape)) return SNullWidget::NullWidget;
	FOTGSourceShape& S = Src.Shapes[SelShape];
	TArray<FString> Kinds = DrawKinds; Kinds.Add(TEXT("trees"));
	V->AddSlot().AutoHeight()[Row(TEXT("It's a"), Button(KindLabel(S.Kind), [this, Kinds]() {
		Change();
		FOTGSourceShape& Sh = Src.Shapes[SelShape];
		Sh.Kind = Next(Kinds, Sh.Kind);
		if (Sh.Kind == TEXT("oob")) { const FOTGSourceShape Keep = Sh; Src.Shapes.RemoveAll([](const FOTGSourceShape& X) { return X.Kind == TEXT("oob"); }); Src.Shapes.Add(Keep); SelShape = Src.Shapes.Num() - 1; }
		Changed();
	}, 12))];
	if (S.Kind == TEXT("trees"))
	{
		static const TArray<FString> Dens = { TEXT("sparse"), TEXT("medium"), TEXT("dense") };
		TArray<FString> Sp = FOTGCourseSource::Mixes(); Sp.Append(FOTGCourseSource::Species());
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Tree density"), Button(S.Density, [this]() { Change(); Src.Shapes[SelShape].Density = Next(Dens, Src.Shapes[SelShape].Density); Changed(); }, 12))];
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Species"), Button(FOTGCourseSource::Mixes().Contains(S.Species) ? FOTGCourseSource::MixLabel(S.Species) : FOTGTreeDesign::SpeciesLabel(S.Species),
			[this, Sp]() { Change(); Src.Shapes[SelShape].Species = Next(Sp, Src.Shapes[SelShape].Species); Changed(); }, 12))];
		auto H = [this](double* Val) {
			return SNew(SBox).WidthOverride(70)[SNew(SSpinBox<double>).MinValue(2).MaxValue(40).Delta(1).Value(FMath::IsFinite(*Val) ? *Val : 0)
				.OnValueCommitted_Lambda([this, Val](double X, ETextCommit::Type) { Change(); *Val = X; Changed(); })];
		};
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Heights (m)"), SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[H(&S.HMin)] + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)[Text(TEXT("to"), 12, Soft)] + SHorizontalBox::Slot().AutoWidth()[H(&S.HMax)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0)[Button(TEXT("As measured"), [this]() { Change(); Src.Shapes[SelShape].HMin = Src.Shapes[SelShape].HMax = NAN; Changed(); }, 11)])];
	}
	if (S.Kind == TEXT("water") && !S.bInner)
	{
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Water"), Button(S.bFlow ? TEXT("A stream or river") : TEXT("A pond or lake"), [this]() { Change(); Src.Shapes[SelShape].bFlow = !Src.Shapes[SelShape].bFlow; Changed(); }, 12))];
		V->AddSlot().AutoHeight()[Note(S.bFlow ? TEXT("Runs downhill: the water slopes with its banks.") : TEXT("Lies flat."))];
	}
	if (S.Kind == TEXT("penalty"))
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Inside it"), Button(S.bInnerOOB ? TEXT("Out of bounds") : TEXT("A penalty area"), [this]() { Change(); Src.Shapes[SelShape].bInnerOOB = !Src.Shapes[SelShape].bInnerOOB; Changed(); }, 12))];
	V->AddSlot().AutoHeight().Padding(0, 4)[Note(FString::Printf(TEXT("%d points · %s m²%s"), S.Pts.Num(), *FText::AsNumber(FMath::RoundToInt(PolyArea(S.Pts))).ToString(), S.bInner ? TEXT(" · a cut-out") : TEXT("")))];
	V->AddSlot().AutoHeight().Padding(0, 4)[Button(TEXT("Delete shape"), [this]() { DeleteSelected(); }, 12)];
	return Section(TEXT("Shape"), V);
}

TSharedRef<SWidget> SOTGCourseEditor::TreePanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	static const TArray<FString> Sizes = { TEXT("small"), TEXT("medium"), TEXT("large") };
	static const TArray<FString> Dens = { TEXT("sparse"), TEXT("medium"), TEXT("dense") };
	TArray<FString> Kinds = FOTGCourseSource::Mixes(); Kinds.Append(FOTGCourseSource::Species());
	auto KLabel = [](const FString& K) { return FOTGCourseSource::Mixes().Contains(K) ? FOTGCourseSource::MixLabel(K) : FOTGTreeDesign::SpeciesLabel(K); };
	V->AddSlot().AutoHeight()[Row(TEXT("On the map"), SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(bShowTrees ? TEXT("Shown") : TEXT("Hidden"), [this]() { bShowTrees = !bShowTrees; Changed(); }, 12, bShowTrees)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Colour by species"), [this]() { bColourSpecies = !bColourSpecies; Changed(); }, 12, bColourSpecies)])];
	if (!Found.Num()) V->AddSlot().AutoHeight()[Note(TEXT("The trees the game plants show once the course is in the game (Save and put in the game)."))];
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Add trees"), [this]() { SetMode(Mode == EMode::AddTree ? EMode::None : EMode::AddTree); }, 12, Mode == EMode::AddTree)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TreeSize, [this]() { TreeSize = Next(Sizes, TreeSize); Changed(); }, 12)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(KLabel(TreeKind), [this, Kinds]() { TreeKind = Next(Kinds, TreeKind); Changed(); }, 12)]
	];
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Remove trees"), [this]() { SetMode(Mode == EMode::Cut ? EMode::None : EMode::Cut); }, 12, Mode == EMode::Cut)]
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SSlider).Value_Lambda([this]() { return float((CutR - 1) / 29.0); }).OnValueChanged_Lambda([this](float X) { CutR = FMath::RoundToDouble(1 + 29 * X); })]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.0f m"), CutR * 2)); })]
	];
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Draw tree area"), [this]() { const bool bOn = Mode == EMode::Draw && ModeKind == TEXT("trees"); SetMode(bOn ? EMode::None : EMode::Draw, TEXT("trees")); }, 12, Mode == EMode::Draw && ModeKind == TEXT("trees"))]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(AreaDensity, [this]() { AreaDensity = Next(Dens, AreaDensity); Changed(); }, 12)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(KLabel(AreaKind), [this, Kinds]() { AreaKind = Next(Kinds, AreaKind); Changed(); }, 12)]
	];
	auto H = [this](double* Val) { return SNew(SBox).WidthOverride(70)[SNew(SSpinBox<double>).MinValue(2).MaxValue(40).Delta(1).Value(*Val).OnValueCommitted_Lambda([Val](double X, ETextCommit::Type) { *Val = X; })]; };
	V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("New areas' heights"), SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[H(&AreaHMin)] + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)[Text(TEXT("to"), 12, Soft)] + SHorizontalBox::Slot().AutoWidth()[H(&AreaHMax)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)[Text(TEXT("m"), 12, Soft)])];
	if (Found.Num()) V->AddSlot().AutoHeight().Padding(0, 4, 0, 2)[Button(TEXT("Review trees in the line of play"), [this]() { StartReview(); }, 12)];
	// the species on this course: one turned off becomes others
	TMap<FString, int32> Count;
	for (const FFound& F : Found) Count.FindOrAdd(F.Kind)++;
	for (const FString& Off : Src.TreesOff) Count.FindOrAdd(Off);
	if (Count.Num())
	{
		int32 Off = 0;
		for (const auto& KV : Count) Off += Src.TreesOff.Contains(KV.Key) ? 1 : 0;
		V->AddSlot().AutoHeight().Padding(0, 8, 0, 2)[Button(FString::Printf(TEXT("%s Tree species on this course (%d%s)"), bSpeciesOpen ? TEXT("\u25BE") : TEXT("\u25B8"), Count.Num(),
			Off ? *FString::Printf(TEXT(", %d off"), Off) : TEXT("")), [this]() { bSpeciesOpen = !bSpeciesOpen; Changed(); }, 12)];
		if (!bSpeciesOpen) return Section(TEXT("Trees"), V);
		Count.ValueSort([](int32 A, int32 B) { return A > B; });
		TSharedRef<SWrapBox> W = SNew(SWrapBox).UseAllottedSize(true);
		for (const auto& KV : Count)
		{
			const FString K = KV.Key;
			const bool bOff = Src.TreesOff.Contains(K);
			W->AddSlot().Padding(0, 0, 4, 4)[Button(FString::Printf(TEXT("%s %d"), *FOTGTreeDesign::SpeciesLabel(K), KV.Value), [this, K]() { Change(); if (!Src.TreesOff.Remove(K)) Src.TreesOff.Add(K); Changed(); }, 11, !bOff)];
		}
		V->AddSlot().AutoHeight()[W];
		V->AddSlot().AutoHeight()[Note(TEXT("Lit: grown on this course. A species turned off has its trees become others."))];
	}
	return Section(TEXT("Trees"), V);
}

TSharedRef<SWidget> SOTGCourseEditor::PlantPanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight()[Row(TEXT("How many"), SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1)[SNew(SSlider).Value_Lambda([this]() { return Src.PlantAmount / 2.f; }).OnMouseCaptureBegin_Lambda([this]() { Change(); })
			.OnValueChanged_Lambda([this](float X) { Src.PlantAmount = FMath::RoundToFloat(X * 40) / 20; })]
		+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)[SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Ink).Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Src.PlantAmount * 100))); })])];
	FString Group;
	TSharedPtr<SWrapBox> W;
	for (const FOTGCourseSource::FPlant& P : FOTGCourseSource::Plants())
	{
		if (Group != P.Group)
		{
			Group = P.Group;
			V->AddSlot().AutoHeight().Padding(0, 6, 0, 2)[Text(Group, 12, Soft, "Bold")];
			V->AddSlot().AutoHeight()[SAssignNew(W, SWrapBox).UseAllottedSize(true)];
		}
		const FString Pid = P.Id;
		const bool bOff = Src.PlantsOff.Contains(Pid), bExtra = Src.PlantsExtra.Contains(Pid);
		W->AddSlot().Padding(0, 0, 4, 4)[Button(FString(P.Label) + (bOff ? TEXT(": off") : bExtra ? TEXT(": added") : TEXT("")), [this, Pid, bOff, bExtra]() {
			// (as the region has it, then off, then added)
			Change();
			if (!bOff && !bExtra) Src.PlantsOff.Add(Pid);
			else if (bOff) { Src.PlantsOff.Remove(Pid); Src.PlantsExtra.Add(Pid); }
			else Src.PlantsExtra.Remove(Pid);
			Changed();
		}, 11, bExtra)];
	}
	V->AddSlot().AutoHeight()[Note(TEXT("Each as the course's region grows it, turned off, or added. (Drawn by the web version's game; the Unreal game doesn't draw them yet.)"))];
	return Section(TEXT("Shrubs, flowers and grasses"), V);
}

TSharedRef<SWidget> SOTGCourseEditor::GrassPanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight()[Row(TEXT("On the map"), Button(bShowGrass ? TEXT("Shown") : TEXT("Hidden"), [this]() { bShowGrass = !bShowGrass; Changed(); }, 12, bShowGrass))];
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Paint tall grass"), [this]() { SetMode(Mode == EMode::Paint ? EMode::None : EMode::Paint); }, 12, Mode == EMode::Paint)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Rub out"), [this]() { SetMode(Mode == EMode::Erase ? EMode::None : EMode::Erase); }, 12, Mode == EMode::Erase)]
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SSlider).Value_Lambda([this]() { return float((BrushR - 0.5) / 29.5); }).OnValueChanged_Lambda([this](float X) { BrushR = FMath::RoundToDouble((0.5 + 29.5 * X) * 2) / 2; })]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.0f m"), BrushR * 2)); })]
	];
	V->AddSlot().AutoHeight()[Note(FString::Printf(TEXT("%d strokes (amber: painted, grey: rubbed out), on top of the tall grass the game finds in the aerial photo. Rubbed-out grass plays as ordinary rough."), Src.TallGrass.Num()))];
	return Section(TEXT("Tall grass"), V);
}

// The course's forests: a group of trees marked (a click) or drawn round, then thickened or thinned,
// made bigger or smaller, or given a forest from the studio - its species, planted out to its spacing
TSharedRef<SWidget> SOTGCourseEditor::ForestPanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	if (GameDir.IsEmpty())
	{
		V->AddSlot().AutoHeight()[Note(TEXT("Forests are kept with the course in the game: put it there first (Save and put in the game)."))];
		return Section(TEXT("Forests"), V);
	}
	if (!LibForests.Num()) LibForests = OTGLibrary::Forests();
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Mark a forest"), [this]() { SetMode(Mode == EMode::Forest ? EMode::None : EMode::Forest); }, 12, Mode == EMode::Forest)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Draw a forest"), [this]() { const bool bOn = Mode == EMode::Draw && ModeKind == TEXT("forest"); SetMode(bOn ? EMode::None : EMode::Draw, TEXT("forest")); }, 12, Mode == EMode::Draw && ModeKind == TEXT("forest"))]
	];
	V->AddSlot().AutoHeight()[Note(TEXT("Mark: click inside a group of trees and they become one forest. Then make it thicker or thinner, its trees bigger or smaller, or give it a forest made in New course > Forests."))];
	TSharedRef<SWrapBox> List = SNew(SWrapBox).UseAllottedSize(true);
	for (int32 i = 0; i < Design.Forests.Num(); i++)
		List->AddSlot().Padding(0, 0, 4, 4)[Button(Design.Forests[i].Name, [this, i]() { ClearPick(); SelForest = i; Changed(); }, 12, i == SelForest)];
	if (Design.Forests.Num()) V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[List];
	if (Design.Forests.IsValidIndex(SelForest))
	{
		const int32 I = SelForest;
		const FOTGForest& F = Design.Forests[I];
		int32 N = 0;
		for (const FFound& T : Found) N += F.Contains(T.P.X, T.P.Y) ? 1 : 0;
		V->AddSlot().AutoHeight().Padding(0, 6, 0, 2)[Text(FString::Printf(TEXT("%s: %d trees as mapped, %.1f ha"), *F.Name, N, F.AreaM2() / 10000.0), 13, Gold, "Bold")];
		// its trees: their own, or a forest's from the library
		TArray<FString> Presets = { FString() };
		for (const FOTGForestPreset& P : LibForests) Presets.Add(P.Name);
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Its trees"), Button(F.Preset.IsSet() ? F.Preset.Name : FString(TEXT("Their own (as mapped)")), [this, I, Presets]() {
			FOTGForest& G = Design.Forests[I];
			const FString Next = Presets[(Presets.IndexOfByKey(G.Preset.IsSet() ? G.Preset.Name : FString()) + 1) % Presets.Num()];
			const FOTGForestPreset* P = LibForests.FindByPredicate([&](const FOTGForestPreset& X) { return X.Name == Next; });
			G.Preset = P ? *P : FOTGForestPreset();
			SaveForests(P ? FString::Printf(TEXT("%s: %s"), *G.Name, *P->Name) : FString::Printf(TEXT("%s: its own trees"), *G.Name));
		}, 12))];
		auto Slide = [&](const FString& Label, float Lo, float Hi, float FOTGForest::* Field, TFunction<FString(float)> Fmt) {
			V->AddSlot().AutoHeight().Padding(0, 2)[Row(Label, SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
				[
					SNew(SSlider)
					.Value_Lambda([this, I, Lo, Hi, Field]() { return Design.Forests.IsValidIndex(I) ? (Design.Forests[I].*Field - Lo) / (Hi - Lo) : 0.f; })
					.OnValueChanged_Lambda([this, I, Lo, Hi, Field](float X) { if (Design.Forests.IsValidIndex(I)) Design.Forests[I].*Field = FMath::RoundToFloat((Lo + (Hi - Lo) * X) * 20) / 20; })
					.OnMouseCaptureEnd_Lambda([this, I]() { if (Design.Forests.IsValidIndex(I)) SaveForests(Design.Forests[I].Name + TEXT(" changed")); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
				[SNew(SBox).WidthOverride(90)[SNew(STextBlock).Font(Font(11, "Bold")).ColorAndOpacity(Ink).Text_Lambda([this, I, Field, Fmt]() { return FText::FromString(Design.Forests.IsValidIndex(I) ? Fmt(Design.Forests[I].*Field) : FString()); })]])];
		};
		Slide(TEXT("How many"), FOTGForest::AmountLo, FOTGForest::AmountHi, &FOTGForest::Amount, [](float X) { return FMath::IsNearlyEqual(X, 1.f) ? FString(TEXT("as mapped")) : X <= 0 ? FString(TEXT("none")) : FString::Printf(TEXT("x %.2g"), X); });
		Slide(TEXT("Their size"), FOTGForest::SizeLo, FOTGForest::SizeHi, &FOTGForest::Size, [](float X) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(X * 100)); });
		if (F.Preset.IsSet())
			V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Planted out"), Button(F.bFill ? FString::Printf(TEXT("Yes: %d a hectare"), FMath::RoundToInt(F.Preset.PerHa * F.Amount)) : FString(TEXT("No: the trees there only")), [this, I]() {
				Design.Forests[I].bFill = !Design.Forests[I].bFill;
				SaveForests(Design.Forests[I].bFill ? TEXT("Planted out to its spacing") : TEXT("Not planted out"));
			}, 12, F.bFill))];
		V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Button(TEXT("Delete this forest"), [this, I]() {
			Design.Forests.RemoveAt(I);
			SelForest = INDEX_NONE;
			SaveForests(TEXT("Forest deleted (its trees as they were)"));
		}, 12)];
		V->AddSlot().AutoHeight()[Note(TEXT("The game draws it as it is here the next time the course is played: no export needed. The ball meets its trees."))];
	}
	return Section(TEXT("Forests"), V);
}

// The course's grass biomes: its tall grass's, and areas drawn with their own (the studio's: New
// course > Grass biomes). The rough is left as it is: one even colour
TSharedRef<SWidget> SOTGCourseEditor::BiomePanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	if (GameDir.IsEmpty())
	{
		V->AddSlot().AutoHeight()[Note(TEXT("Grass biomes are kept with the course in the game: put it there first (Save and put in the game)."))];
		return Section(TEXT("Grass biomes"), V);
	}
	if (!LibBiomes.Num()) LibBiomes = OTGLibrary::Biomes();
	TArray<FString> Names = { FString() };
	for (const FOTGBiome& B : LibBiomes) Names.Add(B.Name);
	if (AreaBiome.IsEmpty() && LibBiomes.Num()) AreaBiome = LibBiomes[0].Name;
	V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("All its tall grass"), Button(Biomes.Default.IsEmpty() ? FString(TEXT("The course's own")) : Biomes.Default, [this, Names]() {
		Biomes.Default = Names[(Names.IndexOfByKey(Biomes.Default) + 1) % Names.Num()];
		if (const FOTGBiome* B = OTGLibrary::FindIn(LibBiomes, Biomes.Default)) Biomes.Use(*B);
		SaveBiomes(Biomes.Default.IsEmpty() ? FString(TEXT("The tall grass: the course's own")) : TEXT("The tall grass: ") + Biomes.Default);
	}, 12))];
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Draw an area"), [this]() { const bool bOn = Mode == EMode::Draw && ModeKind == TEXT("biome"); SetMode(bOn ? EMode::None : EMode::Draw, TEXT("biome")); }, 12, Mode == EMode::Draw && ModeKind == TEXT("biome"))]
		+ SHorizontalBox::Slot().AutoWidth()[Button(AreaBiome.IsEmpty() ? FString(TEXT("(no biomes yet)")) : AreaBiome, [this, Names]() {
			TArray<FString> L = Names; L.RemoveAt(0);
			if (L.Num()) AreaBiome = L[(L.IndexOfByKey(AreaBiome) + 1) % L.Num()];
			Changed();
		}, 12)]
	];
	V->AddSlot().AutoHeight()[Note(TEXT("An area grows its biome's grasses instead (where there's tall grass in it). The biomes are made in New course > Grass biomes, with a preview; the rough stays one even colour."))];
	TSharedRef<SWrapBox> List = SNew(SWrapBox).UseAllottedSize(true);
	for (int32 i = 0; i < Biomes.Areas.Num(); i++)
		List->AddSlot().Padding(0, 0, 4, 4)[Button(FString::Printf(TEXT("%d. %s"), i + 1, *Biomes.Areas[i].Biome), [this, i]() { ClearPick(); SelArea = i; Changed(); }, 12, i == SelArea)];
	if (Biomes.Areas.Num()) V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[List];
	if (Biomes.Areas.IsValidIndex(SelArea))
	{
		const int32 I = SelArea;
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Its biome"), Button(Biomes.Areas[I].Biome, [this, I, Names]() {
			TArray<FString> L = Names; L.RemoveAt(0);
			if (!L.Num()) return;
			const FString Next = L[(L.IndexOfByKey(Biomes.Areas[I].Biome) + 1) % L.Num()];
			if (const FOTGBiome* B = OTGLibrary::FindIn(LibBiomes, Next)) { Biomes.Use(*B); Biomes.Areas[I].Biome = Next; }
			SaveBiomes(TEXT("Area: ") + Next);
		}, 12))];
		V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Button(TEXT("Delete this area"), [this, I]() {
			Biomes.Areas.RemoveAt(I);
			SelArea = INDEX_NONE;
			SaveBiomes(TEXT("Area deleted"));
		}, 12)];
	}
	return Section(TEXT("Grass biomes"), V);
}

TSharedRef<SWidget> SOTGCourseEditor::BuildingPanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight()[Row(TEXT("On the map"), Button(bShowBuildings ? TEXT("Shown") : TEXT("Hidden"), [this]() { bShowBuildings = !bShowBuildings; Changed(); }, 12, bShowBuildings))];
	V->AddSlot().AutoHeight()[Note(FString::Printf(TEXT("%d buildings round the course. Click one to say what it is (it changes how the game draws it)."), Buildings.Num()))];
	return Section(TEXT("Buildings"), V);
}
