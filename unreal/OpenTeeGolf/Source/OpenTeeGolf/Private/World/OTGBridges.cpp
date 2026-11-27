// See OTGBridges.h.

#include "World/OTGBridges.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace
{
	const TCHAR* File = TEXT("bridges.json");

	// (0..1, the same every time for the same numbers: a plank's or a stone's own shade)
	double Hash01(double A, double B, int32 Salt)
	{
		const double S = FMath::Sin(A * 12.9898 + B * 78.233 + Salt * 37.719) * 43758.5453;
		return S - FMath::FloorToDouble(S);
	}

	// Boxes into a mesh description: corners in the level's cm, each face its own flat normal,
	// facing out (a box's corners: [0..3] one end - right low, right high, left high, left low - and
	// [4..7] the other end the same)
	struct FBoxWriter
	{
		FMeshDescription& MD;
		TVertexAttributesRef<FVector3f> Pos;
		TVertexInstanceAttributesRef<FVector3f> Nrm, Tan;
		TVertexInstanceAttributesRef<float> Sign;
		TVertexInstanceAttributesRef<FVector2f> UV;
		FPolygonGroupID Grp;

		explicit FBoxWriter(FMeshDescription& InMD) : MD(InMD)
		{
			FStaticMeshAttributes Attr(MD);
			Attr.Register();
			Pos = Attr.GetVertexPositions();
			Nrm = Attr.GetVertexInstanceNormals();
			Tan = Attr.GetVertexInstanceTangents();
			Sign = Attr.GetVertexInstanceBinormalSigns();
			UV = Attr.GetVertexInstanceUVs();
			UV.SetNumChannels(2);
			Grp = MD.CreatePolygonGroup();
			Attr.GetPolygonGroupMaterialSlotNames()[Grp] = TEXT("Main");
		}

		void Quad(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, const FVector& Out, const FLinearColor& Col)
		{
			FVector N = FVector::CrossProduct(P1 - P0, P2 - P0).GetSafeNormal();
			if (N.IsNearlyZero()) N = FVector::CrossProduct(P2 - P0, P3 - P0).GetSafeNormal();
			if (N.IsNearlyZero()) return;
			// (Unreal's front faces: their corners' cross product points away from the normal)
			const bool bFlip = FVector::DotProduct(N, Out) > 0;
			if (bFlip) N = -N;
			const FVector Normal = -N;
			const FVector P[4] = { P0, P1, P2, P3 };
			FVertexInstanceID VI[4];
			for (int32 k = 0; k < 4; k++)
			{
				const FVertexID V = MD.CreateVertex();
				Pos[V] = FVector3f(P[k]);
				VI[k] = MD.CreateVertexInstance(V);
				Nrm[VI[k]] = FVector3f(Normal);
				FVector T = (FVector(1, 0, 0) - Normal * Normal.X).GetSafeNormal();
				if (T.IsNearlyZero()) T = (FVector(0, 1, 0) - Normal * Normal.Y).GetSafeNormal();
				Tan[VI[k]] = FVector3f(T);
				Sign[VI[k]] = 1.f;
				UV.Set(VI[k], 0, FVector2f(Col.R, Col.G));
				UV.Set(VI[k], 1, FVector2f(Col.B, 0));
			}
			if (bFlip) { MD.CreateTriangle(Grp, { VI[0], VI[2], VI[1] }); MD.CreateTriangle(Grp, { VI[0], VI[3], VI[2] }); }
			else { MD.CreateTriangle(Grp, { VI[0], VI[1], VI[2] }); MD.CreateTriangle(Grp, { VI[0], VI[2], VI[3] }); }
		}

		void Box(const FVector (&C)[8], const FLinearColor& Col)
		{
			FVector Mid = FVector::ZeroVector;
			for (const FVector& P : C) Mid += P / 8;
			static const int32 Faces[6][4] = { { 0, 1, 2, 3 }, { 4, 7, 6, 5 }, { 0, 4, 5, 1 }, { 3, 2, 6, 7 }, { 1, 5, 6, 2 }, { 0, 3, 7, 4 } };
			for (const auto& F : Faces)
			{
				const FVector FMid = (C[F[0]] + C[F[1]] + C[F[2]] + C[F[3]]) / 4;
				Quad(C[F[0]], C[F[1]], C[F[2]], C[F[3]], FMid - Mid, Col);
			}
		}
	};

	FLinearColor Shade(const FLinearColor& C, double F) { return FLinearColor(float(C.R * F), float(C.G * F), float(C.B * F), 1.f); }
}

