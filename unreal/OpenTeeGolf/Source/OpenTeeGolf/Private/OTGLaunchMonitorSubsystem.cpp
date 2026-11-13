// See OTGLaunchMonitorSubsystem.h.

#include "OTGLaunchMonitorSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "LaunchMonitor/OTGFrp.h"
#include "LaunchMonitor/OTGLMOutbox.h"
#include "LaunchMonitor/OTGMevoClient.h"
#include "LaunchMonitor/OTGOpenConnect.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogOTGLM);

namespace
{
	const TCHAR* Section = TEXT("OpenTeeGolf.LaunchMonitor");

	const TCHAR* KindName(EOTGConnection K)
	{
		switch (K)
		{
		case EOTGConnection::Direct: return TEXT("direct");
		case EOTGConnection::R10: return TEXT("r10");
		case EOTGConnection::Square: return TEXT("square");
		case EOTGConnection::Frp: return TEXT("frp");
		default: return TEXT("fsgolf");
		}
	}
	bool KindOf(const FString& S, EOTGConnection& Out)
	{
		for (EOTGConnection K : { EOTGConnection::FSGolf, EOTGConnection::Direct, EOTGConnection::R10, EOTGConnection::Square, EOTGConnection::Frp })
			if (S.Equals(KindName(K), ESearchCase::IgnoreCase)) { Out = K; return true; }
		return false;
	}
	FString DirectText(const FString& State)
	{
		if (State == TEXT("connecting")) return TEXT("Looking for the Mevo+...");
		if (State == TEXT("searching")) return TEXT("Mevo+ not found");
		if (State == TEXT("handshake")) return TEXT("Connecting to the Mevo+...");
		if (State == TEXT("arming")) return TEXT("Arming...");
		if (State == TEXT("ready")) return TEXT("ready");
		if (State == TEXT("shot")) return TEXT("Reading shot...");
		if (State == TEXT("idle")) return TEXT("disarmed");
		if (State == TEXT("off")) return TEXT("disconnected");
		return State;
	}
	bool SameNum(double A, double B) { return A == B || (FMath::IsNaN(A) && FMath::IsNaN(B)); }
	bool Same(const FOTGLaunchMonitorStatus& A, const FOTGLaunchMonitorStatus& B)
	{
		return A.Connection == B.Connection && A.bConnected == B.bConnected && A.bReady == B.bReady && A.Device == B.Device && A.Battery == B.Battery
			&& A.State == B.State && A.Text == B.Text && A.Error == B.Error && A.Mode == B.Mode && A.PortError == B.PortError
			&& SameNum(A.Tilt, B.Tilt) && SameNum(A.Roll, B.Roll) && SameNum(A.TempC, B.TempC) && A.DeviceText == B.DeviceText && A.LastShotAt == B.LastShotAt;
	}
	FString Num(double V) { return FMath::IsFinite(V) ? FString::Printf(TEXT("%.4f"), V) : TEXT("null"); }

