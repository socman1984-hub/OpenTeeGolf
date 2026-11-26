// See SOTGCourseBuilder.h: the page, the search, picking a course, building it.

#include "UI/Builder/SOTGCourseBuilder.h"

#include "UI/OTGUi.h"
#include "Course/OTGTextures.h"
#include "Course/OTGCourseSubsystem.h"
#include "App/OTGHelperSubsystem.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

using namespace otg::ui;

#include "UI/Builder/SOTGBuilderShared.h"

// ------------------------------------------------------------------ the builder

UOTGHelperSubsystem* SOTGCourseBuilder::Helper() const { return HelperSub(); }

TSharedRef<SWidget> SOTGCourseBuilder::KeySwitch(const FString& Label, const FSlateBrush* Dot, float DotSize, bool SOTGWorldMap::* Flag)
{
	static const FButtonStyle Style = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, 0.06f), 6.f))
		.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, 0.14f), 6.f))
		.SetPressed(FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, 0.10f), 6.f))
		.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
	static const FSlateRoundedBoxBrush BoxOn(FLinearColor(0.30f, 0.75f, 0.40f, 1.f), 3.f);
	static const FSlateRoundedBoxBrush BoxOff(FLinearColor::Transparent, 3.f, FLinearColor(1, 1, 1, 0.6f), 1.5f);
	auto On = [this, Flag]() { return Map.IsValid() && (*Map).*Flag; };
	return SNew(SButton).ButtonStyle(&Style).ContentPadding(FMargin(7, 4))
		.ToolTipText(FText::FromString(TEXT("Show or hide these on the map")))
		.OnClicked_Lambda([this, Flag]() { if (Map.IsValid()) (*Map).*Flag = !((*Map).*Flag); return FReply::Handled(); })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 7, 0)
			[
				SNew(SBox).WidthOverride(14).HeightOverride(14)
				[
					SNew(SBorder).BorderImage_Lambda([On]() -> const FSlateBrush* { return On() ? &BoxOn : &BoxOff; }).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(0)
					[SNew(STextBlock).Font(Font(10, "Bold")).ColorAndOpacity(FLinearColor::White).Text_Lambda([On]() { return FText::FromString(On() ? TEXT("✓") : TEXT("")); })]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(DotSize).HeightOverride(DotSize)[SNew(SBorder).BorderImage(Dot).BorderBackgroundColor_Lambda([On]() { return On() ? FLinearColor::White : FLinearColor(1, 1, 1, 0.35f); })]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5, 0, 0, 0)
			[
				SNew(STextBlock).Font(Font(12)).Text(FText::FromString(Label)).ColorAndOpacity_Lambda([On]() { return On() ? FSlateColor(Ink) : FSlateColor(FLinearColor(1, 1, 1, 0.45f)); })
			]
		];
}

