// See OTGUpdateSubsystem.h.

#include "App/OTGUpdateSubsystem.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGUpdate, Log, All);

namespace
{
	const TCHAR* Agent = TEXT("OpenTeeGolf-Updater");

	FString Size(int64 B)
	{
		const double Mb = B / (1024.0 * 1024.0);
		return Mb >= 1024 ? FString::Printf(TEXT("%.1f GB"), Mb / 1024) : FString::Printf(TEXT("%.0f MB"), FMath::Max(1.0, Mb));
	}
}

FString UOTGUpdateSubsystem::Root()
{
	// (testing: -updateroot=<a folder> stands in for the installed game's)
	FString R;
	if (!FParse::Value(FCommandLine::Get(), TEXT("updateroot="), R)) R = FPaths::ConvertRelativePathToFull(FPaths::RootDir());
	FPaths::NormalizeDirectoryName(R);
	return R;
}

FString UOTGUpdateSubsystem::Staging() { return Root() / TEXT("Update/staging"); }

void UOTGUpdateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if ((GIsEditor && !FParse::Param(FCommandLine::Get(), TEXT("game"))) || FParse::Param(FCommandLine::Get(), TEXT("noupdate"))) return;
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *(Root() / TEXT("manifest.json"))) || !Parse(Text, Installed))
	{
		UE_LOG(LogOTGUpdate, Display, TEXT("Updates off: no manifest.json in %s (not an installed release)"), *Root());
		return;
	}
	FParse::Value(FCommandLine::Get(), TEXT("updateurl="), OverrideUrl);
	bAuto = FParse::Param(FCommandLine::Get(), TEXT("updateauto"));
	State = EOTGUpdateState::Idle;
	StartedAt = FPlatformTime::Seconds();
	UE_LOG(LogOTGUpdate, Display, TEXT("OpenTeeGolf %s, %d files (%s)"), *Installed.Version, Installed.Files.Num(), OverrideUrl.IsEmpty() ? *Installed.Repo : *OverrideUrl);
	Check(false);
}

void UOTGUpdateSubsystem::Deinitialize()
{
	if (Req.IsValid()) Req->CancelRequest();
	Req.Reset();
	if (Writer.IsValid()) Writer->Close();
	Writer.Reset();
	Super::Deinitialize();
}

bool UOTGUpdateSubsystem::Parse(const FString& Json, FManifest& Out)
{
	TSharedPtr<FJsonObject> O;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), O) || !O.IsValid()) return false;
	Out = FManifest();
	Out.Text = Json;
	Out.Version = O->GetStringField(TEXT("version"));
	O->TryGetStringField(TEXT("repo"), Out.Repo);
	const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
	if (!O->TryGetArrayField(TEXT("files"), Files)) return false;
	for (const TSharedPtr<FJsonValue>& V : *Files)
	{
		const TSharedPtr<FJsonObject> F = V->AsObject();
		if (!F.IsValid()) continue;
		FEntry E;
		E.Path = F->GetStringField(TEXT("path"));
		E.Size = int64(F->GetNumberField(TEXT("size")));
		E.Sha1 = F->GetStringField(TEXT("sha1")).ToLower();
		F->TryGetStringField(TEXT("url"), E.Url);
		// (never outside the game's folder)
		if (E.Path.IsEmpty() || E.Path.Contains(TEXT("..")) || E.Path.Contains(TEXT(":")) || E.Path.StartsWith(TEXT("/"))) continue;
		Out.Files.Add(E);
	}
	return !Out.Version.IsEmpty();
}

bool UOTGUpdateSubsystem::Newer(const FString& A, const FString& B)
{
	TArray<FString> Pa, Pb;
	A.ParseIntoArray(Pa, TEXT("."));
	B.ParseIntoArray(Pb, TEXT("."));
	for (int32 i = 0; i < FMath::Max(Pa.Num(), Pb.Num()); i++)
	{
		const int32 x = Pa.IsValidIndex(i) ? FCString::Atoi(*Pa[i]) : 0, y = Pb.IsValidIndex(i) ? FCString::Atoi(*Pb[i]) : 0;
		if (x != y) return x > y;
	}
	return false;
}