	UOTGLaunchMonitorSubsystem* Of(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UOTGLaunchMonitorSubsystem>() : nullptr;
	}
	using FCmd = FConsoleCommandWithWorldAndArgsDelegate;
	FAutoConsoleCommandWithWorldAndArgs StatusCmd(TEXT("OTG.LM.Status"), TEXT("The launch monitor connection's status"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) {
		if (UOTGLaunchMonitorSubsystem* S = Of(W))
		{
			const FOTGLaunchMonitorStatus St = S->GetStatus();
			UE_LOG(LogOTGLM, Display, TEXT("%s: %s%s%s (state %s%s%s)%s"), KindName(St.Connection), St.bConnected ? TEXT("connected") : TEXT("not connected"), St.bReady ? TEXT(", ready") : TEXT(""),
				St.Device.IsEmpty() ? TEXT("") : *(TEXT(", ") + St.Device), *St.State, St.Mode.IsEmpty() ? TEXT("") : *(TEXT(", mode ") + St.Mode),
				St.Battery >= 0 ? *FString::Printf(TEXT(", battery %d%%"), St.Battery) : TEXT(""), St.Error.IsEmpty() ? TEXT("") : *(TEXT(": ") + St.Error));
			if (!St.PortError.IsEmpty()) UE_LOG(LogOTGLM, Display, TEXT("OpenConnect: %s"), *St.PortError);
		}
	}));
	FAutoConsoleCommandWithWorldAndArgs ConnectCmd(TEXT("OTG.LM.Connect"), TEXT("OTG.LM.Connect fsgolf|direct|r10|square|frp [address]: how shots arrive"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		EOTGConnection K;
		if (UOTGLaunchMonitorSubsystem* S = Of(W); S && A.Num() && KindOf(A[0], K)) S->SetConnection(K, A.Num() > 1 ? A[1] : FString());
		else UE_LOG(LogOTGLM, Warning, TEXT("OTG.LM.Connect fsgolf|direct|r10|square|frp [address]"));
	}));
	FAutoConsoleCommandWithWorldAndArgs TurnCmd(TEXT("OTG.LM.Turn"), TEXT("OTG.LM.Turn 1|0: the player's turn to hit, or not"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		if (UOTGLaunchMonitorSubsystem* S = Of(W)) S->SetTurn(!A.Num() || A[0] != TEXT("0"));
	}));
	FAutoConsoleCommandWithWorldAndArgs PlayerCmd(TEXT("OTG.LM.Player"), TEXT("OTG.LM.Player <club> [yards] [RH|LH]: what the player is hitting"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		if (UOTGLaunchMonitorSubsystem* S = Of(W); S && A.Num())
			S->SetPlayer(A[0].ToUpper(), A.Num() > 1 ? FCString::Atod(*A[1]) : 0, A.Num() > 2 && A[2].Equals(TEXT("LH"), ESearchCase::IgnoreCase));
	}));
	FAutoConsoleCommandWithWorldAndArgs OffCmd(TEXT("OTG.LM.Disconnect"), TEXT("Disconnect the launch monitor (OTG.LM.Connect to connect again)"), FCmd::CreateLambda([](const TArray<FString>&, UWorld* W) {
		if (UOTGLaunchMonitorSubsystem* S = Of(W)) S->DisconnectDevice();
	}));
	FAutoConsoleCommandWithWorldAndArgs TestShotCmd(TEXT("OTG.LM.TestShot"), TEXT("OTG.LM.TestShot [mph vla hla spin axis]: a shot without a launch monitor"), FCmd::CreateLambda([](const TArray<FString>& A, UWorld* W) {
		auto Arg = [&](int32 i, double D) { return A.IsValidIndex(i) ? FCString::Atod(*A[i]) : D; };
		if (UOTGLaunchMonitorSubsystem* S = Of(W)) S->InjectShot(Arg(0, 150), Arg(1, 12), Arg(2, 0), Arg(3, 2700), Arg(4, 0));
	}));
}

// ------------------------------------------------------------------ settings

