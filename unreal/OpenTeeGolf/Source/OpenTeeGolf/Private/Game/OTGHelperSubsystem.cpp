#include "Game/OTGHelperSubsystem.h"

#include "OTGCourseSubsystem.h"
#include "Game/OTGCourseSource.h"
#include "Engine/GameInstance.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Kismet/GameplayStatics.h"
#include "Async/Async.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGHelper, Log, All);

namespace
{
	// The helper's own log (Saved/Logs/helper.log): a Shipping build writes no other, and this is
	// where a course helper that won't start says why
	void HelperLog(const FString& Line)
	{
		static bool bFresh = true;
		const FString File = FPaths::Combine(FPaths::ProjectLogDir(), TEXT("helper.log"));
		const FString Text = FDateTime::Now().ToString(TEXT("%H:%M:%S ")) + Line + LINE_TERMINATOR;
		FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), bFresh ? 0 : FILEWRITE_Append);
		bFresh = false;
	}
	// (on the disk itself: a Shipping build's file system is the pak files', which may not see loose ones)
	bool OnDisk(const FString& File) { return IPlatformFile::GetPlatformPhysical().FileExists(*File); }

	// A request to the helper on this computer, over a plain socket: Unreal's HTTP module may refuse
	// http:// (not https) in a Shipping build. Blocking: call it off the game thread.
	struct FLocalReply { int32 Code = 0; FString Type, Body, Error; };
	FLocalReply LocalHttp(int32 Port, const FString& Verb, const FString& Path, const FString& Body, double TimeoutS)
	{
		FLocalReply R;
		ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!SS) { R.Error = TEXT("no sockets"); return R; }
		FSocket* S = SS->CreateSocket(NAME_Stream, TEXT("OTG course helper"), false);
		if (!S) { R.Error = TEXT("no socket"); return R; }
		ON_SCOPE_EXIT { S->Close(); SS->DestroySocket(S); };
		const TSharedRef<FInternetAddr> A = SS->CreateInternetAddr();
		bool bValid = false;
		A->SetIp(TEXT("127.0.0.1"), bValid);
		A->SetPort(Port);
		if (!S->Connect(*A)) { R.Error = TEXT("can't connect"); return R; }
		const FTCHARToUTF8 B(*Body);
		const FString Head = FString::Printf(TEXT("%s %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n"), *Verb, *Path, Port, B.Length());
		const FTCHARToUTF8 H(*Head);
		TArray<uint8> Out;
		Out.Append((const uint8*)H.Get(), H.Length());
		Out.Append((const uint8*)B.Get(), B.Length());
		for (int32 Sent = 0; Sent < Out.Num();)
		{
			int32 N = 0;
			if (!S->Send(Out.GetData() + Sent, Out.Num() - Sent, N) || N <= 0) { R.Error = TEXT("send failed"); return R; }
			Sent += N;
		}
		// (the answer: all of it, until the helper closes the connection)
		TArray<uint8> In;
		const double End = FPlatformTime::Seconds() + TimeoutS;
		uint8 Buf[65536];
		for (;;)
		{
			if (FPlatformTime::Seconds() > End) { R.Error = TEXT("timed out"); return R; }
			if (!S->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(1))) continue;
			int32 N = 0;
			if (!S->Recv(Buf, sizeof(Buf), N) || N <= 0) break;
			In.Append(Buf, N);
		}
		int32 HeadEnd = INDEX_NONE;
		for (int32 i = 0; i + 3 < In.Num(); i++) if (In[i] == '\r' && In[i + 1] == '\n' && In[i + 2] == '\r' && In[i + 3] == '\n') { HeadEnd = i; break; }
		if (HeadEnd == INDEX_NONE) { R.Error = TEXT("no answer"); return R; }
		const FString Headers(FUTF8ToTCHAR((const ANSICHAR*)In.GetData(), HeadEnd).Get(), FUTF8ToTCHAR((const ANSICHAR*)In.GetData(), HeadEnd).Length());
		TArray<FString> Lines;
		Headers.ParseIntoArrayLines(Lines);
		if (Lines.Num()) { TArray<FString> P; Lines[0].ParseIntoArrayWS(P); if (P.Num() > 1) R.Code = FCString::Atoi(*P[1]); }
		bool bChunked = false;
		for (const FString& L : Lines)
		{
			if (L.StartsWith(TEXT("Content-Type:"), ESearchCase::IgnoreCase)) R.Type = L.Mid(13).TrimStartAndEnd();
			if (L.StartsWith(TEXT("Transfer-Encoding:"), ESearchCase::IgnoreCase) && L.Contains(TEXT("chunked"), ESearchCase::IgnoreCase)) bChunked = true;
		}
		TArray<uint8> Data(In.GetData() + HeadEnd + 4, In.Num() - HeadEnd - 4);
		if (bChunked)
		{
			TArray<uint8> Plain;
			int32 i = 0;
			while (i < Data.Num())
			{
				int32 E = i;
				while (E + 1 < Data.Num() && !(Data[E] == '\r' && Data[E + 1] == '\n')) E++;
				const FString Hex(E - i, (const ANSICHAR*)Data.GetData() + i);
				const int32 Len = FParse::HexNumber(*Hex);
				if (Len <= 0) break;
				i = E + 2;
				Plain.Append(Data.GetData() + i, FMath::Min(Len, Data.Num() - i));
				i += Len + 2;
			}
			Data = MoveTemp(Plain);
		}
		const FUTF8ToTCHAR Conv((const ANSICHAR*)Data.GetData(), Data.Num());
		R.Body = FString(Conv.Length(), Conv.Get());
		return R;
	}
}

