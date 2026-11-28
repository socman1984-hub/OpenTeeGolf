// See SOTGStudio.h.

#include "UI/Builder/SOTGStudio.h"

#include "World/OTGPreviewStage.h"
#include "World/OTGGrass.h"
#include "UI/OTGUi.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "Brushes/SlateImageBrush.h"
#include "Engine/Texture2D.h"
#include "Widgets/Input/SButton.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

using namespace otg::ui;

namespace
{
	TWeakPtr<SOTGStudio> TheStudio;

	UWorld* MenuWorld()
	{
		UGameInstance* GI = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetGameInstance() : nullptr;
		return GI ? GI->GetWorld() : nullptr;
	}

	// The tall grass's patches (as the grass designer has them: SOTGDesignerShared.h)
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

	// A labelled slider: the value Lo..Hi, Set as it's dragged, Fmt its words
	TSharedRef<SWidget> Slider(const FString& Label, float Lo, float Hi, TFunction<float()> Get, TFunction<void(float)> Set, TFunction<FString()> Fmt, int32 LabelW = 120)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(LabelW)[Text(Label, 12, Soft)]]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([Lo, Hi, Get]() { return FMath::Clamp((Get() - Lo) / (Hi - Lo), 0.f, 1.f); })
				.OnValueChanged_Lambda([Lo, Hi, Set](float X) { Set(Lo + (Hi - Lo) * X); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(130)[SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Ink).Text_Lambda([Fmt]() { return FText::FromString(Fmt()); })]
			];
	}

	FString Pct(float F) { return FMath::IsNearlyEqual(F, 1.f) ? FString(TEXT("as it grows")) : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(F * 100)); }

	// (testing: the studio from the console - OTG.Studio.Pick <name>, OTG.Studio.Set <kind> <field> <v>,
	// OTG.Studio.Add <plant>, OTG.Studio.Forests 0|1)
	FAutoConsoleCommand StudioPick(TEXT("OTG.Studio.Pick"), TEXT("OTG.Studio.Pick <name>: a saved biome or forest in the studio"), FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& A) {
		if (TSharedPtr<SOTGStudio> S = TheStudio.Pin(); S && A.Num()) S->Pick(FString::Join(A, TEXT(" ")));
	}));
	FAutoConsoleCommand StudioSet(TEXT("OTG.Studio.Set"), TEXT("OTG.Studio.Set <kind> on|density|height|cover <v>: the studio's biome"), FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& A) {
		if (TSharedPtr<SOTGStudio> S = TheStudio.Pin(); S && A.Num() >= 3) S->SetKind(A[0], A[1], FCString::Atof(*A[2]));
	}));
	FAutoConsoleCommand StudioAdd(TEXT("OTG.Studio.Add"), TEXT("OTG.Studio.Add <plant>: a plant added to the studio's biome"), FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& A) {
		if (TSharedPtr<SOTGStudio> S = TheStudio.Pin(); S && A.Num()) S->AddPlant(A[0]);
	}));
	FAutoConsoleCommand StudioForests(TEXT("OTG.Studio.Forests"), TEXT("OTG.Studio.Forests 0|1: the studio's grass biomes or forests"), FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& A) {
		if (TSharedPtr<SOTGStudio> S = TheStudio.Pin(); S && A.Num()) S->ShowForests(A[0] != TEXT("0"));
	}));
}

void SOTGStudio::Construct(const FArguments& Args, bool bInForests)
{
	TheStudio = SharedThis(this);
	bForests = bInForests;
	Stage = AOTGPreviewStage::Open(MenuWorld());
	if (Stage.IsValid()) PlantList = Stage->Plants();
	for (const FString& P : PlantList) PlantOpts.Add(MakeShared<FString>(P));
	for (const FString& K : FOTGTreeDesign::AllSpecies()) SpeciesOpts.Add(MakeShared<FString>(K));
	SpeciesPick = SpeciesOpts.Num() ? SpeciesOpts[0] : nullptr;
	Biomes = OTGLibrary::Biomes();
	Forests = OTGLibrary::Forests();
	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(600)[SAssignNew(Panel, SBox)]]
		// the plot shows through here: drag to look round, the wheel to come closer
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Bottom).HAlign(HAlign_Right).Padding(0, 0, 8, 8)
		[
			Box(SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).Text_Lambda([this]() {
				const AOTGPreviewStage* S = Stage.Get();
				if (!Solo.IsEmpty() && !bForests)
				if (const otg::FGrassKind* K = Biome.Kinds.FindByPredicate([&](const otg::FGrassKind& X) { return X.Name == Solo; }))
					return FText::FromString(FString::Printf(TEXT("%s alone  \u00b7  click its picture again for the whole biome  \u00b7  W A S D to fly, right button to look"), *(K->Label.IsEmpty() ? K->Name : K->Label)));
			return FText::FromString(FString::Printf(TEXT("Drag to turn round  ·  wheel to come closer  ·  W A S D to fly, Q / E down and up, right button to look, Shift faster  ·  %d patches of grass%s"),
					S ? S->GrassCount() : 0, bForests && S ? *FString::Printf(TEXT(", %d trees"), S->TreeCount()) : TEXT("")));
			}), FMargin(10, 5))
		]
	];
	FString Arg;
	FParse::Value(FCommandLine::Get(), TEXT("studio="), Arg);
	FString Name;
	Arg.Split(TEXT(":"), nullptr, &Name);
	Load(Name.Replace(TEXT("+"), TEXT(" ")));
	if (Stage.IsValid()) Stage->Frame(bForests);
}

