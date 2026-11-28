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
#include "Brushes/SlateColorBrush.h"
#include "Widgets/Input/SComboBox.h"
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
		case EMode::Forest: return SelMany.Num()
			? FString::Printf(TEXT("%d trees picked: shift-click or shift-drag picks more, Enter makes them a forest  (Esc to cancel)"), SelMany.Num())
			: FString(TEXT("Click inside a group of trees: they're marked as one forest. Or shift-click / shift-drag to pick trees, then Enter  (Esc to cancel)"));
		case EMode::Patch: return TEXT("Click inside a patch of tall grass: it becomes an area of the biome picked  (Esc to cancel)");
		case EMode::Join: return TEXT("Click the forest to join to the picked one  (Esc to cancel)");
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
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[SNew(SBox).WidthOverride(110)[Dropdown(bShowPhoto && PhotoBrush.IsValid(), TEXT("Shading"), TEXT("Photo"), [this](bool b) { bShowPhoto = b; Changed(); })]]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 4, 0)[SNew(SBox).ToolTipText(FText::FromString(TEXT("Keep your changes (the course's source). The game doesn't see them until you update it.")))[Button(TEXT("Save"), [this]() { AskSave(0); }, 14, bDirty)]]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[SNew(SBox).ToolTipText(FText::FromString(TEXT("Save, then patch the course the game plays in a few seconds: shapes, trees, tall grass, the ground. Anything it can't patch is named afterwards: that needs Put in the game (full).")))[Button(bQuicking ? TEXT("Updating the game…") : TEXT("Save and update the game"), [this]() { if (!bQuicking && !bExporting) AskSave(1); }, 14, !bQuicking && !bExporting)]]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[SNew(SBox).ToolTipText(FText::FromString(TEXT("Save, then rebuild the whole course for the game from scratch (5 to 15 minutes, in the background): needed the first time, and for anything the quick update can't do.")))[Button(bExporting ? TEXT("Putting it in the game…") : TEXT("Put in the game (full)"), [this]() { if (!bExporting && !bQuicking) AskSave(2); }, 12)]]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)[Button(TEXT("Close"), [this]() { AskClose(); }, 13)], FMargin(14, 8), Solid);
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
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(Label, Dropdown(*Photo, TEXT("Game colours (even, groomed)"), TEXT("Aerial photo colours"), [this, Photo](bool b) { if (b == *Photo) return; Change(); *Photo = b; Changed(); }))];
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
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(190)[Dropdown(FOTGCourseSource::MowPatterns(), *Id, [](const FString& P) { return FOTGCourseSource::MowLabel(P); }, [this, Id](const FString& P) { if (P == *Id) return; Change(); *Id = P; Changed(); })]]
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
		V->AddSlot().AutoHeight()[Row(TEXT("Size"), Dropdown(Sizes, T.Size, nullptr, [this](const FString& P) { Change(); Src.UserTrees[SelTree].Size = P; Changed(); }))];
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Species"), Dropdown(Kinds, T.Kind, [](const FString& K) { return K == TEXT("auto") || FOTGCourseSource::Mixes().Contains(K) ? FOTGCourseSource::MixLabel(K) : FOTGTreeDesign::SpeciesLabel(K); },
			[this](const FString& P) { Change(); Src.UserTrees[SelTree].Kind = P; Changed(); }))];
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
	V->AddSlot().AutoHeight()[Row(TEXT("It's a"), Dropdown(Kinds, S.Kind, [this](const FString& K) { return KindLabel(K); }, [this](const FString& P) {
		if (P == Src.Shapes[SelShape].Kind) return;
		Change();
		FOTGSourceShape& Sh = Src.Shapes[SelShape];
		Sh.Kind = P;
		if (Sh.Kind == TEXT("oob")) { const FOTGSourceShape Keep = Sh; Src.Shapes.RemoveAll([](const FOTGSourceShape& X) { return X.Kind == TEXT("oob"); }); Src.Shapes.Add(Keep); SelShape = Src.Shapes.Num() - 1; }
		Changed();
	}))];
	if (S.Kind == TEXT("trees"))
	{
		static const TArray<FString> Dens = { TEXT("sparse"), TEXT("medium"), TEXT("dense") };
		TArray<FString> Sp = FOTGCourseSource::Mixes(); Sp.Append(FOTGCourseSource::Species());
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Tree density"), Dropdown(Dens, S.Density, nullptr, [this](const FString& P) { Change(); Src.Shapes[SelShape].Density = P; Changed(); }))];
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Species"), Dropdown(Sp, S.Species, [](const FString& K) { return FOTGCourseSource::Mixes().Contains(K) ? FOTGCourseSource::MixLabel(K) : FOTGTreeDesign::SpeciesLabel(K); },
			[this](const FString& P) { Change(); Src.Shapes[SelShape].Species = P; Changed(); }))];
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
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Water"), Dropdown(S.bFlow, TEXT("A pond or lake"), TEXT("A stream or river"), [this](bool b) { if (b == Src.Shapes[SelShape].bFlow) return; Change(); Src.Shapes[SelShape].bFlow = b; Changed(); }))];
		V->AddSlot().AutoHeight()[Note(S.bFlow ? TEXT("Runs downhill: the water slopes with its banks.") : TEXT("Lies flat."))];
	}
	if (S.Kind == TEXT("penalty"))
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Inside it"), Dropdown(S.bInnerOOB, TEXT("A penalty area"), TEXT("Out of bounds"), [this](bool b) { if (b == Src.Shapes[SelShape].bInnerOOB) return; Change(); Src.Shapes[SelShape].bInnerOOB = b; Changed(); }))];
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
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Check(TEXT("Shown"), bShowTrees, [this](bool b) { bShowTrees = b; Changed(); })]
		+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)[Check(TEXT("Colour by species"), bColourSpecies, [this](bool b) { bColourSpecies = b; Changed(); })])];
	if (!Found.Num()) V->AddSlot().AutoHeight()[Note(TEXT("The trees the game plants show once the course is in the game (Save and put in the game)."))];
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Add trees"), [this]() { SetMode(Mode == EMode::AddTree ? EMode::None : EMode::AddTree); }, 12, Mode == EMode::AddTree)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Dropdown(Sizes, TreeSize, nullptr, [this](const FString& P) { TreeSize = P; Changed(); })]
		+ SHorizontalBox::Slot().AutoWidth()[Dropdown(Kinds, TreeKind, KLabel, [this](const FString& P) { TreeKind = P; Changed(); })]
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
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Dropdown(Dens, AreaDensity, nullptr, [this](const FString& P) { AreaDensity = P; Changed(); })]
		+ SHorizontalBox::Slot().AutoWidth()[Dropdown(Kinds, AreaKind, KLabel, [this](const FString& P) { AreaKind = P; Changed(); })]
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
	V->AddSlot().AutoHeight()[Row(TEXT("On the map"), Check(TEXT("Shown"), bShowGrass, [this](bool b) { bShowGrass = b; Changed(); }))];
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

