// See OTGMevoClient.h (lib/mevo.js, "the client").

#include "OTGMevoClient.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "IPAddress.h"
#include "Misc/DateTime.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

using namespace otg::mevo;

namespace
{
	double Now() { return FPlatformTime::Seconds(); }
	const TCHAR* StepText(const FString& Step)
	{
		if (Step == TEXT("DSP")) return TEXT("introducing itself");
		if (Step == TEXT("radar")) return TEXT("reading the radar");
		if (Step == TEXT("camera")) return TEXT("reading the camera board");
		if (Step == TEXT("settings")) return TEXT("sending settings");
		if (Step == TEXT("arming")) return TEXT("arming");
		return *Step;
	}
	bool IsAck(const FMevoFrame& M, uint8 Src = BUS_AVR) { return M.Src == Src && M.Type == T::CONFIG_ACK; }
	TArray<uint8> I24(int32 V) { return { uint8((V >> 16) & 255), uint8((V >> 8) & 255), uint8(V & 255) }; }
	bool IsShotType(uint8 T) { return T == T::FLIGHT || T == T::CLUB || T == T::SPIN || T == T::SHOT_TEXT; }
	bool ArmedText(const FMevoFrame& M)
	{
		if (M.Type != T::TXT) return false;
		const FString S = Text(M.Payload);
		return S.Contains(TEXT("ARMED")) && !S.Contains(TEXT("CANCELLED"));
	}
}

FOTGMevoClient::FOTGMevoClient(const FString& InHost, int32 InPort, const FString& InMode, TSharedRef<FOTGLMOutbox> InOut, const FString& TracePath)
	: Host(InHost), Port(InPort), Out(InOut), WantMode(InMode), Mode(InMode)
{
	// Every message to and from the Mevo+ (started fresh each run, kept under ~5 MB), so a
	// connection problem can be looked at afterwards
	if (!TracePath.IsEmpty()) TraceFile.Reset(IFileManager::Get().CreateFileWriter(*TracePath, FILEWRITE_AllowRead));
}

void FOTGMevoClient::Start()
{
	if (!Thread) Thread = FRunnableThread::Create(this, TEXT("OTG Mevo+"), 0, TPri_Normal);
}

FOTGMevoClient::~FOTGMevoClient()
{
	Shutdown();
}

void FOTGMevoClient::SetMode(const FString& InMode)
{
	FScopeLock L(&Ctl);
	WantMode = InMode;
}
void FOTGMevoClient::SetWantArmed(bool bArmedWanted)
{
	FScopeLock L(&Ctl);
	bWantDisarmed = !bArmedWanted;
}
void FOTGMevoClient::SetHoldAfterShot(bool bHold)
{
	FScopeLock L(&Ctl);
	bHoldAfterShot = bHold;
}
void FOTGMevoClient::SetSetup(const FOTGMevoSetup& InSetup)
{
	FScopeLock L(&Ctl);
	Setup = InSetup;
	bHasSetup = bSetupChanged = true;
}

void FOTGMevoClient::Shutdown()
{
	if (!Thread) return;
	bStopRequested = true;
	for (const double Until = Now() + 3.5; !bFinished && Now() < Until;) FPlatformProcess::Sleep(0.02f);
	bRunning = false;
	Thread->Kill(true);
	delete Thread;
	Thread = nullptr;
	if (TraceFile) TraceFile->Close();
}

void FOTGMevoClient::Log(const FString& Msg)
{
	UE_LOG(LogOTGLM, Display, TEXT("%s"), *Msg);
	if (TraceFile)
	{
		const FTCHARToUTF8 U(*(FDateTime::Now().ToString(TEXT("%H:%M:%S.%s ")) + Msg + TEXT("\n")));
		TraceFile->Serialize((void*)U.Get(), U.Length());
		TraceFile->Flush();
	}
}

void FOTGMevoClient::Trace(const TCHAR* Dir, const FMevoFrame& F)
{
	if (!TraceFile || TraceFile->TotalSize() > 5000000) return;
	if (F.Type == T::STATUS && (State == TEXT("ready") || State == TEXT("idle"))) return; // (the once-a-second status polls)
	FString Hex;
	for (int32 i = 0; i < F.Payload.Num(); i++) Hex += FString::Printf(TEXT("%s%02X"), i ? TEXT(" ") : TEXT(""), F.Payload[i]);
	FString Line = FString::Printf(TEXT("%s %s %02x->%02x %02x [%s]"), *FDateTime::Now().ToString(TEXT("%H:%M:%S.%s")), Dir, F.Src, F.Dest, F.Type, *Hex);
	if (F.Type == T::TXT || F.Type == T::SHOT_TEXT) Line += FString::Printf(TEXT("  \"%s\""), *Text(F.Payload));
	const FTCHARToUTF8 U(*(Line + TEXT("\n")));
	TraceFile->Serialize((void*)U.Get(), U.Length());
}