FString UOTGUpdateSubsystem::Sha1Of(const FString& File)
{
	TUniquePtr<FArchive> R(IFileManager::Get().CreateFileReader(*File));
	if (!R) return FString();
	FSHA1 H;
	TArray<uint8> Buf;
	Buf.SetNumUninitialized(4 << 20);
	int64 Left = R->TotalSize();
	while (Left > 0)
	{
		const int64 N = FMath::Min<int64>(Left, Buf.Num());
		R->Serialize(Buf.GetData(), N);
		H.Update(Buf.GetData(), uint64(N));
		Left -= N;
	}
	H.Final();
	uint8 D[20];
	H.GetHash(D);
	return BytesToHex(D, 20).ToLower();
}

void UOTGUpdateSubsystem::Fail(const FString& Why)
{
	Message = Why;
	State = EOTGUpdateState::Failed;
	UE_LOG(LogOTGUpdate, Warning, TEXT("Update: %s"), *Why);
	if (Writer.IsValid()) Writer->Close();
	Writer.Reset();
	Req.Reset();
}

void UOTGUpdateSubsystem::Check(bool bUserAsked)
{
	if (State == EOTGUpdateState::Off || State == EOTGUpdateState::Checking || State == EOTGUpdateState::Downloading || State == EOTGUpdateState::Ready) return;
	bAsked = bUserAsked;
	State = EOTGUpdateState::Checking;
	Message.Empty();
	TWeakObjectPtr<UOTGUpdateSubsystem> Self(this);
	auto Get = [Self](const FString& Url, TFunction<void(int32, const FString&)> Then) {
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> R = FHttpModule::Get().CreateRequest();
		R->SetURL(Url);
		R->SetVerb(TEXT("GET"));
		R->SetHeader(TEXT("User-Agent"), Agent);
		R->SetHeader(TEXT("Accept"), TEXT("application/vnd.github+json"));
		R->SetTimeout(30);
		R->OnProcessRequestComplete().BindLambda([Self, Then](FHttpRequestPtr, FHttpResponsePtr Res, bool bOk) {
			if (!Self.IsValid()) return;
			Then(bOk && Res.IsValid() ? Res->GetResponseCode() : 0, bOk && Res.IsValid() ? Res->GetContentAsString() : FString());
		});
		R->ProcessRequest();
		Self->Req = R;
	};
	if (!OverrideUrl.IsEmpty())
	{
		Get(OverrideUrl, [Self](int32 Code, const FString& Body) {
			if (Code != 200) { Self->Fail(FString::Printf(TEXT("couldn't read %s (%d)"), *Self->OverrideUrl, Code)); return; }
			Self->GotManifest(Body);
		});
		return;
	}
	if (Installed.Repo.IsEmpty()) { Fail(TEXT("this copy doesn't say where its releases are")); return; }
	// the newest release of the game (the web version's releases share the repository: tags unreal-v*)
	Get(FString::Printf(TEXT("https://api.github.com/repos/%s/releases?per_page=30"), *Installed.Repo), [Self, Get](int32 Code, const FString& Body) {
		if (Code != 200) { Self->Fail(Code ? FString::Printf(TEXT("GitHub answered %d"), Code) : FString(TEXT("no connection to GitHub"))); return; }
		TArray<TSharedPtr<FJsonValue>> List;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), List)) { Self->Fail(TEXT("GitHub's answer didn't read")); return; }
		for (const TSharedPtr<FJsonValue>& V : List)
		{
			const TSharedPtr<FJsonObject> R = V->AsObject();
			if (!R.IsValid() || R->GetBoolField(TEXT("draft")) || R->GetBoolField(TEXT("prerelease")) || !R->GetStringField(TEXT("tag_name")).StartsWith(TEXT("unreal-v"))) continue;
			const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
			if (!R->TryGetArrayField(TEXT("assets"), Assets)) continue;
			for (const TSharedPtr<FJsonValue>& A : *Assets)
				if (A->AsObject()->GetStringField(TEXT("name")) == TEXT("manifest.json"))
				{
					Get(A->AsObject()->GetStringField(TEXT("browser_download_url")), [Self](int32 C2, const FString& B2) {
						if (C2 != 200) { Self->Fail(FString::Printf(TEXT("the release's file list didn't download (%d)"), C2)); return; }
						Self->GotManifest(B2);
					});
					return;
				}
		}
		// (no release of the Unreal game yet: what's installed is all there is)
		Self->GotManifest(Self->Installed.Text);
	});
}

