// AOTGRound (see OTGRound.h), continued: the free view, the flag blowing in the wind, and the
// weather changed during the round (the weather panel).

#include "Round/OTGRound.h"

#include "Camera/CameraActor.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Course/OTGCourseData.h"
#include "ProceduralMeshComponent.h"
#include "UI/Hud/SOTGHud.h"
#include "UI/Designer/SOTGCourseDesigner.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "World/OTGRuntimeCourse.h"
#include "EngineUtils.h"
#include "WeatherSound/OTGWeatherSound.h"
#include "Components/AudioComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/SkyLight.h"
#include "Kismet/GameplayStatics.h"

// ------------------------------------------------------------------ storms and the weather's sound

// The rain's and wind's sound every frame; in a storm, lightning now and then: the sky light and the
// clouds (M_Clouds' Flash) leap in two or three flickers, and the thunder follows - a second for
// every 343 m to the strike, louder the nearer it was
void FOTGRoundScene::UpdateStorm(float Dt)
{
	if (!WxSound)
	{
		WxSound = NewObject<UOTGWeatherSound>(Round);
		WxAudio = UGameplayStatics::CreateSound2D(Round, WxSound, 0.8f, 1.f, 0.f, nullptr, true, false);
		if (WxAudio) WxAudio->Play();
	}
	const double Mm = Round->Weather.RainMm;
	const float Rain = Mm > 0.05 ? float(FMath::Clamp(0.2 + Mm / 7.0, 0.0, 1.0)) : 0.f;
	const float Wind = Round->bMini ? 0.f : float(FMath::Clamp((Round->Weather.WindMph + 0.3 * FMath::Max(0.0, Round->Weather.GustMph - Round->Weather.WindMph)) / 35.0, 0.0, 1.0));
	WxSound->SetLevels(Rain, Wind);
	// (under the Meridian sky the lightning's its: OnSkyBolt brings the thunder)
	if (Sky) return;
	// lightning: a storm's (heavy rain), every 6 to 22 s
	const bool bStorm = Mm >= 5.0;
	BoltAge += Dt;
	if (bStorm && (NextBolt -= Dt) <= 0.f)
	{
		NextBolt = FMath::FRandRange(6.f, 22.f);
		BoltAge = 0.f;
		const float Km = FMath::FRandRange(0.6f, 9.f);
		BoltNear = FMath::Clamp(1.6f / Km, 0.15f, 1.f);
		WxSound->Thunder(Km * 1000.f / 343.f, BoltNear);
	}
	float F = 0.f;
	if (BoltAge < 1.2f)
		for (const float At : { 0.f, 0.13f, 0.34f })
			if (BoltAge >= At) F += FMath::Exp(-(BoltAge - At) / 0.07f) * (At > 0.2f ? 0.6f : 1.f);
	F *= BoltNear;
	if (BoltAge < 1.5f)
	{
		for (TActorIterator<ASkyLight> It(Round->GetWorld()); It; ++It)
			if (USkyLightComponent* SL = It->GetLightComponent())
			{
				if (SkyNow < 0) SkyNow = SL->Intensity;
				SL->SetIntensity(SkyNow * (1.f + F * 7.f));
				break;
			}
		if (OwnCloudMat) OwnCloudMat->SetScalarParameterValue(TEXT("Flash"), F);
	}
}

// ------------------------------------------------------------------ changing weather

namespace
{
	FOTGWeather LerpWeather(const FOTGWeather& A, const FOTGWeather& B, double T)
	{
		FOTGWeather W = B;
		const double S = FMath::SmoothStep(0.0, 1.0, T);
		W.WindMph = FMath::Lerp(A.WindMph, B.WindMph, S);
		W.GustMph = FMath::Lerp(A.GustMph, B.GustMph, S);
		W.Cloud = FMath::Lerp(A.Cloud, B.Cloud, S);
		W.Fog = FMath::Lerp(A.Fog, B.Fog, S);
		W.TempC = FMath::Lerp(A.TempC, B.TempC, S);
		W.Humidity = FMath::Lerp(A.Humidity, B.Humidity, S);
		// (the rain comes once the clouds are in, and stops before they go)
		W.RainMm = B.RainMm > A.RainMm ? FMath::Lerp(A.RainMm, B.RainMm, FMath::SmoothStep(0.45, 1.0, T)) : FMath::Lerp(A.RainMm, B.RainMm, FMath::SmoothStep(0.0, 0.55, T));
		// (the wind swings round the short way)
		const double D = FMath::Fmod(B.WindFromMet - A.WindFromMet + 540.0, 360.0) - 180.0;
		W.WindFromMet = FMath::Fmod(A.WindFromMet + D * S + 360.0, 360.0);
		W.Label = T < 1.0 ? A.Label : B.Label;
		return W;
	}

	// "changing": fair, clouding over, showers (now and then a storm), clearing, fair again
	FOTGWeather Stage(int32 K, const FOTGWeather& Was)
	{
		FOTGWeather W = Was;
		auto Set = [&](const TCHAR* L, double Wind, double Gust, double T, double H, double Rain, double Cl) {
			W.Label = L; W.WindMph = Wind; W.GustMph = Gust; W.TempC = T; W.Humidity = H; W.RainMm = Rain; W.Cloud = Cl; W.Fog = 0;
		};
		switch (K)
		{
		case 0: Set(TEXT("Changing: fair"), 7, 12, 22, 0.5, 0, 0.25); break;
		case 1: Set(TEXT("Changing: clouding over"), 12, 19, 19, 0.65, 0, 0.62); break;
		case 2: Set(TEXT("Changing: showers"), 14, 24, 16, 0.9, 2.5, 0.96); break;
		case 3: Set(TEXT("Changing: storm"), 22, 36, 14, 1, 7, 1); break;
		default: Set(TEXT("Changing: clearing"), 10, 16, 18, 0.7, 0, 0.45); break;
		}
		W.WindFromMet = FMath::Fmod(Was.WindFromMet + FMath::FRandRange(-35.0, 35.0) + 360.0, 360.0);
		W.Key = TEXT("changing");
		return W;
	}
}

