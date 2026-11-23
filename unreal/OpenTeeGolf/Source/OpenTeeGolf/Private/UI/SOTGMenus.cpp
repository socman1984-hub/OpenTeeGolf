// See SOTGMenus.h. (The web version's menu: public/index.html and players-menu.js; its Mevo+ setup
// page: mevo-page.js.)

#include "UI/SOTGMenus.h"
#include "Round/OTGMiniGolf.h"
#include "UI/SOTGBuilder.h"
#include "UI/SOTGEditor.h"

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
	const FSpinBoxStyle& SpinStyle()
	{
		static const FSpinBoxStyle S = FSpinBoxStyle(FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox"))
			.SetBackgroundBrush(FSlateRoundedBoxBrush(FLinearColor(0, 0, 0, 0.35f), 7.f, FLinearColor(1, 1, 1, 0.14f), 1.f))
			.SetHoveredBackgroundBrush(FSlateRoundedBoxBrush(FLinearColor(0, 0, 0, 0.42f), 7.f, FLinearColor(1, 1, 1, 0.28f), 1.f))
			.SetActiveBackgroundBrush(FSlateRoundedBoxBrush(FLinearColor(0, 0, 0, 0.45f), 7.f, FLinearColor(0.45f, 0.95f, 0.52f, 0.8f), 1.f))
			.SetActiveFillBrush(FSlateRoundedBoxBrush(FLinearColor(0.3f, 0.75f, 0.4f, 0.35f), 7.f))
			.SetHoveredFillBrush(FSlateRoundedBoxBrush(FLinearColor(0.3f, 0.75f, 0.4f, 0.30f), 7.f))
			.SetInactiveFillBrush(FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, 0.06f), 7.f))
			.SetForegroundColor(Ink).SetTextPadding(FMargin(10, 6));
		return S;
	}
	TSharedRef<SWidget> Number(double Min, double Max, double Delta, TFunction<double()> Get, TFunction<void(double)> Set)
	{
		return SNew(SBox).WidthOverride(110)
			[
				SNew(SSpinBox<double>).Style(&SpinStyle()).MinValue(Min).MaxValue(Max).Delta(Delta)
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

	// ---------------------------------------------------------------- the main menu's look
	// (rounded glass cards, small capital labels, chips; everything else is OTGUi.h's)

	const FSlateBrush* CardBrush()
	{
		static const FSlateRoundedBoxBrush B(FLinearColor(0.018f, 0.03f, 0.022f, 0.90f), 12.f, FLinearColor(1, 1, 1, 0.07f), 1.f);
		return &B;
	}
	const FSlateBrush* InsetBrush()
	{
		static const FSlateRoundedBoxBrush B(FLinearColor(1, 1, 1, 0.045f), 8.f);
		return &B;
	}
	const FSlateBrush* PillBrush()
	{
		static const FSlateRoundedBoxBrush B(FLinearColor::White, 9.f);
		return &B;
	}
	const FLinearColor Dim(0.62f, 0.68f, 0.62f, 1.f);
	const FLinearColor Accent(0.40f, 0.86f, 0.48f, 1.f);

	TSharedRef<SBorder> Card(const TSharedRef<SWidget>& Content, FMargin Pad = FMargin(20, 16))
	{
		return SNew(SBorder).BorderImage(CardBrush()).Padding(Pad)[Content];
	}
	// a small spaced-out capitals label
	TSharedRef<STextBlock> Caps(const FString& S, int32 Size = 11, const FLinearColor& Colour = Dim)
	{
		FSlateFontInfo F = Font(Size, "Bold");
		F.LetterSpacing = 110;
		return SNew(STextBlock).Text(FText::FromString(S.ToUpper())).Font(F).ColorAndOpacity(Colour);
	}
	TSharedRef<STextBlock> Para(const FString& S, int32 Size = 14, const FLinearColor& Colour = Soft)
	{
		return SNew(STextBlock).Text(FText::FromString(S)).Font(Font(Size)).ColorAndOpacity(Colour).AutoWrapText(true);
	}
	TSharedRef<SWidget> Pill(const FString& S, const FLinearColor& Fill, const FLinearColor& Ink_ = Ink, int32 Size = 10)
	{
		return SNew(SBorder).BorderImage(PillBrush()).BorderBackgroundColor(Fill).Padding(FMargin(9, 3)).VAlign(VAlign_Center)[Caps(S, Size, Ink_)];
	}

	// the top tabs: words, the page shown underlined in the accent
	const FButtonStyle& TabStyle()
	{
		static const FButtonStyle S = FButtonStyle()
			.SetNormal(FSlateNoResource())
			.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, 0.06f), 6.f))
			.SetPressed(FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, 0.03f), 6.f))
			.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
		return S;
	}
	// a course's row in the list: faint, lighter under the mouse; the one picked green-edged
	const FButtonStyle& RowStyle(bool bOn)
	{
		static const FButtonStyle Off = FButtonStyle()
			.SetNormal(FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, 0.025f), 9.f))
			.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, 0.08f), 9.f, FLinearColor(1, 1, 1, 0.12f), 1.f))
			.SetPressed(FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, 0.05f), 9.f, FLinearColor(1, 1, 1, 0.12f), 1.f))
			.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
		static const FButtonStyle On = FButtonStyle()
			.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.13f, 0.34f, 0.18f, 0.80f), 9.f, FLinearColor(0.45f, 0.95f, 0.52f, 0.65f), 1.5f))
			.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.15f, 0.38f, 0.20f, 0.85f), 9.f, FLinearColor(0.5f, 1.f, 0.58f, 0.8f), 1.5f))
			.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.12f, 0.30f, 0.16f, 0.85f), 9.f, FLinearColor(0.5f, 1.f, 0.58f, 0.8f), 1.5f))
			.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
		return bOn ? On : Off;
	}
	// a choice: a rounded chip, the chosen one green (with a colour dot, for tees)
	TSharedRef<SWidget> Chip(const FString& Label, TFunction<void()> OnClick, bool bOn, const FLinearColor& Dot = FLinearColor::Transparent, int32 Size = 13)
	{
		TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
		if (Dot.A > 0)
			H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 7, 0)
			[
				SNew(SBox).WidthOverride(10).HeightOverride(10)[SNew(SBorder).BorderImage(PillBrush()).BorderBackgroundColor(Dot).Padding(0)]
			];
		H->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Label)).Font(Font(Size, bOn ? "Bold" : "Regular")).ColorAndOpacity(bOn ? Ink : Soft)];
		return SNew(SButton).ButtonStyle(&ButtonStyle(bOn)).ContentPadding(FMargin(12, 6))
			.OnClicked_Lambda([OnClick]() { if (OnClick) OnClick(); return FReply::Handled(); })[H];
	}
	TSharedRef<SWidget> Ghost(const FString& Label, TFunction<void()> OnClick, int32 Size = 12)
	{
		return SNew(SButton).ButtonStyle(&TabStyle()).ContentPadding(FMargin(10, 5))
			.OnClicked_Lambda([OnClick]() { if (OnClick) OnClick(); return FReply::Handled(); })
			[SNew(STextBlock).Text(FText::FromString(Label)).Font(Font(Size, "Bold")).ColorAndOpacity(Accent)];
	}
	// where players talk, and every release's notes (opened in the browser)
	const TCHAR* DiscordUrl = TEXT("https://discord.gg/YNsyWkCayd");
	const TCHAR* ReleasesUrl = TEXT("https://github.com/socman1984-hub/OpenTeeGolf/releases");
	void OpenUrl(const TCHAR* Url) { FPlatformProcess::LaunchURL(Url, nullptr, nullptr); }
	// the Discord button: its own blurple
	TSharedRef<SWidget> DiscordButton(int32 Size = 14)
	{
		static const FButtonStyle S = FButtonStyle()
			.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.10f, 0.12f, 0.80f, 0.95f), 7.f, FLinearColor(1, 1, 1, 0.15f), 1.f))
			.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.14f, 0.17f, 0.92f, 1.f), 7.f, FLinearColor(1, 1, 1, 0.35f), 1.f))
			.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.08f, 0.09f, 0.62f, 1.f), 7.f, FLinearColor(1, 1, 1, 0.35f), 1.f))
			.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0, 1, 0, 0));
		return SNew(SButton).ButtonStyle(&S).ContentPadding(FMargin(12, 6))
			.ToolTipText(FText::FromString(TEXT("Questions, problems, course requests and what's coming next: opens discord.gg/YNsyWkCayd in your browser")))
			.OnClicked_Lambda([]() { OpenUrl(DiscordUrl); return FReply::Handled(); })
			[SNew(STextBlock).Text(FText::FromString(TEXT("Join the Discord"))).Font(Font(Size, "Bold")).ColorAndOpacity(Ink)];
	}
	const FEditableTextBoxStyle& SearchStyle()
	{
		static const FEditableTextBoxStyle S = FEditableTextBoxStyle(FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
			.SetBackgroundImageNormal(FSlateRoundedBoxBrush(FLinearColor(0, 0, 0, 0.35f), 8.f, FLinearColor(1, 1, 1, 0.12f), 1.f))
			.SetBackgroundImageHovered(FSlateRoundedBoxBrush(FLinearColor(0, 0, 0, 0.40f), 8.f, FLinearColor(1, 1, 1, 0.25f), 1.f))
			.SetBackgroundImageFocused(FSlateRoundedBoxBrush(FLinearColor(0, 0, 0, 0.45f), 8.f, FLinearColor(0.45f, 0.95f, 0.52f, 0.8f), 1.5f))
			.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(FLinearColor(0, 0, 0, 0.35f), 8.f))
			.SetPadding(FMargin(12, 9))
			.SetForegroundColor(Ink).SetFocusedForegroundColor(Ink).SetBackgroundColor(FLinearColor::White);
		return S;
	}
	FLinearColor TeeColour(const FString& T)
	{
		if (T == TEXT("Black")) return FLinearColor(0.02f, 0.02f, 0.02f, 1.f);
		if (T == TEXT("Blue")) return FLinearColor(0.15f, 0.35f, 0.95f, 1.f);
		if (T == TEXT("White")) return FLinearColor(0.95f, 0.95f, 0.95f, 1.f);
		if (T == TEXT("Gold") || T == TEXT("Yellow")) return FLinearColor(1.f, 0.78f, 0.2f, 1.f);
		if (T == TEXT("Green")) return FLinearColor(0.2f, 0.7f, 0.3f, 1.f);
		if (T == TEXT("Red")) return FLinearColor(0.9f, 0.15f, 0.12f, 1.f);
		if (T == TEXT("Junior")) return FLinearColor(1.f, 0.5f, 0.15f, 1.f);
		return FLinearColor(0.6f, 0.6f, 0.6f, 1.f);
	}
	FString Ago(const FDateTime& Utc)
	{
		const FTimespan D = FDateTime::UtcNow() - Utc;
		if (D.GetTotalHours() < 1) return TEXT("Just now");
		if (D.GetTotalDays() < 1) return FString::Printf(TEXT("%d h ago"), int32(D.GetTotalHours()));
		if (D.GetTotalDays() < 2) return TEXT("Yesterday");
		if (D.GetTotalDays() < 31) return FString::Printf(TEXT("%d days ago"), int32(D.GetTotalDays()));
		return (Utc + (FDateTime::Now() - FDateTime::UtcNow())).ToString(TEXT("%d %b %Y"));
	}
	// the weather's short name ("Live (the course's weather now)": "Live")
	FString ShortName(const FString& S)
	{
		int32 At;
		return S.FindChar(TEXT('('), At) ? S.Left(At).TrimStartAndEnd() : S;
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
	Setup.Picked.RemoveAll([&](const FString& Id) { return !Profiles.ContainsByPredicate([&](const FOTGPlayerProfile& P) { return P.Id == Id; }); });
	if (UOTGCourseSubsystem* C = Courses()) CourseList = C->ListCourses();
	if (CourseList.Num() && !CourseList.ContainsByPredicate([&](const FOTGCourseInfo& I) { return I.Id == Setup.CourseId; })) Setup.CourseId = CourseList[0].Id;
	// (testing, even in a Shipping build, which has no console: -buildertest opens New course)
	FString BuilderTest;
	if (FParse::Value(FCommandLine::Get(), TEXT("buildertest="), BuilderTest)) Page = EPage::Build;
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
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.02f, 0.04f, 0.03f, 1.f)).Padding(0)
			[
				SNew(SScaleBox).Stretch(EStretch::ScaleToFill)[SAssignNew(Backdrop, SImage).ColorAndOpacity(FLinearColor(0.30f, 0.34f, 0.31f, 1.f))]
			]
		]
		+ SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.01f, 0.02f, 0.015f, 0.62f))]
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
	];
	Rebuild();
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
		if (X == S::Downloading || X == S::Ready) return EVisibility::Visible;
		if (X == S::Available) return bUpdateLater ? EVisibility::Collapsed : EVisibility::Visible;
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
	const TPair<FString, EPage> Pages[] = { { TEXT("Play"), EPage::Play }, { TEXT("Practice"), EPage::Practice }, { TEXT("New course"), EPage::Build }, { TEXT("Players"), EPage::Players }, { TEXT("Launch monitor"), EPage::LaunchMonitor }, { TEXT("Settings"), EPage::Settings }, { TEXT("What's new"), EPage::WhatsNew } };
	for (const auto& T : Pages)
	{
		const EPage P = T.Value;
		const bool bOn = Page == P || (P == EPage::Build && Page == EPage::Edit);
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
		}
		Body->SetContent(Builder.ToSharedRef());
	}
	else if (Page == EPage::Edit && Editor.IsValid()) Body->SetContent(Editor.ToSharedRef());
	else Body->SetContent(SNew(SScrollBox) + SScrollBox::Slot()[Box(SNew(SOTGLaunchMonitorPanel), FMargin(20, 16), Solid)]);
}

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