void UOTGUpdateSubsystem::GotManifest(const FString& Json)
{
	Req.Reset();
	FManifest R;
	if (!Parse(Json, R)) { Fail(TEXT("the release's file list didn't read")); return; }
	// (an older release than this one: nothing to do; the same: only what's missing)
	if (Newer(Installed.Version, R.Version)) R = Installed;
	Remote = MoveTemp(R);
	Needed.Reset();
	Bytes = 0;
	bInstalling = false;
	for (const FEntry& E : Remote.Files)
	{
		const FEntry* Mine = Installed.Files.FindByPredicate([&](const FEntry& X) { return X.Path == E.Path; });
		const int64 Have = IFileManager::Get().FileSize(*(Root() / E.Path));
		if (Mine && Mine->Sha1 == E.Sha1 && Have == E.Size) continue;
		if (Have < 0 && Mine && Mine->Sha1 == E.Sha1) bInstalling = true;   // (listed here, never downloaded: the scenery)
		Needed.Add(E);
		Bytes += E.Size;
	}
	if (!Needed.Num())
	{
		State = EOTGUpdateState::UpToDate;
		UE_LOG(LogOTGUpdate, Display, TEXT("Up to date (%s)"), *Installed.Version);
		return;
	}
	State = EOTGUpdateState::Available;
	UE_LOG(LogOTGUpdate, Display, TEXT("Update: %s, %d files, %s%s"), *Remote.Version, Needed.Num(), *Size(Bytes), bInstalling ? TEXT(" (installing)") : TEXT(""));
	// (installing: the game isn't whole yet - fetch it straight away)
	if (bInstalling || bAuto) Download(bInstalling && !bWindowDone);
}

void UOTGUpdateSubsystem::Download(bool bThenRestart)
{
	if (State != EOTGUpdateState::Available && State != EOTGUpdateState::Failed) return;
	if (!Needed.Num()) return;
	bRestartWhenReady = bThenRestart;
	State = EOTGUpdateState::Downloading;
	Message.Empty();
	Index = 0;
	FileBase = Done = 0;
	NextFile();
}

