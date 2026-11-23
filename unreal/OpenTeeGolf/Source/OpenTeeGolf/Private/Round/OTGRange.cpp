// See OTGRange.h.

#include "Round/OTGRange.h"

#include "Course/OTGCourseData.h"

#include "Async/ParallelFor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGRange, Log, All);

const TCHAR* const otg::range::Id = TEXT("OpenTeeGolf_Driving_Range");

namespace OTGRangeGen
{
	// (written again when this changes: a new layout, or files the game reads differently)
	constexpr int32 Version = 3;
	constexpr double YD = 0.9144;
	constexpr double Base = 2.0;      // the range's ground (metres)
	constexpr double End = 330.0;     // the bank at the far end
	// the land: a square LandN samples a side, a metre apart, its north-west corner at X0, Z1
	constexpr int32 LandN = 1009;
	constexpr double Step = 1.0, X0 = -504.0, Z1 = 600.0;
	// where the ball can go (outside: out)
	constexpr double MinX = -260, MaxX = 260, MinZ = -110, MaxZ = 470;
	constexpr double PlayRes = 0.5;
	constexpr int32 MaskSize = 2048;   // (a power of two about twice the samples: the export's rule)
	constexpr double SdfM = 3.0;       // (the masks: 0.5 on the outline, 0 and 1 this far out and in)

