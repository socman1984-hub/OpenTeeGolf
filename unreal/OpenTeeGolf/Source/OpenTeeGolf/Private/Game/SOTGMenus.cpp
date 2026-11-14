// See SOTGMenus.h. (The web version's menu: public/index.html and players-menu.js; its Mevo+ setup
// page: mevo-page.js.)

#include "SOTGMenus.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "GeneralProjectSettings.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "OTGTextures.h"
#include "Kismet/KismetSystemLibrary.h"
#include "OTGCourseData.h"
#include "OTGLaunchMonitorSubsystem.h"
#include "OTGUi.h"
#include "Game/OTGWeather.h"
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

namespace
{
	UGameInstance* GameInstance()
	{
		return GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetGameInstance() : nullptr;
	}
	const TCHAR* TeeOrder[] = { TEXT("Black"), TEXT("Blue"), TEXT("White"), TEXT("Gold"), TEXT("Green"), TEXT("Yellow"), TEXT("Red"), TEXT("Junior"), TEXT("Par3") };
	const TCHAR* PinDays[] = { TEXT("Thursday"), TEXT("Friday"), TEXT("Saturday"), TEXT("Sunday") };
	const TCHAR* PlayerTees[] = { TEXT(""), TEXT("Black"), TEXT("Blue"), TEXT("White"), TEXT("Gold"), TEXT("Green"), TEXT("Red") };

	TSharedRef<SWidget> Heading(const FString& S) { return Text(S, 14, Soft, "Bold"); }
	TSharedRef<SWidget> Swatch(const FLinearColor& C, float Size = 12)
	{
		return SNew(SBox).WidthOverride(Size).HeightOverride(Size)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(C)];
	}
	TSharedRef<SWidget> Number(double Min, double Max, double Delta, TFunction<double()> Get, TFunction<void(double)> Set)
	{
		return SNew(SBox).WidthOverride(110)
			[
				SNew(SSpinBox<double>).MinValue(Min).MaxValue(Max).Delta(Delta)
				.Value_Lambda([Get]() { return Get(); })
				.OnValueChanged_Lambda([Set](double V) { Set(V); })
			];
	}
	TSharedRef<SWidget> Row(const FString& Label, const TSharedRef<SWidget>& W)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[SNew(SBox).WidthOverride(190)[Text(Label, 14, Soft)]]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[W];
	}
}

// ------------------------------------------------------------------ the launch monitor

UOTGLaunchMonitorSubsystem* SOTGLaunchMonitorPanel::LM() const
{
	UGameInstance* GI = GameInstance();
	return GI ? GI->GetSubsystem<UOTGLaunchMonitorSubsystem>() : nullptr;
}

void SOTGLaunchMonitorPanel::Construct(const FArguments&)
{
	if (UOTGLaunchMonitorSubsystem* L = LM())
	{
		Draft = L->GetMevoSetup();
		bArmOnTurn = L->GetArmOnTurn();
		Shown = L->GetConnection();
		bShownOff = L->IsDeviceOff();
		Address = Shown == EOTGConnection::Frp ? L->GetFrpUrl() : L->GetMevoHost();
	}
	ChildSlot[SAssignNew(Body, SBox).WidthOverride(560)];
	Rebuild();
}

