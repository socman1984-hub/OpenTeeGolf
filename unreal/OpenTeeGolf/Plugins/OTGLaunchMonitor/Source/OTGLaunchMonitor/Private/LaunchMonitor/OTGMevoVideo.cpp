// See OTGMevoVideo.h (server.js proxyMevoVideo).

#include "OTGMevoVideo.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "IPAddress.h"
#include "LaunchMonitor/OTGLMOutbox.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace
{
	double VideoNow() { return FPlatformTime::Seconds(); }
	// (the path that works on the Mevo+ first; then the usual MJPEG ones)
	const TCHAR* VideoPaths[] = { TEXT("/?action=stream"), TEXT("/"), TEXT("/stream"), TEXT("/stream.mjpg"), TEXT("/video"), TEXT("/mjpeg"), TEXT("/video.mjpg"), TEXT("/cam.mjpg") };
	// A JPEG marker (FF xx) from From on, or INDEX_NONE
	int32 FindJpegMarker(const TArray<uint8>& B, uint8 Marker, int32 From)
	{
		for (int32 i = FMath::Max(0, From); i + 1 < B.Num(); i++)
			if (B[i] == 0xff && B[i + 1] == Marker) return i;
		return INDEX_NONE;
	}
	int32 FindHttpHeadEnd(const TArray<uint8>& B)
	{
		for (int32 i = 0; i + 3 < B.Num(); i++)
			if (B[i] == '\r' && B[i + 1] == '\n' && B[i + 2] == '\r' && B[i + 3] == '\n') return i;
		return INDEX_NONE;
	}
}

FOTGMevoVideo::FOTGMevoVideo(const FString& InHost, int32 InPort)
	: Host(InHost), Port(InPort)
{
	Images = &FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
}

FOTGMevoVideo::~FOTGMevoVideo()
{
	Shutdown();
}

void FOTGMevoVideo::Start()
{
	if (!Thread) Thread = FRunnableThread::Create(this, TEXT("OTG Mevo+ video"), 0, TPri_BelowNormal);
}

void FOTGMevoVideo::Shutdown()
{
	bRunning = false;
	if (!Thread) return;
	Thread->Kill(true);
	delete Thread;
	Thread = nullptr;
}

bool FOTGMevoVideo::TakeFrame(int32& InOutSerial, TArray64<uint8>& OutBgra, int32& OutW, int32& OutH)
{
	FScopeLock L(&Lock);
	if (Serial == InOutSerial || !Bgra.Num()) return false;
	OutBgra = Bgra;
	OutW = W;
	OutH = H;
	InOutSerial = Serial;
	return true;
}

FString FOTGMevoVideo::GetError() const
{
	FScopeLock L(&Lock);
	return Error;
}

void FOTGMevoVideo::SetError(const FString& E)
{
	FScopeLock L(&Lock);
	Error = E;
}

bool FOTGMevoVideo::Connect()
{
	Close();
	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	TSharedRef<FInternetAddr> Addr = SS->CreateInternetAddr();
	bool bValid = false;
	Addr->SetIp(*Host, bValid);
	Addr->SetPort(Port);
	if (!bValid) return false;
	Sock = SS->CreateSocket(NAME_Stream, TEXT("OTG Mevo+ video"), Addr->GetProtocolType());
	if (!Sock) return false;
	Sock->SetNonBlocking(true);
	Sock->Connect(*Addr);
	for (const double Until = VideoNow() + 4; VideoNow() < Until && bRunning;)
	{
		const ESocketConnectionState C = Sock->GetConnectionState();
		if (C == SCS_Connected) return true;
		if (C == SCS_ConnectionError) break;
		Sock->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(100));
	}
	Close();
	return false;
}

void FOTGMevoVideo::Close()
{
	if (!Sock) return;
	Sock->Close();
	ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Sock);
	Sock = nullptr;
}

uint32 FOTGMevoVideo::Run()
{
	while (bRunning)
	{
		TArray<FString> Try;
		if (!KnownPath.IsEmpty()) Try.Add(KnownPath);
		for (const TCHAR* P : VideoPaths) Try.AddUnique(P);
		bool bReached = false;
		for (const FString& P : Try)
		{
			if (!bRunning) break;
			const int32 R = Stream(P);
			if (R == 0) break; // (nothing answering on that port yet: the camera may still be starting)
			bReached = true;
			if (R == 1) break; // (it streamed until it stopped: start again)
			if (P == KnownPath) KnownPath.Reset();
		}
		if (bRunning)
		{
			if (!bReached) SetError(FString::Printf(TEXT("Can't reach the Mevo+ camera at %s:%d yet"), *Host, Port));
			else if (KnownPath.IsEmpty()) SetError(TEXT("No video stream from the Mevo+ camera yet"));
		}
		for (const double Until = VideoNow() + 2; bRunning && VideoNow() < Until;) FPlatformProcess::Sleep(0.05f);
	}
	Close();
	return 0;
}

