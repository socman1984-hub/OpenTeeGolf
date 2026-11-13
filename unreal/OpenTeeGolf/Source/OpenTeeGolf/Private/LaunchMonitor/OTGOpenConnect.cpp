// See OTGOpenConnect.h (server.js, "OpenConnect (TCP 921)").

#include "OTGOpenConnect.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "IPAddress.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace
{
	TOptional<double> Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		double V;
		if (O && O->HasField(Key) && O->TryGetNumberField(Key, V)) return V;
		// (numbers sent as text)
		FString S;
		if (O && O->TryGetStringField(Key, S) && S.IsNumeric()) return FCString::Atod(*S);
		return {};
	}
	FString Escape(const FString& S) { return S.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\"")); }
}

FOTGOpenConnectServer::FOTGOpenConnectServer(int32 InPort, TSharedRef<FOTGLMOutbox> InOut)
	: Port(InPort), Out(InOut)
{
	Thread = FRunnableThread::Create(this, TEXT("OTG OpenConnect"), 0, TPri_Normal);
}

FOTGOpenConnectServer::~FOTGOpenConnectServer()
{
	if (Thread)
	{
		Thread->Kill(true);
		delete Thread;
	}
}

void FOTGOpenConnectServer::SetPlayer(const FString& InHanded, const FString& InClub, double InDistance)
{
	FScopeLock L(&PlayerLock);
	Handed = InHanded;
	Club = InClub;
	Distance = InDistance;
	bPlayerChanged = true;
}

FString FOTGOpenConnectServer::PlayerJson()
{
	FScopeLock L(&PlayerLock);
	return FString::Printf(TEXT("{\"Code\":201,\"Message\":\"Player Information\",\"Player\":{\"Handed\":\"%s\",\"Club\":\"%s\",\"DistanceToTarget\":%g}}"),
		*Escape(Handed), *Escape(Club), Distance);
}

bool FOTGOpenConnectServer::Listen()
{
	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	TSharedRef<FInternetAddr> Addr = SS->CreateInternetAddr();
	Addr->SetAnyAddress();
	Addr->SetPort(Port);
	FSocket* S = SS->CreateSocket(NAME_Stream, TEXT("OTG OpenConnect"), Addr->GetProtocolType());
	if (!S) return false;
	// (no address reuse: a second program on the port must fail here, not share it)
	if (!S->Bind(*Addr) || !S->Listen(8))
	{
		const ESocketErrors E = SS->GetLastErrorCode();
		S->Close();
		SS->DestroySocket(S);
		const FString Why = E == SE_EADDRINUSE ? FString::Printf(TEXT("Port %d is in use by another program"), Port)
			: FString::Printf(TEXT("Can't listen on port %d: %s"), Port, SS->GetSocketError(E));
		bool bNew = false;
		Out->Update([&](FOTGLMDeviceState& D) { bNew = D.PortError != Why; D.PortError = Why; });
		if (bNew)
		{
			if (E == SE_EADDRINUSE) { UE_LOG(LogOTGLM, Warning, TEXT("Port %d is already in use. Close any other simulator or connector using it so OpenTeeGolf can receive shots through OpenConnect."), Port); }
			else { UE_LOG(LogOTGLM, Warning, TEXT("%s"), *Why); }
		}
		return false;
	}
	S->SetNonBlocking(true);
	Listener = S;
	Out->Update([](FOTGLMDeviceState& D) { D.PortError.Reset(); });
	UE_LOG(LogOTGLM, Display, TEXT("OpenConnect listener on port %d (point FS Golf here)"), Port);
	return true;
}

void FOTGOpenConnectServer::Publish()
{
	Out->Update([&](FOTGLMDeviceState& D) {
		D.Clients = Clients.Num();
		D.bConnected = Clients.Num() > 0;
		D.Ready = Clients.Num() > 0 && bReady ? 1 : 0;
		D.Device = Device;
		D.State = Clients.Num() ? TEXT("connected") : TEXT("waiting");
	});
}