void FOTGMevoClient::SetState(const FString& InState, const FString& Error)
{
	State = InState;
	Out->Update([&](FOTGLMDeviceState& S) {
		S.State = InState;
		S.Error = Error;
		S.Mode = Mode;
		S.bConnected = InState == TEXT("ready") || InState == TEXT("shot") || InState == TEXT("arming") || InState == TEXT("idle");
		S.Ready = InState == TEXT("ready") ? 1 : 0;
	});
}

// ------------------------------------------------------------------ plumbing

bool FOTGMevoClient::Connect()
{
	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	TSharedRef<FInternetAddr> Addr = SS->CreateInternetAddr();
	bool bValid = false;
	Addr->SetIp(*Host, bValid);
	Addr->SetPort(Port);
	if (!bValid)
	{
		Err = FString::Printf(TEXT("%s isn't an address"), *Host);
		return false;
	}
	Sock = SS->CreateSocket(NAME_Stream, TEXT("OTG Mevo+"), Addr->GetProtocolType());
	if (!Sock)
	{
		Err = TEXT("couldn't make a socket");
		return false;
	}
	Sock->SetNonBlocking(true);
	Sock->SetNoDelay(true);
	Sock->Connect(*Addr);
	for (const double Until = Now() + 5; Now() < Until && bRunning;)
	{
		const ESocketConnectionState C = Sock->GetConnectionState();
		if (C == SCS_Connected) return true;
		if (C == SCS_ConnectionError) break;
		Sock->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(100));
	}
	Err = FString::Printf(TEXT("can't reach the Mevo+ at %s (is this PC on the Mevo+ Wi-Fi?)"), *Host);
	return false;
}

void FOTGMevoClient::Cleanup()
{
	if (Sock)
	{
		Sock->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Sock);
		Sock = nullptr;
	}
	Reader.Buf.Reset();
	Pending.Reset();
	bArmed = false;
	Shot.Reset();
	bFinishPending = false;
	TriggerDeadline = FlightDeadline = 0;
}

bool FOTGMevoClient::Pump(double Seconds)
{
	if (!Sock || bDropped) return false;
	if (Seconds > 0 && !Sock->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(Seconds)))
		return Sock->GetConnectionState() != SCS_ConnectionError;
	uint8 Buf[8192];
	for (;;)
	{
		int32 Read = 0;
		if (!Sock->Recv(Buf, sizeof(Buf), Read))
		{
			bDropped = true;
			return false;
		}
		if (Read <= 0) return true;
		TArray<FMevoFrame> Frames;
		Reader.Push(Buf, Read, Frames);
		for (FMevoFrame& F : Frames)
		{
			Trace(TEXT("<"), F);
			// (shot results are taken whoever they're addressed to)
			if (F.Dest == BUS_APP || IsShotType(F.Type)) Pending.Add(MoveTemp(F));
		}
	}
}

void FOTGMevoClient::Send(uint8 Dest, uint8 Type, const TArray<uint8>& Payload)
{
	if (!Sock) return;
	FMevoFrame F;
	F.Dest = Dest; F.Src = BUS_APP; F.Type = Type; F.Payload = Payload;
	Trace(TEXT(">"), F);
	const TArray<uint8> Wire = EncodeFrame(Dest, Type, Payload);
	int32 Sent = 0;
	Sock->Send(Wire.GetData(), Wire.Num(), Sent);
}

void FOTGMevoClient::DrainPending()
{
	Pump(0);
	while (Pending.Num())
	{
		const FMevoFrame F = Pending[0];
		Pending.RemoveAt(0);
		OnFrame(F);
	}
}

