// See OTGFrp.h (lib/frp.js and lib/bridges.js).

#include "OTGFrp.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformTime.h"
#include "IWebSocket.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "WebSocketsModule.h"

namespace
{
	const double RetrySeconds[] = { 1, 2, 4, 8 };
	const double StaleSeconds = 10; // a shot with no shot_finished this long after its last event is dropped

	TOptional<double> UnitValue(const TSharedPtr<FJsonValue>& V, TFunctionRef<double(const FString&)> Factor)
	{
		if (!V.IsValid()) return {};
		if (V->Type == EJson::Number) return V->AsNumber();
		if (V->Type != EJson::String) return {};
		FString S = V->AsString().TrimStartAndEnd();
		int32 i = 0;
		while (i < S.Len() && (FChar::IsDigit(S[i]) || S[i] == '-' || S[i] == '.' || S[i] == '+' || ((S[i] == 'e' || S[i] == 'E') && i + 1 < S.Len() && (FChar::IsDigit(S[i + 1]) || S[i + 1] == '-' || S[i + 1] == '+'))))
			i++;
		const FString NumPart = S.Left(i), Unit = S.Mid(i).TrimStartAndEnd().ToLower();
		if (NumPart.IsEmpty() || Unit.IsEmpty() || !NumPart.IsNumeric()) return {};
		const double F = Factor(Unit);
		if (F <= 0) return {};
		return FCString::Atod(*NumPart) * F;
	}
	TOptional<double> NumOf(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		const TSharedPtr<FJsonValue> V = O ? O->TryGetField(Key) : nullptr;
		if (!V.IsValid() || V->IsNull()) return {};
		if (V->Type == EJson::Number) return V->AsNumber();
		if (V->Type == EJson::String && !V->AsString().IsEmpty() && V->AsString().IsNumeric()) return FCString::Atod(*V->AsString());
		return {};
	}
	FString Json(const TSharedRef<FJsonObject>& O)
	{
		FString S;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&S);
		FJsonSerializer::Serialize(O, W);
		return S;
	}
}

TOptional<double> OTGFrpSpeed(const TSharedPtr<FJsonValue>& V)
{
	return UnitValue(V, [](const FString& U) {
		if (U == TEXT("mps")) return 1.0;
		if (U == TEXT("mph")) return 0.44704;
		if (U == TEXT("kph")) return 1 / 3.6;
		if (U == TEXT("fps")) return 0.3048;
		return 0.0;
	});
}
TOptional<double> OTGFrpDistance(const TSharedPtr<FJsonValue>& V)
{
	return UnitValue(V, [](const FString& U) {
		if (U == TEXT("m")) return 1.0;
		if (U == TEXT("yd")) return 0.9144;
		if (U == TEXT("ft")) return 0.3048;
		if (U == TEXT("in")) return 0.0254;
		if (U == TEXT("mm")) return 0.001;
		if (U == TEXT("cm")) return 0.01;
		return 0.0;
	});
}

bool OTGFrpToBallData(const TSharedPtr<FJsonObject>& Ball, const TSharedPtr<FJsonObject>& Club, FOTGBallData& Out)
{
	if (!Ball) return false;
	const TOptional<double> Speed = OTGFrpSpeed(Ball->TryGetField(TEXT("launch_speed")));
	if (!Speed) return false;
	TOptional<double> Back = NumOf(Ball, TEXT("backspin_rpm")), Side = NumOf(Ball, TEXT("sidespin_rpm"));
	const TOptional<double> Total = NumOf(Ball, TEXT("total_spin_rpm")), Axis = NumOf(Ball, TEXT("spin_axis"));
	if ((!Back || !Side) && Total)
	{
		const double A = FMath::DegreesToRadians(Axis.Get(0));
		if (!Back) Back = *Total * FMath::Cos(A);
		if (!Side) Side = *Total * FMath::Sin(A);
	}
	const double B = Back.Get(0), S = Side.Get(0);
	const double Tot = Total ? *Total : FMath::Sqrt(B * B + S * S);
	Out.Speed = *Speed / 0.44704;
	Out.VLA = NumOf(Ball, TEXT("launch_elevation")).Get(0);
	Out.HLA = NumOf(Ball, TEXT("launch_azimuth")).Get(0);
	Out.BackSpin = B; Out.SideSpin = S; Out.TotalSpin = Tot;
	Out.SpinAxis = Axis ? *Axis : (Tot ? FMath::RadiansToDegrees(FMath::Atan2(S, B)) : 0.0);
	if (const TOptional<double> Carry = OTGFrpDistance(Ball->TryGetField(TEXT("carry_distance")))) Out.CarryYd = *Carry / 0.9144;
	if (Club)
	{
		Out.Club.bValid = true;
		const TOptional<double> Cs = OTGFrpSpeed(Club->TryGetField(TEXT("club_speed")));
		Out.Club.SpeedMph = Cs ? *Cs / 0.44704 : NAN;
		Out.Club.AngleOfAttack = NumOf(Club, TEXT("attack_angle")).Get(NAN);
		Out.Club.FaceToTarget = NumOf(Club, TEXT("face_angle")).Get(NAN);
		Out.Club.Path = NumOf(Club, TEXT("path")).Get(NAN);
		Out.Club.Loft = NumOf(Club, TEXT("dynamic_loft")).Get(NAN);
		Out.Club.SmashFactor = NumOf(Club, TEXT("smash_factor")).Get(NAN);
	}
	return true;
}

