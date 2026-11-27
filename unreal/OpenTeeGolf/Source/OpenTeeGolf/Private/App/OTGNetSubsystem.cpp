// See OTGNetSubsystem.h.

#include "App/OTGNetSubsystem.h"
#include "App/OTGCourseSync.h"
#include "Course/OTGCourseSubsystem.h"

#include "Async/Async.h"
#include "Common/TcpSocketBuilder.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GeneralProjectSettings.h"
#include "HAL/FileManager.h"
#include "IPAddress.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "JsonObjectConverter.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGNet, Log, All);

namespace
{
	constexpr uint8 KindJson = 1, KindChunk = 2;
	constexpr uint32 MaxFrame = 32u << 20;
	constexpr int64 ChunkBytes = 256 << 10;

	FString GameVersion() { return GetDefault<UGeneralProjectSettings>()->ProjectVersion; }
	ISocketSubsystem* Sockets() { return ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM); }

	void PutU32(TArray<uint8>& B, uint32 V) { for (int32 i = 0; i < 4; i++) B.Add(uint8(V >> (8 * i))); }
	void PutI64(TArray<uint8>& B, int64 V) { for (int32 i = 0; i < 8; i++) B.Add(uint8(uint64(V) >> (8 * i))); }
	uint32 GetU32(const uint8* P) { return uint32(P[0]) | uint32(P[1]) << 8 | uint32(P[2]) << 16 | uint32(P[3]) << 24; }
	int64 GetI64(const uint8* P) { uint64 V = 0; for (int32 i = 0; i < 8; i++) V |= uint64(P[i]) << (8 * i); return int64(V); }

	void Configure(FSocket* S)
	{
		int32 Size = 0;
		S->SetNonBlocking(true);
		S->SetNoDelay(true);
		S->SetSendBufferSize(4 << 20, Size);
		S->SetReceiveBufferSize(4 << 20, Size);
	}

	TSharedRef<FJsonObject> Msg(const TCHAR* Type)
	{
		TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("t"), Type);
		return M;
	}

	TArray<TSharedPtr<FJsonValue>> Colour(const FLinearColor& C)
	{
		return { MakeShared<FJsonValueNumber>(C.R), MakeShared<FJsonValueNumber>(C.G), MakeShared<FJsonValueNumber>(C.B) };
	}
	FLinearColor ColourOf(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!O->TryGetArrayField(Field, A) || A->Num() < 3) return FLinearColor::White;
		return FLinearColor(float((*A)[0]->AsNumber()), float((*A)[1]->AsNumber()), float((*A)[2]->AsNumber()));
	}

	// a folder that isn't there yet, named after Base (Base_2, Base_3, ... if it is)
	FString FreeFolder(const FString& Root, const FString& Base)
	{
		FString Dir = FPaths::Combine(Root, Base);
		for (int32 i = 2; IFileManager::Get().DirectoryExists(*Dir); i++) Dir = FPaths::Combine(Root, FString::Printf(TEXT("%s_%d"), *Base, i));
		return Dir;
	}
}

UOTGNetSubsystem::FPeer::~FPeer()
{
	if (Sock)
	{
		Sock->Close();
		Sockets()->DestroySocket(Sock);
	}
}

UOTGNetSubsystem* UOTGNetSubsystem::Get(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UOTGNetSubsystem>() : nullptr;
}

UOTGNetSubsystem* UOTGNetSubsystem::Get()
{
	UGameInstance* GI = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UOTGNetSubsystem>() : nullptr;
}

void UOTGNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UOTGNetSubsystem::Tick));
}

void UOTGNetSubsystem::Deinitialize()
{
	Leave();
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	Super::Deinitialize();
}

// ------------------------------------------------------------------ hosting, joining, leaving

bool UOTGNetSubsystem::Host(const FOTGRoundSetup& InSetup, bool bInTogether, const FOTGPlayerProfile& InMe, int32 InPort, FString& Why)
{
	Leave();
	ErrorLine.Reset();
	UOTGCourseSubsystem* CS = GetGameInstance()->GetSubsystem<UOTGCourseSubsystem>();
	FOTGCourseInfo Info;
	bool bFound = false;
	if (CS) for (const FOTGCourseInfo& C : CS->ListCourses()) if (C.Id == InSetup.CourseId) { Info = C; bFound = true; }
	if (!bFound || Info.ExportDir.IsEmpty())
	{
		Why = TEXT("Pick a course on the Play page first: one built in the game or exported from the web version (not a course pack, the putt-putt or the range).");
		return false;
	}
	Listener = FTcpSocketBuilder(TEXT("OpenTeeGolf host")).AsNonBlocking().AsReusable().BoundToEndpoint(FIPv4Endpoint(FIPv4Address::Any, InPort)).Listening(8).Build();
	if (!Listener)
	{
		Why = FString::Printf(TEXT("Couldn't open port %d: is another game already hosting on it?"), InPort);
		return false;
	}
	NetRole = EOTGNetRole::Host;
	Generation++;
	Setup = InSetup;
	Setup.Picked.Reset();
	Setup.Teams.Reset();
	Setup.LastPlayed.Reset();
	bTogether = bInTogether;
	Me = InMe;
	HostPort = InPort;
	LocalPlayerId = 1;
	NextId = 2;
	bWelcomed = true;
	bRoundOn = false;
	HostCourseDir = Info.ExportDir;
	otg::sync::FMarker M;
	if (otg::sync::ReadMarker(HostCourseDir, M))
	{
		// (a copy of someone else's: the course it's a version of)
		HostCourseId = M.Original;
		HostCourseName = M.OriginalName.IsEmpty() ? Info.Name : M.OriginalName;
	}
	else
	{
		HostCourseId = Info.Id;
		HostCourseName = Info.Name;
	}
	LocalCourse = Info.Id;
	SyncState = ESync::Done;
	PlayerList.Reset();
	FOTGNetPlayer P;
	P.Id = LocalPlayerId; P.Name = Me.Name; P.Colour = Me.Colour; P.Handicap = Me.Handicap; P.bLeftHanded = Me.bLeftHanded; P.Tee = Me.Tee; P.ProfileId = Me.Id;
	P.Course = TEXT("ready"); P.Progress = 1; P.bHost = true;
	PlayerList.Add(P);
	bManifestReady = false;
	Files.Reset();
	const FString Dir = HostCourseDir;
	HostManifest = Async(EAsyncExecution::ThreadPool, [Dir]() { return otg::sync::Manifest(Dir); });
	StatusLine = TEXT("Getting the course ready to share");
	UE_LOG(LogOTGNet, Display, TEXT("Hosting %s (%s) on port %d, %s"), *HostCourseName, *HostCourseDir, HostPort, bTogether ? TEXT("everyone playing at once") : TEXT("taking turns"));
	Changed();
	return true;
}