bool FOTGMevoClient::WaitFor(FPred Pred, double Seconds, const TCHAR* What, FMevoFrame* OutFrame)
{
	const double Until = Now() + Seconds;
	for (;;)
	{
		while (Pending.Num())
		{
			const FMevoFrame F = Pending[0];
			Pending.RemoveAt(0);
			OnFrame(F);
			if (Pred(F))
			{
				if (OutFrame) *OutFrame = F;
				return true;
			}
		}
		const double Left = Until - Now();
		if (bDropped || !Sock)
		{
			Err = TEXT("the Mevo+ closed the connection");
			return false;
		}
		if (Left <= 0 || !bRunning)
		{
			Err = FString::Printf(TEXT("no %s from the Mevo+"), What);
			return false;
		}
		if (!Pump(FMath::Min(Left, 0.1)))
		{
			Err = TEXT("the Mevo+ closed the connection");
			return false;
		}
	}
}

bool FOTGMevoClient::Ask(uint8 Dest, uint8 Type, const TArray<uint8>& Payload, FPred Pred, const TCHAR* What, double Seconds, FMevoFrame* OutFrame)
{
	DrainPending(); // (only a reply to this counts)
	Send(Dest, Type, Payload);
	return WaitFor(Pred, Seconds, What, OutFrame);
}

void FOTGMevoClient::Idle(double Seconds)
{
	WaitFor([](const FMevoFrame&) { return false; }, Seconds, TEXT("nothing"));
	if (!bDropped) Err.Reset();
}

void FOTGMevoClient::OnFrame(const FMevoFrame& M)
{
	const TArray<uint8>& P = M.Payload;
	// Status for the HUD
	if (M.Type == T::STATUS && M.Src == BUS_DSP && P.Num() >= 64 && P[0] == 0x80)
		Out->Update([&](FOTGLMDeviceState& S) { S.Battery = P[61]; });
	if (M.Type == T::STATUS && M.Src == BUS_AVR && P.Num() >= 25)
	{
		Out->Update([&](FOTGLMDeviceState& S) {
			S.Tilt = FMath::RoundToDouble(ReadFloat40(P, 15) * 100) / 100;
			S.Roll = FMath::RoundToDouble(-ReadFloat40(P, 20) * 100) / 100;
			S.TempC = FMath::RoundToDouble(ReadFloat40(P, 10) * 10) / 10;
		});
	}
	// The device's own status lines ("ARMED DetectionMode=1", "BALL TRIGGER", ...)
	if (M.Type == T::TXT || M.Type == T::SHOT_TEXT)
	{
		const FString S = Text(P);
		if (!S.IsEmpty()) Out->Update([&](FOTGLMDeviceState& D) { D.LastText = S; });
	}
	// Shot data is collected whenever it arrives (its order isn't fixed). A flight result counts as
	// a shot even if the trigger message was missed; if "PROCESSED" never follows, the shot is
	// reported anyway shortly after.
	if (M.Type == T::FLIGHT || M.Type == T::CLUB || M.Type == T::SPIN)
	{
		FFlight Fl;
		if (M.Type == T::FLIGHT && !Shot && DecodeFlight(P, Fl))
		{
			Log(TEXT("Mevo+: flight result without a trigger message"));
			StartShot();
		}
		if (Shot && !Shot->bDone)
		{
			if (M.Type == T::FLIGHT) { if (DecodeFlight(P, Fl)) Shot->Flight = Fl; }
			else if (M.Type == T::CLUB) { FClub C; if (DecodeClub(P, C)) Shot->Club = C; }
			else { FSpin Sp; if (DecodeSpin(P, Sp)) Shot->Spin = Sp; }
			if (M.Type == T::FLIGHT && Shot->Flight) FlightDeadline = Now() + 2.5;
		}
	}
	// Shot progress arrives as shot text (0xE5); some firmware may use plain text (0xE3)
	if (M.Type == T::SHOT_TEXT) OnShotText(Text(P));
	else if (M.Type == T::TXT)
	{
		const FString S = Text(P);
		if (S.Contains(TEXT("BALL TRIGGER")) || S.Contains(TEXT("PROCESSED"))) OnShotText(S);
	}
}

// ------------------------------------------------------------------ connection lifecycle