int32 FOTGMevoVideo::Stream(const FString& Path)
{
	if (!Connect()) return 0;
	const FTCHARToUTF8 Req(*FString::Printf(TEXT("GET %s HTTP/1.0\r\nHost: %s:%d\r\nConnection: close\r\n\r\n"), *Path, *Host, Port));
	int32 Sent = 0;
	Sock->Send(reinterpret_cast<const uint8*>(Req.Get()), Req.Length(), Sent);

	TArray<uint8> Buf, Chunk;
	Chunk.SetNumUninitialized(65536);
	// Read one chunk (waiting up to Ms); false if the connection has closed
	auto ReadSome = [&](int32 Ms) -> bool {
		if (!Sock->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(Ms)))
			return Sock->GetConnectionState() != SCS_ConnectionError;
		int32 Read = 0;
		if (!Sock->Recv(Chunk.GetData(), Chunk.Num(), Read)) return false;
		if (Read > 0) Buf.Append(Chunk.GetData(), Read);
		return Read > 0 || Sock->GetConnectionState() != SCS_ConnectionError;
	};

	// The reply's headers: it has to be 200 with video
	int32 HeadEnd = INDEX_NONE;
	for (const double Until = VideoNow() + 4; bRunning && HeadEnd == INDEX_NONE;)
	{
		if (VideoNow() > Until || !ReadSome(100)) { Close(); return -1; }
		HeadEnd = FindHttpHeadEnd(Buf);
		if (HeadEnd == INDEX_NONE && Buf.Num() > 16384) { Close(); return -1; }
	}
	if (!bRunning) { Close(); return 1; }
	const FUTF8ToTCHAR HeadText(reinterpret_cast<const ANSICHAR*>(Buf.GetData()), HeadEnd);
	const FString Head = FString(HeadText.Length(), HeadText.Get()).ToLower();
	FString StatusLine, Rest;
	if (!Head.Split(TEXT("\r\n"), &StatusLine, &Rest)) StatusLine = Head;
	if (!StatusLine.Contains(TEXT(" 200")) || !(Head.Contains(TEXT("multipart")) || Head.Contains(TEXT("image/jpeg")))) { Close(); return -1; }
	Buf.RemoveAt(0, HeadEnd + 4);
	if (KnownPath != Path) UE_LOG(LogOTGLM, Display, TEXT("Mevo+: camera video at http://%s:%d%s"), *Host, Port, *Path);
	KnownPath = Path;
	SetError(FString());

	// Pictures, one after another (each a JPEG from FF D8 to FF D9 between the part headers). Only the
	// newest complete one in what's arrived is decoded.
	double LastData = VideoNow();
	while (bRunning)
	{
		const int32 Had = Buf.Num();
		if (!ReadSome(200)) break;
		if (Buf.Num() > Had) LastData = VideoNow();
		else if (VideoNow() - LastData > 5) break; // (it's stopped sending)
		int32 Start = INDEX_NONE, End = INDEX_NONE, Consumed = 0;
		for (;;)
		{
			const int32 S = FindJpegMarker(Buf, 0xd8, Consumed);
			if (S == INDEX_NONE) break;
			const int32 E = FindJpegMarker(Buf, 0xd9, S + 2);
			if (E == INDEX_NONE) { Consumed = S; break; }
			Start = S; End = E + 2; Consumed = End;
		}
		if (Start != INDEX_NONE) Picture(Buf.GetData() + Start, End - Start);
		if (Consumed > 0) Buf.RemoveAt(0, Consumed);
		else if (FindJpegMarker(Buf, 0xd8, 0) == INDEX_NONE && Buf.Num() > 1) Buf.RemoveAt(0, Buf.Num() - 1); // (keep a trailing FF)
		if (Buf.Num() > 8 * 1024 * 1024) Buf.Reset(); // (no end in sight: start afresh)
	}
	Close();
	return 1;
}

void FOTGMevoVideo::Picture(const uint8* Jpeg, int32 N)
{
	if (!Images) return;
	TSharedPtr<IImageWrapper> J = Images->CreateImageWrapper(EImageFormat::JPEG);
	TArray64<uint8> Raw;
	if (!J.IsValid() || !J->SetCompressed(Jpeg, N) || !J->GetRaw(ERGBFormat::BGRA, 8, Raw)) return;
	const int32 PicW = int32(J->GetWidth()), PicH = int32(J->GetHeight());
	if (PicW <= 0 || PicH <= 0 || Raw.Num() < int64(PicW) * PicH * 4) return;
	FScopeLock L(&Lock);
	if (Serial == 0) UE_LOG(LogOTGLM, Display, TEXT("Mevo+: first camera picture (%dx%d)"), PicW, PicH);
	Bgra = MoveTemp(Raw);
	W = PicW;
	H = PicH;
	Serial++;
}