namespace
{
	UOTGHelperSubsystem* HelperOf(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UOTGHelperSubsystem>() : nullptr;
	}
	FString Short(const FString& S, int32 N = 600) { return S.Len() > N ? S.Left(N) + TEXT("…") : S; }
	FString JsonText(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid()) return TEXT("(none)");
		FString Out;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(V, FString(), W);
		return Out;
	}
	FString Arg(const TArray<FString>& A, int32 I) { return A.IsValidIndex(I) ? A[I] : FString(); }

	using FCmd = FConsoleCommandWithWorldAndArgsDelegate;
	FAutoConsoleCommandWithWorldAndArgs StartCmd(TEXT("OTG.Helper.Start"), TEXT("Start the course helper (the server that builds courses)"),
		FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) { if (UOTGHelperSubsystem* H = HelperOf(W)) H->Start(); }));
	FAutoConsoleCommandWithWorldAndArgs StatusCmd(TEXT("OTG.Helper.Status"), TEXT("The course helper's state, its job and its last output"),
		FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) {
			if (UOTGHelperSubsystem* H = HelperOf(W))
			{
				UE_LOG(LogOTGHelper, Display, TEXT("%s"), *H->StatusLine());
				const TArray<FString>& O = H->RecentOutput();
				for (int32 i = FMath::Max(0, O.Num() - 15); i < O.Num(); i++) UE_LOG(LogOTGHelper, Display, TEXT("  | %s"), *O[i]);
			}
		}));
	FAutoConsoleCommandWithWorldAndArgs SearchCmd(TEXT("OTG.Helper.Search"), TEXT("OTG.Helper.Search <words>: courses on the map"),
		FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
			if (UOTGHelperSubsystem* H = HelperOf(W)) H->Search(FString::Join(A, TEXT(" ")), [](int32 Code, TSharedPtr<FJsonValue> J, const FString& Body) {
				UE_LOG(LogOTGHelper, Display, TEXT("Search (%d): %s"), Code, *Short(Body, 3000));
			});
		}));
	FAutoConsoleCommandWithWorldAndArgs CheckCmd(TEXT("OTG.Helper.Check"), TEXT("OTG.Helper.Check <way|relation> <id>: what's mapped"),
		FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
			if (UOTGHelperSubsystem* H = HelperOf(W)) H->Check(Arg(A, 0), FCString::Atoi64(*Arg(A, 1)), [](int32 Code, TSharedPtr<FJsonValue> J, const FString& Body) {
				UE_LOG(LogOTGHelper, Display, TEXT("Check (%d): %s"), Code, *Short(Body, 3000));
			});
		}));
	FAutoConsoleCommandWithWorldAndArgs BuildCmd(TEXT("OTG.Helper.Build"), TEXT("OTG.Helper.Build <way|relation> <id> [name]: build a course"),
		FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
			if (UOTGHelperSubsystem* H = HelperOf(W)) H->Build(Arg(A, 0), FCString::Atoi64(*Arg(A, 1)), A.Num() > 2 ? FString::Join(TArray<FString>(A.GetData() + 2, A.Num() - 2), TEXT(" ")) : FString(), FString(), FString());
		}));
	FAutoConsoleCommandWithWorldAndArgs ExportCmd(TEXT("OTG.Helper.Export"), TEXT("OTG.Helper.Export <course id> [visible]: export a built course to the game's courses folder"),
		FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) { if (UOTGHelperSubsystem* H = HelperOf(W)) H->Export(Arg(A, 0), Arg(A, 1) == TEXT("visible")); }));
	FAutoConsoleCommandWithWorldAndArgs CancelCmd(TEXT("OTG.Helper.Cancel"), TEXT("Stop the export going on"),
		FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) { if (UOTGHelperSubsystem* H = HelperOf(W)) H->Cancel(); }));
	FAutoConsoleCommandWithWorldAndArgs LoadCmd(TEXT("OTG.Helper.Load"), TEXT("OTG.Helper.Load <course id> [save]: read the course's source (and save it again unchanged)"),
		FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
			UOTGHelperSubsystem* H = HelperOf(W);
			if (!H) return;
			const FString Id = Arg(A, 0);
			const bool bSave = Arg(A, 1) == TEXT("save");
			TWeakObjectPtr<UOTGHelperSubsystem> Weak(H);
			H->LoadSource(Id, [Weak, Id, bSave](TSharedPtr<FJsonObject> Course, TSharedPtr<FJsonObject> Real, const FString& Error) {
				if (!Error.IsEmpty()) { UE_LOG(LogOTGHelper, Warning, TEXT("Load %s: %s"), *Id, *Error); return; }
				FOTGCourseSource S;
				FString Why;
				if (!S.Load(Id, Course, Real, &Why)) { UE_LOG(LogOTGHelper, Warning, TEXT("Load %s: %s"), *Id, *Why); return; }
				UE_LOG(LogOTGHelper, Display, TEXT("%s"), *S.Summary());
				if (bSave && Weak.IsValid())
					Weak->SaveSource(Id, S.SaveBody(), [Id](int32 Code, TSharedPtr<FJsonValue>, const FString& Body) {
						UE_LOG(LogOTGHelper, Display, TEXT("Saved %s (%d): %s"), *Id, Code, *Short(Body));
					});
			});
		}));
}

void UOTGHelperSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UOTGHelperSubsystem::Tick), 0.1f);
	// (-helperexport=<id>: export a course and quit, for testing the helper without the menus)
	FString Test;
	if (FParse::Value(FCommandLine::Get(), TEXT("helperexport="), Test) && !Test.IsEmpty())
	{
		OnJobDone.AddWeakLambda(this, [this](const FOTGHelperJob& J) {
			UE_LOG(LogOTGHelper, Display, TEXT("-helperexport: %s %s (%s, %d s)"), *J.Id, *J.State, *J.Message, J.Seconds);
			FPlatformMisc::RequestExit(false, TEXT("helperexport"));
		});
		Export(Test, FParse::Param(FCommandLine::Get(), TEXT("helpervisible")));
	}
}

void UOTGHelperSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	Stop();
	Super::Deinitialize();
}

FString UOTGHelperSubsystem::HelperDir(bool* bDev) const
{
	auto Has = [](const FString& D) { return !D.IsEmpty() && OnDisk(FPaths::Combine(D, TEXT("server.js"))); };
	if (bDev) *bDev = false;
	FString D;
	if (FParse::Value(FCommandLine::Get(), TEXT("helper="), D) && Has(D)) return D;
	D = FPlatformMisc::GetEnvironmentVariable(TEXT("OTG_HELPER_DIR"));
	if (Has(D)) return D;
	// (the repository round the project: a development copy, the web version's own folders)
	D = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../..")));
	FPaths::NormalizeDirectoryName(D);
	if (Has(D)) { if (bDev) *bDev = true; return D; }
	D = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::RootDir(), TEXT("Helper")));
	if (Has(D)) return D;
	return FString();
}