// ------------------------------------------------------------------ Practice

void SOTGMainMenu::SetPracticeMode(const FString& Name, TFunction<void()> Open)
{
	if (FPracticeMode* M = PracticeModes.FindByPredicate([&](const FPracticeMode& X) { return X.Name == Name; })) M->Open = MoveTemp(Open);
	else PracticeModes.Add({ Name, FString(), MoveTemp(Open) });
	if (Page == EPage::Practice) Rebuild();
}

TSharedRef<SWidget> SOTGMainMenu::PracticePage()
{
	// a simple picture for each: a range's distance boards, a green with its cup
	auto RangeArt = []() {
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		const TCHAR* Boards[] = { TEXT("250"), TEXT("200"), TEXT("150"), TEXT("100"), TEXT("50") };
		for (const TCHAR* B : Boards)
			V->AddSlot().FillHeight(1).VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SBox).HeightOverride(1)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.18f))]]
				+ SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)[Caps(B, 9, FLinearColor(1, 1, 1, 0.55f))]
			];
		return StaticCastSharedRef<SWidget>(V);
	};
	auto PuttArt = []() {
		return StaticCastSharedRef<SWidget>(SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(150).HeightOverride(110)
				[
					SNew(SBorder).BorderImage(PillBrush()).BorderBackgroundColor(FLinearColor(0.22f, 0.55f, 0.28f, 1.f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(18).HeightOverride(18)[SNew(SBorder).BorderImage(PillBrush()).BorderBackgroundColor(FLinearColor(0.02f, 0.03f, 0.02f, 1.f))]
					]
				]
			]);
	};
	static const FSlateRoundedBoxBrush ArtBrush(FLinearColor::White, 9.f);
	TSharedRef<SHorizontalBox> Cards = SNew(SHorizontalBox);
	for (int32 i = 0; i < PracticeModes.Num(); i++)
	{
		const FPracticeMode& M = PracticeModes[i];
		const bool bReady = bool(M.Open);
		const TFunction<void()> Open = M.Open;
		const FLinearColor Tint = i % 2 ? FLinearColor(0.10f, 0.30f, 0.30f, 1.f) : FLinearColor(0.12f, 0.32f, 0.16f, 1.f);
		Cards->AddSlot().AutoWidth().Padding(0, 0, 20, 0)
		[
			SNew(SBox).WidthOverride(440)
			[
				Card(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).HeightOverride(180)
						[
							SNew(SBorder).BorderImage(&ArtBrush).BorderBackgroundColor(Tint).Padding(FMargin(20, 14))
							[i % 2 ? PuttArt() : RangeArt()]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(M.Name, 22, Ink, "Bold")]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[bReady ? SNullWidget::NullWidget : Pill(TEXT("Coming soon"), FLinearColor(1.f, 0.82f, 0.35f, 0.18f), Gold)]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[SNew(SBox).MinDesiredHeight(60)[Para(M.About, 14)]]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 0).HAlign(HAlign_Left)
					[
						SNew(SBox).IsEnabled(bReady)
						[
							SNew(SButton).ButtonStyle(&ButtonStyle(bReady)).ContentPadding(FMargin(26, 10)).OnClicked_Lambda([Open]() { if (Open) Open(); return FReply::Handled(); })
							[Text(bReady ? TEXT("Start") : TEXT("Not ready yet"), 16, bReady ? Ink : Dim, "Bold")]
						]
					], FMargin(16, 16, 16, 20))
			]
		];
	}
	return SNew(SScrollBox) + SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Text(TEXT("Practice"), 30, Ink, "Bold")]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 22)[Para(TEXT("Warm up before a round, or work on one part of your game."), 15)]
			+ SVerticalBox::Slot().AutoHeight()[Cards]
		];
}

