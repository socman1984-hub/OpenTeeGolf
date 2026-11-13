// See OTGGameMode.h.

#include "Game/OTGGameMode.h"

#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/OTGRound.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OTGCourseData.h"
#include "OTGCourseSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "SOTGMenus.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGGame, Log, All);

namespace
{
	TWeakPtr<SOTGMainMenu> TheMenu;
	FAutoConsoleCommand MenuStartCmd(TEXT("OTG.Menu.Start"), TEXT("Start the round set up in the menu (the Start button)"),
		FConsoleCommandDelegate::CreateLambda([]() { if (TSharedPtr<SOTGMainMenu> M = TheMenu.Pin()) M->Start(); }));
	FAutoConsoleCommand MenuPageCmd(TEXT("OTG.Menu.Page"), TEXT("OTG.Menu.Page play|players|lm"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& A) { if (TSharedPtr<SOTGMainMenu> M = TheMenu.Pin(); M && A.Num()) M->ShowPage(A[0]); }));
	FAutoConsoleCommand ShotCmd(TEXT("OTG.Shot"), TEXT("OTG.Shot <name>: a screenshot, HUD and all, in Saved/Screenshots/<name>.png"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) {
			const FString Name = Args.Num() ? Args[0] : TEXT("otg");
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots") / (Name + TEXT(".png")), true, false);
			UE_LOG(LogOTGGame, Display, TEXT("Screenshot %s"), *Name);
		}));
}

AOTGGameMode::AOTGGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AOTGPlayerController::StaticClass();
}

void AOTGGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// (no pawn: the round's camera is the view)
}

void AOTGGameMode::StartPlay()
{
	Super::StartPlay();
	UOTGCourseSubsystem* Courses = GetGameInstance() ? GetGameInstance()->GetSubsystem<UOTGCourseSubsystem>() : nullptr;
	UOTGCourseData* Course = Courses ? Courses->Current.Get() : nullptr;
	// (a round only in the course's own level; anywhere else, the menu)
	const FString Here = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
	if (!Course || Course->Level.ToSoftObjectPath().GetLongPackageName() != Here)
	{
		ShowMenu();
		return;
	}
	// What the menu set up (or, opened from the command line, what it was last set to)
	FOTGRoundSetup Setup = Courses->bHasPendingSetup && Courses->PendingSetup.CourseId == Course->CourseId ? Courses->PendingSetup : FOTGPlayers::LoadSetup();
	Courses->bHasPendingSetup = false;
	Setup.CourseId = Course->CourseId;
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("hole="), Setup.StartHole);
	FParse::Value(Cmd, TEXT("tee="), Setup.Tee);
	FParse::Value(Cmd, TEXT("pin="), Setup.Pin);
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
	}
	AOTGRound* Round = GetWorld()->SpawnActor<AOTGRound>();
	Round->Setup(Course, Setup);
}

void AOTGGameMode::ShowMenu()
{
	if (!GEngine || !GEngine->GameViewport) return;
	TSharedRef<SOTGMainMenu> M = SNew(SOTGMainMenu);
	Menu = M;
	TheMenu = M;
	GEngine->GameViewport->AddViewportWidgetContent(M, 5);
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->SetShowMouseCursor(true);
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(M);
		PC->SetInputMode(Mode);
	}
	UE_LOG(LogOTGGame, Display, TEXT("Menu"));
}

void AOTGGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Menu && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
	Menu.Reset();
	Super::EndPlay(Reason);
}

// ------------------------------------------------------------------ the keys

AOTGPlayerController::AOTGPlayerController()
{
	bShowMouseCursor = false;
}

AOTGRound* AOTGPlayerController::Round()
{
	if (!Cached.IsValid())
		for (TActorIterator<AOTGRound> It(GetWorld()); It; ++It) { Cached = *It; break; }
	return Cached.Get();
}

void AOTGPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	auto Bind = [this](const FKey& Key, TFunction<void(AOTGRound*)> Fn) {
		FInputKeyBinding B(FInputChord(Key), IE_Pressed);
		B.KeyDelegate.GetDelegateForManualSet().BindLambda([this, Fn]() { if (AOTGRound* R = Round()) Fn(R); });
		InputComponent->KeyBindings.Add(B);
	};
	Bind(EKeys::Up, [](AOTGRound* R) { R->ChangeClub(1); });
	Bind(EKeys::W, [](AOTGRound* R) { R->ChangeClub(1); });
	Bind(EKeys::Down, [](AOTGRound* R) { R->ChangeClub(-1); });
	Bind(EKeys::S, [](AOTGRound* R) { R->ChangeClub(-1); });
	Bind(EKeys::SpaceBar, [](AOTGRound* R) { R->TestShot(); });
	Bind(EKeys::Enter, [](AOTGRound* R) { R->Continue(); });
	Bind(EKeys::Tab, [](AOTGRound* R) { R->ToggleScorecard(); });
	Bind(EKeys::BackSpace, [](AOTGRound* R) { R->Mulligan(); });
	Bind(EKeys::Escape, [this](AOTGRound* R) {
		R->TogglePause();
		// (the pause menu wants the mouse)
		SetShowMouseCursor(R->IsPaused());
		if (R->IsPaused()) SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));
		else SetInputMode(FInputModeGameOnly());
	});
}

void AOTGPlayerController::PlayerTick(float Dt)
{
	Super::PlayerTick(Dt);
	// aiming: held keys turn it (faster with Shift)
	const bool bLeft = IsInputKeyDown(EKeys::Left) || IsInputKeyDown(EKeys::A);
	const bool bRight = IsInputKeyDown(EKeys::Right) || IsInputKeyDown(EKeys::D);
	if (bLeft != bRight)
		if (AOTGRound* R = Round())
		{
			const double Rate = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift) ? 30.0 : 8.0;
			R->AimBy((bRight ? 1 : -1) * Rate * Dt);
		}
}