FString UOTGHelperSubsystem::NodeExe(const FString& Dir) const
{
	const FString Own = FPaths::Combine(Dir, TEXT("node"), TEXT("node.exe"));
	if (OnDisk(Own)) return Own;
	// (else the one on the PATH)
	const FString Path = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
	TArray<FString> Parts;
	Path.ParseIntoArray(Parts, TEXT(";"));
	for (const FString& P : Parts)
	{
		const FString Exe = FPaths::Combine(P.TrimQuotes(), TEXT("node.exe"));
		if (OnDisk(Exe)) return Exe;
	}
	return FString();
}

bool UOTGHelperSubsystem::Available(FString* Why) const
{
	const FString Dir = HelperDir();
	if (Dir.IsEmpty()) { if (Why) *Why = TEXT("The course helper isn't installed with this copy of the game"); return false; }
	if (NodeExe(Dir).IsEmpty()) { if (Why) *Why = TEXT("The course helper needs Node.js, which isn't installed"); return false; }
	return true;
}

void UOTGHelperSubsystem::Start(TFunction<void(bool)> Then)
{
	if (State == EOTGHelperState::Ready && FPlatformProcess::IsProcRunning(Proc)) { if (Then) Then(true); return; }
	if (Then) Waiting.Add(MoveTemp(Then));
	if (State == EOTGHelperState::Starting) return;
	State = EOTGHelperState::Starting;
	Message = TEXT("Starting the course helper…");
	Tries = 0;
	Launch();
}

void UOTGHelperSubsystem::Launch()
{
	FString Why;
	if (!Available(&Why))
	{
		HelperLog(FString::Printf(TEXT("Not available: %s (root %s, project %s)"), *Why, *FPaths::ConvertRelativePathToFull(FPaths::RootDir()), *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir())));
		Finish(false, Why);
		return;
	}
	bool bDev = false;
	const FString Dir = HelperDir(&bDev);
	const FString Node = NodeExe(Dir);
	Tries++;
	// (a port of its own, away from the web version's 8080 and the launch monitors')
	Port = 18100 + FMath::RandRange(0, 799);
	// Its settings, through the environment it inherits
	FPlatformMisc::SetEnvironmentVar(TEXT("OTG_HELPER"), TEXT("1"));
	FPlatformMisc::SetEnvironmentVar(TEXT("PORT"), *FString::FromInt(Port));
	FPlatformMisc::SetEnvironmentVar(TEXT("OTG_PARENT_PID"), *FString::FromInt(FPlatformProcess::GetCurrentProcessId()));
	if (const UOTGCourseSubsystem* C = GetGameInstance()->GetSubsystem<UOTGCourseSubsystem>())
		FPlatformMisc::SetEnvironmentVar(TEXT("OTG_GAME_COURSES"), *C->CoursesFolder());
	if (!bDev)
	{
		// (a release: the game's own folder may not be writable; courses' sources and downloads in Documents)
		const FString Docs = FPaths::Combine(FPlatformProcess::UserDir(), TEXT("OpenTeeGolf"));
		FPlatformMisc::SetEnvironmentVar(TEXT("OTG_COURSES_DIR"), *FPaths::Combine(Docs, TEXT("CourseSources")));
		FPlatformMisc::SetEnvironmentVar(TEXT("OTG_DATA_DIR"), *FPaths::Combine(Docs, TEXT("HelperData")));
		FPlatformMisc::SetEnvironmentVar(TEXT("OTG_EXPORTS_DIR"), TEXT("-"));
	}
	if (!PipeRead) FPlatformProcess::CreatePipe(PipeRead, PipeWrite);
	const FString Args = FString::Printf(TEXT("\"%s\""), *FPaths::Combine(Dir, TEXT("server.js")));
	uint32 Pid = 0;
	Proc = FPlatformProcess::CreateProc(*Node, *Args, false, true, true, &Pid, 0, *Dir, PipeWrite, nullptr, PipeWrite);
	ProcId = Pid;
	if (!Proc.IsValid()) { HelperLog(TEXT("CreateProc failed: ") + Node); Finish(false, FString::Printf(TEXT("The course helper didn't start (%s)"), *Node)); return; }
	UE_LOG(LogOTGHelper, Display, TEXT("Course helper: %s %s on port %d (pid %u, %s)"), *Node, *Args, Port, Pid, bDev ? TEXT("development copy") : TEXT("release"));
	HelperLog(FString::Printf(TEXT("Started %s %s in %s on port %d (pid %u, %s)"), *Node, *Args, *Dir, Port, Pid, bDev ? TEXT("development copy") : TEXT("release")));
	Ping(0);
}