SOTGStudio::~SOTGStudio()
{
	if (Stage.IsValid()) Stage->Close();
}

void SOTGStudio::ShowForests(bool bOn)
{
	bForests = bOn;
	bConfirmDelete = false;
	Load(FString());
	if (Stage.IsValid()) Stage->Frame(bForests);
}

void SOTGStudio::Pick(const FString& Name)
{
	if (Forests.ContainsByPredicate([&](const FOTGForestPreset& P) { return P.Name == Name; }) && !OTGLibrary::FindIn(Biomes, Name)) { bForests = true; if (Stage.IsValid()) Stage->Frame(true); }
	else if (OTGLibrary::FindIn(Biomes, Name)) { bForests = false; if (Stage.IsValid()) Stage->Frame(false); }
	Load(Name);
}

void SOTGStudio::Load(const FString& Name)
{
	bScrollTop = true;
	Solo.Empty();
	if (bForests)
	{
		const FOTGForestPreset* P = Forests.FindByPredicate([&](const FOTGForestPreset& X) { return X.Name == Name; });
		if (!P && Forests.Num()) P = &Forests[0];
		if (P) Forest = *P; else { Forest = FOTGForestPreset(); Forest.Name = TEXT("New forest"); }
		Was = P ? P->Name : FString();
	}
	else
	{
		const FOTGBiome* B = OTGLibrary::FindIn(Biomes, Name);
		if (!B && Biomes.Num()) B = &Biomes[0];
		Biome = B ? *B : FOTGBiome::TallGrass(TEXT("New biome"));
		Was = B ? B->Name : FString();
		ListEveryPlant();
	}
	bUnsaved = false;
	bConfirmDelete = false;
	Status.Empty();
	bDirty = true;
	Rebuild();
}

void SOTGStudio::New()
{
	bScrollTop = true;
	if (bForests)
	{
		TArray<FString> Names;
		for (const FOTGForestPreset& P : Forests) Names.Add(P.Name);
		Forest = FOTGForestPreset();
		Forest.Name = OTGLibrary::FreeName(TEXT("New forest"), Names);
		Forest.Species.Add({ TEXT("oak"), 1.f, FString() });
	}
	else
	{
		TArray<FString> Names;
		for (const FOTGBiome& B : Biomes) Names.Add(B.Name);
		Biome = FOTGBiome::TallGrass(OTGLibrary::FreeName(TEXT("New biome"), Names));
		ListEveryPlant();
	}
	Was.Empty();
	Changed();
	Rebuild();
}

void SOTGStudio::Save()
{
	const FString Name = bForests ? Forest.Name.TrimStartAndEnd() : Biome.Name.TrimStartAndEnd();
	if (Name.IsEmpty()) { Status = TEXT("Give it a name first"); Rebuild(); return; }
	bool bOk;
	if (bForests)
	{
		Forest.Name = Name;
		bOk = OTGLibrary::SaveForest(Forest);
		if (bOk && !Was.IsEmpty() && Was != Name) OTGLibrary::DeleteForest(Was);
		Forests = OTGLibrary::Forests();
	}
	else
	{
		Biome.Name = Name;
		bOk = OTGLibrary::SaveBiome(Biome);
		if (bOk && !Was.IsEmpty() && Was != Name) OTGLibrary::DeleteBiome(Was);
		Biomes = OTGLibrary::Biomes();
	}
	Was = bOk ? Name : Was;
	bUnsaved = !bOk;
	Status = bOk ? FString::Printf(TEXT("Saved: %s (in %s)"), *Name, *OTGLibrary::Dir()) : FString(TEXT("Couldn't save it"));
	Rebuild();
}

void SOTGStudio::Delete()
{
	if (!Was.IsEmpty())
	{
		if (bForests) { OTGLibrary::DeleteForest(Was); Forests = OTGLibrary::Forests(); }
		else { OTGLibrary::DeleteBiome(Was); Biomes = OTGLibrary::Biomes(); }
	}
	Load(FString());
	Status = TEXT("Deleted");
}

TArray<otg::FGrassKind> SOTGStudio::UnderKinds() const
{
	if (const FOTGBiome* B = Forest.Under.IsEmpty() ? nullptr : OTGLibrary::FindIn(Biomes, Forest.Under)) return B->Kinds;
	return FOTGBiome::TallGrass(FString()).Kinds;
}

