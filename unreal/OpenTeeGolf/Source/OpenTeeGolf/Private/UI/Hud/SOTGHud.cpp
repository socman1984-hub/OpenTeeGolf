// See SOTGHud.h.

#include "UI/Hud/SOTGHud.h"
#include "Widgets/Images/SImage.h"

#include "UI/OTGUi.h"
#include "UI/Menus/SOTGLaunchMonitorPanel.h"
#include "Minimap/SOTGMinimap.h"
#include "UI/Hud/SOTGPanels.h"
#include "UI/Designer/SOTGCourseDesigner.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "RenderCore.h"
#include "RHI.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SCheckBox.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "App/OTGPrefs.h"

using namespace otg::ui;

void SOTGHud::Construct(const FArguments&)
{
	ChildSlot
	[
		SNew(SOverlay)
		// the hole, the lie, the club
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(24)
		[
			Box(SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)[SAssignNew(CourseText, STextBlock).Font(Font(13, "Bold")).ColorAndOpacity(Gold).Visibility(EVisibility::Collapsed)]
				+ SVerticalBox::Slot().AutoHeight()[SAssignNew(Info1, STextBlock).Font(Font(22, "Bold")).ColorAndOpacity(Ink)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)[SAssignNew(Info2, STextBlock).Font(Font(15)).ColorAndOpacity(Soft)]
				// (the lie: what the ball sits on, in its colour)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					SAssignNew(LieRow, SBox).Visibility(EVisibility::Collapsed)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Text(TEXT("LIE"), 12, Soft, "Bold")]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 7, 0)
						[
							SNew(SBox).WidthOverride(14).HeightOverride(14)[SAssignNew(LieDot, SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SAssignNew(LieText, STextBlock).Font(Font(17, "Bold")).ColorAndOpacity(Ink)]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[SAssignNew(ClubText, STextBlock).Font(Font(16, "Bold")).ColorAndOpacity(Gold)]
				// (players together: who's playing, whoever is up ringed - SetPlayers)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[SAssignNew(PlayerList, SVerticalBox).Visibility(EVisibility::Collapsed)])
		]
		// the pin's yardage and height, over the flag (placed each frame: SetPinTag)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)
		[
			SAssignNew(PinTag, SBox).Visibility(EVisibility::Collapsed)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.03f, 0.06f, 0.04f, 0.78f)).Padding(FMargin(10, 4))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SAssignNew(PinYards, STextBlock).Font(Font(18, "Bold")).ColorAndOpacity(Ink)]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SAssignNew(PinElev, STextBlock).Font(Font(13, "Bold")).ColorAndOpacity(Soft)]
				]
			]
		]
		// the shot data's tiles (where the player put them: TilePlace)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(24, 24, 24, 64).Expose(TilePlace)
		[
			SAssignNew(TileSlot, SBox).Visibility(EVisibility::Collapsed)
		]
		// which of them show, and where (D)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SAssignNew(ShotSettingsSlot, SBox).Visibility(EVisibility::Collapsed)
		]
		// on the right: who's winning (players together), the wind, the minimap
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Fill).Padding(24)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, 0, 0, 8)
			[
				SAssignNew(StandBox, SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(FMargin(14, 8)).Visibility(EVisibility::Collapsed)
				[
					SAssignNew(StandList, SVerticalBox)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, 0, 0, 8)[SAssignNew(WindBox, SOTGWind)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[SAssignNew(MapBox, SBox).WidthOverride(250).HeightOverride(360)[SAssignNew(Map, SOTGMinimap)]]
			// (the shot data's tiles, put on the right: under the minimap, never behind it - a second
			// column beside the first when they run out of room above the keys)
			+ SVerticalBox::Slot().FillHeight(1).HAlign(HAlign_Right).Padding(0, 8, 0, 40)[SAssignNew(SideSlot, SBox).Visibility(EVisibility::Collapsed)]
		]
		// the weather panel (L)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SAssignNew(WxPanel, SOTGWeatherPanel).Visibility(EVisibility::Collapsed)
		]
		// the course designer (R: the grass, the trees and rocks), on the left so the course shows beside it
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(24, 60, 24, 24)
		[
			SAssignNew(DesignPanel, SOTGCourseDesigner).Visibility(EVisibility::Collapsed)
		]
		// the frame rate and timings (P)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 24, 0, 0)
		[
			SAssignNew(PerfBox, SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(FMargin(12, 5)).Visibility(EVisibility::Collapsed)
			[
				SAssignNew(PerfText, STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Mono", 12)).ColorAndOpacity(Ink)
			]
		]
		// the test shot panel (T)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(24, 60, 24, 24)
		[
			SAssignNew(Shots, SOTGShotPanel).Visibility(EVisibility::Collapsed)
		]
		// the shot data
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(24, 24, 24, 48)
		[
			SAssignNew(ShotBox, SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(FMargin(18, 8)).Visibility(EVisibility::Collapsed)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SAssignNew(Shot1, STextBlock).Font(Font(17, "Bold")).ColorAndOpacity(Ink)]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 3, 0, 0)[SAssignNew(Shot2, STextBlock).Font(Font(15)).ColorAndOpacity(Gold)]
			]
		]
		// the keys and the launch monitor, one row along the bottom: the keys wrap onto a second line
		// rather than run under the launch monitor's status when the window is narrower
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(24)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Bottom).Padding(0, 0, 16, 0)
			[
				SAssignNew(Keys, STextBlock).Font(Font(12)).ColorAndOpacity(FLinearColor(1, 1, 1, 0.7f)).ShadowOffset(FVector2D(1, 1)).AutoWrapText(true)
				.Text(FText::FromString(TEXT("Click the ground or the map to aim     Left / Right: fine aim     Up / Down: club     T: test shot panel     Space: quick test shot     Enter: skip / continue     Tab: scorecard     Backspace: mulligan     F: free view     G: slope grid     L: weather     R: designer     D: shot data     P: frame rate     Esc: menu")))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
			[
				Box(SAssignNew(Status, STextBlock).Font(Font(13)).ColorAndOpacity(Soft), FMargin(10, 5))
			]
		]
		// banners and toasts
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 140, 0, 0)
		[
			SAssignNew(BannerBox, SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(FMargin(36, 14)).Visibility(EVisibility::Collapsed)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SAssignNew(BannerTitle, STextBlock).Font(Font(34, "Bold")).ColorAndOpacity(Ink)]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 0)[SAssignNew(BannerSmall, STextBlock).Font(Font(16)).ColorAndOpacity(Soft).Justification(ETextJustify::Center)]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 260, 0, 0) // (above the ball, which sits low in the view)
		[
			SAssignNew(ToastBox, SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Panel).Padding(FMargin(16, 7)).Visibility(EVisibility::Collapsed)
			[
				SAssignNew(ToastText, STextBlock).Font(Font(16)).ColorAndOpacity(Ink)
			]
		]
		// the scorecard
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SAssignNew(CardSlot, SBox).Visibility(EVisibility::Collapsed)
		]
		// the pause menu
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SAssignNew(PauseSlot, SBox).Visibility(EVisibility::Collapsed)
		]
	];
	LoadShotSettings();
}