// A choice from a list, dropped down (the biomes and forests made in the Course builder): the labels
// shown, the one picked now, and what's done with another (by its place in the list)
static TSharedRef<SWidget> Dropdown(const TArray<FString>& Labels, int32 Current, TFunction<void(int32)> OnPick)
{
	TSharedRef<TArray<TSharedPtr<FString>>> Opts = MakeShared<TArray<TSharedPtr<FString>>>();
	for (const FString& L : Labels) Opts->Add(MakeShared<FString>(L));
	const TSharedPtr<FString> Sel = Opts->IsValidIndex(Current) ? (*Opts)[Current] : nullptr;
	// (the options kept alive by the lambdas: the combo box holds only a pointer to them)
	return SNew(SComboBox<TSharedPtr<FString>>)
		.OptionsSource(&Opts.Get())
		.InitiallySelectedItem(Sel)
		.OnGenerateWidget_Lambda([Opts](TSharedPtr<FString> P) { return StaticCastSharedRef<SWidget>(SNew(STextBlock).Font(Font(12)).Text(FText::FromString(P.IsValid() ? *P : FString()))); })
		.OnSelectionChanged_Lambda([Opts, OnPick](TSharedPtr<FString> P, ESelectInfo::Type How) { if (P.IsValid() && How != ESelectInfo::Direct) OnPick(Opts->IndexOfByKey(P)); })
		[SNew(STextBlock).Font(Font(12)).Text(FText::FromString(Sel.IsValid() ? *Sel : FString(TEXT("(none yet)"))))];
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
	// (trees picked - shift-click or shift-drag: made a forest)
	if (SelMany.Num() >= 2)
		V->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(FString::Printf(TEXT("Make a forest of the %d trees picked"), SelMany.Num()), [this]() { const TArray<int32> Many = SelMany; ForestOf(Many); }, 12, true)]
			+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Clear the picks"), [this]() { SelMany.Reset(); Changed(); }, 12)]
		];
	V->AddSlot().AutoHeight()[Note(TEXT("Mark: click inside a group of trees and they become one forest. Or pick the trees yourself (shift-click them, or shift-drag a box round them; shift-click again puts one back) and make a forest of them. Then make it thicker or thinner, its trees bigger or smaller, or give it a forest made in Course builder > Forests."))];
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
		TArray<FString> PresetLabels = Presets;
		PresetLabels[0] = TEXT("Their own (as mapped)");
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Its trees"), Dropdown(PresetLabels, Presets.IndexOfByKey(F.Preset.IsSet() ? F.Preset.Name : FString()), [this, I, Presets](int32 K) {
			if (!Design.Forests.IsValidIndex(I) || !Presets.IsValidIndex(K)) return;
			FOTGForest& G = Design.Forests[I];
			const FOTGForestPreset* P = LibForests.FindByPredicate([&](const FOTGForestPreset& X) { return X.Name == Presets[K]; });
			G.Preset = P ? *P : FOTGForestPreset();
			SaveForests(P ? FString::Printf(TEXT("%s: %s"), *G.Name, *P->Name) : FString::Printf(TEXT("%s: its own trees"), *G.Name));
		}))];
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
		Slide(TEXT("How many"), FOTGForest::AmountLo, FOTGForest::AmountHi, &FOTGForest::Amount, [](float X) { return FMath::IsNearlyEqual(X, 1.f) ? FString(TEXT("as mapped")) : X <= 0 ? FString(TEXT("none")) : FString::Printf(TEXT("x %g"), FMath::RoundToFloat(X * 10) / 10); });
		Slide(TEXT("Their size"), FOTGForest::SizeLo, FOTGForest::SizeHi, &FOTGForest::Size, [](float X) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(X * 100)); });
		if (F.Preset.IsSet())
			V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Planted out"), Button(F.bFill ? FString::Printf(TEXT("Yes: %d a hectare"), FMath::RoundToInt(F.Preset.PerHa * F.Amount)) : FString(TEXT("No: the trees there only")), [this, I]() {
				Design.Forests[I].bFill = !Design.Forests[I].bFill;
				SaveForests(Design.Forests[I].bFill ? TEXT("Planted out to its spacing") : TEXT("Not planted out"));
			}, 12, F.bFill))];
		V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Go to it"), [this, I]() { if (Canvas.IsValid() && Design.Forests.IsValidIndex(I)) Canvas->Fit(Design.Forests[I].Pts, 80); }, 12)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Join another forest to it"), [this]() { SetMode(Mode == EMode::Join ? EMode::None : EMode::Join); }, 12, Mode == EMode::Join)]
			+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Delete this forest"), [this, I]() {
				Design.Forests.RemoveAt(I);
				SelForest = INDEX_NONE;
				SaveForests(TEXT("Forest deleted (its trees as they were)"));
			}, 12)]
		];
		if (Mode == EMode::Join) V->AddSlot().AutoHeight()[Note(FString::Printf(TEXT("Click the forest to join to %s: they become one (its settings kept), the gap between them filled in, up to 40 m."), *F.Name))];
		V->AddSlot().AutoHeight()[Note(TEXT("The game draws it as it is here the next time the course is played: no export needed. The ball meets its trees."))];
	}
	return Section(TEXT("Forests"), V);
}