void UOTGNetSubsystem::Join(const FString& Address, const FOTGPlayerProfile& InMe)
{
	Leave();
	ErrorLine.Reset();
	FString HostPart = Address.TrimStartAndEnd();
	int32 InPort = DefaultPort;
	FString L, R;
	if (HostPart.Split(TEXT(":"), &L, &R, ESearchCase::IgnoreCase, ESearchDir::FromEnd) && R.IsNumeric()) { HostPart = L; InPort = FCString::Atoi(*R); }
	if (HostPart.IsEmpty())
	{
		ErrorLine = TEXT("Type the host's address (the numbers their Online page shows)");
		Changed();
		return;
	}
	NetRole = EOTGNetRole::Client;
	Generation++;
	Me = InMe;
	HostPort = InPort;
	bWelcomed = false;
	StatusLine = FString::Printf(TEXT("Connecting to %s:%d"), *HostPart, InPort);
	Changed();
	const int32 Gen = Generation;
	TWeakObjectPtr<UOTGNetSubsystem> Self(this);
	// (a connection can take a while to fail: off the game thread)
	Async(EAsyncExecution::Thread, [Self, HostPart, InPort, Gen]() {
		ISocketSubsystem* SS = Sockets();
		FSocket* S = nullptr;
		FString Why;
		const FAddressInfoResult Found = SS->GetAddressInfo(*HostPart, nullptr, EAddressInfoFlags::Default, FNetworkProtocolTypes::IPv4, ESocketType::SOCKTYPE_Streaming);
		if (Found.ReturnCode != SE_NO_ERROR || !Found.Results.Num()) Why = FString::Printf(TEXT("Couldn't find %s: check the address"), *HostPart);
		else
		{
			TSharedRef<FInternetAddr> A = Found.Results[0].Address;
			A->SetPort(InPort);
			S = SS->CreateSocket(NAME_Stream, TEXT("OpenTeeGolf player"), A->GetProtocolType());
			if (!S) Why = TEXT("Couldn't make a connection");
			else if (!S->Connect(*A))
			{
				Why = FString::Printf(TEXT("Nobody answered at %s:%d. Is their game hosting? Over the internet, the host's router needs port %d forwarded to their computer."), *HostPart, InPort, InPort);
				SS->DestroySocket(S);
				S = nullptr;
			}
		}
		AsyncTask(ENamedThreads::GameThread, [Self, S, Why, Gen]() {
			UOTGNetSubsystem* N = Self.Get();
			if (!N || N->Generation != Gen || N->NetRole != EOTGNetRole::Client)
			{
				if (S) Sockets()->DestroySocket(S);
				return;
			}
			N->Connected(S, Why);
		});
	});
}

void UOTGNetSubsystem::Connected(FSocket* S, const FString& Why)
{
	if (!S) return Leave(Why);
	Configure(S);
	TUniquePtr<FPeer> P = MakeUnique<FPeer>();
	P->Sock = S;
	FPeer& Host = *P;
	Peers.Add(MoveTemp(P));
	TSharedRef<FJsonObject> Hello = Msg(TEXT("hello"));
	Hello->SetStringField(TEXT("version"), GameVersion());
	Hello->SetStringField(TEXT("name"), Me.Name);
	Hello->SetStringField(TEXT("profile"), Me.Id);
	Hello->SetArrayField(TEXT("colour"), Colour(Me.Colour));
	Hello->SetNumberField(TEXT("handicap"), Me.Handicap);
	Hello->SetBoolField(TEXT("left"), Me.bLeftHanded);
	Hello->SetStringField(TEXT("tee"), Me.Tee);
	Send(Host, Hello);
	StatusLine = TEXT("Connected: waiting for the host");
	Changed();
}