void SOTGStudio::Apply()
{
	AOTGPreviewStage* S = Stage.Get();
	if (!S) return;
	if (bForests) S->ShowForest(Forest, UnderKinds());
	else
	{
		otg::FGrass::Derive(Biome.Kinds);
		if (Biome.Kinds.ContainsByPredicate([&](const otg::FGrassKind& K) { return K.Name == Solo; }))
		{
			TArray<otg::FGrassKind> One = Biome.Kinds;
			for (otg::FGrassKind& K : One) K.bOn = K.Name == Solo || K.FarOf == Solo;
			S->ShowGrass(One);
		}
		else S->ShowGrass(Biome.Kinds);
	}
	AppliedAt = FPlatformTime::Seconds();
	bDirty = false;
}

void SOTGStudio::Tick(const FGeometry& Geometry, const double Time, const float Dt)
{
	SCompoundWidget::Tick(Geometry, Time, Dt);
	// (grown again as it changes: at most every fifth of a second while a slider's dragged)
	if (bDirty && FPlatformTime::Seconds() - AppliedAt > 0.2) Apply();
	// (-studioicons: each kind grown on its own and photographed close, for its picture in the list -
	// Saved/Screenshots/grassicon_<kind>.png; unreal/grass_icons.ps1 crops them into unreal/grass_icons)
	if (FParse::Param(FCommandLine::Get(), TEXT("studioicons")) && Stage.IsValid() && !bForests)
	{
		if (IconStep < 0)
		{
			IconStep = 0; IconAt = FPlatformTime::Seconds() + 15;
			// (one exposure for all: the camera's own adjusting washed the pale ones out over bare ground)
			if (GEngine) { GEngine->Exec(nullptr, TEXT("r.EyeAdaptationQuality 0")); GEngine->Exec(nullptr, TEXT("r.DefaultFeature.AutoExposure 0")); }
		}
		if (FPlatformTime::Seconds() >= IconAt)
		{
			// (each kind: grown alone at twice its density, given 6 s to grow, then photographed)
			const int32 Kind = IconStep / 2;
			TArray<int32> Kinds;
			for (int32 i = 0; i < Biome.Kinds.Num(); i++) if (Biome.Kinds[i].FarOf.IsEmpty()) Kinds.Add(i);
			if (!Kinds.IsValidIndex(Kind)) { FPlatformMisc::RequestExit(false, TEXT("studioicons")); return; }
			if (IconStep % 2 == 0)
			{
				TArray<otg::FGrassKind> One = Biome.Kinds;
				for (otg::FGrassKind& K : One) K.bOn = false;
				One[Kinds[Kind]].bOn = true;
				One[Kinds[Kind]].Density = FMath::Max(One[Kinds[Kind]].Density * 2, 600.f);   // (thick enough to see, even the wildflowers)
				if (One[Kinds[Kind]].PatchM > 0) SetCover(One[Kinds[Kind]], 1.f);   // (its patches covering all of it)
				otg::FGrass::Derive(One);
				Stage->ShowGrass(One);
				Stage->FrameClose();
				bDirty = false;
				IconAt = FPlatformTime::Seconds() + 6;
			}
			else
			{
				FString Safe;
				for (TCHAR C : Biome.Kinds[Kinds[Kind]].Name) Safe.AppendChar(FChar::IsAlnum(C) ? C : TEXT('_'));
				if (GEngine) GEngine->Exec(nullptr, *FString::Printf(TEXT("OTG.Shot grassicon_%s"), *Safe));
				IconAt = FPlatformTime::Seconds() + 1.5;
			}
			IconStep++;
		}
		return;
	}
	// (testing, -studiotest: the list folded, then its first kind opened and turned off, then the free
	// view flown low over the grass - each photographed (Saved/Screenshots/studio_*.png) - and quit)
	if (FParse::Param(FCommandLine::Get(), TEXT("studiotest")) && Stage.IsValid())
	{
		static int32 Step = 0;
		static double At = FPlatformTime::Seconds() + 20;
		auto Shot = [](const TCHAR* N) { if (GEngine) GEngine->Exec(nullptr, *FString::Printf(TEXT("OTG.Shot %s"), N)); };
		if (FPlatformTime::Seconds() >= At)
		{
			At = FPlatformTime::Seconds() + 4;
			if (Step == 0) Shot(TEXT("studio_folded"));
			else if (Step == 1 && Biome.Kinds.Num()) { OpenKinds.Add(Biome.Kinds[0].Name); Biome.Kinds[0].bOn = false; Changed(); Rebuild(); }
			else if (Step == 2) Shot(TEXT("studio_opened"));
			else if (Step == 3) { Stage->Look(0, 30); Stage->Fly(FVector(0, 0, -5000)); }
			else if (Step == 4) Shot(TEXT("studio_low"));
			else if (Step == 5) { Solo = TEXT("fescue"); for (const otg::FGrassKind& K : Biome.Kinds) if (K.Label.Contains(TEXT("Fescue (Megascans)"))) Solo = K.Name; bDirty = true; Rebuild(); }
			else if (Step == 6) Shot(TEXT("studio_solo_low"));
			else if (Step == 7) { Stage->Orbit(0, -25); Stage->Zoom(2.5f); }
			else if (Step == 8) Shot(TEXT("studio_solo_high"));
			else if (Step == 9) { Solo.Empty(); bDirty = true; Rebuild(); }
			else if (Step == 10) Shot(TEXT("studio_whole_high"));
			else if (Step >= 11) FPlatformMisc::RequestExit(false, TEXT("studiotest"));
			Step++;
		}
	}
	// the free view flown with the keys (faster with Shift, and for a forest)
	if (Held.Num() && Stage.IsValid())
	{
		const FVector Dir((Held.Contains(EKeys::W) ? 1 : 0) - (Held.Contains(EKeys::S) ? 1 : 0), (Held.Contains(EKeys::D) ? 1 : 0) - (Held.Contains(EKeys::A) ? 1 : 0),
			(Held.Contains(EKeys::E) ? 1 : 0) - (Held.Contains(EKeys::Q) ? 1 : 0));
		const float Speed = (bForests ? 1500.f : 500.f) * (Held.Contains(EKeys::LeftShift) || Held.Contains(EKeys::RightShift) ? 3.f : 1.f);
		if (!Dir.IsNearlyZero()) Stage->Fly(Dir.GetSafeNormal() * Speed * Dt);
	}
}

