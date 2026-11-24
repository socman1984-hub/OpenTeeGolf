// See OTGWeatherSound.h.

#include "WeatherSound/OTGWeatherSound.h"
#include <cmath>

UOTGWeatherSound::UOTGWeatherSound(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SetSampleRate(Rate);
	NumChannels = 2;
	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	SampleByteSize = 2;
	bCanProcessAsync = true;
}

void UOTGWeatherSound::Thunder(float DelaySeconds, float Loud)
{
	Queued.Enqueue({ Now.load() + int64(FMath::Max(0.f, DelaySeconds) * Rate), FMath::Clamp(Loud, 0.f, 1.f) });
}

void UOTGWeatherSound::FSvf::Set(float Hz, float Q)
{
	const float G = FMath::Tan(PI * Hz / Rate);
	Kq = 1.f / Q;
	A1 = 1.f / (1.f + G * (G + Kq));
	A2 = G * A1;
	A3 = G * A2;
}

void UOTGWeatherSound::FSvf::Run(float In, float& Lo, float& Band, float& Hi)
{
	const float V3 = In - I2, V1 = A1 * I1 + A2 * V3, V2 = I2 + A2 * I1 + A3 * V3;
	I1 = 2.f * V1 - I1;
	I2 = 2.f * V2 - I2;
	Lo = V2;
	Band = V1;
	Hi = In - Kq * V1 - V2;
}

int32 UOTGWeatherSound::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	// (NumSamples counts both sides: frames of two)
	const int32 Frames = NumSamples / 2;
	OutAudio.SetNumUninitialized(Frames * 2 * 2);
	int16* Out = reinterpret_cast<int16*>(OutAudio.GetData());
	if (!bReady)
	{
		for (int32 c = 0; c < 2; c++)
		{
			FSide& Sd = Side[c];
			Sd.Seed = 12345u + 7919u * uint32(c);
			// the hiss: noise between 500 Hz and 6 kHz (no fizz above, no rumble below) ...
			Sd.HissHi.Set(500.f, 0.707f);
			Sd.HissLo.Set(6000.f, 0.6f);
			// ... over a low wash: the rain on everything further off
			Sd.Wash.Set(420.f, 0.6f);
			// the drops: ticks of noise in three bands (a tap, not a ping: a broad band, dying in ms)
			Sd.TickHi.Set(3800.f, 1.1f);
			Sd.TickMid.Set(2200.f, 1.1f);
			Sd.TickLo.Set(1000.f, 1.1f);
		}
		bReady = true;
	}
	const float RainTo = RainLevel.load(), WindTo = WindLevel.load();
	FClap C;
	while (Queued.Dequeue(C)) Waiting.Add(C);
	int64 T = Now.load();
	// (one-pole filters: the coefficient for a corner frequency)
	auto K = [](float Hz) { return 1.f - FMath::Exp(-2.f * PI * Hz / Rate); };
	const float kWind = K(260.f), kWind2 = K(700.f), kRumble = K(110.f), kRumble2 = K(55.f);
	// (the drops' envelopes: gone in about 1, 2.5 and 5 ms)
	const float DecHi = FMath::Exp(-1.f / (Rate * 0.0012f)), DecMid = FMath::Exp(-1.f / (Rate * 0.0025f)), DecLo = FMath::Exp(-1.f / (Rate * 0.005f));
	for (int32 i = 0; i < Frames; i++, T++)
	{
		// (levels glide: no clicks when the weather changes)
		RainNow += (RainTo - RainNow) * 0.00005f;
		WindNow += (WindTo - WindNow) * 0.00005f;
		// (the rain's strength drifts: a new aim every half second and every four, reached slowly)
		if (T % 24000 == 0) Drift1To = White();
		if (T % 192000 == 0) Drift2To = White();
		Drift1 += (Drift1To - Drift1) * (1.f / (Rate * 1.5f));
		Drift2 += (Drift2To - Drift2) * (1.f / (Rate * 6.f));
		const float Drift = 1.f + 0.12f * Drift1 + 0.2f * Drift2;
		const bool bRain = RainNow > 0.001f;
		const float Hiss = 0.2f * FMath::Pow(RainNow, 0.8f) * Drift, Wash = 0.5f * FMath::Pow(RainNow, 1.2f) * Drift;
		const float DropP = (30.f + 1500.f * RainNow) * Drift / Rate;   // (drops a second, each side)
		// wind: its strength wandering with the gusts
		if ((T & 4095) == 0) GustTo = FMath::Clamp(0.55f + White() * 0.45f, 0.15f, 1.f);
		Gust += (GustTo - Gust) * 0.00003f;
		// thunder: those due now start; each a crack (near) then a rumble rolling away for seconds
		for (int32 w = Waiting.Num() - 1; w >= 0; w--)
			if (Waiting[w].At <= T) { Rumbles.Add({ T, Waiting[w].Loud }); Waiting.RemoveAtSwap(w); }
		float Boom = 0;
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
			Boom += R.Lo2 * 14.f * R.Loud * Env * R.Roll;
			if (R.Loud > 0.6f && Age < 0.12f) Boom += W * 0.6f * R.Loud * (1.f - Age / 0.12f);   // (the crack)
		}
		for (int32 c = 0; c < 2; c++)
		{
			FSide& Sd = Side[c];
			float S = Boom, Lo, Band, Hi;
			if (bRain)
			{
				// the hiss and the wash
				Sd.HissHi.Run(Sd.White(), Lo, Band, Hi);
				float H;
				Sd.HissLo.Run(Hi, H, Band, Hi);
				Sd.Wash.Run(Sd.White(), Lo, Band, Hi);
				S += H * Hiss + Lo * Wash;
				// the patter: drops at random, mostly faint, now and then a nearer one; each a tick of
				// noise (never the same twice, never a tone)
				if (Sd.Unit() < DropP)
				{
					const float U = Sd.Unit(), A = 0.03f + 0.18f * U * U * U * U, Band3 = Sd.Unit();
					if (Band3 < 0.55f) Sd.EnvHi += A;
					else if (Band3 < 0.9f) Sd.EnvMid += A;
					else Sd.EnvLo += A * 1.4f;
				}
				Sd.EnvHi *= DecHi; Sd.EnvMid *= DecMid; Sd.EnvLo *= DecLo;
				float Tick = 0;
				Sd.TickHi.Run(Sd.White() * Sd.EnvHi, Lo, Band, Hi); Tick += Band;
				Sd.TickMid.Run(Sd.White() * Sd.EnvMid, Lo, Band, Hi); Tick += Band;
				Sd.TickLo.Run(Sd.White() * Sd.EnvLo, Lo, Band, Hi); Tick += Band;
				S += Tick;
			}
			// the wind: a low rush
			if (WindNow > 0.001f)
			{
				const float W = Sd.White();
				Sd.WindLo += (W - Sd.WindLo) * kWind;
				Sd.WindLo2 += (Sd.WindLo - Sd.WindLo2) * kWind2;
				S += Sd.WindLo2 * 2.4f * WindNow * Gust;
			}
			// (gently limited: only the loudest thunder is touched)
			Out[i * 2 + c] = int16(FMath::Clamp(std::tanh(S) * 30000.f, -32767.f, 32767.f));
		}
	}
	Now.store(T);
	return Frames * 2;
}