void SOTGLaunchMonitorPanel::Rebuild()
{
	UOTGLaunchMonitorSubsystem* L = LM();
	if (!L) { Body->SetContent(Text(TEXT("No launch monitor connection"), 15, Soft)); return; }
	const EOTGConnection C = L->GetConnection();
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Text(TEXT("Launch monitor"), 22, Ink, "Bold")];
	// how shots arrive
	TSharedRef<SWrapBox> Kinds = SNew(SWrapBox).UseAllottedSize(true);
	const TPair<EOTGConnection, FString> Options[] = {
		{ EOTGConnection::FSGolf, TEXT("FS Golf (OpenConnect)") }, { EOTGConnection::Direct, TEXT("Mevo+ directly") },
		{ EOTGConnection::R10, TEXT("Garmin R10") }, { EOTGConnection::Square, TEXT("Square Golf") }, { EOTGConnection::Frp, TEXT("Other (FRP)") },
	};
	for (const TPair<EOTGConnection, FString>& O : Options)
	{
		const EOTGConnection K = O.Key;
		Kinds->AddSlot().Padding(0, 0, 6, 6)[Button(O.Value, [this, K]() {
			if (UOTGLaunchMonitorSubsystem* X = LM())
			{
				X->SetConnection(K);
				Address = K == EOTGConnection::Frp ? X->GetFrpUrl() : X->GetMevoHost();
			}
			Rebuild();
		}, 14, C == K)];
	}
	V->AddSlot().AutoHeight()[Kinds];
	// where it is
	auto Hint = [&]() -> FString {
		switch (C)
		{
		case EOTGConnection::FSGolf: return FString::Printf(TEXT("In FS Golf, connect to this PC (OpenConnect, port %d)."), L->GetOpenConnectPort());
		case EOTGConnection::Direct: return TEXT("Join this PC to the Mevo+'s Wi-Fi (FS M2-...); FS Golf must be closed.");
		case EOTGConnection::R10: return TEXT("Pair the R10 in Windows Bluetooth settings; the game runs its bridge program.");
		case EOTGConnection::Square: return TEXT("Turn the Square Golf on; the game runs its bridge program.");
		default: return TEXT("The device's Flight Relay Protocol address (ws://...).");
		}
	};
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 6)[SNew(STextBlock).Text(FText::FromString(Hint())).Font(Font(13)).ColorAndOpacity(Soft).AutoWrapText(true)];
	if (C == EOTGConnection::Direct || C == EOTGConnection::Frp)
	{
		V->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 6, 0)
			[
				SNew(SEditableTextBox).Text(FText::FromString(Address)).Font(Font(14))
				.OnTextChanged_Lambda([this](const FText& T) { Address = T.ToString(); })
			]
			+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Connect"), [this, C]() { if (UOTGLaunchMonitorSubsystem* X = LM()) X->SetConnection(C, Address.TrimStartAndEnd()); Rebuild(); }, 14)]
		];
	}
	// what it's doing (Tick keeps it current)
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SAssignNew(StatusText, STextBlock).Font(Font(16, "Bold")).ColorAndOpacity(Ink)];
	V->AddSlot().AutoHeight().Padding(0, 2, 0, 8)[SAssignNew(DetailText, STextBlock).Font(Font(13)).ColorAndOpacity(Soft).AutoWrapText(true)];
	// the Mevo+ setup
	if (C == EOTGConnection::Direct)
	{
		V->AddSlot().AutoHeight().Padding(0, 4, 0, 4)[Heading(TEXT("Mevo+ setup"))];
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Mevo+ to ball (m)"), Number(1.5, 6, 0.01, [this]() { return Draft.DistanceM; }, [this](double X) { Draft.DistanceM = X; }))];
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Mat height (in)"), Number(0, 10, 0.25, [this]() { return Draft.SurfaceIn; }, [this](double X) { Draft.SurfaceIn = X; }))];
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Driver tee height (in)"), Number(0, 4, 0.25, [this]() { return Draft.TeeHeightIn; }, [this](double X) { Draft.TeeHeightIn = X; }))];
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Tracking minimum"), Number(0.6, 1, 0.05, [this]() { return Draft.TrackPct; }, [this](double X) { Draft.TrackPct = X; }))];
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Balls"), SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Standard"), [this]() { Draft.BallType = 0; Rebuild(); }, 13, Draft.BallType == 0)]
			+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("RCT"), [this]() { Draft.BallType = 1; Rebuild(); }, 13, Draft.BallType == 1)])];
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Detection mode"), SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("By club"), [this]() { Draft.Mode = TEXT("auto"); Rebuild(); }, 13, Draft.Mode == TEXT("auto"))]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Full swing"), [this]() { Draft.Mode = TEXT("full"); Rebuild(); }, 13, Draft.Mode == TEXT("full"))]
			+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Outdoor"), [this]() { Draft.Mode = TEXT("outdoor"); Rebuild(); }, 13, Draft.Mode == TEXT("outdoor"))])];
		V->AddSlot().AutoHeight().Padding(0, 4)
		[
			SNew(SCheckBox).IsChecked_Lambda([this]() { return bArmOnTurn ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bArmOnTurn = S == ECheckBoxState::Checked; if (UOTGLaunchMonitorSubsystem* X = LM()) X->SetArmOnTurn(bArmOnTurn); })
			[Text(TEXT("Arm only on my turn (stays quiet between shots)"), 14, Ink)]
		];
		V->AddSlot().AutoHeight().Padding(0, 4)[Button(TEXT("Save the setup"), [this]() { if (UOTGLaunchMonitorSubsystem* X = LM()) X->SetMevoSetup(Draft); }, 14)];
	}
	V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Test shot"), [this]() { if (UOTGLaunchMonitorSubsystem* X = LM()) X->InjectShot(150, 12, 0, 2700, 0); }, 14)]
		+ SHorizontalBox::Slot().AutoWidth()[C == EOTGConnection::FSGolf ? SNullWidget::NullWidget
			: Button(L->IsDeviceOff() ? TEXT("Connect") : TEXT("Disconnect"), [this]() {
				if (UOTGLaunchMonitorSubsystem* X = LM()) { if (X->IsDeviceOff()) X->ConnectDevice(); else X->DisconnectDevice(); }
				Rebuild();
			}, 14)]
	];
	Shown = C;
	bShownOff = L->IsDeviceOff();
	Body->SetContent(V);
}

