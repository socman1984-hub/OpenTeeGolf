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
#include "RenderCore.h"
#include "RHI.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

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
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[SAssignNew(ClubText, STextBlock).Font(Font(16, "Bold")).ColorAndOpacity(Gold)])
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
			Button(bOn ? FString(TEXT("✓  ")) + I.Label : FString(TEXT("     ")) + I.Label, [this, Id]() {
				if (!ShotShown.Remove(Id)) ShotShown.Add(Id);
				SaveShotSettings(); BuildTiles(); BuildShotSettings();
			}, 14, bOn)
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
		PerfText->SetText(FText::FromString(FString::Printf(TEXT("%3.0f fps  %5.1f ms   game %4.1f  draw %4.1f  GPU %4.1f ms   %dx%d  %s"),
			GAverageFPS, GAverageMS, GameMs, RenderMs, GpuMs, Res.X, Res.Y, *Quality)));
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

void SOTGHud::ShowScorecard(const FOTGCard& Card)
{
	TSharedRef<SGridPanel> Grid = SNew(SGridPanel);
	auto Cell = [&](int32 Col, int32 Row, const FString& S, const FLinearColor& C, bool bBold = false) {
		Grid->AddSlot(Col, Row).Padding(FMargin(7, 3)).HAlign(HAlign_Center)[Text(S, 15, C, bBold ? "Bold" : "Regular")];
	};
	// front and back nine in rows of their own (a short course: one row)
	const int32 N = Card.Holes.Num();
	const int32 PerRow = N > 9 ? 9 : N;
	int32 Row = 0;
	TArray<int32> Totals;
	Totals.Init(0, Card.Sides.Num());
	TArray<int32> PlayedPar;
	PlayedPar.Init(0, Card.Sides.Num());
	for (int32 Start = 0; Start < N; Start += PerRow)
	{
		const int32 End = FMath::Min(N, Start + PerRow);
		Cell(0, Row, TEXT("Hole"), Soft, true);
		Cell(0, Row + 1, TEXT("Par"), Soft, true);
		int32 SubPar = 0;
		for (int32 i = Start; i < End; i++)
		{
			Cell(1 + i - Start, Row, FString::FromInt(Card.Holes[i].Number), Soft);
			Cell(1 + i - Start, Row + 1, FString::FromInt(Card.Holes[i].Par), Ink);
			SubPar += Card.Holes[i].Par;
		}
		const FString SubName = N > 9 ? (Start == 0 ? TEXT("Out") : TEXT("In")) : TEXT("Total");
		Cell(1 + PerRow, Row, SubName, Soft, true);
		Cell(1 + PerRow, Row + 1, FString::FromInt(SubPar), Ink, true);
		for (int32 k = 0; k < Card.Sides.Num(); k++)
		{
			const FOTGCard::FSide& S = Card.Sides[k];
			Cell(0, Row + 2 + k, S.Name, Card.Sides.Num() > 1 ? S.Colour : Soft, true);
			int32 Sub = 0;
			for (int32 i = Start; i < End; i++)
			{
				const int32 V = S.Scores.IsValidIndex(i) ? S.Scores[i] : -1;
				const int32 Par = Card.Holes[i].Par;
				const FLinearColor C = V < 0 ? Soft : V < Par ? FLinearColor(1.f, 0.45f, 0.4f) : V > Par ? FLinearColor(0.55f, 0.75f, 1.f) : Ink;
				Cell(1 + i - Start, Row + 2 + k, V < 0 ? TEXT("-") : FString::FromInt(V), C, true);
				if (V >= 0) { Sub += V; Totals[k] += V; PlayedPar[k] += Par; }
			}
			Cell(1 + PerRow, Row + 2 + k, FString::FromInt(Sub), Ink, true);
		}
		Row += 3 + Card.Sides.Num();
	}
	TSharedRef<SVerticalBox> Foot = SNew(SVerticalBox);
	if (Card.Sides.Num() == 1)
	{
		const int32 ToPar = Totals[0] - PlayedPar[0];
		Foot->AddSlot().AutoHeight().HAlign(HAlign_Center)[Text(FString::Printf(TEXT("%d strokes, %s"), Totals[0], ToPar == 0 ? TEXT("even par") : *FString::Printf(TEXT("%+d"), ToPar)), 18, Gold, "Bold")];
	}
	else
	{
		for (const FOTGStanding& R : Card.Standings)
			Foot->AddSlot().AutoHeight().HAlign(HAlign_Center)[Text(FString::Printf(TEXT("%s  %s  %s"), *R.Name, *R.Value, *R.Sub), 16, R.Colour, "Bold")];
		if (!Card.Summary.IsEmpty()) Foot->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 0)[Text(Card.Summary, 15, Gold, "Bold")];
	}
	const FString Heading = (Card.bFinal ? Card.Course + TEXT(": round complete") : Card.Course) + (Card.Format.IsEmpty() ? FString() : TEXT("  ·  ") + Card.Format);
	CardSlot->SetContent(
		Box(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)[Text(Heading, 24, Ink, "Bold")]
			+ SVerticalBox::Slot().AutoHeight()[Grid]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)[Foot]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)[Text(Card.bFinal ? TEXT("Enter: back to the menu") : TEXT("Enter: next hole"), 13, Soft)],
			FMargin(28, 18), Solid));
	CardSlot->SetVisibility(EVisibility::HitTestInvisible);
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