void UOTGHelperSubsystem::Ping(int32 Attempt)
{
	TWeakObjectPtr<UOTGHelperSubsystem> Self(this);
	const int32 P = Port;
	Async(EAsyncExecution::ThreadPool, [Self, Attempt, P]() {
		const FLocalReply R = LocalHttp(P, TEXT("GET"), TEXT("/api/helper/ping"), FString(), 3);
		AsyncTask(ENamedThreads::GameThread, [Self, Attempt, R]() {
			if (!Self.IsValid() || Self->State != EOTGHelperState::Starting) return;
			if (R.Code == 200)
			{
				TSharedPtr<FJsonObject> J;
				if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(R.Body), J) && J.IsValid() && J->GetBoolField(TEXT("helper")))
				{
					UE_LOG(LogOTGHelper, Display, TEXT("Course helper ready: version %s, courses in %s, browser %s"),
						*J->GetStringField(TEXT("version")), *J->GetStringField(TEXT("dir")), J->GetBoolField(TEXT("browser")) ? TEXT("found") : TEXT("missing"));
					HelperLog(TEXT("Answers: ") + R.Body);
					Self->Finish(true);
					return;
				}
			}
			// (gone: its port taken, or it failed - another port, twice more)
			if (!FPlatformProcess::IsProcRunning(Self->Proc))
			{
				FPlatformProcess::CloseProc(Self->Proc);
				HelperLog(TEXT("It stopped as it started"));
				if (Self->Tries < 3) { Self->Launch(); return; }
				Self->Finish(false, TEXT("The course helper stopped as it started: ") + (Self->Output.Num() ? Self->Output.Last() : FString()));
				return;
			}
			if (Attempt > 60) { Self->Finish(false, TEXT("The course helper didn't answer (") + R.Error + TEXT(")")); return; }
			// (again in a moment: the ticker's clock)
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, Attempt](float) {
				if (Self.IsValid()) Self->Ping(Attempt + 1);
				return false;
			}), 0.3f);
		});
	});
}

void UOTGHelperSubsystem::Finish(bool bOk, const FString& Why)
{
	State = bOk ? EOTGHelperState::Ready : EOTGHelperState::Failed;
	Message = bOk ? FString(TEXT("Ready")) : Why;
	if (!bOk) UE_LOG(LogOTGHelper, Warning, TEXT("Course helper: %s"), *Why);
	HelperLog(bOk ? FString(TEXT("Ready")) : TEXT("Failed: ") + Why);
	TArray<TFunction<void(bool)>> Then = MoveTemp(Waiting);
	Waiting.Reset();
	for (TFunction<void(bool)>& F : Then) F(bOk);
}

void UOTGHelperSubsystem::Stop()
{
	if (Proc.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(Proc)) FPlatformProcess::TerminateProc(Proc, true);
		FPlatformProcess::CloseProc(Proc);
		// (an export's browser isn't in its tree - Edge starts itself again - but runs on a profile
		// named after the helper: lib/helper.js; the next helper would clear it up too)
		if (Job.IsRunning() && Job.Kind == TEXT("export") && ProcId)
		{
			const FString Ps = FString::Printf(TEXT("-NoProfile -NonInteractive -WindowStyle Hidden -Command \"Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like '*OpenTeeGolf-export-%u-*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }\""), ProcId);
			FProcHandle K = FPlatformProcess::CreateProc(TEXT("powershell.exe"), *Ps, true, true, true, nullptr, 0, nullptr, nullptr);
			FPlatformProcess::CloseProc(K);
		}
	}
	ProcId = 0;
	if (PipeRead) FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
	PipeRead = PipeWrite = nullptr;
	State = EOTGHelperState::Off;
}