void FOTGRoundChanging::BlendTo(const FOTGWeather& Next, const FString& Say)
{
	WxFrom = Round->Weather;
	WxTo = Next;
	WxBlend = 0.f;
	if (!Say.IsEmpty()) Round->HudView.Toast(Say, 4.f);
}

// Weather that changes as the round goes on: the "changing" choice moves through its stages - each
// held 3 to 6 minutes, a minute and a half from one to the next - and live weather is fetched again
// every 15 minutes and blended in. The scene, the wind box and the HUD follow as it goes; the
// physics plays the weather as it is at each shot.
static TAutoConsoleVariable<float> CVarWxChangeSpeed(TEXT("OTG.Weather.ChangeSpeed"), 1.f, TEXT("How fast changing weather moves on (a test: 20 for twenty times as fast)"));

void FOTGRoundChanging::UpdateChanging(float Dt)
{
	Dt *= FMath::Max(0.01f, CVarWxChangeSpeed.GetValueOnGameThread());
	const bool bChanging = Round->Weather.Key == TEXT("changing");
	if (!bChanging && !Round->Weather.bLive) return;
	if (WxBlend < 1.f)
	{
		WxBlend = FMath::Min(1.f, WxBlend + Dt / 90.f);
		Round->Weather = LerpWeather(WxFrom, WxTo, WxBlend);
		if ((WxApplyIn -= Dt) <= 0.f || WxBlend >= 1.f)
		{
			WxApplyIn = 0.5f;
			Round->Scene.ApplyWeatherToScene();
			Round->HudView.UpdateHudInfo();
			Round->HudView.UpdateWindBox();
		}
		return;
	}
	if (bChanging)
	{
		if (WxHold <= 0.f) WxHold = FMath::FRandRange(180.f, 360.f);
		if ((WxHold -= Dt) > 0.f) return;
		WxHold = FMath::FRandRange(180.f, 360.f);
		// (after the showers: a storm one time in three, otherwise clearing)
		WxStage = WxStage == 2 ? (FMath::FRand() < 0.33f ? 3 : 4) : WxStage >= 3 ? 4 : (WxStage + 1) % 5;
		if (WxStage == 4 && Round->Weather.RainMm < 0.05 && Round->Weather.Cloud < 0.5) WxStage = 0;
		static const TCHAR* Says[] = { TEXT("The sky's clearing"), TEXT("Clouds coming in"), TEXT("Rain on the way"), TEXT("A storm's coming"), TEXT("The rain's easing off") };
		BlendTo(Stage(WxStage, Round->Weather), Says[WxStage]);
		return;
	}
	// live: fetched again every 15 minutes
	if ((WxRefresh += Dt) < 900.f || !Round->Data || (Round->Data->Latitude == 0 && Round->Data->Longitude == 0)) return;
	WxRefresh = 0.f;
	TWeakObjectPtr<AOTGRound> Self(Round);
	FOTGWeather::FetchLive(Round->Data->Latitude, Round->Data->Longitude, [Self](bool bOk, const FOTGWeather& W, const FString&) {
		if (bOk && Self.IsValid()) Self->Changing.BlendTo(W, FString());
	});
}

// ------------------------------------------------------------------ the free view

void FOTGRoundCamera::ToggleFreeView()
{
	bFreeView = !bFreeView;
	if (bFreeView)
	{
		// (from where the camera is, looking where it looks)
		FreePos = CamPos;
		FreeRot = (CamLookNow - CamPos).Rotation();
		FreeRot.Roll = 0;
		Round->HudView.Toast(TEXT("Free view: W A S D to fly, Q / E down and up, hold the right mouse button to look, Shift faster, F to come back"), 5.f);
	}
	else
	{
		AddressView(false);
		CamPos = FreePos; // (and glide back from there)
	}
}

// Move (forward, right, up: -1..1) and look (degrees) this frame
void FOTGRoundCamera::FreeFly(float Dt, const FVector& Move, const FVector2D& Look, bool bFast)
{
	if (!bFreeView) return;
	FreeRot.Yaw += Look.X;
	FreeRot.Pitch = FMath::Clamp(FreeRot.Pitch + Look.Y, -85.0, 85.0);
	const FRotationMatrix R(FreeRot);
	// (faster the higher it is: a metre a second near the ground, tens of metres up high)
	const FVector C = Round->Data->WorldToCourse(FreePos);
	const double G = Round->Course.HeightAt(C.X, C.Z);
	const double Above = FMath::IsFinite(G) ? FMath::Max(1.0, (FreePos.Z - Round->Data->CourseToWorld(FVector(C.X, G, C.Z)).Z) / 100.0) : 30.0;
	const double Speed = FMath::Clamp(Above * 120.0, 400.0, 9000.0) * (bFast ? 6.0 : 1.0);
	FreePos += (R.GetUnitAxis(EAxis::X) * Move.X + R.GetUnitAxis(EAxis::Y) * Move.Y + FVector::UpVector * Move.Z) * Speed * Dt;
	if (FMath::IsFinite(G))
	{
		const double MinZ = Round->Data->CourseToWorld(FVector(C.X, G, C.Z)).Z + 40;
		if (FreePos.Z < MinZ) FreePos.Z = MinZ;
	}
	// (nor under the water: AOTGRuntimeCourse::WaterTopAt)
	for (TActorIterator<AOTGRuntimeCourse> It(Round->GetWorld()); It; ++It) { FreePos.Z = FMath::Max(FreePos.Z, It->WaterTopAt(FreePos) + 40); break; }
	if (Cam) Cam->SetActorLocationAndRotation(FreePos, FreeRot);
}

