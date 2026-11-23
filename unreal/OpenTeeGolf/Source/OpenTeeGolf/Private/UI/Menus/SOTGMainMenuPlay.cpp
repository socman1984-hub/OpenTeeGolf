// The main menu's Play page (see SOTGMainMenu.h): the courses, searched and sorted, and for the one
// picked the tees, pins, holes, weather, players and game.

#include "UI/Menus/SOTGMainMenu.h"
#include "UI/Menus/SOTGLaunchMonitorPanel.h"
#include "Round/OTGMiniGolf.h"
#include "UI/Builder/SOTGCourseBuilder.h"
#include "UI/Editor/SOTGCourseEditor.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "GeneralProjectSettings.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "App/OTGUpdateSubsystem.h"
#include "App/OTGHelperSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Course/OTGTextures.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Course/OTGCourseData.h"
#include "OTGLaunchMonitorSubsystem.h"
#include "UI/OTGUi.h"
#include "Round/OTGWeather.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Engine/Texture2D.h"

using namespace otg::ui;

#include "UI/Menus/SOTGMenuStyle.h"

// ------------------------------------------------------------------ the Play page

FDateTime SOTGMainMenu::LastPlayedOf(const FOTGCourseInfo& Info) const
{
	FDateTime Best(0);
	if (const FDateTime* T = Setup.LastPlayed.Find(Info.Id)) Best = *T;
	// (and the players' saved rounds there: kept in local time, by the course's name)
	const FTimespan ToUtc = FDateTime::UtcNow() - FDateTime::Now();
	for (const FOTGPlayerProfile& P : Profiles)
		for (const FOTGPlayedRound& R : P.Rounds)
			if (R.Course == Info.Name && R.At + ToUtc > Best) Best = R.At + ToUtc;
	return Best;
}

FDateTime SOTGMainMenu::AddedOf(const FOTGCourseInfo& Info) const
{
	if (const FDateTime* T = AddedAt.Find(Info.Id)) return *T;
	// (an exported course: when its folder was made; a pack: never, so last)
	FDateTime T(0);
	if (!Info.ExportDir.IsEmpty())
	{
		const FFileStatData S = IFileManager::Get().GetStatData(*Info.ExportDir);
		if (S.bIsValid) T = S.CreationTime;
		if (T.GetTicks() <= 0 || T == FDateTime::MinValue()) T = IFileManager::Get().GetTimeStamp(*FPaths::Combine(Info.ExportDir, TEXT("play.json")));
	}
	AddedAt.Add(Info.Id, T);
	return T;
}

TArray<int32> SOTGMainMenu::ShownCourses() const
{
	// every word typed somewhere in the name or the place
	TArray<FString> Words;
	Search.ParseIntoArrayWS(Words);
	TArray<int32> Out;
	TArray<FDateTime> Key;
	Key.SetNum(CourseList.Num());
	for (int32 i = 0; i < CourseList.Num(); i++)
	{
		const FOTGCourseInfo& I = CourseList[i];
		const FString Hay = I.Name + TEXT(" ") + I.Location + TEXT(" ") + I.Id;
		bool bAll = true;
		for (const FString& W : Words) if (!Hay.Contains(W, ESearchCase::IgnoreCase)) { bAll = false; break; }
		if (!bAll) continue;
		Out.Add(i);
		if (Sort == ESort::Recent) Key[i] = LastPlayedOf(I);
		else if (Sort == ESort::Added) Key[i] = AddedOf(I);
	}
	Out.StableSort([&](int32 A, int32 B) {
		if (Sort != ESort::Name && Key[A] != Key[B]) return Key[A] > Key[B];
		return CourseList[A].Name.Compare(CourseList[B].Name, ESearchCase::IgnoreCase) < 0;
	});
	return Out;
}

