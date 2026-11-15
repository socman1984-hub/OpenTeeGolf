// The course helper: OpenTeeGolf's Node server (server.js, lib/helper.js) run hidden by the game,
// which finds, builds and edits courses through it. It downloads the map data, heights and aerial
// photos, builds the course's source files (courses/<id>/<id>.course.json, .real.json, .sat/), and
// exports a course to the game's courses folder (headless Edge or Chrome running the web game's
// export: /?export=<id>). Started the first time something needs it, on a free port on this
// computer only; it goes when the game does (it watches the game's process too).
//
// Where it is: -helper=<folder>, else OTG_HELPER_DIR, else the repository round the project (a
// development copy: courses in its courses folder, as the web version keeps them), else Helper\
// next to the game (a release: courses' sources in Documents\OpenTeeGolf\CourseSources). Node:
// <helper>\node\node.exe, else node on the PATH.
//
// Console: OTG.Helper.Start, OTG.Helper.Status, OTG.Helper.Search <words>, OTG.Helper.Check
// <way|relation> <id>, OTG.Helper.Build <way|relation> <id>, OTG.Helper.Export <course id>,
// OTG.Helper.Cancel, OTG.Helper.Load <course id> (reads the course source and logs it).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "OTGHelperSubsystem.generated.h"

class FJsonObject;
class FJsonValue;

UENUM()
enum class EOTGHelperState : uint8
{
	Off,        // not started
	Starting,
	Ready,
	Failed,     // Message says why
};

// A long job on the helper (a build or an export): where it is
USTRUCT()
struct FOTGHelperJob
{
	GENERATED_BODY()
	UPROPERTY() FString Kind;        // build, update or export
	UPROPERTY() FString Id;          // the build's job id, the exported course's id
	UPROPERTY() FString State;       // start, run, done, failed
	UPROPERTY() float Frac = 0;      // 0..1 (an export's; a build says only its step)
	UPROPERTY() FString Message;
	UPROPERTY() FString CourseId;    // a finished build's course
	UPROPERTY() int32 Seconds = 0;
	double StartedAt = 0;              // (FPlatformTime::Seconds when it began: the card's clock)
	double EndedAt = 0;
	TArray<FString> Log;               // (what it said, last first-come: the card shows the latest)
	bool IsRunning() const { return State == TEXT("start") || State == TEXT("run"); }
};

UCLASS()
class OPENTEEGOLF_API UOTGHelperSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// (an answer: the HTTP code, 0 for none, and the body read as JSON when it is JSON)
	using FDone = TFunction<void(int32 Code, TSharedPtr<FJsonValue> Json, const FString& Body)>;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Start it if it isn't running (Then: when it answers, or has failed)
	void Start(TFunction<void(bool bOk)> Then = nullptr);
	void Stop();
	EOTGHelperState GetState() const { return State; }
	FString GetMessage() const { return Message; }
	int32 GetPort() const { return Port; }
	// The helper's console output, last lines
	const TArray<FString>& RecentOutput() const { return Output; }
	// Whether there's a helper to start at all (its folder, and Node)
	bool Available(FString* Why = nullptr) const;

	// Any request to it (started first if need be); Path from the root, e.g. /api/courses
	void Request(const FString& Verb, const FString& Path, const FString& Body, FDone Done);
	void Get(const FString& Path, FDone Done) { Request(TEXT("GET"), Path, FString(), MoveTemp(Done)); }

	// ---- finding and building courses (the web version's course builder)
	void Search(const FString& Words, FDone Done);                        // { results: [...] }
	void World(int32 From, FDone Done);                                   // the world's mapped courses
	void Where(double Lat, double Lon, FDone Done);                       // { location }
	void Check(const FString& OsmType, int64 OsmId, FDone Done);          // what's mapped, its hole groups
	// Build a course (Name, Location, Group optional); the job follows in Job
	void Build(const FString& OsmType, int64 OsmId, const FString& Name, const FString& Location, const FString& Group, bool bPhotoColours = false);
	// Update a built course from OpenStreetMap (kept: the editor's trees, grass and looks)
	void Update(const FString& CourseId);
	// Export a built course to the game's courses folder (about 5-15 minutes)
	void Export(const FString& CourseId, bool bVisible = false);
	void Cancel();
	// The long job going on or last done (none: Kind empty)
	const FOTGHelperJob& GetJob() const { return Job; }
	// Called when the job finishes (done or failed)
	TMulticastDelegate<void(const FOTGHelperJob&)> OnJobDone;

	// The course's source, as the editor reads it: { course, real } (course.json, real.json)
	void LoadSource(const FString& CourseId, TFunction<void(TSharedPtr<FJsonObject> Course, TSharedPtr<FJsonObject> Real, const FString& Error)> Done);
	// Save it (the body the web editor's Save sends: FOTGCourseSource::SaveBody)
	void SaveSource(const FString& CourseId, TSharedRef<FJsonObject> Body, FDone Done);

	FString StatusLine() const;

private:
	bool Tick(float Dt);
	void Launch();
	void Ping(int32 Attempt);
	void Finish(bool bOk, const FString& Why = FString());
	void PollJob();
	FString HelperDir(bool* bDev = nullptr) const;
	FString NodeExe(const FString& Dir) const;

	EOTGHelperState State = EOTGHelperState::Off;
	FString Message;
	int32 Port = 0;
	int32 Tries = 0;
	FProcHandle Proc;
	uint32 ProcId = 0;
	void* PipeRead = nullptr;
	void* PipeWrite = nullptr;
	FString Partial;
	TArray<FString> Output;
	TArray<TFunction<void(bool)>> Waiting;
	FTSTicker::FDelegateHandle TickHandle;
	double NextPoll = 0;
	FOTGHelperJob Job;
	bool bPolling = false;
};