// The course's grass biomes: its tall grass's, and areas drawn with their own (the studio's: the
// Course builder > Grass biomes). The rough is left as it is: one even colour
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
	TArray<FString> Labels = Names;
	Labels[0] = TEXT("The course's own");
	V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("All its tall grass"), Dropdown(Labels, Names.IndexOfByKey(Biomes.Default), [this, Names](int32 K) {
		if (!Names.IsValidIndex(K)) return;
		Biomes.Default = Names[K];
		if (const FOTGBiome* B = OTGLibrary::FindIn(LibBiomes, Biomes.Default)) Biomes.Use(*B);
		SaveBiomes(Biomes.Default.IsEmpty() ? FString(TEXT("The tall grass: the course's own")) : TEXT("The tall grass: ") + Biomes.Default);
	}))];
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Mark a patch"), [this]() { SetMode(Mode == EMode::Patch ? EMode::None : EMode::Patch); }, 12, Mode == EMode::Patch)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Draw an area"), [this]() { const bool bOn = Mode == EMode::Draw && ModeKind == TEXT("biome"); SetMode(bOn ? EMode::None : EMode::Draw, TEXT("biome")); }, 12, Mode == EMode::Draw && ModeKind == TEXT("biome"))]
		+ SHorizontalBox::Slot().FillWidth(1)[Dropdown(TArray<FString>(Names.GetData() + 1, Names.Num() - 1), Names.IndexOfByKey(AreaBiome) - 1, [this, Names](int32 K) {
			if (Names.IsValidIndex(K + 1)) AreaBiome = Names[K + 1];
			Changed();
		})]
	];
	V->AddSlot().AutoHeight()[Note(TEXT("Mark: click inside a patch of tall grass (amber) and it becomes an area of the biome picked here. Or draw one round it. An area only changes what kind of tall grass grows where there's tall grass already: it doesn't add any (paint that below). Click an area on the map to pick it."))];
	TSharedRef<SWrapBox> List = SNew(SWrapBox).UseAllottedSize(true);
	for (int32 i = 0; i < Biomes.Areas.Num(); i++)
		List->AddSlot().Padding(0, 0, 4, 4)[Button(FString::Printf(TEXT("%d. %s"), i + 1, *Biomes.Areas[i].Biome), [this, i]() { ClearPick(); SelArea = i; Changed(); }, 12, i == SelArea)];
	if (Biomes.Areas.Num()) V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[List];
	if (Biomes.Areas.IsValidIndex(SelArea))
	{
		const int32 I = SelArea;
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Its biome"), Dropdown(TArray<FString>(Names.GetData() + 1, Names.Num() - 1), Names.IndexOfByKey(Biomes.Areas[I].Biome) - 1, [this, I, Names](int32 K) {
			if (!Names.IsValidIndex(K + 1) || !Biomes.Areas.IsValidIndex(I)) return;
			const FString Next = Names[K + 1];
			if (const FOTGBiome* B = OTGLibrary::FindIn(LibBiomes, Next)) { Biomes.Use(*B); Biomes.Areas[I].Biome = Next; }
			SaveBiomes(TEXT("Area: ") + Next);
		}))];
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
	V->AddSlot().AutoHeight()[Row(TEXT("On the map"), Check(TEXT("Shown"), bShowBuildings, [this](bool b) { bShowBuildings = b; Changed(); }))];
	V->AddSlot().AutoHeight()[Note(FString::Printf(TEXT("%d buildings round the course. Click one to say what it is (it changes how the game draws it)."), Buildings.Num()))];
	return Section(TEXT("Buildings"), V);
}

