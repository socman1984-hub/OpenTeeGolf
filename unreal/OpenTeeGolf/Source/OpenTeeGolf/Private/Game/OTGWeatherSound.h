// The weather's sound, made as it plays (no recordings): rain - a hiss and the patter of single drops,
// as heavy as the rain; wind - a low rush rising and falling with the gusts; thunder - a crack when
// the strike is near, then a long rolling rumble. AOTGRound sets the levels each frame and queues
// the thunder (after the flash: a second for each 343 m).

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Sound/SoundWaveProcedural.h"
#include <atomic>
#include "OTGWeatherSound.generated.h"

UCLASS()
class UOTGWeatherSound : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	UOTGWeatherSound(const FObjectInitializer& ObjectInitializer);

	// 0..1 each
	void SetLevels(float Rain, float Wind) { RainLevel.store(Rain); WindLevel.store(Wind); }
	// A thunderclap DelaySeconds from now, Loud 0..1 (near: a crack first)
	void Thunder(float DelaySeconds, float Loud);

	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;   // (16-bit, mono)

private:
	static constexpr int32 Rate = 48000;
	std::atomic<float> RainLevel{ 0.f }, WindLevel{ 0.f };
	struct FClap { int64 At; float Loud; };
	TQueue<FClap, EQueueMode::Spsc> Queued;
	std::atomic<int64> Now{ 0 };   // (samples made so far: the audio thread's clock)

	// (the audio thread's own state)
	uint32 Seed = 22222;
	float RainLo = 0, RainHi = 0, WindLo = 0, WindLo2 = 0, Gust = 0.6f, GustTo = 0.6f, Smooth[2] = { 0, 0 };
	float RainNow = 0, WindNow = 0;
	struct FDrop { float Phase, Freq, Amp; };
	TArray<FDrop> Drops;
	struct FRumble { int64 Start; float Loud; float Lo = 0, Lo2 = 0, Roll = 0, RollTo = 1; };
	TArray<FRumble> Rumbles;
	TArray<FClap> Waiting;
	float White() { Seed = Seed * 1664525u + 1013904223u; return float(Seed >> 8) / float(1 << 23) - 1.f; }
};
