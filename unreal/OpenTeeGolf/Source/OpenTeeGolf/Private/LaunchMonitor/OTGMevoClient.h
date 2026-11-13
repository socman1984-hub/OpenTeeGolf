// Direct connection to a FlightScope Mevo+ / Mevo Gen2 (no FS Golf needed): lib/mevo.js's MevoClient
// in C++, on a thread of its own. The PC joins the Mevo+ Wi-Fi ("FS M2-xxxxxx"); the radar talks
// binary frames (OTGMevoWire.h) on TCP 192.168.2.1:5100, strictly one request, one reply. As with
// ironsight, the device's licensing exchange isn't touched: the radar arms without it.
//
// The thread connects, introduces itself (handshake), configures the detection mode and setup,
// arms, keeps the session alive with status polls, reads shots (BALL TRIGGER ... PROCESSED), and
// re-arms after each - or stays disarmed until the game says it's the player's turn
// (SetHoldAfterShot / SetWantArmed). It reconnects whenever the device drops.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "OTGMevoWire.h"
#include "OTGLMOutbox.h"

class FSocket;
class FRunnableThread;

class FOTGMevoClient : public FRunnable
{
public:
	FOTGMevoClient(const FString& InHost, int32 InPort, const FString& InMode, TSharedRef<FOTGLMOutbox> InOut, const FString& TraceFile);
	virtual ~FOTGMevoClient() override;
	void Start(); // (after the Set... calls below that it should start with)

	// (from the game thread; applied by the thread when it's free to)
	void SetMode(const FString& Mode);         // full, chipping, putting, outdoor
	void SetWantArmed(bool bArmed);
	void SetHoldAfterShot(bool bHold);         // after a shot, stay disarmed (until SetWantArmed(true))
	void SetSetup(const FOTGMevoSetup& Setup); // applied straight away if it's armed
	// Disarm (so the radar stops watching for shots), hang up, end the thread (waits up to ~3.5 s)
	void Shutdown();

private:
	virtual uint32 Run() override;
	virtual void Stop() override { bRunning = false; }

	using FPred = TFunctionRef<bool(const otg::mevo::FMevoFrame&)>;

	// ---- plumbing
	bool Connect();
	void Cleanup();
	bool Pump(double Seconds);             // read what's arrived (waiting up to that long); false if the connection closed
	void Send(uint8 Dest, uint8 Type, const TArray<uint8>& Payload = {});
	void DrainPending();                   // everything already here, through OnFrame
	bool WaitFor(FPred Pred, double Seconds, const TCHAR* What, otg::mevo::FMevoFrame* Out = nullptr);
	bool Ask(uint8 Dest, uint8 Type, const TArray<uint8>& Payload, FPred Pred, const TCHAR* What, double Seconds = 3.0, otg::mevo::FMevoFrame* Out = nullptr);
	void Idle(double Seconds);             // wait, still reading
	void OnFrame(const otg::mevo::FMevoFrame& F);
	void Trace(const TCHAR* Dir, const otg::mevo::FMevoFrame& F);

	// ---- the session
	bool Session();
	bool Handshake();
	bool Configure(int32 Mode);
	bool Arm();
	void Disarm();
	bool Keepalive();
	void StartShot();
	void OnShotText(const FString& T);
	bool FinishShot();
	bool ApplyRequests();                  // mode / setup / armed changes the game asked for

	void SetState(const FString& State, const FString& Error = FString());
	void Log(const FString& Msg);

	FString Host;
	int32 Port;
	TSharedRef<FOTGLMOutbox> Out;
	FRunnableThread* Thread = nullptr;
	FThreadSafeBool bRunning = true, bStopRequested = false, bFinished = false;

	// asked for by the game (under Ctl)
	FCriticalSection Ctl;
	FString WantMode;
	bool bWantDisarmed = false, bHoldAfterShot = false, bSetupChanged = false, bHasSetup = false;
	FOTGMevoSetup Setup;

	// the thread's own
	FSocket* Sock = nullptr;
	otg::mevo::FFrameReader Reader;
	TArray<otg::mevo::FMevoFrame> Pending;
	bool bDropped = false, bArmed = false;
	FString State = TEXT("off"), Step, Err, Mode;
	FOTGMevoSetup Using;
	bool bUsingSetup = false;
	int32 KeepFails = 0, ShotCount = 0;
	double NextKeep = 0;
	struct FShot
	{
		TOptional<otg::mevo::FFlight> Flight;
		TOptional<otg::mevo::FClub> Club;
		TOptional<otg::mevo::FSpin> Spin;
		bool bDone = false, bFinishing = false;
	};
	TOptional<FShot> Shot;
	bool bFinishPending = false, bPendingModeChange = false;
	double TriggerDeadline = 0, FlightDeadline = 0;
	TUniquePtr<class FArchive> TraceFile;
};
