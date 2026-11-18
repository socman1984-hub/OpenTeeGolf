#include "MeridianAstronomy.h"

namespace
{
	double Rad(double Deg) { return FMath::DegreesToRadians(Deg); }
	double Deg(double InRad) { return FMath::RadiansToDegrees(InRad); }
	double Wrap360(double InDeg) { return InDeg - 360.0 * FMath::FloorToDouble(InDeg / 360.0); }
	double SinD(double InDeg) { return FMath::Sin(Rad(InDeg)); }
	double CosD(double InDeg) { return FMath::Cos(Rad(InDeg)); }

	/** Equatorial coordinates to azimuth (clockwise from north) and elevation. */
	void ToHorizontal(double RightAscension, double Declination, double LocalSidereal, double Latitude, float& OutAz, float& OutEl)
	{
		const double HourAngle = Rad(LocalSidereal) - RightAscension;
		const double Lat = Rad(Latitude);
		const double SinEl = FMath::Sin(Lat) * FMath::Sin(Declination) + FMath::Cos(Lat) * FMath::Cos(Declination) * FMath::Cos(HourAngle);
		const double Az = FMath::Atan2(
			-FMath::Sin(HourAngle) * FMath::Cos(Declination),
			FMath::Sin(Declination) * FMath::Cos(Lat) - FMath::Cos(Declination) * FMath::Cos(HourAngle) * FMath::Sin(Lat));
		OutEl = (float)Deg(FMath::Asin(FMath::Clamp(SinEl, -1.0, 1.0)));
		OutAz = (float)Wrap360(Deg(Az));
	}
}

FMeridianCelestialState MeridianAstronomy::Compute(double JulianDay, double Latitude, double Longitude)
{
	FMeridianCelestialState Out;

	const double N = JulianDay - 2451545.0; // days since J2000.0
	const double T = N / 36525.0;           // Julian centuries
	const double Obliquity = Rad(23.439 - 0.0000004 * N);
	const double LocalSidereal = Wrap360((18.697374558 + 24.06570982441908 * N) * 15.0 + Longitude);
	Out.SiderealAngle = (float)LocalSidereal;

	// Sun
	const double SunMeanLon = Wrap360(280.460 + 0.9856474 * N);
	const double SunAnomaly = Wrap360(357.528 + 0.9856003 * N);
	const double SunLon = Wrap360(SunMeanLon + 1.915 * SinD(SunAnomaly) + 0.020 * SinD(2.0 * SunAnomaly));
	{
		const double L = Rad(SunLon);
		const double RA = FMath::Atan2(FMath::Cos(Obliquity) * FMath::Sin(L), FMath::Cos(L));
		const double Dec = FMath::Asin(FMath::Sin(Obliquity) * FMath::Sin(L));
		ToHorizontal(RA, Dec, LocalSidereal, Latitude, Out.SunAzimuth, Out.SunElevation);
	}

	// Moon
	const double MoonLon = Wrap360(218.32 + 481267.881 * T
		+ 6.29 * SinD(135.0 + 477198.87 * T)
		- 1.27 * SinD(259.3 - 413335.36 * T)
		+ 0.66 * SinD(235.7 + 890534.22 * T)
		+ 0.21 * SinD(269.9 + 954397.74 * T)
		- 0.19 * SinD(357.5 + 35999.05 * T)
		- 0.11 * SinD(186.5 + 966404.03 * T));
	const double MoonLat = 5.13 * SinD(93.3 + 483202.02 * T)
		+ 0.28 * SinD(228.2 + 960400.89 * T)
		- 0.28 * SinD(318.3 + 6003.15 * T)
		- 0.17 * SinD(217.6 - 407332.21 * T);
	{
		const double L = Rad(MoonLon);
		const double B = Rad(MoonLat);
		const double RA = FMath::Atan2(
			FMath::Sin(L) * FMath::Cos(Obliquity) - FMath::Tan(B) * FMath::Sin(Obliquity),
			FMath::Cos(L));
		const double Dec = FMath::Asin(FMath::Clamp(
			FMath::Sin(B) * FMath::Cos(Obliquity) + FMath::Cos(B) * FMath::Sin(Obliquity) * FMath::Sin(L), -1.0, 1.0));
		ToHorizontal(RA, Dec, LocalSidereal, Latitude, Out.MoonAzimuth, Out.MoonElevation);
	}

	const double CosElongation = CosD(MoonLat) * CosD(MoonLon - SunLon);
	Out.MoonIllumination = (float)FMath::Clamp((1.0 - CosElongation) * 0.5, 0.0, 1.0);
	Out.MoonPhase = (float)(Wrap360(MoonLon - SunLon) / 360.0);
	return Out;
}

FVector MeridianAstronomy::DirectionFromAzimuthElevation(float AzimuthDeg, float ElevationDeg, float NorthYawDeg)
{
	const float Az = FMath::DegreesToRadians(AzimuthDeg + NorthYawDeg);
	const float El = FMath::DegreesToRadians(ElevationDeg);
	return FVector(FMath::Cos(El) * FMath::Cos(Az), FMath::Cos(El) * FMath::Sin(Az), FMath::Sin(El));
}

void MeridianAstronomy::AzimuthElevationFromDirection(const FVector& Direction, float NorthYawDeg, float& OutAzimuthDeg, float& OutElevationDeg)
{
	const FVector D = Direction.GetSafeNormal();
	OutElevationDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp((float)D.Z, -1.f, 1.f)));
	OutAzimuthDeg = (float)Wrap360(FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)) - NorthYawDeg);
}