bool UOTGHelperSubsystem::Tick(float)
{
	// Its output, line by line, into the log
	if (PipeRead)
	{
		FString More = FPlatformProcess::ReadPipe(PipeRead);
		if (!More.IsEmpty())
		{
			Partial += More;
			int32 Nl;
			while (Partial.FindChar(TEXT('\n'), Nl))
			{
				const FString Line = Partial.Left(Nl).TrimEnd();
				Partial.RightChopInline(Nl + 1);
				if (Line.IsEmpty()) continue;
				UE_LOG(LogOTGHelper, Log, TEXT("| %s"), *Line);
				HelperLog(TEXT("| ") + Line);
				Output.Add(Line);
				if (Output.Num() > 300) Output.RemoveAt(0, Output.Num() - 300);
			}
		}
	}
	if (State == EOTGHelperState::Ready && Proc.IsValid() && !FPlatformProcess::IsProcRunning(Proc))
	{
		State = EOTGHelperState::Failed;
		Message = TEXT("The course helper stopped");
		UE_LOG(LogOTGHelper, Warning, TEXT("%s"), *Message);
		if (Job.IsRunning()) { Job.State = TEXT("failed"); Job.Message = Message; OnJobDone.Broadcast(Job); }
	}
	// The long job: asked about twice a second
	const double Now = FPlatformTime::Seconds();
	if (Job.IsRunning() && State == EOTGHelperState::Ready && !bPolling && Now >= NextPoll)
	{
		NextPoll = Now + 0.5;
		PollJob();
	}
	return true;
}

void UOTGHelperSubsystem::Request(const FString& Verb, const FString& Path, const FString& Body, FDone Done)
{
	TWeakObjectPtr<UOTGHelperSubsystem> Self(this);
	Start([Self, Verb, Path, Body, Done = MoveTemp(Done)](bool bOk) mutable {
		if (!Self.IsValid()) return;
		if (!bOk) { if (Done) Done(0, nullptr, Self->Message); return; }
		// (searches and checks ask OpenStreetMap: a minute or two when it's busy)
		const int32 P = Self->Port;
		Async(EAsyncExecution::ThreadPool, [P, Verb, Path, Body, Done = MoveTemp(Done)]() mutable {
			const FLocalReply R = LocalHttp(P, Verb, Path, Body, 300);
			AsyncTask(ENamedThreads::GameThread, [R, Path, Done = MoveTemp(Done)]() {
				if (!R.Error.IsEmpty()) HelperLog(FString::Printf(TEXT("%s: %s"), *Path, *R.Error));
				if (!Done) return;
				if (!R.Code) { Done(0, nullptr, TEXT("No answer from the course helper (") + R.Error + TEXT(")")); return; }
				TSharedPtr<FJsonValue> J;
				if (R.Type.StartsWith(TEXT("application/json"))) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(R.Body), J);
				Done(R.Code, J, R.Body);
			});
		});
	});
}

void UOTGHelperSubsystem::Search(const FString& Words, FDone Done)
{
	Get(TEXT("/api/builder/search?q=") + FGenericPlatformHttp::UrlEncode(Words), MoveTemp(Done));
}

void UOTGHelperSubsystem::World(int32 From, FDone Done)
{
	Get(FString::Printf(TEXT("/api/builder/world?from=%d"), From), MoveTemp(Done));
}

void UOTGHelperSubsystem::Where(double Lat, double Lon, FDone Done)
{
	Get(FString::Printf(TEXT("/api/builder/where?lat=%.6f&lon=%.6f"), Lat, Lon), MoveTemp(Done));
}

void UOTGHelperSubsystem::Check(const FString& OsmType, int64 OsmId, FDone Done)
{
	Get(FString::Printf(TEXT("/api/builder/check?type=%s&id=%lld"), *FGenericPlatformHttp::UrlEncode(OsmType), OsmId), MoveTemp(Done));
}

