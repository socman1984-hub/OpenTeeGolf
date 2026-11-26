// What a connection (on its own thread or the game thread) hands the launch monitor subsystem:
// shots, and its status. The subsystem collects both on the game thread each frame.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "LaunchMonitor/OTGLaunchMonitorTypes.h"

DECLARE_LOG_CATEGORY_EXTERN(LogOTGLM, Log, All);

// One connection's status
struct FOTGLMDeviceState
{
	FString State = TEXT("off"), Text, Error, Model, Ssid, Mode, LastText;
	bool bConnected = false;
	int32 Ready = -1;     // 1 ready, 0 not, -1 not said
	int32 Battery = -1;
	double Tilt = NAN, Roll = NAN, TempC = NAN;
	int32 Camera = -1;    // (Mevo+ live view: 1 streaming, 0 off, -1 not asked)
	FString CameraError;  // (Mevo+: why the camera didn't start)
	int32 Clients = 0;    // (OpenConnect: connections open)
	FString PortError;    // (OpenConnect)
	FString Device;       // (OpenConnect and FRP: what the device calls itself)
};

struct FOTGLMOutbox
{
	void Shot(const FOTGShot& S)
	{
		FScopeLock L(&Lock);
		Shots.Add(S);
	}
	template <typename F> void Update(F&& Fn)
	{
		FScopeLock L(&Lock);
		Fn(State);
	}
	FOTGLMDeviceState Read()
	{
		FScopeLock L(&Lock);
		return State;
	}
	TArray<FOTGShot> TakeShots()
	{
		FScopeLock L(&Lock);
		TArray<FOTGShot> Out = MoveTemp(Shots);
		Shots.Reset();
		return Out;
	}
	// (Mevo+ fast shots: the full result of a shot sent early)
	void ShotUpdate(const FOTGShot& S)
	{
		FScopeLock L(&Lock);
		Updates.Add(S);
	}
	TArray<FOTGShot> TakeShotUpdates()
	{
		FScopeLock L(&Lock);
		TArray<FOTGShot> Out = MoveTemp(Updates);
		Updates.Reset();
		return Out;
	}

private:
	FCriticalSection Lock;
	FOTGLMDeviceState State;
	TArray<FOTGShot> Shots, Updates;
};
