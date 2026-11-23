// See SOTGGrassDesigner.h.

#include "UI/Designer/SOTGGrassDesigner.h"
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