// ------------------------------------------------------------------ what's changed, and the popup

TArray<FString> SOTGCourseEditor::ChangeList() const
{
	TArray<FString> Out;
	const FOTGCourseSource& A = Saved;
	const FOTGCourseSource& B = Src;
	if (A.Name != B.Name) Out.Add(FString::Printf(TEXT("Name: %s -> %s"), *A.Name, *B.Name));
	if (A.Location != B.Location) Out.Add(FString::Printf(TEXT("Location: %s -> %s"), *A.Location, *B.Location));
	if (A.Stimp != B.Stimp) Out.Add(FString::Printf(TEXT("Green speed: %g -> %g"), A.Stimp, B.Stimp));
	// the holes (by their place)
	if (A.Holes.Num() != B.Holes.Num()) Out.Add(FString::Printf(TEXT("Holes: %d -> %d"), A.Holes.Num(), B.Holes.Num()));
	auto Near = [](const FVector2D& P, const FVector2D& Q) { return FVector2D::Distance(P, Q) < 0.05; };
	auto Points = [&](const TMap<FString, FVector2D>& X, const TMap<FString, FVector2D>& Y, const TCHAR* What, TArray<FString>& Bits) {
		for (const auto& P : Y)
		{
			const FVector2D* Q = X.Find(P.Key);
			if (!Q) Bits.Add(FString::Printf(TEXT("%s %s added"), *P.Key, What));
			else if (!Near(*Q, P.Value)) Bits.Add(FString::Printf(TEXT("%s %s moved"), *P.Key, What));
		}
		for (const auto& P : X) if (!Y.Contains(P.Key)) Bits.Add(FString::Printf(TEXT("%s %s taken off"), *P.Key, What));
	};
	for (int32 i = 0; i < FMath::Min(A.Holes.Num(), B.Holes.Num()); i++)
	{
		const FOTGSourceHole& X = A.Holes[i];
		const FOTGSourceHole& Y = B.Holes[i];
		TArray<FString> Bits;
		if (X.Par != Y.Par) Bits.Add(FString::Printf(TEXT("par %d -> %d"), X.Par, Y.Par));
		if (X.Index != Y.Index) Bits.Add(FString::Printf(TEXT("index %d -> %d"), X.Index, Y.Index));
		Points(X.Tees, Y.Tees, TEXT("tee"), Bits);
		Points(X.Pins, Y.Pins, TEXT("pin"), Bits);
		if (!Near(X.Green, Y.Green)) Bits.Add(TEXT("green's centre moved"));
		if (X.Aims.Num() != Y.Aims.Num()) Bits.Add(FString::Printf(TEXT("aim points %d -> %d"), X.Aims.Num(), Y.Aims.Num()));
		else for (int32 k = 0; k < X.Aims.Num(); k++) if (!Near(X.Aims[k], Y.Aims[k])) { Bits.Add(TEXT("aim point moved")); break; }
		if (Bits.Num()) Out.Add(FString::Printf(TEXT("Hole %d: %s"), Y.Number, *FString::Join(Bits, TEXT(", "))));
	}
	// the shapes, kind by kind: those not found as they were (changed where there's one new for one gone)
	auto SameShape = [&](const FOTGSourceShape& X, const FOTGSourceShape& Y) {
		if (X.Kind != Y.Kind || X.Pts.Num() != Y.Pts.Num() || X.bInner != Y.bInner || X.bFlow != Y.bFlow || X.bInnerOOB != Y.bInnerOOB || X.Density != Y.Density || X.Species != Y.Species) return false;
		for (int32 k = 0; k < X.Pts.Num(); k++) if (!Near(X.Pts[k], Y.Pts[k])) return false;
		return true;
	};
	TArray<FString> Kinds;
	for (const FOTGSourceShape& S : A.Shapes) Kinds.AddUnique(S.Kind);
	for (const FOTGSourceShape& S : B.Shapes) Kinds.AddUnique(S.Kind);
	for (const FString& K : Kinds)
	{
		TArray<const FOTGSourceShape*> Gone, New;
		for (const FOTGSourceShape& S : A.Shapes) if (S.Kind == K) Gone.Add(&S);
		for (const FOTGSourceShape& S : B.Shapes)
		{
			if (S.Kind != K) continue;
			const int32 M = Gone.IndexOfByPredicate([&](const FOTGSourceShape* G) { return SameShape(*G, S); });
			if (M != INDEX_NONE) Gone.RemoveAt(M); else New.Add(&S);
		}
		const int32 Ch = FMath::Min(Gone.Num(), New.Num());
		TArray<FString> Bits;
		if (New.Num() - Ch) Bits.Add(FString::Printf(TEXT("%d added"), New.Num() - Ch));
		if (Ch) Bits.Add(FString::Printf(TEXT("%d changed"), Ch));
		if (Gone.Num() - Ch) Bits.Add(FString::Printf(TEXT("%d deleted"), Gone.Num() - Ch));
		if (Bits.Num()) Out.Add(FString::Printf(TEXT("%s: %s"), *KindLabel(K), *FString::Join(Bits, TEXT(", "))));
	}
	// the trees
	{
		TArray<const FOTGSourceTree*> Gone;
		for (const FOTGSourceTree& T : A.UserTrees) Gone.Add(&T);
		int32 New = 0;
		for (const FOTGSourceTree& T : B.UserTrees)
		{
			const int32 M = Gone.IndexOfByPredicate([&](const FOTGSourceTree* G) { return Near(G->P, T.P) && G->Size == T.Size && G->Kind == T.Kind; });
			if (M != INDEX_NONE) Gone.RemoveAt(M); else New++;
		}
		const int32 Ch = FMath::Min(Gone.Num(), New);
		TArray<FString> Bits;
		if (New - Ch) Bits.Add(FString::Printf(TEXT("%d added"), New - Ch));
		if (Ch) Bits.Add(FString::Printf(TEXT("%d moved or changed"), Ch));
		if (Gone.Num() - Ch) Bits.Add(FString::Printf(TEXT("%d deleted"), Gone.Num() - Ch));
		if (Bits.Num()) Out.Add(TEXT("Your own trees: ") + FString::Join(Bits, TEXT(", ")));
	}
	{
		int32 Removed = 0, Back = 0;
		for (const FVector2D& P : B.RemovedTrees) if (!A.RemovedTrees.ContainsByPredicate([&](const FVector2D& Q) { return Near(P, Q); })) Removed++;
		for (const FVector2D& P : A.RemovedTrees) if (!B.RemovedTrees.ContainsByPredicate([&](const FVector2D& Q) { return Near(P, Q); })) Back++;
		if (Removed) Out.Add(FString::Printf(TEXT("Trees from the photo: %d removed"), Removed));
		if (Back) Out.Add(FString::Printf(TEXT("Trees from the photo: %d put back"), Back));
	}
	// the tall grass: strokes added after those there were, or some taken back
	{
		int32 Same = 0;
		while (Same < A.TallGrass.Num() && Same < B.TallGrass.Num() && A.TallGrass[Same].Pts == B.TallGrass[Same].Pts && A.TallGrass[Same].bErase == B.TallGrass[Same].bErase) Same++;
		int32 Painted = 0, Rubbed = 0;
		for (int32 k = Same; k < B.TallGrass.Num(); k++) { if (B.TallGrass[k].bErase) Rubbed++; else Painted++; }
		if (Painted) Out.Add(FString::Printf(TEXT("Tall grass: %d stroke%s painted"), Painted, Painted == 1 ? TEXT("") : TEXT("s")));
		if (Rubbed) Out.Add(FString::Printf(TEXT("Tall grass: %d stroke%s rubbed out"), Rubbed, Rubbed == 1 ? TEXT("") : TEXT("s")));
		if (A.TallGrass.Num() > Same) Out.Add(FString::Printf(TEXT("Tall grass: %d earlier strokes taken back"), A.TallGrass.Num() - Same));
	}
	// the buildings
	for (const auto& P : B.BuildingTypes)
	{
		const FString* Was = A.BuildingTypes.Find(P.Key);
		if (!Was || *Was != P.Value)
		{
			const FBuilding* Bd = Buildings.FindByPredicate([&](const FBuilding& X) { return X.Key == P.Key; });
			const FString Which = Bd && !Bd->Name.IsEmpty() ? Bd->Name : FString::Printf(TEXT("at %s"), *P.Key);
			Out.Add(FString::Printf(TEXT("Building %s: %s"), *Which, *P.Value));
		}
	}
	for (const auto& P : A.BuildingTypes) if (!B.BuildingTypes.Contains(P.Key)) Out.Add(FString::Printf(TEXT("Building at %s: back to as found"), *P.Key));
	// the looks
	auto Look = [&](const TCHAR* What, bool X, bool Y) { if (X != Y) Out.Add(FString::Printf(TEXT("%s: %s"), What, Y ? TEXT("aerial photo colours") : TEXT("game colours"))); };
	Look(TEXT("Fairways & tees"), A.bPhotoFairway, B.bPhotoFairway);
	Look(TEXT("Greens"), A.bPhotoGreen, B.bPhotoGreen);
	Look(TEXT("Rough"), A.bPhotoRough, B.bPhotoRough);
	if (A.MowFairway != B.MowFairway) Out.Add(FString::Printf(TEXT("Fairway mowing: %s -> %s"), *FOTGCourseSource::MowLabel(A.MowFairway), *FOTGCourseSource::MowLabel(B.MowFairway)));
	if (A.MowGreen != B.MowGreen) Out.Add(FString::Printf(TEXT("Green mowing: %s -> %s"), *FOTGCourseSource::MowLabel(A.MowGreen), *FOTGCourseSource::MowLabel(B.MowGreen)));
	if (FOTGCourseSource::RoughCm(A.RoughScale) != FOTGCourseSource::RoughCm(B.RoughScale))
		Out.Add(FString::Printf(TEXT("Rough height: %d cm -> %d cm"), FOTGCourseSource::RoughCm(A.RoughScale), FOTGCourseSource::RoughCm(B.RoughScale)));
	if (FOTGCourseSource::NativeCm(A.NativeScale) != FOTGCourseSource::NativeCm(B.NativeScale))
		Out.Add(FString::Printf(TEXT("Tall grass height: %d cm -> %d cm"), FOTGCourseSource::NativeCm(A.NativeScale), FOTGCourseSource::NativeCm(B.NativeScale)));
	for (const FString& S : B.TreesOff) if (!A.TreesOff.Contains(S)) Out.Add(FString::Printf(TEXT("Tree species %s: turned off"), *S));
	for (const FString& S : A.TreesOff) if (!B.TreesOff.Contains(S)) Out.Add(FString::Printf(TEXT("Tree species %s: turned back on"), *S));
	if (A.PlantsOff.Num() != B.PlantsOff.Num() || !A.PlantsOff.Includes(B.PlantsOff) || A.PlantsExtra.Num() != B.PlantsExtra.Num() || !A.PlantsExtra.Includes(B.PlantsExtra))
		Out.Add(TEXT("Shrubs, flowers and grasses: changed"));
	if (!FMath::IsNearlyEqual(A.PlantAmount, B.PlantAmount)) Out.Add(FString::Printf(TEXT("Plants, how many: %d%% -> %d%%"), FMath::RoundToInt(A.PlantAmount * 100), FMath::RoundToInt(B.PlantAmount * 100)));
	return Out;
}

