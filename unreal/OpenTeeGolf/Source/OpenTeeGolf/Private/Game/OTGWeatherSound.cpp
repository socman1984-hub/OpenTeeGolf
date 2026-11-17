// See OTGWeatherSound.h.

#include "OTGWeatherSound.h"

UOTGWeatherSound::UOTGWeatherSound(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SetSampleRate(Rate);
	NumChannels = 1;
	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	SampleByteSize = 2;
	bCanProcessAsync = true;
}

void UOTGWeatherSound::Thunder(float DelaySeconds, float Loud)
{
	Queued.Enqueue({ Now.load() + int64(FMath::Max(0.f, DelaySeconds) * Rate), FMath::Clamp(Loud, 0.f, 1.f) });
}

int32 UOTGWeatherSound::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	OutAudio.SetNumUninitialized(NumSamples * 2);
	int16* Out = reinterpret_cast<int16*>(OutAudio.GetData());
	const float RainTo = RainLevel.load(), WindTo = WindLevel.load();
	FClap C;
	while (Queued.Dequeue(C)) Waiting.Add(C);
	int64 T = Now.load();
	// (one-pole filters: the coefficient for a corner frequency)
	auto K = [](float Hz) { return 1.f - FMath::Exp(-2.f * PI * Hz / Rate); };
	const float kRainLo = K(1800.f), kRainHi = K(9000.f), kWind = K(260.f), kWind2 = K(700.f), kRumble = K(110.f), kRumble2 = K(55.f);
	for (int32 i = 0; i < NumSamples; i++, T++)
	{
		// (levels glide: no clicks when the weather changes)
		RainNow += (RainTo - RainNow) * 0.00005f;
		WindNow += (WindTo - WindNow) * 0.00005f;
		float S = 0;
		// rain: the hiss (high-passed noise, softened at the top) ...
		if (RainNow > 0.001f)
		{
			const float W = White();
			RainLo += (W - RainLo) * kRainLo;
			RainHi += ((W - RainLo) - RainHi) * kRainHi;
			S += RainHi * 0.32f * FMath::Sqrt(RainNow);
			// ... and single drops: short pinging taps, more in heavier rain
			if (Drops.Num() < 24 && (White() * 0.5f + 0.5f) < RainNow * 0.0022f)
				Drops.Add({ 0.f, 1800.f + (White() * 0.5f + 0.5f) * 3800.f, 0.05f + (White() * 0.5f + 0.5f) * 0.12f });
			for (int32 d = Drops.Num() - 1; d >= 0; d--)
			{
				FDrop& D = Drops[d];
				S += FMath::Sin(D.Phase) * D.Amp;
				D.Phase += 2.f * PI * D.Freq / Rate;
				D.Amp *= 0.9975f;
				if (D.Amp < 0.002f) Drops.RemoveAtSwap(d);
			}
		}
		// wind: a low rush, its strength wandering with the gusts
		if (WindNow > 0.001f)
		{
			if ((T & 4095) == 0) GustTo = FMath::Clamp(0.55f + White() * 0.45f, 0.15f, 1.f);
			Gust += (GustTo - Gust) * 0.00003f;
			const float W = White();
			WindLo += (W - WindLo) * kWind;
			WindLo2 += (WindLo - WindLo2) * kWind2;
			S += WindLo2 * 2.4f * WindNow * Gust;
		}
		// thunder: those due now start; each a crack (near) then a rumble rolling away for seconds
		for (int32 w = Waiting.Num() - 1; w >= 0; w--)
			if (Waiting[w].At <= T) { Rumbles.Add({ T, Waiting[w].Loud }); Waiting.RemoveAtSwap(w); }
		for (int32 r = Rumbles.Num() - 1; r >= 0; r--)
		{
			FRumble& R = Rumbles[r];
			const float Age = float(T - R.Start) / Rate;
			if (Age > 9.f) { Rumbles.RemoveAtSwap(r); continue; }
			const float W = White();
			R.Lo += (W - R.Lo) * kRumble;
			R.Lo2 += (R.Lo - R.Lo2) * kRumble2;
			if ((T & 2047) == 0) R.RollTo = 0.4f + (White() * 0.5f + 0.5f) * 1.2f;
			R.Roll += (R.RollTo - R.Roll) * 0.0004f;
			const float Env = FMath::Min(1.f, Age / 0.25f) * FMath::Exp(-Age / 2.4f);
			S += R.Lo2 * 14.f * R.Loud * Env * R.Roll;
			if (R.Loud > 0.6f && Age < 0.12f) S += W * 0.6f * R.Loud * (1.f - Age / 0.12f);   // (the crack)
		}
		// (gently limited, and smoothed a touch)
		S = FMath::Clamp(S, -3.f, 3.f);
		S = S / (1.f + FMath::Abs(S));
		Smooth[0] += (S - Smooth[0]) * 0.7f;
		Out[i] = int16(FMath::Clamp(Smooth[0] * 30000.f, -32767.f, 32767.f));
	}
	Now.store(T);
	return NumSamples;
}