void UOTGUpdateSubsystem::NextFile(bool bFresh)
{
	if (Index >= Needed.Num())
	{
		FFileHelper::SaveStringToFile(Remote.Text, *(Staging() / TEXT("manifest.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		State = EOTGUpdateState::Ready;
		UE_LOG(LogOTGUpdate, Display, TEXT("Update %s downloaded: restart to apply"), *Remote.Version);
		if (bAuto || bRestartWhenReady) ApplyAndRestart();
		return;
	}
	const FEntry& E = Needed[Index];
	const FString Dest = Staging() / E.Path;
	int64 Have = IFileManager::Get().FileSize(*Dest);
	if (bFresh || Have > E.Size) { IFileManager::Get().Delete(*Dest, false, true, true); Have = -1; }
	TWeakObjectPtr<UOTGUpdateSubsystem> Self(this);
	if (Have == E.Size)
	{
		// (there already, from a download cut short: just checked)
		Done = FileBase + E.Size;
		const FString Want = E.Sha1;
		Async(EAsyncExecution::ThreadPool, [Self, Dest, Want]() {
			const bool bOk = Sha1Of(Dest) == Want;
			AsyncTask(ENamedThreads::GameThread, [Self, bOk]() { if (Self.IsValid()) Self->Verified(bOk); });
		});
		return;
	}
	if (E.Url.IsEmpty()) { Fail(FString::Printf(TEXT("no address to download %s from"), *E.Path)); return; }
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Dest), true);
	const int64 From = FMath::Max<int64>(Have, 0);
	Writer = MakeShareable(IFileManager::Get().CreateFileWriter(*Dest, From > 0 ? FILEWRITE_Append : 0));
	if (!Writer.IsValid()) { Fail(FString::Printf(TEXT("can't write in %s"), *Staging())); return; }
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> R = FHttpModule::Get().CreateRequest();
	R->SetURL(E.Url);
	R->SetVerb(TEXT("GET"));
	R->SetHeader(TEXT("User-Agent"), Agent);
	if (From > 0) R->SetHeader(TEXT("Range"), FString::Printf(TEXT("bytes=%lld-"), From));
	R->SetTimeout(0);              // (a big file takes as long as it takes...)
	R->SetActivityTimeout(60);     // (...as long as it keeps coming)
	R->SetResponseBodyReceiveStream(Writer.ToSharedRef());
	const int64 Base = FileBase + From;
	R->OnRequestProgress64().BindLambda([Self, Base](FHttpRequestPtr, uint64, uint64 Got) { if (Self.IsValid()) Self->Done = Base + int64(Got); });
	R->OnProcessRequestComplete().BindLambda([Self, From](FHttpRequestPtr, FHttpResponsePtr Res, bool bOk) {
		if (Self.IsValid()) Self->FileDone(bOk && Res.IsValid(), Res.IsValid() ? Res->GetResponseCode() : 0, From > 0);
	});
	Req = R;
	UE_LOG(LogOTGUpdate, Display, TEXT("Downloading %s (%s%s)"), *E.Path, *Size(E.Size), From > 0 ? *FString::Printf(TEXT(", from %s"), *Size(From)) : TEXT(""));
	R->ProcessRequest();
}

void UOTGUpdateSubsystem::Cancel()
{
	if (State != EOTGUpdateState::Downloading) return;
	UE_LOG(LogOTGUpdate, Display, TEXT("Download stopped at %lld of %lld bytes"), Done, Bytes);
	State = EOTGUpdateState::Available;
	bRestartWhenReady = false;
	if (Req.IsValid()) Req->CancelRequest();
	Req.Reset();
	if (Writer.IsValid()) Writer->Close();
	Writer.Reset();
}

bool UOTGUpdateSubsystem::StartupWindowOpen()
{
	if (bWindowDone) return false;
	switch (State)
	{
	// (checking: only a few seconds - a slow answer turns up on the menu's update bar instead)
	case EOTGUpdateState::Idle:
	case EOTGUpdateState::Checking: if (FPlatformTime::Seconds() - StartedAt < 8.0) return true; break;
	case EOTGUpdateState::Available:
	case EOTGUpdateState::Downloading:
	case EOTGUpdateState::Ready: return true;
	// (a download from the window that failed: said there, with Try again)
	case EOTGUpdateState::Failed: if (Done > 0 || bRestartWhenReady) return true; break;
	default: break;
	}
	bWindowDone = true;
	return false;
}

void UOTGUpdateSubsystem::FileDone(bool bOk, int32 Code, bool bResumed)
{
	if (Writer.IsValid()) Writer->Close();
	Writer.Reset();
	Req.Reset();
	if (State != EOTGUpdateState::Downloading) return;
	const FEntry& E = Needed[Index];
	const FString Dest = Staging() / E.Path;
	if (bResumed && Code == 200) { NextFile(true); return; }   // (the server sent it all again: start it over)
	if (!bOk || (Code != 200 && Code != 206 && Code != 416))
	{
		Fail(Code ? FString::Printf(TEXT("%s didn't download (%d)"), *FPaths::GetCleanFilename(E.Path), Code) : FString(TEXT("the connection dropped: try again (it carries on where it stopped)")));
		return;
	}
	TWeakObjectPtr<UOTGUpdateSubsystem> Self(this);
	const FString Want = E.Sha1;
	Async(EAsyncExecution::ThreadPool, [Self, Dest, Want]() {
		const bool bGood = Sha1Of(Dest) == Want;
		AsyncTask(ENamedThreads::GameThread, [Self, bGood]() { if (Self.IsValid()) Self->Verified(bGood); });
	});
}

void UOTGUpdateSubsystem::Verified(bool bOk)
{
	if (State != EOTGUpdateState::Downloading) return;
	const FEntry& E = Needed[Index];
	if (!bOk)
	{
		IFileManager::Get().Delete(*(Staging() / E.Path), false, true, true);
		Fail(FString::Printf(TEXT("%s didn't download correctly: try again"), *FPaths::GetCleanFilename(E.Path)));
		return;
	}
	FileBase += E.Size;
	Done = FileBase;
	Index++;
	NextFile();
}

void UOTGUpdateSubsystem::ApplyAndRestart()
{
	if (State != EOTGUpdateState::Ready) return;
	// apply.cmd: wait for the game to close, move each new file into place (trying for half a minute
	// if one's still held open), the new file list last - only if every file went in, so a failed
	// update is found again next time - then start the game
	const FString R = Root().Replace(TEXT("/"), TEXT("\\"));
	const uint32 Pid = FPlatformProcess::GetCurrentProcessId();
	TArray<FString> L;
	L.Add(TEXT("@echo off"));
	L.Add(TEXT("setlocal"));
	L.Add(FString::Printf(TEXT("cd /d \"%s\""), *R));
	L.Add(TEXT("set failed=0"));
	L.Add(TEXT(":wait"));
	L.Add(FString::Printf(TEXT("tasklist /FI \"PID eq %u\" 2>nul | find \"%u\" >nul"), Pid, Pid));
	L.Add(TEXT("if not errorlevel 1 ( ping -n 2 127.0.0.1 >nul & goto wait )"));
	L.Add(TEXT("ping -n 3 127.0.0.1 >nul"));
	for (const FEntry& E : Needed)
	{
		const FString P = E.Path.Replace(TEXT("/"), TEXT("\\"));
		const FString Dir = FPaths::GetPath(E.Path).Replace(TEXT("/"), TEXT("\\"));
		if (!Dir.IsEmpty()) L.Add(FString::Printf(TEXT("if not exist \"%s\" mkdir \"%s\""), *Dir, *Dir));
		L.Add(FString::Printf(TEXT("call :put \"Update\\staging\\%s\" \"%s\""), *P, *P));
	}
	L.Add(TEXT("if \"%failed%\"==\"0\" move /y \"Update\\staging\\manifest.json\" \"manifest.json\" >nul"));
	L.Add(TEXT("if \"%failed%\"==\"0\" rmdir /s /q \"Update\\staging\""));
	L.Add(TEXT("echo %date% %time% done, failed=%failed%"));
	// (started again as it was: the same options)
	const FString Again = FString(FCommandLine::Get()).Replace(TEXT("%"), TEXT("%%")).TrimStartAndEnd();
	L.Add(FString::Printf(TEXT("start \"\" \"%s\\OpenTeeGolf.exe\" %s"), *R, *Again));
	L.Add(TEXT("exit /b 0"));
	L.Add(TEXT(":put"));
	L.Add(TEXT("set n=0"));
	L.Add(TEXT(":retry"));
	L.Add(TEXT("move /y %1 %2 >nul 2>&1 && exit /b 0"));
	L.Add(TEXT("set /a n+=1"));
	L.Add(TEXT("if %n% geq 30 ( echo could not replace %2 & set failed=1 & exit /b 1 )"));
	L.Add(TEXT("ping -n 2 127.0.0.1 >nul"));
	L.Add(TEXT("goto retry"));
	const FString Cmd = Root() / TEXT("Update/apply.cmd");
	if (!FFileHelper::SaveStringToFile(FString::Join(L, TEXT("\r\n")) + TEXT("\r\n"), *Cmd, FFileHelper::EEncodingOptions::ForceAnsi))
	{
		Fail(TEXT("couldn't write the update step"));
		return;
	}
	const FString Args = FString::Printf(TEXT("/c \"\"%s\" > \"%s\\Update\\apply.log\" 2>&1\""), *Cmd.Replace(TEXT("/"), TEXT("\\")), *R);
	FProcHandle H = FPlatformProcess::CreateProc(TEXT("cmd.exe"), *Args, true, true, true, nullptr, 0, nullptr, nullptr);
	if (!H.IsValid()) { Fail(TEXT("couldn't start the update step")); return; }
	UE_LOG(LogOTGUpdate, Display, TEXT("Applying %s: restarting"), *Remote.Version);
	FPlatformMisc::RequestExit(false, TEXT("OpenTeeGolf update"));
}

FString UOTGUpdateSubsystem::StatusLine() const
{
	switch (State)
	{
	case EOTGUpdateState::Off: return TEXT("Updates work in the installed game (from its release zip).");
	case EOTGUpdateState::Idle: return FString::Printf(TEXT("OpenTeeGolf %s"), *Installed.Version);
	case EOTGUpdateState::Checking: return TEXT("Checking for updates...");
	case EOTGUpdateState::UpToDate: return FString::Printf(TEXT("OpenTeeGolf %s is up to date."), *Installed.Version);
	case EOTGUpdateState::Available:
		return bInstalling ? FString::Printf(TEXT("The scenery isn't downloaded yet (%s)."), *Size(Bytes))
			: FString::Printf(TEXT("Update available: OpenTeeGolf %s  ·  %s"), *Remote.Version, *Size(Bytes));
	case EOTGUpdateState::Downloading:
		return FString::Printf(TEXT("%s  %d%%  (%s of %s)"), bInstalling ? TEXT("Downloading the scenery...") : TEXT("Downloading the update..."),
			Bytes > 0 ? int32(100 * Done / Bytes) : 0, *Size(Done), *Size(Bytes));
	case EOTGUpdateState::Ready: return bInstalling ? TEXT("The scenery is downloaded: restart to finish installing.") : FString::Printf(TEXT("OpenTeeGolf %s is downloaded: restart to finish updating."), *Remote.Version);
	case EOTGUpdateState::Failed: return FString::Printf(TEXT("Couldn't update: %s"), *Message);
	}
	return FString();
}