FReply SOTGStudio::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	if (G.AbsoluteToLocal(E.GetScreenSpacePosition()).X < 600) return FReply::Unhandled();
	// (left: turned round the plot; right: the free view, looking where it stands. Either way the keys
	// fly it from here on: the studio takes the keyboard)
	bLooking = E.GetEffectingButton() == EKeys::RightMouseButton;
	bDragging = !bLooking;
	return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SOTGStudio::OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E)
{
	if (!bDragging && !bLooking) return FReply::Unhandled();
	bDragging = bLooking = false;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SOTGStudio::OnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	if (!Stage.IsValid() || (!bDragging && !bLooking)) return FReply::Unhandled();
	if (bLooking) Stage->Look(E.GetCursorDelta().X * 0.25f, -E.GetCursorDelta().Y * 0.2f);
	else Stage->Orbit(E.GetCursorDelta().X * 0.3f, -E.GetCursorDelta().Y * 0.2f);
	return FReply::Handled();
}

static bool FlyKey(const FKey& K)
{
	return K == EKeys::W || K == EKeys::A || K == EKeys::S || K == EKeys::D || K == EKeys::Q || K == EKeys::E || K == EKeys::LeftShift || K == EKeys::RightShift;
}

FReply SOTGStudio::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	if (!FlyKey(E.GetKey())) return FReply::Unhandled();
	Held.Add(E.GetKey());
	return FReply::Handled();
}

FReply SOTGStudio::OnKeyUp(const FGeometry& G, const FKeyEvent& E)
{
	if (!FlyKey(E.GetKey())) return FReply::Unhandled();
	Held.Remove(E.GetKey());
	return FReply::Handled();
}

FReply SOTGStudio::OnMouseWheel(const FGeometry& G, const FPointerEvent& E)
{
	if (G.AbsoluteToLocal(E.GetScreenSpacePosition()).X < 600 || !Stage.IsValid()) return FReply::Unhandled();
	if (Stage->IsFree()) Stage->Fly(FVector(E.GetWheelDelta() > 0 ? 150.f : -150.f, 0, 0));   // (the free view: on and back)
	else Stage->Zoom(E.GetWheelDelta() > 0 ? 0.85f : 1.18f);
	return FReply::Handled();
}

// ------------------------------------------------------------------ the panel

