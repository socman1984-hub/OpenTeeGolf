// The main menu's Online page (see SOTGMainMenu.h; the game itself: UOTGNetSubsystem): who's playing
// at this computer, hosting a game (the Play page's course and round, taking turns or everyone at
// once) or joining one by the host's address, and then the game's lobby - everyone in it, whether
// they have the course yet, and the host's Start. And the window that asks a joining player what to
// do when their version of the course isn't the host's.

#include "UI/Menus/SOTGMainMenu.h"
#include "App/OTGNetSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UI/OTGUi.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

using namespace otg::ui;

#include "UI/Menus/SOTGMenuStyle.h"

// ------------------------------------------------------------------ what's kept between games

UOTGNetSubsystem* SOTGMainMenu::Net() const
{
	UGameInstance* GI = GameInstance();
	return GI ? GI->GetSubsystem<UOTGNetSubsystem>() : nullptr;
}

void SOTGMainMenu::LoadOnline()
{
	if (GConfig)
	{
		GConfig->GetString(TEXT("OpenTeeGolf"), TEXT("OnlineAddress"), JoinAddress, GGameUserSettingsIni);
		GConfig->GetString(TEXT("OpenTeeGolf"), TEXT("OnlineProfile"), OnlineProfile, GGameUserSettingsIni);
		GConfig->GetString(TEXT("OpenTeeGolf"), TEXT("OnlineName"), OnlineName, GGameUserSettingsIni);
		GConfig->GetBool(TEXT("OpenTeeGolf"), TEXT("OnlineTogether"), bOnlineTogether, GGameUserSettingsIni);
		GConfig->GetInt(TEXT("OpenTeeGolf"), TEXT("OnlinePort"), OnlinePort, GGameUserSettingsIni);
	}
	if (OnlinePort < 1024 || OnlinePort > 65535) OnlinePort = UOTGNetSubsystem::DefaultPort;
	// (no one picked yet: the first on the Play page, else the first saved)
	if (!Profiles.ContainsByPredicate([&](const FOTGPlayerProfile& P) { return P.Id == OnlineProfile; }))
		OnlineProfile = Setup.Picked.Num() ? Setup.Picked[0] : Profiles.Num() ? Profiles[0].Id : FString();
}