uint32 FOTGOpenConnectServer::Run()
{
	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	double NextTry = 0;
	Publish();
	while (bRunning)
	{
		// (keeps trying, so closing the other program is enough)
		if (!Listener && FPlatformTime::Seconds() >= NextTry && !Listen()) NextTry = FPlatformTime::Seconds() + 5;
		if (Listener)
		{
			bool bPending = false;
			while (Listener->HasPendingConnection(bPending) && bPending)
			{
				TSharedRef<FInternetAddr> From = SS->CreateInternetAddr();
				FSocket* C = Listener->Accept(*From, TEXT("OTG OpenConnect client"));
				if (!C) break;
				C->SetNonBlocking(true);
				C->SetNoDelay(true);
				FClient& Cl = Clients.AddDefaulted_GetRef();
				Cl.Sock = C;
				Cl.Addr = From->ToString(true);
				UE_LOG(LogOTGLM, Display, TEXT("Launch monitor connected from %s"), *Cl.Addr);
				SendJson(Cl, PlayerJson());
				Publish();
			}
		}
		if (bPlayerChanged)
		{
			bPlayerChanged = false;
			const FString P = PlayerJson();
			for (FClient& C : Clients) SendJson(C, P);
		}
		for (int32 i = Clients.Num() - 1; i >= 0; i--)
		{
			Read(Clients[i]);
			if (!Clients[i].Sock)
			{
				UE_LOG(LogOTGLM, Display, TEXT("Launch monitor disconnected (%s)"), *Clients[i].Addr);
				Clients.RemoveAt(i);
				Publish();
			}
		}
		FPlatformProcess::Sleep(0.01f);
	}
	for (FClient& C : Clients) if (C.Sock) { C.Sock->Close(); SS->DestroySocket(C.Sock); }
	Clients.Reset();
	if (Listener) { Listener->Close(); SS->DestroySocket(Listener); Listener = nullptr; }
	return 0;
}

void FOTGOpenConnectServer::Read(FClient& C)
{
	uint8 Buf[8192];
	for (;;)
	{
		int32 N = 0;
		if (!C.Sock->Recv(Buf, sizeof(Buf), N))
		{
			C.Sock->Close();
			ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(C.Sock);
			C.Sock = nullptr;
			return;
		}
		if (N <= 0) break;
		C.Buf.Append(Buf, N);
	}
	// Messages arrive as back-to-back JSON objects with no delimiter; split on balanced braces
	int32 Depth = 0, Start = -1, Consumed = 0;
	bool bStr = false, bEsc = false;
	for (int32 i = 0; i < C.Buf.Num(); i++)
	{
		const uint8 Ch = C.Buf[i];
		if (bStr)
		{
			if (bEsc) bEsc = false;
			else if (Ch == '\\') bEsc = true;
			else if (Ch == '"') bStr = false;
			continue;
		}
		if (Ch == '"') bStr = true;
		else if (Ch == '{') { if (Depth++ == 0) Start = i; }
		else if (Ch == '}' && Depth > 0 && --Depth == 0)
		{
			const FUTF8ToTCHAR Text((const UTF8CHAR*)(C.Buf.GetData() + Start), i + 1 - Start);
			Consumed = i + 1;
			Handle(C, FString(Text.Length(), Text.Get()));
			if (!C.Sock) return;
		}
	}
	C.Buf.RemoveAt(0, Consumed);
	if (Depth == 0)
	{
		int32 Brace = C.Buf.Find('{');
		C.Buf.RemoveAt(0, Brace == INDEX_NONE ? C.Buf.Num() : Brace);
	}
	if (C.Buf.Num() > 1000000) C.Buf.Reset();
}

void FOTGOpenConnectServer::SendJson(FClient& C, const FString& Json)
{
	if (!C.Sock) return;
	const FTCHARToUTF8 U(*Json);
	int32 Sent = 0;
	C.Sock->Send((const uint8*)U.Get(), U.Length(), Sent);
}