const TArray<FString>& FOTGBridge::Looks()
{
	static const TArray<FString> L = { TEXT("wood"), TEXT("stone"), TEXT("white") };
	return L;
}

FString FOTGBridge::LookLabel(const FString& Look)
{
	if (Look == TEXT("stone")) return TEXT("Stone");
	if (Look == TEXT("white")) return TEXT("White boards");
	return TEXT("Wood");
}

bool FOTGBridge::Over(double X, double Z, double& OutT, double& OutTop) const
{
	const double DX = BX - AX, DZ = BZ - AZ, L2 = DX * DX + DZ * DZ;
	if (L2 < 1e-6) return false;
	const double T = ((X - AX) * DX + (Z - AZ) * DZ) / L2;
	if (T < 0 || T > 1) return false;
	if (FMath::Abs((X - AX) * DZ - (Z - AZ) * DX) / FMath::Sqrt(L2) > HalfReach()) return false;
	OutT = T;
	OutTop = TopAt(T);
	return true;
}

void FOTGBridge::Rails(TArray<FRail>& Out) const
{
	if (!bRails) return;
	const double L = Span();
	if (L < 0.5) return;
	const FVector2D D((BX - AX) / L, (BZ - AZ) / L), R(D.Y, -D.X);
	const int32 N = FMath::Max(1, FMath::CeilToInt32(L / 1.0));
	const double High = Look == TEXT("stone") ? 0.9 : 1.05;
	for (int32 Side = -1; Side <= 1; Side += 2)
		for (int32 i = 0; i < N; i++)
		{
			const double T0 = double(i) / N, T1 = double(i + 1) / N;
			const FVector2D P0(AX + (BX - AX) * T0, AZ + (BZ - AZ) * T0), P1(AX + (BX - AX) * T1, AZ + (BZ - AZ) * T1);
			const double S0 = Side * (Width / 2 - 0.12), S1 = Side * (Width / 2 + 0.12);
			FRail Rl;
			Rl.Pts = { P0 + R * S0, P1 + R * S0, P1 + R * S1, P0 + R * S1 };
			Rl.Bottom = FMath::Min(TopAt(T0), TopAt(T1));
			Rl.Top = FMath::Max(TopAt(T0), TopAt(T1)) + High;
			Out.Add(MoveTemp(Rl));
		}
}