TSharedRef<SWidget> SOTGMainMenu::PlayersPage()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Text(TEXT("Players"), 22, Ink, "Bold")];
	const TArray<FLinearColor> Colours = FOTGPlayers::Colours();
	// (each player a card: name, handicap and remove on top; then each choice laid out in full - every
	// colour, both hands, every tee - the chosen one green, or ringed for a colour)
	static const FSlateRoundedBoxBrush Ring(FLinearColor::Transparent, 15.f, FLinearColor::White, 2.f);
	static const FSlateRoundedBoxBrush NoRing(FLinearColor::Transparent, 15.f, FLinearColor(1, 1, 1, 0.f), 2.f);
	auto Choice = [](const FString& Label, const TSharedRef<SWidget>& W) -> TSharedRef<SWidget> {
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(80)[Caps(Label)]]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[W];
	};
	for (int32 i = 0; i < Draft.Num(); i++)
	{
		FOTGPlayerProfile& P = Draft[i];
		int32 Played = P.Rounds.Num();
		// the colours: a swatch each
		TSharedRef<SWrapBox> Swatches = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4));
		for (int32 k = 0; k < Colours.Num(); k++)
		{
			const bool bOn = Colours[k].Equals(P.Colour, 0.01f);
			const FLinearColor C = Colours[k];
			Swatches->AddSlot()
			[
				SNew(SButton).ButtonStyle(&TabStyle()).ContentPadding(FMargin(0))
				.ToolTipText(FText::FromString(bOn ? TEXT("This player's colour") : TEXT("Use this colour")))
				.OnClicked_Lambda([this, i, C]() { if (Draft.IsValidIndex(i)) { Draft[i].Colour = C; Rebuild(); } return FReply::Handled(); })
				[
					SNew(SBorder).BorderImage(bOn ? &Ring : &NoRing).Padding(3)
					[
						SNew(SBox).WidthOverride(22).HeightOverride(22)[SNew(SBorder).BorderImage(PillBrush()).BorderBackgroundColor(C).Padding(0)]
					]
				]
			];
		}
		// the tees: the round's, or one of these
		TSharedRef<SWrapBox> Tees = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6, 6));
		for (int32 k = 0; k < UE_ARRAY_COUNT(PlayerTees); k++)
		{
			const FString T = PlayerTees[k];
			Tees->AddSlot()[Chip(T.IsEmpty() ? FString(TEXT("Round's tees")) : T, [this, i, T]() { if (Draft.IsValidIndex(i)) { Draft[i].Tee = T; Rebuild(); } }, P.Tee == T, T.IsEmpty() ? FLinearColor::Transparent : TeeColour(T))];
		}
		TSharedRef<SVerticalBox> CardBody = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
				[
					SNew(SBox).WidthOverride(14).HeightOverride(14)[SNew(SBorder).BorderImage(PillBrush()).BorderBackgroundColor(P.Colour).Padding(0)]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 14, 0)
				[
					SNew(SBox).WidthOverride(240)[SNew(SEditableTextBox).Style(&SearchStyle()).Text(FText::FromString(P.Name)).Font(Font(15))
						.OnTextChanged_Lambda([this, i](const FText& T) { if (Draft.IsValidIndex(i)) Draft[i].Name = T.ToString(); })]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Caps(TEXT("Handicap"))]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)
				[
					Number(-10, 54, 0.1, [this, i]() { return Draft.IsValidIndex(i) ? Draft[i].Handicap : 0.0; }, [this, i](double X) { if (Draft.IsValidIndex(i)) Draft[i].Handicap = FMath::RoundToDouble(X * 10) / 10; })
				]
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(Played ? FString::Printf(TEXT("%d round%s played"), Played, Played == 1 ? TEXT("") : TEXT("s")) : FString(), 12, Soft)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Button(ConfirmRemove == i ? TEXT("Remove (sure?)") : TEXT("Remove"), [this, i]() {
					if (ConfirmRemove != i) { ConfirmRemove = i; Rebuild(); return; }
					Draft.RemoveAt(i);
					ConfirmRemove = -1;
					Rebuild();
				}, 13)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[Choice(TEXT("Colour"), Swatches)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				Choice(TEXT("Hand"), SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Chip(TEXT("Right handed"), [this, i]() { if (Draft.IsValidIndex(i)) { Draft[i].bLeftHanded = false; Rebuild(); } }, !P.bLeftHanded)]
					+ SHorizontalBox::Slot().AutoWidth()[Chip(TEXT("Left handed"), [this, i]() { if (Draft.IsValidIndex(i)) { Draft[i].bLeftHanded = true; Rebuild(); } }, P.bLeftHanded)])
			]
			+ SVerticalBox::Slot().AutoHeight()[Choice(TEXT("Tees"), Tees)];
		V->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[SNew(SBorder).BorderImage(InsetBrush()).Padding(FMargin(16, 14))[CardBody]];
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

