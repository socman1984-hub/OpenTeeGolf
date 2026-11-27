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
// <helper>\node\node.exe (on Linux <helper>/node/bin/node), else node on the PATH.
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

// A course in the helper's queue (lib/queue.js): built, updated or got ready for the game in the
// background. Kind is the step it's on (build, update or export); State waiting, run, done or failed
USTRUCT()
struct FOTGHelperJob
{
	GENERATED_BODY()
	UPROPERTY() FString QueueId;     // (its place in the helper's queue)
	UPROPERTY() FString Name;
	UPROPERTY() bool bNeedsReview = false;
	int64 OsmId = 0;                 // (a build's: the course on the map)   // (built, its holes guessed: not put in the game)
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
	bool IsWaiting() const { return State == TEXT("waiting"); }
	bool IsActive() const { return IsRunning() || IsWaiting(); }
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
	// ---- the queue: each of these adds a course to it (built, then put in the game; it runs in the
	// background, two builds and one export at a time, and carries on next time if the game closes)
	// Build a course (Name, Location, Group optional), then put it in the game
	void Build(const FString& OsmType, int64 OsmId, const FString& Name, const FString& Location, const FString& Group, bool bPhotoColours = false);
	// Update a built course from OpenStreetMap (kept: the editor's trees, grass and looks)
	void Update(const FString& CourseId);
	// Export a built course to the game's courses folder (about 5-15 minutes)
	void Export(const FString& CourseId, bool bVisible = false);
	void Cancel();                                   // (the latest added: stopped, or taken out)
	void Remove(const FString& QueueId);             // waiting: taken out; going: stopped
	void Retry(const FString& QueueId);              // a failed one, from the step that failed
	void ClearFinished();
	// The latest added (none: Kind empty), every one, how many are waiting or going
	const FOTGHelperJob& GetJob() const { return Job; }
	const TArray<FOTGHelperJob>& GetQueue() const { return Queue; }
	int32 ActiveCount() const { int32 N = 0; for (const FOTGHelperJob& J : Queue) N += J.IsActive() ? 1 : 0; return N; }
	// A course's in the queue, waiting or going (null: none): by its id, or by its OpenStreetMap id
	const FOTGHelperJob* ActiveFor(const FString& CourseId, int64 OsmId = 0) const { for (const FOTGHelperJob& J : Queue) if (J.IsActive() && ((!CourseId.IsEmpty() && J.CourseId == CourseId) || (OsmId && J.OsmId == OsmId))) return &J; return nullptr; }
	// The newest of a course's (null: none)
	const FOTGHelperJob* FindJob(const FString& CourseId) const { for (int32 i = Queue.Num() - 1; i >= 0; i--) if (Queue[i].CourseId == CourseId) return &Queue[i]; return nullptr; }
	// Called when one finishes (done or failed)
	TMulticastDelegate<void(const FOTGHelperJob&)> OnJobDone;

	// The course's source, as the editor reads it: { course, real } (course.json, real.json)
	void LoadSource(const FString& CourseId, TFunction<void(TSharedPtr<FJsonObject> Course, TSharedPtr<FJsonObject> Real, const FString& Error)> Done);
	// Save it (the body the web editor's Save sends: FOTGCourseSource::SaveBody)
	void SaveSource(const FString& CourseId, TSharedRef<FJsonObject> Body, FDone Done);
	// Where the helper keeps the courses' sources (what the editor edits: Documents/OpenTeeGolf/
	// CourseSources, or a development copy's courses folder), and whether a course has one there
	FString SourcesDir() const;
	bool HasSource(const FString& CourseId) const;

	FString StatusLine() const;

private:
	bool Tick(float Dt);
	void Launch();
	void Ping(int32 Attempt);
	void Finish(bool bOk, const FString& Why = FString());
	void PollJob();
	void Enqueue(TSharedRef<FJsonObject> Body);
	FString DataDir() const;
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
	TArray<FOTGHelperJob> Queue;
	FString FocusId;                  // (the latest added: Job)
	TMap<FString, FString> Seen;      // (each one's state and step last time: finishing ones told of)
	bool bPolling = false;
};
