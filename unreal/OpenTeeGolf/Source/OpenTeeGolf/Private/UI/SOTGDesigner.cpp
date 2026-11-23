// See SOTGDesigner.h.

#include "UI/SOTGDesigner.h"

#include "UI/OTGUi.h"
#include "World/OTGRuntimeCourse.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"

using namespace otg::ui;

namespace
{
	// A drop-down of choices (Values, shown by Label), the current one shown; Set when one's picked
	TSharedRef<SWidget> Dropdown(const TArray<FString>& Values, const FString& Current, TFunction<FString(const FString&)> Label, TFunction<void(const FString&)> Set)
	{
		// (the options outlive this call: the combo box holds them by pointer, the lambdas keep them)
		TSharedRef<TArray<TSharedPtr<FString>>> Opts = MakeShared<TArray<TSharedPtr<FString>>>();
		TSharedPtr<FString> Sel;
		for (const FString& V : Values)
		{
			Opts->Add(MakeShared<FString>(V));
			if (V == Current) Sel = Opts->Last();
		}
		return SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&Opts.Get())
			.InitiallySelectedItem(Sel)
			.OnGenerateWidget_Lambda([Opts, Label](TSharedPtr<FString> P) { return StaticCastSharedRef<SWidget>(Text(Label(*P), 12)); })
			.OnSelectionChanged_Lambda([Opts, Set](TSharedPtr<FString> P, ESelectInfo::Type How) { if (P.IsValid() && How != ESelectInfo::Direct) Set(*P); })
			[SNew(STextBlock).Font(Font(12)).Text(FText::FromString(Label(Current)))];
	}

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

void SOTGGrassDesigner::SetTurf(const TMap<FString, FOTGTurfLook>& Looks)
{
	if (!Course.IsValid()) return;
	for (const TCHAR* S : { TEXT("fairway"), TEXT("green") })
	{
		const FOTGTurfLook* L = Looks.Find(S);
		Course->SetTurfLook(S, L ? *L : FOTGTurfLook());
	}
}

TSharedRef<SWidget> SOTGGrassDesigner::TurfRow(const FString& Surface, const FString& Label)
{
	using G = otg::FGrassKind;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 8, 0, 2)[Text(Label, 14, Ink, "Bold")];
	auto Colour = [this, &V, Surface](const FString& Name, float Lo, float Hi, float FOTGTurfLook::* Field, TFunction<FString(float)> Fmt) {
		V->AddSlot().AutoHeight().Padding(18, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(112)[Text(Name, 12, Soft)]]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([this, Surface, Lo, Hi, Field]() { return Course.IsValid() ? FMath::Clamp((Course->TurfLook(Surface).*Field - Lo) / (Hi - Lo), 0.f, 1.f) : 0.f; })
				.OnValueChanged_Lambda([this, Surface, Lo, Hi, Field](float X) {
					if (!Course.IsValid()) return;
					// (a little stickiness at its own colour, so it's easy to go back to)
					float F = Lo + (Hi - Lo) * X;
					const float Own = Field == &FOTGTurfLook::Hue ? 0.f : 1.f;
					if (FMath::Abs(F - Own) < (Hi - Lo) * 0.02f) F = Own;
					FOTGTurfLook L = Course->TurfLook(Surface);
					L.*Field = F;
					Course->SetTurfLook(Surface, L);
					bUnsaved = true;
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(140)[SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Ink).Text_Lambda([this, Surface, Field, Fmt]() { return FText::FromString(Course.IsValid() ? Fmt(Course->TurfLook(Surface).*Field) : FString()); })]
			]
		];
	};
	Colour(TEXT("Colour"), -G::HueMax, G::HueMax, &FOTGTurfLook::Hue, [](float F) {
		const int32 D = FMath::RoundToInt(F);
		return D == 0 ? FString(TEXT("as mown")) : D < 0 ? FString::Printf(TEXT("%d° golder"), -D) : FString::Printf(TEXT("%d° bluer"), D);
	});
	Colour(TEXT("Colour strength"), G::SatLo, G::SatHi, &FOTGTurfLook::Sat, [](float F) { return FMath::IsNearlyEqual(F, 1.f) ? FString(TEXT("as mown")) : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(F * 100)); });
	Colour(TEXT("Brightness"), G::BrightLo, G::BrightHi, &FOTGTurfLook::Bright, [](float F) { return FMath::IsNearlyEqual(F, 1.f) ? FString(TEXT("as mown")) : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(F * 100)); });
	V->AddSlot().AutoHeight().Padding(130, 2, 0, 0).HAlign(HAlign_Left)
		[Button(TEXT("As mown"), [this, Surface]() { if (Course.IsValid()) { Course->SetTurfLook(Surface, FOTGTurfLook()); bUnsaved = true; } }, 11)];
	return V;
}

const otg::FGrassKind* SOTGGrassDesigner::Default(const FString& Name) const
{
	return otg::FGrass::DefaultKinds().FindByPredicate([&](const otg::FGrassKind& K) { return K.Name == Name; });
}

