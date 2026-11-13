// See OTGMevoWire.h (lib/mevo.js, "wire format" and "shot messages").

#include "OTGMevoWire.h"

namespace otg::mevo
{
	int32 ModeNumber(const FString& Mode)
	{
		if (Mode == TEXT("chipping")) return 5;
		if (Mode == TEXT("putting")) return 3;
		if (Mode == TEXT("outdoor")) return 9;
		return 1;
	}

	namespace
	{
		uint8 EscapeOf(uint8 B)
		{
			switch (B) { case 0xf0: return 0x01; case 0xf1: return 0x02; case 0xfd: return 0x03; case 0xfa: return 0x04; default: return 0; }
		}
		int32 Unescape(uint8 B)
		{
			switch (B) { case 0x01: return 0xf0; case 0x02: return 0xf1; case 0x03: return 0xfd; case 0x04: return 0xfa; default: return -1; }
		}
		void Stuff(const TArray<uint8>& In, TArray<uint8>& Out)
		{
			for (uint8 B : In)
			{
				if (const uint8 E = EscapeOf(B)) { Out.Add(0xfd); Out.Add(E); }
				else Out.Add(B);
			}
		}
	}

	TArray<uint8> EncodeFrame(uint8 Dest, uint8 Type, const TArray<uint8>& Payload, uint8 Src)
	{
		TArray<uint8> Raw = { Dest, Src, Type };
		Raw.Append(Payload);
		TArray<uint8> Data;
		Stuff(Raw, Data);
		uint32 Cs = 0;
		for (uint8 B : Data) Cs = (Cs + B) & 0xffff;
		TArray<uint8> Out = { 0xf0 };
		Out.Append(Data);
		Stuff({ uint8(Cs >> 8), uint8(Cs & 0xff) }, Out);
		Out.Add(0xf1);
		return Out;
	}

	bool ParseFrame(const uint8* Wire, int32 N, FMevoFrame& Out)
	{
		if (N < 7 || Wire[0] != 0xf0 || Wire[N - 1] != 0xf1) return false;
		const uint8* Inner = Wire + 1;
		const int32 Len = N - 2;
		TArray<uint8> Vals;
		TArray<int32> Offs;
		for (int32 i = 0; i < Len; i++)
		{
			if (Inner[i] == 0xfd)
			{
				const int32 V = i + 1 < Len ? Unescape(Inner[i + 1]) : -1;
				if (V < 0) return false;
				Vals.Add(uint8(V)); Offs.Add(i); i++;
			}
			else { Vals.Add(Inner[i]); Offs.Add(i); }
		}
		const int32 Num = Vals.Num();
		if (Num < 5) return false;
		uint32 Cs = 0;
		for (int32 i = 0; i < Offs[Num - 2]; i++) Cs = (Cs + Inner[i]) & 0xffff;
		if (Cs != ((uint32(Vals[Num - 2]) << 8) | Vals[Num - 1])) return false;
		Out.Dest = Vals[0]; Out.Src = Vals[1]; Out.Type = Vals[2];
		Out.Payload = TArray<uint8>(Vals.GetData() + 3, Num - 5);
		return true;
	}

	void FFrameReader::Push(const uint8* Data, int32 N, TArray<FMevoFrame>& Out)
	{
		Buf.Append(Data, N);
		for (;;)
		{
			int32 S = Buf.Find(0xf0);
			if (S == INDEX_NONE) { Buf.Reset(); break; }
			int32 E = INDEX_NONE;
			for (int32 i = S + 1; i < Buf.Num(); i++) if (Buf[i] == 0xf1) { E = i; break; }
			if (E == INDEX_NONE) { Buf.RemoveAt(0, S); break; }
			FMevoFrame F;
			if (ParseFrame(Buf.GetData() + S, E - S + 1, F)) Out.Add(MoveTemp(F));
			Buf.RemoveAt(0, E + 1);
		}
	}