void SOTGCourseBuilder::Construct(const FArguments&)
{
	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 14, 0)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[SAssignNew(Map, SOTGWorldMap)]
			// the key, and how far the world's courses are gathered (builder.js map-note)
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(10, 10, 10, 30)
			[
				Box(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Text(TEXT("Show:"), 12, Soft)]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[KeySwitch(TEXT("Built"), new FSlateRoundedBoxBrush(FLinearColor(0.18f, 0.68f, 0.31f, 1.f), 7.f, FLinearColor(1.f, 0.85f, 0.29f, 1.f), 2.f), 14, &SOTGWorldMap::bShowBuilt)]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[KeySwitch(TEXT("Not built yet"), new FSlateRoundedBoxBrush(FLinearColor(0.18f, 0.55f, 0.9f, 1.f), 6.f, FLinearColor::White, 2.f), 12, &SOTGWorldMap::bShowNotBuilt)]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)[KeySwitch(TEXT("Search results"), new FSlateRoundedBoxBrush(FLinearColor(1.f, 0.62f, 0.18f, 1.f), 6.f, FLinearColor::White, 2.f), 12, &SOTGWorldMap::bShowFound)]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(16).HeightOverride(16)[SNew(SBorder).BorderImage(new FSlateRoundedBoxBrush(FLinearColor(0.23f, 0.56f, 0.85f, 0.9f), 8.f, FLinearColor::White, 2.f))]]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5, 0, 0, 0)[Text(TEXT("Several: click to zoom in"), 12)]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
					[
						SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity_Lambda([this]() { return bWorldDone ? FSlateColor(Soft) : FSlateColor(Gold); }).Text_Lambda([this]() {
							int32 Built = 0;
							for (const FOTGMapCourse& M : Courses) Built += M.BuiltId.IsEmpty() ? 0 : 1;
							if (bWorldDone) return FText::FromString(FString::Printf(TEXT("%s golf courses (%d built) - click one to build it, or search"), *FText::AsNumber(Courses.Num()).ToString(), Built));
							// (from the list that comes with the game, checked for courses mapped since)
							if (WorldSeeded > 0)
								return FText::FromString(FString::Printf(TEXT("%s golf courses (%d built) - checking OpenStreetMap for new ones (%d%%)"),
									*FText::AsNumber(Courses.Num()).ToString(), Built, FMath::RoundToInt(WorldProgress * 100)));
							return FText::FromString(FString::Printf(TEXT("Gathering every golf course in OpenStreetMap: %s so far (%d%% of the world; the first time takes a few minutes)"),
								*FText::AsNumber(Courses.Num()).ToString(), FMath::RoundToInt(WorldProgress * 100)));
						})
					], FMargin(10, 6), FLinearColor(0.02f, 0.03f, 0.02f, 0.78f))
			]
			// Map / Aerial photos
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(10)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)[Button(TEXT("Map"), [this]() { Map->SetLayer(TEXT("map")); }, 12)]
				+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Aerial photos"), [this]() { Map->SetLayer(TEXT("photo")); }, 12)]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SAssignNew(Side, SBox).WidthOverride(440)
			[
				SNew(SVerticalBox)
				// the grass biomes and forests the course editor gives a course (SOTGStudio)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					// (the words above the buttons: beside them a narrow column cut them off)
					Box(SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[Text(TEXT("Grass and trees for your courses"), 13, Soft)]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Grass biomes"), [this]() { if (OnStudio) OnStudio(false); }, 12)]
							+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)[Button(TEXT("Forests"), [this]() { if (OnStudio) OnStudio(true); }, 12)]
						], FMargin(12, 8), Solid)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					Box(SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[Text(TEXT("Find a course"), 15, Gold, "Bold")]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1)
							[
								SAssignNew(SearchBox, SEditableTextBox).Font(Font(14)).HintText(FText::FromString(TEXT("Course name or town")))
								.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type T) { if (T == ETextCommit::OnEnter) Search(); })
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)[Button(TEXT("Search"), [this]() { Search(); }, 13)]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
						[
							SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(400).Text_Lambda([this]() {
								return FText::FromString(!Status.IsEmpty() ? Status
									: bWorldDone ? FString::Printf(TEXT("%d golf courses on the map"), Courses.Num())
									: FString::Printf(TEXT("Finding the world's golf courses: %d so far (%d%%)"), Courses.Num(), FMath::RoundToInt(WorldProgress * 100)));
							})
						], FMargin(12, 10), Solid)
				]
				+ SVerticalBox::Slot().FillHeight(1)[SAssignNew(Lower, SBox)]
			]
		]
	];
	Map->SetCourses(&Courses);
	Map->OnPick = [this](int32 I) { Pick(I, false); };
	if (UOTGHelperSubsystem* H = Helper())
	{
		// (bound weakly: the helper outlives this page, and a course can finish after it's closed)
		JobHandle = H->OnJobDone.AddSP(this, &SOTGCourseBuilder::JobDone);
		FString Why;
		if (!H->Available(&Why)) Status = Why;
	}
	LoadBuilt();
	Rebuild();
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SOTGCourseBuilder::Pulse));
	if (FParse::Value(FCommandLine::Get(), TEXT("buildertest="), TestQuery, false) && !TestQuery.IsEmpty())
	{
		TestQuery.ReplaceInline(TEXT("+"), TEXT(" "));
		bTestBuild = FParse::Param(FCommandLine::Get(), TEXT("buildertestbuild"));
		TestStep = 1;
		TestAt = FPlatformTime::Seconds() + 3;
	}
}