void SOTGMainMenu::FillCourseList()
{
	if (!CourseRows) return;
	CourseRows->ClearChildren();
	const TArray<int32> Shown = ShownCourses();
	if (CourseCount)
		CourseCount->SetText(FText::FromString(Shown.Num() == CourseList.Num() ? FString::FromInt(CourseList.Num()) : FString::Printf(TEXT("%d of %d"), Shown.Num(), CourseList.Num())));
	if (!CourseList.Num())
	{
		CourseRows->AddSlot().AutoHeight().Padding(4, 8)[Para(TEXT("No courses yet. Build one on the New course page, or put exported courses in the courses folder."), 14)];
		return;
	}
	if (!Shown.Num())
	{
		CourseRows->AddSlot().AutoHeight().Padding(4, 8, 4, 6)[Para(FString::Printf(TEXT("No courses match “%s”."), *Search.TrimStartAndEnd()), 14)];
		CourseRows->AddSlot().AutoHeight().HAlign(HAlign_Left)[Ghost(TEXT("Clear the search"), [this]() { Search.Empty(); Rebuild(); }, 13)];
		return;
	}
	for (const int32 Ix : Shown)
	{
		const FOTGCourseInfo& I = CourseList[Ix];
		const FString Id = I.Id;
		const bool bOn = Setup.CourseId == Id;
		const FDateTime Played = LastPlayedOf(I);
		FString Note;
		if (Sort == ESort::Added && AddedOf(I).GetTicks() > 0) Note = TEXT("Added ") + Ago(AddedOf(I));
		else if (Played.GetTicks() > 0) Note = TEXT("Played ") + Ago(Played);
		CourseRows->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SButton).ButtonStyle(&RowStyle(bOn)).ContentPadding(FMargin(8))
			.OnClicked_Lambda([this, Id]() { Setup.CourseId = Id; Remember(); Rebuild(); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)
				[
					SNew(SBox).WidthOverride(108).HeightOverride(72)[SNew(SImage).Image_Lambda([this, Id]() { return ThumbOf(Id); })]
				]
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(I.Name)).Font(Font(15, "Bold")).ColorAndOpacity(Ink).AutoWrapText(true)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)[SNew(STextBlock).Text(FText::FromString(I.Location)).Font(Font(12)).ColorAndOpacity(Soft).AutoWrapText(true)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)[Note.IsEmpty() ? SNullWidget::NullWidget : StaticCastSharedRef<SWidget>(Caps(Note, 9, bOn ? Accent : Dim))]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 6, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Text(FString::FromInt(I.Holes), 18, bOn ? Ink : Soft, "Bold")]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Caps(TEXT("holes"), 8)]
				]
			]
		];
	}
}

