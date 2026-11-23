// Shots from any launch monitor that speaks the Flight Relay Protocol (FRP, github.com/flightrelay/spec,
// CC0): JSON over a WebSocket. lib/frp.js's FrpController in C++: the game is the "controller", it
// connects to the "device" (a launch monitor, or a bridge program), says hello, and collects each
// shot's events until the shot is finished. On the game thread (Tick).
//
// And lib/bridges.js's BridgeProcess: the small open-source bridge programs by Eric Thill (MIT /
// Apache-2.0) that find a Bluetooth launch monitor, arm it and serve its shots over FRP on a local
// port - Garmin Approach R10 (tenover-frp, github.com/divotmaker/10over) and Square Golf Omni / Home
// (allsquare-frp, github.com/divotmaker/allsquare). Run (and restarted when they stop) here.

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "OTGLMOutbox.h"

class IWebSocket;
class FJsonObject;

// A unit-tagged string ("67.2mps", "180.5m") in m/s or metres (a plain number as is)
TOptional<double> OTGFrpSpeed(const TSharedPtr<class FJsonValue>& V);
TOptional<double> OTGFrpDistance(const TSharedPtr<class FJsonValue>& V);
// An FRP ball (and club) as ball data. FRP and OpenConnect agree on signs (+ = right).
bool OTGFrpToBallData(const TSharedPtr<FJsonObject>& Ball, const TSharedPtr<FJsonObject>& Club, FOTGBallData& Out);

class FOTGFrpController : public TSharedFromThis<FOTGFrpController>
{
public:
	FOTGFrpController(const FString& InUrl, const FString& InSource, TSharedRef<FOTGLMOutbox> InOut);
	~FOTGFrpController();
	void Start();
	void Stop();
	void Tick(double Now);
	// Detection mode (full | putting | chipping) and handedness (rh | lh); devices that can't switch ignore it
	void SetMode(const FString& Mode, const FString& Handed);

private:
	void Connect();
	void Retry();
	void Send(const FString& Json);
	void Handle(const TSharedPtr<FJsonObject>& Msg);
	void OnConnected(int32 Gen);
	void OnError(const FString& Error, int32 Gen);
	void OnClosed(int32 Code, const FString& Reason, bool bClean, int32 Gen);
	void OnMessage(const FString& Text, int32 Gen);
	void Status(TFunctionRef<void(FOTGLMDeviceState&)> Fn) { Out->Update(Fn); }

	FString Url, Source;
	TSharedRef<FOTGLMOutbox> Out;
	TSharedPtr<IWebSocket> Ws;
	int32 Gen = 0, Tries = 0;
	bool bRunning = false, bOpened = false, bInit = false;
	double RetryAt = 0;
	FString Mode, Handed;
	struct FShotParts
	{
		int32 Number = -1;
		double At = 0;
		TSharedPtr<FJsonObject> Ball, Club;
	};
	TMap<FString, FShotParts> Shots;
};

class FOTGBridgeProcess
{
public:
	// Kind r10 / square. Command: run this instead of the bridge program (a program and its first
	// arguments, for tests)
	FOTGBridgeProcess(const FString& InKind, int32 InPort, const TArray<FString>& InArgs, const FString& InCommand, TSharedRef<FOTGLMOutbox> InOut);
	~FOTGBridgeProcess();
	void Start();
	void Stop();
	void Restart(const TArray<FString>& NewArgs); // (the Square Golf bridge arms for one club)
	void Tick(double Now);
	FString Url() const { return FString::Printf(TEXT("ws://127.0.0.1:%d/frp"), Port); }
	const TArray<FString>& GetArgs() const { return Args; }
	static FString NameOf(const FString& Kind);
	static FString FindProgram(const FString& Kind, FString& Searched);

private:
	void Launch();
	void Kill();
	void OnLine(const FString& Line);

	FString Kind, Command;
	int32 Port;
	TArray<FString> Args;
	TSharedRef<FOTGLMOutbox> Out;
	FProcHandle Proc;
	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	bool bRunning = false;
	double RelaunchAt = 0;
	FString Buf;
};