void SOTGLaunchMonitorPanel::Tick(const FGeometry& Geometry, const double Time, const float Dt)
{
	SCompoundWidget::Tick(Geometry, Time, Dt);
	UOTGLaunchMonitorSubsystem* L = LM();
	if (!L || !StatusText) return;
	if (L->GetConnection() != Shown || L->IsDeviceOff() != bShownOff) Rebuild();
	const FOTGLaunchMonitorStatus S = L->GetStatus();
	FString Line = (S.Device.IsEmpty() ? FString(TEXT("Launch monitor")) : S.Device) + TEXT(": ") + S.Text;
	if (S.Battery >= 0) Line += FString::Printf(TEXT("  ·  %d%%"), S.Battery);
	StatusText->SetText(FText::FromString(Line));
	StatusText->SetColorAndOpacity(S.bReady ? Green : S.bConnected ? Gold : Ink);
	TArray<FString> D;
	if (!S.Error.IsEmpty()) D.Add(S.Error);
	if (!S.PortError.IsEmpty()) D.Add(S.PortError);
	if (!S.Mode.IsEmpty()) D.Add(FString::Printf(TEXT("Mode: %s"), *S.Mode));
	if (FMath::IsFinite(S.Tilt)) D.Add(FString::Printf(TEXT("Level: tilt %.1f°, roll %.1f° (aim for %.0f° tilt, 0° roll)"), S.Tilt, S.Roll, Draft.TiltTarget));
	if (!S.DeviceText.IsEmpty()) D.Add(FString::Printf(TEXT("It says: %s"), *S.DeviceText));
	if (S.LastShotAt.GetTicks()) D.Add(FString::Printf(TEXT("Last shot: %s"), *(S.LastShotAt + (FDateTime::Now() - FDateTime::UtcNow())).ToString(TEXT("%H:%M:%S"))));
	DetailText->SetText(FText::FromString(FString::Join(D, TEXT("\n"))));
}

// ------------------------------------------------------------------ the main menu

UOTGCourseSubsystem* SOTGMainMenu::Courses() const
{
	UGameInstance* GI = GameInstance();
	return GI ? GI->GetSubsystem<UOTGCourseSubsystem>() : nullptr;
}

const FSlateBrush* SOTGMainMenu::PhotoOf(const FString& Id, bool bCrop)
{
	const FString Key = Id + (bCrop ? TEXT("#card") : TEXT(""));
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
		Brush->ImageSize = bCrop ? FVector2D(132, 88) : FVector2D(1920, 1920);
		// (a card: the middle of the course; the backdrop: most of it)
		Brush->SetUVRegion(bCrop ? FBox2f(FVector2f(0.33f, 0.36f), FVector2f(0.67f, 0.59f)) : FBox2f(FVector2f(0.15f, 0.15f), FVector2f(0.85f, 0.85f)));
		if (!Tex->IsA<UTexture2D>() || Tex->GetOutermost() != GetTransientPackage()) Tex->SetForceMipLevelsToBeResident(600.f);
	}
	else Brush->TintColor = FLinearColor(0.05f, 0.1f, 0.06f, 1.f);
	Photos.Add(Key, Brush);
	return Brush.Get();
}

void SOTGMainMenu::Construct(const FArguments&)
{
	Setup = FOTGPlayers::LoadSetup();
	Profiles = FOTGPlayers::Load();
	Setup.Picked.RemoveAll([&](const FString& Id) { return !Profiles.ContainsByPredicate([&](const FOTGPlayerProfile& P) { return P.Id == Id; }); });
	if (UOTGCourseSubsystem* C = Courses()) CourseList = C->ListCourses();
	if (CourseList.Num() && !CourseList.ContainsByPredicate([&](const FOTGCourseInfo& I) { return I.Id == Setup.CourseId; })) Setup.CourseId = CourseList[0].Id;
	ChildSlot
	[
		SNew(SOverlay)
		// the chosen course from the air, behind everything, darkened
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.02f, 0.04f, 0.03f, 1.f)).Padding(0)
			[
				SNew(SScaleBox).Stretch(EStretch::ScaleToFill)[SAssignNew(Backdrop, SImage).ColorAndOpacity(FLinearColor(0.45f, 0.5f, 0.45f, 1.f))]
			]
		]
		+ SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.01f, 0.03f, 0.02f, 0.4f))]
		+ SOverlay::Slot().Padding(FMargin(48, 30))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 22)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 44, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("OpenTeeGolf"))).Font(Font(46, "Bold")).ColorAndOpacity(Ink).ShadowOffset(FVector2D(2, 2)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))]
					+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("Real courses. Your launch monitor."))).Font(Font(15, "Italic")).ColorAndOpacity(Gold).ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SAssignNew(TabBar, SBox)]
				+ SHorizontalBox::Slot().FillWidth(1)[SNullWidget::NullWidget]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Button(TEXT("Quit"), []() {
					if (UGameInstance* GI = GameInstance()) UKismetSystemLibrary::QuitGame(GI->GetWorld(), nullptr, EQuitPreference::Quit, false);
				}, 16)]
			]
			+ SVerticalBox::Slot().FillHeight(1)[SAssignNew(Body, SBox)]
			// the version, and where the course data comes from
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
			[
				SNew(STextBlock).Font(Font(11)).ColorAndOpacity(FLinearColor(1, 1, 1, 0.65f)).AutoWrapText(true).ShadowOffset(FVector2D(1, 1))
				.Text(FText::FromString(FString::Printf(TEXT("OpenTeeGolf %s  \u00B7  Courses: \u00A9 OpenStreetMap contributors (ODbL); elevation and aerial photos from USGS/USDA (US) or as credited in NOTICE.md; tree heights: Meta & WRI (CC BY 4.0); live weather: Open-Meteo (CC BY 4.0)  \u00B7  Made with Unreal Engine"),
					*GetDefault<UGeneralProjectSettings>()->ProjectVersion)))
			]
		]
	];
	Rebuild();
}