void FOTGBridge::BuildMesh(FMeshDescription& MD, TFunctionRef<FVector(const FVector&)> CourseToWorld, const FVector& Origin, TFunctionRef<double(double, double)> GroundAt) const
{
	FBoxWriter W(MD);
	const double L = Span();
	if (L < 0.5) return;
	const bool bStone = Look == TEXT("stone"), bWhite = Look == TEXT("white");
	// (course metres: x east, y up, z north)
	const FVector Dir((BX - AX) / L, 0, (BZ - AZ) / L), Right(Dir.Z, 0, -Dir.X);
	auto Centre = [&](double T) { return FVector(AX + (BX - AX) * T, TopAt(T), AZ + (BZ - AZ) * T); };
	auto UpAt = [&](double T) {
		const double Slope = (BY - AY) + (bArch ? Rise * 4 * (1 - 2 * T) : 0.0);
		const FVector Along = (Dir * L + FVector(0, Slope, 0)).GetSafeNormal();
		FVector U = FVector::CrossProduct(Along, Right).GetSafeNormal();
		if (U.Y < 0) U = -U;
		return U;
	};
	auto ToLevel = [&](const FVector& P) { return CourseToWorld(P) - Origin; };
	// A box following the deck from T0 to T1, S0..S1 across it (m from its middle line), Y0..Y1 above its top (along its up)
	auto Along = [&](double T0, double T1, double S0, double S1, double Y0, double Y1, const FLinearColor& Col) {
		FVector C[8];
		const double Ts[2] = { T0, T1 };
		for (int32 e = 0; e < 2; e++)
		{
			const FVector M = Centre(Ts[e]), U = UpAt(Ts[e]);
			C[e * 4 + 0] = ToLevel(M + Right * S1 + U * Y0);
			C[e * 4 + 1] = ToLevel(M + Right * S1 + U * Y1);
			C[e * 4 + 2] = ToLevel(M + Right * S0 + U * Y1);
			C[e * 4 + 3] = ToLevel(M + Right * S0 + U * Y0);
		}
		W.Box(C, Col);
	};
	// A box standing upright at T, S across, from Y0 to Y1 (course metres, absolute), HA along and HS across (half sizes)
	auto Upright = [&](double T, double S, double HA, double HS, double Y0, double Y1, const FLinearColor& Col) {
		const FVector M = Centre(T) + Right * S;
		FVector C[8];
		for (int32 e = 0; e < 2; e++)
		{
			const FVector At = FVector(M.X, 0, M.Z) + Dir * (e ? HA : -HA);
			C[e * 4 + 0] = ToLevel(At + Right * HS + FVector(0, Y0, 0));
			C[e * 4 + 1] = ToLevel(At + Right * HS + FVector(0, Y1, 0));
			C[e * 4 + 2] = ToLevel(At - Right * HS + FVector(0, Y1, 0));
			C[e * 4 + 3] = ToLevel(At - Right * HS + FVector(0, Y0, 0));
		}
		W.Box(C, Col);
	};
	auto Ground = [&](double T, double S) { const FVector P = Centre(T) + Right * S; return GroundAt(P.X, P.Z); };

	const double HW = Width / 2;
	// (pieces a metre or so along: an arch bends through them)
	const int32 Segs = FMath::Max(1, FMath::CeilToInt32(L / (bArch ? 0.6 : 1.5)));
	const FLinearColor Wood(0.13f, 0.075f, 0.04f), DarkWood(0.08f, 0.047f, 0.026f), Paint(0.78f, 0.78f, 0.75f), Boards(0.5f, 0.48f, 0.44f);
	const FLinearColor Stone(0.34f, 0.32f, 0.29f), Cap(0.45f, 0.43f, 0.4f), PathTop(0.3f, 0.295f, 0.28f);

	if (!bStone)
	{
		// boards across, a gap between, on two beams; the beams on posts (flat) or curved (arched)
		const int32 Planks = FMath::Max(2, FMath::RoundToInt32(L / 0.3));
		for (int32 i = 0; i < Planks; i++)
		{
			const double T0 = (i + 0.06) / Planks, T1 = (i + 0.94) / Planks;
			const double F = 0.8 + 0.4 * Hash01(AX + i, AZ - i, 3);
			Along(T0, T1, -HW, HW, -0.07, 0.0, Shade(bWhite ? Boards : Wood, F));
		}
		for (int32 i = 0; i < Segs; i++)
		{
			const double T0 = double(i) / Segs, T1 = double(i + 1) / Segs;
			for (int32 Side = -1; Side <= 1; Side += 2)
				Along(T0, T1, Side * (HW - 0.35), Side * (HW - 0.1), -0.4, -0.07, bWhite ? Shade(Paint, 0.85) : DarkWood);
			// (an arch's beams: a fascia along each side)
			if (bArch)
				for (int32 Side = -1; Side <= 1; Side += 2)
					Along(T0, T1, Side * (HW + 0.0), Side * (HW + 0.05), -0.35, -0.02, bWhite ? Paint : Shade(Wood, 0.8));
		}
		// posts down to the ground (a flat bridge's, every 3 to 4 m; under an arch, by its ends)
		const int32 Bents = bArch ? 0 : FMath::Max(0, FMath::FloorToInt32(L / 3.5));
		for (int32 k = 1; k <= Bents; k++)
		{
			const double T = double(k) / (Bents + 1);
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				const double S = Side * (HW - 0.22);
				const double G = Ground(T, S);
				if (Centre(T).Y - 0.4 - G < 0.15) continue;
				Upright(T, S, 0.11, 0.11, G - 0.8, Centre(T).Y - 0.4, bWhite ? Shade(Paint, 0.8) : DarkWood);
			}
			// (a cross-beam on them)
			Along(T - 0.12 / L, T + 0.12 / L, -HW + 0.05, HW - 0.05, -0.55, -0.4, bWhite ? Shade(Paint, 0.85) : DarkWood);
		}
		if (bRails)
		{
			const int32 Posts = FMath::Max(1, FMath::RoundToInt32(L / 1.8));
			const FLinearColor RailCol = bWhite ? Paint : Shade(Wood, 0.85);
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				const double S = Side * (HW - 0.06);
				for (int32 k = 0; k <= Posts; k++)
				{
					const double T = FMath::Clamp(double(k) / Posts, 0.04 / L, 1 - 0.04 / L);
					const double Y = Centre(T).Y;
					Upright(T, S, 0.06, 0.06, Y - 0.3, Y + 1.05, RailCol);
				}
				for (int32 i = 0; i < Segs; i++)
				{
					const double T0 = double(i) / Segs, T1 = double(i + 1) / Segs;
					Along(T0, T1, S - 0.07, S + 0.07, 1.0, 1.1, RailCol);
					Along(T0, T1, S - 0.035, S + 0.035, 0.48, 0.56, RailCol);
				}
			}
		}
	}
	else
	{
		// stones: the deck a slab of blocks, a path on top; an arch's spandrels down to its curve;
		// piers (flat) and abutments at the ends
		for (int32 i = 0; i < Segs; i++)
		{
			const double T0 = double(i) / Segs, T1 = double(i + 1) / Segs;
			const double F = 0.85 + 0.3 * Hash01(AX + i, AZ + i, 5);
			Along(T0, T1, -HW, HW, -0.55, -0.04, Shade(Stone, F));
			Along(T0, T1, -HW + 0.02, HW - 0.02, -0.04, 0.0, PathTop);
			if (bRails)
				for (int32 Side = -1; Side <= 1; Side += 2)
				{
					const double F2 = 0.85 + 0.3 * Hash01(AX - i, AZ + Side, 7);
					Along(T0, T1, Side * (HW - 0.3), Side * HW, 0.0, 0.8, Shade(Stone, F2));
					Along(T0, T1, Side * (HW - 0.34), Side * (HW + 0.04), 0.8, 0.92, Cap);
				}
		}
		if (bArch)
		{
			// (the spandrels: from the deck down toward its ends' ground, thinning to the crown)
			for (int32 i = 0; i < Segs; i++)
			{
				const double T0 = double(i) / Segs, T1 = double(i + 1) / Segs, Tm = (T0 + T1) / 2;
				const double Drop = 0.55 + Rise * (1 - 4 * Tm * (1 - Tm)) * 0.9;
				const double F = 0.8 + 0.3 * Hash01(AX + i, AZ - 2 * i, 9);
				for (int32 Side = -1; Side <= 1; Side += 2)
					Along(T0, T1, Side * (HW - 0.25), Side * HW, -Drop, -0.55, Shade(Stone, F * 0.92));
			}
		}
		else
		{
			const int32 Piers = FMath::Max(0, FMath::FloorToInt32(L / 6.0));
			for (int32 k = 1; k <= Piers; k++)
			{
				const double T = double(k) / (Piers + 1);
				const double G = FMath::Min(Ground(T, -HW + 0.3), Ground(T, HW - 0.3));
				if (Centre(T).Y - 0.55 - G < 0.2) continue;
				Upright(T, 0, 0.45, HW - 0.1, G - 1.0, Centre(T).Y - 0.55, Shade(Stone, 0.9));
			}
		}
		// abutments: a block under each end, into the bank
		for (int32 e = 0; e < 2; e++)
		{
			const double T = e ? 1 - 0.5 / L : 0.5 / L;
			const double G = FMath::Min(Ground(T, -HW), Ground(T, HW));
			Upright(T, 0, 0.5, HW + 0.1, G - 1.0, Centre(T).Y - 0.3, Shade(Stone, 0.95));
		}
	}
}