	double Smooth(double A, double B, double X) { const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0); return T * T * (3 - 2 * T); }
	double Hash(int32 I, int32 J, int32 K)
	{
		uint32 H = uint32(I) * 374761393u + uint32(J) * 668265263u + uint32(K) * 2246822519u;
		H = (H ^ (H >> 13)) * 1274126177u;
		H ^= H >> 16;
		return double(H & 0xFFFFFF) / double(0xFFFFFF);
	}
	// smooth value noise, -1..1
	double Noise(double X, double Z, int32 K = 0)
	{
		const int32 I = FMath::FloorToInt32(X), J = FMath::FloorToInt32(Z);
		const double FX = X - I, FZ = Z - J, SX = FX * FX * (3 - 2 * FX), SZ = FZ * FZ * (3 - 2 * FZ);
		auto V = [&](int32 A, int32 B) { return Hash(A, B, K) * 2 - 1; };
		return FMath::Lerp(FMath::Lerp(V(I, J), V(I + 1, J), SX), FMath::Lerp(V(I, J + 1), V(I + 1, J + 1), SX), SZ);
	}

	struct FBunker { double X, Z, A, B; };
	const TArray<FBunker>& Bunkers()
	{
		static const TArray<FBunker> B = [] {
			const TArray<otg::range::FTarget>& T = otg::range::Targets();
			return TArray<FBunker>{ { T[1].X + T[1].R + 4.5, T[1].Z - 2, 4.5, 2.6 }, { T[3].X - T[3].R - 5, T[3].Z + 3, 5, 3 } };
		}();
		return B;
	}

	// How far outside the mown range a spot is (metres; below 0 inside it, or by the tee)
	double Outside(double X, double Z) { return FMath::Max3(FMath::Abs(X) - otg::range::HalfWidth(Z), Z - End, -28 - Z); }
	// The shapes, as signed distances (metres, + inside)
	double TeeSd(double X, double Z) { return -FMath::Max3(FMath::Abs(X) - 32, Z - 5, -12 - Z); }
	double FairSd(double X, double Z) { return -FMath::Max3(FMath::Abs(X) - otg::range::HalfWidth(Z), 5 - Z, Z - End); }
	double GreenSd(double X, double Z, double Grow = 0)
	{
		double Best = -1e9;
		const TArray<otg::range::FTarget>& T = otg::range::Targets();
		for (int32 i = 0; i < T.Num(); i++)
		{
			const double DX = X - T[i].X, DZ = Z - T[i].Z, D = FMath::Sqrt(DX * DX + DZ * DZ);
			if (D > T[i].R * 1.3 + 12) continue;
			const double A = FMath::Atan2(DZ, DX);
			const double R = T[i].R * (1 + 0.07 * FMath::Sin(2 * A + i * 1.7) + 0.04 * FMath::Sin(3 * A + i));
			Best = FMath::Max(Best, R + Grow - D);
		}
		return Best;
	}
	double BunkerSd(double X, double Z)
	{
		double Best = -1e9;
		for (const FBunker& B : Bunkers())
		{
			const double Q = FMath::Sqrt(FMath::Square((X - B.X) / B.A) + FMath::Square((Z - B.Z) / B.B));
			Best = FMath::Max(Best, (1 - Q) * FMath::Min(B.A, B.B));
		}
		return Best;
	}
	double Field(double Sd) { return FMath::Clamp(0.5 + Sd / (2 * SdfM), 0.0, 1.0); }

	// The valley's sides beyond the mown range: steepening for the first few hundred metres, then
	// levelling off into the land round it (the mountains, Land, stand on that)
	double Valley(double Out)
	{
		static const double At300 = 0.035 * FMath::Pow(300.0, 1.3);
		return Out <= 300 ? 0.035 * FMath::Pow(FMath::Max(0.0, Out), 1.3) : At300 + 120.0 * (1 - FMath::Exp(-(Out - 300) / 1500.0));
	}

	// The ground's height (metres): flat on the tee line, the range nearly so, rising beyond it into
	// banks and the valley's sides; each target green a low mound, the bunkers sunk
	double HeightAt(double X, double Z)
	{
		double H = Base + 0.12 * Noise(X / 35, Z / 35, 1);
		// (beyond the far end a lower bank than up the sides: the mountains show over it)
		const double Out = FMath::Max3(FMath::Abs(X) - otg::range::HalfWidth(Z), (Z - End) * 0.45, -28 - Z);
		if (Out > 0) H += Valley(Out) + Smooth(0, 30, Out) * (1.4 * Noise(X / 60 + 3, Z / 60 - 2, 2) + 0.5 * Noise(X / 19, Z / 19, 3));
		for (const otg::range::FTarget& T : otg::range::Targets())
			H += 0.45 * (1 - Smooth(T.R * 0.6, T.R * 1.9, FMath::Sqrt(FMath::Square(X - T.X) + FMath::Square(Z - T.Z))));
		H -= 0.45 * Smooth(-0.5, 1.2, BunkerSd(X, Z));
		return FMath::Lerp(Base, H, Smooth(-1, 7, -TeeSd(X, Z)));
	}

	// The play data's surface (otg::Surface codes: 1 water 2 green 3 fringe 4 fairway 5 tee 6 first cut
	// 7 rough 8 deep rough 9 native ... 11 sand)
	uint8 SurfaceAt(double X, double Z)
	{
		if (TeeSd(X, Z) > 0) return 5;
		const double G = GreenSd(X, Z);
		if (G > 0) return 2;
		if (BunkerSd(X, Z) > 0) return 11;
		if (G > -1.5) return 3;
		const double F = FairSd(X, Z);
		if (F > 0) return 4;
		if (F > -2 && Z > 3 && Z < End + 2) return 6;
		const double Out = Outside(X, Z);
		return Out < 14 ? 7 : Out < 90 ? 9 : 8;
	}

	bool SavePng(const FString& File, const void* Raw, int64 Bytes, int32 W, int32 H, ERGBFormat Format, int32 Bits)
	{
		IImageWrapperModule& M = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> P = M.CreateImageWrapper(EImageFormat::PNG);
		if (!P.IsValid() || !P->SetRaw(Raw, Bytes, W, H, Format, Bits)) return false;
		return FFileHelper::SaveArrayToFile(P->GetCompressed(), *File);
	}
	bool SaveJpg(const FString& File, const TArray<FColor>& Px, int32 W, int32 H)
	{
		IImageWrapperModule& M = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> J = M.CreateImageWrapper(EImageFormat::JPEG);
		if (!J.IsValid() || !J->SetRaw(Px.GetData(), Px.Num() * sizeof(FColor), W, H, ERGBFormat::BGRA, 8)) return false;
		return FFileHelper::SaveArrayToFile(J->GetCompressed(90), *File);
	}

	FString Num(double V) { return FString::Printf(TEXT("%.3f"), V); }
	FString Pt(double X, double Y, double Z) { return FString::Printf(TEXT("{\"x\":%s,\"y\":%s,\"z\":%s}"), *Num(X), *Num(Y), *Num(Z)); }

	// The mountains (the web version's practice.js buildMountains): a flat valley floor out to a
	// couple of km, wooded foothills, then ridged snowy peaks 10 to 30 km off, highest beyond the far
	// end of the range (north, + z), lower ridges round the sides. Metres above the valley floor.
	double Ridged(double X, double Z)
	{
		double S = 0, A = 1, F = 1 / 9000.0, W = 0;
		for (int32 o = 0; o < 5; o++)
		{
			const double V = 1 - FMath::Abs(Noise(X * F + o * 17.3, Z * F - o * 9.1, 30 + o));
			S += V * V * A; W += A; A *= 0.5; F *= 2.07;
		}
		return S / W;
	}
	double Land(double X, double Z)
	{
		const double D = FMath::Sqrt(X * X + Z * Z), Ahead = Z / FMath::Max(D, 1.0);
		const double Sector = 0.3 + 0.7 * Smooth(-0.3, 0.75, Ahead);
		const double Rise = Smooth(7000, 17000, D) * (1 - 0.3 * Smooth(26000, 39000, D));
		const double Peaks = Rise > 0 ? Rise * Sector * (5400 * Ridged(X, Z) - 900) : 0;
		const double Hills = Smooth(1500, 6500, D) * (110 + 30 * Sector) * (Noise(X / 2200, Z / 2200, 21) + 0.6 * Noise(X / 700 + 4, Z / 700, 22) + 1.2);
		return FMath::Max(0.0, Peaks) + Hills;
	}

	// The land round the range as an export's farland.json has it (AOTGRuntimeCourse::BuildRealFarLand):
	// heights on a grid over a square 80 km across, closest together near the range, and pictures of it
	// - meadows and woods low down, rock above the trees, snow on the tops
	bool WriteFarLand(const FString& Dir, double HBase, FString& Why)
	{
		constexpr double Half = 40000;
		constexpr int32 Grid = 448, Side = Grid + 1;
		const double Span = (LandN - 1) * Step, Cx = -X0, Cz = Z1;   // (the range's tee, in the export's frame: x east, z south from the north-west corner)
		auto Sq = [](double F) { const double U = F * 2 - 1; return FMath::Sign(U) * U * U; };
		TArray<float> Hts;
		Hts.SetNumUninitialized(Side * Side);
		ParallelFor(Side, [&](int32 j) {
			for (int32 i = 0; i < Side; i++)
			{
				const double XE = Cx + Sq(double(i) / Grid) * Half, ZS = Cz + Sq(double(j) / Grid) * Half;
				const double X = X0 + XE, Z = Z1 - ZS;
				double H = HeightAt(X, Z) + Land(X, Z) - HBase;
				// (under the range's own ground: 4 m down, as the export puts it)
				if (XE >= 0 && ZS >= 0 && XE <= Span && ZS <= Span) H -= 4;
				Hts[j * Side + i] = float(H);
			}
		});
		if (!FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(reinterpret_cast<const uint8*>(Hts.GetData()), Hts.Num() * sizeof(float)), *FPaths::Combine(Dir, TEXT("farland_heights.bin")))) { Why = TEXT("the mountains' heights"); return false; }
		const FLinearColor Meadow(94 / 255.f, 132 / 255.f, 54 / 255.f), Forest(34 / 255.f, 64 / 255.f, 30 / 255.f), Alpine(110 / 255.f, 112 / 255.f, 80 / 255.f),
			Rock(112 / 255.f, 106 / 255.f, 100 / 255.f), Snow(238 / 255.f, 242 / 255.f, 247 / 255.f);
		const double Halves[] = { 6000, Half };
		constexpr int32 Px = 2048;
		for (int32 k = 0; k < 2; k++)
		{
			const double H = Halves[k], Res = 2 * H / Px, E = FMath::Max(Res, 40.0);
			TArray<FColor> Img;
			Img.SetNumUninitialized(Px * Px);
			ParallelFor(Px, [&](int32 j) {
				const double Z = Z1 - (Cz - H + (j + 0.5) * Res);
				for (int32 i = 0; i < Px; i++)
				{
					const double X = X0 + (Cx - H + (i + 0.5) * Res);
					const double L = Land(X, Z);
					const double Sl = FMath::Sqrt(FMath::Square(Land(X + E, Z) - Land(X - E, Z)) + FMath::Square(Land(X, Z + E) - Land(X, Z - E))) / (2 * E);
					const double N1 = Noise(X / 700, Z / 700, 41), N2 = Noise(X / 160 + 7, Z / 160, 42);
					FLinearColor C = FMath::Lerp(Meadow, Forest, float(Smooth(-0.2, 0.35, N1 + 0.3 * N2 + L / 900 - 0.15)));
					C = FMath::Lerp(C, Alpine, float(Smooth(1900, 2500, L + 250 * N1)));
					C = FMath::Lerp(C, Rock, float(FMath::Max(Smooth(0.45, 0.9, Sl), Smooth(2300, 2900, L + 200 * N2))));
					C = FMath::Lerp(C, Snow, float(Smooth(2650, 2950, L + 300 * N1 + 120 * N2) * (1 - Smooth(0.7, 1.1, Sl))));
					// (the range's own valley, near it: the rough's colours, so the ground left out round it matches)
					const double Out = Outside(X, Z);
					if (Out < 400) C = FMath::Lerp(C, FLinearColor(0.31f, 0.39f, 0.18f), float(1 - Smooth(150, 400, Out)));
					const float B = float(1 + 0.08 * N2 + (Res < 10 ? 0.07 * Noise(X / 35, Z / 35, 43) : 0));
					C = C * B;
					Img[j * Px + i] = FColor(uint8(FMath::Clamp(C.R * 255.f, 0.f, 255.f)), uint8(FMath::Clamp(C.G * 255.f, 0.f, 255.f)), uint8(FMath::Clamp(C.B * 255.f, 0.f, 255.f)), 255);
				}
			});
			if (!SaveJpg(FPaths::Combine(Dir, FString::Printf(TEXT("far_%d.jpg"), k)), Img, Px, Px)) { Why = TEXT("the mountains' pictures"); return false; }
		}
		const FString Json = FString::Printf(TEXT("{\"centre\":{\"x\":%s,\"z\":%s},\"halfMetres\":%s,\"grid\":%d,\"heights\":\"farland_heights.bin\",\"layers\":[{\"file\":\"far_0.jpg\",\"halfMetres\":6000},{\"file\":\"far_1.jpg\",\"halfMetres\":%s}]}"),
			*Num(Cx), *Num(Cz), *Num(Half), Grid, *Num(Half));
		if (!FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Dir, TEXT("farland.json")))) { Why = TEXT("farland.json"); return false; }
		return true;
	}

	struct FTreeSpot { double X, Z, H; FString Kind; };
	// Woods down both sides of the range, behind the tee and past the bank: rows a few deep,
	// staggered, never nearer the mown range than 16 m
	TArray<FTreeSpot> TreeSpots()
	{
		TArray<FTreeSpot> Out;
		int32 K = 0;
		auto Add = [&](double X, double Z) {
			K++;
			X += (Hash(K, 1, 7) - 0.5) * 6;
			Z += (Hash(K, 2, 7) - 0.5) * 6;
			if (Hash(K, 3, 7) < 0.12 || Outside(X, Z) < 16 || FMath::Abs(X) > 470 || Z < -380 || Z > 580) return;
			const double R = Hash(K, 4, 7);
			const TCHAR* Kind = R < 0.3 ? TEXT("oak") : R < 0.55 ? TEXT("maple") : R < 0.8 ? TEXT("pine") : TEXT("spruce");
			Out.Add({ X, Z, 12.0 + 9.0 * Hash(K, 5, 7), Kind });
		};
		for (int32 S : { -1, 1 })
			for (double Z = -70; Z <= End + 60; Z += 8)
				for (int32 k = 0; k < 5; k++) Add(S * (otg::range::HalfWidth(Z) + 18 + k * 9 + (k % 2) * 4), Z);
		for (double Z = -40; Z >= -100; Z -= 9)
			for (double X = -60; X <= 60; X += 9) Add(X, Z);
		for (double Z = End + 35; Z <= End + 90; Z += 9)
			for (double X = -otg::range::HalfWidth(Z) - 18; X <= otg::range::HalfWidth(Z) + 18; X += 9) Add(X, Z);
		return Out;
	}

	bool Write(const FString& Dir, FString& Why)
	{
		const double T0 = FPlatformTime::Seconds();
		IFileManager::Get().MakeDirectory(*Dir, true);
		auto File = [&](const TCHAR* Name) { return FPaths::Combine(Dir, Name); };

		// ---- the heightmap
		TArray<double> Hs;
		Hs.SetNumUninitialized(LandN * LandN);
		double Lo = 1e9, Hi = -1e9;
		for (int32 j = 0; j < LandN; j++)
			for (int32 i = 0; i < LandN; i++)
			{
				const double H = HeightAt(X0 + i * Step, Z1 - j * Step);
				Hs[j * LandN + i] = H;
				Lo = FMath::Min(Lo, H); Hi = FMath::Max(Hi, H);
			}
		const double HBase = FMath::FloorToDouble(Lo - 1), HRange = FMath::CeilToDouble(Hi - HBase + 1);
		TArray<uint16> V16;
		V16.SetNumUninitialized(LandN * LandN);
		for (int32 q = 0; q < LandN * LandN; q++) V16[q] = uint16(FMath::Clamp(FMath::RoundToInt((Hs[q] - HBase) / HRange * 65535.0), 0, 65535));
		if (!SavePng(File(TEXT("heightmap.png")), V16.GetData(), V16.Num() * sizeof(uint16), LandN, LandN, ERGBFormat::Gray, 16)) { Why = TEXT("the heightmap"); return false; }
		auto GroundAt = [&](double X, double Z) {
			const double U = FMath::Clamp((X - X0) / Step, 0.0, LandN - 1.001), V = FMath::Clamp((Z1 - Z) / Step, 0.0, LandN - 1.001);
			const int32 I = FMath::FloorToInt32(U), J = FMath::FloorToInt32(V);
			const double FU = U - I, FV = V - J;
			auto H = [&](int32 A, int32 B) { return HBase + V16[B * LandN + A] * HRange / 65535.0; };
			return FMath::Lerp(FMath::Lerp(H(I, J), H(I + 1, J), FU), FMath::Lerp(H(I, J + 1), H(I + 1, J + 1), FU), FV);
		};

		// ---- the play data's surfaces
		const int32 SW = FMath::CeilToInt32((MaxX - MinX) / PlayRes), SH = FMath::CeilToInt32((MaxZ - MinZ) / PlayRes);
		TArray<uint8> Codes;
		Codes.SetNumUninitialized(SW * SH);
		for (int32 j = 0; j < SH; j++)
			for (int32 i = 0; i < SW; i++) Codes[j * SW + i] = SurfaceAt(MinX + (i + 0.5) * PlayRes, MaxZ - (j + 0.5) * PlayRes);
		if (!SavePng(File(TEXT("surfaces.png")), Codes.GetData(), Codes.Num(), SW, SH, ERGBFormat::Gray, 8)) { Why = TEXT("the surfaces"); return false; }

		// ---- the masks (distance fields over the land's square), the mowing directions and the photo
		const int32 M = MaskSize;
		const double Span = (LandN - 1) * Step, Ms = Span / M;
		const TCHAR* Keys[] = { TEXT("fairway"), TEXT("green"), TEXT("tee"), TEXT("bunker"), TEXT("water"), TEXT("rough"), TEXT("native"), TEXT("stream"), TEXT("path"), TEXT("fringe") };
		constexpr int32 NumKeys = UE_ARRAY_COUNT(Keys);
		TArray<TArray<uint8>> Layers;
		Layers.SetNum(NumKeys);
		for (TArray<uint8>& L : Layers) L.SetNumZeroed(M * M);
		TArray<FColor> Photo, Mow;
		Photo.SetNumUninitialized(M * M);
		Mow.Init(FColor(128, 128, 128, 255), M * M);
		auto Mix = [](FLinearColor& C, const FLinearColor& To, double K) { C = FMath::Lerp(C, To, float(FMath::Clamp(K, 0.0, 1.0))); };
		// (sRGB, 0..1)
		const FLinearColor RoughC(0.29f, 0.41f, 0.17f), FairC(0.35f, 0.50f, 0.20f), GreenC(0.38f, 0.57f, 0.23f), TeeC(0.35f, 0.53f, 0.22f),
			SandC(0.84f, 0.78f, 0.63f), NativeC(0.44f, 0.44f, 0.24f), DeepC(0.33f, 0.38f, 0.19f);
		ParallelFor(M, [&](int32 j) {
			const double Z = Z1 - (j + 0.5) * Ms;
			for (int32 i = 0; i < M; i++)
			{
				const double X = X0 + (i + 0.5) * Ms;
				const int32 q = j * M + i;
				const double Gs = GreenSd(X, Z);
				const double Green = Field(Gs), Tee = Field(TeeSd(X, Z)), Bunker = Field(BunkerSd(X, Z));
				// (a fringe of fairway-cut grass round every green, as the export has it)
				const double Fringe = FMath::Min(Field(Gs + 1.5), 1 - Green);
				const double Fair = FMath::Max(Field(FairSd(X, Z)), Fringe);
				const double Out = Outside(X, Z);
				const double Native = FMath::Min(Field(Out - 16), 1 - FMath::Max(Fair, Tee));
				const double Rough = 1 - FMath::Max(FMath::Max(Fair, Green), FMath::Max(Tee, Bunker));
				const double Vals[NumKeys] = { Fair, Green, Tee, Bunker, 0, Rough, Native, 0, 0, Fringe };
				for (int32 k = 0; k < NumKeys; k++) Layers[k][q] = uint8(FMath::RoundToInt(Vals[k] * 255));
				// (mown down the range: north, x east y south)
				if (Fair > 0.02 || Tee > 0.02 || Green > 0.02) Mow[q] = FColor(128, 0, 128, 255);
				// the photo: the surfaces' colours, mottled
				FLinearColor C = Out > 90 ? DeepC : RoughC;
				Mix(C, NativeC, Native * 0.8);
				Mix(C, FairC, Smooth(0.3, 0.7, Fair));
				Mix(C, TeeC, Smooth(0.3, 0.7, Tee));
				Mix(C, GreenC, Smooth(0.3, 0.7, Green));
				Mix(C, SandC, Smooth(0.35, 0.65, Bunker));
				const float Mottle = float(1.0 + 0.06 * Noise(X / 7, Z / 7, 11) + 0.035 * Noise(X / 1.6, Z / 1.6, 12));
				C = C * Mottle;
				Photo[q] = FColor(uint8(FMath::Clamp(C.R * 255.f, 0.f, 255.f)), uint8(FMath::Clamp(C.G * 255.f, 0.f, 255.f)), uint8(FMath::Clamp(C.B * 255.f, 0.f, 255.f)), 255);
			}
		});
		for (int32 k = 0; k < NumKeys; k++)
			if (!SavePng(File(*FString::Printf(TEXT("layer_%s.png"), Keys[k])), Layers[k].GetData(), Layers[k].Num(), M, M, ERGBFormat::Gray, 8)) { Why = Keys[k]; return false; }
		if (!SavePng(File(TEXT("layer_mow.png")), Mow.GetData(), Mow.Num() * sizeof(FColor), M, M, ERGBFormat::BGRA, 8)) { Why = TEXT("the mowing"); return false; }
		if (!SaveJpg(File(TEXT("photo.jpg")), Photo, M, M)) { Why = TEXT("the photo"); return false; }

		// ---- the trees: drawn (trees.json, the level's cm) and for the ball (play.json), the same list
		const TArray<FTreeSpot> Spots = TreeSpots();
		FString TreesJson = TEXT("["), PlayTrees = TEXT("[");
		for (int32 t = 0; t < Spots.Num(); t++)
		{
			const FTreeSpot& S = Spots[t];
			const double G = GroundAt(S.X, S.Z);
			const bool bCone = S.Kind == TEXT("pine") || S.Kind == TEXT("spruce");
			const TCHAR* Sep = t ? TEXT(",") : TEXT("");
			TreesJson += FString::Printf(TEXT("%s{\"x\":%d,\"y\":%d,\"z\":%d,\"h\":%d,\"r\":%d,\"kind\":\"%s\"}"), Sep, FMath::RoundToInt((S.X - X0) * 100), FMath::RoundToInt((Z1 - S.Z) * 100),
				FMath::RoundToInt((G - HBase) * 100), FMath::RoundToInt(S.H * 100), FMath::RoundToInt(S.H * (bCone ? 22 : 32)), *S.Kind);
			// [x, z, trunkTop, trunkR, kind, cone, coneBase | cy, coneH | rh, coneR | rv]
			PlayTrees += bCone
				? FString::Printf(TEXT("%s[%s,%s,%s,0.25,\"%s\",1,%s,%s,%s]"), Sep, *Num(S.X), *Num(S.Z), *Num(G + S.H * 0.25), *S.Kind, *Num(G + S.H * 0.18), *Num(S.H * 0.82), *Num(S.H * 0.22))
				: FString::Printf(TEXT("%s[%s,%s,%s,0.3,\"%s\",0,%s,%s,%s]"), Sep, *Num(S.X), *Num(S.Z), *Num(G + S.H * 0.35), *S.Kind, *Num(G + S.H * 0.62), *Num(S.H * 0.32), *Num(S.H * 0.36));
		}
		TreesJson += TEXT("]");
		PlayTrees += TEXT("]");
		if (!FFileHelper::SaveStringToFile(TreesJson, *File(TEXT("trees.json")))) { Why = TEXT("trees.json"); return false; }

		// ---- play.json: one "hole", the tee line to the 150's green (the round's own flag and cup)
		const otg::range::FTarget* Pin = otg::range::Targets().FindByPredicate([](const otg::range::FTarget& T) { return T.Yards == 150; });
		const FString TeeP = Pt(0, Base, 0), PinP = Pt(Pin->X, GroundAt(Pin->X, Pin->Z), Pin->Z);
		FString Play = TEXT("{\"version\":1,\"name\":\"Driving Range\",\"id\":\"");
		Play += otg::range::Id;
		Play += TEXT("\",\"location\":\"Practice\",\"lat\":40.0,\"lon\":0.0,\"altitudeM\":0,\"defaultStimp\":10,");
		Play += FString::Printf(TEXT("\"bounds\":{\"minX\":%s,\"maxX\":%s,\"minZ\":%s,\"maxZ\":%s},"), *Num(MinX), *Num(MaxX), *Num(MinZ), *Num(MaxZ));
		Play += FString::Printf(TEXT("\"heightmap\":{\"file\":\"heightmap.png\",\"x0\":%s,\"z1\":%s,\"metresPerSample\":%s,\"samples\":%d,\"heightBaseMetres\":%s,\"heightRangeMetres\":%s},"),
			*Num(X0), *Num(Z1), *Num(Step), LandN, *Num(HBase), *Num(HRange));
		Play += FString::Printf(TEXT("\"surfaces\":{\"file\":\"surfaces.png\",\"minX\":%s,\"maxZ\":%s,\"res\":%s,\"w\":%d,\"h\":%d},"), *Num(MinX), *Num(MaxZ), *Num(PlayRes), SW, SH);
		Play += FString::Printf(TEXT("\"holes\":[{\"number\":1,\"par\":3,\"index\":1,\"greenR\":%s,\"tees\":{\"Blue\":%s,\"White\":%s,\"Red\":%s},\"pins\":{\"Saturday\":%s},\"greenCenter\":%s,\"aims\":[],\"path\":[]}],"),
			*Num(Pin->R), *TeeP, *TeeP, *TeeP, *PinP, *PinP);
		Play += TEXT("\"dropZones\":[],\"solids\":[],\"trees\":") + PlayTrees + TEXT("}");
		if (!FFileHelper::SaveStringToFile(Play, *File(TEXT("play.json")))) { Why = TEXT("play.json"); return false; }
		if (!WriteFarLand(Dir, HBase, Why)) return false;
		FFileHelper::SaveStringToFile(FString::FromInt(Version), *File(TEXT("range_version.txt")));
		UE_LOG(LogOTGRange, Display, TEXT("Driving range written to %s in %.1f s (%d trees)"), *Dir, FPlatformTime::Seconds() - T0, Spots.Num());
		return true;
	}
}