// ------------------------------------------------------------------ the course, the lie, the pin

void SOTGHud::SetCourseName(const FString& Name)
{
	CourseText->SetText(FText::FromString(Name.ToUpper()));
	CourseText->SetVisibility(Name.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
}

void SOTGHud::SetLie(const FString& Lie, const FLinearColor& Colour)
{
	LieRow->SetVisibility(Lie.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
	LieText->SetText(FText::FromString(Lie));
	LieDot->SetBorderBackgroundColor(Colour);
}

void SOTGHud::SetPinTag(bool bShow, const FVector2D& Screen, const FString& Yards, const FString& Elevation, const FLinearColor& ElevColour)
{
	PinTag->SetVisibility(bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	if (!bShow) return;
	PinYards->SetText(FText::FromString(Yards));
	PinElev->SetText(FText::FromString(Elevation));
	PinElev->SetColorAndOpacity(ElevColour);
	PinElev->SetVisibility(Elevation.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
	// (centred over the point, its bottom just above it)
	const FVector2D Sz = PinTag->GetDesiredSize();
	PinTag->SetRenderTransform(FSlateRenderTransform(FVector2f(Screen.X - Sz.X / 2, Screen.Y - Sz.Y - 6)));
}

// ------------------------------------------------------------------ the shot data

const TArray<SOTGHud::FShotItem>& SOTGHud::ShotItems()
{
	static const TArray<FShotItem> L = {
		{ TEXT("ballspeed"), TEXT("Ball speed"), true },
		{ TEXT("clubspeed"), TEXT("Club speed"), true },
		{ TEXT("smash"), TEXT("Smash"), true },
		{ TEXT("launch"), TEXT("Launch"), true },
		{ TEXT("direction"), TEXT("Direction"), true },
		{ TEXT("spin"), TEXT("Spin"), true },
		{ TEXT("backspin"), TEXT("Back spin"), false },
		{ TEXT("sidespin"), TEXT("Side spin"), false },
		{ TEXT("axis"), TEXT("Spin axis"), true },
		{ TEXT("aoa"), TEXT("Attack angle"), false },
		{ TEXT("path"), TEXT("Club path"), false },
		{ TEXT("face"), TEXT("Face to target"), false },
		{ TEXT("loft"), TEXT("Dynamic loft"), false },
		{ TEXT("carry"), TEXT("Carry"), true },
		{ TEXT("total"), TEXT("Total"), true },
		{ TEXT("offline"), TEXT("Offline"), true },
		{ TEXT("apex"), TEXT("Height"), true },
	};
	return L;
}

const TArray<FString>& SOTGHud::ShotPlaces()
{
	static const TArray<FString> L = { TEXT("bottom"), TEXT("top"), TEXT("left"), TEXT("right"), TEXT("bottomleft"), TEXT("bottomright") };
	return L;
}

namespace
{
	const TCHAR* ShotSection = TEXT("OpenTeeGolf.ShotData");
	FString PlaceLabel(const FString& P)
	{
		return P == TEXT("top") ? TEXT("Top") : P == TEXT("left") ? TEXT("Left side") : P == TEXT("right") ? TEXT("Right side")
			: P == TEXT("bottomleft") ? TEXT("Bottom left") : P == TEXT("bottomright") ? TEXT("Bottom right") : TEXT("Bottom");
	}
}

void SOTGHud::LoadShotSettings()
{
	ShotShown.Reset();
	FString Shown;
	if (GConfig && GConfig->GetString(ShotSection, TEXT("Shown"), Shown, GGameUserSettingsIni))
	{
		TArray<FString> L;
		Shown.ParseIntoArray(L, TEXT(","));
		for (const FString& S : L) ShotShown.Add(S.TrimStartAndEnd());
		ShotShown.Remove(TEXT("none"));
	}
	else for (const FShotItem& I : ShotItems()) if (I.bDefault) ShotShown.Add(I.Id);
	FString Where;
	if (GConfig && GConfig->GetString(ShotSection, TEXT("Where"), Where, GGameUserSettingsIni) && ShotPlaces().Contains(Where)) ShotWhere = Where;
}

void SOTGHud::SaveShotSettings() const
{
	if (!GConfig) return;
	TArray<FString> L;
	for (const FShotItem& I : ShotItems()) if (ShotShown.Contains(I.Id)) L.Add(I.Id);
	GConfig->SetString(ShotSection, TEXT("Shown"), L.Num() ? *FString::Join(L, TEXT(",")) : TEXT("none"), GGameUserSettingsIni);
	GConfig->SetString(ShotSection, TEXT("Where"), *ShotWhere, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void SOTGHud::SetShotValues(const TMap<FString, FString>& Values)
{
	ShotValues = Values;
	bSample = false;
	BuildTiles();
}

void SOTGHud::BuildTiles()
{
	if (!TilePlace) return;
	const bool bSide = ShotWhere == TEXT("left") || ShotWhere == TEXT("right");
	// where: along the bottom (above the keys) or the top (under the hole's box... beside it), or down a side
	const FString& W = ShotWhere;
	TilePlace->SetHorizontalAlignment(W == TEXT("left") || W == TEXT("bottomleft") ? HAlign_Left : W == TEXT("right") || W == TEXT("bottomright") ? HAlign_Right : HAlign_Center);
	TilePlace->SetVerticalAlignment(W == TEXT("top") ? VAlign_Top : W == TEXT("left") ? VAlign_Center : VAlign_Bottom);
	TilePlace->SetPadding(W == TEXT("top") ? FMargin(340, 24, 300, 24) : W == TEXT("left") ? FMargin(24, 200, 24, 64)
		: W == TEXT("right") ? FMargin(24, 24, 24, 64) : FMargin(24, 24, 24, 64));
	TSharedRef<SWidget> Tiles = SNullWidget::NullWidget;
	auto Tile = [](const FString& Label, const FString& Value) {
		return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.03f, 0.06f, 0.04f, 0.8f)).Padding(FMargin(12, 5))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Text(Label.ToUpper(), 11, Soft, "Bold")]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Text(Value.IsEmpty() ? FString(TEXT("–")) : Value, 19, Value.IsEmpty() ? Soft : Ink, "Bold")]
		];
	};
	int32 N = 0;
	if (W == TEXT("right"))
	{
		TSharedRef<SWrapBox> V = SNew(SWrapBox).Orientation(Orient_Vertical).UseAllottedSize(true).InnerSlotPadding(FVector2D(4, 4));
		for (const FShotItem& I : ShotItems())
			if (ShotShown.Contains(I.Id) && ShotValues.Contains(I.Id)) { V->AddSlot()[SNew(SBox).MinDesiredWidth(130)[Tile(I.Label, ShotValues[I.Id])]]; N++; }
		SideSlot->SetContent(V);
		SideSlot->SetVisibility(N ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		TileSlot->SetContent(SNullWidget::NullWidget);
		TileSlot->SetVisibility(EVisibility::Collapsed);
		return;
	}
	SideSlot->SetContent(SNullWidget::NullWidget);
	SideSlot->SetVisibility(EVisibility::Collapsed);
	if (bSide)
	{
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		for (const FShotItem& I : ShotItems())
			if (ShotShown.Contains(I.Id) && ShotValues.Contains(I.Id)) { V->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[SNew(SBox).MinDesiredWidth(130)[Tile(I.Label, ShotValues[I.Id])]]; N++; }
		Tiles = V;
	}
	else
	{
		TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
		for (const FShotItem& I : ShotItems())
			if (ShotShown.Contains(I.Id) && ShotValues.Contains(I.Id)) { H->AddSlot().AutoWidth().Padding(0, 0, 4, 0)[SNew(SBox).MinDesiredWidth(92)[Tile(I.Label, ShotValues[I.Id])]]; N++; }
		Tiles = H;
	}
	TileSlot->SetContent(Tiles);
	TileSlot->SetVisibility(N ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
}

void SOTGHud::BuildShotSettings()
{
	TSharedRef<SVerticalBox> Items = SNew(SVerticalBox);
	// (two columns of switches)
	TSharedRef<SHorizontalBox> Cols = SNew(SHorizontalBox);
	TSharedRef<SVerticalBox> C1 = SNew(SVerticalBox), C2 = SNew(SVerticalBox);
	const int32 Half = (ShotItems().Num() + 1) / 2;
	for (int32 k = 0; k < ShotItems().Num(); k++)
	{
		const FShotItem& I = ShotItems()[k];
		const FString Id = I.Id;
		const bool bOn = ShotShown.Contains(Id);
		(k < Half ? C1 : C2)->AddSlot().AutoHeight().Padding(0, 2)
		[
			Check(I.Label, bOn, [this, Id](bool b) {
				if (b) ShotShown.Add(Id); else ShotShown.Remove(Id);
				SaveShotSettings(); BuildTiles(); BuildShotSettings();
			}, 14)
		];
	}
	Cols->AddSlot().AutoWidth().Padding(0, 0, 10, 0)[C1];
	Cols->AddSlot().AutoWidth()[C2];
	TSharedRef<SHorizontalBox> Places = SNew(SHorizontalBox);
	for (const FString& P : ShotPlaces())
		Places->AddSlot().AutoWidth().Padding(0, 0, 4, 0)[Button(PlaceLabel(P), [this, P]() { ShotWhere = P; SaveShotSettings(); BuildTiles(); BuildShotSettings(); }, 13, ShotWhere == P)];
	ShotSettingsSlot->SetContent(
		Box(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1)[Text(TEXT("Shot data"), 22, Ink, "Bold")]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Button(TEXT("Close (D)"), [this]() { ShowShotSettings(false); }, 13)]]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 10)[Text(TEXT("The numbers shown after each shot. Ticked ones show; club numbers need a launch monitor that measures the club."), 13, Soft)]
			+ SVerticalBox::Slot().AutoHeight()[Cols]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 4)[Text(TEXT("Where on the screen"), 15, Gold, "Bold")]
			+ SVerticalBox::Slot().AutoHeight()[Places]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)[SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Show all"), [this]() { for (const FShotItem& I : ShotItems()) ShotShown.Add(I.Id); SaveShotSettings(); BuildTiles(); BuildShotSettings(); }, 13)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Hide all"), [this]() { ShotShown.Reset(); SaveShotSettings(); BuildTiles(); BuildShotSettings(); }, 13)]
				+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("As it was"), [this]() { ShotShown.Reset(); for (const FShotItem& I : ShotItems()) if (I.bDefault) ShotShown.Add(I.Id); ShotWhere = TEXT("bottom"); SaveShotSettings(); BuildTiles(); BuildShotSettings(); }, 13)]],
			FMargin(24, 18), Solid));
}