EActiveTimerReturnType SOTGCourseBuilder::Pulse(double Time, float Dt)
{
	// the world's courses: asked for a page at a time while the helper gathers them
	if (!bWorldDone && !bWorldAsking && FPlatformTime::Seconds() >= NextWorld) LoadWorld();
	// (the test: one step at a time)
	if (TestStep > 0 && FPlatformTime::Seconds() >= TestAt)
	{
		UOTGHelperSubsystem* H = Helper();
		auto Shot = [](const TCHAR* Name) { if (GEngine) GEngine->Exec(nullptr, *FString::Printf(TEXT("OTG.Shot %s"), Name)); };
		auto Quit = []() { FPlatformMisc::RequestExit(false, TEXT("buildertest")); };
		// (-buildertest=world: the map zoomed out, then over Ohio, no search)
		if (TestStep == 1 && TestQuery == TEXT("world")) { Map->FlyTo(38, -40, 2.6); TestStep = 20; TestAt = FPlatformTime::Seconds() + 25; }
		// (each shot a frame before the map moves on: a screenshot is taken at the end of its frame)
		else if (TestStep == 20) { Shot(TEXT("builder_world")); TestStep = 21; TestAt = FPlatformTime::Seconds() + 1; }
		else if (TestStep == 21) { Map->FlyTo(40, -84, 6.5); TestStep = 22; TestAt = FPlatformTime::Seconds() + 10; }
		else if (TestStep == 22) { Shot(TEXT("builder_ohio")); TestStep = 23; TestAt = FPlatformTime::Seconds() + 1; }
		else if (TestStep == 23) { Map->FlyTo(39.33, -84.43, 10); TestStep = 24; TestAt = FPlatformTime::Seconds() + 10; }
		else if (TestStep == 24) { Shot(TEXT("builder_near")); TestStep = 9; TestAt = FPlatformTime::Seconds() + 3; }
		else if (TestStep == 1) { SearchBox->SetText(FText::FromString(TestQuery)); Search(); TestStep = 2; TestAt = FPlatformTime::Seconds() + 1; }
		else if (TestStep == 2 && Found.Num()) { Pick(Found[0], true); if (Courses.IsValidIndex(Picked) && Courses[Picked].BuiltId.IsEmpty()) Check(); TestStep = 3; TestAt = FPlatformTime::Seconds() + 25; }
		else if (TestStep == 2 && !Status.StartsWith(TEXT("Searching")) && FPlatformTime::Seconds() > TestAt + 60) { UE_LOG(LogTemp, Warning, TEXT("buildertest: nothing found")); Quit(); TestStep = 0; }
		else if (TestStep == 3 && !bChecking) { Shot(TEXT("builder_picked")); TestStep = bTestBuild ? 4 : 9; TestAt = FPlatformTime::Seconds() + 3; }
		else if (TestStep == 4)
		{
			if (Courses.IsValidIndex(Picked) && !Courses[Picked].BuiltId.IsEmpty()) { UE_LOG(LogTemp, Display, TEXT("buildertest: built already (%s): exporting"), *Courses[Picked].BuiltId); if (H) H->Export(Courses[Picked].BuiltId); }
			else StartBuild();
			// (-buildertestexport=<id>: another course queued with it, put in the game meanwhile)
			FString Also;
			if (H && FParse::Value(FCommandLine::Get(), TEXT("buildertestexport="), Also)) H->Export(Also);
			TestStep = 5; TestAt = FPlatformTime::Seconds() + 30;
		}
		else if (TestStep == 5) { Shot(TEXT("builder_building")); TestStep = 6; TestAt = FPlatformTime::Seconds() + 5; }
		else if (TestStep == 6 && H && H->GetQueue().Num() && !H->ActiveCount())
		{
			UE_LOG(LogTemp, Display, TEXT("buildertest: %s %s %s (ready: %s)"), *H->GetJob().Kind, *H->GetJob().State, *H->GetJob().Message, *ReadyId);
			Shot(TEXT("builder_done"));
			TestStep = 9; TestAt = FPlatformTime::Seconds() + 4;
		}
		else if (TestStep == 6) TestAt = FPlatformTime::Seconds() + 5;
		else if (TestStep == 9) { TestStep = 0; Quit(); }
	}
	// the queue: its card kept up to date
	if (UOTGHelperSubsystem* H = Helper())
	{
		FString Now = H->GetJob().State + H->GetJob().Message;
		for (const FOTGHelperJob& J : H->GetQueue()) Now += J.QueueId + J.Kind + J.State + J.Message + FString::FromInt(FMath::RoundToInt(J.Frac * 100));
		if (Now != JobShown) { JobShown = Now; Rebuild(); }
	}
	return EActiveTimerReturnType::Continue;
}

