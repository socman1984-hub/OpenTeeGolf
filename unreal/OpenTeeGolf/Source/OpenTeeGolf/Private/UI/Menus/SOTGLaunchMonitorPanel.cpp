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
	LiveBrush = MakeShared<FSlateBrush>();
	LiveBrush->DrawAs = ESlateBrushDrawType::Image;
	LiveBrush->ImageSize = FVector2D(1024, 768);
	ChildSlot[SAssignNew(Body, SBox).WidthOverride(560)];
	Rebuild();
}

SOTGLaunchMonitorPanel::~SOTGLaunchMonitorPanel()
{
	// (the camera only streams while someone's looking)
	if (UOTGLaunchMonitorSubsystem* L = LM(); L && L->IsMevoLiveViewOn()) L->SetMevoLiveView(false);
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
	// the Mevo+ camera: live view for lining up the ball (armed or not)
	bShownLive = L->IsMevoLiveViewOn();
	LiveText.Reset();
	if (C == EOTGConnection::Direct && !L->IsDeviceOff())
	{
		V->AddSlot().AutoHeight().Padding(0, 4, 0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Heading(TEXT("Camera"))]
			+ SHorizontalBox::Slot().AutoWidth()[Button(bShownLive ? TEXT("Stop live view") : TEXT("Start live view"), [this]() {
				if (UOTGLaunchMonitorSubsystem* X = LM()) X->SetMevoLiveView(!X->IsMevoLiveViewOn());
				Rebuild();
			}, 13, bShownLive)]
		];
		if (bShownLive)
		{
			// The picture is drawn 520 x 390 (the camera's 1024 x 768); the guide is mevo.html's: the
			// centre line and the "ball here" box at (432, 470), 160 square, in the camera's pixels
			static const FSlateRoundedBoxBrush GuideBox(FLinearColor::Transparent, 5.f, FLinearColor(1.f, 0.82f, 0.35f, 0.9f), 2.f);
			const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
			const float Sx = 520.f / 1024.f, Sy = 390.f / 768.f;
			V->AddSlot().AutoHeight().Padding(0, 2, 0, 4)
			[
				SNew(SBox).WidthOverride(520).HeightOverride(390)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0, 0, 0, 0.6f))]
					+ SOverlay::Slot()[SNew(SImage).Image(LiveBrush.Get()).Visibility_Lambda([this]() { return LiveTex.IsValid() ? EVisibility::HitTestInvisible : EVisibility::Hidden; })]
					+ SOverlay::Slot().HAlign(HAlign_Center)[SNew(SBox).WidthOverride(2)[SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(1.f, 0.82f, 0.35f, 0.6f))]]
					+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(432 * Sx, 470 * Sy, 0, 0)
					[
						SNew(SBox).WidthOverride(160 * Sx).HeightOverride(160 * Sy)[SNew(SBorder).BorderImage(&GuideBox)]
					]
					+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(432 * Sx, 470 * Sy + 160 * Sy + 2, 0, 0)
					[
						SNew(SBox).WidthOverride(160 * Sx)[SNew(STextBlock).Text(FText::FromString(TEXT("ball here"))).Font(Font(11)).ColorAndOpacity(Gold).Justification(ETextJustify::Center)]
					]
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(16)
					[
						SAssignNew(LiveText, STextBlock).Font(Font(14)).ColorAndOpacity(Ink).AutoWrapText(true).Justification(ETextJustify::Center)
					]
				]
			];
			V->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[SNew(STextBlock).Text(FText::FromString(TEXT("Place the ball inside the box, on the centre line, and aim the Mevo+ straight down your target line. The box is a guide only.")))
				.Font(Font(12)).ColorAndOpacity(Soft).AutoWrapText(true)];
		}
	}
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
		V->AddSlot().AutoHeight().Padding(0, 4)
		[
			SNew(SCheckBox).IsChecked_Lambda([this]() { return bArmOnTurn ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bArmOnTurn = S == ECheckBoxState::Checked; if (UOTGLaunchMonitorSubsystem* X = LM()) X->SetArmOnTurn(bArmOnTurn); })
			[Text(TEXT("Arm only on my turn (stays quiet between shots)"), 14, Ink)]
		];
		V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
		[
			SNew(SCheckBox).IsChecked_Lambda([this]() { return Draft.bFast ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { Draft.bFast = S == ECheckBoxState::Checked; })
			[Text(TEXT("Fast shots (full swings start ~0.15 s after impact)"), 14, Ink)]
		];
		V->AddSlot().AutoHeight().Padding(24, 0, 0, 4)[SNew(STextBlock).Text(FText::FromString(TEXT("From the Mevo+'s first reading: speed and angles are final, spin is a first estimate, corrected in flight when the full reading arrives. Chips and putts wait for the full reading. Save the setup to apply.")))
			.Font(Font(12)).ColorAndOpacity(Soft).AutoWrapText(true)];
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Fast shots: wait up to (s)"), Number(0, 3, 0.1, [this]() { return Draft.FastWait; }, [this](double X) { Draft.FastWait = X; }))];
		V->AddSlot().AutoHeight().Padding(24, 0, 0, 4)[SNew(STextBlock).Text(FText::FromString(TEXT("How long to wait for the full reading before starting with the first one (0-3 s). When the Mevo+ reads the spin it comes about 0.4 s after the first; when it can't, about 1.6 s. So 0 is fastest, about 0.6 waits for a measured spin, and 1.8 or more is the same as fast shots off. If it comes sooner, the ball takes off with it straight away.")))
			.Font(Font(12)).ColorAndOpacity(Soft).AutoWrapText(true)];
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

void SOTGLaunchMonitorPanel::UpdateLiveView()
{
	UOTGLaunchMonitorSubsystem* L = LM();
	if (!L || !L->IsMevoLiveViewOn())
	{
		if (LiveTex.IsValid())
		{
			LiveBrush->SetResourceObject(nullptr);
			LiveTex.Reset();
		}
		LiveSerial = 0;
		return;
	}
	if (LiveText) LiveText->SetText(FText::FromString(L->GetMevoLiveViewText()));
	int32 W = 0, H = 0;
	if (!L->TakeMevoLiveViewFrame(LiveSerial, LivePixels, W, H) || W <= 0 || H <= 0 || LivePixels.Num() < int64(W) * H * 4) return;
	if (!LiveTex.IsValid() || LiveTex->GetSizeX() != W || LiveTex->GetSizeY() != H)
	{
		UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (!T) return;
		LiveTex.Reset(T);
		LiveBrush->SetResourceObject(T);
		LiveBrush->ImageSize = FVector2D(W, H);
	}
	UTexture2D* T = LiveTex.Get();
	void* Data = T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, LivePixels.GetData(), int64(W) * H * 4);
	T->GetPlatformData()->Mips[0].BulkData.Unlock();
	T->UpdateResource();
}

void SOTGLaunchMonitorPanel::Tick(const FGeometry& Geometry, const double Time, const float Dt)
{
	SCompoundWidget::Tick(Geometry, Time, Dt);
	UOTGLaunchMonitorSubsystem* L = LM();
	if (!L || !StatusText) return;
	if (L->GetConnection() != Shown || L->IsDeviceOff() != bShownOff || L->IsMevoLiveViewOn() != bShownLive) Rebuild();
	UpdateLiveView();
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
