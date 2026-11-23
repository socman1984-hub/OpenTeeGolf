// See SOTGLaunchMonitorPanel.h. (The web version's Mevo+ setup page: mevo-page.js.)

#include "UI/Menus/SOTGLaunchMonitorPanel.h"
#include "UI/Menus/SOTGMainMenu.h"
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