void UOTGLaunchMonitorSubsystem::LoadSettings()
{
	FString S;
	if (GConfig->GetString(Section, TEXT("Connection"), S, GGameUserSettingsIni)) KindOf(S, Connection);
	GConfig->GetString(Section, TEXT("MevoHost"), MevoHost, GGameUserSettingsIni);
	GConfig->GetInt(Section, TEXT("MevoPort"), MevoPort, GGameUserSettingsIni);
	GConfig->GetString(Section, TEXT("FrpUrl"), FrpUrl, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("ArmOnTurn"), bArmOnTurn, GGameUserSettingsIni);
	if (GConfig->GetBool(Section, TEXT("HasSetup"), bHasSetup, GGameUserSettingsIni) && bHasSetup)
	{
		GConfig->GetDouble(Section, TEXT("DistanceM"), MevoSetup.DistanceM, GGameUserSettingsIni);
		GConfig->GetDouble(Section, TEXT("SurfaceIn"), MevoSetup.SurfaceIn, GGameUserSettingsIni);
		GConfig->GetDouble(Section, TEXT("TeeHeightIn"), MevoSetup.TeeHeightIn, GGameUserSettingsIni);
		GConfig->GetDouble(Section, TEXT("TiltTarget"), MevoSetup.TiltTarget, GGameUserSettingsIni);
		GConfig->GetInt(Section, TEXT("BallType"), MevoSetup.BallType, GGameUserSettingsIni);
		GConfig->GetDouble(Section, TEXT("TrackPct"), MevoSetup.TrackPct, GGameUserSettingsIni);
		GConfig->GetString(Section, TEXT("Mode"), MevoSetup.Mode, GGameUserSettingsIni);
	}
	// (for this run only)
	const TCHAR* Cmd = FCommandLine::Get();
	if (FParse::Value(Cmd, TEXT("lm="), S)) KindOf(S, Connection);
	if (FParse::Value(Cmd, TEXT("mevo="), S, false))
	{
		FString H, P;
		if (S.Split(TEXT(":"), &H, &P)) { MevoHost = H; MevoPort = FCString::Atoi(*P); }
		else MevoHost = S;
	}
	FParse::Value(Cmd, TEXT("frp="), FrpUrl, false);
	FParse::Value(Cmd, TEXT("ocport="), OpenConnectPort);
	FParse::Value(Cmd, TEXT("bridgeport="), BridgePort);
	FParse::Value(Cmd, TEXT("bridgecmd="), BridgeCommand, false);
	if (FParse::Param(Cmd, TEXT("lmturn"))) bTurn = true;
}

void UOTGLaunchMonitorSubsystem::SaveSettings()
{
	GConfig->SetString(Section, TEXT("Connection"), KindName(Connection), GGameUserSettingsIni);
	GConfig->SetString(Section, TEXT("MevoHost"), *MevoHost, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("MevoPort"), MevoPort, GGameUserSettingsIni);
	GConfig->SetString(Section, TEXT("FrpUrl"), *FrpUrl, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("ArmOnTurn"), bArmOnTurn, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("HasSetup"), bHasSetup, GGameUserSettingsIni);
	if (bHasSetup)
	{
		GConfig->SetDouble(Section, TEXT("DistanceM"), MevoSetup.DistanceM, GGameUserSettingsIni);
		GConfig->SetDouble(Section, TEXT("SurfaceIn"), MevoSetup.SurfaceIn, GGameUserSettingsIni);
		GConfig->SetDouble(Section, TEXT("TeeHeightIn"), MevoSetup.TeeHeightIn, GGameUserSettingsIni);
		GConfig->SetDouble(Section, TEXT("TiltTarget"), MevoSetup.TiltTarget, GGameUserSettingsIni);
		GConfig->SetInt(Section, TEXT("BallType"), MevoSetup.BallType, GGameUserSettingsIni);
		GConfig->SetDouble(Section, TEXT("TrackPct"), MevoSetup.TrackPct, GGameUserSettingsIni);
		GConfig->SetString(Section, TEXT("Mode"), *MevoSetup.Mode, GGameUserSettingsIni);
	}
	GConfig->Flush(false, GGameUserSettingsIni);
}

// ------------------------------------------------------------------ lifecycle

void UOTGLaunchMonitorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadSettings();
	StartedAt = FPlatformTime::Seconds();
	OcOut = MakeShared<FOTGLMOutbox>();
	OpenConnect = MakeShared<FOTGOpenConnectServer>(OpenConnectPort, OcOut.ToSharedRef());
	OpenConnect->SetPlayer(Handed, Club, DistanceYd);
	StartDevice();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UOTGLaunchMonitorSubsystem::Tick));

	// (-execat="2:OTG.LM.Turn 1|5:OTG.LM.Player PT 4", -quitafter=20: for checking without anyone at the game)
	FString Exec;
	if (FParse::Value(FCommandLine::Get(), TEXT("execat="), Exec, false))
	{
		TArray<FString> Items;
		Exec.ParseIntoArray(Items, TEXT("|"));
		for (const FString& I : Items)
		{
			FString At, Command;
			if (I.Split(TEXT(":"), &At, &Command)) ExecAt.Emplace(FCString::Atod(*At), Command.TrimStartAndEnd());
		}
	}
	double Quit = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("quitafter="), Quit) && Quit > 0) QuitAt = StartedAt + Quit;
	UE_LOG(LogOTGLM, Display, TEXT("Launch monitor: %s"), KindName(Connection));
}

void UOTGLaunchMonitorSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	StopDevice(); // (disarms a Mevo+)
	OpenConnect.Reset();
	Super::Deinitialize();
}

void UOTGLaunchMonitorSubsystem::StopDevice()
{
	if (Mevo) { Mevo->Shutdown(); Mevo.Reset(); }
	if (Frp) { Frp->Stop(); Frp.Reset(); }
	if (Bridge) { Bridge->Stop(); Bridge.Reset(); }
}

void UOTGLaunchMonitorSubsystem::StartDevice()
{
	StopDevice();
	DevOut = MakeShared<FOTGLMOutbox>();
	BridgeOut = MakeShared<FOTGLMOutbox>();
	if (bDeviceOff) return;
	switch (Connection)
	{
	case EOTGConnection::Direct:
		Mevo = MakeShared<FOTGMevoClient>(MevoHost, MevoPort, MevoMode(), DevOut.ToSharedRef(), FPaths::Combine(FPaths::ProjectLogDir(), TEXT("mevo-log.txt")));
		if (bHasSetup) Mevo->SetSetup(MevoSetup);
		Mevo->SetHoldAfterShot(bArmOnTurn);
		Mevo->SetWantArmed(!(bArmOnTurn && !bTurn));
		Mevo->Start();
		break;
	case EOTGConnection::R10:
	case EOTGConnection::Square:
	{
		const FString Kind = KindName(Connection);
		Bridge = MakeShared<FOTGBridgeProcess>(Kind, BridgePort, Connection == EOTGConnection::Square ? SquareArgs() : TArray<FString>(), BridgeCommand, BridgeOut.ToSharedRef());
		Frp = MakeShared<FOTGFrpController>(Bridge->Url(), Kind, DevOut.ToSharedRef());
		Frp->SetMode(FrpModeName(), Handed == TEXT("LH") ? TEXT("lh") : TEXT("rh"));
		Bridge->Start();
		Frp->Start();
		break;
	}
	case EOTGConnection::Frp:
		Frp = MakeShared<FOTGFrpController>(FrpUrl, TEXT("frp"), DevOut.ToSharedRef());
		Frp->SetMode(FrpModeName(), Handed == TEXT("LH") ? TEXT("lh") : TEXT("rh"));
		Frp->Start();
		break;
	default:
		break;
	}
	Compose();
}

// ------------------------------------------------------------------ the game's side

void UOTGLaunchMonitorSubsystem::SetConnection(EOTGConnection Kind, const FString& Address)
{
	Connection = Kind;
	if (!Address.IsEmpty())
	{
		if (Kind == EOTGConnection::Direct)
		{
			FString H, P;
			if (Address.Split(TEXT(":"), &H, &P)) { MevoHost = H; MevoPort = FCString::Atoi(*P); }
			else MevoHost = Address;
		}
		else if (Kind == EOTGConnection::Frp) FrpUrl = Address;
	}
	bDeviceOff = false;
	SaveSettings();
	UE_LOG(LogOTGLM, Display, TEXT("Launch monitor: %s"), KindName(Connection));
	StartDevice();
}

