// The main menu's What's new and Settings pages (see SOTGMainMenu.h).

#include "UI/Menus/SOTGMainMenu.h"
#include "UI/Menus/SOTGLaunchMonitorPanel.h"
#include "Round/OTGMiniGolf.h"
#include "UI/Builder/SOTGCourseBuilder.h"
#include "UI/Editor/SOTGCourseEditor.h"
#include "App/OTGPrefs.h"

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
		// (the parts set to different levels - by Pick for this PC, or by hand: no one level, shown as such)
		if (Level < 0) W->AddSlot().Padding(0, 0, 6, 6)[Button(TEXT("Custom (mixed)"), []() {}, 14, true)];
		W->AddSlot().Padding(8, 0, 6, 6)[Button(TEXT("Pick for this PC"), [GS, Apply]() { GS->RunHardwareBenchmark(); GS->ApplyHardwareBenchmarkResults(); Apply(); }, 13)];
		Row2(TEXT("Graphics quality"), W);
		if (Level < 0)
			V->AddSlot().AutoHeight().Padding(0, 2)[Text(TEXT("Custom: shadows, effects, foliage and view distance are at different levels. Pick one above to set them all the same."), 12, Soft)];
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
	// playing: the scorecard after each hole, or straight on
	{
		TSharedRef<SVerticalBox> W = SNew(SVerticalBox);
		W->AddSlot().AutoHeight()
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([]() { return otg::prefs::SkipScorecard() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([](ECheckBoxState St) { otg::prefs::SetSkipScorecard(St == ECheckBoxState::Checked); })
			[Text(TEXT("Go straight to the next hole after holing out (skip the scorecard)"), 14, Ink)]
		];
		W->AddSlot().AutoHeight().Padding(0, 4)[Text(TEXT("Tab shows the scorecard any time. Off: it shows after each hole, counting down to the next."), 12, Soft)];
		Row2(TEXT("Between holes"), W);
	}
	V->AddSlot().AutoHeight().Padding(0, 8)
	[
		SNew(SCheckBox).IsChecked_Lambda([GS]() { return GS->IsVSyncEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([GS, Apply](ECheckBoxState S) { GS->SetVSyncEnabled(S == ECheckBoxState::Checked); Apply(); })
		[Text(TEXT("Vertical sync (no tearing; caps the frame rate at the screen's)"), 14, Ink)]
	];
	return SNew(SScrollBox) + SScrollBox::Slot()[Box(V, FMargin(20, 16), Solid)];
}
