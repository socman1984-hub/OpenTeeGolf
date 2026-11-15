// See SOTGDesigner.h.

#include "SOTGDesigner.h"

#include "OTGUi.h"
#include "Game/OTGRuntimeCourse.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"

using namespace otg::ui;

namespace
{
	// The tall grass's patches: the noise's lightness runs 0.27..0.46 (OTGGrass.cpp Place); a kind
	// grows where it is past the middle of its Lo..Hi (or short of it, inverted)
	constexpr float PatchL0 = 0.27f, PatchL1 = 0.46f;
	float Cover(const otg::FGrassKind& K)
	{
		const float C = FMath::Clamp(((K.PatchLo + K.PatchHi) / 2 - PatchL0) / (PatchL1 - PatchL0), 0.f, 1.f);
		return K.bInvert ? C : 1.f - C;
	}
	void SetCover(otg::FGrassKind& K, float V)
	{
		const float W = FMath::Max(0.01f, K.PatchHi - K.PatchLo);
		const float C = PatchL0 + (PatchL1 - PatchL0) * (K.bInvert ? V : 1.f - V);
		K.PatchLo = C - W / 2; K.PatchHi = C + W / 2;
	}
	float Mid(const otg::FGrassKind& K) { return (K.ScaleLo + K.ScaleHi) / 2; }
	float Spread(const otg::FGrassKind& K) { return (K.ScaleHi - K.ScaleLo) / FMath::Max(0.01f, K.ScaleHi + K.ScaleLo); }
	void SetScale(otg::FGrassKind& K, float M, float S) { K.ScaleLo = FMath::Max(0.02f, M * (1 - S)); K.ScaleHi = FMath::Max(K.ScaleLo, M * (1 + S)); }
}

void SOTGGrassDesigner::Construct(const FArguments&)
{
	ChildSlot[SAssignNew(Body, SBox).WidthOverride(520).MaxDesiredHeight(760)];
}

void SOTGGrassDesigner::Open(AOTGRuntimeCourse* InCourse)
{
	Course = InCourse;
	Edit = InCourse ? InCourse->GrassKinds() : TArray<otg::FGrassKind>();
	Status.Empty();
	bUnsaved = false;
	Rebuild();
}

const otg::FGrassKind* SOTGGrassDesigner::Default(const FString& Name) const
{
	return otg::FGrass::DefaultKinds().FindByPredicate([&](const otg::FGrassKind& K) { return K.Name == Name; });
}

void SOTGGrassDesigner::Apply()
{
	if (!Course.IsValid()) return;
	Course->SetGrassKinds(Edit);
	Edit = Course->GrassKinds();   // (with the far-off copies following)
	bUnsaved = true;
	Status = TEXT("Not saved yet");
}

