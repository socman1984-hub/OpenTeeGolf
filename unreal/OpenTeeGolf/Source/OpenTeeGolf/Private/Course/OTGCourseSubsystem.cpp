// See OTGCourseSubsystem.h.

#include "Course/OTGCourseSubsystem.h"

#include "Course/OTGCourseData.h"
#include "Round/OTGRange.h"
#include "Round/OTGMiniGolf.h"
#include "App/OTGCourseSync.h"

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
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGCourses, Log, All);

namespace
{
	const FPrimaryAssetType CourseType(TEXT("OTGCourse"));
	// (the level an exported course is built in: sky, sun, fog and clouds, nothing else)
	const TCHAR* TemplateLevel = TEXT("/Game/OpenTeeGolf/Game/L_Course.L_Course");

	// An exported course's name, place and holes, from its play.json (without loading the rest)
	bool PeekExport(const FString& Dir, FOTGCourseInfo& Out)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Dir, TEXT("play.json")))) return false;
		TSharedPtr<FJsonObject> Play;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Play) || !Play.IsValid()) return false;
		if (!Play->TryGetStringField(TEXT("id"), Out.Id)) return false;
		Play->TryGetStringField(TEXT("name"), Out.Name);
		Play->TryGetStringField(TEXT("location"), Out.Location);
		const TArray<TSharedPtr<FJsonValue>>* Holes = nullptr;
		Out.Holes = Play->TryGetArrayField(TEXT("holes"), Holes) ? Holes->Num() : 0;
		Out.ExportDir = Dir;
		// (a saved copy of another player's version of a course, from an online game: its own id and name)
		otg::sync::FMarker M;
		if (otg::sync::ReadMarker(Dir, M))
		{
			Out.Id = M.Id;
			if (!M.Name.IsEmpty()) Out.Name = M.Name;
			Out.SharedFrom = M.From;
		}
		return true;
	}

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
	FAutoConsoleCommandWithWorldAndArgs RangeCommand(TEXT("OTG.Range"), TEXT("Opens the driving range"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) {
			if (UOTGCourseSubsystem* S = SubsystemOf(World)) S->PlayRange();
		}));
}

void UOTGCourseSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const TArray<FOTGCourseInfo> Courses = ListCourses();
	UE_LOG(LogOTGCourses, Display, TEXT("%d course(s): exported courses from %s, and installed packs"), Courses.Num(), *CoursesFolder());
	for (const FOTGCourseInfo& C : Courses) UE_LOG(LogOTGCourses, Display, TEXT("  %s: %s, %d holes"), *C.Id, *C.Name, C.Holes);
	// (-course=<id>: straight onto it, once the game is up)
	// (-minigolf: the putt-putt)
	FString Id;
	if (!FParse::Value(FCommandLine::Get(), TEXT("course="), Id) && FParse::Param(FCommandLine::Get(), TEXT("minigolf"))) Id = AOTGMiniGolf::CourseId;
	if (!Id.IsEmpty())
	{
		FTimerDelegate Open = FTimerDelegate::CreateWeakLambda(this, [this, Id]() { PlayCourse(Id); });
		GetGameInstance()->GetTimerManager().SetTimerForNextTick(Open);
	}
	// (-range: straight onto the driving range)
	else if (FParse::Param(FCommandLine::Get(), TEXT("range")))
		GetGameInstance()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { PlayRange(); }));
	// (-shot=<seconds>: a screenshot of the game that long after it starts, then quit - for checking
	// a build without anyone at the screen; -shotpos=X,Y,Z -shotrot=Pitch,Yaw puts the camera there)
	float ShotAfter = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("shot="), ShotAfter) && ShotAfter > 0)
	{
		FTimerHandle Handle;
		GetGameInstance()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]() {
			FString Pos, Rot, Cmd;
			UWorld* World = GetGameInstance()->GetWorld();
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			// (-shotcmd=<console command>[;<another>], + for a space: run first, as the round is set - a
			// time of day, the weather - so a screenshot can show them)
			if (PC && FParse::Value(FCommandLine::Get(), TEXT("shotcmd="), Cmd, false))
			{
				Cmd.ReplaceInline(TEXT("+"), TEXT(" "));
				TArray<FString> Cmds;
				Cmd.ParseIntoArray(Cmds, TEXT(";"));
				for (const FString& C : Cmds) PC->ConsoleCommand(C.TrimStartAndEnd());
			}
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
						float Fov = 75;
						FParse::Value(FCommandLine::Get(), TEXT("shotfov="), Fov);   // (narrow: a closer look)
						Cam->GetCameraComponent()->SetFieldOfView(Fov);
						PC->SetViewTarget(Cam);
						UE_LOG(LogOTGCourses, Display, TEXT("Screenshot camera at %s, %s"), *Location.ToString(), *Rotation.ToString());
					}
				}
			}
			// (-shotwait=<s>: longer before the picture - grass changed by -shotcmd takes a while to grow)
			float Wait = 5;
			FParse::Value(FCommandLine::Get(), TEXT("shotwait="), Wait);
			FTimerHandle Later;
			GetGameInstance()->GetTimerManager().SetTimer(Later, FTimerDelegate::CreateWeakLambda(this, [this]() {
				if (UWorld* W = GetGameInstance()->GetWorld())
					if (APlayerController* P2 = W->GetFirstPlayerController())
						if (P2->PlayerCameraManager)
							UE_LOG(LogOTGCourses, Display, TEXT("Screenshot from %s looking %s"), *P2->PlayerCameraManager->GetCameraLocation().ToString(), *P2->PlayerCameraManager->GetCameraRotation().ToString());
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/otg_shot.png"), FParse::Param(FCommandLine::Get(), TEXT("shotui")), false);
				FTimerHandle Quit;
				GetGameInstance()->GetTimerManager().SetTimer(Quit, FTimerDelegate::CreateWeakLambda(this, [this]() {
					if (UWorld* W = GetGameInstance()->GetWorld()) UKismetSystemLibrary::QuitGame(W, nullptr, EQuitPreference::Quit, false);
				}), 3.0f, false);
			}), Wait, false);
		}), ShotAfter, false);
	}
}