void SOTGMainMenu::SaveOnline() const
{
	if (!GConfig) return;
	GConfig->SetString(TEXT("OpenTeeGolf"), TEXT("OnlineAddress"), *JoinAddress, GGameUserSettingsIni);
	GConfig->SetString(TEXT("OpenTeeGolf"), TEXT("OnlineProfile"), *OnlineProfile, GGameUserSettingsIni);
	GConfig->SetString(TEXT("OpenTeeGolf"), TEXT("OnlineName"), *OnlineName, GGameUserSettingsIni);
	GConfig->SetBool(TEXT("OpenTeeGolf"), TEXT("OnlineTogether"), bOnlineTogether, GGameUserSettingsIni);
	GConfig->SetInt(TEXT("OpenTeeGolf"), TEXT("OnlinePort"), OnlinePort, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

// (testing two games on one computer, no one at either: -netname=<name> plays as that name;
// -nethost hosts the Play page's course (-netcourse=<id> another; -nettogether everyone at once),
// -netstart=<players> starts the round once that many have the course; -netjoin=<address> joins,
// -netchoose=copy|overwrite|leave answers the different-version window; the round: -netbot)
void SOTGMainMenu::OnlineTest()
{
	const TCHAR* Cmd = FCommandLine::Get();
	FString Name, Join, Choose, CourseArg;
	int32 StartAt = 0;
	FParse::Value(Cmd, TEXT("netname="), Name);
	FParse::Value(Cmd, TEXT("netjoin="), Join);
	FParse::Value(Cmd, TEXT("netchoose="), Choose);
	FParse::Value(Cmd, TEXT("netcourse="), CourseArg);
	FParse::Value(Cmd, TEXT("netstart="), StartAt);
	const bool bHost = FParse::Param(Cmd, TEXT("nethost"));
	if (!bHost && Join.IsEmpty()) return;
	if (!Name.IsEmpty()) { OnlineName = Name; OnlineProfile.Reset(); }
	if (!CourseArg.IsEmpty()) Setup.CourseId = CourseArg;
	if (FParse::Param(Cmd, TEXT("nettogether"))) bOnlineTogether = true;
	UOTGNetSubsystem* N = Net();
	// (once: not again when the menu comes back after the round)
	static bool bDone = false;
	if (!N || bDone) return;
	bDone = true;
	if (bHost)
	{
		FString Why;
		if (!N->Host(Setup, bOnlineTogether, OnlineMe(), OnlinePort, Why)) UE_LOG(LogTemp, Warning, TEXT("(nethost) %s"), *Why);
	}
	else N->Join(Join, OnlineMe());
	Page = EPage::Online;
	// (-netshot=<name>: the page's screenshot once 2 players are in, 3 s on)
	FString ShotName;
	FParse::Value(Cmd, TEXT("netshot="), ShotName);
	TSharedRef<double> ShotAt = MakeShared<double>(0);
	RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateLambda([this, Choose, StartAt, ShotName, ShotAt](double, float) {
		UOTGNetSubsystem* X = Net();
		if (!X) return EActiveTimerReturnType::Stop;
		if (!ShotName.IsEmpty() && *ShotAt >= 0 && X->Players().Num() >= 2)
		{
			if (*ShotAt == 0) *ShotAt = FPlatformTime::Seconds() + 3;
			else if (FPlatformTime::Seconds() >= *ShotAt && GEngine) { GEngine->Exec(nullptr, *(TEXT("OTG.Shot ") + ShotName)); *ShotAt = -1; }
		}
		if (X->NeedsCourseChoice() && !Choose.IsEmpty())
			X->ChooseCourse(Choose == TEXT("overwrite") ? EOTGCourseChoice::Overwrite : Choose == TEXT("leave") ? EOTGCourseChoice::Leave : EOTGCourseChoice::Copy);
		FString Why;
		if (StartAt > 0 && X->IsHost() && X->Players().Num() >= StartAt && X->CanStart(Why)) { X->StartRound(); return EActiveTimerReturnType::Stop; }
		return X->InSession() ? EActiveTimerReturnType::Continue : EActiveTimerReturnType::Stop;
	}));
}

FOTGPlayerProfile SOTGMainMenu::OnlineMe() const
{
	for (const FOTGPlayerProfile& P : Profiles) if (P.Id == OnlineProfile) return P;
	// (not one of the saved players: a name only, and the round isn't kept with anyone)
	FOTGPlayerProfile P = FOTGPlayers::NewProfile(OnlineName.TrimStartAndEnd().IsEmpty() ? FString(TEXT("Player")) : OnlineName.TrimStartAndEnd(), 0);
	P.Id.Reset();
	return P;
}

// ------------------------------------------------------------------ the page

TSharedRef<SWidget> SOTGMainMenu::OnlinePage()
{
	UOTGNetSubsystem* N = Net();
	if (!N) return Para(TEXT("Online games aren't available here."), 14, Warn);
	auto Section = [](const FString& Title, const TSharedRef<SWidget>& Rows) {
		return Card(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[Text(Title, 16, Ink, "Bold")]
			+ SVerticalBox::Slot().AutoHeight()[Rows], FMargin(20, 16));
	};
	auto Hint = [](const FString& S) { return Para(S, 12, Dim); };
	const FString Error = N->LastError();
	auto RoundLine = [](const FOTGRoundSetup& S) {
		const FOTGFormat& F = FOTGGame::FormatOf(S.Format);
		// (team games aren't played online: stroke play instead)
		const FString Game = F.bTeams || !F.Shared.IsEmpty() ? FString(TEXT("Stroke play")) : F.Name;
		return FString::Printf(TEXT("%s tees  ·  %s pins  ·  %s from hole %d  ·  %s%s"), *S.Tee, *S.Pin, S.Holes == 9 ? TEXT("9 holes") : TEXT("all the holes"), S.StartHole, *Game,
			S.bAutoPutt ? TEXT("  ·  auto-putt") : TEXT(""));
	};
	TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
	if (!Error.IsEmpty())
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 14)[Card(Para(Error, 14, Warn), FMargin(20, 12))];

	// ---------------------------------------------------------------- in a game: its lobby
	if (N->InSession())
	{
		const bool bHost = N->IsHost();
		TSharedRef<SVerticalBox> Head = SNew(SVerticalBox);
		Head->AddSlot().AutoHeight()[Caps(bHost ? TEXT("You're hosting") : N->IsConnecting() ? TEXT("Joining") : *FString::Printf(TEXT("In %s's game"), *N->HostName()), 10, Gold)];
		if (N->IsConnecting())
		{
			Head->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SNew(STextBlock).Text(FText::FromString(N->Status())).Font(Font(22, "Bold")).ColorAndOpacity(Ink).AutoWrapText(true)];
			Head->AddSlot().AutoHeight().Padding(0, 14, 0, 0).HAlign(HAlign_Left)[Button(TEXT("Cancel"), [this]() { if (UOTGNetSubsystem* X = Net()) X->Leave(); Rebuild(); }, 14)];
			Col->AddSlot().AutoHeight()[Card(Head, FMargin(24, 18))];
			return SNew(SScrollBox) + SScrollBox::Slot()[Col];
		}
		Head->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SNew(STextBlock).Text(FText::FromString(N->CourseName())).Font(Font(28, "Bold")).ColorAndOpacity(Ink).AutoWrapText(true)];
		Head->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Para(RoundLine(N->GameSetup()), 14)];
		Head->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
		[
			Para(N->Together() ? TEXT("Everyone at once: hit whenever you're ready and watch the others' balls fly. Nobody goes on to the next hole until everyone has finished it.")
				: TEXT("Taking turns: whoever's away hits, and everyone watches their shot."), 13, Dim)
		];
		if (bHost)
		{
			const TArray<FString> Addr = N->Addresses();
			Head->AddSlot().AutoHeight().Padding(0, 14, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[Caps(TEXT("Your address"), 10)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[Text(Addr.Num() ? FString::Join(Addr, TEXT("   or   ")) : FString(TEXT("(no network found)")), 18, Accent, "Bold")]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)[Text(FString::Printf(TEXT("port %d"), N->Port()), 14, Soft)]
			];
			Head->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
			[
				Hint(FString::Printf(TEXT("Players on your network type this address on their Online page. Over the internet: forward TCP port %d on your router to this computer, and give them your public address (search \"what is my IP\")."), N->Port()))
			];
		}
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 16)[Card(Head, FMargin(24, 18))];

		// everyone, and whether they have the course
		TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
		for (const FOTGNetPlayer& P : N->Players())
		{
			FString State;
			FLinearColor C = Soft;
			if (!P.bConnected) { State = TEXT("Left"); C = Dim; }
			else if (P.Course == TEXT("ready")) { State = TEXT("Has the course"); C = Accent; }
			else if (P.Course == TEXT("downloading")) State = FString::Printf(TEXT("Getting the course  %d%%"), FMath::RoundToInt(P.Progress * 100));
			else if (P.Course == TEXT("choosing")) State = TEXT("Has a different version: choosing what to do");
			else if (P.Course == TEXT("failed")) { State = TEXT("Couldn't get the course"); C = Warn; }
			else State = TEXT("Checking their course");
			FString Name = P.Name;
			if (P.bHost) Name += TEXT("  (host)");
			if (P.Id == N->LocalId()) Name += TEXT("  (you)");
			Rows->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(SBorder).BorderImage(InsetBrush()).Padding(FMargin(14, 10))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
					[SNew(SBox).WidthOverride(14).HeightOverride(14)[SNew(SBorder).BorderImage(PillBrush()).BorderBackgroundColor(P.Colour).Padding(0)]]
					+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(Name, 16, P.bConnected ? Ink : Dim, "Bold")]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Text(State, 14, C)]
				]
			];
		}
		// (this computer's download: how far)
		Rows->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(SVerticalBox).Visibility_Lambda([this]() { UOTGNetSubsystem* X = Net(); return X && X->DownloadTotal() > 0 && !X->CourseReady() ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Soft).Text_Lambda([this]() {
					UOTGNetSubsystem* X = Net();
					if (!X) return FText::GetEmpty();
					return FText::FromString(FString::Printf(TEXT("Downloading the course from the host: %.1f of %.1f MB (the files you have already, from your other courses, aren't downloaded)"),
						X->DownloadDone() / 1048576.0, X->DownloadTotal() / 1048576.0));
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
			[
				SNew(SBox).HeightOverride(8)
				[
					SNew(SProgressBar).FillColorAndOpacity(FLinearColor(0.3f, 0.75f, 0.4f, 1.f))
					.Percent_Lambda([this]() { UOTGNetSubsystem* X = Net(); return TOptional<float>(X && X->DownloadTotal() > 0 ? float(double(X->DownloadDone()) / X->DownloadTotal()) : 0.f); })
				]
			]
		];
		Col->AddSlot().AutoHeight().Padding(0, 0, 0, 16)[Section(FString::Printf(TEXT("Players (%d)"), N->Players().Num()), Rows)];

		// the host's Start; leaving
		FString Why;
		const bool bCanStart = bHost && N->CanStart(Why);
		TSharedRef<SHorizontalBox> Action = SNew(SHorizontalBox);
		Action->AddSlot().FillWidth(1).VAlign(VAlign_Center)
		[
			Para(bHost ? (bCanStart ? FString(TEXT("Everyone has the course.")) : Why) : FString::Printf(TEXT("%s starts the round when everyone has the course. While you're in the game, the course builder and editors are off."), *N->HostName()), 14)
		];
		Action->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(16, 0, 0, 0)
		[Button(bHost ? TEXT("Close the game") : TEXT("Leave the game"), [this]() { if (UOTGNetSubsystem* X = Net()) X->Leave(); Rebuild(); }, 14)];
		if (bHost)
			Action->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)
			[
				SNew(SBox).IsEnabled(bCanStart)
				[
					SNew(SButton).ButtonStyle(&ButtonStyle(true)).ContentPadding(FMargin(40, 14))
					.OnClicked_Lambda([this]() { if (UOTGNetSubsystem* X = Net()) X->StartRound(); return FReply::Handled(); })
					[Text(TEXT("Start the round"), 20, Ink, "Bold")]
				]
			];
		Col->AddSlot().AutoHeight()[Card(Action, FMargin(22, 14))];
		return SNew(SScrollBox) + SScrollBox::Slot()[Col];
	}

	// ---------------------------------------------------------------- not in a game: host or join
	// who's playing at this computer
	TSharedRef<SVerticalBox> WhoRows = SNew(SVerticalBox);
	if (Profiles.Num())
	{
		TSharedRef<SWrapBox> Who = SNew(SWrapBox).UseAllottedSize(true);
		for (const FOTGPlayerProfile& P : Profiles)
		{
			const FString Id = P.Id;
			Who->AddSlot().Padding(0, 0, 8, 6)[Chip(P.Name, [this, Id]() { OnlineProfile = Id; SaveOnline(); Rebuild(); }, P.Id == OnlineProfile, P.Colour)];
		}
		WhoRows->AddSlot().AutoHeight()[Who];
		WhoRows->AddSlot().AutoHeight().Padding(0, 2, 0, 0)[Hint(TEXT("One player at each computer: the others see your name, colour, tees and handicap. Your round is kept with your player, as on your own."))];
	}
	else
	{
		WhoRows->AddSlot().AutoHeight()
		[
			SNew(SBox).WidthOverride(320)
			[
				SNew(SEditableTextBox).Style(&SearchStyle()).Font(Font(14)).ForegroundColor(Ink)
				.HintText(FText::FromString(TEXT("Your name")))
				.Text(FText::FromString(OnlineName))
				.OnTextChanged_Lambda([this](const FText& T) { OnlineName = T.ToString(); })
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { OnlineName = T.ToString(); SaveOnline(); })
			]
		];
		WhoRows->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Hint(TEXT("Or add yourself on the Players page, so your rounds are kept."))];
	}

	// hosting: the Play page's course and round
	const FOTGCourseInfo* Info = CourseList.FindByPredicate([&](const FOTGCourseInfo& I) { return I.Id == Setup.CourseId; });
	const bool bCanHost = Info && !Info->ExportDir.IsEmpty();
	TSharedRef<SVerticalBox> HostRows = SNew(SVerticalBox);
	HostRows->AddSlot().AutoHeight()[Caps(TEXT("Course"), 10)];
	HostRows->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
		[SNew(STextBlock).Text(FText::FromString(bCanHost ? Info->Name : FString(TEXT("Pick a course on the Play page")))).Font(Font(20, "Bold")).ColorAndOpacity(bCanHost ? Ink : Warn).AutoWrapText(true)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Ghost(TEXT("Change"), [this]() { Page = EPage::Play; Rebuild(); }, 12)]
	];
	if (bCanHost) HostRows->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Para(RoundLine(Setup), 13)];
	HostRows->AddSlot().AutoHeight().Padding(0, 2, 0, 0)[Hint(TEXT("The tees, pins, holes, weather and game are the Play page's. Anyone who hasn't got the course gets it from you, only the files they don't have."))];
	HostRows->AddSlot().AutoHeight().Padding(0, 16, 0, 0)[Caps(TEXT("How you play"), 10)];
	HostRows->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Chip(TEXT("Take turns"), [this]() { bOnlineTogether = false; SaveOnline(); Rebuild(); }, !bOnlineTogether)]
		+ SHorizontalBox::Slot().AutoWidth()[Chip(TEXT("Everyone at once"), [this]() { bOnlineTogether = true; SaveOnline(); Rebuild(); }, bOnlineTogether)]
	];
	HostRows->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
	[
		Hint(bOnlineTogether ? TEXT("Everyone hits whenever they're ready and sees the others' balls fly. Each of you finishes the hole; nobody goes on to the next until everyone has.")
			: TEXT("One ball at a time: whoever's away hits, and everyone watches their shot."))
	];
	HostRows->AddSlot().AutoHeight().Padding(0, 16, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[Caps(TEXT("Port"), 10)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(110)
			[
				SNew(SSpinBox<int32>).Style(&SpinStyle()).MinValue(1024).MaxValue(65535).Delta(1)
				.Value_Lambda([this]() { return OnlinePort; })
				.OnValueChanged_Lambda([this](int32 V) { OnlinePort = V; })
				.OnValueCommitted_Lambda([this](int32 V, ETextCommit::Type) { OnlinePort = V; SaveOnline(); })
			]
		]
	];
	if (!HostProblem.IsEmpty()) HostRows->AddSlot().AutoHeight().Padding(0, 12, 0, 0)[Para(HostProblem, 13, Warn)];
	HostRows->AddSlot().AutoHeight().Padding(0, 16, 0, 0).HAlign(HAlign_Left)
	[
		SNew(SBox).IsEnabled(bCanHost)
		[
			SNew(SButton).ButtonStyle(&ButtonStyle(true)).ContentPadding(FMargin(30, 12))
			.OnClicked_Lambda([this]() {
				if (UOTGNetSubsystem* X = Net())
				{
					FString Why;
					SaveOnline();
					HostProblem = X->Host(Setup, bOnlineTogether, OnlineMe(), OnlinePort, Why) ? FString() : Why;
				}
				Rebuild();
				return FReply::Handled();
			})
			[Text(TEXT("Host the game"), 18, Ink, "Bold")]
		]
	];

	// joining: the host's address
	auto DoJoin = [this]() {
		JoinAddress = JoinAddress.TrimStartAndEnd();
		SaveOnline();
		if (UOTGNetSubsystem* X = Net()) X->Join(JoinAddress, OnlineMe());
		Rebuild();
	};
	TSharedRef<SVerticalBox> JoinRows = SNew(SVerticalBox);
	JoinRows->AddSlot().AutoHeight()[Caps(TEXT("The host's address"), 10)];
	JoinRows->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
	[
		SNew(SEditableTextBox).Style(&SearchStyle()).Font(Font(16)).ForegroundColor(Ink)
		.HintText(FText::FromString(TEXT("192.168.1.20")))
		.Text(FText::FromString(JoinAddress))
		.OnTextChanged_Lambda([this](const FText& T) { JoinAddress = T.ToString(); })
		.OnTextCommitted_Lambda([this, DoJoin](const FText& T, ETextCommit::Type How) { JoinAddress = T.ToString(); if (How == ETextCommit::OnEnter) DoJoin(); })
	];
	JoinRows->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
	[
		Hint(FString::Printf(TEXT("The address on the host's Online page. Their port too if it isn't %d: 192.168.1.20:7790. You both need the same version of OpenTeeGolf."), UOTGNetSubsystem::DefaultPort))
	];
	JoinRows->AddSlot().AutoHeight().Padding(0, 16, 0, 0).HAlign(HAlign_Left)
	[
		SNew(SButton).ButtonStyle(&ButtonStyle(true)).ContentPadding(FMargin(30, 12)).OnClicked_Lambda([DoJoin]() { DoJoin(); return FReply::Handled(); })
		[Text(TEXT("Join"), 18, Ink, "Bold")]
	];
	JoinRows->AddSlot().AutoHeight().Padding(0, 16, 0, 0)
	[
		Hint(TEXT("If you have the course already the game checks it's the same as the host's. If yours is different, it asks before changing anything."))
	];

	Col->AddSlot().AutoHeight().Padding(0, 0, 0, 16)[Section(TEXT("Playing at this computer"), WhoRows)];
	Col->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.55f).Padding(0, 0, 8, 0)[Section(TEXT("Host a game"), HostRows)]
		+ SHorizontalBox::Slot().FillWidth(0.45f).Padding(8, 0, 0, 0)[Section(TEXT("Join a game"), JoinRows)]
	];
	return SNew(SScrollBox) + SScrollBox::Slot()[Col];
}