void UOTGNetSubsystem::Leave(const FString& Why)
{
	if (NetRole != EOTGNetRole::None)
	{
		UE_LOG(LogOTGNet, Display, TEXT("Leaving the online game%s%s"), Why.IsEmpty() ? TEXT("") : TEXT(": "), *Why);
		// (a goodbye, sent if it can be)
		const TSharedRef<FJsonObject> Bye = Msg(IsHost() ? TEXT("closed") : TEXT("bye"));
		for (TUniquePtr<FPeer>& P : Peers) if (!P->bDead) { Send(*P, Bye); Pump(*P); }
	}
	Peers.Reset();
	if (Listener)
	{
		Listener->Close();
		Sockets()->DestroySocket(Listener);
		Listener = nullptr;
	}
	if (DlWriter)
	{
		DlWriter->Close();
		DlWriter.Reset();
		if (Files.IsValidIndex(DlQueue.IsValidIndex(DlAt) ? DlQueue[DlAt] : -1)) IFileManager::Get().Delete(*(FPaths::Combine(Target, Files[DlQueue[DlAt]].Path) + TEXT(".otgpart")), false, true, true);
	}
	DlSha.Reset();
	const bool bWasOn = NetRole != EOTGNetRole::None;
	NetRole = EOTGNetRole::None;
	Generation++;
	bWelcomed = false;
	bRoundOn = false;
	bManifestReady = false;
	PlayerList.Reset();
	WaitingWelcome.Reset();
	GameIn.Reset();
	LocalCourse.Reset();
	SyncState = ESync::Idle;
	DlQueue.Reset();
	DlAt = 0; DlTotal = DlDone = FileGot = 0;
	StatusLine.Reset();
	if (!Why.IsEmpty()) ErrorLine = Why;
	if (bWasOn || !Why.IsEmpty()) Changed();
}

void UOTGNetSubsystem::DropPeer(FPeer& P, const FString& Why)
{
	if (IsHost())
	{
		if (FOTGNetPlayer* Pl = PlayerList.FindByPredicate([&](const FOTGNetPlayer& X) { return X.Id == P.Player; }))
		{
			UE_LOG(LogOTGNet, Display, TEXT("%s left (%s)"), *Pl->Name, *Why);
			// (in a round they stay on the card, gone; in the lobby, off the list)
			if (bRoundOn) Pl->bConnected = false;
			else PlayerList.RemoveAll([&](const FOTGNetPlayer& X) { return X.Id == P.Player; });
		}
		P.Player = 0;
	}
}

// ------------------------------------------------------------------ every frame

bool UOTGNetSubsystem::Tick(float Dt)
{
	if (NetRole == EOTGNetRole::None) return true;
	const int32 Gen = Generation;
	if (IsHost())
	{
		// the course's files counted: the players who said hello meanwhile welcomed
		if (!bManifestReady && HostManifest.IsValid() && HostManifest.IsReady())
		{
			Files = HostManifest.Get();
			bManifestReady = true;
			int64 Bytes = 0;
			for (const otg::sync::FFile& F : Files) Bytes += F.Size;
			UE_LOG(LogOTGNet, Display, TEXT("The course to share: %d files, %.0f MB"), Files.Num(), Bytes / 1048576.0);
			StatusLine = FString::Printf(TEXT("Hosting: waiting for players to join on port %d"), HostPort);
			for (TUniquePtr<FPeer>& P : Peers) if (WaitingWelcome.Contains(P->Player)) Send(*P, Welcome(P->Player));
			WaitingWelcome.Reset();
			Changed();
		}
		// new players
		bool bPending = false;
		while (Listener && Listener->HasPendingConnection(bPending) && bPending)
		{
			FSocket* S = Listener->Accept(TEXT("OpenTeeGolf player"));
			if (!S) break;
			Configure(S);
			TUniquePtr<FPeer> P = MakeUnique<FPeer>();
			P->Sock = S;
			Peers.Add(MoveTemp(P));
		}
	}
	for (int32 i = 0; i < Peers.Num(); i++)
	{
		FPeer& P = *Peers[i];
		Read(P);
		if (Gen != Generation) return true;   // (left, in what came)
		Pump(P);
	}
	// the gone
	bool bDropped = false;
	for (int32 i = Peers.Num() - 1; i >= 0; i--)
	{
		if (!Peers[i]->bDead) continue;
		if (!IsHost())
		{
			Leave(TEXT("Lost the connection to the host"));
			return true;
		}
		DropPeer(*Peers[i], TEXT("disconnected"));
		Peers.RemoveAt(i);
		bDropped = true;
	}
	if (bDropped) { SendAll(PlayersJson()); Changed(); }

	// the course here
	if (SyncState == ESync::Checking && CheckTask.IsValid() && CheckTask.IsReady())
	{
		const FCheckResult R = CheckTask.Get();
		const FString Root = GetGameInstance()->GetSubsystem<UOTGCourseSubsystem>()->CoursesFolder();
		if (!R.UseId.IsEmpty()) CourseDone(R.UseId);
		else if (!R.UpdateDir.IsEmpty()) Prepare(R.UpdateDir, true, true);   // (a copy of this host's from before: brought up to date)
		else if (!R.OwnDir.IsEmpty())
		{
			OwnDir = R.OwnDir;
			OwnName = R.OwnName.IsEmpty() ? HostCourseName : R.OwnName;
			SyncState = ESync::Choosing;
			LocalStatus(TEXT("choosing"), 0);
		}
		else Prepare(FreeFolder(Root, otg::sync::SafeName(HostCourseId)), false, false);
	}
	if (SyncState == ESync::Preparing && PrepTask.IsValid() && PrepTask.IsReady())
	{
		const FPrepResult R = PrepTask.Get();
		if (!R.bOk) CourseFailed(R.Why);
		else
		{
			RemoveAfter = R.Remove;
			DlQueue = R.Download;
			DlAt = 0;
			DlTotal = R.Bytes;
			DlDone = 0;
			UE_LOG(LogOTGNet, Display, TEXT("The course here: %d files the same, %d copied from other courses, %d to download (%.1f MB), %d to take out"), R.Kept, R.Copied, DlQueue.Num(), DlTotal / 1048576.0, RemoveAfter.Num());
			if (!DlQueue.Num()) Downloaded();
			else
			{
				SyncState = ESync::Downloading;
				TSharedRef<FJsonObject> W = Msg(TEXT("want"));
				TArray<TSharedPtr<FJsonValue>> L;
				for (int32 I : DlQueue) L.Add(MakeShared<FJsonValueNumber>(I));
				W->SetArrayField(TEXT("files"), L);
				if (Peers.Num()) Send(*Peers[0], W);
				LocalStatus(TEXT("downloading"), 0);
			}
		}
	}
	if (SyncState == ESync::Downloading && FPlatformTime::Seconds() - StatusSentAt > 0.5)
		LocalStatus(TEXT("downloading"), DlTotal > 0 ? float(double(DlDone) / DlTotal) : 0.f);
	return true;
}

