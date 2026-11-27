// Online games: one player hosts, the others join by the host's address (direct IP: on the same
// network, or over the internet with a port forwarded on the host's router).
//
//  - The host picks the course and the round (the Play page's setup) and whether everyone takes turns
//    or plays at once; the joining players' games check they have the same course, file by file
//    (OTGCourseSync.h). A missing course is downloaded from the host - only the files that aren't
//    here already (the tree models are the same in every course). A different version of it: the
//    player chooses - overwrite theirs, or keep theirs and save the host's beside it as a copy.
//  - Nobody changes a course while they're in an online game (the editors are shut).
//  - The host starts the round once everyone has the course; each game plays it on its own copy,
//    and the shots go to everyone (the round's part FOTGRoundNet: OTGRoundNet.cpp).
//
// The connection: TCP, the host's port (7787 unless changed), messages framed by their length -
// JSON for everything but the course's files, which go as chunks. The host passes the players'
// messages on to the others.
//
// Kept by the game instance, so it lasts from the menu into the round and back.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Async/Future.h"
#include "Misc/SecureHash.h"
#include "Round/OTGPlayers.h"
#include "App/OTGCourseSync.h"   // (Private/App: this module's own)
#include "OTGNetSubsystem.generated.h"

class FSocket;
class FJsonObject;

// A player in the game, as everyone sees them
struct FOTGNetPlayer
{
	int32 Id = 0;
	FString Name, Tee, ProfileId;
	FLinearColor Colour = FLinearColor::White;
	double Handicap = 0;
	bool bLeftHanded = false;
	FString Course = TEXT("checking");   // checking, downloading, choosing, ready, failed
	float Progress = 0;
	bool bConnected = true;
	bool bHost = false;
};

enum class EOTGNetRole : uint8 { None, Host, Client };
enum class EOTGCourseChoice : uint8 { Overwrite, Copy, Leave };

UCLASS()
class OPENTEEGOLF_API UOTGNetSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 DefaultPort = 7787;
	static UOTGNetSubsystem* Get(const UObject* WorldContext);
	// (anywhere: the game's, through the viewport)
	static UOTGNetSubsystem* Get();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---- hosting, joining, leaving
	bool Host(const FOTGRoundSetup& Setup, bool bTogether, const FOTGPlayerProfile& Me, int32 Port, FString& Why);
	void Join(const FString& Address, const FOTGPlayerProfile& Me);
	void Leave(const FString& Why = FString());

	EOTGNetRole Role() const { return NetRole; }
	bool InSession() const { return NetRole != EOTGNetRole::None; }
	bool IsHost() const { return NetRole == EOTGNetRole::Host; }
	bool IsConnecting() const { return NetRole == EOTGNetRole::Client && !bWelcomed; }
	const TArray<FOTGNetPlayer>& Players() const { return PlayerList; }
	int32 LocalId() const { return LocalPlayerId; }
	const FOTGNetPlayer* Player(int32 Id) const;
	bool IsConnected(int32 Id) const;   // (here and playing: false once they've left)
	const FOTGRoundSetup& GameSetup() const { return Setup; }
	bool Together() const { return bTogether; }
	FString CourseName() const { return HostCourseName; }
	FString HostName() const;
	int32 Port() const { return HostPort; }
	TArray<FString> Addresses() const;  // (this computer's, to give the others)
	FString Status() const { return StatusLine; }
	FString LastError() const { return ErrorLine; }
	int32 Revision() const { return Rev; }   // (changes whenever anything the menu shows does)

	// ---- the course
	FString LocalCourseId() const { return LocalCourse; }   // (the host's course here: empty until it is)
	bool CourseReady() const { return !LocalCourse.IsEmpty(); }
	bool NeedsCourseChoice() const { return SyncState == ESync::Choosing; }
	FString OwnCourseName() const { return OwnName; }       // (yours, the version that's different)
	FString CopyName() const;                                // (what the host's would be called beside it)
	void ChooseCourse(EOTGCourseChoice Choice);
	int64 DownloadTotal() const { return DlTotal; }
	int64 DownloadDone() const { return DlDone; }

	// ---- the round
	bool CanStart(FString& Why) const;
	void StartRound();                  // (the host: everyone into the round)
	bool IsRoundOn() const { return bRoundOn; }
	void RoundOver();                   // (the round's finished: back to the lobby)
	// The round's messages: sent to everyone (the host passes a player's on); and those that came,
	// in order, kept until the round takes them
	void SendGame(const TSharedRef<FJsonObject>& Msg);
	bool PopGame(TSharedPtr<FJsonObject>& Out);