// ------------------------------------------------------------------ wet ground

// How wet the course is (MPC_Weather's Wet: the ground's material darker and glossier): in rain it
// soaks in over a minute or so - more in heavier rain - and once it stops dries over several. A
// round that starts in rain starts wet.
void FOTGRoundRain::UpdateWet(float Dt)
{
	const float Target = float(FMath::Clamp(Round->Weather.RainMm / 3.0, 0.0, 1.0)) * (Round->Weather.RainMm > 0.05 ? 1.f : 0.f);
	if (Wet < 0) Wet = Target;
	else if (Target > Wet) Wet = FMath::Min(Target, Wet + Dt / 60.f);
	else Wet = FMath::Max(Target, Wet - Dt / 300.f);
	if (!Round->WeatherMPC) Round->WeatherMPC = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/OpenTeeGolf/Game/MPC_Weather.MPC_Weather"));
	if (Round->WeatherMPC)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(Round, Round->WeatherMPC, TEXT("Wet"), Wet);
		// (Greensward's grass, the rough: as wet - season, dryness and snow as it grows)
		if (!Round->GsMPC) Round->GsMPC = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Greensward/Materials/MPC_Greensward.MPC_Greensward"));
		if (Round->GsMPC) UKismetMaterialLibrary::SetVectorParameterValue(Round, Round->GsMPC, TEXT("Environment"), FLinearColor(0.f, 0.f, 0.f, Wet));
		UKismetMaterialLibrary::SetScalarParameterValue(Round, Round->WeatherMPC, TEXT("Rain"), Round->Weather.RainMm > 0.05 ? float(FMath::Clamp(0.25 + Round->Weather.RainMm / 6.0, 0.0, 1.0)) : 0.f);
	}
}