FString UOTGCourseSubsystem::DefaultCoursesFolder()
{
	// (Documents: the user folder Unreal knows, on Windows)
	return FPaths::Combine(FPlatformProcess::UserDir(), TEXT("OpenTeeGolf"), TEXT("Courses"));
}

FString UOTGCourseSubsystem::CoursesFolder() const
{
	FString Folder;
	if (FParse::Value(FCommandLine::Get(), TEXT("courses="), Folder) && !Folder.IsEmpty()) return Folder;
	if (GConfig && GConfig->GetString(TEXT("OpenTeeGolf"), TEXT("CoursesFolder"), Folder, GGameUserSettingsIni) && !Folder.IsEmpty()) return Folder;
	return DefaultCoursesFolder();
}

void UOTGCourseSubsystem::SetCoursesFolder(const FString& Folder)
{
	if (!GConfig) return;
	GConfig->SetString(TEXT("OpenTeeGolf"), TEXT("CoursesFolder"), *Folder, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

TArray<FOTGCourseInfo> UOTGCourseSubsystem::ListCourses() const
{
	TArray<FOTGCourseInfo> Out;
	// the exported courses: every folder with a play.json
	const FString Root = CoursesFolder();
	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *FPaths::Combine(Root, TEXT("*")), false, true);
	for (const FString& D : Dirs)
	{
		FOTGCourseInfo Info;
		if (PeekExport(FPaths::Combine(Root, D), Info)) Out.Add(Info);
	}
	const bool bPacks = FParse::Param(FCommandLine::Get(), TEXT("packs"));
	UAssetManager& AM = UAssetManager::Get();
	TArray<FAssetData> Assets;
	AM.GetPrimaryAssetDataList(CourseType, Assets);
	for (const FAssetData& A : Assets)
	{
		if (UOTGCourseData* D = Cast<UOTGCourseData>(A.GetAsset()))
		{
			// (a pack of a course that's also exported: the export, unless -packs)
			const int32 Dup = Out.IndexOfByPredicate([&](const FOTGCourseInfo& I) { return I.Id == D->CourseId; });
			if (Dup != INDEX_NONE) { if (!bPacks) continue; Out.RemoveAt(Dup); }
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
	// the putt-putt: made by the game itself
	if (Id == AOTGMiniGolf::CourseId)
	{
		if (!MiniGolf) MiniGolf = AOTGMiniGolf::MakeCourse(const_cast<UOTGCourseSubsystem*>(this));
		return MiniGolf;
	}
	// an exported course: its play data read from the folder, built in the template level
	for (const FOTGCourseInfo& C : ListCourses())
	{
		if (C.Id != Id || C.ExportDir.IsEmpty()) continue;
		if (LastExport && LastExport->CourseId == Id && LastExport->ExportDir == C.ExportDir) return LastExport;
		UOTGCourseData* D = NewObject<UOTGCourseData>(const_cast<UOTGCourseSubsystem*>(this));
		FString Why;
		if (!D->LoadFromExport(C.ExportDir, &Why)) { UE_LOG(LogOTGCourses, Warning, TEXT("%s: %s"), *Id, *Why); return nullptr; }
		D->ExportDir = C.ExportDir;
		// (a saved copy: known by its own id and name, not the course's)
		if (!C.SharedFrom.IsEmpty()) { D->CourseId = C.Id; D->CourseName = C.Name; }
		D->Level = TSoftObjectPtr<UWorld>(FSoftObjectPath(TemplateLevel));
		D->LevelOrigin = FVector::ZeroVector;   // (the export's north-west corner, at its base height)
		LastExport = D;
		return D;
	}
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

bool UOTGCourseSubsystem::PlayRange()
{
	FString Dir, Why;
	if (!otg::range::Ensure(Dir, &Why)) { UE_LOG(LogOTGCourses, Warning, TEXT("No driving range: %s"), *Why); return false; }
	UOTGCourseData* D = NewObject<UOTGCourseData>(this);
	if (!D->LoadFromExport(Dir, &Why)) { UE_LOG(LogOTGCourses, Warning, TEXT("The driving range doesn't read: %s"), *Why); return false; }
	D->ExportDir = Dir;
	D->Level = TSoftObjectPtr<UWorld>(FSoftObjectPath(TemplateLevel));
	D->LevelOrigin = FVector::ZeroVector;
	Current = D;
	bHasPendingSetup = false;
	UE_LOG(LogOTGCourses, Display, TEXT("Opening the driving range (%s)"), *Dir);
	UGameplayStatics::OpenLevelBySoftObjectPtr(GetGameInstance(), D->Level);
	return true;
}