void SOTGStudio::Rebuild()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	// grass biomes or forests; back to the Course builder
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Grass biomes"), [this]() { ShowForests(false); }, 13, !bForests)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Forests"), [this]() { ShowForests(true); }, 13, bForests)]
		+ SHorizontalBox::Slot().FillWidth(1)[SNullWidget::NullWidget]
		+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Back"), [this]() { if (OnClose) OnClose(); }, 13)]
	];
	V->AddSlot().AutoHeight()
	[
		SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).AutoWrapText(true).Text(FText::FromString(bForests
			? TEXT("A mix of trees for a forest: the species and how many of each, how tall, how close, and the grass under them. The course editor gives a course's forests one.")
			: TEXT("A mix of tall grass for the deep rough and the wild places (the rough itself stays one even colour). The course editor gives a course's tall grass one, and areas of it their own.")))
	];
	// the saved ones
	TSharedRef<SWrapBox> Saved = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4));
	auto Chip = [&](const FString& Name) {
		Saved->AddSlot()[Button(Name, [this, Name]() { Load(Name); }, 12, Name == Was)];
	};
	if (bForests) for (const FOTGForestPreset& P : Forests) Chip(P.Name);
	else for (const FOTGBiome& B : Biomes) Chip(B.Name);
	Saved->AddSlot()[Button(TEXT("+ New"), [this]() { New(); }, 12)];
	V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[Saved];
	// its name, and save / copy / delete
	V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Text(TEXT("Name"), 12, Soft)]
		+ SHorizontalBox::Slot().FillWidth(1)
		[
			SNew(SEditableTextBox).Font(Font(13))
			.Text(FText::FromString(bForests ? Forest.Name : Biome.Name))
			.OnTextChanged_Lambda([this](const FText& T) { (bForests ? Forest.Name : Biome.Name) = T.ToString(); bUnsaved = true; Status = TEXT("Not saved yet"); })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)[Button(TEXT("Save"), [this]() { Save(); }, 12, bUnsaved)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)[Button(TEXT("Copy"), [this]() {
			TArray<FString> Names;
			if (bForests) { for (const FOTGForestPreset& P : Forests) Names.Add(P.Name); Forest.Name = OTGLibrary::FreeName(Forest.Name + TEXT(" copy"), Names); }
			else { for (const FOTGBiome& B : Biomes) Names.Add(B.Name); Biome.Name = OTGLibrary::FreeName(Biome.Name + TEXT(" copy"), Names); }
			Was.Empty(); Changed(); Rebuild();
		}, 12)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)[Button(bConfirmDelete ? TEXT("Sure? Delete") : TEXT("Delete"), [this]() {
			if (!bConfirmDelete) { bConfirmDelete = true; Rebuild(); return; }
			Delete();
		}, 12)]
	];
	if (!Status.IsEmpty()) V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(bUnsaved ? Gold : Soft).AutoWrapText(true).Text(FText::FromString(Status))];
	const float WasAt = ListScroll.IsValid() && !bScrollTop ? ListScroll->GetScrollOffset() : 0.f;
	bScrollTop = false;
	V->AddSlot().FillHeight(1).Padding(0, 8, 0, 0)[SAssignNew(ListScroll, SScrollBox) + SScrollBox::Slot()[bForests ? ForestPanel() : BiomePanel()]];
	Panel->SetContent(Box(V, FMargin(14, 12), Solid));
	ListScroll->SetScrollOffset(WasAt);   // (where it was: a change doesn't send the list back to the top)
}