// The rain away from the camera. No curtains: sheets at a fixed distance read as a wall standing
// across the hole; far rain is the height fog's (thicker in rain, ApplySky), which greys the far end
// of a hole smoothly with distance. Here: splashes where the drops hit hard ground in front of the
// camera - paths (little crowns, a fifth of a second each: three streaks fanning up and out, with the
// rain's own material) and, softer and lower, bunker sand. Grass, greens and rough take the rain
// without a splash (their wetness is MPC_Weather's).
void FOTGRoundRain::UpdateRainFar(float Dt, double Mm)
{
	const bool bOn = Mm > 0.05 && Round->Camera.Cam && Round->Data && RainMat;
	if (!bOn)
	{
		if (SplashVerts) { RainMesh->ClearMeshSection(1); SplashVerts = 0; }
		Splashes.Reset();
		return;
	}
	// ---- splashes: as many as the rain is heavy, on the ground ahead (in a fan 3 to 18 m out)
	const FVector Fwd = Round->Camera.Cam->GetActorForwardVector().GetSafeNormal2D();
	SplashCarry += float(FMath::Min(Mm, 10.0) * 70.0 * Dt);
	while (SplashCarry >= 1.f)
	{
		SplashCarry -= 1.f;
		if (Splashes.Num() >= 500) continue;
		const double D = 300.0 + FMath::Sqrt(FMath::FRand()) * 1500.0;
		FVector P = Round->Camera.CamPos + Fwd.RotateAngleAxis(FMath::FRandRange(-65.f, 65.f), FVector::UpVector) * D;
		const FVector C = Round->Data->WorldToCourse(P);
		// (only where a drop would splash: a path's hard top; sand a small low puff; anything grown none)
		const otg::Surface Su = Round->Course.SurfaceAt(C.X, C.Z);
		float Size;
		if (Su == otg::Surface::Path) Size = FMath::FRandRange(0.7f, 1.4f);
		else if (Su == otg::Surface::Sand) Size = FMath::FRandRange(0.3f, 0.5f);
		else continue;
		P.Z = Round->World(otg::V3{ C.X, Round->Course.HeightAt(C.X, C.Z), C.Z }).Z + 0.5;
		Splashes.Add({ P, 0.f, Size });
	}
	TArray<FVector> V;
	TArray<int32> T;
	TArray<FVector2D> UV;
	V.Reserve(Splashes.Num() * 12);
	for (int32 i = Splashes.Num() - 1; i >= 0; i--)
	{
		FOTGRoundRain::FSplash& S = Splashes[i];
		S.Age += Dt;
		if (S.Age > 0.2f) { Splashes.RemoveAtSwap(i); continue; }
		const float K = S.Age / 0.2f;
		const double Up = 6.0 * S.Size * FMath::Sin(PI * FMath::Min(1.f, K * 1.4f)), Out = 4.0 * S.Size * K;
		const FVector ToCam = (Round->Camera.CamPos - S.P).GetSafeNormal();
		for (int32 k = 0; k < 3; k++)
		{
			const FVector Dir = FVector(FMath::Cos(k * 2.1 + i), FMath::Sin(k * 2.1 + i), 0);
			const FVector A = S.P, Bp = S.P + Dir * Out + FVector(0, 0, Up);
			const FVector Side = FVector::CrossProduct((Bp - A).GetSafeNormal(), ToCam).GetSafeNormal() * 0.3;
			const int32 B = V.Num();
			V.Append({ Bp - Side, Bp + Side, A - Side, A + Side });
			UV.Append({ FVector2D(0, 0.3), FVector2D(1, 0.3), FVector2D(0, 1), FVector2D(1, 1) });
			T.Append({ B, B + 2, B + 1, B + 1, B + 2, B + 3 });
		}
	}
	if (V.Num())
	{
		RainMesh->CreateMeshSection_LinearColor(1, V, T, TArray<FVector>(), UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		RainMesh->SetMaterial(1, RainMat);
	}
	else if (SplashVerts) RainMesh->ClearMeshSection(1);
	SplashVerts = V.Num();
}

// ------------------------------------------------------------------ the flag

// A cloth flag on the pole: hanging down in calm air, flying out downwind and rippling more the
// harder it blows (and a little with each gust)
void FOTGRoundFlag::UpdateFlag(float Dt)
{
	if (!FlagCloth || !Round->Data) return;
	FlagTime += Dt;
	double Mps, FromDeg;
	Round->HoleWind(Mps, FromDeg);
	const double Mph = Mps / 0.44704;
	// (gusting: the strength wanders a little)
	const double Gust = Round->Weather.GustMph > Round->Weather.WindMph ? (Round->Weather.GustMph - Round->Weather.WindMph) * 0.5 * (0.5 + 0.5 * FMath::Sin(FlagTime * 0.7) * FMath::Sin(FlagTime * 1.9)) : 0.0;
	const double S = FMath::Clamp((Mph + Gust) / 16.0, 0.0, 1.0);
	const double F = FMath::DegreesToRadians(FromDeg);
	// downwind, in the level (course (-sin, -cos) is world (-sin, +cos)); in calm air any way
	const FVector Down = Mps > 0.05 ? FVector(-FMath::Sin(F), FMath::Cos(F), 0) : FVector(1, 0, 0);
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Down);
	// (and the same wind for the trees and grass: their materials read it from MPC_Wind - which way,
	// m/s with the gusts)
	if (!Round->WindMPC) Round->WindMPC = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/OpenTeeGolf/Game/MPC_Wind.MPC_Wind"));
	if (Round->WindMPC) UKismetMaterialLibrary::SetVectorParameterValue(Round, Round->WindMPC, TEXT("Wind"), FLinearColor(Down.X, Down.Y, float((Mph + Gust) * 0.44704), 0.f));
	// (and the Megascans grasses': their own collection - which way the wind blows, in degrees, and
	// how hard; their pace left alone, as ours is: a pace that follows the gusts makes them jump)
	if (!Round->FabWindMPC) Round->FabWindMPC = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Fab/MaterialParameterCollection/QMPC_GlobalFoliageActor.QMPC_GlobalFoliageActor"));
	if (Round->FabWindMPC)
	{
		const float K = float(FMath::Clamp((Mph + Gust) * 0.44704 / 12.0, 0.0, 1.0));
		UKismetMaterialLibrary::SetScalarParameterValue(Round, Round->FabWindMPC, TEXT("Wind Direction"), float(FMath::RadiansToDegrees(FMath::Atan2(Down.Y, Down.X))));
		UKismetMaterialLibrary::SetScalarParameterValue(Round, Round->FabWindMPC, TEXT("Wind Strength Plants"), FMath::Lerp(0.25f, 3.0f, K));
	}
	const double Droop = FMath::DegreesToRadians(FMath::Lerp(82.0, 12.0, S));
	const FVector Along = Down * FMath::Cos(Droop) - FVector::UpVector * FMath::Sin(Droop);
	const FVector Hang = FVector::CrossProduct(Side, Along).GetSafeNormal(); // (the flag's own "down": along the pole when it flies)
	const FVector Top = Round->World(Round->Pin()) + FVector(0, 0, Round->FlagTopCm);
	const double L = Round->bMini ? 34 : 55, H = Round->bMini ? 22 : 36;   // cm
	const int32 NU = 12, NV = 6;
	const double Amp = FMath::Lerp(1.5, 9.0, S), Freq = FMath::Lerp(2.0, 7.5, S);
	// (the ripples' progress, added up a frame at a time: time x a pace that follows the gusts
	// jumped whenever a gust changed it - the cloth leapt to a new shape every frame and smeared)
	FlagPhase += Dt * Freq;
	TArray<FVector> V;
	TArray<FVector2D> UV;
	V.Reserve((NU + 1) * (NV + 1));
	for (int32 j = 0; j <= NV; j++)
		for (int32 i = 0; i <= NU; i++)
		{
			const double U = double(i) / NU, W = double(j) / NV;
			// ripples travelling out from the pole, growing toward the free end
			// (and the free end flutters: a quicker, smaller ripple near it)
			const double Wave = FMath::Sin(U * 7.0 - FlagPhase + W * 0.8) * Amp * U + FMath::Sin(U * 13.0 - FlagPhase * 1.7) * Amp * 0.3 * U
				+ FMath::Sin(U * 17.0 - FlagPhase * 2.1 + W * 2.0) * Amp * 0.15 * U * U * S;
			// (the free end sags a little when it's slack)
			const double Sag = (1 - S) * 6.0 * U * U;
			V.Add(Top + Along * (U * L) + Hang * (W * H + Sag) + Side * Wave);
			UV.Add(FVector2D(U, W));
		}
	TArray<int32> Tri;
	for (int32 j = 0; j < NV; j++)
		for (int32 i = 0; i < NU; i++)
		{
			const int32 A = j * (NU + 1) + i, B = A + 1, C = A + NU + 1, D = C + 1;
			Tri.Append({ A, C, B, B, C, D });
		}
	// normals from the grid
	TArray<FVector> N;
	N.SetNum(V.Num());
	for (int32 j = 0; j <= NV; j++)
		for (int32 i = 0; i <= NU; i++)
		{
			const FVector& P0 = V[j * (NU + 1) + FMath::Max(i - 1, 0)];
			const FVector& P1 = V[j * (NU + 1) + FMath::Min(i + 1, NU)];
			const FVector& Q0 = V[FMath::Max(j - 1, 0) * (NU + 1) + i];
			const FVector& Q1 = V[FMath::Min(j + 1, NV) * (NU + 1) + i];
			N[j * (NU + 1) + i] = FVector::CrossProduct(P1 - P0, Q1 - Q0).GetSafeNormal();
		}
	// the back: its own copy of the cloth, facing the other way and a hair behind (one surface shared
	// by both sides was lit as the front from behind, and the two fought in the shadows: a flicker)
	{
		const int32 Nv = V.Num(), Nt = Tri.Num();
		V.Reserve(Nv * 2); N.Reserve(Nv * 2); UV.Reserve(Nv * 2);
		for (int32 i = 0; i < Nv; i++)
		{
			const FVector P = V[i], Nn = N[i];
			const FVector2D T = UV[i];
			V.Add(P - Nn * 0.12); N.Add(-Nn); UV.Add(T);
		}
		Tri.Reserve(Nt * 2);
		for (int32 t = 0; t < Nt; t += 3)
		{
			const int32 A = Tri[t] + Nv, B = Tri[t + 1] + Nv, C = Tri[t + 2] + Nv;
			Tri.Add(A); Tri.Add(C); Tri.Add(B);
		}
	}
	if (!bFlagMade)
	{
		FlagCloth->CreateMeshSection_LinearColor(0, V, Tri, N, UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		FlagCloth->SetMaterial(0, Round->FlagMat);
		bFlagMade = true;
	}
	else FlagCloth->UpdateMeshSection_LinearColor(0, V, N, UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>());
}

// ------------------------------------------------------------------ the weather, changed in the round

void AOTGRound::ToggleWeatherPanel()
{
	if (Hud) Hud->ShowWeatherPanel(!Hud->IsWeatherPanelShown());
}

void AOTGRound::ToggleGrassDesigner(int32 Tab)
{
	if (!Hud) return;
	const bool bShow = !Hud->IsDesignerShown();
	if (bShow)
	{
		AOTGRuntimeCourse* C = nullptr;
		for (TActorIterator<AOTGRuntimeCourse> It(GetWorld()); It; ++It) { C = *It; break; }
		TWeakObjectPtr<AOTGRound> Self(this);
		Hud->Designer()->Trees()->OnChanged = [Self]() { if (Self.IsValid()) Self->RefreshTrees(); };
		Hud->Designer()->Trees()->OnShow = [Self](const FString& Kind) { if (Self.IsValid()) Self->ShowTree(Kind); };
		Hud->Designer()->GrassTab()->OnShow = [Self](const FString& Kind) { if (Self.IsValid()) Self->ShowGrass(Kind); };
		Hud->Designer()->BridgeTab()->OnChanged = [Self]() { if (Self.IsValid()) Self->RefreshBridges(); };
		Hud->Designer()->Open(C);
		if (Tab >= 0) Hud->Designer()->ShowTab(Tab);
	}
	Hud->ShowDesigner(bShow);
}

void AOTGRound::RefreshTrees()
{
	if (!Data) return;
	Data->FillTrees(Trees);
	UE_LOG(LogTemp, Display, TEXT("The ball's trees: %d"), int32(Trees.all.size()));
}

void AOTGRound::ShowTree(const FString& Kind)
{
	AOTGRuntimeCourse* C = nullptr;
	for (TActorIterator<AOTGRuntimeCourse> It(GetWorld()); It; ++It) { C = *It; break; }
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const FVector From = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	FVector Base;
	double H = 0;
	if (!C || !C->NearestTree(Kind, From, Base, H)) return;
	// (from the side you're on, far enough back to see it whole)
	const FVector Dir = (Base - From).GetSafeNormal2D();
	const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	if (Kind == TEXT("rock")) Camera.FreeLookAt(Base + FVector(0, 0, H * 0.3), FMath::Max(700.0, H * 4.0), FMath::Max(150.0, H * 0.8), Yaw);   // (a rock: closer, a little above it)
	else Camera.FreeLookAt(Base + FVector(0, 0, H * 0.5), FMath::Max(2000.0, H * 2.6), H * 0.15, Yaw);
}

void AOTGRound::ShowGrass(const FString& Kind)
{
	AOTGRuntimeCourse* C = nullptr;
	for (TActorIterator<AOTGRuntimeCourse> It(GetWorld()); It; ++It) { C = *It; break; }
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const FVector From = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	FVector At;
	if (!C || !C->FindGrass(Kind, From, At)) { HudView.Toast(TEXT("None of it grows near here"), 3.f); return; }
	// (from the side you're on, low down at a few metres: as you'd see it walking up to it)
	const FVector Dir = (At - From).GetSafeNormal2D();
	const double Yaw = Dir.IsNearlyZero() ? Camera.FreeRot.Yaw : FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	Camera.FreeLookAt(At + FVector(0, 0, 30), 650, 170, Yaw);
}

bool AOTGRound::DesignClick(const FVector& Origin, const FVector& Dir)
{
	if (!Hud || !Hud->IsDesignerShown()) return false;
	FVector Ground;
	const bool bGround = GroundAlongRay(Origin, Dir, Ground);
	return Hud->Designer()->WorldClick(Origin, Dir, bGround, Ground);
}

void AOTGRound::DesignClickAt(double FX, double FY)
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return;
	int32 W = 0, H = 0;
	PC->GetViewportSize(W, H);
	FVector Origin, Dir;
	if (!PC->DeprojectScreenPositionToWorld(FX * W, FY * H, Origin, Dir)) return;
	// (clicked first: a Shipping build leaves out the log, its arguments and all)
	const bool bUsed = DesignClick(Origin, Dir);
	UE_LOG(LogTemp, Display, TEXT("Design click at %.2f %.2f: %s"), FX, FY, bUsed ? TEXT("used") : TEXT("not used"));
}

