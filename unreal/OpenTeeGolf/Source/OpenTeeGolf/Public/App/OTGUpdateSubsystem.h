// Keeping the game up to date from its GitHub releases, without downloading the whole game again.
//
// A release of the game lists its files - path, size, SHA-1, where to download it - in manifest.json
// (unreal/release_manifest.py writes it, unreal/publish_release.ps1 fills in the addresses). The
// installed game keeps its own copy next to OpenTeeGolf.exe. On start this asks GitHub for the
// newest release tagged unreal-v<version>, reads its manifest, and works out which files differ from
// (or are missing in) the installed game: only those are downloaded, into Update\staging, each
// checked against its SHA-1, and a download cut short carries on where it stopped. A running game
// can't replace its own files, so applying the update writes Update\apply.cmd, starts it, and quits:
// it waits for the game to close, moves the new files into place, and starts the game again.
//
// The release on GitHub is a small starter zip (everything but the tree scenery, whose files are
// too big for one zip there): the installed manifest lists the scenery, the files are missing, and
// the same download fetches them on the first start.
//
// On start the main menu holds a window over everything while this checks (a few seconds at most):
// an update found is offered there - Update now (downloaded with its progress shown, then put in
// place and the game started again) or Not now (the game as it is) - before the game starts.
//
// Testing: -updateurl=<a manifest's address> reads that manifest instead of asking GitHub;
// -updateauto downloads and restarts without asking; -noupdate turns it off. Nothing happens in the editor, or in a build without manifest.json.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "OTGUpdateSubsystem.generated.h"

UENUM()
enum class EOTGUpdateState : uint8
{
	Off,          // not an installed release (the editor, a build without manifest.json), or -noupdate
	Idle,
	Checking,
	UpToDate,
	Available,    // NewVersion, Bytes to download in Files files
	Downloading,  // Done of Bytes
	Ready,        // downloaded and checked: restart to apply
	Failed,       // Message says why
};

UCLASS()
class OPENTEEGOLF_API UOTGUpdateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Ask for the newest release (bAsked: the player pressed the button - failures are shown)
	void Check(bool bAsked = false);
	// Fetch what's Available (bThenRestart: put it in place and start again as soon as it's in)
	void Download(bool bThenRestart = false);
	// Stop a download (what's in so far is kept: the next one carries on from there)
	void Cancel();
	// The start-up window (SOTGMainMenu): still to be shown - checking (a few seconds at most), an
	// update to offer, its download - or done with for this run
	bool StartupWindowOpen();
	void CloseStartupWindow() { bWindowDone = true; }
	// Not now: the menu's update bar left alone too until the game starts again
	void Decline() { bDeclined = true; bWindowDone = true; }
	bool WasDeclined() const { return bDeclined; }
	bool WillRestart() const { return bRestartWhenReady; }
	// Ready: quit, put the new files in place, start again
	void ApplyAndRestart();

	EOTGUpdateState GetState() const { return State; }
	FString InstalledVersion() const { return Installed.Version; }
	FString NewVersion() const { return Remote.Version; }
	int64 BytesToGet() const { return Bytes; }
	int64 BytesDone() const { return Done; }
	int32 FilesToGet() const { return Needed.Num(); }
	// (the scenery isn't there yet: the first start after installing)
	bool IsInstalling() const { return bInstalling; }
	bool WasAsked() const { return bAsked; }
	FString GetMessage() const { return Message; }
	// One line for the menu
	FString StatusLine() const;

private:
	struct FEntry
	{
		FString Path;   // relative to the game's folder, forward slashes
		int64 Size = 0;
		FString Sha1;
		FString Url;
	};
	struct FManifest
	{
		FString Version;
		FString Repo;   // owner/name on GitHub
		TArray<FEntry> Files;
		FString Text;   // as read
	};

	static bool Parse(const FString& Json, FManifest& Out);
	static FString Root();
	static FString Staging();
	static bool Newer(const FString& A, const FString& B);   // A newer than B
	static FString Sha1Of(const FString& File);

	void Fail(const FString& Why);
	void GotManifest(const FString& Json);
	void NextFile(bool bFresh = false);
	void FileDone(bool bOk, int32 Code, bool bResumed);
	void Verified(bool bOk);

	EOTGUpdateState State = EOTGUpdateState::Off;
	FManifest Installed, Remote;
	TArray<FEntry> Needed;
	int32 Index = 0;
	int64 Bytes = 0, Done = 0, FileBase = 0;
	bool bInstalling = false, bAsked = false;
	bool bAuto = false;   // (-updateauto: download and restart without asking - testing)
	bool bRestartWhenReady = false, bWindowDone = false, bDeclined = false;
	double StartedAt = 0;
	FString Message;
	FString OverrideUrl;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Req;
	TSharedPtr<FArchive> Writer;
};