// ------------------------------------------------------------------ the connection

void UOTGNetSubsystem::Read(FPeer& P)
{
	if (P.bDead || !P.Sock) return;
	uint8 Buf[64 * 1024];
	for (int32 k = 0; k < 256; k++)
	{
		int32 Got = 0;
		// (false: closed or broken; true with nothing: nothing yet)
		if (!P.Sock->Recv(Buf, sizeof(Buf), Got)) { P.bDead = true; break; }
		if (Got <= 0) break;
		P.In.Append(Buf, Got);
	}
	const int32 Gen = Generation;
	while (!P.bDead)
	{
		const int32 Avail = P.In.Num() - P.InAt;
		if (Avail < 4) break;
		const uint32 Len = GetU32(P.In.GetData() + P.InAt);
		if (Len == 0 || Len > MaxFrame) { P.bDead = true; break; }
		if (Avail < 4 + int32(Len)) break;
		const int32 At = P.InAt + 4;
		P.InAt += 4 + int32(Len);
		Handle(P, P.In.GetData() + At, int32(Len));
		if (Gen != Generation) return;   // (left: P's gone)
	}
	if (P.InAt >= P.In.Num()) { P.In.Reset(); P.InAt = 0; }
	else if (P.InAt > (4 << 20)) { P.In.RemoveAt(0, P.InAt, EAllowShrinking::No); P.InAt = 0; }
}

void UOTGNetSubsystem::Pump(FPeer& P)
{
	if (P.bDead || !P.Sock) return;
	for (int32 k = 0; k < 64; k++)
	{
		if (P.Out.Num() - P.OutAt < (1 << 20)) FillChunks(P);
		const int32 Left = P.Out.Num() - P.OutAt;
		if (Left <= 0) break;
		int32 Sent = 0;
		if (!P.Sock->Send(P.Out.GetData() + P.OutAt, Left, Sent))
		{
			const ESocketErrors E = Sockets()->GetLastErrorCode();
			if (E != SE_EWOULDBLOCK && E != SE_TRY_AGAIN && E != SE_NO_ERROR) P.bDead = true;
			break;
		}
		if (Sent <= 0) break;
		P.OutAt += Sent;
		if (Sent < Left) break;   // (its buffer's full: more next frame)
	}
	if (P.OutAt >= P.Out.Num()) { P.Out.Reset(); P.OutAt = 0; if (P.bCloseWhenSent) P.bDead = true; }
	else if (P.OutAt > (4 << 20)) { P.Out.RemoveAt(0, P.OutAt, EAllowShrinking::No); P.OutAt = 0; }
}

void UOTGNetSubsystem::Send(FPeer& P, const TSharedRef<FJsonObject>& M)
{
	FString Text;
	FJsonSerializer::Serialize(M, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
	const FTCHARToUTF8 U(*Text);
	PutU32(P.Out, uint32(U.Length() + 1));
	P.Out.Add(KindJson);
	P.Out.Append(reinterpret_cast<const uint8*>(U.Get()), U.Length());
}

void UOTGNetSubsystem::SendAll(const TSharedRef<FJsonObject>& M, const FPeer* Except)
{
	for (TUniquePtr<FPeer>& P : Peers)
		if (P.Get() != Except && !P->bDead && (P->Player || !IsHost())) Send(*P, M);
}

bool UOTGNetSubsystem::FillChunks(FPeer& P)
{
	if (!IsHost() || !bManifestReady) return false;
	while (P.WantAt < P.Want.Num())
	{
		const int32 Idx = P.Want[P.WantAt];
		if (!Files.IsValidIndex(Idx)) { P.WantAt++; continue; }
		const otg::sync::FFile& F = Files[Idx];
		if (!P.Reader)
		{
			P.Reader.Reset(IFileManager::Get().CreateFileReader(*FPaths::Combine(HostCourseDir, F.Path)));
			P.ReadAt = 0;
			if (!P.Reader)
			{
				UE_LOG(LogOTGNet, Warning, TEXT("Couldn't read %s to send"), *F.Path);
				P.WantAt++;
				continue;
			}
		}
		const int64 N = FMath::Clamp<int64>(F.Size - P.ReadAt, 0, ChunkBytes);
		PutU32(P.Out, uint32(1 + 4 + 8 + N));
		P.Out.Add(KindChunk);
		PutU32(P.Out, uint32(Idx));
		PutI64(P.Out, P.ReadAt);
		const int32 At = P.Out.AddUninitialized(int32(N));
		if (N > 0) P.Reader->Serialize(P.Out.GetData() + At, N);
		P.ReadAt += N;
		if (P.ReadAt >= F.Size)
		{
			P.Reader.Reset();
			P.WantAt++;
		}
		return true;
	}
	return false;
}

void UOTGNetSubsystem::Handle(FPeer& P, const uint8* Data, int32 N)
{
	if (N < 1) return;
	if (Data[0] == KindChunk) return HandleChunk(Data + 1, N - 1);
	if (Data[0] != KindJson) return;
	const FUTF8ToTCHAR C(reinterpret_cast<const ANSICHAR*>(Data + 1), N - 1);
	const FString Text(C.Length(), C.Get());
	TSharedPtr<FJsonObject> M;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), M) || !M.IsValid()) return;
	HandleJson(P, M.ToSharedRef());
}

