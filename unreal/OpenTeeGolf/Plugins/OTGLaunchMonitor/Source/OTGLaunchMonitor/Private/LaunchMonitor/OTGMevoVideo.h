// The Mevo+ camera's live picture (server.js proxyMevoVideo, in C++ on a thread of its own). Once the
// camera board streams (FOTGMevoClient::SetCamera), it serves MJPEG over HTTP on port 8080 of the
// Mevo+. The stream's path isn't documented, so the usual ones are tried and the first that answers
// with video is kept. Each JPEG picture is decoded here; the game thread takes the newest.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include <atomic>

class FSocket;
class FRunnableThread;
class IImageWrapperModule;

class FOTGMevoVideo : public FRunnable
{
public:
	FOTGMevoVideo(const FString& InHost, int32 InPort); // (on the game thread: loads the JPEG decoder)
	virtual ~FOTGMevoVideo() override;
	void Start();
	void Shutdown(); // (waits for the thread)

	// The newest picture if it's newer than InOutSerial (BGRA, 8 bits a channel)
	bool TakeFrame(int32& InOutSerial, TArray64<uint8>& OutBgra, int32& OutW, int32& OutH);
	bool HasPictures() const { return Serial > 0; }
	FString GetError() const;

private:
	virtual uint32 Run() override;
	virtual void Stop() override { bRunning = false; }

	bool Connect();
	void Close();
	int32 Stream(const FString& Path); // -1 not video (try the next path), 0 couldn't connect, 1 streamed until it stopped
	void Picture(const uint8* Jpeg, int32 N);
	void SetError(const FString& E);

	FString Host;
	int32 Port;
	IImageWrapperModule* Images = nullptr;
	FRunnableThread* Thread = nullptr;
	FThreadSafeBool bRunning = true;
	FSocket* Sock = nullptr;
	FString KnownPath;

	mutable FCriticalSection Lock;
	TArray64<uint8> Bgra;
	int32 W = 0, H = 0;
	std::atomic<int32> Serial { 0 };
	FString Error;
};