FString UOTGLaunchMonitorSubsystem::MevoMode() const
{
	// The detection mode follows the club: putter -> putting; a wedge for a short shot (or any club
	// inside 25 yd) -> chipping, since full-swing mode can miss slow, low chips; else full swing.
	// (The setup can force full swing or outdoor mode for everything but putts.)
	if (Club == TEXT("PT")) return TEXT("putting");
	if (bHasSetup && (MevoSetup.Mode == TEXT("full") || MevoSetup.Mode == TEXT("outdoor"))) return MevoSetup.Mode;
	const bool bWedge = Club == TEXT("PW") || Club == TEXT("GW") || Club == TEXT("SW") || Club == TEXT("LW");
	if (DistanceYd > 0 && (DistanceYd < 25 || (DistanceYd < 50 && bWedge))) return TEXT("chipping");
	return TEXT("full");
}

FString UOTGLaunchMonitorSubsystem::FrpModeName() const
{
	const FString M = MevoMode();
	return M == TEXT("outdoor") ? TEXT("full") : M;
}

// The Square Golf bridge arms for one club when it starts
TArray<FString> UOTGLaunchMonitorSubsystem::SquareArgs() const
{
	static const TMap<FString, FString> Clubs = {
		{ TEXT("DR"), TEXT("driver") }, { TEXT("W3"), TEXT("3w") }, { TEXT("W5"), TEXT("3w") }, { TEXT("H4"), TEXT("3h") }, { TEXT("I4"), TEXT("4i") },
		{ TEXT("I5"), TEXT("5i") }, { TEXT("I6"), TEXT("6i") }, { TEXT("I7"), TEXT("7i") }, { TEXT("I8"), TEXT("8i") }, { TEXT("I9"), TEXT("9i") },
		{ TEXT("PW"), TEXT("pw") }, { TEXT("GW"), TEXT("gw") }, { TEXT("SW"), TEXT("sw") }, { TEXT("LW"), TEXT("lw") }, { TEXT("PT"), TEXT("putter") },
	};
	const FString* C = Clubs.Find(Club);
	return { C ? *C : FString(TEXT("driver")) };
}

void UOTGLaunchMonitorSubsystem::SetPlayer(const FString& InClub, double InDistanceYd, bool bLeftHanded)
{
	Club = InClub;
	DistanceYd = InDistanceYd;
	Handed = bLeftHanded ? TEXT("LH") : TEXT("RH");
	PlayerChanged();
}

void UOTGLaunchMonitorSubsystem::PlayerChanged()
{
	if (OpenConnect) OpenConnect->SetPlayer(Handed, Club, DistanceYd);
	if (Mevo) Mevo->SetMode(MevoMode());
	if (Frp) Frp->SetMode(FrpModeName(), Handed == TEXT("LH") ? TEXT("lh") : TEXT("rh"));
	// (the Omni putts and chips by club, not by mode: its bridge starts again when the putter is
	// picked up or put down)
	if (Bridge && Connection == EOTGConnection::Square && (Bridge->GetArgs().Num() && Bridge->GetArgs()[0] == TEXT("putter")) != (Club == TEXT("PT")))
		Bridge->Restart(SquareArgs());
}

void UOTGLaunchMonitorSubsystem::SetTurn(bool bReady)
{
	bTurn = bReady;
	ApplyTurn();
	PlayerChanged();
}

void UOTGLaunchMonitorSubsystem::ApplyTurn()
{
	if (!Mevo) return;
	Mevo->SetHoldAfterShot(bArmOnTurn);
	if (!bArmOnTurn)
	{
		Mevo->SetWantArmed(true);
		return;
	}
	if (bTurn) Mevo->SetMode(MevoMode());
	Mevo->SetWantArmed(bTurn);
}

void UOTGLaunchMonitorSubsystem::SetArmOnTurn(bool bOn)
{
	bArmOnTurn = bOn;
	SaveSettings();
	ApplyTurn();
}