void SOTGCourseBuilder::LoadWorld()
{
	UOTGHelperSubsystem* H = Helper();
	if (!H || !H->Available()) { bWorldDone = true; return; }
	bWorldAsking = true;
	TWeakPtr<SOTGCourseBuilder> Weak = SharedThis(this);
	H->World(WorldFrom, [Weak](int32 Code, TSharedPtr<FJsonValue> J, const FString& Body) {
		TSharedPtr<SOTGCourseBuilder> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		Self->bWorldAsking = false;
		Self->NextWorld = FPlatformTime::Seconds() + 2.0;
		const TSharedPtr<FJsonObject> O = J.IsValid() ? J->AsObject() : nullptr;
		if (Code != 200 || !O.IsValid()) { Self->NextWorld = FPlatformTime::Seconds() + 10.0; return; }
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (O->TryGetArrayField(TEXT("courses"), Rows))
			for (const TSharedPtr<FJsonValue>& V : *Rows)
			{
				// [lat, lon, n|w|r, id, name]
				const TArray<TSharedPtr<FJsonValue>>& R = V->AsArray();
				if (R.Num() < 5) continue;
				FOTGMapCourse M;
				M.Lat = R[0]->AsNumber(); M.Lon = R[1]->AsNumber(); M.OsmType = OsmTypeName(R[2]->AsString()); M.OsmId = int64(R[3]->AsNumber()); M.Name = R[4]->AsString();
				if (M.Name.IsEmpty()) M.Name = TEXT("Golf course");
				if (Self->ById.Contains(M.OsmId)) continue;
				for (const FBuilt& B : Self->Built) if (B.OsmId == M.OsmId) { M.BuiltId = B.Id; M.bInGame = B.bInGame; }
				Self->ById.Add(M.OsmId, Self->Courses.Add(M));
			}
		Self->WorldFrom += Rows ? Rows->Num() : 0;
		Self->bWorldDone = O->GetBoolField(TEXT("done"));
		Self->WorldProgress = O->GetNumberField(TEXT("progress"));
		O->TryGetNumberField(TEXT("seeded"), Self->WorldSeeded);
	});
}