TSharedRef<SWidget> SOTGGrassDesigner::KindRow(int32 I)
{
	const otg::FGrassKind& K = Edit[I];
	const otg::FGrassKind* D = Default(K.Name);
	const otg::FGrassKind Base = D ? *D : otg::FGrass::PlantKind(K.Fab.Num() ? K.Fab[0] : FString());
	const float HeightCm = Course.IsValid() ? Course->GrassHeightCm(K.Name) : 0.f;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	// the kind: on or off, where it grows, its name
	TSharedRef<SHorizontalBox> Head = SNew(SHorizontalBox);
	Head->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
		[Button(K.bOn ? TEXT("On") : TEXT("Off"), [this, I]() { Edit[I].bOn = !Edit[I].bOn; Apply(); Rebuild(); }, 12, K.bOn)];
	Head->AddSlot().FillWidth(1).VAlign(VAlign_Center)[Text(K.Label.IsEmpty() ? K.Name : K.Label, 14, K.bOn ? Ink : Soft, K.bOn ? "Bold" : "Regular")];
	if (K.bEven)
	{
		// (only the rough's own grasses can move between the two)
		const bool bRough = K.Masks.Contains(TEXT("rough"));
		Head->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
			[Button(bRough ? TEXT("Move to tall grass") : TEXT("Move to the rough"), [this, I, bRough]() { Edit[I].Masks = { bRough ? TEXT("native") : TEXT("rough") }; Apply(); Rebuild(); }, 11)];
	}
	if (K.bAdded)
		Head->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)[Button(TEXT("Remove"), [this, I]() { Edit.RemoveAt(I); Apply(); Rebuild(); }, 11)];
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 2)[Head];
	if (!K.bOn) return V;

	// its numbers: sliders, the grass grown again when one is let go
	auto Slider = [this, I, &V](const FString& Label, float Lo, float Hi, TFunction<float(const otg::FGrassKind&)> Get, TFunction<void(otg::FGrassKind&, float)> Set, TFunction<FString(const otg::FGrassKind&)> Fmt) {
		V->AddSlot().AutoHeight().Padding(18, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(112)[Text(Label, 12, Soft)]]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([this, I, Lo, Hi, Get]() { return Edit.IsValidIndex(I) ? FMath::Clamp((Get(Edit[I]) - Lo) / (Hi - Lo), 0.f, 1.f) : 0.f; })
				.OnValueChanged_Lambda([this, I, Lo, Hi, Set](float X) { if (Edit.IsValidIndex(I)) Set(Edit[I], Lo + (Hi - Lo) * X); })
				.OnMouseCaptureEnd_Lambda([this]() { Apply(); })
				.OnControllerCaptureEnd_Lambda([this]() { Apply(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(140)[SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Ink).Text_Lambda([this, I, Fmt]() { return FText::FromString(Edit.IsValidIndex(I) ? Fmt(Edit[I]) : FString()); })]
			]
		];
	};
	Slider(TEXT("How many"), 0, FMath::Max(Base.Density * 3, 100.f),
		[](const otg::FGrassKind& X) { return X.Density; }, [](otg::FGrassKind& X, float F) { X.Density = FMath::RoundToFloat(F); },
		[](const otg::FGrassKind& X) { return X.Density < 1 ? FString(TEXT("none")) : FString::Printf(TEXT("%d per 100 m²"), FMath::RoundToInt(X.Density)); });
	Slider(TEXT("Height"), Mid(Base) * 0.25f, Mid(Base) * 3.f,
		[](const otg::FGrassKind& X) { return Mid(X); }, [](otg::FGrassKind& X, float F) { SetScale(X, F, Spread(X)); },
		[HeightCm, Base](const otg::FGrassKind& X) {
			return HeightCm > 0 ? FString::Printf(TEXT("about %d cm"), FMath::RoundToInt(HeightCm * Mid(X)))
				: FString::Printf(TEXT("%d%%"), FMath::RoundToInt(100 * Mid(X) / FMath::Max(0.01f, Mid(Base))));
		});
	Slider(TEXT("Height varies"), 0, 0.7f,
		[](const otg::FGrassKind& X) { return Spread(X); }, [](otg::FGrassKind& X, float F) { SetScale(X, Mid(X), F); },
		[](const otg::FGrassKind& X) { return FString::Printf(TEXT("±%d%%"), FMath::RoundToInt(100 * Spread(X))); });
	Slider(TEXT("Seen out to"), 20, FMath::Min(800.f, FMath::Max(Base.DrawTo * 2.5f, 150.f)),
		[](const otg::FGrassKind& X) { return X.DrawTo; },
		[](otg::FGrassKind& X, float F) { const float R = X.DrawTo > 0 ? X.DrawFrom / X.DrawTo : 0.7f; X.DrawTo = FMath::RoundToFloat(F); X.DrawFrom = X.DrawTo * R; },
		[](const otg::FGrassKind& X) { return FString::Printf(TEXT("%d m"), FMath::RoundToInt(X.DrawTo)); });
	if (K.PatchM > 0)
	{
		Slider(TEXT("Patches"), 4, 60,
			[](const otg::FGrassKind& X) { return X.PatchM; }, [](otg::FGrassKind& X, float F) { X.PatchM = FMath::RoundToFloat(F); },
			[](const otg::FGrassKind& X) { return FString::Printf(TEXT("%d m across"), FMath::RoundToInt(X.PatchM)); });
		Slider(TEXT("Covers"), 0, 1,
			[](const otg::FGrassKind& X) { return Cover(X); }, [](otg::FGrassKind& X, float F) { SetCover(X, F); },
			[](const otg::FGrassKind& X) { return FString::Printf(TEXT("about %d%%"), FMath::RoundToInt(100 * Cover(X))); });
	}
	return V;
}

