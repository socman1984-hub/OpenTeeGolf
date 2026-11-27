// See SOTGMainMenu.h: the menu's frame, its tabs and the update bar. (The web version's menu:
// public/index.html and players-menu.js.)

#include "UI/Menus/SOTGMainMenu.h"
#include "UI/Menus/SOTGLaunchMonitorPanel.h"
#include "Round/OTGMiniGolf.h"
#include "UI/Builder/SOTGCourseBuilder.h"
#include "UI/Editor/SOTGCourseEditor.h"
#include "UI/Builder/SOTGStudio.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "GeneralProjectSettings.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "App/OTGUpdateSubsystem.h"
#include "App/OTGHelperSubsystem.h"
#include "App/OTGNetSubsystem.h"
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

// ------------------------------------------------------------------ the main menu

UOTGCourseSubsystem* SOTGMainMenu::Courses() const
{
	UGameInstance* GI = GameInstance();
	return GI ? GI->GetSubsystem<UOTGCourseSubsystem>() : nullptr;
}

const FSlateBrush* SOTGMainMenu::ThumbOf(const FString& Id)
{
	const FString Key = Id + TEXT("#thumb");
	if (const TSharedPtr<FSlateBrush>* B = Photos.Find(Key)) return B->Get();
	ThumbsWanted.AddUnique(Id);
	static FSlateBrush Waiting = []() { FSlateBrush B; B.TintColor = FLinearColor(0.06f, 0.1f, 0.07f, 1.f); return B; }();
	return &Waiting;
}

void SOTGMainMenu::Tick(const FGeometry& Geometry, const double Time, const float Dt)
{
	SCompoundWidget::Tick(Geometry, Time, Dt);
	// (the rows asked for their photos as they were drawn: one a frame, the latest first)
	if (ThumbsWanted.Num()) PhotoOf(ThumbsWanted.Pop(), EPhoto::Thumb);
	// (the online game changed: its page again; and a new course here, the course list again)
	if (UOTGNetSubsystem* N = Net(); N && N->Revision() != SeenNetRev)
	{
		const bool bHadCourse = SeenNetRev >= 0;
		SeenNetRev = N->Revision();
		if (bHadCourse && N->CourseReady()) if (UOTGCourseSubsystem* C = Courses()) CourseList = C->ListCourses();
		if (Page == EPage::Online) Rebuild();
	}
}

const FSlateBrush* SOTGMainMenu::PhotoOf(const FString& Id, EPhoto Kind)
{
	const FString Key = Id + (Kind == EPhoto::Thumb ? TEXT("#thumb") : Kind == EPhoto::Header ? TEXT("#header") : TEXT(""));
	if (const TSharedPtr<FSlateBrush>* B = Photos.Find(Key)) return B->Get();
	UTexture2D* Tex = nullptr;
	// (an exported course: its photo from the folder, smaller; a pack: the pack's)
	if (const FOTGCourseInfo* Info = CourseList.FindByPredicate([&](const FOTGCourseInfo& I) { return I.Id == Id; }); Info && !Info->ExportDir.IsEmpty())
	{
		if (const TStrongObjectPtr<UTexture2D>* Have = FolderPhotos.Find(Id)) Tex = Have->Get();
		else if ((Tex = otg::LoadTexture(FPaths::Combine(Info->ExportDir, TEXT("photo.jpg")), otg::ETexKind::Photo, 1024, GetTransientPackage())))
			FolderPhotos.Add(Id, TStrongObjectPtr<UTexture2D>(Tex));
	}
	if (!Tex) Tex = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/Courses/%s/Build/Textures/T_CoursePhoto.T_CoursePhoto"), *Id));
	TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
	if (Tex)
	{
		Brush->SetResourceObject(Tex);
		// (a thumbnail: the middle of the course; the header: more of it, 3:2; the backdrop: most of it)
		Brush->ImageSize = Kind == EPhoto::Thumb ? FVector2D(108, 72) : Kind == EPhoto::Header ? FVector2D(300, 200) : FVector2D(1920, 1920);
		Brush->SetUVRegion(Kind == EPhoto::Thumb ? FBox2f(FVector2f(0.33f, 0.36f), FVector2f(0.67f, 0.59f))
			: Kind == EPhoto::Header ? FBox2f(FVector2f(0.22f, 0.32f), FVector2f(0.78f, 0.69f))
			: FBox2f(FVector2f(0.15f, 0.15f), FVector2f(0.85f, 0.85f)));
		if (!Tex->IsA<UTexture2D>() || Tex->GetOutermost() != GetTransientPackage()) Tex->SetForceMipLevelsToBeResident(600.f);
	}
	else Brush->TintColor = FLinearColor(0.05f, 0.1f, 0.06f, 1.f);
	Photos.Add(Key, Brush);
	return Brush.Get();
}