void SOTGHud::ShowShotSettings(bool bShow)
{
	if (bShow)
	{
		// (a sample until there's a shot, so the choice can be seen)
		if (!ShotValues.Num())
		{
			ShotValues = { { TEXT("ballspeed"), TEXT("148.2 mph") }, { TEXT("clubspeed"), TEXT("101.4 mph") }, { TEXT("smash"), TEXT("1.46") }, { TEXT("launch"), TEXT("12.1°") },
				{ TEXT("direction"), TEXT("R1.2°") }, { TEXT("spin"), TEXT("2,650 rpm") }, { TEXT("backspin"), TEXT("2,610 rpm") }, { TEXT("sidespin"), TEXT("460 rpm R") },
				{ TEXT("axis"), TEXT("R4.1°") }, { TEXT("aoa"), TEXT("2.0° up") }, { TEXT("path"), TEXT("R1.5°") }, { TEXT("face"), TEXT("R0.4°") }, { TEXT("loft"), TEXT("14.2°") },
				{ TEXT("carry"), TEXT("246 yd") }, { TEXT("total"), TEXT("271 yd") }, { TEXT("offline"), TEXT("9 yd R") }, { TEXT("apex"), TEXT("94 ft") } };
			bSample = true;
			BuildTiles();
		}
		BuildShotSettings();
	}
	else if (bSample) { ShotValues.Reset(); bSample = false; BuildTiles(); }
	ShotSettingsSlot->SetVisibility(bShow ? EVisibility::Visible : EVisibility::Collapsed);
}