void UOTGNetSubsystem::HandleJson(FPeer& P, const TSharedRef<FJsonObject>& M)
{
	const FString T = M->GetStringField(TEXT("t"));
	if (IsHost())
	{
		if (T == TEXT("hello") && !P.Player)
		{
			auto Refuse = [&](const FString& Why) {
				TSharedRef<FJsonObject> R = Msg(TEXT("refuse"));
				R->SetStringField(TEXT("why"), Why);
				Send(P, R);
				P.bCloseWhenSent = true;
			};
			const FString V = M->GetStringField(TEXT("version"));
			if (V != GameVersion()) return Refuse(FString::Printf(TEXT("The host has OpenTeeGolf %s and you have %s: you both need the same version (update from the menu)."), *GameVersion(), *V));
			if (bRoundOn) return Refuse(TEXT("They've started their round: join before the next one."));
			FOTGNetPlayer Pl;
			Pl.Id = NextId++;
			Pl.Name = M->GetStringField(TEXT("name"));
			if (Pl.Name.IsEmpty()) Pl.Name = FString::Printf(TEXT("Player %d"), Pl.Id);
			Pl.ProfileId = M->GetStringField(TEXT("profile"));
			Pl.Colour = ColourOf(M, TEXT("colour"));
			Pl.Handicap = M->GetNumberField(TEXT("handicap"));
			Pl.bLeftHanded = M->GetBoolField(TEXT("left"));
			Pl.Tee = M->GetStringField(TEXT("tee"));
			// (a colour someone already has: the first nobody has, so every ball and tracer is told apart)
			auto Taken = [&](const FLinearColor& C) { return PlayerList.ContainsByPredicate([&](const FOTGNetPlayer& X) { return X.Colour.Equals(C, 0.08f); }); };
			if (Taken(Pl.Colour))
				for (const FLinearColor& C : FOTGPlayers::Colours()) if (!Taken(C)) { Pl.Colour = C; break; }
			PlayerList.Add(Pl);
			P.Player = Pl.Id;
			UE_LOG(LogOTGNet, Display, TEXT("%s joined"), *Pl.Name);
			if (bManifestReady) Send(P, Welcome(Pl.Id));
			else WaitingWelcome.Add(Pl.Id);
			SendAll(PlayersJson());
			Changed();
		}
		else if (T == TEXT("status") && P.Player)
		{
			if (FOTGNetPlayer* Pl = PlayerList.FindByPredicate([&](const FOTGNetPlayer& X) { return X.Id == P.Player; }))
			{
				Pl->Course = M->GetStringField(TEXT("course"));
				Pl->Progress = float(M->GetNumberField(TEXT("progress")));
				SendAll(PlayersJson());
				Changed();
			}
		}
		else if (T == TEXT("want") && P.Player)
		{
			P.Want.Reset();
			for (const TSharedPtr<FJsonValue>& V : M->GetArrayField(TEXT("files"))) P.Want.Add(int32(V->AsNumber()));
			P.WantAt = 0;
			P.Reader.Reset();
		}
		else if (T == TEXT("g") && P.Player)
		{
			M->SetNumberField(TEXT("p"), P.Player);
			if (bRoundOn) GameIn.Add(M);
			SendAll(M, &P);
		}
		else if (T == TEXT("bye")) P.bDead = true;
		return;
	}
	// a player: from the host
	if (T == TEXT("refuse")) return Leave(M->GetStringField(TEXT("why")));
	if (T == TEXT("closed")) return Leave(TEXT("The host closed the game"));
	if (T == TEXT("welcome"))
	{
		LocalPlayerId = int32(M->GetNumberField(TEXT("you")));
		const TSharedPtr<FJsonObject>* S = nullptr;
		if (M->TryGetObjectField(TEXT("setup"), S)) FJsonObjectConverter::JsonObjectToUStruct(S->ToSharedRef(), &Setup);
		bTogether = M->GetBoolField(TEXT("together"));
		HostCourseId = M->GetStringField(TEXT("course"));
		HostCourseName = M->GetStringField(TEXT("courseName"));
		Files.Reset();
		for (const TSharedPtr<FJsonValue>& V : M->GetArrayField(TEXT("files")))
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			otg::sync::FFile F;
			F.Path = O->GetStringField(TEXT("p"));
			F.Size = int64(O->GetNumberField(TEXT("s")));
			F.Hash = O->GetStringField(TEXT("h"));
			// (a name with a folder in it, or that climbs out: not a course's file)
			if (F.Path.Contains(TEXT("/")) || F.Path.Contains(TEXT("\\")) || F.Path.Contains(TEXT("..")) || !otg::sync::Counts(F.Path)) return Leave(TEXT("The host's course has a file this game can't take"));
			Files.Add(F);
		}
		ReadPlayers(M);
		bWelcomed = true;
		StatusLine = FString::Printf(TEXT("In %s's game"), *HostName());
		UE_LOG(LogOTGNet, Display, TEXT("Joined %s's game: %s, %d files"), *HostName(), *HostCourseName, Files.Num());
		CheckCourse();
		Changed();
	}
	else if (T == TEXT("players")) { ReadPlayers(M); Changed(); }
	else if (T == TEXT("start")) BeginRound(int32(M->GetNumberField(TEXT("seed"))));
	else if (T == TEXT("g")) { if (bRoundOn) GameIn.Add(M); }
}