using namespace OTGRangeGen;

const TArray<otg::range::FTarget>& otg::range::Targets()
{
	static const TArray<FTarget> T = [] {
		TArray<FTarget> Out;
		const int32 Yards[] = { 50, 100, 150, 200, 250 };
		const double Xs[] = { -9, 13, -16, 15, -5 };
		const FLinearColor Colours[] = { FLinearColor(0.80f, 0.06f, 0.05f), FLinearColor(0.92f, 0.92f, 0.90f), FLinearColor(0.05f, 0.30f, 0.85f), FLinearColor(0.95f, 0.75f, 0.05f), FLinearColor(0.10f, 0.55f, 0.12f) };
		for (int32 i = 0; i < 5; i++)
		{
			FTarget G;
			G.Yards = Yards[i];
			G.X = Xs[i];
			G.Z = Yards[i] * YD;
			G.R = 4.5 + G.Z * 0.022;
			G.Colour = Colours[i];
			Out.Add(G);
		}
		return Out;
	}();
	return T;
}

double otg::range::HalfWidth(double Z) { return 40 + FMath::Max(0.0, Z) * 0.2; }

FString otg::range::Folder() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Range")); }

bool otg::range::Ensure(FString& OutDir, FString* OutError)
{
	OutDir = Folder();
	FString Have;
	if (FFileHelper::LoadFileToString(Have, *FPaths::Combine(OutDir, TEXT("range_version.txt"))) && FCString::Atoi(*Have) == Version
		&& FPaths::FileExists(FPaths::Combine(OutDir, TEXT("play.json")))) return true;
	FString Why;
	if (Write(OutDir, Why)) return true;
	if (OutError) *OutError = TEXT("couldn't write ") + Why + TEXT(" in ") + OutDir;
	UE_LOG(LogOTGRange, Error, TEXT("Driving range: couldn't write %s in %s"), *Why, *OutDir);
	return false;
}