bool SOTGHud::IsShotSettingsShown() const
{
	return ShotSettingsSlot->GetVisibility() == EVisibility::Visible;
}

void SOTGHud::Tick(const FGeometry& Geometry, const double Time, const float Dt)
{
	SCompoundWidget::Tick(Geometry, Time, Dt);
	auto Fade = [Dt](float& Left, const TSharedPtr<SBorder>& B) {
		if (Left <= 0) return;
		Left -= Dt;
		B->SetRenderOpacity(FMath::Clamp(Left / 0.4f, 0.f, 1.f));
		if (Left <= 0) B->SetVisibility(EVisibility::Collapsed);
	};
	Fade(BannerLeft, BannerBox);
	Fade(ToastLeft, ToastBox);
	// the frame rate and where the time goes (twice a second)
	if (PerfBox->GetVisibility() != EVisibility::Collapsed && Time - PerfShown > 0.5)
	{
		PerfShown = Time;
		extern ENGINE_API float GAverageFPS;
		extern ENGINE_API float GAverageMS;
		const double GameMs = FPlatformTime::ToMilliseconds(GGameThreadTime), RenderMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
		const double GpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
		FString Quality = TEXT("?");
		FIntPoint Res(0, 0);
		if (UGameUserSettings* GS = GEngine ? GEngine->GetGameUserSettings() : nullptr)
		{
			const TCHAR* Names[] = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic"), TEXT("Cinematic") };
			const int32 Q = GS->GetOverallScalabilityLevel();
			Quality = Q >= 0 && Q < 5 ? Names[Q] : TEXT("Custom");
			Res = GS->GetScreenResolution();
		}
		// (and where the camera is and which way it looks - centimetres, then pitch and yaw - so a
		// screenshot with this on says exactly where to look again: -shotpos=X,Y,Z -shotrot=P,Y)
		FString At;
		if (UWorld* W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr)
			if (APlayerController* PC = W->GetFirstPlayerController())
				if (PC->PlayerCameraManager)
				{
					const FVector L = PC->PlayerCameraManager->GetCameraLocation();
					const FRotator R = PC->PlayerCameraManager->GetCameraRotation();
					At = FString::Printf(TEXT("   at %d,%d,%d  %d,%d"), FMath::RoundToInt(L.X), FMath::RoundToInt(L.Y), FMath::RoundToInt(L.Z), FMath::RoundToInt(R.Pitch), FMath::RoundToInt(R.Yaw));
				}
		PerfText->SetText(FText::FromString(FString::Printf(TEXT("%3.0f fps  %5.1f ms   game %4.1f  draw %4.1f  GPU %4.1f ms   %dx%d  %s%s"),
			GAverageFPS, GAverageMS, GameMs, RenderMs, GpuMs, Res.X, Res.Y, *Quality, *At)));
		const FLinearColor C = GAverageFPS >= 55 ? FLinearColor(0.6f, 1.f, 0.6f) : GAverageFPS >= 30 ? FLinearColor(1.f, 0.85f, 0.4f) : FLinearColor(1.f, 0.5f, 0.45f);
		PerfText->SetColorAndOpacity(C);
	}
}

