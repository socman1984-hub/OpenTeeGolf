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
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGGame, Log, All);

namespace
{
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
	if (!Course) return;
	// (only in that course's level)
	const FString Level = Course->Level.ToSoftObjectPath().GetLongPackageName();
	const FString Here = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
	if (Level != Here)
	{
		UE_LOG(LogOTGGame, Display, TEXT("Not the course's level (%s, not %s): no round"), *Here, *Level);
		return;
	}
	int32 Hole = 1;
	FParse::Value(FCommandLine::Get(), TEXT("hole="), Hole);
	AOTGRound* Round = GetWorld()->SpawnActor<AOTGRound>();
	Round->Setup(Course, Hole - 1);
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