// The picked course's way into the editor: there when the helper has what it was built from
TSharedRef<SWidget> SOTGMainMenu::EditLink(const FOTGCourseInfo* Info)
{
	if (!Info || Info->ExportDir.IsEmpty()) return SNullWidget::NullWidget;
	UGameInstance* G = GameInstance();
	UOTGHelperSubsystem* H = G ? G->GetSubsystem<UOTGHelperSubsystem>() : nullptr;
	if (H && H->HasSource(Info->Id))
	{
		const FString Id = Info->Id;
		return Ghost(TEXT("Edit this course"), [this, Id]() { OpenEditor(Id); }, 13);
	}
	return SNew(SBox).ToolTipText(FText::FromString(TEXT("The editor works from what a course was built from (its OpenStreetMap data and photos), kept in Documents\\OpenTeeGolf\\CourseSources. Courses built in the game are there; this one was made elsewhere (the web version).")))
		[Para(TEXT("Made with the web version: edit it there"), 12, Dim)];
}

void SOTGMainMenu::OpenEditor(const FString& Id)
{
	SAssignNew(Editor, SOTGCourseEditor, Id);
	Editor->OnClose = [this](bool bExported) {
		if (bExported) if (UOTGCourseSubsystem* S = Courses()) CourseList = S->ListCourses();
		if (Builder.IsValid()) Builder->Refresh();
		Page = EPage::Build;
		Editor.Reset();
		Rebuild();
	};
	Page = EPage::Edit;
	Rebuild();
}