void SOTGHud::SetPractice(const TSharedRef<SWidget>& Content, const FString& KeysText)
{
	MapBox->SetWidthOverride(FOptionalSize());
	MapBox->SetHeightOverride(FOptionalSize());
	MapBox->SetContent(Content);
	Keys->SetText(FText::FromString(KeysText));
}

void SOTGHud::SetInfo(const FString& Line1, const FString& Line2, const FString& ClubLine)
{
	Info1->SetText(FText::FromString(Line1));
	Info2->SetText(FText::FromString(Line2));
	ClubText->SetText(FText::FromString(ClubLine));
}

void SOTGHud::SetShotData(const FString& Line1, const FString& Line2)
{
	if (Line1.IsEmpty() && !bSample) { ShotValues.Reset(); BuildTiles(); }
	ShotBox->SetVisibility(Line1.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
	Shot1->SetText(FText::FromString(Line1));
	Shot2->SetText(FText::FromString(Line2));
	Shot2->SetVisibility(Line2.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
}

void SOTGHud::SetStatus(const FString& Text, bool bReady)
{
	Status->SetText(FText::FromString(Text));
	Status->SetColorAndOpacity(bReady ? Green : Soft);
}

void SOTGHud::SetStandings(const FString& Title, const TArray<FOTGStanding>& Rows, const FString& Summary)
{
	// (rebuilt only when something changed)
	FString Key = Title + Summary;
	for (const FOTGStanding& R : Rows) Key += R.Name + R.Value + R.Sub;
	if (Key == StandKey) return;
	StandKey = Key;
	StandList->ClearChildren();
	StandList->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[Text(Title, 13, Soft, "Bold")];
	for (const FOTGStanding& R : Rows)
	{
		StandList->AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(SBox).WidthOverride(10).HeightOverride(10)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(R.Colour)]]
			+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 14, 0)[Text(R.Name, 15, Ink, "Bold")]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Text(R.Value, 15, Gold, "Bold")]
			+ SHorizontalBox::Slot().AutoWidth()[Text(R.Sub, 13, Soft)]
		];
	}
	if (!Summary.IsEmpty()) StandList->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Text(Summary, 13, Soft)];
	StandBox->SetVisibility(Rows.Num() ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
}

void SOTGHud::SetPlayers(const TArray<FPlayerRow>& Rows)
{
	FString Key;
	for (const FPlayerRow& R : Rows) Key += R.Name + TEXT("|") + R.Note + (R.bUp ? TEXT("*") : TEXT(""));
	if (Key == PlayersKey || !PlayerList.IsValid()) return;
	PlayersKey = Key;
	PlayerList->ClearChildren();
	for (const FPlayerRow& R : Rows)
	{
		// (whoever is up: a gold ring round their row, their name in gold, UP beside it)
		const TSharedRef<SWidget> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(SBox).WidthOverride(10).HeightOverride(10)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(R.Colour)]]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0, 0, 12, 0)[Text(R.Name, 15, R.bUp ? Gold : Ink, "Bold")]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, R.bUp ? 8 : 0, 0)[Text(R.Note, 13, Soft)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Text(R.bUp ? TEXT("UP") : TEXT(""), 12, Gold, "Bold")];
		PlayerList->AddSlot().AutoHeight().Padding(0, 1)
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(R.bUp ? Gold : FLinearColor::Transparent).Padding(2)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(R.bUp ? FLinearColor(0.05f, 0.09f, 0.06f, 0.95f) : FLinearColor::Transparent).Padding(FMargin(6, 3))
				[
					Row
				]
			]
		];
	}
	PlayerList->SetVisibility(Rows.Num() ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
}