// ------------------------------------------------------------------ the controller

FOTGFrpController::FOTGFrpController(const FString& InUrl, const FString& InSource, TSharedRef<FOTGLMOutbox> InOut)
	: Url(InUrl), Source(InSource), Out(InOut)
{
	Status([](FOTGLMDeviceState& S) { S.State = TEXT("off"); S.Text = TEXT("Not connected"); });
}

FOTGFrpController::~FOTGFrpController()
{
	Stop();
}

void FOTGFrpController::Start()
{
	if (bRunning) return;
	bRunning = true;
	Connect();
}

void FOTGFrpController::Stop()
{
	bRunning = false;
	RetryAt = 0;
	Gen++; // (the old socket's callbacks are ignored from now on)
	if (Ws)
	{
		Ws->Close();
		Ws.Reset();
	}
	Shots.Reset();
	bInit = false;
	Status([](FOTGLMDeviceState& S) { S.State = TEXT("off"); S.Text = TEXT("Not connected"); S.Ready = 0; S.bConnected = false; });
}

void FOTGFrpController::Connect()
{
	if (!bRunning) return;
	Status([&](FOTGLMDeviceState& S) { S.State = TEXT("connecting"); S.Text = FString::Printf(TEXT("Connecting to %s..."), *Url); });
	FWebSocketsModule& M = FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
	Ws = M.CreateWebSocket(Url, TArray<FString>(), TMap<FString, FString>());
	if (!Ws)
	{
		Status([](FOTGLMDeviceState& S) { S.State = TEXT("error"); S.Text = TEXT("Bad address"); });
		return Retry();
	}
	const int32 G = ++Gen;
	bOpened = false;
	bInit = false;
	Ws->OnConnected().AddSP(this, &FOTGFrpController::OnConnected, G);
	Ws->OnConnectionError().AddSP(this, &FOTGFrpController::OnError, G);
	Ws->OnClosed().AddSP(this, &FOTGFrpController::OnClosed, G);
	Ws->OnMessage().AddSP(this, &FOTGFrpController::OnMessage, G);
	Ws->Connect();
}

void FOTGFrpController::Retry()
{
	RetryAt = FPlatformTime::Seconds() + RetrySeconds[FMath::Min(Tries++, 3)];
}

void FOTGFrpController::Tick(double Now)
{
	if (bRunning && RetryAt && Now >= RetryAt)
	{
		RetryAt = 0;
		Connect();
	}
}

void FOTGFrpController::Send(const FString& Text)
{
	if (Ws && Ws->IsConnected()) Ws->Send(Text);
}