void SOTGMainMenu::Rebuild()
{
	// the tabs, the page shown lit
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	const TPair<FString, EPage> Pages[] = { { TEXT("Play"), EPage::Play }, { TEXT("Players"), EPage::Players }, { TEXT("Launch monitor"), EPage::LaunchMonitor }, { TEXT("Settings"), EPage::Settings } };
	for (const auto& T : Pages)
	{
		const EPage P = T.Value;
		Tabs->AddSlot().AutoWidth().Padding(0, 0, 8, 0)
		[
			SNew(SButton).ButtonStyle(&ButtonStyle(Page == P)).ContentPadding(FMargin(18, 8))
			.OnClicked_Lambda([this, P]() { Page = P; if (P == EPage::Players) { Draft = Profiles; ConfirmRemove = -1; } Rebuild(); return FReply::Handled(); })
			[SNew(STextBlock).Text(FText::FromString(T.Key)).Font(Font(16, "Bold")).ColorAndOpacity(Ink)]
		];
	}
	if (TabBar) TabBar->SetContent(Tabs);
	if (Backdrop) Backdrop->SetImage(Setup.CourseId.IsEmpty() ? nullptr : PhotoOf(Setup.CourseId, false));
	if (Page == EPage::Play) Body->SetContent(PlayPage());
	else if (Page == EPage::Players) Body->SetContent(PlayersPage());
	else if (Page == EPage::Settings) Body->SetContent(SettingsPage());
	else Body->SetContent(SNew(SScrollBox) + SScrollBox::Slot()[Box(SNew(SOTGLaunchMonitorPanel), FMargin(20, 16), Solid)]);
}