private:
	// One end of a connection: what's come in and what's waiting to go
	struct FPeer
	{
		~FPeer();
		FSocket* Sock = nullptr;
		TArray<uint8> In, Out;
		int32 InAt = 0, OutAt = 0;
		int32 Player = 0;            // (the host: which player, once they've said hello)
		bool bDead = false, bCloseWhenSent = false;
		// (the host: the course's files they asked for, sent a chunk at a time)
		TArray<int32> Want;
		int32 WantAt = 0;
		TUniquePtr<FArchive> Reader;
		int64 ReadAt = 0;
	};
	enum class ESync : uint8 { Idle, Checking, Choosing, Preparing, Downloading, Done, Failed };

	bool Tick(float Dt);
	void Changed() { Rev++; }
	void Fail(const FString& Why) { Leave(Why); }

	// the connection
	void Pump(FPeer& P);
	void Read(FPeer& P);
	void Send(FPeer& P, const TSharedRef<FJsonObject>& Msg);
	void SendAll(const TSharedRef<FJsonObject>& Msg, const FPeer* Except = nullptr);
	void Handle(FPeer& P, const uint8* Data, int32 N);
	void HandleJson(FPeer& P, const TSharedRef<FJsonObject>& Msg);
	void HandleChunk(const uint8* Data, int32 N);
	bool FillChunks(FPeer& P);   // (the host: the next of the files a player asked for)
	void Connected(FSocket* S, const FString& Why);
	void DropPeer(FPeer& P, const FString& Why);

	// the lobby
	TSharedRef<FJsonObject> PlayersJson() const;
	void ReadPlayers(const TSharedPtr<FJsonObject>& Msg);
	TSharedRef<FJsonObject> Welcome(int32 Id) const;
	void SendStatus();
	void BeginRound(int32 Seed);

	// the course here: checked, then made the host's (copied, downloaded)
	void CheckCourse();
	void Prepare(const FString& Target, bool bPrune, bool bCopy);
	void Downloaded();
	void CourseDone(const FString& Id);
	void CourseFailed(const FString& Why);
	void LocalStatus(const FString& State, float Progress);

	EOTGNetRole NetRole = EOTGNetRole::None;
	FTSTicker::FDelegateHandle TickHandle;
	FSocket* Listener = nullptr;
	TArray<TUniquePtr<FPeer>> Peers;   // (the host: the players; a player: the host)
	int32 Generation = 0;              // (a connect's answer for a session since left is dropped)
	int32 NextId = 2;
	int32 LocalPlayerId = 0;
	int32 HostPort = DefaultPort;
	bool bWelcomed = false;
	bool bRoundOn = false;
	bool bTogether = false;
	FOTGPlayerProfile Me;
	FOTGRoundSetup Setup;
	TArray<FOTGNetPlayer> PlayerList;
	FString StatusLine, ErrorLine;
	int32 Rev = 0;
	TArray<TSharedPtr<FJsonObject>> GameIn;

	// the course: the host's id, name and files
	FString HostCourseId, HostCourseName, HostCourseDir;
	TArray<otg::sync::FFile> Files;
	TFuture<TArray<otg::sync::FFile>> HostManifest;
	bool bManifestReady = false;
	TArray<int32> WaitingWelcome;      // (players who said hello before the files were counted)

	// the course here
	ESync SyncState = ESync::Idle;
	FString LocalCourse, OwnDir, OwnName, UpdateDir;
	struct FCheckResult { FString UseId, OwnDir, OwnName, UpdateDir; };
	TFuture<FCheckResult> CheckTask;
	struct FPrepResult { bool bOk = false; FString Why; TArray<int32> Download; int64 Bytes = 0; TArray<FString> Remove; int32 Kept = 0, Copied = 0; };
	TFuture<FPrepResult> PrepTask;
	FString Target;
	bool bTargetCopy = false;
	TArray<FString> RemoveAfter;
	TArray<int32> DlQueue;
	int32 DlAt = 0;
	int64 DlTotal = 0, DlDone = 0, FileGot = 0;
	TUniquePtr<FArchive> DlWriter;
	TUniquePtr<class FSHA1> DlSha;
	double StatusSentAt = 0;
};