TSharedRef<SWidget> SOTGStudio::KindRow(int32 I)
{
	const otg::FGrassKind& K = Biome.Kinds[I];
	const otg::FGrassKind* D = otg::FGrass::DefaultKinds().FindByPredicate([&](const otg::FGrassKind& X) { return X.Name == K.Name; });
	const otg::FGrassKind Base = D ? *D : otg::FGrass::PlantKind(K.Fab.Num() ? K.Fab[0] : FString());
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	// its line: growing or not (a checkbox), its picture and name - click to open its sliders
	const FString KindName = K.Name;
	const bool bOpen = OpenKinds.Contains(KindName);
	auto Fold = [this, KindName]() { if (!OpenKinds.Remove(KindName)) OpenKinds.Add(KindName); Rebuild(); return FReply::Handled(); };
	TSharedRef<SHorizontalBox> Head = SNew(SHorizontalBox);
	Head->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
	[
		SNew(SCheckBox)
		.IsChecked_Lambda([this, I]() { return Biome.Kinds.IsValidIndex(I) && Biome.Kinds[I].bOn ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this, I](ECheckBoxState C) { if (Biome.Kinds.IsValidIndex(I)) { Biome.Kinds[I].bOn = C == ECheckBoxState::Checked; Changed(); Rebuild(); } })
	];
	Head->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
	[
		SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").OnClicked_Lambda(Fold)
		[Text(bOpen ? TEXT("\u2212") : TEXT("+"), 16, Soft, "Bold")]
	];
	const FSlateBrush* Pic = Picture(KindName);
	Head->AddSlot().FillWidth(1).VAlign(VAlign_Center)
	[
		SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").OnClicked_Lambda(Fold)
		[
			SNew(SHorizontalBox)
			// (a picture of it, the size of the line: a blank square where there's none yet)
			// (clicked: this grass alone on the plot, to fly round; again, the whole biome)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
			[
				SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(FMargin(0))
				.ToolTipText(FText::FromString(Solo == KindName ? TEXT("Showing it alone: click for the whole biome") : TEXT("See it alone in the preview")))
				.OnClicked_Lambda([this, KindName]() { Solo = Solo == KindName ? FString() : KindName; bDirty = true; Rebuild(); return FReply::Handled(); })
				[
					SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).Padding(2)
					.BorderBackgroundColor(Solo == KindName ? FLinearColor(0.95f, 0.78f, 0.3f, 1) : FLinearColor(0, 0, 0, 0.25f))
					[
						SNew(SBox).WidthOverride(44).HeightOverride(44)
						[Pic ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Pic).ColorAndOpacity(FLinearColor(1, 1, 1, K.bOn || Solo == KindName ? 1.f : 0.55f))) : StaticCastSharedRef<SWidget>(SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Soft).Justification(ETextJustify::Center).Text(FText::FromString(TEXT("see\nalone"))))]
					]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(K.Label.IsEmpty() ? K.Name : K.Label, 14, K.bOn ? Ink : Soft, K.bOn ? "Bold" : "Regular")]
		]
	];
	V->AddSlot().AutoHeight().Padding(0, 2, 0, 2)[Head];
	if (!bOpen) return V;
	auto Add = [&](TSharedRef<SWidget> W) { V->AddSlot().AutoHeight().Padding(18, 2, 0, 0)[W]; };
	auto Get = [this, I](TFunction<float(const otg::FGrassKind&)> F) { return [this, I, F]() { return Biome.Kinds.IsValidIndex(I) ? F(Biome.Kinds[I]) : 0.f; }; };
	auto Put = [this, I](TFunction<void(otg::FGrassKind&, float)> F) { return [this, I, F](float X) { if (Biome.Kinds.IsValidIndex(I)) { F(Biome.Kinds[I], X); Changed(); } }; };
	auto Say = [this, I](TFunction<FString(const otg::FGrassKind&)> F) { return [this, I, F]() { return Biome.Kinds.IsValidIndex(I) ? F(Biome.Kinds[I]) : FString(); }; };
	Add(Slider(TEXT("How many"), 0, FMath::Max(Base.Density * 3, 100.f), Get([](const otg::FGrassKind& X) { return X.Density; }), Put([](otg::FGrassKind& X, float F) { X.Density = FMath::RoundToFloat(F); }),
		Say([](const otg::FGrassKind& X) { return X.Density < 1 ? FString(TEXT("none")) : FString::Printf(TEXT("%d per 100 m²"), FMath::RoundToInt(X.Density)); })));
	Add(Slider(TEXT("Height"), Mid(Base) * 0.25f, Mid(Base) * 3.f, Get([](const otg::FGrassKind& X) { return Mid(X); }), Put([](otg::FGrassKind& X, float F) { SetScale(X, F, Spread(X)); }),
		Say([Base](const otg::FGrassKind& X) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(100 * Mid(X) / FMath::Max(0.01f, Mid(Base)))); })));
	Add(Slider(TEXT("Height varies"), 0, 0.7f, Get([](const otg::FGrassKind& X) { return Spread(X); }), Put([](otg::FGrassKind& X, float F) { SetScale(X, Mid(X), F); }),
		Say([](const otg::FGrassKind& X) { return FString::Printf(TEXT("±%d%%"), FMath::RoundToInt(100 * Spread(X))); })));
	if (K.PatchM > 0)
	{
		Add(Slider(TEXT("Patches"), 4, 60, Get([](const otg::FGrassKind& X) { return X.PatchM; }), Put([](otg::FGrassKind& X, float F) { X.PatchM = FMath::RoundToFloat(F); }),
			Say([](const otg::FGrassKind& X) { return FString::Printf(TEXT("%d m across"), FMath::RoundToInt(X.PatchM)); })));
		Add(Slider(TEXT("Covers"), 0, 1, Get([](const otg::FGrassKind& X) { return Cover(X); }), Put([](otg::FGrassKind& X, float F) { SetCover(X, F); }),
			Say([](const otg::FGrassKind& X) { return FString::Printf(TEXT("about %d%%"), FMath::RoundToInt(100 * Cover(X))); })));
	}
	using G = otg::FGrassKind;
	if (K.IsKite())
		Add(Text(TEXT("Its colour: a KiteDemo plant keeps its own colours"), 11, Soft));
	else
	{
		if (!K.Fab.Num())
			Add(Slider(TEXT("Colour"), -G::HueMax, G::HueMax, Get([](const G& X) { return X.Hue; }), Put([](G& X, float F) { X.Hue = FMath::Abs(F) < 1.5f ? 0.f : FMath::RoundToFloat(F); }),
				Say([](const G& X) { const int32 D = FMath::RoundToInt(X.Hue); return D == 0 ? FString(TEXT("as it grows")) : D < 0 ? FString::Printf(TEXT("%d° golder"), -D) : FString::Printf(TEXT("%d° bluer"), D); })));
		else Add(Text(TEXT("Its colour: a Megascans plant keeps its own hue"), 11, Soft));
		Add(Slider(TEXT("Colour strength"), G::SatLo, G::SatHi, Get([](const G& X) { return X.Sat; }), Put([](G& X, float F) { X.Sat = FMath::Abs(F - 1) < 0.03f ? 1.f : F; }), Say([](const G& X) { return Pct(X.Sat); })));
		Add(Slider(TEXT("Brightness"), G::BrightLo, G::BrightHi, Get([](const G& X) { return X.Bright; }), Put([](G& X, float F) { X.Bright = FMath::Abs(F - 1) < 0.03f ? 1.f : F; }), Say([](const G& X) { return Pct(X.Bright); })));
	}
	return V;
}