TSharedRef<SWidget> SOTGMainMenu::PlayPage()
{
	// the courses
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	List->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Heading(TEXT("Course"))];
	if (!CourseList.Num())
		List->AddSlot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("No courses yet. In OpenTeeGolf's web version, click Export to Unreal on a course's card: it appears here (restart the game if it's open)."))).Font(Font(14)).ColorAndOpacity(Soft).AutoWrapText(true)];
	// (where the game looks, and a way to open it)
	if (UOTGCourseSubsystem* C = Courses())
	{
		const FString Folder = C->CoursesFolder();
		List->AddSlot().AutoHeight().Padding(0, 4, 0, 8)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("Courses folder: ") + Folder)).Font(Font(12)).ColorAndOpacity(Soft).AutoWrapText(true)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)[Button(TEXT("Open"), [Folder]() { IFileManager::Get().MakeDirectory(*Folder, true); FPlatformProcess::ExploreFolder(*Folder); }, 12)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)[Button(TEXT("Refresh"), [this]() { if (UOTGCourseSubsystem* S = Courses()) CourseList = S->ListCourses(); Rebuild(); }, 12)]
		];
	}
	for (const FOTGCourseInfo& I : CourseList)
	{
		const FString Id = I.Id;
		const bool bOn = Setup.CourseId == Id;
		List->AddSlot().AutoHeight().Padding(0, 3)
		[
			SNew(SButton).ButtonStyle(&ButtonStyle(bOn)).ContentPadding(FMargin(6))
			.OnClicked_Lambda([this, Id]() { Setup.CourseId = Id; Remember(); Rebuild(); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)[SNew(SBox).WidthOverride(132).HeightOverride(88)[SNew(SImage).Image(PhotoOf(Id, true))]]
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(I.Name)).Font(Font(17, "Bold")).ColorAndOpacity(Ink).AutoWrapText(true)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[SNew(STextBlock).Text(FText::FromString(I.Location)).Font(Font(13)).ColorAndOpacity(Soft).AutoWrapText(true)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[Text(FString::Printf(TEXT("%d holes"), I.Holes), 13, Gold, "Bold")]
				]
			]
		];
	}
	UOTGCourseData* Course = Courses() && !Setup.CourseId.IsEmpty() ? Courses()->LoadCourse(Setup.CourseId) : nullptr;

	TSharedRef<SVerticalBox> Opts = SNew(SVerticalBox);
	auto Choices = [&](const FString& Label, const TArray<FString>& Values, TFunction<FString()> Get, TFunction<void(const FString&)> Set, TFunction<FString(const FString&)> Name = nullptr) {
		TSharedRef<SWrapBox> W = SNew(SWrapBox).UseAllottedSize(true);
		for (const FString& V : Values)
			W->AddSlot().Padding(0, 0, 6, 6)[Button(Name ? Name(V) : V, [this, Set, V]() { Set(V); Remember(); Rebuild(); }, 14, Get() == V)];
		Opts->AddSlot().AutoHeight().Padding(0, 6, 0, 2)[Heading(Label)];
		Opts->AddSlot().AutoHeight()[W];
	};
	if (Course)
	{
		// the tees and pins the course has
		TArray<FString> Tees, Pins;
		for (const TCHAR* T : TeeOrder) for (const FOTGHole& H : Course->Holes) if (H.Tees.Contains(T)) { Tees.AddUnique(T); break; }
		for (const FOTGHole& H : Course->Holes) for (const auto& T : H.Tees) Tees.AddUnique(T.Key);
		for (const TCHAR* P : PinDays) for (const FOTGHole& H : Course->Holes) if (H.Pins.Contains(P)) { Pins.AddUnique(P); break; }
		for (const FOTGHole& H : Course->Holes) for (const auto& P : H.Pins) Pins.AddUnique(P.Key);
		if (!Tees.Contains(Setup.Tee) && Tees.Num()) Setup.Tee = Tees.Contains(TEXT("White")) ? TEXT("White") : Tees[0];
		if (!Pins.Contains(Setup.Pin) && Pins.Num()) Setup.Pin = Pins.Contains(TEXT("Saturday")) ? TEXT("Saturday") : Pins[0];
		Choices(TEXT("Tees"), Tees, [this]() { return Setup.Tee; }, [this](const FString& V) { Setup.Tee = V; });
		Choices(TEXT("Pins"), Pins, [this]() { return Setup.Pin; }, [this](const FString& V) { Setup.Pin = V; });
		const int32 N = Course->Holes.Num();
		Setup.StartHole = FMath::Clamp(Setup.StartHole, 1, FMath::Max(1, N));
		Opts->AddSlot().AutoHeight().Padding(0, 6, 0, 2)[Heading(TEXT("Start at hole"))];
		Opts->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("1"), [this]() { Setup.StartHole = 1; Remember(); Rebuild(); }, 14, Setup.StartHole == 1)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[N > 9 ? StaticCastSharedRef<SWidget>(Button(TEXT("10"), [this]() { Setup.StartHole = 10; Remember(); Rebuild(); }, 14, Setup.StartHole == 10)) : SNullWidget::NullWidget]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 10, 0)[Text(TEXT("or"), 13, Soft)]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(80)[SNew(SSpinBox<int32>).MinValue(1).MaxValue(FMath::Max(1, N)).Value_Lambda([this]() { return Setup.StartHole; })
					.OnValueCommitted_Lambda([this](int32 V, ETextCommit::Type) { Setup.StartHole = V; Remember(); Rebuild(); })]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(18, 0, 6, 0)[Button(TEXT("18 holes"), [this]() { Setup.Holes = 18; Remember(); Rebuild(); }, 14, Setup.Holes != 9)]
			+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("9 holes"), [this]() { Setup.Holes = 9; Remember(); Rebuild(); }, 14, Setup.Holes == 9)]
		];
	}
	// the weather
	{
		TArray<FString> Keys, Names;
		for (const auto& C : FOTGWeather::Choices()) { Keys.Add(C.Key); Names.Add(C.Value); }
		Choices(TEXT("Conditions"), Keys, [this]() { return Setup.Weather; }, [this](const FString& V) { Setup.Weather = V; },
			[Keys, Names](const FString& K) { const int32 I = Keys.IndexOfByKey(K); return I >= 0 ? Names[I] : K; });
		if (Setup.Weather == TEXT("custom"))
		{
			TSharedRef<SHorizontalBox> Dir = SNew(SHorizontalBox);
			const TPair<FString, FString> Dirs[] = { { TEXT("0"), TEXT("Into you") }, { TEXT("180"), TEXT("Behind you") }, { TEXT("90"), TEXT("Right to left") }, { TEXT("270"), TEXT("Left to right") }, { TEXT("rand"), TEXT("Random") } };
			for (const auto& D : Dirs)
			{
				const FString K = D.Key;
				Dir->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[Button(D.Value, [this, K]() { Setup.WindDir = K; Remember(); Rebuild(); }, 13, Setup.WindDir == K)];
			}
			Opts->AddSlot().AutoHeight().Padding(0, 4)[Row(TEXT("Wind (mph)"), Number(0, 40, 1, [this]() { return Setup.WindMph; }, [this](double X) { Setup.WindMph = FMath::RoundToDouble(X); Remember(); }))];
			Opts->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Wind from"), Dir)];
		}
	}
	// who's playing: click to add (the order picked is the order they tee off)
	const FOTGFormat& F = FOTGGame::FormatOf(Setup.Format);
	const bool bTeamsOn = F.bTeams || (F.Key == TEXT("match") && Setup.Picked.Num() > 2);
	Opts->AddSlot().AutoHeight().Padding(0, 10, 0, 2)[Heading(TEXT("Players"))];
	TSharedRef<SWrapBox> Chips = SNew(SWrapBox).UseAllottedSize(true);
	for (const FOTGPlayerProfile& P : Profiles)
	{
		const FString Id = P.Id;
		const int32 At = Setup.Picked.IndexOfByKey(Id);
		const FString Team = Setup.Teams.Contains(Id) ? Setup.Teams[Id] : TEXT("A");
		TSharedRef<SHorizontalBox> Chip = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)[Swatch(P.Colour)]
			+ SHorizontalBox::Slot().AutoWidth()[Button(At >= 0 ? FString::Printf(TEXT("%d. %s"), At + 1, *P.Name) : P.Name, [this, Id]() {
				const int32 I = Setup.Picked.IndexOfByKey(Id);
				if (I >= 0) Setup.Picked.RemoveAt(I);
				else
				{
					Setup.Picked.Add(Id);
					// (new team players alternate A, B, A, B)
					if (!Setup.Teams.Contains(Id)) Setup.Teams.Add(Id, Setup.Picked.Num() % 2 ? TEXT("A") : TEXT("B"));
				}
				Remember(); Rebuild();
			}, 14, At >= 0)];
		if (At >= 0 && bTeamsOn)
			Chip->AddSlot().AutoWidth().Padding(4, 0, 0, 0)[Button(TEXT("Team ") + Team, [this, Id, Team]() { Setup.Teams.Add(Id, Team == TEXT("A") ? TEXT("B") : TEXT("A")); Remember(); Rebuild(); }, 13)];
		Chips->AddSlot().Padding(0, 0, 10, 6)[Chip];
	}
	if (!Profiles.Num()) Chips->AddSlot()[Text(TEXT("No players yet: add yourself and your friends under Players."), 14, Soft)];
	Opts->AddSlot().AutoHeight()[Chips];
	// the game
	TArray<FString> Keys;
	for (const FOTGFormat& X : FOTGGame::Formats()) Keys.Add(X.Key);
	Choices(TEXT("Game"), Keys, [this]() { return Setup.Format; }, [this](const FString& V) { Setup.Format = V; }, [](const FString& K) { return FOTGGame::FormatOf(K).Name; });
	Opts->AddSlot().AutoHeight().Padding(0, 4)
	[
		SNew(SCheckBox).IsChecked_Lambda([this]() { return Setup.bHandicaps ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { Setup.bHandicaps = S == ECheckBoxState::Checked; Remember(); })
		[Text(TEXT("Handicaps count"), 14, Ink)]
	];
	const FString Problem = FOTGGame::Problem(Setup.Format, Setup.Picked, Setup.Teams);
	const FString About = Setup.Picked.Num() ? F.About : TEXT("Pick players to play together, or leave everyone out for a round on your own.");
	Opts->AddSlot().AutoHeight().Padding(0, 4)[SNew(STextBlock).Text(FText::FromString(About)).Font(Font(13)).ColorAndOpacity(Soft).AutoWrapText(true)];
	if (!Problem.IsEmpty()) Opts->AddSlot().AutoHeight()[Text(Problem, 14, Warn, "Bold")];
	const bool bCanStart = Course && Problem.IsEmpty();
	Opts->AddSlot().AutoHeight().Padding(0, 16, 0, 0).HAlign(HAlign_Left)
	[
		SNew(SBox).IsEnabled(bCanStart)[Button(TEXT("Start the round"), [this]() { Start(); }, 20, true)]
	];

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.38f).Padding(0, 0, 24, 0)[SNew(SScrollBox) + SScrollBox::Slot()[Box(List, FMargin(16, 12), Solid)]]
		+ SHorizontalBox::Slot().FillWidth(0.62f)[SNew(SScrollBox) + SScrollBox::Slot()[Box(Opts, FMargin(20, 14), Solid)]];
}