	int32 Int16(const TArray<uint8>& P, int32 O)
	{
		const int32 V = (int32(P[O]) << 8) | P[O + 1];
		return V >= 0x8000 ? V - 0x10000 : V;
	}
	int32 Int24(const TArray<uint8>& P, int32 O)
	{
		const int32 V = (int32(P[O]) << 16) | (int32(P[O + 1]) << 8) | P[O + 2];
		return V >= 0x800000 ? V - 0x1000000 : V;
	}
	double ReadFloat40(const TArray<uint8>& P, int32 O)
	{
		const int32 M = Int24(P, O + 2);
		return M == 0 ? 0.0 : M * FMath::Pow(2.0, double(Int16(P, O) - 23));
	}
	TArray<uint8> WriteFloat40(double V)
	{
		if (V == 0 || !FMath::IsFinite(V)) return { 0, 0, 0, 0, 0 };
		int32 E = FMath::FloorToInt(FMath::Log2(FMath::Abs(V))) + 1; // frexp: |v| = f * 2^e, f in [0.5, 1)
		double F = FMath::Abs(V) / FMath::Pow(2.0, double(E));
		if (F >= 1) { F /= 2; E++; }
		else if (F < 0.5) { F *= 2; E--; }
		int32 M = FMath::FloorToInt(F * 8388608.0);
		if (V < 0) M = -M;
		const uint32 Mu = uint32(M) & 0xffffff, Eu = uint32(E) & 0xffff;
		return { uint8(Eu >> 8), uint8(Eu & 0xff), uint8((Mu >> 16) & 0xff), uint8((Mu >> 8) & 0xff), uint8(Mu & 0xff) };
	}
	FString Text(const TArray<uint8>& P)
	{
		int32 N = P.Num();
		while (N > 0 && P[N - 1] == 0) N--;
		FString S;
		for (int32 i = 0; i < N; i++)
			if ((P[i] >= 0x20 && P[i] <= 0x7e) || P[i] == '\n') S.AppendChar(TCHAR(P[i]));
		return S.TrimStartAndEnd();
	}

	bool DecodeFlight(const TArray<uint8>& P, FFlight& Out)
	{
		if (P.Num() < 49) return false;
		auto K = [&](int32 O) { return Int24(P, O) / 1000.0; };
		Out.Shot = Int24(P, 1);
		Out.LaunchSpeed = K(16); Out.Azimuth = K(19); Out.Elevation = K(22); Out.Carry = K(25); Out.FlightTime = K(28); Out.MaxHeight = K(31);
		Out.Backspin = Int24(P, 43); Out.Sidespin = Int24(P, 46);
		Out.ClubSpeed = P.Num() >= 88 ? K(85) : NAN;
		return true;
	}
	bool DecodeClub(const TArray<uint8>& P, FClub& Out)
	{
		if (P.Num() < 26) return false;
		auto C = [&](int32 O) { return Int24(P, O) / 100.0; };
		Out.Speed = C(5); Out.SpeedPost = C(8); Out.Path = -C(11); Out.Attack = C(14); Out.Face = -C(17); Out.Loft = C(20);
		Out.Smash = Int24(P, 23) / 1000.0;
		return true;
	}
	bool DecodeSpin(const TArray<uint8>& P, FSpin& Out)
	{
		if (P.Num() < 134) return false;
		Out.Total = Int16(P, 108);
		Out.Axis = -Int16(P, 132) / 10.0;
		return true;
	}

	FOTGBallData ToBallData(const FFlight& Flight, const FSpin* Spin, const FClub* Club)
	{
		double Back = Flight.Backspin, Side = -Flight.Sidespin;
		// The first shot of a session can carry uninitialised spin in 0xD4; use 0xEF then
		const bool bBad = FMath::Abs(Back) > 16000 || FMath::Abs(Side) > 9000 || (Back == 0 && Side == 0);
		if (bBad && Spin && Spin->Total > 0)
		{
			const double A = FMath::DegreesToRadians(Spin->Axis);
			Back = FMath::RoundToDouble(Spin->Total * FMath::Cos(A));
			Side = FMath::RoundToDouble(Spin->Total * FMath::Sin(A));
		}
		const double Total = FMath::Sqrt(Back * Back + Side * Side);
		FOTGBallData B;
		B.Speed = Flight.LaunchSpeed * 2.23694;
		B.VLA = Flight.Elevation;
		B.HLA = -Flight.Azimuth;
		B.BackSpin = Back; B.SideSpin = Side; B.TotalSpin = Total;
		B.SpinAxis = Total ? FMath::RadiansToDegrees(FMath::Atan2(Side, Back)) : 0.0;
		B.CarryYd = Flight.Carry * 1.09361;
		if (Club)
		{
			B.Club.bValid = true;
			B.Club.SpeedMph = Club->Speed * 2.23694;
			B.Club.AngleOfAttack = Club->Attack;
			B.Club.FaceToTarget = Club->Face;
			B.Club.Path = Club->Path;
			B.Club.SmashFactor = Club->Smash;
		}
		return B;
	}
}