void FOTGFrpController::SetMode(const FString& InMode, const FString& InHanded)
{
	FString NextMode = Mode, NextHanded = Handed;
	if (InMode == TEXT("full") || InMode == TEXT("putting") || InMode == TEXT("chipping")) NextMode = InMode;
	if (InHanded == TEXT("rh") || InHanded == TEXT("lh")) NextHanded = InHanded;
	if (NextMode == Mode && NextHanded == Handed) return;
	Mode = NextMode;
	Handed = NextHanded;
	if (bInit)
	{
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("kind"), TEXT("set_detection_mode"));
		if (!Mode.IsEmpty()) O->SetStringField(TEXT("mode"), Mode);
		if (!Handed.IsEmpty()) O->SetStringField(TEXT("handed"), Handed);
		Send(Json(O));
	}
}

void FOTGFrpController::OnConnected(int32 G)
{
	if (G != Gen) return;
	bOpened = true;
	const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("kind"), TEXT("start"));
	TArray<TSharedPtr<FJsonValue>> Versions;
	Versions.Add(MakeShared<FJsonValueString>(TEXT("0.1.0")));
	O->SetArrayField(TEXT("version"), Versions);
	O->SetStringField(TEXT("name"), TEXT("OpenTeeGolf"));
	Send(Json(O));
}

void FOTGFrpController::OnError(const FString& Error, int32 G)
{
	OnClosed(0, Error, false, G);
}

void FOTGFrpController::OnClosed(int32, const FString&, bool, int32 G)
{
	if (G != Gen) return;
	Gen++;
	Ws.Reset();
	bInit = false;
	if (!bRunning) return;
	if (bOpened) UE_LOG(LogOTGLM, Display, TEXT("FRP: %s closed the connection"), *Url);
	const bool bWas = bOpened;
	Status([&](FOTGLMDeviceState& S) {
		S.State = TEXT("connecting");
		S.Text = bWas ? TEXT("Connection lost, reconnecting...") : TEXT("Waiting for the launch monitor...");
		S.Ready = 0;
		S.bConnected = false;
	});
	Retry();
}

void FOTGFrpController::OnMessage(const FString& Text, int32 G)
{
	if (G != Gen) return;
	TSharedPtr<FJsonObject> Msg;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Msg) && Msg) Handle(Msg);
}