void UOTGHelperSubsystem::Build(const FString& OsmType, int64 OsmId, const FString& Name, const FString& Location, const FString& Group, bool bPhotoColours)
{
	if (Job.IsRunning()) { UE_LOG(LogOTGHelper, Warning, TEXT("Already %s %s"), *Job.Kind, *Job.Id); return; }
	const TSharedRef<FJsonObject> B = MakeShared<FJsonObject>();
	B->SetStringField(TEXT("osmType"), OsmType);
	B->SetNumberField(TEXT("osmId"), double(OsmId));
	if (!Name.IsEmpty()) B->SetStringField(TEXT("name"), Name);
	B->SetStringField(TEXT("location"), Location);
	if (!Group.IsEmpty()) B->SetStringField(TEXT("group"), Group);
	if (bPhotoColours) B->SetStringField(TEXT("surface"), TEXT("photo"));
	FString Body;
	FJsonSerializer::Serialize(B, TJsonWriterFactory<>::Create(&Body));
	Job = FOTGHelperJob();
	Job.Kind = TEXT("build"); Job.State = TEXT("start"); Job.Message = TEXT("Starting the build…");
	TWeakObjectPtr<UOTGHelperSubsystem> Self(this);
	Request(TEXT("POST"), TEXT("/api/builder/build"), Body, [Self](int32 Code, TSharedPtr<FJsonValue> J, const FString& Text) {
		if (!Self.IsValid()) return;
		FString Id;
		if (Code == 200 && J.IsValid() && J->AsObject().IsValid() && J->AsObject()->TryGetStringField(TEXT("jobId"), Id)) { Self->Job.Id = Id; Self->Job.State = TEXT("run"); return; }
		Self->Job.State = TEXT("failed"); Self->Job.Message = Text;
		Self->OnJobDone.Broadcast(Self->Job);
	});
}

void UOTGHelperSubsystem::Update(const FString& InCourseId)
{
	const FString CourseId = InCourseId;
	if (Job.IsRunning()) { UE_LOG(LogOTGHelper, Warning, TEXT("Already %s %s"), *Job.Kind, *Job.Id); return; }
	Job = FOTGHelperJob();
	Job.Kind = TEXT("update"); Job.State = TEXT("start"); Job.CourseId = CourseId; Job.Message = TEXT("Starting the update…");
	TWeakObjectPtr<UOTGHelperSubsystem> Self(this);
	Request(TEXT("POST"), FString::Printf(TEXT("/api/courses/%s/update"), *CourseId), FString(), [Self](int32 Code, TSharedPtr<FJsonValue> J, const FString& Text) {
		if (!Self.IsValid()) return;
		FString Id;
		if (Code == 200 && J.IsValid() && J->AsObject().IsValid() && J->AsObject()->TryGetStringField(TEXT("jobId"), Id)) { Self->Job.Id = Id; Self->Job.State = TEXT("run"); return; }
		Self->Job.State = TEXT("failed"); Self->Job.Message = Text;
		Self->OnJobDone.Broadcast(Self->Job);
	});
}

void UOTGHelperSubsystem::Export(const FString& InCourseId, bool bVisible)
{
	// (a copy: the id may be the last job's, which is cleared below)
	const FString CourseId = InCourseId;
	if (Job.IsRunning()) { UE_LOG(LogOTGHelper, Warning, TEXT("Already %s %s"), *Job.Kind, *Job.Id); return; }
	Job = FOTGHelperJob();
	Job.Kind = TEXT("export"); Job.Id = CourseId; Job.CourseId = CourseId; Job.State = TEXT("start"); Job.Message = TEXT("Starting the export…");
	UE_LOG(LogOTGHelper, Display, TEXT("Exporting %s"), *CourseId);
	TWeakObjectPtr<UOTGHelperSubsystem> Self(this);
	Request(TEXT("POST"), FString::Printf(TEXT("/api/helper/export?course=%s%s"), *FGenericPlatformHttp::UrlEncode(CourseId), bVisible ? TEXT("&visible=1") : TEXT("")), FString(),
		[Self](int32 Code, TSharedPtr<FJsonValue>, const FString& Text) {
			if (!Self.IsValid() || Code == 200) return;
			Self->Job.State = TEXT("failed"); Self->Job.Message = Text;
			Self->OnJobDone.Broadcast(Self->Job);
		});
}

void UOTGHelperSubsystem::Cancel()
{
	if (Job.Kind == TEXT("export")) Request(TEXT("POST"), TEXT("/api/helper/cancel"), FString(), nullptr);
}