void SOTGGrassDesigner::Rebuild()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[Text(TEXT("Grass designer"), 18, Ink, "Bold")]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(8, 0, 0, 2)[Text(TEXT("seen at once  ·  R: close"), 12, Soft)]
	];
	if (!Course.IsValid() || !Edit.Num())
	{
		V->AddSlot().AutoHeight()[Text(TEXT("This course has no grass to design (a course pack, or still loading)."), 12, Soft)];
		Body->SetContent(Box(V, FMargin(14, 10), Solid));
		return;
	}
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	auto Section = [&](const FString& Title, const FString& Note, const TCHAR* Surface) {
		List->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[Text(Title, 15, Gold, "Bold")];
		List->AddSlot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Note)).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(470)];
		for (int32 i = 0; i < Edit.Num(); i++)
			if (Edit[i].FarOf.IsEmpty() && Edit[i].Masks.Contains(Surface)) List->AddSlot().AutoHeight()[KindRow(i)];
	};
	Section(TEXT("Rough"), TEXT("Kept one even green, as a looked-after rough is: only the rough's own grasses grow on it."), TEXT("rough"));
	Section(TEXT("Tall grass (deep rough, heather)"), TEXT("Out of play and under the trees, where the course lets it grow: mix any of these, each in its own patches."), TEXT("native"));

	// a Megascans plant added to the tall grass
	Plants.Reset();
	for (const FString& P : Course->GrassPlants())
		if (!Edit.ContainsByPredicate([&](const otg::FGrassKind& K) { return K.Fab.Contains(P); })) Plants.Add(MakeShared<FString>(P));
	if (!PlantPick.IsValid() || !Plants.ContainsByPredicate([&](const TSharedPtr<FString>& P) { return *P == *PlantPick; })) PlantPick = Plants.Num() ? Plants[0] : nullptr;
	if (Plants.Num())
	{
		List->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Text(TEXT("Add a plant"), 13, Soft)]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&Plants)
				.InitiallySelectedItem(PlantPick)
				.OnGenerateWidget_Lambda([](TSharedPtr<FString> P) { return StaticCastSharedRef<SWidget>(Text(otg::FGrass::PlantKind(*P).Label, 12)); })
				.OnSelectionChanged_Lambda([this](TSharedPtr<FString> P, ESelectInfo::Type) { if (P.IsValid()) PlantPick = P; })
				[SNew(STextBlock).Font(Font(12)).Text_Lambda([this]() { return FText::FromString(PlantPick.IsValid() ? otg::FGrass::PlantKind(*PlantPick).Label : FString()); })]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				Button(TEXT("Add"), [this]() {
					if (!PlantPick.IsValid()) return;
					Edit.Add(otg::FGrass::PlantKind(*PlantPick));
					Apply();
					Rebuild();
				}, 12)
			]
		];
	}
	V->AddSlot().AutoHeight()[SNew(SBox).MaxDesiredHeight(600)[SNew(SScrollBox) + SScrollBox::Slot()[List]]];

	// saving, and starting again
	V->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Save for this course"), [this]() {
			FString Where;
			const bool bOk = Course.IsValid() && Course->SaveGrassKinds(&Where);
			bUnsaved = !bOk;
			Status = bOk ? FString(TEXT("Saved: the course grows it like this from now on")) : FString(TEXT("Couldn't save ")) + Where;
		}, 13, true)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("As saved"), [this]() {
			if (!Course.IsValid()) return;
			Edit = Course->SavedGrassKinds();
			Apply();
			bUnsaved = false;
			Status = TEXT("Back as it was saved");
			Rebuild();
		}, 12)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("The game's own"), [this]() {
			Edit = otg::FGrass::DefaultKinds();
			Apply();
			Status = TEXT("The game's own grass (not saved yet)");
			Rebuild();
		}, 12)]
	];
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
	[
		SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).Text_Lambda([this]() {
			const int32 N = Course.IsValid() ? Course->GrassInstances() : 0;
			return FText::FromString(FString::Printf(TEXT("%s%s%d patches drawn round you"), *Status, Status.IsEmpty() ? TEXT("") : TEXT("  ·  "), N));
		})
	];
	Body->SetContent(Box(V, FMargin(14, 10), Solid));
}

