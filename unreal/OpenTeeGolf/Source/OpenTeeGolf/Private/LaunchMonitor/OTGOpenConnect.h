// The OpenConnect listener (TCP 921): what FS Golf (and other connectors) send shots to, as in
// server.js. Back-to-back JSON messages; each is answered (200 received, 501 no ball speed), and on
// connecting (and when the player changes) the connector is told the player's club and handedness.
// It keeps trying the port while another program has it.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "OTGLMOutbox.h"

class FSocket;
class FRunnableThread;

class FOTGOpenConnectServer : public FRunnable
{
public:
	FOTGOpenConnectServer(int32 InPort, TSharedRef<FOTGLMOutbox> InOut);
	virtual ~FOTGOpenConnectServer() override;

	// The player, for the "Player Information" message (Handed RH/LH, Club DR..PT, yards)
	void SetPlayer(const FString& Handed, const FString& Club, double DistanceToTarget);

private:
	virtual uint32 Run() override;
	virtual void Stop() override { bRunning = false; }

	struct FClient
	{
		FSocket* Sock = nullptr;
		FString Addr;
		TArray<uint8> Buf;
	};
	bool Listen();
	void Read(FClient& C);
	void Handle(FClient& C, const FString& Json);
	void SendJson(FClient& C, const FString& Json);
	FString PlayerJson();
	void Publish();

	int32 Port;
	TSharedRef<FOTGLMOutbox> Out;
	FRunnableThread* Thread = nullptr;
	FThreadSafeBool bRunning = true;

	FCriticalSection PlayerLock;
	FString Handed = TEXT("RH"), Club = TEXT("DR");
	double Distance = 0;
	FThreadSafeBool bPlayerChanged = false;

	FSocket* Listener = nullptr;
	TArray<FClient> Clients;
	bool bReady = false, bBallDetected = false;
	FString Device;
	FString LastKey;
	double LastKeyAt = 0;
};