void AOTGRound::EditPicked(const FString& Field, const FString& Value)
{
	if (Hud && Hud->Designer()) Hud->Designer()->Trees()->EditPicked(Field, Value);
}

void AOTGRound::SetDesignTool(int32 Tool)
{
	if (Hud && Hud->Designer()) Hud->Designer()->Trees()->SetTool(Tool);
}

void AOTGRound::SetBridgeTool(int32 Tool)
{
	if (Hud && Hud->Designer()) Hud->Designer()->BridgeTab()->SetTool(Tool);
}

void AOTGRound::EditBridge(const FString& Field, const FString& Value)
{
	if (Hud && Hud->Designer()) Hud->Designer()->BridgeTab()->EditPicked(Field, Value);
}

void AOTGRound::AddBridgeAt(double AX, double AZ, double BX, double BZ)
{
	if (Hud && Hud->Designer()) Hud->Designer()->BridgeTab()->AddAt(AX, AZ, BX, BZ);
}

void AOTGRound::WeatherChanged()
{
	Changing.WxBlend = 1.f;   // (the player's change: any change under way stops)
	Scene.ApplyWeatherToScene();
	HudView.UpdateHudInfo();
	HudView.UpdateWindBox();
}

void AOTGRound::SetWeatherKey(const FString& Key)
{
	Setup_.Weather = Key;
	SetupWeather();   // (live: fetched again)
	WeatherChanged();
}