void UOTGLaunchMonitorSubsystem::SetMevoSetup(const FOTGMevoSetup& Setup)
{
	MevoSetup = Setup;
	MevoSetup.DistanceM = FMath::Clamp(Setup.DistanceM, 1.5, 6.0);
	MevoSetup.SurfaceIn = FMath::Clamp(Setup.SurfaceIn, 0.0, 10.0);
	MevoSetup.TeeHeightIn = FMath::Clamp(Setup.TeeHeightIn, 0.0, 4.0);
	MevoSetup.TiltTarget = FMath::Clamp(Setup.TiltTarget, -10.0, 25.0);
	MevoSetup.BallType = Setup.BallType ? 1 : 0;
	MevoSetup.TrackPct = FMath::Clamp(Setup.TrackPct, 0.6, 1.0);
	if (MevoSetup.Mode != TEXT("full") && MevoSetup.Mode != TEXT("outdoor")) MevoSetup.Mode = TEXT("auto");
	bHasSetup = true;
	SaveSettings();
	if (Mevo)
	{
		Mevo->SetSetup(MevoSetup);
		Mevo->SetMode(MevoMode());
	}
}

void UOTGLaunchMonitorSubsystem::DisconnectDevice()
{
	bDeviceOff = true;
	StartDevice();
}

void UOTGLaunchMonitorSubsystem::ConnectDevice()
{
	bDeviceOff = false;
	StartDevice();
}

void UOTGLaunchMonitorSubsystem::InjectShot(double SpeedMph, double VLA, double HLA, double TotalSpin, double SpinAxis)
{
	FOTGBallData B;
	B.Speed = SpeedMph; B.VLA = VLA; B.HLA = HLA; B.TotalSpin = TotalSpin; B.SpinAxis = SpinAxis;
	Deliver(OTGToShot(B, TEXT("test"), TEXT("test shot")));
}

// ------------------------------------------------------------------ each frame

void UOTGLaunchMonitorSubsystem::Deliver(const FOTGShot& Shot)
{
	Status.LastShotAt = Shot.At;
	UE_LOG(LogOTGLM, Display, TEXT("Shot (%s%s): %s"), *Shot.Source, Shot.ShotNumber >= 0 ? *FString::Printf(TEXT(" #%d"), Shot.ShotNumber) : TEXT(""), *OTGShotLine(Shot));
	// (every number, for tests)
	UE_LOG(LogOTGLM, Log, TEXT("OTGSHOT {\"source\":\"%s\",\"device\":\"%s\",\"shotNumber\":%d,\"speedMph\":%s,\"vla\":%s,\"hla\":%s,\"backSpin\":%s,\"sideSpin\":%s,\"totalSpin\":%s,\"spinAxis\":%s,\"carryYd\":%s,\"clubSpeedMph\":%s}"),
		*Shot.Source, *Shot.Device.Replace(TEXT("\""), TEXT("'")), Shot.ShotNumber, *Num(Shot.SpeedMph), *Num(Shot.VLA), *Num(Shot.HLA), *Num(Shot.BackSpin), *Num(Shot.SideSpin),
		*Num(Shot.TotalSpin), *Num(Shot.SpinAxis), *Num(Shot.LmCarryYd >= 0 ? Shot.LmCarryYd : NAN), *Num(Shot.Club.bValid ? Shot.Club.SpeedMph : NAN));
	OnShotNative.Broadcast(Shot);
	OnShot.Broadcast(Shot);
}