void SOTGCourseBuilder::LoadBuilt()
{
	UOTGHelperSubsystem* H = Helper();
	if (!H || !H->Available()) return;
	TWeakPtr<SOTGCourseBuilder> Weak = SharedThis(this);
	H->Get(TEXT("/api/courses"), [Weak](int32 Code, TSharedPtr<FJsonValue> J, const FString&) {
		TSharedPtr<SOTGCourseBuilder> Self = Weak.Pin();
		const TSharedPtr<FJsonObject> O = J.IsValid() ? J->AsObject() : nullptr;
		if (!Self.IsValid() || Code != 200 || !O.IsValid()) return;
		// (the ones the game has: exported to its courses folder)
		TSet<FString> InGame;
		if (UGameInstance* G = GI()) if (UOTGCourseSubsystem* C = G->GetSubsystem<UOTGCourseSubsystem>()) for (const FOTGCourseInfo& I : C->ListCourses()) if (!I.ExportDir.IsEmpty()) InGame.Add(I.Id);
		Self->Built.Reset();
		for (const TSharedPtr<FJsonValue>& V : O->GetArrayField(TEXT("courses")))
		{
			const TSharedPtr<FJsonObject> C = V->AsObject();
			if (!C.IsValid()) continue;
			FBuilt B;
			B.Id = C->GetStringField(TEXT("id"));
			B.Name = C->GetStringField(TEXT("name"));
			C->TryGetStringField(TEXT("location"), B.Location);
			double Id = 0;
			if (C->TryGetNumberField(TEXT("osmId"), Id)) B.OsmId = int64(Id);
			B.bInGame = InGame.Contains(B.Id);
			Self->Built.Add(B);
			double La = 0, Lo = 0;
			C->TryGetNumberField(TEXT("lat"), La); C->TryGetNumberField(TEXT("lon"), Lo);
			// (its dot: the world's list's, or one of its own until that list brings it)
			int32 Idx = INDEX_NONE;
			if (const int32* Have = B.OsmId ? Self->ById.Find(B.OsmId) : nullptr) Idx = *Have;
			else if (La || Lo)
			{
				FOTGMapCourse M;
				M.Lat = La; M.Lon = Lo; M.Name = B.Name; M.Location = B.Location; M.OsmId = B.OsmId;
				FString T;
				M.OsmType = C->TryGetStringField(TEXT("osmType"), T) ? T : FString(TEXT("way"));
				Idx = Self->Courses.Add(M);
				if (B.OsmId) Self->ById.Add(B.OsmId, Idx);
			}
			if (Self->Courses.IsValidIndex(Idx)) { Self->Courses[Idx].BuiltId = B.Id; Self->Courses[Idx].bInGame = B.bInGame; }
		}
		Self->Rebuild();
	});
}

void SOTGCourseBuilder::Search()
{
	UOTGHelperSubsystem* H = Helper();
	const FString Q = SearchBox.IsValid() ? SearchBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (!H || Q.Len() < 3) return;
	Status = TEXT("Searching OpenStreetMap…");
	Rebuild();
	TWeakPtr<SOTGCourseBuilder> Weak = SharedThis(this);
	H->Search(Q, [Weak](int32 Code, TSharedPtr<FJsonValue> J, const FString& Body) {
		TSharedPtr<SOTGCourseBuilder> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		const TSharedPtr<FJsonObject> O = J.IsValid() ? J->AsObject() : nullptr;
		FString Err;
		if (Code != 200 || !O.IsValid()) { Self->Status = O.IsValid() && O->TryGetStringField(TEXT("error"), Err) ? Err : FString(TEXT("The search didn't work: try again in a minute")); Self->Rebuild(); return; }
		for (int32 F : Self->Found) if (Self->Courses.IsValidIndex(F)) Self->Courses[F].bFound = false;
		Self->Found.Reset();
		for (const TSharedPtr<FJsonValue>& V : O->GetArrayField(TEXT("results")))
		{
			const TSharedPtr<FJsonObject> R = V->AsObject();
			if (!R.IsValid()) continue;
			const int64 Id = int64(R->GetNumberField(TEXT("osmId")));
			int32 I;
			if (const int32* Have = Self->ById.Find(Id)) I = *Have;
			else
			{
				FOTGMapCourse M;
				M.Lat = R->GetNumberField(TEXT("lat")); M.Lon = R->GetNumberField(TEXT("lon"));
				M.OsmType = R->GetStringField(TEXT("osmType")); M.OsmId = Id;
				I = Self->Courses.Add(M);
				Self->ById.Add(Id, I);
			}
			FOTGMapCourse& M = Self->Courses[I];
			M.Name = R->GetStringField(TEXT("name"));
			R->TryGetStringField(TEXT("location"), M.Location);
			FString B;
			if (R->TryGetStringField(TEXT("built"), B)) M.BuiltId = B;
			M.bFound = true;
			Self->Found.Add(I);
		}
		Self->Status = Self->Found.Num() ? FString::Printf(TEXT("%d found"), Self->Found.Num()) : FString(TEXT("No golf course by that name on OpenStreetMap"));
		if (Self->Found.Num() == 1) Self->Pick(Self->Found[0], true);
		else Self->Rebuild();
	});
}

