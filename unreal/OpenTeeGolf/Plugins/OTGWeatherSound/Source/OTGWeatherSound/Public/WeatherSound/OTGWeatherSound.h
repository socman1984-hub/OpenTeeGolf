// The weather's sound, made as it plays (no recordings, nothing to license): rain - a soft steady
// hiss over a low wash, with a patter of tiny drop ticks, as heavy as the rain; wind - a low rush
// rising and falling with the gusts; thunder - a crack when the strike is near, then a long rolling
// rumble. Stereo, each side its own noise, so the rain is all round rather than in the middle; its
// strength drifts slowly at random, so nothing in it repeats. AOTGRound sets the levels each frame
// and queues the thunder (after the flash: a second for each 343 m).

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Sound/SoundWaveProcedural.h"
#include <atomic>
#include "OTGWeatherSound.generated.h"

UCLASS()
class OTGWEATHERSOUND_API UOTGWeatherSound : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	UOTGWeatherSound(const FObjectInitializer& ObjectInitializer);

	// 0..1 each
	void SetLevels(float Rain, float Wind) { RainLevel.store(Rain); WindLevel.store(Wind); }
	// A thunderclap DelaySeconds from now, Loud 0..1 (near: a crack first)
	void Thunder(float DelaySeconds, float Loud);

	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;   // (16-bit, stereo interleaved)

private:
	static constexpr int32 Rate = 48000;
	std::atomic<float> RainLevel{ 0.f }, WindLevel{ 0.f };
	struct FClap { int64 At; float Loud; };
	TQueue<FClap, EQueueMode::Spsc> Queued;
	std::atomic<int64> Now{ 0 };   // (frames made so far: the audio thread's clock)

	// A state-variable filter (Zavalishin's: stable at any setting): low, band and high pass at once
	struct FSvf
	{
		float A1 = 0, A2 = 0, A3 = 0, Kq = 1, I1 = 0, I2 = 0;
		void Set(float Hz, float Q);
		void Run(float In, float& Lo, float& Band, float& Hi);
	};
	// (each side's own: its noise, filters and drop envelopes)
	struct FSide
	{
		uint32 Seed = 1;
		FSvf HissHi, HissLo, Wash, TickHi, TickMid, TickLo;
		float EnvHi = 0, EnvMid = 0, EnvLo = 0, WindLo = 0, WindLo2 = 0;
		float White() { Seed = Seed * 1664525u + 1013904223u; return float(Seed >> 8) / float(1 << 23) - 1.f; }
		float Unit() { return White() * 0.5f + 0.5f; }
	};
	FSide Side[2];
	bool bReady = false;

	// (the audio thread's own state)
	uint32 Seed = 22222;
	float Gust = 0.6f, GustTo = 0.6f, Drift1 = 0, Drift1To = 0, Drift2 = 0, Drift2To = 0;
	float RainNow = 0, WindNow = 0;
	struct FRumble { int64 Start; float Loud; float Lo = 0, Lo2 = 0, Roll = 0, RollTo = 1; };
	TArray<FRumble> Rumbles;
	TArray<FClap> Waiting;
	float White() { Seed = Seed * 1664525u + 1013904223u; return float(Seed >> 8) / float(1 << 23) - 1.f; }
};