void AOTGRound::SetWindMph(double Mph)
{
	Weather.GustMph = Mph + FMath::Max(0.0, Weather.GustMph - Weather.WindMph);
	Weather.WindMph = Mph;
	WeatherChanged();
}

// The wind's direction against the hole (0: into you from the green, 90: from the right, 180: behind)
void AOTGRound::SetWindFrom(double RelDeg)
{
	Weather.bRelative = true;
	for (double& R : WindRel) R = RelDeg;
	WeatherChanged();
}

void AOTGRound::SetCloud(double Cloud) { Weather.Cloud = FMath::Clamp(Cloud, 0.0, 1.0); WeatherChanged(); }
void AOTGRound::SetRain(double Mm) { Weather.RainMm = FMath::Max(0.0, Mm); WeatherChanged(); }
void AOTGRound::SetFog(double Fog) { Weather.Fog = FMath::Clamp(Fog, 0.0, 1.0); WeatherChanged(); }
void AOTGRound::SetTemp(double C) { Weather.TempC = C; WeatherChanged(); }

void AOTGRound::SetSunHour(double Hour)
{
	SunHour = Hour < 0 ? -1.0 : FMath::Clamp(Hour, 5.5, 19.5);
	Scene.ApplyLook();
	WeatherChanged();   // (the weather's dimming works from the look's light)
}

// Where the sun is at a time of day (local sun time, hours) for the course's latitude, on a
// midsummer day (golf's season, and daylight from dawn to dusk whenever you play):
// its height above the horizon and its bearing from north, degrees (the usual formulas: the sun's
// declination for the day of the year, the hour angle from noon)
void AOTGRound::SunAt(double Hour, double& ElevDeg, double& AzDeg) const
{
	const double Lat = FMath::DegreesToRadians(Data ? Data->Latitude : 40.0);
	const int32 Day = 172;   // (21 June)
	const double Dec = FMath::DegreesToRadians(23.44 * FMath::Sin(FMath::DegreesToRadians(360.0 / 365.0 * (284 + Day))));
	const double H = FMath::DegreesToRadians(15.0 * (Hour - 12.0));
	const double SinE = FMath::Sin(Lat) * FMath::Sin(Dec) + FMath::Cos(Lat) * FMath::Cos(Dec) * FMath::Cos(H);
	ElevDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(SinE, -1.0, 1.0)));
	const double Az = FMath::Atan2(FMath::Sin(H), FMath::Cos(H) * FMath::Sin(Lat) - FMath::Tan(Dec) * FMath::Cos(Lat));
	AzDeg = FMath::Fmod(FMath::RadiansToDegrees(Az) + 180.0 + 360.0, 360.0);
}