// ------------------------------------------------------------------ a different version of the course

TSharedRef<SWidget> SOTGMainMenu::CourseChoiceWindow()
{
	auto Choose = [this](EOTGCourseChoice C) { if (UOTGNetSubsystem* X = Net()) X->ChooseCourse(C); Rebuild(); };
	return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.005f, 0.012f, 0.008f, 0.82f))
		.Visibility_Lambda([this]() { UOTGNetSubsystem* X = Net(); return X && X->NeedsCourseChoice() ? EVisibility::Visible : EVisibility::Collapsed; })
		.HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(760)
			[
				Card(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Caps(TEXT("Your course is different"), 11, Gold)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
					[
						SNew(STextBlock).Font(Font(22, "Bold")).ColorAndOpacity(Ink).AutoWrapText(true).Text_Lambda([this]() {
							UOTGNetSubsystem* X = Net();
							return FText::FromString(X ? FString::Printf(TEXT("Your %s isn't the same as %s's"), *X->OwnCourseName(), *X->HostName()) : FString());
						})
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
					[
						SNew(STextBlock).Font(Font(14)).ColorAndOpacity(Soft).AutoWrapText(true).Text_Lambda([this]() {
							UOTGNetSubsystem* X = Net();
							return FText::FromString(X ? FString::Printf(TEXT("One of you has changed it. To play together you need the host's version. Keep yours and the host's is saved beside it as \"%s\" (in your course list after). Or overwrite yours with the host's: your changes to it are lost."), *X->CopyName()) : FString());
						})
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 22, 0, 0).HAlign(HAlign_Right)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Button(TEXT("Leave the game"), [Choose]() { Choose(EOTGCourseChoice::Leave); }, 14)]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Button(TEXT("Overwrite mine"), [Choose]() { Choose(EOTGCourseChoice::Overwrite); }, 14)]
						+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Keep mine, save theirs as a copy"), [Choose]() { Choose(EOTGCourseChoice::Copy); }, 14, true)]
					]
					, FMargin(28, 24))
			]
		];
}

TSharedRef<SWidget> SOTGMainMenu::EditorsOff()
{
	return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0, 40, 0, 0))
		[
			SNew(SBox).WidthOverride(620)
			[
				Card(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Caps(TEXT("Online game"), 11, Gold)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[SNew(STextBlock).Text(FText::FromString(TEXT("The course builder and editors are off"))).Font(Font(22, "Bold")).ColorAndOpacity(Ink).AutoWrapText(true)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
					[Para(TEXT("Everyone in an online game plays the host's course, file for file: changing one now would make yours different. Leave the game to build or edit courses."), 14)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 20, 0, 0).HAlign(HAlign_Right)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Button(TEXT("Leave the game"), [this]() { if (UOTGNetSubsystem* X = Net()) X->Leave(); Rebuild(); }, 14)]
						+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Back to the game"), [this]() { Page = EPage::Online; Rebuild(); }, 14, true)]
					]
					, FMargin(28, 24))
			]
		];
}