TSharedRef<SWidget> SOTGMainMenu::PlayersPage()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Text(TEXT("Players"), 22, Ink, "Bold")];
	const TArray<FLinearColor> Colours = FOTGPlayers::Colours();
	for (int32 i = 0; i < Draft.Num(); i++)
	{
		FOTGPlayerProfile& P = Draft[i];
		int32 Played = P.Rounds.Num();
		V->AddSlot().AutoHeight().Padding(0, 3)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			[
				SNew(SButton).ButtonColorAndOpacity(P.Colour).ToolTipText(FText::FromString(TEXT("Colour: click for the next one")))
				.OnClicked_Lambda([this, i, Colours]() {
					int32 Ci = 0;
					for (int32 k = 0; k < Colours.Num(); k++) if (Colours[k].Equals(Draft[i].Colour, 0.01f)) Ci = k;
					Draft[i].Colour = Colours[(Ci + 1) % Colours.Num()];
					Rebuild();
					return FReply::Handled();
				})[SNew(SBox).WidthOverride(18).HeightOverride(18)]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SBox).WidthOverride(200)[SNew(SEditableTextBox).Text(FText::FromString(P.Name)).Font(Font(15))
					.OnTextChanged_Lambda([this, i](const FText& T) { if (Draft.IsValidIndex(i)) Draft[i].Name = T.ToString(); })]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)[Text(TEXT("HCP"), 13, Soft)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				Number(-10, 54, 0.1, [this, i]() { return Draft.IsValidIndex(i) ? Draft[i].Handicap : 0.0; }, [this, i](double X) { if (Draft.IsValidIndex(i)) Draft[i].Handicap = FMath::RoundToDouble(X * 10) / 10; })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(P.bLeftHanded ? TEXT("Left handed") : TEXT("Right handed"), [this, i]() { Draft[i].bLeftHanded = !Draft[i].bLeftHanded; Rebuild(); }, 13)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(P.Tee.IsEmpty() ? TEXT("Round's tees") : P.Tee + TEXT(" tees"), [this, i]() {
				int32 Ti = 0;
				for (int32 k = 0; k < UE_ARRAY_COUNT(PlayerTees); k++) if (Draft[i].Tee == PlayerTees[k]) Ti = k;
				Draft[i].Tee = PlayerTees[(Ti + 1) % UE_ARRAY_COUNT(PlayerTees)];
				Rebuild();
			}, 13)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Button(ConfirmRemove == i ? TEXT("Remove (sure?)") : TEXT("Remove"), [this, i]() {
				if (ConfirmRemove != i) { ConfirmRemove = i; Rebuild(); return; }
				Draft.RemoveAt(i);
				ConfirmRemove = -1;
				Rebuild();
			}, 13)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Text(Played ? FString::Printf(TEXT("%d round%s played"), Played, Played == 1 ? TEXT("") : TEXT("s")) : FString(), 12, Soft)]
		];
	}
	if (!Draft.Num()) V->AddSlot().AutoHeight()[Text(TEXT("No players yet."), 14, Soft)];
	V->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Button(TEXT("Add a player"), [this]() { Draft.Add(FOTGPlayers::NewProfile(FString::Printf(TEXT("Player %d"), Draft.Num() + 1), Draft.Num())); Rebuild(); }, 15)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Save the players"), [this]() {
			Draft.RemoveAll([](const FOTGPlayerProfile& P) { return P.Name.TrimStartAndEnd().IsEmpty(); });
			Profiles = Draft;
			FOTGPlayers::Save(Profiles);
			Setup.Picked.RemoveAll([&](const FString& Id) { return !Profiles.ContainsByPredicate([&](const FOTGPlayerProfile& P) { return P.Id == Id; }); });
			Remember();
			Page = EPage::Play;
			Rebuild();
		}, 15, true)]
	];
	return SNew(SScrollBox) + SScrollBox::Slot()[Box(V, FMargin(20, 16), Solid)];
}