TSharedRef<FJsonObject> FOTGBridge::ToJson() const
{
	const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	auto Arr = [](double X, double Z, double Y) {
		return TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(X), MakeShared<FJsonValueNumber>(Z), MakeShared<FJsonValueNumber>(Y) };
	};
	O->SetArrayField(TEXT("a"), Arr(AX, AZ, AY));
	O->SetArrayField(TEXT("b"), Arr(BX, BZ, BY));
	O->SetStringField(TEXT("shape"), bArch ? TEXT("arch") : TEXT("flat"));
	O->SetBoolField(TEXT("rails"), bRails);
	O->SetStringField(TEXT("look"), Look);
	O->SetNumberField(TEXT("width"), Width);
	O->SetNumberField(TEXT("rise"), Rise);
	return O;
}

bool FOTGBridge::FromJson(const TSharedPtr<FJsonObject>& O, FOTGBridge& Out)
{
	if (!O.IsValid()) return false;
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* B = nullptr;
	if (!O->TryGetArrayField(TEXT("a"), A) || !O->TryGetArrayField(TEXT("b"), B) || A->Num() < 3 || B->Num() < 3) return false;
	FOTGBridge Br;
	Br.AX = (*A)[0]->AsNumber(); Br.AZ = (*A)[1]->AsNumber(); Br.AY = (*A)[2]->AsNumber();
	Br.BX = (*B)[0]->AsNumber(); Br.BZ = (*B)[1]->AsNumber(); Br.BY = (*B)[2]->AsNumber();
	if (!FMath::IsFinite(Br.AX + Br.AZ + Br.AY + Br.BX + Br.BZ + Br.BY) || Br.Span() < 0.5) return false;
	FString S;
	if (O->TryGetStringField(TEXT("shape"), S)) Br.bArch = S == TEXT("arch");
	O->TryGetBoolField(TEXT("rails"), Br.bRails);
	if (O->TryGetStringField(TEXT("look"), S) && Looks().Contains(S)) Br.Look = S;
	double V;
	if (O->TryGetNumberField(TEXT("width"), V) && FMath::IsFinite(V)) Br.Width = FMath::Clamp(float(V), WidthLo, WidthHi);
	if (O->TryGetNumberField(TEXT("rise"), V) && FMath::IsFinite(V)) Br.Rise = FMath::Clamp(float(V), RiseLo, RiseHi);
	Out = Br;
	return true;
}