double AOTGRound::WindFromRelative() const
{
	double Mps, FromDeg;
	HoleWind(Mps, FromDeg);
	const FOTGHole& H = Hole();
	const otg::V3 T = TeeOf(H, TeeName);
	const double Ref = FMath::RadiansToDegrees(FMath::Atan2(H.GreenCenter.X - T.x, H.GreenCenter.Z - T.z));
	return FMath::Fmod(FromDeg - Ref + 720.0, 360.0);
}

// ------------------------------------------------------------------ a ball into the water
// Tidewright's water takes nothing thrown into it (its ripples are its own simulation's), so the
// game draws it: a crown of spray for half a second, then three rings spreading over the surface and
// fading, as a stone dropped in a pond. M_Rain (see-through, unlit: UV.y its opacity, the soft edges
// across UV.x), whiter. OTG.Look.Ripple splashes in front of the camera (testing).
void FOTGRoundWaterFx::AddRipple(const FVector& WorldAt)
{
	FVector P = WorldAt;
	for (TActorIterator<AOTGRuntimeCourse> It(Round->GetWorld()); It; ++It)
	{
		const double Top = It->WaterTopAt(P);
		if (Top > -1e9) P.Z = Top + 2.0;
		break;
	}
	Ripples.Add({ P, 0.f });
}

// Rain on the water: where the drops land, small rings - a couple of centimetres growing to a
// hand's width in under a second, one and a fainter one inside it - scattered over the water ahead
// of the camera out to 35 m, as many as the rain is hard (drizzle a few a second on a pond, heavy
// rain dozens). Only on open water (AOTGRuntimeCourse::OpenWaterAt), at most RainRingMax at once;
// further off a ring's line is drawn wider, so it doesn't break up into flicker.
namespace
{
	constexpr float RainRingLife = 0.75f;
	constexpr int32 RainRingMax = 400;
}

void FOTGRoundWaterFx::SpawnRainRings(float Dt)
{
	const double Mm = Round->Weather.RainMm;
	APlayerController* PC = Round->GetWorld() ? Round->GetWorld()->GetFirstPlayerController() : nullptr;
	if (Mm <= 0.05 || !PC || !PC->PlayerCameraManager) { RainRingCarry = 0; return; }
	AOTGRuntimeCourse* Built = nullptr;
	for (TActorIterator<AOTGRuntimeCourse> It(Round->GetWorld()); It; ++It) { Built = *It; break; }
	if (!Built) return;
	const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
	const double Yaw = PC->PlayerCameraManager->GetCameraRotation().Yaw;
	// (spots tried a second: 300 a mm of rain an hour, most of them on dry ground)
	RainRingCarry += Dt * float(FMath::Min(Mm, 10.0) * 300.0);
	int32 Tries = FMath::FloorToInt(RainRingCarry);
	RainRingCarry -= Tries;
	Tries = FMath::Min(Tries, 60);
	for (int32 n = 0; n < Tries && RainRings.Num() < RainRingMax; n++)
	{
		// (even over the ground: a sector 50 degrees either side of the view, 2 to 35 m out)
		const double Dist = FMath::Sqrt(FMath::FRandRange(0.0033, 1.0)) * 3500.0;
		const double A = FMath::DegreesToRadians(Yaw + FMath::FRandRange(-50.0, 50.0));
		const FVector At = Eye + FVector(FMath::Cos(A), FMath::Sin(A), 0) * Dist;
		const double Top = Built->OpenWaterAt(At);
		if (Top < -1e9 || Top > Eye.Z) continue;
		FOTGRoundWaterFx::FRainRing R;
		R.P = FVector(At.X, At.Y, Top + 1.0);
		R.Size = FMath::FRandRange(0.7f, 1.3f);
		R.Width = float(FMath::Max(0.6, FVector::Dist(Eye, R.P) * 0.0012));
		RainRings.Add(R);
	}
}