void FOTGOpenConnectServer::Handle(FClient& C, const FString& Json)
{
	TSharedPtr<FJsonObject> Msg;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Msg) || !Msg)
	{
		UE_LOG(LogOTGLM, Warning, TEXT("Bad JSON from launch monitor"));
		return;
	}
	FString Id;
	if (Msg->TryGetStringField(TEXT("DeviceID"), Id) && !Id.IsEmpty() && Id != Device) { Device = Id; Publish(); }
	const TSharedPtr<FJsonObject>* OptsP = nullptr;
	TSharedPtr<FJsonObject> Opts = Msg->TryGetObjectField(TEXT("ShotDataOptions"), OptsP) ? *OptsP : MakeShared<FJsonObject>();
	bool B;
	bool bChanged = false;
	if (Opts->TryGetBoolField(TEXT("LaunchMonitorIsReady"), B) && B != bReady) { bReady = B; bChanged = true; }
	if (Opts->TryGetBoolField(TEXT("LaunchMonitorBallDetected"), B) && B != bBallDetected) { bBallDetected = B; bChanged = true; }
	if (bChanged) Publish();
	const TSharedPtr<FJsonObject>* BallP = nullptr;
	bool bHeartbeat = false, bHasBall = true;
	Opts->TryGetBoolField(TEXT("IsHeartBeat"), bHeartbeat);
	Opts->TryGetBoolField(TEXT("ContainsBallData"), bHasBall);
	if (bHeartbeat || !bHasBall || !Msg->TryGetObjectField(TEXT("BallData"), BallP))
	{
		SendJson(C, TEXT("{\"Code\":200,\"Message\":\"Heartbeat received\"}"));
		return;
	}
	const TSharedPtr<FJsonObject> Ball = *BallP;
	FOTGBallData D;
	D.Speed = Num(Ball, TEXT("Speed"));
	D.VLA = Num(Ball, TEXT("VLA"));
	D.HLA = Num(Ball, TEXT("HLA"));
	D.BackSpin = Num(Ball, TEXT("BackSpin"));
	D.SideSpin = Num(Ball, TEXT("SideSpin"));
	D.TotalSpin = Num(Ball, TEXT("TotalSpin"));
	D.SpinAxis = Num(Ball, TEXT("SpinAxis"));
	D.CarryYd = Num(Ball, TEXT("CarryDistance"));
	const TSharedPtr<FJsonObject>* ClubP = nullptr;
	if (Msg->TryGetObjectField(TEXT("ClubData"), ClubP))
	{
		const TSharedPtr<FJsonObject> Cl = *ClubP;
		D.Club.bValid = true;
		auto Get = [&](const TCHAR* K) { TOptional<double> V = Num(Cl, K); return V.IsSet() ? *V : NAN; };
		D.Club.SpeedMph = Get(TEXT("Speed"));
		D.Club.AngleOfAttack = Get(TEXT("AngleOfAttack"));
		D.Club.FaceToTarget = Get(TEXT("FaceToTarget"));
		D.Club.Path = Get(TEXT("Path"));
		D.Club.Loft = Get(TEXT("Loft"));
		D.Club.SmashFactor = Get(TEXT("SmashFactor"));
	}
	double Number = -1;
	const TOptional<double> ShotNo = Num(Msg, TEXT("ShotNumber"));
	FOTGShot S = OTGToShot(D, TEXT("launch-monitor"), Device, ShotNo.IsSet() ? int32(*ShotNo) : -1);
	(void)Number;
	if (S.SpeedMph <= 0)
	{
		SendJson(C, TEXT("{\"Code\":501,\"Message\":\"Ball speed missing\"}"));
		return;
	}
	// Some connectors resend a shot; ignore exact duplicates within a few seconds
	const FString Key = FString::Printf(TEXT("%d|%g|%g|%g"), S.ShotNumber, S.SpeedMph, S.VLA, S.HLA);
	const double Now = FPlatformTime::Seconds();
	if (Key == LastKey && Now - LastKeyAt < 5)
	{
		SendJson(C, TEXT("{\"Code\":200,\"Message\":\"Shot received successfully\"}"));
		return;
	}
	LastKey = Key;
	LastKeyAt = Now;
	Out->Shot(S);
	SendJson(C, TEXT("{\"Code\":200,\"Message\":\"Shot received successfully\"}"));
}