uint32 FOTGMevoClient::Run()
{
	while (bRunning && !bStopRequested)
	{
		Err.Reset();
		Step.Reset();
		bDropped = false;
		const bool bClean = Session();
		if (!bClean && bRunning && !bStopRequested)
		{
			const FString At = Sock && !Step.IsEmpty() ? FString::Printf(TEXT(" (while %s)"), StepText(Step)) : FString();
			Log(FString::Printf(TEXT("Mevo+: %s%s"), *Err, *At));
			SetState(TEXT("searching"), Err + At);
		}
		Cleanup();
		for (const double Until = Now() + 4; bRunning && !bStopRequested && Now() < Until;) FPlatformProcess::Sleep(0.05f);
	}
	Cleanup();
	SetState(TEXT("off"));
	bFinished = true;
	return 0;
}

bool FOTGMevoClient::Session()
{
	SetState(TEXT("connecting"));
	if (!Connect()) return false;
	SetState(TEXT("handshake"));
	if (!Handshake()) return false;
	SetState(TEXT("arming"));
	bool bDisarmed;
	{
		FScopeLock L(&Ctl);
		bDisarmed = bWantDisarmed;
		Mode = WantMode;
		Using = Setup;
		bUsingSetup = bHasSetup;
		bSetupChanged = false;
	}
	if (bDisarmed) SetState(TEXT("idle"));
	else
	{
		Step = TEXT("settings");
		if (!Configure(ModeNumber(Mode))) return false;
		Step = TEXT("arming");
		if (!Arm()) return false;
		SetState(TEXT("ready"));
	}
	Step.Reset();
	KeepFails = 0;
	NextKeep = Now() + 1;
	while (bRunning)
	{
		if (!Pump(0.05))
		{
			Err = TEXT("connection to the Mevo+ dropped");
			return false;
		}
		while (Pending.Num())
		{
			const FMevoFrame F = Pending[0];
			Pending.RemoveAt(0);
			OnFrame(F);
		}
		if (bStopRequested)
		{
			// Done for now: disarm, so the radar stops watching for shots
			if (bArmed || State == TEXT("arming")) Disarm();
			return true;
		}
		if (Shot)
		{
			if (bFinishPending)
			{
				bFinishPending = false;
				if (!FinishShot())
				{
					Log(FString::Printf(TEXT("Mevo+: %s, reconnecting"), *Err));
					return false;
				}
			}
			else if (FlightDeadline && Now() > FlightDeadline && !Shot->bFinishing)
			{
				Log(TEXT("Mevo+: no \"PROCESSED\" after the flight result; reporting the shot"));
				OnShotText(TEXT("PROCESSED"));
			}
			else if (Now() > TriggerDeadline)
			{
				// Trigger but never processed: the session is stuck; start over
				Err = TEXT("shot never finished, reconnecting");
				return false;
			}
			continue;
		}
		if (!ApplyRequests()) return false;
		if (Now() >= NextKeep && (State == TEXT("ready") || State == TEXT("idle")))
		{
			NextKeep = Now() + 1;
			if (Keepalive()) KeepFails = 0;
			else if (bDropped) { Err = TEXT("connection to the Mevo+ dropped"); return false; }
			else if (++KeepFails >= 3) { Err = TEXT("not answering, reconnecting"); return false; }
		}
	}
	return true;
}

bool FOTGMevoClient::ApplyRequests()
{
	FString M;
	bool bDisarmWanted, bNewSetup;
	{
		FScopeLock L(&Ctl);
		M = WantMode;
		bDisarmWanted = bWantDisarmed;
		bNewSetup = bSetupChanged;
		if (bNewSetup) { Using = Setup; bUsingSetup = true; bSetupChanged = false; }
	}
	auto Fail = [&](const TCHAR* What) { Err = FString::Printf(TEXT("%s failed (%s)"), What, *Err); return false; };
	// Full swing / chipping / putting: disarm, set the mode, re-arm (if it's armed; otherwise the
	// mode is set when it next arms). New setup likewise.
	if ((M != Mode || bNewSetup) && bArmed)
	{
		Mode = M;
		SetState(TEXT("arming"));
		Disarm();
		if (!Configure(ModeNumber(Mode))) return Fail(TEXT("mode change"));
		if (bDisarmWanted) { SetState(TEXT("idle")); return true; }
		if (!Arm()) return Fail(TEXT("mode change"));
		SetState(TEXT("ready"));
		return true;
	}
	Mode = M;
	// Stay connected but stop detecting shots / start again
	if (!bDisarmWanted && State == TEXT("idle"))
	{
		SetState(TEXT("arming"));
		if (!Configure(ModeNumber(Mode)) || !Arm()) return Fail(TEXT("arming"));
		SetState(TEXT("ready"));
	}
	else if (bDisarmWanted && State == TEXT("ready"))
	{
		Disarm();
		SetState(TEXT("idle"));
	}
	return true;
}