FOTGBridgeDesign FOTGBridgeDesign::Load(const FString& Dir)
{
	FOTGBridgeDesign D;
	FString Text;
	TSharedPtr<FJsonObject> J;
	if (Dir.IsEmpty() || !FFileHelper::LoadFileToString(Text, *FPaths::Combine(Dir, File)) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) || !J.IsValid()) return D;
	const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
	if (J->TryGetArrayField(TEXT("bridges"), L))
		for (const TSharedPtr<FJsonValue>& V : *L)
		{
			FOTGBridge B;
			if (FOTGBridge::FromJson(V->AsObject(), B)) D.Bridges.Add(B);
		}
	return D;
}

bool FOTGBridgeDesign::Save(const FString& Dir) const
{
	if (Dir.IsEmpty()) return false;
	const FString Path = FPaths::Combine(Dir, File);
	if (!Bridges.Num()) return !IFileManager::Get().FileExists(*Path) || IFileManager::Get().Delete(*Path);
	const TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("version"), 1);
	TArray<TSharedPtr<FJsonValue>> L;
	for (const FOTGBridge& B : Bridges) L.Add(MakeShared<FJsonValueObject>(B.ToJson()));
	J->SetArrayField(TEXT("bridges"), L);
	FString Out;
	const TSharedRef<TJsonWriter<>> Wr = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(J, Wr);
	return FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

int32 FOTGBridgeDesign::At(double X, double Z, double* OutTop) const
{
	int32 Best = -1;
	double BestTop = -1e300;
	for (int32 i = 0; i < Bridges.Num(); i++)
	{
		double T, Top;
		if (Bridges[i].Over(X, Z, T, Top) && Top > BestTop) { Best = i; BestTop = Top; }
	}
	if (OutTop && Best >= 0) *OutTop = BestTop;
	return Best;
}

void otg::BridgeMarkerMesh(FMeshDescription& MD, double HeightCm)
{
	FBoxWriter W(MD);
	const FLinearColor Gold(0.95f, 0.62f, 0.08f);
	auto Block = [&](double H, double Z0, double Z1) {
		FVector C[8];
		for (int32 e = 0; e < 2; e++)
		{
			const double X = e ? H : -H;
			C[e * 4 + 0] = FVector(X, H, Z0);
			C[e * 4 + 1] = FVector(X, H, Z1);
			C[e * 4 + 2] = FVector(X, -H, Z1);
			C[e * 4 + 3] = FVector(X, -H, Z0);
		}
		W.Box(C, Gold);
	};
	Block(8, -20, HeightCm);
	Block(30, HeightCm, HeightCm + 40);
}