void FOTGFrpController::Handle(const TSharedPtr<FJsonObject>& Msg)
{
	FString Kind;
	Msg->TryGetStringField(TEXT("kind"), Kind);
	// protocol messages (no envelope)
	if (Kind == TEXT("init"))
	{
		Tries = 0;
		bInit = true;
		FString Version;
		Msg->TryGetStringField(TEXT("version"), Version);
		UE_LOG(LogOTGLM, Display, TEXT("FRP: connected to %s (FRP %s)"), *Url, *Version);
		Status([](FOTGLMDeviceState& S) { S.State = TEXT("connected"); S.Text = TEXT("Connected"); S.Error.Reset(); S.bConnected = true; });
		if (!Mode.IsEmpty() || !Handed.IsEmpty())
		{
			const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("kind"), TEXT("set_detection_mode"));
			if (!Mode.IsEmpty()) O->SetStringField(TEXT("mode"), Mode);
			if (!Handed.IsEmpty()) O->SetStringField(TEXT("handed"), Handed);
			Send(Json(O));
		}
		return;
	}
	if (Kind == TEXT("alert"))
	{
		FString Sev, Message;
		Msg->TryGetStringField(TEXT("severity"), Sev);
		Msg->TryGetStringField(TEXT("message"), Message);
		UE_LOG(LogOTGLM, Display, TEXT("FRP %s: %s"), *Sev, *Message);
		if (Sev != TEXT("warn")) Status([&](FOTGLMDeviceState& S) { S.Error = Message; });
		return;
	}
	const TSharedPtr<FJsonObject>* EvP = nullptr;
	if (!Msg->TryGetObjectField(TEXT("event"), EvP)) return;
	const TSharedPtr<FJsonObject> Ev = *EvP;
	FString Device;
	if (Msg->TryGetStringField(TEXT("device"), Device) && !Device.IsEmpty()) Status([&](FOTGLMDeviceState& S) { S.Device = Device; });
	FString EvKind;
	Ev->TryGetStringField(TEXT("kind"), EvKind);
	if (EvKind == TEXT("device_telemetry"))
	{
		FString Model, Maker;
		Ev->TryGetStringField(TEXT("model"), Model);
		Ev->TryGetStringField(TEXT("manufacturer"), Maker);
		const TSharedPtr<FJsonObject>* TP = nullptr;
		const TSharedPtr<FJsonObject> Tel = Ev->TryGetObjectField(TEXT("telemetry"), TP) ? *TP : MakeShared<FJsonObject>();
		Status([&](FOTGLMDeviceState& S) {
			if (!Model.IsEmpty()) S.Model = Maker.IsEmpty() ? Model : Maker + TEXT(" ") + Model;
			if (const TSharedPtr<FJsonValue> R = Tel->TryGetField(TEXT("ready")))
				S.Ready = (R->Type == EJson::Boolean ? R->AsBool() : R->AsString() == TEXT("true")) ? 1 : 0;
			if (const TOptional<double> Bat = NumOf(Tel, TEXT("battery_pct"))) S.Battery = FMath::RoundToInt(*Bat);
		});
		return;
	}
	if (EvKind == TEXT("alert"))
	{
		FString Sev, Message;
		Ev->TryGetStringField(TEXT("severity"), Sev);
		Ev->TryGetStringField(TEXT("message"), Message);
		UE_LOG(LogOTGLM, Display, TEXT("FRP %s %s: %s"), *Device, *Sev, *Message);
		if (Sev != TEXT("warn")) Status([&](FOTGLMDeviceState& S) { S.Error = Message; });
		return;
	}
	const TSharedPtr<FJsonObject>* KeyP = nullptr;
	FString Id;
	if (!Ev->TryGetObjectField(TEXT("key"), KeyP) || !(*KeyP)->TryGetStringField(TEXT("shot_id"), Id) || Id.IsEmpty()) return;
	const double Now = FPlatformTime::Seconds();
	for (auto It = Shots.CreateIterator(); It; ++It) if (Now - It->Value.At > StaleSeconds) It.RemoveCurrent();
	FShotParts* S = Shots.Find(Id);
	if (!S)
	{
		S = &Shots.Add(Id);
		double N;
		if ((*KeyP)->TryGetNumberField(TEXT("shot_number"), N)) S->Number = int32(N);
	}
	S->At = Now;
	const TSharedPtr<FJsonObject>* P = nullptr;
	if (EvKind == TEXT("ball_flight")) { if (Ev->TryGetObjectField(TEXT("ball"), P)) S->Ball = *P; }
	else if (EvKind == TEXT("club_path")) { if (Ev->TryGetObjectField(TEXT("club"), P)) S->Club = *P; }
	else if (EvKind == TEXT("shot_finished"))
	{
		const FShotParts Done = *S;
		Shots.Remove(Id);
		FOTGBallData B;
		if (OTGFrpToBallData(Done.Ball, Done.Club, B))
		{
			const FOTGLMDeviceState St = Out->Read();
			const FString Name = !St.Model.IsEmpty() ? St.Model : !Device.IsEmpty() ? Device : FOTGBridgeProcess::NameOf(Source);
			const FOTGShot Shot = OTGToShot(B, Source, Name, Done.Number);
			if (Shot.SpeedMph > 0) Out->Shot(Shot);
		}
		else UE_LOG(LogOTGLM, Display, TEXT("FRP: shot %s had no ball data"), *Id);
	}
}

// ------------------------------------------------------------------ bridge programs

FString FOTGBridgeProcess::NameOf(const FString& Kind)
{
	if (Kind == TEXT("r10")) return TEXT("Garmin R10");
	if (Kind == TEXT("square")) return TEXT("Square Golf");
	return TEXT("Launch monitor");
}

FString FOTGBridgeProcess::FindProgram(const FString& Kind, FString& Searched)
{
	const FString File = Kind == TEXT("r10") ? TEXT("tenover-frp-windows-x86_64.exe") : TEXT("allsquare-frp-windows-x86_64.exe");
	// Next to the game, in the game project, or in the web version's bridges folder (which
	// downloads them on first use)
	const TArray<FString> Dirs = {
		FPaths::Combine(FPaths::LaunchDir(), TEXT("bridges")),
		FPaths::Combine(FPaths::ProjectDir(), TEXT("bridges")),
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../../bridges")),
	};
	Searched.Reset();
	for (const FString& D : Dirs)
	{
		const FString P = FPaths::ConvertRelativePathToFull(FPaths::Combine(D, File));
		if (FPaths::FileExists(P)) return P;
		Searched += (Searched.IsEmpty() ? TEXT("") : TEXT(", ")) + FPaths::ConvertRelativePathToFull(D);
	}
	return FString();
}