// Phases 1-3: introduce ourselves to the DSP, AVR and PI, in the order FS Golf does. Only the status
// replies are required; the rest is information (model, firmware, network name), so a step that
// goes unanswered is noted and skipped. (Units differ in firmware, and the camera board can take a
// while to wake up.)
bool FOTGMevoClient::Handshake()
{
	auto From = [](uint8 Src, uint8 Type) { return [Src, Type](const FMevoFrame& M) { return M.Src == Src && M.Type == Type; }; };
	TArray<FString> Skipped;
	TMap<uint8, int32> Quiet; // a board that leaves two in a row unanswered isn't asked any more
	auto Opt = [&](uint8 Bus, uint8 Type, const TArray<uint8>& Payload, uint8 ReplyType, const TCHAR* What, FMevoFrame* Reply = nullptr, double Seconds = 3.0) {
		if (Quiet.FindRef(Bus) >= 2) { Skipped.AddUnique(What); return false; }
		if (Ask(Bus, Type, Payload, From(Bus, ReplyType), What, Seconds, Reply)) { Quiet.Add(Bus, 0); return true; }
		Quiet.FindOrAdd(Bus)++;
		Skipped.AddUnique(What);
		Err.Reset();
		return false;
	};
	// DSP
	Step = TEXT("DSP");
	if (!Ask(BUS_DSP, T::STATUS, { 1, 1 }, From(BUS_DSP, T::STATUS), TEXT("DSP status"))) return false;
	FMevoFrame Hw, Dsp, Pi, Net2, AvrInfo;
	const bool bHw = Opt(BUS_DSP, T::DSP_QUERY, {}, T::DSP_QUERY_RESP, TEXT("DSP query"), &Hw);
	if (bDropped) return false;
	const FString Model = bHw && Hw.Payload.Num() > 1 && Hw.Payload[1] == 0xc0 ? TEXT("Mevo Gen2") : TEXT("Mevo+");
	const bool bDsp = Opt(BUS_DSP, T::DEV_INFO, {}, T::DEV_INFO_RESP, TEXT("DSP info"), &Dsp);
	for (uint8 Q : TArray<uint8>{ 0x00, 0x08, 0x09 }) Opt(BUS_DSP, T::PROD_INFO, { 1, Q }, T::PROD_INFO, TEXT("product info"));
	Opt(BUS_DSP, T::CONFIG_QUERY, {}, T::CONFIG_RESP, TEXT("DSP config"));
	if (bDropped) return false;
	// AVR (the radar)
	Step = TEXT("radar");
	if (!Ask(BUS_AVR, T::STATUS, { 1, 1 }, From(BUS_AVR, T::STATUS), TEXT("AVR status"))) return false;
	Opt(BUS_AVR, T::STATUS, { 1, 1 }, T::STATUS, TEXT("AVR status"));
	bool bAvr = false;
	for (int32 i = 0; i < 2; i++) bAvr |= Opt(BUS_AVR, T::DEV_INFO, {}, T::DEV_INFO_RESP, TEXT("AVR info"), &AvrInfo);
	for (uint8 Id : TArray<uint8>{ 0x0c, 0x0d }) Opt(BUS_AVR, T::PARAM_READ, { 3, 0, 0, Id }, T::PARAM_VALUE, TEXT("AVR parameter"));
	Opt(BUS_AVR, T::CONFIG_QUERY, {}, T::CONFIG_RESP, TEXT("AVR config"));
	Opt(BUS_AVR, T::CAL_DATA_REQ, { 9, 0, 0, 3, 0, 0, 0, 0, 0, 0xa5 }, T::CAL_DATA_RESP, TEXT("factory calibration"));
	Opt(BUS_AVR, T::CAL_PARAM_REQ, { 2, 0, 8 }, T::CAL_PARAM_RESP, TEXT("calibration parameters"));
	Opt(BUS_AVR, T::AVR_CONFIG_QUERY, {}, T::AVR_CONFIG_RESP, TEXT("AVR settings"));
	Opt(BUS_AVR, T::PARAM_READ, { 3, 0, 0, 0x64 }, T::PARAM_VALUE, TEXT("AVR parameter"));
	const uint32 Unix = uint32(FDateTime::UtcNow().ToUnixTimestamp());
	Opt(BUS_AVR, T::TIME_SYNC, { 8, 0, uint8(Unix >> 24), uint8(Unix >> 16), uint8(Unix >> 8), uint8(Unix), 0, 0, 1 }, T::TIME_SYNC, TEXT("time sync"));
	if (bDropped) return false;
	// PI (camera board)
	Step = TEXT("camera");
	Opt(BUS_PI, T::STATUS, { 1, 3 }, T::STATUS, TEXT("camera status"), nullptr, 5.0);
	const bool bPi = Opt(BUS_PI, T::DEV_INFO, {}, T::DEV_INFO_RESP, TEXT("camera info"), &Pi);
	Opt(BUS_PI, T::PARAM_READ, { 3, 0, 0, 0x0a }, T::PARAM_VALUE, TEXT("camera parameter"));
	for (int32 i = 0; i < 2; i++) Opt(BUS_PI, T::CAM_CONFIG_REQ, { 2, 1, 5 }, T::CAM_CONFIG, TEXT("camera config"));
	Opt(BUS_PI, T::NET_CONFIG, { 1, 0 }, T::NET_CONFIG, TEXT("network name"));
	const bool bNet = Opt(BUS_PI, T::NET_CONFIG, { 1, 8 }, T::NET_CONFIG, TEXT("network settings"), &Net2);
	for (uint8 Id : TArray<uint8>{ 0x01, 0x07, 0x08, 0x09, 0x06, 0x0b, 0x03, 0x04, 0x05 }) Opt(BUS_PI, T::PARAM_READ, { 3, 0, 0, Id }, T::PARAM_VALUE, TEXT("camera parameter"));
	if (bDropped) return false;
	// "<SSID>\0<password>" after a length byte; only the network name is kept
	FString Ssid;
	if (bNet)
		for (int32 i = 1; i < Net2.Payload.Num() && Net2.Payload[i]; i++)
			if (Net2.Payload[i] >= 0x20 && Net2.Payload[i] <= 0x7e) Ssid.AppendChar(TCHAR(Net2.Payload[i]));
	Ssid.TrimStartAndEndInline();
	Out->Update([&](FOTGLMDeviceState& S) { S.Model = Model; S.Ssid = Ssid; });
	(void)bDsp; (void)bPi; (void)bAvr;
	FString Note;
	if (Skipped.Num()) Note = FString::Printf(TEXT(" (no answer to: %s)"), *FString::Join(Skipped, TEXT(", ")));
	Log(FString::Printf(TEXT("Mevo+: connected to %s %s%s"), *Model, *Ssid, *Note));
	return true;
}