// ------------------------------------------------------------------ the lobby

FString UOTGNetSubsystem::HostName() const
{
	for (const FOTGNetPlayer& P : PlayerList) if (P.bHost) return P.Name;
	return TEXT("the host");
}

const FOTGNetPlayer* UOTGNetSubsystem::Player(int32 Id) const
{
	return PlayerList.FindByPredicate([&](const FOTGNetPlayer& P) { return P.Id == Id; });
}

bool UOTGNetSubsystem::IsConnected(int32 Id) const
{
	if (Id == LocalPlayerId) return true;
	if (!InSession()) return false;
	const FOTGNetPlayer* P = Player(Id);
	return P && P->bConnected;
}

TArray<FString> UOTGNetSubsystem::Addresses() const
{
	TArray<FString> Out;
	TArray<TSharedPtr<FInternetAddr>> A;
	if (Sockets()->GetLocalAdapterAddresses(A))
		for (const TSharedPtr<FInternetAddr>& X : A)
		{
			if (!X.IsValid() || X->GetProtocolType() != FNetworkProtocolTypes::IPv4) continue;
			const FString S = X->ToString(false);
			if (!S.StartsWith(TEXT("127.")) && !S.StartsWith(TEXT("169.254.")) && S != TEXT("0.0.0.0")) Out.AddUnique(S);
		}
	return Out;
}

TSharedRef<FJsonObject> UOTGNetSubsystem::PlayersJson() const
{
	TSharedRef<FJsonObject> M = Msg(TEXT("players"));
	TArray<TSharedPtr<FJsonValue>> L;
	for (const FOTGNetPlayer& P : PlayerList)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("id"), P.Id);
		O->SetStringField(TEXT("name"), P.Name);
		O->SetStringField(TEXT("tee"), P.Tee);
		O->SetStringField(TEXT("profile"), P.ProfileId);
		O->SetArrayField(TEXT("colour"), Colour(P.Colour));
		O->SetNumberField(TEXT("handicap"), P.Handicap);
		O->SetBoolField(TEXT("left"), P.bLeftHanded);
		O->SetStringField(TEXT("course"), P.Course);
		O->SetNumberField(TEXT("progress"), P.Progress);
		O->SetBoolField(TEXT("on"), P.bConnected);
		O->SetBoolField(TEXT("host"), P.bHost);
		L.Add(MakeShared<FJsonValueObject>(O));
	}
	M->SetArrayField(TEXT("players"), L);
	return M;
}

void UOTGNetSubsystem::ReadPlayers(const TSharedPtr<FJsonObject>& M)
{
	const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
	if (!M->TryGetArrayField(TEXT("players"), L)) return;
	TArray<FOTGNetPlayer> Out;
	for (const TSharedPtr<FJsonValue>& V : *L)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O.IsValid()) continue;
		FOTGNetPlayer P;
		P.Id = int32(O->GetNumberField(TEXT("id")));
		P.Name = O->GetStringField(TEXT("name"));
		P.Tee = O->GetStringField(TEXT("tee"));
		P.ProfileId = O->GetStringField(TEXT("profile"));
		P.Colour = ColourOf(O, TEXT("colour"));
		P.Handicap = O->GetNumberField(TEXT("handicap"));
		P.bLeftHanded = O->GetBoolField(TEXT("left"));
		P.Course = O->GetStringField(TEXT("course"));
		P.Progress = float(O->GetNumberField(TEXT("progress")));
		P.bConnected = O->GetBoolField(TEXT("on"));
		P.bHost = O->GetBoolField(TEXT("host"));
		Out.Add(P);
	}
	PlayerList = Out;
}

TSharedRef<FJsonObject> UOTGNetSubsystem::Welcome(int32 Id) const
{
	TSharedRef<FJsonObject> M = PlayersJson();
	M->SetStringField(TEXT("t"), TEXT("welcome"));
	M->SetNumberField(TEXT("you"), Id);
	TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
	FJsonObjectConverter::UStructToJsonObject(FOTGRoundSetup::StaticStruct(), &Setup, S);
	M->SetObjectField(TEXT("setup"), S);
	M->SetBoolField(TEXT("together"), bTogether);
	M->SetStringField(TEXT("course"), HostCourseId);
	M->SetStringField(TEXT("courseName"), HostCourseName);
	TArray<TSharedPtr<FJsonValue>> L;
	for (const otg::sync::FFile& F : Files)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("p"), F.Path);
		O->SetNumberField(TEXT("s"), double(F.Size));
		O->SetStringField(TEXT("h"), F.Hash);
		L.Add(MakeShared<FJsonValueObject>(O));
	}
	M->SetArrayField(TEXT("files"), L);
	return M;
}