FOTGBridgeProcess::FOTGBridgeProcess(const FString& InKind, int32 InPort, const TArray<FString>& InArgs, const FString& InCommand, TSharedRef<FOTGLMOutbox> InOut)
	: Kind(InKind), Command(InCommand), Port(InPort), Args(InArgs), Out(InOut)
{
}

FOTGBridgeProcess::~FOTGBridgeProcess()
{
	Stop();
}

void FOTGBridgeProcess::Start()
{
	if (bRunning) return;
	bRunning = true;
	Launch();
}

void FOTGBridgeProcess::Kill()
{
	if (Proc.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(Proc)) FPlatformProcess::TerminateProc(Proc, true);
		FPlatformProcess::CloseProc(Proc);
		Proc.Reset();
	}
	if (ReadPipe || WritePipe) FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
	ReadPipe = WritePipe = nullptr;
	Buf.Reset();
}

void FOTGBridgeProcess::Stop()
{
	bRunning = false;
	RelaunchAt = 0;
	Kill();
	Out->Update([](FOTGLMDeviceState& S) { S.State = TEXT("off"); S.Text = TEXT("Not connected"); S.Error.Reset(); });
}

void FOTGBridgeProcess::Restart(const TArray<FString>& NewArgs)
{
	Args = NewArgs;
	if (!bRunning) return;
	Kill();
	RelaunchAt = FPlatformTime::Seconds() + 0.3;
}

void FOTGBridgeProcess::Launch()
{
	if (!bRunning) return;
	const FString Name = NameOf(Kind);
	FString Exe, Params;
	if (!Command.IsEmpty())
	{
		// "program first-args..." (the program may be quoted) or "program|arg|arg"
		FString Rest = Command.TrimStartAndEnd();
		if (Rest.Contains(TEXT("|")))
		{
			// ("program|arg|arg", for paths with spaces)
			TArray<FString> Parts;
			Rest.ParseIntoArray(Parts, TEXT("|"));
			Exe = Parts[0];
			for (int32 i = 1; i < Parts.Num(); i++) Params += (i > 1 ? TEXT(" ") : TEXT("")) + (Parts[i].Contains(TEXT(" ")) ? TEXT("\"") + Parts[i] + TEXT("\"") : Parts[i]);
		}
		else if (Rest.StartsWith(TEXT("\"")))
		{
			const int32 End = Rest.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
			Exe = Rest.Mid(1, End - 1);
			Params = Rest.Mid(End + 1).TrimStart();
		}
		else if (!Rest.Split(TEXT(" "), &Exe, &Params)) Exe = Rest;
	}
	else
	{
		FString Searched;
		Exe = FindProgram(Kind, Searched);
		if (Exe.IsEmpty())
		{
			const FString Why = FString::Printf(TEXT("The %s bridge program isn't installed (looked in %s)"), *Name, *Searched);
			UE_LOG(LogOTGLM, Warning, TEXT("%s"), *Why);
			Out->Update([&](FOTGLMDeviceState& S) { S.State = TEXT("error"); S.Text = FString::Printf(TEXT("Couldn't find the %s bridge"), *Name); S.Error = Why; });
			RelaunchAt = FPlatformTime::Seconds() + 30;
			return;
		}
	}
	Params += FString::Printf(TEXT(" 127.0.0.1:%d"), Port);
	for (const FString& A : Args) Params += TEXT(" ") + A;
	Out->Update([&](FOTGLMDeviceState& S) { S.State = TEXT("starting"); S.Text = FString::Printf(TEXT("Starting the %s bridge..."), *Name); S.Error.Reset(); });
	FPlatformProcess::CreatePipe(ReadPipe, WritePipe);
	Proc = FPlatformProcess::CreateProc(*Exe, *Params.TrimStart(), false, true, true, nullptr, 0, nullptr, WritePipe, nullptr, WritePipe); // (stdout and stderr: the bridges report on stderr)
	if (!Proc.IsValid())
	{
		UE_LOG(LogOTGLM, Warning, TEXT("%s: couldn't start the bridge (%s)"), *Name, *Exe);
		Out->Update([&](FOTGLMDeviceState& S) { S.State = TEXT("error"); S.Text = TEXT("Couldn't start the bridge"); S.Error = Exe; });
		Kill();
		RelaunchAt = FPlatformTime::Seconds() + 30;
		return;
	}
	UE_LOG(LogOTGLM, Display, TEXT("%s: started %s on port %d"), *Name, *FPaths::GetCleanFilename(Exe), Port);
}

