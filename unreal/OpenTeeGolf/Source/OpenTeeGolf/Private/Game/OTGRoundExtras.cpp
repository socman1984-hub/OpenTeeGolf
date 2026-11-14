// AOTGRound (see OTGRound.h), continued: the free view, the flag blowing in the wind, and the
// weather changed during the round (the weather panel).

#include "Game/OTGRound.h"

#include "Camera/CameraActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "OTGCourseData.h"
#include "ProceduralMeshComponent.h"
#include "SOTGHud.h"

// ------------------------------------------------------------------ the free view

void AOTGRound::ToggleFreeView()
{
	bFreeView = !bFreeView;
	if (bFreeView)
	{
		// (from where the camera is, looking where it looks)
		FreePos = CamPos;
		FreeRot = (CamLookNow - CamPos).Rotation();
		FreeRot.Roll = 0;
		Toast(TEXT("Free view: W A S D to fly, Q / E down and up, hold the right mouse button to look, Shift faster, F to come back"), 5.f);
	}
	else
	{
		AddressView(false);
		CamPos = FreePos; // (and glide back from there)
	}
}

// Move (forward, right, up: -1..1) and look (degrees) this frame
void AOTGRound::FreeFly(float Dt, const FVector& Move, const FVector2D& Look, bool bFast)
{
	if (!bFreeView) return;
	FreeRot.Yaw += Look.X;
	FreeRot.Pitch = FMath::Clamp(FreeRot.Pitch + Look.Y, -85.0, 85.0);
	const FRotationMatrix R(FreeRot);
	// (faster the higher it is: a metre a second near the ground, tens of metres up high)
	const FVector C = Data->WorldToCourse(FreePos);
	const double G = Course.HeightAt(C.X, C.Z);
	const double Above = FMath::IsFinite(G) ? FMath::Max(1.0, (FreePos.Z - Data->CourseToWorld(FVector(C.X, G, C.Z)).Z) / 100.0) : 30.0;
	const double Speed = FMath::Clamp(Above * 120.0, 400.0, 9000.0) * (bFast ? 3.0 : 1.0);
	FreePos += (R.GetUnitAxis(EAxis::X) * Move.X + R.GetUnitAxis(EAxis::Y) * Move.Y + FVector::UpVector * Move.Z) * Speed * Dt;
	if (FMath::IsFinite(G))
	{
		const double MinZ = Data->CourseToWorld(FVector(C.X, G, C.Z)).Z + 40;
		if (FreePos.Z < MinZ) FreePos.Z = MinZ;
	}
	if (Cam) Cam->SetActorLocationAndRotation(FreePos, FreeRot);
}

// ------------------------------------------------------------------ the flag

// A cloth flag on the pole: hanging down in calm air, flying out downwind and rippling more the
// harder it blows (and a little with each gust)
void AOTGRound::UpdateFlag(float Dt)
{
	if (!FlagCloth || !Data) return;
	FlagTime += Dt;
	double Mps, FromDeg;
	HoleWind(Mps, FromDeg);
	const double Mph = Mps / 0.44704;
	// (gusting: the strength wanders a little)
	const double Gust = Weather.GustMph > Weather.WindMph ? (Weather.GustMph - Weather.WindMph) * 0.5 * (0.5 + 0.5 * FMath::Sin(FlagTime * 0.7) * FMath::Sin(FlagTime * 1.9)) : 0.0;
	const double S = FMath::Clamp((Mph + Gust) / 16.0, 0.0, 1.0);
	const double F = FMath::DegreesToRadians(FromDeg);
	// downwind, in the level (course (-sin, -cos) is world (-sin, +cos)); in calm air any way
	const FVector Down = Mps > 0.05 ? FVector(-FMath::Sin(F), FMath::Cos(F), 0) : FVector(1, 0, 0);
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Down);
	const double Droop = FMath::DegreesToRadians(FMath::Lerp(82.0, 12.0, S));
	const FVector Along = Down * FMath::Cos(Droop) - FVector::UpVector * FMath::Sin(Droop);
	const FVector Hang = FVector::CrossProduct(Side, Along).GetSafeNormal(); // (the flag's own "down": along the pole when it flies)
	const FVector Top = World(Pin()) + FVector(0, 0, 228);
	const double L = 55, H = 36;   // cm
	const int32 NU = 12, NV = 6;
	const double Amp = FMath::Lerp(1.5, 10.0, S), Freq = FMath::Lerp(2.0, 10.0, S);
	TArray<FVector> V;
	TArray<FVector2D> UV;
	V.Reserve((NU + 1) * (NV + 1));
	for (int32 j = 0; j <= NV; j++)
		for (int32 i = 0; i <= NU; i++)
		{
			const double U = double(i) / NU, W = double(j) / NV;
			// ripples travelling out from the pole, growing toward the free end
			// (and the free end flutters: a quicker, smaller ripple near it)
			const double Wave = FMath::Sin(U * 7.0 - FlagTime * Freq + W * 0.8) * Amp * U + FMath::Sin(U * 13.0 - FlagTime * Freq * 1.7) * Amp * 0.3 * U
				+ FMath::Sin(U * 21.0 - FlagTime * Freq * 2.6 + W * 2.0) * Amp * 0.25 * U * U * S;
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
			Tri.Append({ A, C, B, B, C, D });   // (one face...)
			Tri.Append({ A, B, C, B, D, C });   // (...and the other)
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
	if (!bFlagMade)
	{
		FlagCloth->CreateMeshSection_LinearColor(0, V, Tri, N, UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		FlagCloth->SetMaterial(0, FlagMat);
		bFlagMade = true;
	}
	else FlagCloth->UpdateMeshSection_LinearColor(0, V, N, UV, TArray<FLinearColor>(), TArray<FProcMeshTangent>());
}

// ------------------------------------------------------------------ the weather, changed in the round

void AOTGRound::ToggleWeatherPanel()
{
	if (Hud) Hud->ShowWeatherPanel(!Hud->IsWeatherPanelShown());
}

void AOTGRound::WeatherChanged()
{
	ApplyWeatherToScene();
	UpdateHudInfo();
	UpdateWindBox();
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
	ApplyLook();
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