void SOTGMainMenu::ShowPage(const FString& Name)
{
	Page = Name == TEXT("players") ? EPage::Players : Name == TEXT("lm") ? EPage::LaunchMonitor : Name == TEXT("settings") ? EPage::Settings : Name == TEXT("build") ? EPage::Build : Name == TEXT("practice") ? EPage::Practice : Name == TEXT("news") ? EPage::WhatsNew : EPage::Play;
	if (Page == EPage::Players) { Draft = Profiles; ConfirmRemove = -1; }
	Rebuild();
}

// What's new: WHATS_NEW.md's versions, newest first, each a card ("## <version> - <date>", then its
// "- " lines); this version marked. (Next to OpenTeeGolf.exe in a release; in the repo, unreal/release.)
TSharedRef<SWidget> SOTGMainMenu::WhatsNewPage()
{
	struct FVersionNotes { FString Version, Date; TArray<FString> Lines; };
	TArray<FVersionNotes> Versions;
	{
		const FString Candidates[] = {
			FPaths::RootDir() / TEXT("WHATS_NEW.md"),
			FPaths::ProjectDir() / TEXT("../release/WHATS_NEW.md"),
		};
		TArray<FString> Lines;
		for (const FString& Path : Candidates)
			if (FFileHelper::LoadFileToStringArray(Lines, *FPaths::ConvertRelativePathToFull(Path))) break;
		for (const FString& L : Lines)
		{
			if (L.StartsWith(TEXT("## ")))
			{
				FVersionNotes& V = Versions.AddDefaulted_GetRef();
				if (!L.Mid(3).Split(TEXT(" - "), &V.Version, &V.Date)) V.Version = L.Mid(3);
				V.Version.TrimStartAndEndInline();
				V.Date.TrimStartAndEndInline();
			}
			else if (Versions.Num() && L.StartsWith(TEXT("- "))) Versions.Last().Lines.Add(L.Mid(2).TrimStartAndEnd());
		}
	}
	const FString Mine = GetDefault<UGeneralProjectSettings>()->ProjectVersion;

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	if (Versions.IsEmpty())
		List->AddSlot().AutoHeight()[Card(Para(TEXT("The release notes aren't with this copy of the game. Every version's are on GitHub: \"All release notes\" above.")))];
	for (const FVersionNotes& V : Versions)
	{
		TSharedRef<SVerticalBox> C = SNew(SVerticalBox);
		TSharedRef<SHorizontalBox> Head = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[Text(TEXT("Version ") + V.Version, 18, Ink, "Bold")]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[Text(V.Date, 13, Dim)];
		if (V.Version == Mine)
			Head->AddSlot().AutoWidth().VAlign(VAlign_Center)[Pill(TEXT("You have this"), FLinearColor(0.20f, 0.52f, 0.26f, 0.95f))];
		C->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Head];
		for (const FString& L : V.Lines)
			C->AddSlot().AutoHeight().Padding(0, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 10, 0)[Text(TEXT("•"), 14, Accent, "Bold")]
				+ SHorizontalBox::Slot().FillWidth(1)[Para(L)]
			];
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 12)[Card(C)];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Text(TEXT("What's new"), 22, Ink, "Bold")]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)[Para(FString::Printf(TEXT("You have version %s. The game updates itself: it checks when it starts."), *Mine), 13, Dim)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[Button(TEXT("All release notes"), []() { OpenUrl(ReleasesUrl); }, 14)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[DiscordButton()]
		]
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot()[List]];
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
			W->AddSlot().Padding(0, 0, 6, 6)[Button(Names[i], [GS, i, Apply]() {
				// (a quality level sets the render resolution to 100% too: kept as chosen)
				float N, Keep, Lo, Hi;
				GS->GetResolutionScaleInformationEx(N, Keep, Lo, Hi);
				GS->SetOverallScalabilityLevel(i);
				GS->SetResolutionScaleValueEx(Keep);
				Apply();
			}, 14, Level == i)];
		W->AddSlot().Padding(8, 0, 6, 6)[Button(TEXT("Pick for this PC"), [GS, Apply]() { GS->RunHardwareBenchmark(); GS->ApplyHardwareBenchmarkResults(); Apply(); }, 13)];
		Row2(TEXT("Graphics quality"), W);
		V->AddSlot().AutoHeight().Padding(0, 2)[Text(TEXT("Lower it if the game runs slowly: the grass, trees and shadows cost the most."), 12, Soft)];
	}
	// render resolution: the picture drawn smaller and sharpened up to the screen (TSR) - the
	// biggest single saving, most of the cost is per pixel
	{
		TSharedRef<SWrapBox> W = SNew(SWrapBox).UseAllottedSize(true);
		float N, Now, Lo, Hi;
		GS->GetResolutionScaleInformationEx(N, Now, Lo, Hi);
		const TPair<int32, const TCHAR*> Scales[] = { { 100, TEXT("Native (100%)") }, { 85, TEXT("85%") }, { 75, TEXT("75%") }, { 67, TEXT("67%") } };
		for (const auto& Sc : Scales)
		{
			const int32 Pct = Sc.Key;
			W->AddSlot().Padding(0, 0, 6, 6)[Button(Sc.Value, [GS, Pct, Apply]() { GS->SetResolutionScaleValueEx(float(Pct)); Apply(); }, 14, FMath::RoundToInt(Now) == Pct)];
		}
		Row2(TEXT("Render resolution"), W);
		V->AddSlot().AutoHeight().Padding(0, 2)[Text(TEXT("Below 100% the game draws fewer pixels and sharpens them up to your screen: much faster, a little softer. 75% suits most PCs."), 12, Soft)];
	}
	// updates: where things stand, and a button to look now
	{
		TSharedRef<SVerticalBox> W = SNew(SVerticalBox);
		W->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Font(Font(14)).ColorAndOpacity(Ink).AutoWrapText(true)
			.Text_Lambda([this]() { UOTGUpdateSubsystem* U = Updater(); return FText::FromString(U ? U->StatusLine() : FString(TEXT("Updates aren't available here."))); })
		];
		W->AddSlot().AutoHeight().Padding(0, 6, 0, 0).HAlign(HAlign_Left)
		[
			Button(TEXT("Check for updates"), [this]() { bUpdateLater = false; if (UOTGUpdateSubsystem* U = Updater()) U->Check(true); }, 13)
		];
		W->AddSlot().AutoHeight().Padding(0, 4)[Text(TEXT("The game checks each time it starts and downloads only the files that changed."), 12, Soft)];
		Row2(TEXT("Updates"), W);
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
	Setup.LastPlayed.Add(Setup.CourseId, FDateTime::UtcNow());
	Remember();
	C->PendingSetup = Setup;
	C->bHasPendingSetup = true;
	C->PlayCourse(Setup.CourseId);
}
