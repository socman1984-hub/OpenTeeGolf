// See OTGLaunchMonitorTypes.h.

#include "LaunchMonitor/OTGLaunchMonitorTypes.h"

namespace
{
	bool Finite(const TOptional<double>& V) { return V.IsSet() && FMath::IsFinite(*V); }

	// Unreadable or impossible numbers are clamped, never passed on (0 when missing)
	double Lim(double V, double Lo, double Hi) { return FMath::IsFinite(V) ? FMath::Clamp(V, Lo, Hi) : 0.0; }
	double Lim(const TOptional<double>& V, double Lo, double Hi) { return V.IsSet() ? Lim(*V, Lo, Hi) : 0.0; }
}

FOTGShot OTGToShot(const FOTGBallData& B, const FString& Source, const FString& Device, int32 ShotNumber)
{
	// normalizeSpin (physics.js): back and side spin from total and axis when they're missing
	double Back = Finite(B.BackSpin) ? *B.BackSpin : 0, Side = Finite(B.SideSpin) ? *B.SideSpin : 0;
	const bool bNoBack = !B.BackSpin.IsSet() || (*B.BackSpin == 0 && B.SideSpin.IsSet() && *B.SideSpin == 0);
	if (bNoBack && B.TotalSpin.IsSet())
	{
		const double Ax = FMath::DegreesToRadians(Finite(B.SpinAxis) ? *B.SpinAxis : 0.0);
		Back = *B.TotalSpin * FMath::Cos(Ax);
		Side = *B.TotalSpin * FMath::Sin(Ax);
	}
	FOTGShot S;
	S.Source = Source;
	S.Device = Device.IsEmpty() ? TEXT("unknown") : Device;
	S.ShotNumber = ShotNumber;
	S.SpeedMph = Lim(B.Speed, 0, 240);
	S.VLA = Lim(B.VLA, -20, 85);
	S.HLA = Lim(B.HLA, -60, 60);
	S.BackSpin = Lim(Back, -4000, 15000);
	S.SideSpin = Lim(Side, -8000, 8000);
	S.TotalSpin = Finite(B.TotalSpin) && FMath::Abs(*B.TotalSpin) <= 16000 ? *B.TotalSpin : FMath::Sqrt(S.BackSpin * S.BackSpin + S.SideSpin * S.SideSpin);
	S.SpinAxis = Finite(B.SpinAxis) ? FMath::Clamp(*B.SpinAxis, -90.0, 90.0) : FMath::RadiansToDegrees(FMath::Atan2(S.SideSpin, S.BackSpin));
	S.LmCarryYd = Finite(B.CarryYd) ? *B.CarryYd : -1;
	S.Club = B.Club;
	S.At = FDateTime::UtcNow();
	return S;
}

FString OTGShotLine(const FOTGShot& S)
{
	return FString::Printf(TEXT("%.1f mph, VLA %.1f, HLA %.1f, spin %d rpm, axis %.1f"), S.SpeedMph, S.VLA, S.HLA, FMath::RoundToInt(S.TotalSpin), S.SpinAxis);
}