// What a bridge's progress lines mean for the player ("tenover-frp: searching for Garmin R10...")
void FOTGBridgeProcess::OnLine(const FString& Raw)
{
	UE_LOG(LogOTGLM, Log, TEXT("bridge: %s"), *Raw);
	FString T = Raw;
	int32 Colon;
	if (T.FindChar(':', Colon) && T.Left(Colon).EndsWith(TEXT("-frp"))) T = T.Mid(Colon + 1).TrimStart();
	const FString L = T.ToLower();
	FString State, Text;
	bool bError = false;
	if (L.StartsWith(TEXT("searching")) || L.StartsWith(TEXT("scanning"))) { State = TEXT("searching"); Text = TEXT("Searching over Bluetooth..."); }
	else if (L.StartsWith(TEXT("connected"))) { State = TEXT("connecting"); Text = TEXT("Found it, connecting..."); }
	else if (L.StartsWith(TEXT("registered")) || L.StartsWith(TEXT("handshake complete")) || L.StartsWith(TEXT("model "))) { State = TEXT("connecting"); Text = TEXT("Setting up..."); }
	else if (L.StartsWith(TEXT("ready")) || L.StartsWith(TEXT("armed"))) { State = TEXT("ready"); Text = TEXT("ready"); }
	else if (L.StartsWith(TEXT("connection failed")) || L.StartsWith(TEXT("failed to connect"))) { State = TEXT("searching"); Text = TEXT("Not found: is it on, and (for the R10) paired in Windows Bluetooth settings?"); bError = true; }
	else if (L.Contains(TEXT("failed to open frp endpoint"))) { State = TEXT("error"); Text = TEXT("Its port is in use by another program"); bError = true; }
	else if (L.StartsWith(TEXT("transport error")) || L.StartsWith(TEXT("poll error")) || L.StartsWith(TEXT("device error"))) { State = TEXT("searching"); Text = TEXT("Bluetooth connection lost, reconnecting..."); bError = true; }
	if (State.IsEmpty()) return;
	Out->Update([&](FOTGLMDeviceState& S) { S.State = State; S.Text = Text; S.Error = bError ? T : FString(); });
}

void FOTGBridgeProcess::Tick(double Now)
{
	if (!bRunning) return;
	if (!Proc.IsValid())
	{
		if (RelaunchAt && Now >= RelaunchAt)
		{
			RelaunchAt = 0;
			Launch();
		}
		return;
	}
	Buf += FPlatformProcess::ReadPipe(ReadPipe);
	int32 Nl;
	while (Buf.FindChar('\n', Nl))
	{
		const FString Line = Buf.Left(Nl).TrimStartAndEnd();
		Buf.MidInline(Nl + 1);
		if (!Line.IsEmpty()) OnLine(Line);
	}
	if (!FPlatformProcess::IsProcRunning(Proc))
	{
		int32 Code = 0;
		FPlatformProcess::GetProcReturnCode(Proc, &Code);
		Buf += FPlatformProcess::ReadPipe(ReadPipe);
		for (const FString& Line : TArray<FString>{ Buf }) if (!Line.TrimStartAndEnd().IsEmpty()) OnLine(Line.TrimStartAndEnd());
		Kill();
		UE_LOG(LogOTGLM, Display, TEXT("%s: bridge stopped (%d), trying again shortly"), *NameOf(Kind), Code);
		const FOTGLMDeviceState S = Out->Read();
		if (S.State != TEXT("error") && S.State != TEXT("searching"))
			Out->Update([](FOTGLMDeviceState& D) { D.State = TEXT("searching"); D.Text = TEXT("Connection lost, reconnecting..."); });
		RelaunchAt = Now + 4;
	}
}