TSharedRef<SWidget> SOTGMainMenu::PlayPage()
{
	CourseScroll.Reset();
	OptionsScroll.Reset();
	UOTGCourseSubsystem* Subs = Courses();
	const FString Folder = Subs ? Subs->CoursesFolder() : FString();
	auto OpenFolder = [Folder]() { if (Folder.IsEmpty()) return; IFileManager::Get().MakeDirectory(*Folder, true); FPlatformProcess::ExploreFolder(*Folder); };
	auto ToBuild = [this]() { Page = EPage::Build; Rebuild(); };

	// ---- the courses: a search, the order, the list
	TSharedRef<SWidget> Browser = Card(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Text(TEXT("Courses"), 20, Ink, "Bold")]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 2, 0, 0)
			[
				SNew(SBorder).BorderImage(PillBrush()).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.10f)).Padding(FMargin(9, 2))
				[SAssignNew(CourseCount, STextBlock).Font(Font(11, "Bold")).ColorAndOpacity(Soft)]
			]
			+ SHorizontalBox::Slot().FillWidth(1)[SNullWidget::NullWidget]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Ghost(TEXT("+ New course"), ToBuild, 13)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 10)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SEditableTextBox).Style(&SearchStyle()).Font(Font(14)).ForegroundColor(Ink)
				.HintText(FText::FromString(TEXT("Search courses by name or place")))
				.Text(FText::FromString(Search))
				.IsEnabled(CourseList.Num() > 0)
				.OnTextChanged_Lambda([this](const FText& T) { Search = T.ToString(); FillCourseList(); })
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type How) {
					// (Enter: the first match, if the course picked isn't one)
					Search = T.ToString();
					if (How != ETextCommit::OnEnter) return;
					const TArray<int32> Shown = ShownCourses();
					if (Shown.Num() && !Shown.ContainsByPredicate([&](int32 i) { return CourseList[i].Id == Setup.CourseId; }))
					{
						Setup.CourseId = CourseList[Shown[0]].Id;
						Remember();
						Rebuild();
					}
				})
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(SBox).Visibility_Lambda([this]() { return Search.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				[Ghost(TEXT("Clear"), [this]() { Search.Empty(); Rebuild(); }, 12)]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
		[
			SNew(SHorizontalBox).Visibility(CourseList.Num() ? EVisibility::Visible : EVisibility::Collapsed)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 10, 0)[Caps(TEXT("Sort"), 10)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Chip(TEXT("A–Z"), [this]() { Sort = ESort::Name; Rebuild(); }, Sort == ESort::Name, FLinearColor::Transparent, 12)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Chip(TEXT("Recently played"), [this]() { Sort = ESort::Recent; Rebuild(); }, Sort == ESort::Recent, FLinearColor::Transparent, 12)]
			+ SHorizontalBox::Slot().AutoWidth()[Chip(TEXT("Newest"), [this]() { Sort = ESort::Added; Rebuild(); }, Sort == ESort::Added, FLinearColor::Transparent, 12)]
		]
		+ SVerticalBox::Slot().FillHeight(1)
		[
			SAssignNew(CourseScroll, SScrollBox)
			+ SScrollBox::Slot().Padding(0, 0, 6, 0)[SAssignNew(CourseRows, SVerticalBox)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Caps(TEXT("Courses folder"), 9)]
			+ SHorizontalBox::Slot().AutoWidth()[Ghost(TEXT("Open"), OpenFolder, 12)]
			+ SHorizontalBox::Slot().AutoWidth()[Ghost(TEXT("Refresh"), [this]() { if (UOTGCourseSubsystem* S = Courses()) CourseList = S->ListCourses(); AddedAt.Reset(); Rebuild(); }, 12)]
		], FMargin(18, 16));
	FillCourseList();

	// ---- no courses at all: how to get one
	if (!CourseList.Num())
	{
		TSharedRef<SWidget> Welcome = Card(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Caps(TEXT("Get started"), 12, Gold)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[Text(TEXT("Build your first course"), 34, Ink, "Bold")]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
			[
				SNew(SBox).MaxDesiredWidth(640)[Para(TEXT("OpenTeeGolf plays real golf courses. Find one on the world map and the game builds it for you: the holes, greens, bunkers, trees and water, from open map data and aerial photos."), 16)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 24, 0, 0).HAlign(HAlign_Left)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).ButtonStyle(&ButtonStyle(true)).ContentPadding(FMargin(28, 12)).OnClicked_Lambda([ToBuild]() { ToBuild(); return FReply::Handled(); })
					[Text(TEXT("Find a course"), 18, Ink, "Bold")]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)[Ghost(TEXT("Open the courses folder"), OpenFolder, 14)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 28, 0, 0)
			[
				SNew(SBox).MaxDesiredWidth(640)[Para(TEXT("Courses exported from the web version go in ") + Folder + TEXT("."), 12, Dim)]
			], FMargin(40, 36));
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.34f).Padding(0, 0, 20, 0)[Browser]
			+ SHorizontalBox::Slot().FillWidth(0.66f).VAlign(VAlign_Top)[Welcome];
	}

	UOTGCourseData* Course = Subs && !Setup.CourseId.IsEmpty() ? Subs->LoadCourse(Setup.CourseId) : nullptr;
	const FOTGCourseInfo* Info = CourseList.FindByPredicate([&](const FOTGCourseInfo& I) { return I.Id == Setup.CourseId; });

	// ---- the round's options, in cards
	auto Option = [](const FString& Label, const TSharedRef<SWidget>& W) {
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 7)[Caps(Label, 10)]
			+ SVerticalBox::Slot().AutoHeight()[W];
	};
	auto Section = [](const FString& Title, const TSharedRef<SVerticalBox>& Rows, const TSharedRef<SWidget>& Extra = SNullWidget::NullWidget) {
		return Card(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(Title, 16, Ink, "Bold")]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Extra]
			]
			+ SVerticalBox::Slot().AutoHeight()[Rows], FMargin(20, 16));
	};
	auto Chips = [this](const TArray<FString>& Values, TFunction<FString()> Get, TFunction<void(const FString&)> Set, TFunction<FString(const FString&)> Name = nullptr, bool bDots = false) {
		TSharedRef<SWrapBox> W = SNew(SWrapBox).UseAllottedSize(true);
		for (const FString& V : Values)
			W->AddSlot().Padding(0, 0, 6, 6)[Chip(Name ? Name(V) : V, [this, Set, V]() { Set(V); Remember(); Rebuild(); }, Get() == V, bDots ? TeeColour(V) : FLinearColor::Transparent)];
		return W;
	};

	// the round: tees, pins, how many holes, where to start
	TSharedRef<SVerticalBox> RoundRows = SNew(SVerticalBox);
	TArray<FString> Tees, Pins;
	int32 Par = 0;
	double Yards = 0;
	if (Course)
	{
		for (const TCHAR* T : TeeOrder) for (const FOTGHole& H : Course->Holes) if (H.Tees.Contains(T)) { Tees.AddUnique(T); break; }
		for (const FOTGHole& H : Course->Holes) for (const auto& T : H.Tees) Tees.AddUnique(T.Key);
		for (const TCHAR* P : PinDays) for (const FOTGHole& H : Course->Holes) if (H.Pins.Contains(P)) { Pins.AddUnique(P); break; }
		for (const FOTGHole& H : Course->Holes) for (const auto& P : H.Pins) Pins.AddUnique(P.Key);
		if (!Tees.Contains(Setup.Tee) && Tees.Num()) Setup.Tee = Tees.Contains(TEXT("White")) ? TEXT("White") : Tees[0];
		if (!Pins.Contains(Setup.Pin) && Pins.Num()) Setup.Pin = Pins.Contains(TEXT("Saturday")) ? TEXT("Saturday") : Pins[0];
		// (par, and the length from the tees picked: tee, the aim points, the pin; course metres)
		for (const FOTGHole& H : Course->Holes)
		{
			Par += H.Par;
			const FVector* T = H.Tees.Find(Setup.Tee);
			if (!T) continue;
			FVector2D At(T->X, T->Z);
			auto Walk = [&](const FVector& P) { Yards += FVector2D::Distance(At, FVector2D(P.X, P.Z)) * 1.0936; At = FVector2D(P.X, P.Z); };
			for (const FVector& A : H.Aims) Walk(A);
			const FVector* Pin = H.Pins.Find(Setup.Pin);
			Walk(Pin ? *Pin : H.GreenCenter);
		}
		if (Tees.Num()) RoundRows->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[Option(TEXT("Tees"), Chips(Tees, [this]() { return Setup.Tee; }, [this](const FString& V) { Setup.Tee = V; }, nullptr, true))];
		if (Pins.Num()) RoundRows->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[Option(TEXT("Pins"), Chips(Pins, [this]() { return Setup.Pin; }, [this](const FString& V) { Setup.Pin = V; }))];
		const int32 N = Course->Holes.Num();
		Setup.StartHole = FMath::Clamp(Setup.StartHole, 1, FMath::Max(1, N));
		RoundRows->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 28, 0)
			[
				Option(TEXT("Holes"), SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Chip(TEXT("18"), [this]() { Setup.Holes = 18; Remember(); Rebuild(); }, Setup.Holes != 9)]
					+ SHorizontalBox::Slot().AutoWidth()[Chip(TEXT("9"), [this]() { Setup.Holes = 9; Remember(); Rebuild(); }, Setup.Holes == 9)])
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Option(TEXT("Start at hole"), SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Chip(TEXT("1"), [this]() { Setup.StartHole = 1; Remember(); Rebuild(); }, Setup.StartHole == 1)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[N > 9 ? Chip(TEXT("10"), [this]() { Setup.StartHole = 10; Remember(); Rebuild(); }, Setup.StartHole == 10) : SNullWidget::NullWidget]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(72)[SNew(SSpinBox<int32>).Style(&SpinStyle()).MinValue(1).MaxValue(FMath::Max(1, N)).Font(Font(13)).Value_Lambda([this]() { return Setup.StartHole; })
							.OnValueCommitted_Lambda([this](int32 V, ETextCommit::Type) { Setup.StartHole = V; Remember(); Rebuild(); })]
					])
			]
		];
	}
	else RoundRows->AddSlot().AutoHeight()[Para(TEXT("This course didn't load: try Refresh, or build it again on the New course page."), 14, Warn)];

	// the weather
	TSharedRef<SVerticalBox> WeatherRows = SNew(SVerticalBox);
	FString WeatherName;
	{
		TArray<FString> Keys, Names;
		for (const auto& C : FOTGWeather::Choices()) { Keys.Add(C.Key); Names.Add(ShortName(C.Value)); }
		const int32 At = Keys.IndexOfByKey(Setup.Weather);
		WeatherName = At >= 0 ? Names[At] : Setup.Weather;
		WeatherRows->AddSlot().AutoHeight()[Option(TEXT("Weather"), Chips(Keys, [this]() { return Setup.Weather; }, [this](const FString& V) { Setup.Weather = V; },
			[Keys, Names](const FString& K) { const int32 I = Keys.IndexOfByKey(K); return I >= 0 ? Names[I] : K; }))];
		if (Setup.Weather == TEXT("live"))
			WeatherRows->AddSlot().AutoHeight().Padding(0, 2, 0, 0)[Para(TEXT("Live: the wind, rain and temperature at the course right now."), 12, Dim)];
		if (Setup.Weather == TEXT("custom"))
		{
			TSharedRef<SWrapBox> Dir = SNew(SWrapBox).UseAllottedSize(true);
			const TPair<FString, FString> Dirs[] = { { TEXT("0"), TEXT("Into you") }, { TEXT("180"), TEXT("Behind you") }, { TEXT("90"), TEXT("Right to left") }, { TEXT("270"), TEXT("Left to right") }, { TEXT("rand"), TEXT("Random") } };
			for (const auto& D : Dirs)
			{
				const FString K = D.Key;
				Dir->AddSlot().Padding(0, 0, 6, 6)[Chip(D.Value, [this, K]() { Setup.WindDir = K; Remember(); Rebuild(); }, Setup.WindDir == K)];
			}
			WeatherRows->AddSlot().AutoHeight().Padding(0, 6, 0, 10)[Option(TEXT("Wind (mph)"), Number(0, 40, 1, [this]() { return Setup.WindMph; }, [this](double X) { Setup.WindMph = FMath::RoundToDouble(X); Remember(); }))];
			WeatherRows->AddSlot().AutoHeight()[Option(TEXT("Wind from"), Dir)];
		}
	}

	// who's playing: click to add (the order picked is the order they tee off)
	const FOTGFormat& F = FOTGGame::FormatOf(Setup.Format);
	const bool bTeamsOn = F.bTeams || (F.Key == TEXT("match") && Setup.Picked.Num() > 2);
	TSharedRef<SVerticalBox> PlayerRows = SNew(SVerticalBox);
	{
		TSharedRef<SWrapBox> Who = SNew(SWrapBox).UseAllottedSize(true);
		for (const FOTGPlayerProfile& P : Profiles)
		{
			const FString Id = P.Id;
			const int32 At = Setup.Picked.IndexOfByKey(Id);
			const FString Team = Setup.Teams.Contains(Id) ? Setup.Teams[Id] : TEXT("A");
			TSharedRef<SHorizontalBox> One = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[Chip(At >= 0 ? FString::Printf(TEXT("%d. %s"), At + 1, *P.Name) : P.Name, [this, Id]() {
					const int32 I = Setup.Picked.IndexOfByKey(Id);
					if (I >= 0) Setup.Picked.RemoveAt(I);
					else
					{
						Setup.Picked.Add(Id);
						// (new team players alternate A, B, A, B)
						if (!Setup.Teams.Contains(Id)) Setup.Teams.Add(Id, Setup.Picked.Num() % 2 ? TEXT("A") : TEXT("B"));
					}
					Remember(); Rebuild();
				}, At >= 0, P.Colour)];
			if (At >= 0 && bTeamsOn)
				One->AddSlot().AutoWidth().Padding(4, 0, 0, 0)[Chip(TEXT("Team ") + Team, [this, Id, Team]() { Setup.Teams.Add(Id, Team == TEXT("A") ? TEXT("B") : TEXT("A")); Remember(); Rebuild(); }, false, FLinearColor::Transparent, 12)];
			Who->AddSlot().Padding(0, 0, 8, 6)[One];
		}
		if (Profiles.Num()) PlayerRows->AddSlot().AutoHeight()[Who];
		PlayerRows->AddSlot().AutoHeight().Padding(0, 2, 0, 0)
		[
			Para(!Profiles.Num() ? TEXT("No players yet: add yourself and your friends on the Players page, or play a round on your own.")
				: Setup.Picked.Num() ? TEXT("Tee-off order is the order you pick them in.") : TEXT("Pick who's playing, or leave everyone out for a round on your own."), 12, Dim)
		];
	}

	// the game
	TSharedRef<SVerticalBox> GameRows = SNew(SVerticalBox);
	{
		TArray<FString> Keys;
		for (const FOTGFormat& X : FOTGGame::Formats()) Keys.Add(X.Key);
		GameRows->AddSlot().AutoHeight()[Chips(Keys, [this]() { return Setup.Format; }, [this](const FString& V) { Setup.Format = V; }, [](const FString& K) { return FOTGGame::FormatOf(K).Name; })];
		GameRows->AddSlot().AutoHeight().Padding(0, 2, 0, 14)[Para(F.About, 12, Dim)];
		GameRows->AddSlot().AutoHeight()
		[
			Option(TEXT("Handicaps"), SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Chip(TEXT("Off"), [this]() { Setup.bHandicaps = false; Remember(); Rebuild(); }, !Setup.bHandicaps)]
				+ SHorizontalBox::Slot().AutoWidth()[Chip(TEXT("Count them"), [this]() { Setup.bHandicaps = true; Remember(); Rebuild(); }, Setup.bHandicaps)])
		];
	}

	OptionsScroll = SNew(SScrollBox);
	OptionsScroll->AddSlot().Padding(0, 0, 6, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.5f).Padding(0, 0, 8, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)[Section(TEXT("Round"), RoundRows)]
			+ SVerticalBox::Slot().AutoHeight()[Section(TEXT("Conditions"), WeatherRows)]
		]
		+ SHorizontalBox::Slot().FillWidth(0.5f).Padding(8, 0, 0, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)[Section(TEXT("Players"), PlayerRows, Ghost(TEXT("Manage players"), [this]() { Page = EPage::Players; Draft = Profiles; ConfirmRemove = -1; Rebuild(); }, 12))]
			+ SVerticalBox::Slot().AutoHeight()[Section(TEXT("Game"), GameRows)]
		]
	];

	// ---- the course picked: its photo, name, place and numbers
	auto Stat = [](const FString& Value, const FString& Label) {
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Text(Value, 22, Ink, "Bold")]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)[Caps(Label, 9)];
	};
	TSharedRef<SHorizontalBox> Stats = SNew(SHorizontalBox);
	if (Info) Stats->AddSlot().AutoWidth().Padding(0, 0, 34, 0)[Stat(FString::FromInt(Course ? Course->Holes.Num() : Info->Holes), TEXT("Holes"))];
	if (Course && Par) Stats->AddSlot().AutoWidth().Padding(0, 0, 34, 0)[Stat(FString::FromInt(Par), TEXT("Par"))];
	if (Course && Yards > 0) Stats->AddSlot().AutoWidth().Padding(0, 0, 34, 0)[Stat(FText::AsNumber(FMath::RoundToInt(Yards)).ToString(), TEXT("Yards · ") + Setup.Tee)];
	if (Info)
	{
		const FDateTime Played = LastPlayedOf(*Info);
		Stats->AddSlot().AutoWidth()[Stat(Played.GetTicks() > 0 ? Ago(Played) : FString(TEXT("Not yet")), TEXT("Last played"))];
	}
	TSharedRef<SWidget> Header = Card(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(258).HeightOverride(172)[SNew(SImage).Image(PhotoOf(Setup.CourseId, EPhoto::Header))]]
		+ SHorizontalBox::Slot().FillWidth(1).Padding(26, 4, 8, 4)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Caps(TEXT("Selected course"), 10, Gold)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[SNew(STextBlock).Text(FText::FromString(Info ? Info->Name : Setup.CourseId)).Font(Font(30, "Bold")).ColorAndOpacity(Ink).AutoWrapText(true)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[Para(Info ? Info->Location : FString(), 15)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0).HAlign(HAlign_Left)[EditLink(Info)]
			+ SVerticalBox::Slot().FillHeight(1)[SNullWidget::NullWidget]
			+ SVerticalBox::Slot().AutoHeight()[Stats]
		], FMargin(12));

	// ---- what's set, and Start
	const FString Problem = FOTGGame::Problem(Setup.Format, Setup.Picked, Setup.Teams);
	const bool bCanStart = Course && Problem.IsEmpty();
	const int32 N = Course ? Course->Holes.Num() : 0;
	const bool bNine = Setup.Holes == 9 && N > 9;
	FString Line1 = Course ? FString::Printf(TEXT("%s tees  ·  %s pins  ·  %d holes from hole %d"), *Setup.Tee, *Setup.Pin, bNine ? 9 : N, Setup.StartHole) : FString(TEXT("Pick a course"));
	FString Who = Setup.Picked.Num() == 0 ? FString(TEXT("On your own")) : Setup.Picked.Num() == 1 ? FString(TEXT("1 player")) : FString::Printf(TEXT("%d players"), Setup.Picked.Num());
	FString Line2 = FString::Printf(TEXT("%s  ·  %s  ·  %s%s"), *F.Name, *Who, *WeatherName, Setup.bHandicaps ? TEXT("  ·  handicaps") : TEXT(""));
	TSharedRef<SWidget> Action = Card(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(Line1)).Font(Font(15, "Bold")).ColorAndOpacity(Ink).AutoWrapText(true)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
			[
				Problem.IsEmpty() ? StaticCastSharedRef<SWidget>(Para(Line2, 13)) : StaticCastSharedRef<SWidget>(Para(Problem, 13, Warn))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(20, 0, 0, 0)
		[
			SNew(SBox).IsEnabled(bCanStart)
			[
				SNew(SButton).ButtonStyle(&ButtonStyle(true)).ContentPadding(FMargin(40, 14)).OnClicked_Lambda([this]() { Start(); return FReply::Handled(); })
				[Text(TEXT("Start the round"), 20, Ink, "Bold")]
			]
		], FMargin(22, 14));

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.34f).Padding(0, 0, 20, 0)[Browser]
		+ SHorizontalBox::Slot().FillWidth(0.66f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Header]
			+ SVerticalBox::Slot().FillHeight(1).Padding(0, 16)[OptionsScroll.ToSharedRef()]
			+ SVerticalBox::Slot().AutoHeight()[Action]
		];
}