void UOTGNetSubsystem::LocalStatus(const FString& State, float Progress)
{
	StatusSentAt = FPlatformTime::Seconds();
	if (FOTGNetPlayer* P = PlayerList.FindByPredicate([&](const FOTGNetPlayer& X) { return X.Id == LocalPlayerId; }))
	{
		P->Course = State;
		P->Progress = Progress;
	}
	if (IsHost()) SendAll(PlayersJson());
	else if (Peers.Num())
	{
		TSharedRef<FJsonObject> M = Msg(TEXT("status"));
		M->SetStringField(TEXT("course"), State);
		M->SetNumberField(TEXT("progress"), Progress);
		Send(*Peers[0], M);
	}
	Changed();
}

bool UOTGNetSubsystem::CanStart(FString& Why) const
{
	if (!IsHost()) { Why = TEXT("The host starts the round"); return false; }
	if (!bManifestReady) { Why = TEXT("Getting the course ready to share"); return false; }
	int32 N = 0;
	for (const FOTGNetPlayer& P : PlayerList)
	{
		if (!P.bConnected) continue;
		N++;
		if (P.Course != TEXT("ready")) { Why = FString::Printf(TEXT("Waiting for %s to get the course"), *P.Name); return false; }
	}
	if (N < 2) { Why = TEXT("Waiting for someone to join"); return false; }
	return true;
}

void UOTGNetSubsystem::StartRound()
{
	FString Why;
	if (!CanStart(Why)) return;
	const int32 Seed = FMath::Rand();
	TSharedRef<FJsonObject> M = Msg(TEXT("start"));
	M->SetNumberField(TEXT("seed"), Seed);
	SendAll(M);
	BeginRound(Seed);
}

void UOTGNetSubsystem::BeginRound(int32 Seed)
{
	UOTGCourseSubsystem* CS = GetGameInstance()->GetSubsystem<UOTGCourseSubsystem>();
	if (!CS || LocalCourse.IsEmpty()) return;
	bRoundOn = true;
	GameIn.Reset();
	UE_LOG(LogOTGNet, Display, TEXT("The round starts: %s, %d players"), *LocalCourse, PlayerList.Num());
	CS->PendingSetup = Setup;
	CS->PendingSetup.CourseId = LocalCourse;
	CS->bHasPendingSetup = true;
	CS->PlayCourse(LocalCourse);
	Changed();
}

void UOTGNetSubsystem::RoundOver()
{
	if (!bRoundOn) return;
	bRoundOn = false;
	GameIn.Reset();
	if (IsHost())
	{
		PlayerList.RemoveAll([](const FOTGNetPlayer& P) { return !P.bConnected; });
		SendAll(PlayersJson());
	}
	Changed();
}

void UOTGNetSubsystem::SendGame(const TSharedRef<FJsonObject>& M)
{
	if (!InSession()) return;
	M->SetStringField(TEXT("t"), TEXT("g"));
	M->SetNumberField(TEXT("p"), LocalPlayerId);
	if (IsHost()) SendAll(M);
	else if (Peers.Num()) Send(*Peers[0], M);
}

bool UOTGNetSubsystem::PopGame(TSharedPtr<FJsonObject>& Out)
{
	if (!GameIn.Num()) return false;
	Out = GameIn[0];
	GameIn.RemoveAt(0);
	return true;
}

// ------------------------------------------------------------------ the course here

FString UOTGNetSubsystem::CopyName() const
{
	return FString::Printf(TEXT("%s (from %s)"), *HostCourseName, *HostName());
}

void UOTGNetSubsystem::CheckCourse()
{
	SyncState = ESync::Checking;
	LocalStatus(TEXT("checking"), 0);
	const FString Root = GetGameInstance()->GetSubsystem<UOTGCourseSubsystem>()->CoursesFolder();
	const FString HostId = HostCourseId, From = HostName();
	const TArray<otg::sync::FFile> Want = Files;
	CheckTask = Async(EAsyncExecution::ThreadPool, [Root, HostId, From, Want]() {
		FCheckResult R;
		const FString Fp = otg::sync::Fingerprint(Want);
		for (const otg::sync::FLocal& L : otg::sync::LocalCourses(Root))
		{
			if (L.OriginalId() != HostId) continue;
			// (the same files: this one)
			if (otg::sync::Fingerprint(otg::sync::Manifest(L.Dir)) == Fp) { R.UseId = L.CourseId(); return R; }
			if (L.bMarked && L.M.From == From) { if (R.UpdateDir.IsEmpty()) R.UpdateDir = L.Dir; }
			else if (!L.bMarked && R.OwnDir.IsEmpty()) { R.OwnDir = L.Dir; R.OwnName = L.Name; }
		}
		return R;
	});
}

void UOTGNetSubsystem::ChooseCourse(EOTGCourseChoice Choice)
{
	if (SyncState != ESync::Choosing) return;
	if (Choice == EOTGCourseChoice::Leave) return Leave();
	if (Choice == EOTGCourseChoice::Overwrite) return Prepare(OwnDir, true, false);
	const FString Root = GetGameInstance()->GetSubsystem<UOTGCourseSubsystem>()->CoursesFolder();
	Prepare(FreeFolder(Root, otg::sync::SafeName(HostCourseId + TEXT("_from_") + HostName())), false, true);
}