void SOTGMainMenu::Construct(const FArguments&)
{
	// (the first start: render at 75% - see Settings > Render resolution)
	if (UGameUserSettings* G = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		bool bSet = false;
		GConfig->GetBool(TEXT("OpenTeeGolf"), TEXT("RenderScaleChosen"), bSet, GGameUserSettingsIni);
		if (!bSet)
		{
			G->SetResolutionScaleValueEx(75.f);
			G->ApplySettings(false);
			G->SaveSettings();
			GConfig->SetBool(TEXT("OpenTeeGolf"), TEXT("RenderScaleChosen"), true, GGameUserSettingsIni);
			GConfig->Flush(false, GGameUserSettingsIni);
		}
	}
	Setup = FOTGPlayers::LoadSetup();
	Profiles = FOTGPlayers::Load();
	LoadOnline();
	// (back from an online round, or in a game still: its lobby)
	if (UOTGNetSubsystem* N = Net(); N && N->InSession()) Page = EPage::Online;
	Setup.Picked.RemoveAll([&](const FString& Id) { return !Profiles.ContainsByPredicate([&](const FOTGPlayerProfile& P) { return P.Id == Id; }); });
	if (UOTGCourseSubsystem* C = Courses()) CourseList = C->ListCourses();
	if (CourseList.Num() && !CourseList.ContainsByPredicate([&](const FOTGCourseInfo& I) { return I.Id == Setup.CourseId; })) Setup.CourseId = CourseList[0].Id;
	// (testing, even in a Shipping build, which has no console: -buildertest opens the Course builder)
	FString BuilderTest;
	if (FParse::Value(FCommandLine::Get(), TEXT("buildertest="), BuilderTest)) Page = EPage::Build;
	// (-studio=biomes|forests[:<name>]: the studio, open on it)
	FString StudioArg;
	if (FParse::Value(FCommandLine::Get(), TEXT("studio="), StudioArg)) { Page = EPage::Studio; bStudioForests = StudioArg.StartsWith(TEXT("forests")); }
	// (-menuplayers: opens on the Players page)
	if (FParse::Param(FCommandLine::Get(), TEXT("menuplayers")))
	{
		Page = EPage::Players;
		Draft = Profiles;
		// (none saved: two to show, not saved)
		if (!Draft.Num()) { Draft.Add(FOTGPlayers::NewProfile(TEXT("Player 1"), 0)); Draft.Add(FOTGPlayers::NewProfile(TEXT("Player 2"), 1)); Draft[1].bLeftHanded = true; Draft[1].Tee = TEXT("Blue"); }
	}
	// (testing: -menusearch=<words> types them in the course search, -menusort=name|recent|added)
	FParse::Value(FCommandLine::Get(), TEXT("menusearch="), Search);
	FString SortArg;
	if (FParse::Value(FCommandLine::Get(), TEXT("menusort="), SortArg)) Sort = SortArg == TEXT("recent") ? ESort::Recent : SortArg == TEXT("added") ? ESort::Added : ESort::Name;
	// the Practice tab's ways in (SetPracticeMode gives one its start)
	PracticeModes = {
		{ TEXT("Driving range"), TEXT("Hit any club as often as you like, with the ball's flight and every number from the launch monitor."), nullptr },
		{ TEXT("Putt-putt"), TEXT("A short course of fun putting holes, for a few minutes or a whole evening."), nullptr },
	};
	// (their starts: the range on your own; the putt-putt with the players picked on the Play tab)
	SetPracticeMode(TEXT("Driving range"), [this]() { if (UOTGCourseSubsystem* C = Courses()) C->PlayRange(); });
	SetPracticeMode(TEXT("Putt-putt"), [this]() {
		UOTGCourseSubsystem* C = Courses();
		if (!C) return;
		Remember();
		C->PendingSetup = Setup;
		C->PendingSetup.CourseId = AOTGMiniGolf::CourseId;
		C->bHasPendingSetup = true;
		C->PlayCourse(AOTGMiniGolf::CourseId);
	});
	ChildSlot
	[
		SNew(SOverlay)
		// the chosen course from the air, behind everything, darkened
		// (not over the studio's preview plot: it's drawn in the level behind)
		+ SOverlay::Slot()
		[
			SNew(SBorder).Visibility_Lambda([this]() { return Page == EPage::Studio ? EVisibility::Collapsed : EVisibility::Visible; })
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.02f, 0.04f, 0.03f, 1.f)).Padding(0)
			[
				SNew(SScaleBox).Stretch(EStretch::ScaleToFill)[SAssignNew(Backdrop, SImage).ColorAndOpacity(FLinearColor(0.30f, 0.34f, 0.31f, 1.f))]
			]
		]
		+ SOverlay::Slot()[SNew(SBorder).Visibility_Lambda([this]() { return Page == EPage::Studio ? EVisibility::Collapsed : EVisibility::Visible; })
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.01f, 0.02f, 0.015f, 0.62f))]
		+ SOverlay::Slot().Padding(FMargin(40, 22, 40, 16))
		[
			SNew(SVerticalBox)
			// the top bar: the name, the pages, Quit
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 40, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(TEXT("OpenTee"))).Font(Font(30, "Bold")).ColorAndOpacity(Ink).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.5f))]
						+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Text(FText::FromString(TEXT("Golf"))).Font(Font(30, "Bold")).ColorAndOpacity(Accent).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.5f))]
					]
					+ SVerticalBox::Slot().AutoHeight()[Caps(TEXT("Real courses. Your launch monitor."), 10, Gold)]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[SAssignNew(TabBar, SBox)]
				+ SHorizontalBox::Slot().FillWidth(1)[SNullWidget::NullWidget]
				// the Discord: questions, problems, what's coming (in the browser)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[DiscordButton()]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Button(TEXT("Quit"), []() {
					if (UGameInstance* GI = GameInstance()) UKismetSystemLibrary::QuitGame(GI->GetWorld(), nullptr, EQuitPreference::Quit, false);
				}, 14)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 20)
			[
				SNew(SBox).HeightOverride(1)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.10f))]
			]
			+ SVerticalBox::Slot().FillHeight(1)[SAssignNew(Body, SBox)]
			// an update: there is one (Update now / Later), how the download's going, ready to restart
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[UpdateBar()]
			// the version, and where the course data comes from
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
			[
				SNew(STextBlock).Font(Font(10)).ColorAndOpacity(FLinearColor(1, 1, 1, 0.45f)).AutoWrapText(true)
				.Text(FText::FromString(FString::Printf(TEXT("OpenTeeGolf %s  \u00B7  Courses: \u00A9 OpenStreetMap contributors (ODbL); elevation and aerial photos from USGS/USDA (US) or as credited in NOTICE.md; tree heights: Meta & WRI (CC BY 4.0); live weather: Open-Meteo (CC BY 4.0)  \u00B7  Made with Unreal Engine"),
					*GetDefault<UGeneralProjectSettings>()->ProjectVersion)))
			]
		]
		// joining an online game with a different version of its course: overwrite yours, or keep it
		+ SOverlay::Slot()[CourseChoiceWindow()]
		// on start, over everything: checking for an update, then (one found) Update now or Not now, and
		// its download - before the game starts
		+ SOverlay::Slot()[StartupWindow()]
	];
	OnlineTest();
	Rebuild();
}