TSharedRef<SWidget> SOTGStudio::BiomePanel()
{
	TSharedRef<SVerticalBox> L = SNew(SVerticalBox);
	// (all in one order, which ticking one on or off doesn't change; each folded to a line until opened)
	int32 Shown = 0;
	for (const otg::FGrassKind& K : Biome.Kinds) if (K.FarOf.IsEmpty()) Shown++;
	L->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(FString::Printf(TEXT("%d of %d kinds growing"), Biome.Growing(), Shown), 12, Gold, "Bold")]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)[Button(TEXT("Open all"), [this]() { for (const otg::FGrassKind& K : Biome.Kinds) OpenKinds.Add(K.Name); Rebuild(); }, 11)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)[Button(TEXT("Fold all"), [this]() { OpenKinds.Reset(); Rebuild(); }, 11)]
	];
	for (int32 i = 0; i < Biome.Kinds.Num(); i++)
		if (Biome.Kinds[i].FarOf.IsEmpty()) L->AddSlot().AutoHeight()[KindRow(i)];
	return L;
}

void SOTGStudio::ListEveryPlant()
{
	// (the Megascans and KiteDemo plants the biome hasn't: listed off, by name, after its own - so every
	// grass the game has is there to tick, and to see)
	TArray<otg::FGrassKind> Add;
	for (const FString& P : PlantList)
		if (!Biome.Kinds.ContainsByPredicate([&](const otg::FGrassKind& K) { return K.Fab.Contains(P); }))
		{
			otg::FGrassKind K = otg::FGrass::PlantKind(P);
			K.bOn = false;
			Add.Add(K);
		}
	Add.Sort([](const otg::FGrassKind& A, const otg::FGrassKind& B) { return A.Label < B.Label; });
	Biome.Kinds.Append(Add);
}

// (the textures make_game_assets.py makes from unreal/grass_icons/<kind>.png)
static FString PictureAsset(const FString& KindName)
{
	FString Safe;
	for (TCHAR C : KindName) Safe.AppendChar(FChar::IsAlnum(C) ? C : TEXT('_'));
	return FString::Printf(TEXT("/Game/OpenTeeGolf/Game/Runtime/GrassIcons/T_GrassIcon_%s.T_GrassIcon_%s"), *Safe, *Safe);
}