// ------------------------------------------------------------------ the tree designer

void SOTGTreeDesigner::Construct(const FArguments&)
{
	ChildSlot[SAssignNew(Body, SBox).WidthOverride(520).MaxDesiredHeight(760)];
}

void SOTGTreeDesigner::Open(AOTGRuntimeCourse* InCourse)
{
	Course = InCourse;
	Edit = InCourse ? InCourse->TreeDesign() : FOTGTreeDesign();
	Species = InCourse ? InCourse->TreeSpecies() : TArray<TPair<FString, int32>>();
	Status.Empty();
	Rebuild();
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
				.OnMouseCaptureEnd_Lambda([this]() { Apply(); })
				.OnControllerCaptureEnd_Lambda([this]() { Apply(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(140)[SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Ink).Text_Lambda([this, Get, Fmt]() { return FText::FromString(Fmt(*Get(Edit))); })]
			]
		];
	};
	auto Times = [](float F) { return F < 0.025f ? FString(TEXT("none")) : FMath::IsNearlyEqual(F, 1.f) ? FString(TEXT("as mapped")) : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(F * 100)); };
	auto SizeFmt = [](float F) { return FMath::IsNearlyEqual(F, 1.f) ? FString(TEXT("as mapped")) : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(F * 100)); };

	List->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Text(TEXT("All trees"), 15, Gold, "Bold")];
	Slider(List, TEXT("Size"), 0.5f, 1.8f, [](FOTGTreeDesign& D) { return &D.AllSize; }, SizeFmt);
	Slider(List, TEXT("How many"), 0.f, 2.5f, [](FOTGTreeDesign& D) { return &D.AllDensity; }, Times);

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
			+ SHorizontalBox::Slot().AutoWidth()[Button(FOTGTreeDesign::ModelLabel(L.Model), [this, Kind]() {
				FOTGSpeciesLook& X = Edit.Species.FindOrAdd(Kind);
				const TArray<FString>& M = FOTGTreeDesign::Models();
				X.Model = M[(M.IndexOfByKey(X.Model) + 1) % M.Num()];
				Apply();
				Rebuild();
			}, 12)]
		];
		Slider(Row, TEXT("Size"), 0.4f, 2.f, [Kind](FOTGTreeDesign& D) { return &D.Species.FindOrAdd(Kind).Size; }, SizeFmt);
		Slider(Row, TEXT("How many"), 0.f, 3.f, [Kind](FOTGTreeDesign& D) { return &D.Species.FindOrAdd(Kind).Density; }, Times);
		List->AddSlot().AutoHeight()[Row];
	}

	List->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Button(Edit.bRocks ? TEXT("On") : TEXT("Off"), [this]() { Edit.bRocks = !Edit.bRocks; Apply(); Rebuild(); }, 12, Edit.bRocks)]
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(TEXT("Rocks"), 15, Gold, "Bold")]
	];
	if (Edit.bRocks)
	{
		Slider(List, TEXT("Size"), 0.4f, 2.5f, [](FOTGTreeDesign& D) { return &D.RockSize; }, SizeFmt);
		Slider(List, TEXT("How many"), 0.f, 1.f, [](FOTGTreeDesign& D) { return &D.RockDensity; }, Times);
	}
	V->AddSlot().AutoHeight()[SNew(SBox).MaxDesiredHeight(600)[SNew(SScrollBox) + SScrollBox::Slot()[List]]];

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
			Status = TEXT("Back as it was saved");
			Rebuild();
		}, 12)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("As mapped"), [this]() {
			Edit = FOTGTreeDesign();
			Apply();
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