void UOTGHelperSubsystem::PollJob()
{
	if (Job.Id.IsEmpty()) return;
	bPolling = true;
	const FString Path = Job.Kind == TEXT("export") ? FString(TEXT("/api/helper/job")) : TEXT("/api/helper/build?job=") + Job.Id;
	TWeakObjectPtr<UOTGHelperSubsystem> Self(this);
	Get(Path, [Self](int32 Code, TSharedPtr<FJsonValue> J, const FString&) {
		if (!Self.IsValid()) return;
		Self->bPolling = false;
		const TSharedPtr<FJsonObject> O = J.IsValid() ? J->AsObject() : nullptr;
		if (Code != 200 || !O.IsValid() || !Self->Job.IsRunning()) return;
		FOTGHelperJob& Jb = Self->Job;
		const FString Before = Jb.Message;
		if (Jb.Kind == TEXT("export"))
		{
			if (O->GetStringField(TEXT("course")) != Jb.Id) return;
			Jb.State = O->GetStringField(TEXT("state"));
			Jb.Frac = O->GetNumberField(TEXT("frac"));
			Jb.Message = O->GetStringField(TEXT("msg"));
			Jb.Seconds = O->GetIntegerField(TEXT("seconds"));
		}
		else
		{
			FString S;
			if (O->TryGetStringField(TEXT("msg"), S) && !S.IsEmpty()) Jb.Message = S;
			FString Err;
			if (O->TryGetStringField(TEXT("error"), Err) && !Err.IsEmpty()) { Jb.State = TEXT("failed"); Jb.Message = Err; }
			else if (O->GetBoolField(TEXT("done"))) { Jb.State = TEXT("done"); O->TryGetStringField(TEXT("courseId"), Jb.CourseId); }
		}
		if (Jb.Message != Before) UE_LOG(LogOTGHelper, Display, TEXT("%s %s: %.0f%% %s"), *Jb.Kind, *Jb.Id, Jb.Frac * 100, *Jb.Message);
		if (!Jb.IsRunning())
		{
			UE_LOG(LogOTGHelper, Display, TEXT("%s %s: %s %s"), *Jb.Kind, *Jb.Id, *Jb.State, *Jb.CourseId);
			Self->OnJobDone.Broadcast(Jb);
		}
	});
}

void UOTGHelperSubsystem::LoadSource(const FString& CourseId, TFunction<void(TSharedPtr<FJsonObject>, TSharedPtr<FJsonObject>, const FString&)> Done)
{
	TWeakObjectPtr<UOTGHelperSubsystem> Self(this);
	Get(FString::Printf(TEXT("/api/courses/%s/data"), *CourseId), [Self, CourseId, Done](int32 Code, TSharedPtr<FJsonValue> J, const FString& Text) {
		if (!Self.IsValid()) return;
		const TSharedPtr<FJsonObject> Course = J.IsValid() ? J->AsObject() : nullptr;
		if (Code != 200 || !Course.IsValid()) { Done(nullptr, nullptr, Code ? FString::Printf(TEXT("No course %s (%d)"), *CourseId, Code) : Text); return; }
		// (the real-world part: none for a course not made with the builder)
		Self->Get(FString::Printf(TEXT("/api/courses/%s/real"), *CourseId), [Course, Done](int32 Code2, TSharedPtr<FJsonValue> J2, const FString& Text2) {
			TSharedPtr<FJsonObject> Real;
			if (Code2 == 200 && !J2.IsValid()) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text2), Real);
			else if (J2.IsValid()) Real = J2->AsObject();
			Done(Course, Real, FString());
		});
	});
}

void UOTGHelperSubsystem::SaveSource(const FString& CourseId, TSharedRef<FJsonObject> Body, FDone Done)
{
	FString Text;
	FJsonSerializer::Serialize(Body, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
	Request(TEXT("PUT"), FString::Printf(TEXT("/api/courses/%s"), *CourseId), Text, MoveTemp(Done));
}

FString UOTGHelperSubsystem::StatusLine() const
{
	static const TCHAR* Names[] = { TEXT("off"), TEXT("starting"), TEXT("ready"), TEXT("failed") };
	FString S = FString::Printf(TEXT("Course helper %s (port %d): %s"), Names[int32(State)], Port, *Message);
	if (!Job.Kind.IsEmpty()) S += FString::Printf(TEXT(" | %s %s %s %.0f%% %s"), *Job.Kind, *Job.Id, *Job.State, Job.Frac * 100, *Job.Message);
	return S;
}