void SOTGHud::Banner(const FString& Title, const FString& Small, float Seconds)
{
	BannerTitle->SetText(FText::FromString(Title));
	BannerSmall->SetText(FText::FromString(Small));
	BannerSmall->SetVisibility(Small.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
	BannerBox->SetVisibility(EVisibility::HitTestInvisible);
	BannerBox->SetRenderOpacity(1);
	BannerLeft = Seconds;
}

void SOTGHud::Toast(const FString& TextS, float Seconds)
{
	ToastText->SetText(FText::FromString(TextS));
	ToastBox->SetVisibility(EVisibility::HitTestInvisible);
	ToastBox->SetRenderOpacity(1);
	ToastLeft = Seconds;
}

// The scorecard: a printed club card - the course's name on a green band, a ruled paper grid of hole,
// yardage, par and score (a birdie ringed, a bogey boxed, as players mark them), Out / In / Total, and
// a green foot with the totals, the countdown to the next hole and the choice to skip the card
void SOTGHud::ShowScorecard(const FOTGCard& Card)
{
	const FLinearColor Paper(0.97f, 0.96f, 0.91f, 1.f), PaperShade(0.89f, 0.91f, 0.84f, 1.f), Rule(0.62f, 0.66f, 0.58f, 1.f);
	const FLinearColor Band(0.07f, 0.27f, 0.14f, 0.97f), BandText(0.97f, 0.96f, 0.88f, 1.f), CardInk(0.10f, 0.14f, 0.10f, 1.f), CardSoft(0.36f, 0.42f, 0.35f, 1.f);
	const FLinearColor Under(0.75f, 0.12f, 0.12f, 1.f), Over(0.12f, 0.24f, 0.55f, 1.f);
	// (the outline takes its own colour, not the image's tint: a brush each)
	static const FSlateRoundedBoxBrush Circle(FLinearColor::Transparent, 13.f, FLinearColor(0.75f, 0.12f, 0.12f, 1.f), 2.f);
	static const FSlateRoundedBoxBrush Square(FLinearColor::Transparent, 1.f, FLinearColor(0.12f, 0.24f, 0.55f, 1.f), 2.f);
	static const FSlateRoundedBoxBrush Dot(FLinearColor::White, 5.f);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");

	TSharedRef<SGridPanel> Grid = SNew(SGridPanel);
	// (each cell paper on the rule colour, a pixel apart: the card's lines)
	auto Cell = [&](int32 Col, int32 Row, const TSharedRef<SWidget>& W, const FLinearColor& Bg, float Width) {
		Grid->AddSlot(Col, Row).Padding(FMargin(0, 0, 1, 1))
		[
			SNew(SBorder).BorderImage(White).BorderBackgroundColor(Bg).Padding(0)
			[
				SNew(SBox).WidthOverride(Width).HeightOverride(32).HAlign(HAlign_Center).VAlign(VAlign_Center)[W]
			]
		];
	};
	auto T = [&](const FString& S, const FLinearColor& C, bool bBold = false, int32 Size = 15) { return StaticCastSharedRef<SWidget>(Text(S, Size, C, bBold ? "Bold" : "Regular")); };
	// a score as it's marked: eagle two rings, birdie a ring, par plain, bogey a box, double bogey or worse two boxes
	auto Mark = [&](int32 V, int32 Par) -> TSharedRef<SWidget> {
		if (V < 0) return T(FString(), CardInk);
		const int32 D = V - Par;
		const FLinearColor C = D < 0 ? Under : D > 0 ? Over : CardInk;
		TSharedRef<SOverlay> O = SNew(SOverlay);
		const FSlateBrush* Shape = D < 0 ? &Circle : D > 0 ? &Square : nullptr;
		if (Shape)
		{
			O->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(SBox).WidthOverride(30).HeightOverride(30)[SNew(SImage).Image(Shape)]];
			if (D <= -2 || D >= 2) O->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)[SNew(SBox).WidthOverride(24).HeightOverride(24)[SNew(SImage).Image(Shape)]];
		}
		O->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)[Text(FString::FromInt(V), 15, C, "Bold")];
		return O;
	};

	const int32 N = Card.Holes.Num();
	const bool bNines = N > 9;
	const int32 PerRow = bNines ? 9 : N;
	const bool bYards = N > 0 && Card.Holes[0].Yards > 0;
	const float LabelW = 112, HoleW = 40, SubW = 52;
	TArray<int32> Totals, PlayedPar;
	Totals.Init(0, Card.Sides.Num());
	PlayedPar.Init(0, Card.Sides.Num());
	int32 AllPar = 0, AllYards = 0;
	int32 Row = 0;
	for (int32 Start = 0; Start < N; Start += PerRow)
	{
		const int32 End = FMath::Min(N, Start + PerRow);
		const bool bLast = End >= N;
		const int32 SubCol = 1 + PerRow, TotCol = SubCol + 1;
		const FString SubName = bNines ? (Start == 0 ? TEXT("OUT") : TEXT("IN")) : TEXT("TOT");
		// hole numbers: on the green band
		Cell(0, Row, T(TEXT("HOLE"), BandText, true, 13), Band, LabelW);
		for (int32 i = Start; i < End; i++) Cell(1 + i - Start, Row, T(FString::FromInt(Card.Holes[i].Number), BandText, true), Band, HoleW);
		for (int32 i = End; i < Start + PerRow; i++) Cell(1 + i - Start, Row, T(FString(), BandText), Band, HoleW);
		Cell(SubCol, Row, T(SubName, BandText, true, 13), Band, SubW);
		if (bNines) Cell(TotCol, Row, T(bLast ? TEXT("TOT") : FString(), BandText, true, 13), Band, SubW);
		Row++;
		int32 SubPar = 0, SubYards = 0;
		for (int32 i = Start; i < End; i++) { SubPar += Card.Holes[i].Par; SubYards += Card.Holes[i].Yards; }
		AllPar += SubPar; AllYards += SubYards;
		auto Filler = [&](int32 R) { for (int32 i = End; i < Start + PerRow; i++) Cell(1 + i - Start, R, T(FString(), CardInk), Paper, HoleW); };
		if (bYards)
		{
			Cell(0, Row, T(Card.Tee.IsEmpty() ? FString(TEXT("YARDS")) : Card.Tee.ToUpper(), CardSoft, true, 12), Paper, LabelW);
			for (int32 i = Start; i < End; i++) Cell(1 + i - Start, Row, T(FString::FromInt(Card.Holes[i].Yards), CardSoft, false, 12), Paper, HoleW);
			Filler(Row);
			Cell(SubCol, Row, T(FString::FromInt(SubYards), CardSoft, true, 12), PaperShade, SubW);
			if (bNines) Cell(TotCol, Row, T(bLast ? FString::FromInt(AllYards) : FString(), CardSoft, true, 12), PaperShade, SubW);
			Row++;
		}
		Cell(0, Row, T(TEXT("PAR"), CardInk, true, 13), PaperShade, LabelW);
		for (int32 i = Start; i < End; i++) Cell(1 + i - Start, Row, T(FString::FromInt(Card.Holes[i].Par), CardInk), PaperShade, HoleW);
		for (int32 i = End; i < Start + PerRow; i++) Cell(1 + i - Start, Row, T(FString(), CardInk), PaperShade, HoleW);
		Cell(SubCol, Row, T(FString::FromInt(SubPar), CardInk, true), PaperShade, SubW);
		if (bNines) Cell(TotCol, Row, T(bLast ? FString::FromInt(AllPar) : FString(), CardInk, true), PaperShade, SubW);
		Row++;
		for (int32 k = 0; k < Card.Sides.Num(); k++)
		{
			const FOTGCard::FSide& S = Card.Sides[k];
			const bool bMany = Card.Sides.Num() > 1;
			TSharedRef<SHorizontalBox> Who = SNew(SHorizontalBox);
			if (bMany) Who->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[SNew(SBox).WidthOverride(10).HeightOverride(10)[SNew(SImage).Image(&Dot).ColorAndOpacity(S.Colour)]];
			Who->AddSlot().AutoWidth().VAlign(VAlign_Center)[Text(bMany ? S.Name : FString(TEXT("SCORE")), bMany ? 14 : 13, CardInk, "Bold")];
			Cell(0, Row, Who, Paper, LabelW);
			int32 Sub = 0;
			for (int32 i = Start; i < End; i++)
			{
				const int32 V = S.Scores.IsValidIndex(i) ? S.Scores[i] : -1;
				Cell(1 + i - Start, Row, Mark(V, Card.Holes[i].Par), Paper, HoleW);
				if (V >= 0) { Sub += V; Totals[k] += V; PlayedPar[k] += Card.Holes[i].Par; }
			}
			Filler(Row);
			Cell(SubCol, Row, T(Sub > 0 ? FString::FromInt(Sub) : FString(), CardInk, true), PaperShade, SubW);
			if (bNines) Cell(TotCol, Row, T(bLast && Totals[k] > 0 ? FString::FromInt(Totals[k]) : FString(), CardInk, true), PaperShade, SubW);
			Row++;
		}
		// (a gap between the nines)
		if (!bLast) { Grid->AddSlot(0, Row)[SNew(SBox).HeightOverride(10)]; Row++; }
	}
	const TSharedRef<SWidget> Ruled = SNew(SBorder).BorderImage(White).BorderBackgroundColor(Rule).Padding(FMargin(1, 1, 0, 0))[Grid];

	// the foot: the totals, the countdown, the choice to skip
	TSharedRef<SVerticalBox> Foot = SNew(SVerticalBox);
	if (Card.Sides.Num() == 1)
	{
		const int32 ToPar = Totals[0] - PlayedPar[0];
		const FString Rel = ToPar == 0 ? FString(TEXT("Even par")) : FString::Printf(TEXT("%+d"), ToPar);
		Foot->AddSlot().AutoHeight().HAlign(HAlign_Center)[Text(FString::Printf(TEXT("%d strokes  ·  %s"), Totals[0], *Rel), 20, Gold, "Bold")];
	}
	else
	{
		for (const FOTGStanding& R : Card.Standings)
			Foot->AddSlot().AutoHeight().HAlign(HAlign_Center)[Text(FString::Printf(TEXT("%s  %s  %s"), *R.Name, *R.Value, *R.Sub), 16, R.Colour, "Bold")];
		if (!Card.Summary.IsEmpty()) Foot->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 0)[Text(Card.Summary, 15, Gold, "Bold")];
	}
	if (Card.SecondsLeft && !Card.bFinal)
	{
		const TFunction<float()> Left = Card.SecondsLeft;
		Foot->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 0)
		[
			SNew(STextBlock).Font(Font(15, "Bold")).ColorAndOpacity(BandText)
			.Text_Lambda([Left]() { return FText::FromString(FString::Printf(TEXT("Next hole in %d"), FMath::Max(0, FMath::CeilToInt(Left())))); })
		];
		Foot->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([]() { return otg::prefs::SkipScorecard() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([](ECheckBoxState St) { otg::prefs::SetSkipScorecard(St == ECheckBoxState::Checked); })
			[Text(TEXT("Go straight to the next hole from now on (no scorecard)"), 13, Soft)]
		];
	}
	Foot->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)[Text(Card.bFinal ? TEXT("Enter: back to the menu") : Card.SecondsLeft ? TEXT("Enter: next hole now") : TEXT("Tab: close"), 12, Soft)];

	const FString Heading = Card.bFinal ? Card.Course + TEXT("  ·  Round complete") : Card.Course;
	CardSlot->SetContent(
		SNew(SVerticalBox)
		// the green band, the club's name on it
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder).BorderImage(White).BorderBackgroundColor(Band).Padding(FMargin(24, 12))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Text(Heading.ToUpper(), 22, BandText, "Bold")]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Text(Card.Format.IsEmpty() ? FString(TEXT("Official scorecard")) : Card.Format, 12, Gold)]
			]
		]
		// the paper
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder).BorderImage(White).BorderBackgroundColor(Paper).Padding(FMargin(18, 16))[Ruled]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder).BorderImage(White).BorderBackgroundColor(Band).Padding(FMargin(24, 10))[Foot]
		]);
	// (the checkbox takes the mouse; the rest of the card lets clicks through)
	CardSlot->SetVisibility(EVisibility::SelfHitTestInvisible);
}