void UOTGLaunchMonitorSubsystem::Compose()
{
	FOTGLaunchMonitorStatus S;
	S.Connection = Connection;
	S.LastShotAt = Status.LastShotAt;
	const FOTGLMDeviceState Oc = OcOut ? OcOut->Read() : FOTGLMDeviceState();
	S.PortError = Oc.PortError;
	const FOTGLMDeviceState D = DevOut ? DevOut->Read() : FOTGLMDeviceState();
	if (Connection == EOTGConnection::FSGolf)
	{
		S.bConnected = Oc.bConnected;
		S.bReady = Oc.bConnected && Oc.Ready == 1;
		S.Device = Oc.Device;
		S.State = Oc.State;
		S.Text = !Oc.PortError.IsEmpty() ? Oc.PortError : !Oc.bConnected ? FString::Printf(TEXT("Waiting for FS Golf on port %d"), OpenConnectPort) : S.bReady ? TEXT("ready") : TEXT("connected, not ready");
	}
	else if (bDeviceOff)
	{
		S.State = TEXT("off");
		S.Text = TEXT("disconnected");
	}
	else if (Connection == EOTGConnection::Direct)
	{
		S.bConnected = D.bConnected;
		S.bReady = D.State == TEXT("ready");
		S.Device = D.Model.IsEmpty() ? TEXT("Mevo+") : (D.Model + TEXT(" ") + D.Ssid).TrimEnd();
		S.Battery = D.Battery;
		S.State = D.State;
		S.Text = DirectText(D.State);
		S.Error = D.Error;
		S.Mode = D.Mode;
		S.Tilt = D.Tilt; S.Roll = D.Roll; S.TempC = D.TempC;
		S.DeviceText = D.LastText;
	}
	else
	{
		// the bridge's own progress until it has the device; then whether the device says it's ready
		const FOTGLMDeviceState B = BridgeOut ? BridgeOut->Read() : FOTGLMDeviceState();
		const bool bLive = D.State == TEXT("connected");
		const bool bFound = !Bridge || B.State == TEXT("ready");
		S.bConnected = bLive && bFound;
		S.bReady = bLive && bFound && D.Ready != 0;
		S.Device = !D.Model.IsEmpty() ? D.Model : FOTGBridgeProcess::NameOf(KindName(Connection));
		S.Battery = D.Battery;
		if (!bFound) { S.State = B.State; S.Text = B.Text; }
		else if (bLive) { S.State = D.Ready == 0 ? TEXT("connected") : TEXT("ready"); S.Text = D.Ready == 0 ? TEXT("connected, not ready") : TEXT("ready"); }
		else { S.State = D.State; S.Text = D.Text; }
		S.Error = !D.Error.IsEmpty() ? D.Error : B.Error;
	}
	if (!Same(S, Status))
	{
		const bool bReadyChanged = S.bReady != Status.bReady || S.State != Status.State;
		Status = S;
		if (bReadyChanged) UE_LOG(LogOTGLM, Display, TEXT("Launch monitor %s: %s%s"), KindName(S.Connection), *S.Text, S.Error.IsEmpty() ? TEXT("") : *(TEXT(" (") + S.Error + TEXT(")")));
		OnStatus.Broadcast(Status);
	}
}

bool UOTGLaunchMonitorSubsystem::Tick(float)
{
	const double Now = FPlatformTime::Seconds();
	if (Frp) Frp->Tick(Now);
	if (Bridge) Bridge->Tick(Now);
	// Shots from OpenConnect always count (whatever the connection), as in the web version
	if (OcOut) for (const FOTGShot& S : OcOut->TakeShots()) Deliver(S);
	if (DevOut) for (const FOTGShot& S : DevOut->TakeShots()) Deliver(S);
	Compose();
	for (int32 i = 0; i < ExecAt.Num();)
	{
		if (Now - StartedAt < ExecAt[i].Key) { i++; continue; }
		const FString Command = ExecAt[i].Value;
		ExecAt.RemoveAt(i);
		UE_LOG(LogOTGLM, Display, TEXT("(execat) %s"), *Command);
		if (GEngine) GEngine->Exec(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr, *Command);
	}
	if (QuitAt && Now >= QuitAt)
	{
		QuitAt = 0;
		UE_LOG(LogOTGLM, Display, TEXT("(quitafter) quitting"));
		FPlatformMisc::RequestExit(false, TEXT("quitafter"));
	}
	return true;
}