void SOTGGrassDesigner::ApplyColours()
{
	if (!Course.IsValid()) return;
	Course->SetGrassColours(Edit);
	bUnsaved = true;
	Status = TEXT("Not saved yet");
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
	if (K.bOn)
		Head->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)[Button(TEXT("Show me"), [this, I]() { if (OnShow && Edit.IsValidIndex(I)) OnShow(Edit[I].Name); }, 11)];
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

	// its colour: tinted as you drag (nothing grown again), kept to what grass can look like
	auto Colour = [this, I, &V](const FString& Label, float Lo, float Hi, float otg::FGrassKind::* Field, TFunction<FString(float)> Fmt) {
		V->AddSlot().AutoHeight().Padding(18, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(112)[Text(Label, 12, Soft)]]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([this, I, Lo, Hi, Field]() { return Edit.IsValidIndex(I) ? FMath::Clamp((Edit[I].*Field - Lo) / (Hi - Lo), 0.f, 1.f) : 0.f; })
				.OnValueChanged_Lambda([this, I, Lo, Hi, Field](float X) {
					if (!Edit.IsValidIndex(I)) return;
					// (a little stickiness at the kind's own colour, so it's easy to go back to)
					float F = Lo + (Hi - Lo) * X;
					const float Own = Field == &otg::FGrassKind::Hue ? 0.f : 1.f;
					if (FMath::Abs(F - Own) < (Hi - Lo) * 0.02f) F = Own;
					Edit[I].*Field = F;
					ApplyColours();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(140)[SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Ink).Text_Lambda([this, I, Field, Fmt]() { return FText::FromString(Edit.IsValidIndex(I) ? Fmt(Edit[I].*Field) : FString()); })]
			]
		];
	};
	using G = otg::FGrassKind;
	// (a Megascans plant's material turns its strength and brightness, not its hue)
	if (K.IsKite())
	{
		// (KiteDemo's materials have no tint to turn)
		V->AddSlot().AutoHeight().Padding(18, 2, 0, 0)[Text(TEXT("Its colour: a KiteDemo plant keeps its own colours"), 11, Soft)];
		return V;
	}
	if (K.Fab.Num())
		V->AddSlot().AutoHeight().Padding(18, 2, 0, 0)[Text(TEXT("Its colour: a Megascans plant keeps its own hue"), 11, Soft)];
	else
	Colour(TEXT("Colour"), -G::HueMax, G::HueMax, &G::Hue, [](float F) {
		const int32 D = FMath::RoundToInt(F);
		return D == 0 ? FString(TEXT("as it grows")) : D < 0 ? FString::Printf(TEXT("%d° golder"), -D) : FString::Printf(TEXT("%d° bluer"), D);
	});
	Colour(TEXT("Colour strength"), G::SatLo, G::SatHi, &G::Sat, [](float F) { return FMath::IsNearlyEqual(F, 1.f) ? FString(TEXT("as it grows")) : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(F * 100)); });
	Colour(TEXT("Brightness"), G::BrightLo, G::BrightHi, &G::Bright, [](float F) { return FMath::IsNearlyEqual(F, 1.f) ? FString(TEXT("as it grows")) : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(F * 100)); });
	if (!K.IsOwnColour())
		V->AddSlot().AutoHeight().Padding(130, 2, 0, 0).HAlign(HAlign_Left)
			[Button(TEXT("Its own colour"), [this, I]() { if (!Edit.IsValidIndex(I)) return; Edit[I].Hue = 0; Edit[I].Sat = 1; Edit[I].Bright = 1; ApplyColours(); Rebuild(); }, 11)];
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
	// the mown turf: the fairways (and tees) and the greens - their colour, seen at once
	List->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Text(TEXT("Fairways and greens"), 15, Gold, "Bold")];
	List->AddSlot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("The mown turf's colour: the fairways (with the tees) and the greens, each its own."))).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(470)];
	List->AddSlot().AutoHeight()[TurfRow(TEXT("fairway"), TEXT("Fairways and tees"))];
	List->AddSlot().AutoHeight()[TurfRow(TEXT("green"), TEXT("Greens"))];
	// (their blades, each cut at its own height: off, the ground alone draws them - quicker)
	for (int32 i = 0; i < Edit.Num(); i++)
		if (Edit[i].bMown && Edit[i].FarOf.IsEmpty()) List->AddSlot().AutoHeight()[KindRow(i)];
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
	// (the list's scroll kept: a change rebuilds it, which would start it at the top again)
	const float ScrolledTo = Scroll.IsValid() ? Scroll->GetScrollOffset() : 0.f;
	V->AddSlot().AutoHeight()[SNew(SBox).MaxDesiredHeight(600)[SAssignNew(Scroll, SScrollBox) + SScrollBox::Slot()[List]]];
	Scroll->SetScrollOffset(ScrolledTo);

	// saving, and starting again
	V->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Save for this course"), [this]() {
			FString Where;
			const bool bOk = Course.IsValid() && Course->SaveGrassKinds(&Where) && Course->SaveTurfLooks(&Where);
			bUnsaved = !bOk;
			Status = bOk ? FString(TEXT("Saved: the course grows it like this from now on")) : FString(TEXT("Couldn't save ")) + Where;
		}, 13, true)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("As saved"), [this]() {
			if (!Course.IsValid()) return;
			Edit = Course->SavedGrassKinds();
			SetTurf(Course->SavedTurfLooks());
			Apply();
			bUnsaved = false;
			Status = TEXT("Back as it was saved");
			Rebuild();
		}, 12)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("The game's own"), [this]() {
			Edit = otg::FGrass::DefaultKinds();
			SetTurf({});
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
	// (the picked one marked on the course while this shows: the marks go when it stops asking)
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float) {
		if (bPicked && Course.IsValid() && GetVisibility().IsVisible()) Course->MarkPicked(Picked);
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
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Button(Edit.bRocks ? TEXT("On") : TEXT("Off"), [this]() { Edit.bRocks = !Edit.bRocks; Apply(); Rebuild(); }, 12, Edit.bRocks)]
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