void SOTGCourseEditor::HideDialog()
{
	if (DialogHost.IsValid()) { DialogHost->SetContent(SNullWidget::NullWidget); DialogHost->SetVisibility(EVisibility::Collapsed); }
}

void SOTGCourseEditor::ShowDialog(const FString& Title, const FString& Intro, const TArray<FString>& Lines, TArray<TPair<FString, TFunction<void()>>> Buttons)
{
	if (!DialogHost.IsValid()) return;
	static const FSlateColorBrush Dimmer(FLinearColor(0, 0, 0, 0.6f));
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FString& L : Lines) List->AddSlot().AutoHeight().Padding(0, 1)[SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Ink).AutoWrapText(true).Text(FText::FromString(TEXT("-  ") + L))];
	TSharedRef<SHorizontalBox> Btns = SNew(SHorizontalBox);
	Btns->AddSlot().FillWidth(1)[SNullWidget::NullWidget];
	for (int32 k = 0; k < Buttons.Num(); k++)
	{
		const TFunction<void()> Do = Buttons[k].Value;
		Btns->AddSlot().AutoWidth().Padding(8, 0, 0, 0)[Button(Buttons[k].Key, [this, Do]() { HideDialog(); if (Do) Do(); }, 13, k == 0)];
	}
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[Text(Title, 17, Gold, "Bold")]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Soft).AutoWrapText(true).Text(FText::FromString(Intro))];
	if (Lines.Num()) Body->AddSlot().AutoHeight().Padding(0, 0, 0, 12)[SNew(SBox).MaxDesiredHeight(380)[SNew(SScrollBox) + SScrollBox::Slot()[List]]];
	Body->AddSlot().AutoHeight()[Btns];
	DialogHost->SetContent(SNew(SBorder).BorderImage(&Dimmer).Padding(0).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[SNew(SBox).WidthOverride(560)[Box(Body, FMargin(20, 16), Solid)]]);
	DialogHost->SetVisibility(EVisibility::Visible);
}