const FSlateBrush* SOTGStudio::Picture(const FString& KindName)
{
	if (const TSharedPtr<FSlateBrush>* B = Pictures.Find(KindName)) return B->Get();
	TSharedPtr<FSlateBrush> Brush;
	if (UTexture2D* T = LoadObject<UTexture2D>(nullptr, *PictureAsset(KindName), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		PictureTextures.Emplace(T);
		Brush = MakeShared<FSlateBrush>();
		Brush->SetResourceObject(T);
		Brush->ImageSize = FVector2D(44, 44);
		Brush->DrawAs = ESlateBrushDrawType::Image;
	}
	Pictures.Add(KindName, Brush);
	return Brush.Get();
}

void SOTGStudio::AddPlant(const FString& Plant)
{
	// (OTG.Studio.Add: a plant turned on - every one is listed already)
	if (bForests) return;
	if (otg::FGrassKind* K = Biome.Kinds.FindByPredicate([&](const otg::FGrassKind& X) { return X.Fab.Contains(Plant); })) K->bOn = true;
	else Biome.Kinds.Add(otg::FGrass::PlantKind(Plant));
	Changed();
	Rebuild();
}

void SOTGStudio::SetKind(const FString& Kind, const FString& Field, float V)
{
	otg::FGrassKind* K = Biome.Kinds.FindByPredicate([&](const otg::FGrassKind& X) { return X.Name == Kind; });
	if (bForests || !K) return;
	if (Field == TEXT("on")) K->bOn = V != 0;
	else if (Field == TEXT("density")) K->Density = FMath::Max(0.f, V);
	else if (Field == TEXT("height")) SetScale(*K, Mid(*K) * FMath::Max(0.05f, V), Spread(*K));
	else if (Field == TEXT("cover")) SetCover(*K, FMath::Clamp(V, 0.f, 1.f));
	Changed();
	Rebuild();
}

TSharedRef<SWidget> SOTGStudio::ForestPanel()
{
	TSharedRef<SVerticalBox> L = SNew(SVerticalBox);
	L->AddSlot().AutoHeight()[Text(TEXT("Its trees"), 14, Gold, "Bold")];
	float Total = 0;
	for (const FOTGForestSpecies& S : Forest.Species) Total += FMath::Max(0.f, S.Share);
	for (int32 i = 0; i < Forest.Species.Num(); i++)
	{
		const FOTGForestSpecies& S = Forest.Species[i];
		L->AddSlot().AutoHeight().Padding(0, 8, 0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(FOTGTreeDesign::SpeciesLabel(S.Kind), 14, Ink, "Bold")]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Text(Total > 0 ? FString::Printf(TEXT("%d%% of them"), FMath::RoundToInt(100 * FMath::Max(0.f, S.Share) / Total)) : FString(), 12, Soft)]
			+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Remove"), [this, i]() { if (Forest.Species.IsValidIndex(i)) Forest.Species.RemoveAt(i); Changed(); Rebuild(); }, 11)]
		];
		L->AddSlot().AutoHeight().Padding(18, 2, 0, 0)
		[
			Slider(TEXT("How many"), 0, 10, [this, i]() { return Forest.Species.IsValidIndex(i) ? Forest.Species[i].Share : 0.f; },
				[this, i](float F) { if (Forest.Species.IsValidIndex(i)) { Forest.Species[i].Share = FMath::RoundToFloat(F * 2) / 2; Changed(); } },
				[this, i]() { return Forest.Species.IsValidIndex(i) ? FString::Printf(TEXT("%.1f shares"), Forest.Species[i].Share) : FString(); })
		];
		// its model: the next at each click (the game's choice first)
		L->AddSlot().AutoHeight().Padding(18, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(120)[Text(TEXT("Drawn as"), 12, Soft)]]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).MinDesiredWidth(200)[Dropdown(FOTGTreeDesign::Models(), S.Model.IsEmpty() ? FString(TEXT("auto")) : S.Model, [](const FString& M) { return FOTGTreeDesign::ModelLabel(M); }, [this, i](const FString& M) {
				if (!Forest.Species.IsValidIndex(i)) return;
				Forest.Species[i].Model = M;
				Changed(); Rebuild();
			})]]
		];
	}
	// a species added
	L->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Text(TEXT("Add a species"), 13, Soft)]
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
		[
			SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&SpeciesOpts)
			.InitiallySelectedItem(SpeciesPick)
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> P) { return StaticCastSharedRef<SWidget>(Text(FOTGTreeDesign::SpeciesLabel(*P), 12)); })
			.OnSelectionChanged_Lambda([this](TSharedPtr<FString> P, ESelectInfo::Type) { if (P.IsValid()) SpeciesPick = P; })
			[SNew(STextBlock).Font(Font(12)).Text_Lambda([this]() { return FText::FromString(SpeciesPick.IsValid() ? FOTGTreeDesign::SpeciesLabel(*SpeciesPick) : FString()); })]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[Button(TEXT("Add"), [this]() {
			if (!SpeciesPick.IsValid()) return;
			Forest.Species.Add({ *SpeciesPick, 1.f, FString() });
			Changed(); Rebuild();
		}, 12)]
	];
	// how tall, how close
	L->AddSlot().AutoHeight().Padding(0, 16, 0, 2)[Text(TEXT("The forest"), 14, Gold, "Bold")];
	using P = FOTGForestPreset;
	L->AddSlot().AutoHeight().Padding(18, 2, 0, 0)[Slider(TEXT("Shortest"), P::HeightMin, P::HeightMax, [this]() { return Forest.HeightLo; },
		[this](float F) { Forest.HeightLo = FMath::RoundToFloat(F); Forest.HeightHi = FMath::Max(Forest.HeightHi, Forest.HeightLo); Changed(); }, [this]() { return FString::Printf(TEXT("%d m"), FMath::RoundToInt(Forest.HeightLo)); })];
	L->AddSlot().AutoHeight().Padding(18, 2, 0, 0)[Slider(TEXT("Tallest"), P::HeightMin, P::HeightMax, [this]() { return Forest.HeightHi; },
		[this](float F) { Forest.HeightHi = FMath::RoundToFloat(F); Forest.HeightLo = FMath::Min(Forest.HeightHi, Forest.HeightLo); Changed(); }, [this]() { return FString::Printf(TEXT("%d m"), FMath::RoundToInt(Forest.HeightHi)); })];
	L->AddSlot().AutoHeight().Padding(18, 2, 0, 0)[Slider(TEXT("How close"), P::PerHaLo, P::PerHaHi, [this]() { return Forest.PerHa; },
		[this](float F) { Forest.PerHa = FMath::RoundToFloat(F / 10) * 10; Changed(); },
		[this]() { return FString::Printf(TEXT("%d a hectare (%.0f m apart)"), FMath::RoundToInt(Forest.PerHa), 100.0 / FMath::Sqrt(FMath::Max(1.f, Forest.PerHa))); })];
	// the grass under them
	TArray<FString> Under = { FString() };
	for (const FOTGBiome& B : Biomes) Under.Add(B.Name);
	L->AddSlot().AutoHeight().Padding(18, 8, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(120)[Text(TEXT("Grass under"), 12, Soft)]]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).MinDesiredWidth(200)[Dropdown(Under, Forest.Under, [](const FString& U) { return U.IsEmpty() ? FString(TEXT("The course's own")) : U; }, [this](const FString& U) {
			Forest.Under = U;
			Changed(); Rebuild();
		})]]
	];
	return L;
}