// Phase 4: detection mode (the AVR wants a B0 [01 00] commit after each command). With a saved
// setup, also ball type, tracking share, tee height and the radar calibration (distance to ball,
// surface height), each followed by a commit, as FS Golf does.
bool FOTGMevoClient::Configure(int32 ModeNo)
{
	auto Commit = [&](const TCHAR* What) { return Ask(BUS_AVR, T::AVR_CONFIG_CMD, { 1, 0 }, [](const FMevoFrame& M) { return IsAck(M); }, What); };
	if (!Commit(TEXT("config gate"))) return false;
	if (bUsingSetup)
	{
		const FOTGMevoSetup& St = Using;
		TArray<TArray<uint8>> Params;
		TArray<uint8> P1 = { 6, 0, 0, 0x06 }; P1.Append(I24(St.BallType ? 1 : 0)); Params.Add(P1);
		TArray<uint8> P2 = { 8, 0, 0, 0x0f }; P2.Append(WriteFloat40(FMath::Clamp(St.TrackPct, 0.6, 1.0))); Params.Add(P2);
		TArray<uint8> P3 = { 8, 0, 0, 0x26 }; P3.Append(WriteFloat40(St.TeeHeightIn * 0.0254)); Params.Add(P3);
		for (const TArray<uint8>& P : Params)
		{
			if (!Ask(BUS_AVR, T::PARAM_VALUE, P, [](const FMevoFrame& M) { return IsAck(M); }, TEXT("setting ack"))) return false;
			if (!Commit(TEXT("setting commit"))) return false;
		}
	}
	if (!Ask(BUS_AVR, T::MODE_SET, { 2, 0, uint8(ModeNo) }, [](const FMevoFrame& M) { return M.Src == BUS_AVR && M.Type == T::MODE_SET; }, TEXT("mode echo"))) return false;
	if (!Commit(TEXT("mode commit"))) return false;
	if (bUsingSetup)
	{
		const int32 Mm = FMath::RoundToInt(FMath::Clamp(Using.DistanceM, 1.0, 6.0) * 1000);
		const int32 H = FMath::Clamp(FMath::FloorToInt(Using.SurfaceIn * 25.4), 0, 255);
		if (!Ask(BUS_AVR, T::RADAR_CAL, { 6, uint8(Mm >> 8), uint8(Mm & 255), 0, uint8(H), 0, 0 }, [](const FMevoFrame& M) { return M.Src == BUS_AVR && M.Type == T::RADAR_CAL; }, TEXT("calibration echo"))) return false;
		if (!Commit(TEXT("calibration commit"))) return false;
	}
	return true;
}