bool otg::range::IsRange(const UOTGCourseData* D) { return D && D->CourseId == Id; }

// ------------------------------------------------------------------ the range's things

AOTGRangeProps::AOTGRangeProps()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;
	BaseMat = Mat.Object;
}

UInstancedStaticMeshComponent* AOTGRangeProps::Ism(UStaticMesh* Mesh, const FLinearColor& Colour)
{
	const FString Key = Mesh->GetName() + Colour.ToString();
	if (UInstancedStaticMeshComponent** Have = ByLook.Find(Key)) return *Have;
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetMobility(EComponentMobility::Movable);
	C->SetupAttachment(RootComponent);
	C->RegisterComponent();
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, this);
	M->SetVectorParameterValue(TEXT("Color"), Colour);
	C->SetMaterial(0, M);
	Parts.Add(C);
	ByLook.Add(Key, C);
	return C;
}

void AOTGRangeProps::Build(const UOTGCourseData* D)
{
	if (!D || !CubeMesh || !CylinderMesh) return;
	SetActorLocation(FVector::ZeroVector);
	auto At = [D](double X, double Z, double UpCm = 0) { return D->CourseToWorld(FVector(X, D->GroundAt(X, Z), Z)) + FVector(0, 0, UpCm); };
	// (the engine's cube and cylinder: a metre across, centred)
	auto Box = [&](UStaticMesh* Mesh, const FLinearColor& Colour, const FVector& Centre, const FRotator& R, const FVector& SizeCm) {
		UInstancedStaticMeshComponent* C = Ism(Mesh, Colour);
		const int32 I = C->AddInstance(FTransform(R, Centre, SizeCm / 100.0), true);
		return TPair<UInstancedStaticMeshComponent*, int32>(C, I);
	};
	const FLinearColor White(0.85f, 0.85f, 0.82f), Dark(0.02f, 0.02f, 0.02f), Post(0.20f, 0.16f, 0.12f);

	// the hitting mats along the tee line (the middle one is the player's: the ball's teed on it)
	for (int32 k = -6; k <= 6; k++)
	{
		const double X = k * 4.0;
		Box(CubeMesh, FLinearColor(0.012f, 0.045f, 0.016f), At(X, -0.1, 0.5), FRotator::ZeroRotator, FVector(150, 150, 2));
		// (a strip of rubber to stand on, beside it)
		Box(CubeMesh, FLinearColor(0.012f, 0.012f, 0.012f), At(X - 1.05, -0.1, 0.5), FRotator::ZeroRotator, FVector(55, 150, 2.2));
	}

	// a flag on each target green (the 150's is the round's own: it blows in the wind and you can hit it)
	for (const otg::range::FTarget& T : otg::range::Targets())
	{
		if (T.Yards == 150) continue;
		const FVector Foot = At(T.X, T.Z);
		Box(CylinderMesh, Dark, Foot + FVector(0, 0, 0.3), FRotator::ZeroRotator, FVector(10.8, 10.8, 0.2));
		Box(CylinderMesh, FLinearColor(0.9f, 0.9f, 0.85f), Foot + FVector(0, 0, 115), FRotator::ZeroRotator, FVector(2, 2, 230));
		const FVector Top = Foot + FVector(0, 0, 210);
		const TPair<UInstancedStaticMeshComponent*, int32> F = Box(CubeMesh, T.Colour, Top + FVector(28, 0, 0), FRotator::ZeroRotator, FVector(55, 1.5, 36));
		Flags.Add({ F.Key, F.Value, Top });
	}

	// distance boards every 50 yards down both sides, facing the tee: a white board on two posts, a
	// band in the target's colour across the top, the yards in big black figures
	static const uint8 Segs[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };   // (bits a b c d e f g)
	const double W = 55, Hd = 100, Th = 13, Gap = 22;   // a figure's size (cm)
	for (int32 Yd = 50; Yd <= 300; Yd += 50)
	{
		const double Z = Yd * YD;
		const otg::range::FTarget* T = otg::range::Targets().FindByPredicate([Yd](const otg::range::FTarget& G) { return G.Yards == Yd; });
		for (int32 S : { -1, 1 })
		{
			const double X = S * (otg::range::HalfWidth(Z) + 6);
			const FVector Foot = At(X, Z);
			FVector Face = At(0, 0) - Foot;
			Face.Z = 0;
			Face = Face.GetSafeNormal();
			const FVector Right = FVector::CrossProduct(FVector::UpVector, -Face);   // (as you look at it)
			const FRotator R = FRotationMatrix::MakeFromXZ(Face, FVector::UpVector).Rotator();
			for (int32 p : { -1, 1 }) Box(CylinderMesh, Post, Foot + Right * (p * 120.0) + FVector(0, 0, 130), FRotator::ZeroRotator, FVector(12, 12, 260));
			const FVector Mid = Foot + FVector(0, 0, 180);
			Box(CubeMesh, White, Mid, R, FVector(12, 320, 160));
			Box(CubeMesh, T ? T->Colour : FLinearColor(0.15f, 0.15f, 0.15f), Mid + Face * 1.0 + FVector(0, 0, 66), R, FVector(12, 320, 28));
			// the figures
			const FString Txt = FString::FromInt(Yd);
			const double Total = Txt.Len() * W + (Txt.Len() - 1) * Gap;
			const FVector Centre = Mid + Face * 7.5 - FVector(0, 0, 14);
			for (int32 c = 0; c < Txt.Len(); c++)
			{
				const uint8 Bits = Segs[Txt[c] - TEXT('0')];
				const double U0 = -Total / 2 + W / 2 + c * (W + Gap);
				// (each segment: across, up, its width and height)
				const double Sg[7][4] = {
					{ 0, Hd / 2 - Th / 2, W, Th }, { W / 2 - Th / 2, Hd / 4, Th, Hd / 2 }, { W / 2 - Th / 2, -Hd / 4, Th, Hd / 2 }, { 0, -Hd / 2 + Th / 2, W, Th },
					{ -W / 2 + Th / 2, -Hd / 4, Th, Hd / 2 }, { -W / 2 + Th / 2, Hd / 4, Th, Hd / 2 }, { 0, 0, W, Th } };
				for (int32 s = 0; s < 7; s++)
					if (Bits & (1 << s)) Box(CubeMesh, Dark, Centre + Right * (U0 + Sg[s][0]) + FVector(0, 0, Sg[s][1]), R, FVector(2, Sg[s][2], Sg[s][3]));
			}
		}
	}
	int32 Count = 0;
	for (UInstancedStaticMeshComponent* C : Parts) Count += C->GetInstanceCount();
	UE_LOG(LogOTGRange, Display, TEXT("Range: %d mats, %d flags, boards every 50 yd (%d pieces)"), 13, Flags.Num(), Count);
}

void AOTGRangeProps::SetWind(const FVector& Down, double S)
{
	const FVector D = Down.IsNearlyZero() ? FVector(1, 0, 0) : FVector(Down.X, Down.Y, 0).GetSafeNormal();
	const double Droop = FMath::Lerp(70.0, 10.0, FMath::Clamp(S, 0.0, 1.0));
	const FRotator R = FRotator(-Droop * 0.5, D.Rotation().Yaw, 0);
	for (const FFlag& F : Flags)
		if (F.Comp)
		{
			const FVector Along = R.Vector();
			F.Comp->UpdateInstanceTransform(F.Index, FTransform(R, F.Top + Along * 28 - FVector(0, 0, 4), FVector(0.55, 0.015, 0.36)), true, true);
		}
}
