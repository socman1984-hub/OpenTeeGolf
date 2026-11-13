// See OTGCourseSubsystem.h.

#include "OTGCourseSubsystem.h"

#include "OTGCourseData.h"

#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGCourses, Log, All);

namespace
{
	const FPrimaryAssetType CourseType(TEXT("OTGCourse"));

	UOTGCourseSubsystem* SubsystemOf(UWorld* World)
	{
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UOTGCourseSubsystem>() : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs CoursesCommand(TEXT("OTG.Courses"), TEXT("Lists the courses in the installed course packs"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) {
			if (UOTGCourseSubsystem* S = SubsystemOf(World))
				for (const FOTGCourseInfo& C : S->ListCourses()) UE_LOG(LogOTGCourses, Display, TEXT("%s: %s (%s), %d holes"), *C.Id, *C.Name, *C.Location, C.Holes);
		}));
	FAutoConsoleCommandWithWorldAndArgs PlayCommand(TEXT("OTG.Play"), TEXT("OTG.Play <course id>: opens the course"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			if (UOTGCourseSubsystem* S = SubsystemOf(World); S && Args.Num()) S->PlayCourse(Args[0]);
		}));
}

void UOTGCourseSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const TArray<FOTGCourseInfo> Courses = ListCourses();
	UE_LOG(LogOTGCourses, Display, TEXT("%d course(s) in the installed packs"), Courses.Num());
	for (const FOTGCourseInfo& C : Courses) UE_LOG(LogOTGCourses, Display, TEXT("  %s: %s, %d holes"), *C.Id, *C.Name, C.Holes);
	// (-course=<id>: straight onto it, once the game is up)
	FString Id;
	if (FParse::Value(FCommandLine::Get(), TEXT("course="), Id))
	{
		FTimerDelegate Open = FTimerDelegate::CreateWeakLambda(this, [this, Id]() { PlayCourse(Id); });
		GetGameInstance()->GetTimerManager().SetTimerForNextTick(Open);
	}
	// (-shot=<seconds>: a screenshot of the game that long after it starts, then quit - for checking
	// a build without anyone at the screen; -shotpos=X,Y,Z -shotrot=Pitch,Yaw puts the camera there)
	float ShotAfter = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("shot="), ShotAfter) && ShotAfter > 0)
	{
		FTimerHandle Handle;
		GetGameInstance()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]() {
			FString Pos, Rot;
			UWorld* World = GetGameInstance()->GetWorld();
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			if (World && PC && FParse::Value(FCommandLine::Get(), TEXT("shotpos="), Pos, false))
			{
				// (a camera of its own there, looked through)
				TArray<FString> P, R;
				Pos.ParseIntoArray(P, TEXT(","));
				FRotator Rotation = FRotator::ZeroRotator;
				if (FParse::Value(FCommandLine::Get(), TEXT("shotrot="), Rot, false))
				{
					Rot.ParseIntoArray(R, TEXT(","));
					if (R.Num() == 2) Rotation = FRotator(FCString::Atod(*R[0]), FCString::Atod(*R[1]), 0);
				}
				if (P.Num() == 3)
				{
					const FVector Location(FCString::Atod(*P[0]), FCString::Atod(*P[1]), FCString::Atod(*P[2]));
					if (ACameraActor* Cam = World->SpawnActor<ACameraActor>(Location, Rotation))
					{
						Cam->GetCameraComponent()->SetFieldOfView(75);
						PC->SetViewTarget(Cam);
						UE_LOG(LogOTGCourses, Display, TEXT("Screenshot camera at %s, %s"), *Location.ToString(), *Rotation.ToString());
					}
				}
			}
			FTimerHandle Later;
			GetGameInstance()->GetTimerManager().SetTimer(Later, FTimerDelegate::CreateWeakLambda(this, [this]() {
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/otg_shot.png"), false, false);
				FTimerHandle Quit;
				GetGameInstance()->GetTimerManager().SetTimer(Quit, FTimerDelegate::CreateWeakLambda(this, [this]() {
					if (UWorld* W = GetGameInstance()->GetWorld()) UKismetSystemLibrary::QuitGame(W, nullptr, EQuitPreference::Quit, false);
				}), 3.0f, false);
			}), 5.0f, false);
		}), ShotAfter, false);
	}
}

TArray<FOTGCourseInfo> UOTGCourseSubsystem::ListCourses() const
{
	TArray<FOTGCourseInfo> Out;
	UAssetManager& AM = UAssetManager::Get();
	TArray<FAssetData> Assets;
	AM.GetPrimaryAssetDataList(CourseType, Assets);
	for (const FAssetData& A : Assets)
	{
		if (UOTGCourseData* D = Cast<UOTGCourseData>(A.GetAsset()))
		{
			FOTGCourseInfo Info;
			Info.Id = D->CourseId; Info.Name = D->CourseName; Info.Location = D->Location; Info.Holes = D->Holes.Num();
			Out.Add(Info);
		}
	}
	Out.Sort([](const FOTGCourseInfo& A, const FOTGCourseInfo& B) { return A.Name < B.Name; });
	return Out;
}

UOTGCourseData* UOTGCourseSubsystem::LoadCourse(const FString& Id) const
{
	const FSoftObjectPath Path = UAssetManager::Get().GetPrimaryAssetPath(FPrimaryAssetId(CourseType, FName(*Id)));
	if (Path.IsValid()) return Cast<UOTGCourseData>(Path.TryLoad());
	// (not in the asset registry: a pack added after the game was built - try where packs put it)
	return LoadObject<UOTGCourseData>(nullptr, *FString::Printf(TEXT("/Game/Courses/%s/CourseData.CourseData"), *Id));
}

bool UOTGCourseSubsystem::PlayCourse(const FString& Id)
{
	UOTGCourseData* D = LoadCourse(Id);
	if (!D || D->Level.IsNull())
	{
		UE_LOG(LogOTGCourses, Warning, TEXT("No course %s in the installed packs"), *Id);
		return false;
	}
	Current = D;
	UE_LOG(LogOTGCourses, Display, TEXT("Opening %s (%s)"), *D->CourseName, *D->Level.ToSoftObjectPath().ToString());
	UGameplayStatics::OpenLevelBySoftObjectPtr(GetGameInstance(), D->Level);
	return true;
}