TSharedRef<SWidget> SOTGMainMenu::StartupWindow()
{
	using S = EOTGUpdateState;
	auto St = [this]() { UOTGUpdateSubsystem* U = Updater(); return U ? U->GetState() : S::Off; };
	auto When = [St](std::initializer_list<S> Any) {
		TArray<S> L(Any);
		return TAttribute<EVisibility>::CreateLambda([St, L]() { return L.Contains(St()) ? EVisibility::Visible : EVisibility::Collapsed; });
	};
	auto Title = [this, St]() {
		UOTGUpdateSubsystem* U = Updater();
		if (!U) return FString();
		switch (St())
		{
		case S::Available: return U->IsInstalling() ? FString(TEXT("The scenery still needs downloading")) : FString::Printf(TEXT("OpenTeeGolf %s is out"), *U->NewVersion());
		case S::Downloading: return U->IsInstalling() ? FString(TEXT("Downloading the scenery")) : FString::Printf(TEXT("Downloading OpenTeeGolf %s"), *U->NewVersion());
		case S::Ready: return FString(TEXT("Installing the update"));
		case S::Failed: return FString(TEXT("The update didn't download"));
		default: return FString(TEXT("Checking for updates"));
		}
	};
	auto Line = [this, St]() {
		UOTGUpdateSubsystem* U = Updater();
		if (!U) return FString();
		const double Mb = U->BytesToGet() / (1024.0 * 1024.0);
		const FString Size = Mb >= 1024 ? FString::Printf(TEXT("%.1f GB"), Mb / 1024) : FString::Printf(TEXT("%.0f MB"), FMath::Max(1.0, Mb));
		switch (St())
		{
		case S::Available: return FString::Printf(TEXT("You have %s. Update now to download it (%s) and start the new version, or play this one and update another time."), *U->InstalledVersion(), *Size);
		case S::Downloading: return U->StatusLine();
		case S::Ready: return FString(TEXT("The game closes and starts again in a moment, updated."));
		case S::Failed: return U->GetMessage() + TEXT(". Try again carries on where it stopped.");
		default: return FString(TEXT("A moment while the game asks GitHub for the newest version."));
		}
	};
	auto Do = [this](TFunction<void(UOTGUpdateSubsystem&)> F) { if (UOTGUpdateSubsystem* U = Updater()) F(*U); };
	// (testing, -updatewindow=update|skip: photographed - Saved/Screenshots/update_*.png - and answered)
	FString Test;
	if (FParse::Value(FCommandLine::Get(), TEXT("updatewindow="), Test))
		RegisterActiveTimer(1.f, FWidgetActiveTimerDelegate::CreateLambda([this, Test](double, float) {
			static int32 Step = 0;
			UOTGUpdateSubsystem* U = Updater();
			if (!U || !GEngine) return EActiveTimerReturnType::Continue;
			auto Shot = [](const TCHAR* N) { GEngine->Exec(nullptr, *FString::Printf(TEXT("OTG.Shot %s"), N)); };
			const EOTGUpdateState X = U->GetState();
			if (Step == 0 && X == EOTGUpdateState::Available) { Shot(TEXT("update_offer")); Step = 1; }
			else if (Step == 1) { if (Test == TEXT("update")) U->Download(true); else U->Decline(); Step = 2; }
			else if (Step == 2 && X == EOTGUpdateState::Downloading && U->BytesDone() > U->BytesToGet() / 3) { Shot(TEXT("update_progress")); Step = 3; }
			else if (Step == 2 && Test != TEXT("update")) { Shot(TEXT("update_skipped")); Step = 3; }
			return EActiveTimerReturnType::Continue;
		}));
	return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.005f, 0.012f, 0.008f, 0.82f))
		.Visibility_Lambda([this]() { UOTGUpdateSubsystem* U = Updater(); return U && U->StartupWindowOpen() ? EVisibility::Visible : EVisibility::Collapsed; })
		.HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(560)
			[
				Card(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Caps(TEXT("Update"), 11, Accent)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
					[SNew(STextBlock).Font(Font(22, "Bold")).ColorAndOpacity(Ink).AutoWrapText(true).Text_Lambda([Title]() { return FText::FromString(Title()); })]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
					[SNew(STextBlock).Font(Font(14)).ColorAndOpacity(Soft).AutoWrapText(true).Text_Lambda([Line]() { return FText::FromString(Line()); })]
					// (how the download's going; checking and installing: the bar moving to and fro)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 0)
					[
						SNew(SBox).HeightOverride(10).Visibility(When({ S::Idle, S::Checking, S::Downloading, S::Ready }))
						[
							SNew(SProgressBar).FillColorAndOpacity(FLinearColor(0.3f, 0.75f, 0.4f, 1.f))
							.Percent_Lambda([this, St]() {
								UOTGUpdateSubsystem* U = Updater();
								if (St() != S::Downloading || !U || U->BytesToGet() <= 0) return TOptional<float>();
								return TOptional<float>(float(double(U->BytesDone()) / U->BytesToGet()));
							})
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 20, 0, 0).HAlign(HAlign_Right)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
						[SNew(SBox).Visibility(When({ S::Available }))[Button(TEXT("Not now"), [Do]() { Do([](UOTGUpdateSubsystem& U) { U.Decline(); }); }, 14)]]
						+ SHorizontalBox::Slot().AutoWidth()
						[SNew(SBox).Visibility(When({ S::Available }))[Button(TEXT("Update now"), [Do]() { Do([](UOTGUpdateSubsystem& U) { U.Download(true); }); }, 14, true)]]
						+ SHorizontalBox::Slot().AutoWidth()
						[SNew(SBox).Visibility(When({ S::Downloading }))[Button(TEXT("Cancel and play"), [Do]() { Do([](UOTGUpdateSubsystem& U) { U.Cancel(); U.Decline(); }); }, 14)]]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
						[SNew(SBox).Visibility(When({ S::Failed }))[Button(TEXT("Play without it"), [Do]() { Do([](UOTGUpdateSubsystem& U) { U.Decline(); }); }, 14)]]
						+ SHorizontalBox::Slot().AutoWidth()
						[SNew(SBox).Visibility(When({ S::Failed }))[Button(TEXT("Try again"), [Do]() { Do([](UOTGUpdateSubsystem& U) { U.Download(true); }); }, 14, true)]]
						+ SHorizontalBox::Slot().AutoWidth()
						[SNew(SBox).Visibility(When({ S::Idle, S::Checking }))[Button(TEXT("Skip"), [Do]() { Do([](UOTGUpdateSubsystem& U) { U.CloseStartupWindow(); }); }, 14)]]
					]
					, FMargin(28, 24))
			]
		];
}