void FOTGRoundWaterFx::UpdateRipples(float Dt)
{
	SpawnRainRings(Dt);
	if (!Ripples.Num() && !RainRings.Num() && !RippleMesh) return;
	if (!RippleMesh)
	{
		RippleMesh = NewObject<UProceduralMeshComponent>(Round);
		RippleMesh->SetupAttachment(Round->RootComponent);
		RippleMesh->SetUsingAbsoluteLocation(true);
		RippleMesh->SetUsingAbsoluteRotation(true);
		RippleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RippleMesh->SetCastShadow(false);
		RippleMesh->bVisibleInRayTracing = false;
		RippleMesh->RegisterComponent();
		RippleMesh->SetWorldLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/OpenTeeGolf/Game/M_Rain.M_Rain")))
		{
			RippleMat = UMaterialInstanceDynamic::Create(M, Round);
			RippleMat->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.7f, 0.76f, 0.8f, 1));
			RippleMat->SetScalarParameterValue(TEXT("Opacity"), 0.3f);
			// (the rain's rings, their own: much darker - unlit, in the dim light of a rainy day the
			// camera's exposure made them chalk-white whatever their opacity)
			RainRingMat = UMaterialInstanceDynamic::Create(M, Round);
			RainRingMat->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.16f, 0.18f, 0.2f, 1));
			RainRingMat->SetScalarParameterValue(TEXT("Opacity"), 0.3f);
		}
	}
	constexpr float Life = 4.f, SprayTime = 0.5f;
	TArray<FVector> V; TArray<int32> T; TArray<FVector2D> UV;
	auto Quad = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& D, float Fade) {
		const int32 I = V.Num();
		V.Append({ A, B, C, D });
		UV.Append({ FVector2D(0, Fade), FVector2D(1, Fade), FVector2D(0, Fade), FVector2D(1, Fade) });
		T.Append({ I, I + 2, I + 1, I + 1, I + 2, I + 3 });
	};
	for (int32 i = Ripples.Num() - 1; i >= 0; i--)
	{
		FOTGRoundWaterFx::FRipple& R = Ripples[i];
		R.Age += Dt;
		if (R.Age > Life) { Ripples.RemoveAtSwap(i); continue; }
		// the rings: each a band 12 cm wide, spreading at 70 cm/s, a quarter second after the last
		for (int32 k = 0; k < 3; k++)
		{
			const float A = R.Age - k * 0.3f;
			if (A <= 0) continue;
			const double Rad = 12.0 + A * 70.0, W = 3.0 + A * 2.0;
			const float Fade = FMath::Clamp(1.f - A / (Life - k * 0.3f), 0.f, 1.f) * (k == 0 ? 1.f : 0.7f) * FMath::Clamp(A * 4.f, 0.f, 1.f);
			constexpr int32 Seg = 48;
			for (int32 s = 0; s < Seg; s++)
			{
				const double a0 = 2 * PI * s / Seg, a1 = 2 * PI * (s + 1) / Seg;
				const FVector D0(FMath::Cos(a0), FMath::Sin(a0), 0), D1(FMath::Cos(a1), FMath::Sin(a1), 0);
				// (UV.x across the band: 0 inside, 1 outside, its middle the brightest)
				Quad(R.P + D0 * (Rad - W), R.P + D0 * (Rad + W), R.P + D1 * (Rad - W), R.P + D1 * (Rad + W), Fade);
			}
		}
		// the spray: a crown of streaks up and out, falling back
		if (R.Age < SprayTime && Round->Camera.Cam)
		{
			const float K = R.Age / SprayTime;
			const FVector ToCam = (Round->Camera.CamPos - R.P).GetSafeNormal();
			for (int32 k = 0; k < 10; k++)
			{
				const FVector Dir(FMath::Cos(k * 0.63 + i), FMath::Sin(k * 0.63 + i), 0);
				const double Up = 45.0 * FMath::Sin(PI * FMath::Min(1.f, K * 1.2f)), Out = 25.0 * K;
				const FVector A = R.P, B = R.P + Dir * Out + FVector(0, 0, Up);
				const FVector Side = FVector::CrossProduct((B - A).GetSafeNormal(), ToCam).GetSafeNormal() * 1.5;
				Quad(B - Side, B + Side, A - Side, A + Side, 1.f - K);
			}
		}
	}
	// (the ball's: section 0)
	if (V.Num())
	{
		RippleMesh->CreateMeshSection_LinearColor(0, V, T, TArray<FVector>(), UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		if (RippleMat) RippleMesh->SetMaterial(0, RippleMat);
	}
	else if (RippleMesh->GetNumSections() > 0) RippleMesh->ClearMeshSection(0);
	V.Reset(); T.Reset(); UV.Reset();
	// the rain's rings (section 1): an outer ring and, a moment behind it, a fainter one inside
	for (int32 i = RainRings.Num() - 1; i >= 0; i--)
	{
		FOTGRoundWaterFx::FRainRing& R = RainRings[i];
		R.Age += Dt;
		if (R.Age > RainRingLife) { RainRings.RemoveAtSwap(i); continue; }
		for (int32 k = 0; k < 2; k++)
		{
			const float A = R.Age - k * 0.12f;
			if (A <= 0) continue;
			const double Rad = (2.0 + A * 32.0) * R.Size, W = R.Width * (1.0 + A * 1.5);
			const float Fade = FMath::Clamp(1.f - A / (RainRingLife - k * 0.12f), 0.f, 1.f) * FMath::Clamp(A * 12.f, 0.f, 1.f) * (k == 0 ? 0.7f : 0.4f);
			constexpr int32 Seg = 16;
			for (int32 s = 0; s < Seg; s++)
			{
				const double a0 = 2 * PI * s / Seg, a1 = 2 * PI * (s + 1) / Seg;
				const FVector D0(FMath::Cos(a0), FMath::Sin(a0), 0), D1(FMath::Cos(a1), FMath::Sin(a1), 0);
				Quad(R.P + D0 * FMath::Max(0.0, Rad - W), R.P + D0 * (Rad + W), R.P + D1 * FMath::Max(0.0, Rad - W), R.P + D1 * (Rad + W), Fade);
			}
		}
	}
	if (V.Num())
	{
		RippleMesh->CreateMeshSection_LinearColor(1, V, T, TArray<FVector>(), UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		if (RainRingMat) RippleMesh->SetMaterial(1, RainRingMat);
	}
	else if (RippleMesh->GetNumSections() > 1) RippleMesh->ClearMeshSection(1);
}

namespace
{
	FAutoConsoleCommandWithWorldAndArgs RippleCmd(TEXT("OTG.Look.Ripple"), TEXT("(testing) a ball's splash into the water 15 m in front of the camera"), FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W) {
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!PC || !PC->PlayerCameraManager) return;
		for (TActorIterator<AOTGRound> It(W); It; ++It)
		{
			const FVector C = PC->PlayerCameraManager->GetCameraLocation(), F = PC->PlayerCameraManager->GetCameraRotation().Vector().GetSafeNormal2D();
			It->AddRipple(C + F * 1500.0);
			return;
		}
	}));
}