void SOTGHud::HideScorecard()
{
	CardSlot->SetVisibility(EVisibility::Collapsed);
}

void SOTGHud::ShowPause(FSimpleDelegate OnResume, FSimpleDelegate OnScorecard, FSimpleDelegate OnQuit)
{
	PauseSlot->SetContent(
		SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0, 0, 0, 0.45f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			Box(SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 24, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)[Text(TEXT("Paused"), 28, Ink, "Bold")]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)[Button(TEXT("Resume"), [OnResume]() { OnResume.ExecuteIfBound(); }, 17)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)[Button(TEXT("Scorecard"), [OnScorecard]() { OnScorecard.ExecuteIfBound(); }, 17)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)[Button(TEXT("Quit to the menu"), [OnQuit]() { OnQuit.ExecuteIfBound(); }, 17)]
				]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SOTGLaunchMonitorPanel)],
				FMargin(28, 22), Solid)
		]);
	PauseSlot->SetVisibility(EVisibility::Visible);
}

void SOTGHud::ShowWeatherPanel(bool bShow)
{
	WxPanel->SetVisibility(bShow ? EVisibility::Visible : EVisibility::Collapsed);
}

bool SOTGHud::IsWeatherPanelShown() const
{
	return WxPanel->GetVisibility() == EVisibility::Visible;
}

void SOTGHud::ShowDesigner(bool bShow)
{
	DesignPanel->SetVisibility(bShow ? EVisibility::Visible : EVisibility::Collapsed);
}

bool SOTGHud::IsDesignerShown() const
{
	return DesignPanel->GetVisibility() == EVisibility::Visible;
}

void SOTGHud::ShowPerf(bool bShow)
{
	PerfBox->SetVisibility(bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	PerfShown = -1;
}

bool SOTGHud::IsPerfShown() const
{
	return PerfBox->GetVisibility() != EVisibility::Collapsed;
}

void SOTGHud::ShowShotPanel(bool bShow)
{
	Shots->SetVisibility(bShow ? EVisibility::Visible : EVisibility::Collapsed);
}

bool SOTGHud::IsShotPanelShown() const
{
	return Shots->GetVisibility() == EVisibility::Visible;
}

void SOTGHud::HidePause()
{
	PauseSlot->SetVisibility(EVisibility::Collapsed);
	PauseSlot->SetContent(SNullWidget::NullWidget);
}