void SOTGCourseEditor::AskSave(int32 Then)
{
	const TArray<FString> L = ChangeList();
	if (!L.Num())
	{
		// (nothing new to keep: the game updated as asked, or nothing to do)
		if (Then == 0) { Status = TEXT("Nothing has changed since it was last saved"); Changed(); }
		else Save(Then);
		return;
	}
	const FString Go = Then == 1 ? TEXT("Save and update the game") : Then == 2 ? TEXT("Save and put in the game (full)") : TEXT("Save");
	ShowDialog(FString::Printf(TEXT("Save %d change%s?"), L.Num(), L.Num() == 1 ? TEXT("") : TEXT("s")),
		TEXT("Since the course was opened or last saved:"), L,
		{ { Go, [this, Then]() { Save(Then); } }, { TEXT("Cancel"), nullptr } });
}

void SOTGCourseEditor::AskClose()
{
	const TArray<FString> L = bDirty ? ChangeList() : TArray<FString>();
	if (!L.Num()) { if (OnClose) OnClose(bExported); return; }
	ShowDialog(TEXT("Changes not saved"),
		FString::Printf(TEXT("%d change%s since it was last saved, lost if you close now. (Forests and grass biome areas are kept already: they save as you make them.)"), L.Num(), L.Num() == 1 ? TEXT("") : TEXT("s")), L,
		{ { TEXT("Save and close"), [this]() { Save(3); } }, { TEXT("Close without saving"), [this]() { if (OnClose) OnClose(bExported); } }, { TEXT("Keep editing"), nullptr } });
}