UOTGUpdateSubsystem* SOTGMainMenu::Updater() const
{
	UGameInstance* GI = GameInstance();
	return GI ? GI->GetSubsystem<UOTGUpdateSubsystem>() : nullptr;
}

TSharedRef<SWidget> SOTGMainMenu::UpdateBar()
{
	using S = EOTGUpdateState;
	auto St = [this]() { UOTGUpdateSubsystem* U = Updater(); return U ? U->GetState() : S::Off; };
	auto Shown = [this, St]() {
		UOTGUpdateSubsystem* U = Updater();
		const S X = St();
		if (U && U->StartupWindowOpen()) return EVisibility::Collapsed;   // (the start-up window says it all)
		if (X == S::Downloading || X == S::Ready) return EVisibility::Visible;
		if (X == S::Available) return bUpdateLater || (U && U->WasDeclined()) ? EVisibility::Collapsed : EVisibility::Visible;
		if (X == S::Failed) return U && (U->WasAsked() || U->BytesDone() > 0) ? EVisibility::Visible : EVisibility::Collapsed;
		return EVisibility::Collapsed;
	};
	auto When = [St](std::initializer_list<S> Any) {
		TArray<S> L(Any);
		return TAttribute<EVisibility>::CreateLambda([St, L]() { return L.Contains(St()) ? EVisibility::Visible : EVisibility::Collapsed; });
	};
	return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.02f, 0.06f, 0.04f, 0.85f)).Padding(FMargin(16, 10))
		.Visibility_Lambda(Shown)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(Font(15, "Bold")).ColorAndOpacity(Ink).AutoWrapText(true)
					.Text_Lambda([this]() { UOTGUpdateSubsystem* U = Updater(); return FText::FromString(U ? U->StatusLine() : FString()); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SNew(SBox).HeightOverride(8).Visibility(When({ S::Downloading }))
					[
						SNew(SProgressBar).FillColorAndOpacity(FLinearColor(0.3f, 0.75f, 0.4f, 1.f))
						.Percent_Lambda([this]() { UOTGUpdateSubsystem* U = Updater(); return TOptional<float>(U && U->BytesToGet() > 0 ? float(double(U->BytesDone()) / U->BytesToGet()) : 0.f); })
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)
			[
				SNew(SBox).Visibility(When({ S::Available, S::Failed }))[Button(TEXT("Update now"), [this]() { if (UOTGUpdateSubsystem* U = Updater()) U->Download(); }, 14, true)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[
				SNew(SBox).Visibility(When({ S::Available }))[Button(TEXT("Later"), [this]() { bUpdateLater = true; }, 14)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)
			[
				SNew(SBox).Visibility(When({ S::Ready }))[Button(TEXT("Restart now"), [this]() { if (UOTGUpdateSubsystem* U = Updater()) U->ApplyAndRestart(); }, 14, true)]
			]
		];
}

void SOTGMainMenu::Rebuild()
{
	// the tabs, the page shown lit
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	const TPair<FString, EPage> Pages[] = { { TEXT("Play"), EPage::Play }, { TEXT("Practice"), EPage::Practice }, { TEXT("Course builder"), EPage::Build }, { TEXT("Players"), EPage::Players }, { TEXT("Online"), EPage::Online }, { TEXT("Launch monitor"), EPage::LaunchMonitor }, { TEXT("Settings"), EPage::Settings }, { TEXT("What's new"), EPage::WhatsNew } };
	for (const auto& T : Pages)
	{
		const EPage P = T.Value;
		const bool bOn = Page == P || (P == EPage::Build && Page == EPage::Studio) || (P == EditBack && Page == EPage::Edit);
		Tabs->AddSlot().AutoWidth().Padding(0, 0, 4, 0)
		[
			SNew(SButton).ButtonStyle(&TabStyle()).ContentPadding(FMargin(14, 8, 14, 0))
			.OnClicked_Lambda([this, P]() { Page = P; if (P == EPage::Players) { Draft = Profiles; ConfirmRemove = -1; } Rebuild(); return FReply::Handled(); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(FText::FromString(T.Key)).Font(Font(15, "Bold")).ColorAndOpacity(bOn ? Ink : Dim)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
				[
					SNew(SBox).HeightOverride(3)[SNew(SBorder).BorderImage(PillBrush()).BorderBackgroundColor(bOn ? Accent : FLinearColor::Transparent).Padding(0)]
				]
			]
		];
	}
	if (TabBar) TabBar->SetContent(Tabs);
	// (the studio's preview plot goes when it's left)
	if (Page != EPage::Studio && Studio.IsValid()) { Body->SetContent(SNullWidget::NullWidget); Studio.Reset(); }
	if (Backdrop) Backdrop->SetImage(Setup.CourseId.IsEmpty() ? nullptr : PhotoOf(Setup.CourseId, EPhoto::Backdrop));
	if (Page == EPage::Play)
	{
		// (the lists stay where they were scrolled to)
		const float CourseAt = CourseScroll ? CourseScroll->GetScrollOffset() : 0.f, OptionsAt = OptionsScroll ? OptionsScroll->GetScrollOffset() : 0.f;
		Body->SetContent(PlayPage());
		if (CourseScroll) CourseScroll->SetScrollOffset(CourseAt);
		if (OptionsScroll) OptionsScroll->SetScrollOffset(OptionsAt);
	}
	else if (Page == EPage::Practice) Body->SetContent(PracticePage());
	else if (Page == EPage::Players) Body->SetContent(PlayersPage());
	else if (Page == EPage::Settings) Body->SetContent(SettingsPage());
	else if (Page == EPage::WhatsNew) Body->SetContent(WhatsNewPage());
	else if (Page == EPage::Online) Body->SetContent(OnlinePage());
	// (in an online game nobody changes a course: everyone's has to stay the same as the host's)
	else if ((Page == EPage::Build || Page == EPage::Edit || Page == EPage::Studio) && Net() && Net()->InSession()) Body->SetContent(EditorsOff());
	else if (Page == EPage::Build)
	{
		if (!Builder.IsValid())
		{
			SAssignNew(Builder, SOTGCourseBuilder);
			// (built and ready: on to Play with it picked)
			Builder->OnReady = [this](const FString& Id) {
				if (UOTGCourseSubsystem* S = Courses()) CourseList = S->ListCourses();
				if (CourseList.ContainsByPredicate([&](const FOTGCourseInfo& I) { return I.Id == Id; })) { Setup.CourseId = Id; Remember(); }
				Page = EPage::Play;
				Rebuild();
			};
			Builder->OnEdit = [this](const FString& Id) { OpenEditor(Id); };
			Builder->OnStudio = [this](bool bForests) { OpenStudio(bForests); };
		}
		Body->SetContent(Builder.ToSharedRef());
	}
	else if (Page == EPage::Edit && Editor.IsValid()) Body->SetContent(Editor.ToSharedRef());
	else if (Page == EPage::Studio)
	{
		if (!Studio.IsValid())
		{
			SAssignNew(Studio, SOTGStudio, bStudioForests);
			Studio->OnClose = [this]() { Page = EPage::Build; Rebuild(); };
		}
		Body->SetContent(Studio.ToSharedRef());
	}
	else Body->SetContent(SNew(SScrollBox) + SScrollBox::Slot()[Box(SNew(SOTGLaunchMonitorPanel), FMargin(20, 16), Solid)]);
}

// The picked course's way into the editor: there when the helper has what it was built from
TSharedRef<SWidget> SOTGMainMenu::EditLink(const FOTGCourseInfo* Info)
{
	if (!Info || Info->ExportDir.IsEmpty()) return SNullWidget::NullWidget;
	if (Net() && Net()->InSession()) return Para(TEXT("Editing is off while you're in an online game"), 12, Dim);
	UGameInstance* G = GameInstance();
	UOTGHelperSubsystem* H = G ? G->GetSubsystem<UOTGHelperSubsystem>() : nullptr;
	if (H && H->HasSource(Info->Id))
	{
		const FString Id = Info->Id;
		// (and fetching its map data afresh: the helper's queue, shown on the Course builder page;
		// its trees, grass and looks are kept)
		const bool bQueued = H->ActiveFor(Id) != nullptr;
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[Ghost(TEXT("Edit this course"), [this, Id]() { OpenEditor(Id); }, 13)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0).VAlign(VAlign_Center)
			[
				bQueued ? StaticCastSharedRef<SWidget>(Para(TEXT("Updating (see the Course builder)"), 12, Dim))
					: StaticCastSharedRef<SWidget>(SNew(SBox).ToolTipText(FText::FromString(TEXT("Fetch its OpenStreetMap data, heights and photos again, then put it in the game again. Your trees, grass and looks are kept.")))
						[Ghost(TEXT("Update its map data"), [this, Id]() { if (UGameInstance* G2 = GameInstance()) if (UOTGHelperSubsystem* H2 = G2->GetSubsystem<UOTGHelperSubsystem>()) H2->Update(Id); Rebuild(); }, 13)])
			];
	}
	return SNew(SBox).ToolTipText(FText::FromString(TEXT("The editor works from what a course was built from (its OpenStreetMap data and photos), kept in Documents\\OpenTeeGolf\\CourseSources. Courses built in the game are there; this one was made elsewhere (the web version).")))
		[Para(TEXT("Made with the web version: edit it there"), 12, Dim)];
}

void SOTGMainMenu::OpenEditor(const FString& Id)
{
	if (Net() && Net()->InSession()) return;   // (online: no changing courses)
	SAssignNew(Editor, SOTGCourseEditor, Id);
	Editor->OnClose = [this](bool bExported) {
		if (bExported) if (UOTGCourseSubsystem* S = Courses()) CourseList = S->ListCourses();
		if (Builder.IsValid()) Builder->Refresh();
		Page = EditBack;
		Editor.Reset();
		Rebuild();
	};
	if (Page != EPage::Edit) EditBack = Page;
	Page = EPage::Edit;
	Rebuild();
}

void SOTGMainMenu::OpenStudio(bool bForests)
{
	bStudioForests = bForests;
	Page = EPage::Studio;
	Rebuild();
}

void SOTGMainMenu::ShowPage(const FString& Name)
{
	if (Name == TEXT("biomes") || Name == TEXT("forests")) { OpenStudio(Name == TEXT("forests")); return; }
	Page = Name == TEXT("online") ? EPage::Online : Name == TEXT("players") ? EPage::Players : Name == TEXT("lm") ? EPage::LaunchMonitor : Name == TEXT("settings") ? EPage::Settings : Name == TEXT("build") ? EPage::Build : Name == TEXT("practice") ? EPage::Practice : Name == TEXT("news") ? EPage::WhatsNew : EPage::Play;
	if (Page == EPage::Players) { Draft = Profiles; ConfirmRemove = -1; }
	Rebuild();
}

// What's new: WHATS_NEW.md's versions, newest first, each a card ("## <version> - <date>", then its
// "- " lines); this version marked. (Next to OpenTeeGolf.exe in a release; in the repo, unreal/release.)

void SOTGMainMenu::Start()
{
	UOTGCourseSubsystem* C = Courses();
	if (!C || Setup.CourseId.IsEmpty()) return;
	Setup.LastPlayed.Add(Setup.CourseId, FDateTime::UtcNow());
	Remember();
	C->PendingSetup = Setup;
	C->bHasPendingSetup = true;
	C->PlayCourse(Setup.CourseId);
}