// Phase 6: DSP status, AVR status, ARM; the AVR may refuse (NACK) for a while after configuring
bool FOTGMevoClient::Arm()
{
	for (int32 Tries = 0; Tries < 40; Tries++)
	{
		if (!Ask(BUS_DSP, T::STATUS, { 1, 1 }, [](const FMevoFrame& M) { return M.Src == BUS_DSP && M.Type == T::STATUS; }, TEXT("DSP status"))) return false;
		if (!Ask(BUS_AVR, T::STATUS, { 1, 1 }, [](const FMevoFrame& M) { return M.Src == BUS_AVR && M.Type == T::STATUS; }, TEXT("AVR status"))) return false;
		FMevoFrame R;
		if (!Ask(BUS_AVR, T::AVR_CONFIG_CMD, { 1, 1 }, [](const FMevoFrame& M) { return M.Src == BUS_AVR && (M.Type == T::CONFIG_ACK || M.Type == T::CONFIG_NACK); }, TEXT("arm reply"), 3.0, &R)) return false;
		if (R.Type == T::CONFIG_NACK)
		{
			Idle(0.75);
			continue;
		}
		// ARMED, and the camera board's status (optional), whichever comes first
		bool bGotArmed = false, bGotPi = false;
		Send(BUS_PI, T::STATUS, { 1, 3 });
		WaitFor([&](const FMevoFrame& M) {
			bGotArmed |= ArmedText(M);
			bGotPi |= M.Src == BUS_PI && M.Type == T::STATUS;
			return bGotArmed && bGotPi;
		}, 5.0, TEXT("ARMED"));
		if (!bGotArmed)
		{
			Err = TEXT("no ARMED from the Mevo+");
			return false;
		}
		Err.Reset();
		bArmed = true;
		return true;
	}
	Err = TEXT("the Mevo+ would not arm");
	return false;
}

void FOTGMevoClient::Disarm()
{
	bool bReset = false;
	auto ResetMsg = [](const FMevoFrame& M) { return M.Type == T::MODE_ACK || (M.Type == T::TXT && Text(M.Payload).Contains(TEXT("ARMED CANCELLED"))); };
	Ask(BUS_AVR, T::AVR_CONFIG_CMD, { 1, 0 }, [&](const FMevoFrame& M) {
		if (ResetMsg(M)) { bReset = true; return false; }
		return IsAck(M);
	}, TEXT("disarm ack"));
	if (!bReset) WaitFor(ResetMsg, 3.0, TEXT("disarm"));
	WaitFor([](const FMevoFrame& M) { return (M.Type == T::TXT && Text(M.Payload).Contains(TEXT("System State"))) || M.Type == T::MODE_ACK; }, 1.5, TEXT("idle"));
	if (!bDropped) Err.Reset();
	bArmed = false;
}

// Status polls every second keep the session alive (not during a shot)
bool FOTGMevoClient::Keepalive()
{
	DrainPending();
	bool bDsp = false, bAvr = false, bPi = false;
	Send(BUS_DSP, T::STATUS, { 1, 1 });
	Send(BUS_AVR, T::STATUS, { 1, 1 });
	Send(BUS_PI, T::STATUS, { 1, 3 });
	WaitFor([&](const FMevoFrame& M) {
		if (M.Type == T::STATUS) { bDsp |= M.Src == BUS_DSP; bAvr |= M.Src == BUS_AVR; bPi |= M.Src == BUS_PI; }
		return bDsp && bAvr && bPi;
	}, 3.0, TEXT("status"));
	Err.Reset();
	return bDsp && bAvr;
}