void UOTGNetSubsystem::Prepare(const FString& InTarget, bool bPrune, bool bCopy)
{
	SyncState = ESync::Preparing;
	Target = InTarget;
	bTargetCopy = bCopy;
	LocalStatus(TEXT("downloading"), 0);
	const FString Root = GetGameInstance()->GetSubsystem<UOTGCourseSubsystem>()->CoursesFolder();
	const TArray<otg::sync::FFile> Want = Files;
	UE_LOG(LogOTGNet, Display, TEXT("Making %s the host's %s%s"), *InTarget, *HostCourseName, bCopy ? TEXT(" (a copy)") : TEXT(""));
	PrepTask = Async(EAsyncExecution::ThreadPool, [Root, InTarget, Want, bPrune]() {
		FPrepResult R;
		TArray<FString> Others;
		for (const otg::sync::FLocal& L : otg::sync::LocalCourses(Root))
			if (FPaths::ConvertRelativePathToFull(L.Dir) != FPaths::ConvertRelativePathToFull(InTarget)) Others.Add(L.Dir);
		const otg::sync::FPlan P = otg::sync::MakePlan(InTarget, Want, Others, bPrune);
		if (!otg::sync::CopyFiles(P, Want, R.Why)) return R;
		R.bOk = true;
		R.Download = P.Download;
		R.Bytes = P.DownloadBytes;
		R.Remove = P.Remove;
		R.Kept = P.Kept;
		R.Copied = P.Copies.Num();
		return R;
	});
}

void UOTGNetSubsystem::HandleChunk(const uint8* Data, int32 N)
{
	if (IsHost() || SyncState != ESync::Downloading) return;
	if (N < 12) return CourseFailed(TEXT("the host sent something this game can't read"));
	const int32 Idx = int32(GetU32(Data));
	const int64 Offset = GetI64(Data + 4);
	const uint8* Bytes = Data + 12;
	const int32 Len = N - 12;
	if (!DlQueue.IsValidIndex(DlAt) || DlQueue[DlAt] != Idx || !Files.IsValidIndex(Idx)) return CourseFailed(TEXT("the host's files came out of order"));
	const otg::sync::FFile& F = Files[Idx];
	const FString Part = FPaths::Combine(Target, F.Path) + TEXT(".otgpart");
	if (!DlWriter)
	{
		if (Offset != 0) return CourseFailed(TEXT("the host's files came out of order"));
		IFileManager::Get().MakeDirectory(*Target, true);
		DlWriter.Reset(IFileManager::Get().CreateFileWriter(*Part));
		if (!DlWriter) return CourseFailed(FString::Printf(TEXT("couldn't write %s"), *Part));
		DlSha = MakeUnique<FSHA1>();
		FileGot = 0;
	}
	if (Offset != FileGot) return CourseFailed(TEXT("part of a file went missing"));
	if (Len > 0)
	{
		DlWriter->Serialize(const_cast<uint8*>(Bytes), Len);
		DlSha->Update(Bytes, Len);
	}
	FileGot += Len;
	DlDone += Len;
	if (FileGot < F.Size) return;
	DlWriter->Close();
	DlWriter.Reset();
	DlSha->Final();
	uint8 Digest[FSHA1::DigestSize];
	DlSha->GetHash(Digest);
	DlSha.Reset();
	if (BytesToHex(Digest, FSHA1::DigestSize) != F.Hash)
	{
		IFileManager::Get().Delete(*Part, false, true, true);
		return CourseFailed(FString::Printf(TEXT("%s came through damaged"), *F.Path));
	}
	if (!IFileManager::Get().Move(*FPaths::Combine(Target, F.Path), *Part, true, true)) return CourseFailed(FString::Printf(TEXT("couldn't write %s"), *F.Path));
	DlAt++;
	if (DlAt >= DlQueue.Num()) Downloaded();
}

void UOTGNetSubsystem::Downloaded()
{
	for (const FString& R : RemoveAfter)
	{
		UE_LOG(LogOTGNet, Display, TEXT("Not in the host's course: %s (taken out)"), *R);
		IFileManager::Get().Delete(*R, false, true, true);
	}
	RemoveAfter.Reset();
	FString Id = HostCourseId;
	if (bTargetCopy)
	{
		otg::sync::FMarker M;
		M.Id = FPaths::GetCleanFilename(Target);
		M.Name = CopyName();
		M.Original = HostCourseId;
		M.OriginalName = HostCourseName;
		M.From = HostName();
		otg::sync::WriteMarker(Target, M);
		Id = M.Id;
	}
	CourseDone(Id);
}

void UOTGNetSubsystem::CourseDone(const FString& Id)
{
	LocalCourse = Id;
	SyncState = ESync::Done;
	// (the course's files may have changed under the one read last)
	if (UOTGCourseSubsystem* CS = GetGameInstance()->GetSubsystem<UOTGCourseSubsystem>()) CS->LastExport = nullptr;
	UE_LOG(LogOTGNet, Display, TEXT("The host's course is here: %s"), *Id);
	LocalStatus(TEXT("ready"), 1);
}

void UOTGNetSubsystem::CourseFailed(const FString& Why)
{
	UE_LOG(LogOTGNet, Warning, TEXT("Couldn't get the host's course: %s"), *Why);
	if (DlWriter) { DlWriter->Close(); DlWriter.Reset(); }
	DlSha.Reset();
	SyncState = ESync::Failed;
	ErrorLine = TEXT("Couldn't get the course: ") + Why;
	LocalStatus(TEXT("failed"), 0);
}