void SOTGCourseBuilder::Pick(int32 I, bool bFly)
{
	if (!Courses.IsValidIndex(I)) return;
	Picked = I;
	Map->SetPicked(I);
	Checked.Reset();
	bCheckFailed = false;
	Group.Empty();
	ReadyId.Empty();
	Status.Empty();
	FOTGMapCourse& M = Courses[I];
	NameDraft = M.Name;
	if (bFly) Map->FlyTo(M.Lat, M.Lon, 15);
	// (where it is, when the world list didn't say)
	if (M.Location.IsEmpty())
		if (UOTGHelperSubsystem* H = Helper())
		{
			TWeakPtr<SOTGCourseBuilder> Weak = SharedThis(this);
			H->Where(M.Lat, M.Lon, [Weak, I](int32 Code, TSharedPtr<FJsonValue> J, const FString&) {
				TSharedPtr<SOTGCourseBuilder> Self = Weak.Pin();
				FString L;
				if (Self.IsValid() && Code == 200 && J.IsValid() && J->AsObject().IsValid() && J->AsObject()->TryGetStringField(TEXT("location"), L) && Self->Courses.IsValidIndex(I))
				{
					Self->Courses[I].Location = L;
					Self->Rebuild();
				}
			});
		}
	// (what's mapped there is looked at when asked: PickedCard's button)
	Rebuild();
}

void SOTGCourseBuilder::Check()
{
	UOTGHelperSubsystem* H = Helper();
	if (!H || !Courses.IsValidIndex(Picked)) return;
	bChecking = true;
	bCheckFailed = false;
	CheckStarted = FPlatformTime::Seconds();
	const int32 I = Picked;
	TWeakPtr<SOTGCourseBuilder> Weak = SharedThis(this);
	H->Check(Courses[I].OsmType, Courses[I].OsmId, [Weak, I](int32 Code, TSharedPtr<FJsonValue> J, const FString& Body) {
		TSharedPtr<SOTGCourseBuilder> Self = Weak.Pin();
		if (!Self.IsValid() || Self->Picked != I) return;
		Self->bChecking = false;
		const TSharedPtr<FJsonObject> O = J.IsValid() ? J->AsObject() : nullptr;
		FString Err;
		if (Code != 200 || !O.IsValid())
		{
			Self->bCheckFailed = true;
			Self->Status = O.IsValid() && O->TryGetStringField(TEXT("error"), Err) ? Err : FString(TEXT("OpenStreetMap didn't answer (it's busy at times)"));
			Self->Rebuild();
			return;
		}
		Self->Checked = O;
		const FString Mapped = O->GetStringField(TEXT("name"));
		Self->NameDraft = Mapped.IsEmpty() || Mapped == TEXT("Unnamed course") ? Self->Courses[I].Name : Mapped;
		const TArray<TSharedPtr<FJsonValue>>* G = nullptr;
		if (O->TryGetArrayField(TEXT("groups"), G) && G->Num()) Self->Group = (*G)[0]->AsObject()->GetStringField(TEXT("name"));
		Self->Rebuild();
	});
	Rebuild();
}

void SOTGCourseBuilder::StartBuild()
{
	UOTGHelperSubsystem* H = Helper();
	if (!H || !Courses.IsValidIndex(Picked) || H->ActiveFor(FString(), Courses[Picked].OsmId)) return;
	const FOTGMapCourse& M = Courses[Picked];
	const FString Name = NameDraft.TrimStartAndEnd();
	H->Build(M.OsmType, M.OsmId, Name.IsEmpty() ? M.Name : Name, M.Location, Group);
	Rebuild();
}

void SOTGCourseBuilder::JobDone(const FOTGHelperJob& J)
{
	// (the queue builds, then puts it in the game, itself: here the lists catch up)
	LoadBuilt();
	if (J.Kind == TEXT("export") && J.State == TEXT("done")) ReadyId = J.CourseId;
	Rebuild();
}