void SOTGMainMenu::ShowPage(const FString& Name)
{
	Page = Name == TEXT("players") ? EPage::Players : Name == TEXT("lm") ? EPage::LaunchMonitor : Name == TEXT("settings") ? EPage::Settings : EPage::Play;
	if (Page == EPage::Players) { Draft = Profiles; ConfirmRemove = -1; }
	Rebuild();
}

// Display and graphics, applied and saved at once (GameUserSettings.ini)
TSharedRef<SWidget> SOTGMainMenu::SettingsPage()
{
	UGameUserSettings* GS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Text(TEXT("Settings"), 22, Ink, "Bold")];
	if (!GS) return Box(V, FMargin(20, 16), Solid);
	auto Apply = [this, GS]() { GS->ApplySettings(false); GS->SaveSettings(); Rebuild(); };
	auto Row2 = [&](const FString& Label, const TSharedRef<SWidget>& W) {
		V->AddSlot().AutoHeight().Padding(0, 6, 0, 2)[Heading(Label)];
		V->AddSlot().AutoHeight()[W];
	};
	// how the game fills the screen
	{
		TSharedRef<SWrapBox> W = SNew(SWrapBox).UseAllottedSize(true);
		const TPair<EWindowMode::Type, FString> Modes[] = { { EWindowMode::WindowedFullscreen, TEXT("Full screen (borderless)") }, { EWindowMode::Fullscreen, TEXT("Full screen") }, { EWindowMode::Windowed, TEXT("In a window") } };
		for (const auto& M : Modes)
		{
			const EWindowMode::Type Mode = M.Key;
			W->AddSlot().Padding(0, 0, 6, 6)[Button(M.Value, [GS, Mode, Apply]() { GS->SetFullscreenMode(Mode); Apply(); }, 14, GS->GetFullscreenMode() == Mode)];
		}
		Row2(TEXT("Display"), W);
	}
	// the resolution (the screen's own sizes)
	{
		TArray<FIntPoint> Sizes;
		UKismetSystemLibrary::GetSupportedFullscreenResolutions(Sizes);
		Sizes.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X * A.Y > B.X * B.Y; });
		TSharedRef<SWrapBox> W = SNew(SWrapBox).UseAllottedSize(true);
		const FIntPoint Now = GS->GetScreenResolution();
		int32 Shown = 0;
		for (const FIntPoint& S : Sizes)
		{
			if (S.X < 1280 || ++Shown > 8) continue;
			W->AddSlot().Padding(0, 0, 6, 6)[Button(FString::Printf(TEXT("%d x %d"), S.X, S.Y), [GS, S, Apply]() { GS->SetScreenResolution(S); Apply(); }, 13, Now == S)];
		}
		Row2(TEXT("Resolution"), W);
	}
	// graphics quality (Unreal's scalability levels: shadows, foliage, effects, view distance)
	{
		TSharedRef<SWrapBox> W = SNew(SWrapBox).UseAllottedSize(true);
		const FString Names[] = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") };
		const int32 Level = GS->GetOverallScalabilityLevel();
		for (int32 i = 0; i < 4; i++)
			W->AddSlot().Padding(0, 0, 6, 6)[Button(Names[i], [GS, i, Apply]() { GS->SetOverallScalabilityLevel(i); Apply(); }, 14, Level == i)];
		W->AddSlot().Padding(8, 0, 6, 6)[Button(TEXT("Pick for this PC"), [GS, Apply]() { GS->RunHardwareBenchmark(); GS->ApplyHardwareBenchmarkResults(); Apply(); }, 13)];
		Row2(TEXT("Graphics quality"), W);
		V->AddSlot().AutoHeight().Padding(0, 2)[Text(TEXT("Lower it if the game runs slowly: the grass, trees and shadows cost the most."), 12, Soft)];
	}
	// where the game finds exported courses (the web version writes them to the default)
	if (UOTGCourseSubsystem* C = Courses())
	{
		const FString Folder = C->CoursesFolder();
		const bool bDefault = Folder == UOTGCourseSubsystem::DefaultCoursesFolder();
		TSharedRef<SVerticalBox> W = SNew(SVerticalBox);
		W->AddSlot().AutoHeight()
		[
			SNew(SEditableTextBox).Text(FText::FromString(Folder)).Font(Font(13))
			.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type How) {
				if (How == ETextCommit::OnCleared) return;
				if (UOTGCourseSubsystem* S = Courses()) { S->SetCoursesFolder(T.ToString().TrimStartAndEnd()); CourseList = S->ListCourses(); }
				Rebuild();
			})
		];
		TSharedRef<SWrapBox> Btns = SNew(SWrapBox).UseAllottedSize(true);
		Btns->AddSlot().Padding(0, 6, 6, 0)[Button(TEXT("Open it"), [Folder]() { IFileManager::Get().MakeDirectory(*Folder, true); FPlatformProcess::ExploreFolder(*Folder); }, 13)];
		if (!bDefault)
			Btns->AddSlot().Padding(0, 6, 6, 0)[Button(TEXT("Back to the usual place"), [this]() { if (UOTGCourseSubsystem* S = Courses()) { S->SetCoursesFolder(FString()); CourseList = S->ListCourses(); } Rebuild(); }, 13)];
		W->AddSlot().AutoHeight()[Btns];
		W->AddSlot().AutoHeight().Padding(0, 4)[Text(TEXT("The web version's Export to Unreal puts courses in the usual place (Documents\\OpenTeeGolf\\Courses)."), 12, Soft)];
		Row2(TEXT("Courses folder"), W);
	}
	V->AddSlot().AutoHeight().Padding(0, 8)
	[
		SNew(SCheckBox).IsChecked_Lambda([GS]() { return GS->IsVSyncEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([GS, Apply](ECheckBoxState S) { GS->SetVSyncEnabled(S == ECheckBoxState::Checked); Apply(); })
		[Text(TEXT("Vertical sync (no tearing; caps the frame rate at the screen's)"), 14, Ink)]
	];
	return SNew(SScrollBox) + SScrollBox::Slot()[Box(V, FMargin(20, 16), Solid)];
}

void SOTGMainMenu::Start()
{
	UOTGCourseSubsystem* C = Courses();
	if (!C || Setup.CourseId.IsEmpty()) return;
	Remember();
	C->PendingSetup = Setup;
	C->bHasPendingSetup = true;
	C->PlayCourse(Setup.CourseId);
}