// ------------------------------------------------------------------ shots

void FOTGMevoClient::StartShot()
{
	Shot.Emplace();
	bArmed = false;
	bFinishPending = false;
	FlightDeadline = 0;
	TriggerDeadline = Now() + 30;
	SetState(TEXT("shot"));
}

void FOTGMevoClient::OnShotText(const FString& S)
{
	if (S.Contains(TEXT("BALL TRIGGER")))
	{
		if (Shot && !Shot->bFinishing) return; // (the same trigger, reported twice)
		Log(TEXT("Mevo+: ball trigger"));
		StartShot();
	}
	else if (S.Contains(TEXT("PROCESSED")))
	{
		if (Shot && Shot->bFinishing) return;
		if (!Shot) { Shot.Emplace(); TriggerDeadline = Now() + 30; }
		Shot->bFinishing = true;
		FlightDeadline = 0;
		bFinishPending = true;
	}
}

// Report the shot as soon as the device says PROCESSED (its results are all in by then), then
// acknowledge, drain to IDLE and re-arm (the device goes dormant if it isn't re-armed)
bool FOTGMevoClient::FinishShot()
{
	// (the flight result normally comes just before PROCESSED; allow it a moment if not)
	for (int32 i = 0; i < 10 && !Shot->Flight; i++)
	{
		Idle(0.1);
		if (bDropped) return false;
	}
	Shot->bDone = true;
	if (Shot->Flight)
	{
		const FOTGBallData Ball = ToBallData(*Shot->Flight, Shot->Spin.GetPtrOrNull(), Shot->Club.GetPtrOrNull());
		FString Device;
		const FOTGLMDeviceState Now_ = Out->Read();
		Device = Now_.Model.IsEmpty() ? TEXT("Mevo+") : (Now_.Model + TEXT(" ") + Now_.Ssid).TrimEnd();
		FOTGShot S = OTGToShot(Ball, TEXT("mevo"), Device, ++ShotCount);
		Log(FString::Printf(TEXT("Mevo+: shot %.1f mph, launch %.1f, direction %.1f, spin %d"), *Ball.Speed, *Ball.VLA, *Ball.HLA, FMath::RoundToInt(*Ball.TotalSpin)));
		// Held: the game arms it again when it's the player's turn
		{
			FScopeLock L(&Ctl);
			if (bHoldAfterShot) bWantDisarmed = true;
		}
		Out->Shot(S);
	}
	else Log(TEXT("Mevo+: shot without a usable flight result (miss-hit?)"));
	Send(BUS_AVR, T::SHOT_DATA_ACK);
	Send(BUS_AVR, T::SHOT_DATA_ACK);
	// (some firmware never sends IDLE)
	const bool bIdle = WaitFor([](const FMevoFrame& M) { return M.Type == T::SHOT_TEXT && Text(M.Payload).Contains(TEXT("IDLE")); }, 4.0, TEXT("IDLE"));
	if (bDropped) return false;
	if (bIdle) Ask(BUS_AVR, T::SHOT_RESULT_REQ, {}, [](const FMevoFrame& M) { return M.Src == BUS_AVR && M.Type == T::CLUB; }, TEXT("club result"), 1.5);
	if (bDropped) return false;
	Err.Reset();
	Shot.Reset();
	// Re-arm, in the current mode if it changed meanwhile (unless it's to stay disarmed)
	FString M;
	bool bDisarmWanted, bNewSetup;
	{
		FScopeLock L(&Ctl);
		M = WantMode;
		bDisarmWanted = bWantDisarmed;
		bNewSetup = bSetupChanged;
		if (bNewSetup) { Using = Setup; bUsingSetup = true; bSetupChanged = false; }
	}
	if (M != Mode || bNewSetup)
	{
		Mode = M;
		if (!Configure(ModeNumber(Mode))) return false;
	}
	if (bDisarmWanted)
	{
		bArmed = false;
		SetState(TEXT("idle"));
		return true;
	}
	if (!Ask(BUS_AVR, T::AVR_CONFIG_CMD, { 1, 1 }, [](const FMevoFrame& Fr) { return IsAck(Fr); }, TEXT("arm ack"))) return false;
	if (!WaitFor(ArmedText, 4.0, TEXT("ARMED"))) return false;
	bArmed = true;
	SetState(TEXT("ready"));
	return true;
}
