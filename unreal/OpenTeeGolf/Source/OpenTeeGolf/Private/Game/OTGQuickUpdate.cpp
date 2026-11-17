// See OTGQuickUpdate.h.

#include "OTGQuickUpdate.h"

#include "OTGCourseData.h"
#include "Game/OTGTreeDesign.h"
#include "Dom/JsonObject.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGQuick, Log, All);

namespace
{
	// export-unreal.js SURFACE_CODES
	enum ECode : uint8 { None = 0, Water, Green, Fringe, Fairway, Tee, FirstCut, Rough, DeepRough, Native, Path, Sand, Oob };
	constexpr double SdfM = 3.0, FringeM = 1.5, FirstCutM = 2.0, LayerSize = 4096;

	bool WritePng(const FString& File, const void* Data, int64 Bytes, int32 W, int32 H, ERGBFormat Fmt, int32 Depth)
	{
		IImageWrapperModule& M = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Png = M.CreateImageWrapper(EImageFormat::PNG);
		if (!Png.IsValid() || !Png->SetRaw(Data, Bytes, W, H, Fmt, Depth)) return false;
		const TArray64<uint8> Out = Png->GetCompressed();
		// (written beside, then moved over: the game never reads half a file)
		const FString Tmp = File + TEXT(".part");
		if (!FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(Out.GetData(), Out.Num()), *Tmp)) return false;
		return IFileManager::Get().Move(*File, *Tmp, true, true);
	}

	TSharedPtr<FJsonObject> ReadJsonObject(const FString& File)
	{
		FString Text;
		TSharedPtr<FJsonObject> O;
		if (FFileHelper::LoadFileToString(Text, *File)) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), O);
		return O;
	}
	TSharedPtr<FJsonValue> ReadJsonValue(const FString& File)
	{
		FString Text;
		TSharedPtr<FJsonValue> V;
		if (FFileHelper::LoadFileToString(Text, *File)) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), V);
		return V;
	}
	bool WriteJson(const FString& File, const TSharedRef<FJsonValue>& V, bool bPretty)
	{
		FString Text;
		if (bPretty) FJsonSerializer::Serialize(V, FString(), TJsonWriterFactory<>::Create(&Text));
		else FJsonSerializer::Serialize(V, FString(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
		return FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	bool Same(const FOTGSourceShape& A, const FOTGSourceShape& B)
	{
		return A.Kind == B.Kind && A.bInner == B.bInner && A.bFlow == B.bFlow && A.bInnerOOB == B.bInnerOOB && A.Pts == B.Pts && A.Density == B.Density && A.Species == B.Species
			&& (A.HMin == B.HMin || (FMath::IsNaN(A.HMin) && FMath::IsNaN(B.HMin))) && (A.HMax == B.HMax || (FMath::IsNaN(A.HMax) && FMath::IsNaN(B.HMax)));
	}
	FBox2D BoxOf(const TArray<FVector2D>& P) { FBox2D B(ForceInit); for (const FVector2D& Q : P) B += Q; return B; }
	bool InPoly(const TArray<FVector2D>& P, double X, double Z)
	{
		bool In = false;
		for (int32 i = 0, j = P.Num() - 1; i < P.Num(); j = i++)
			if ((P[i].Y > Z) != (P[j].Y > Z) && X < (P[j].X - P[i].X) * (Z - P[i].Y) / (P[j].Y - P[i].Y) + P[i].X) In = !In;
		return In;
	}
	double EdgeDist(const TArray<FVector2D>& P, double X, double Z)
	{
		double Best = TNumericLimits<double>::Max();
		const FVector2D Q(X, Z);
		for (int32 i = 0, j = P.Num() - 1; i < P.Num(); j = i++)
		{
			const FVector2D A = P[j], D = P[i] - P[j];
			const double L = D.SizeSquared();
			const double T = L > 0 ? FMath::Clamp(FVector2D::DotProduct(Q - A, D) / L, 0.0, 1.0) : 0.0;
			Best = FMath::Min(Best, FVector2D::DistSquared(Q, A + D * T));
		}
		return FMath::Sqrt(Best);
	}

	// One kind's outlines (the cut-outs taken away): inside, and the signed distance to the nearest edge
	struct FKind
	{
		TArray<const FOTGSourceShape*> Out, In;
		void Add(const FOTGSourceShape& S) { (S.bInner ? In : Out).Add(&S); }
		bool Inside(double X, double Z) const
		{
			bool bIn = false;
			for (const FOTGSourceShape* S : Out) if (InPoly(S->Pts, X, Z)) { bIn = true; break; }
			if (!bIn) return false;
			for (const FOTGSourceShape* S : In) if (InPoly(S->Pts, X, Z)) return false;
			return true;
		}
		// (metres, + inside; only the edges within Reach looked at, else +-Reach)
		double Signed(double X, double Z, double Reach) const
		{
			double D = Reach;
			for (const TArray<const FOTGSourceShape*>* L : { &Out, &In })
				for (const FOTGSourceShape* S : *L)
				{
					const FBox2D B = BoxOf(S->Pts).ExpandBy(Reach);
					if (X < B.Min.X || X > B.Max.X || Z < B.Min.Y || Z > B.Max.Y) continue;
					D = FMath::Min(D, EdgeDist(S->Pts, X, Z));
				}
			return Inside(X, Z) ? D : -D;
		}
		double OutsideDist(double X, double Z, double Reach) const   // (to the outer edges only: fringe, first cut)
		{
			double D = Reach;
			for (const FOTGSourceShape* S : Out)
			{
				const FBox2D B = BoxOf(S->Pts).ExpandBy(Reach);
				if (X < B.Min.X || X > B.Max.X || Z < B.Min.Y || Z > B.Max.Y) continue;
				D = FMath::Min(D, EdgeDist(S->Pts, X, Z));
			}
			return D;
		}
	};

	// derive-layers.js boxBlur: radius R pixels both ways, edges clamped
	void BoxBlur(TArray<float>& A, int32 N, int32 R)
	{
		if (R < 1) return;
		TArray<float> T;
		T.SetNumUninitialized(N * N);
		const float K = 1.f / (2 * R + 1);
		for (int32 j = 0; j < N; j++)
		{
			const int32 O = j * N;
			double S = 0;
			for (int32 t = -R; t <= R; t++) S += A[O + FMath::Clamp(t, 0, N - 1)];
			for (int32 i = 0; i < N; i++)
			{
				T[O + i] = float(S * K);
				S += A[O + FMath::Min(N - 1, i + R + 1)] - A[O + FMath::Max(0, i - R)];
			}
		}
		for (int32 i = 0; i < N; i++)
		{
			double S = 0;
			for (int32 t = -R; t <= R; t++) S += T[FMath::Clamp(t, 0, N - 1) * N + i];
			for (int32 j = 0; j < N; j++)
			{
				A[j * N + i] = float(S * K);
				S += T[FMath::Min(N - 1, j + R + 1) * N + i] - T[FMath::Max(0, j - R) * N + i];
			}
		}
	}

	// A new tree's kind: a species, or one from a mix (by its position)
	FString KindFor(const FString& K, double X, double Z)
	{
		const double H = FOTGTreeDesign::Hash01(X, Z, 77);
		auto Of = [H](std::initializer_list<const TCHAR*> L) { TArray<FString> A; for (const TCHAR* S : L) A.Add(S); return A[FMath::Min(A.Num() - 1, int32(H * A.Num()))]; };
		if (K == TEXT("broadleaf") || K == TEXT("auto") || K.IsEmpty()) return Of({ TEXT("oak"), TEXT("maple"), TEXT("ash"), TEXT("beech"), TEXT("elm") });
		if (K == TEXT("conifer")) return Of({ TEXT("pine"), TEXT("spruce"), TEXT("hemlock") });
		if (K == TEXT("ornamental")) return Of({ TEXT("dogwood"), TEXT("crapemyrtle"), TEXT("magnolia"), TEXT("holly") });
		if (K == TEXT("desert")) return Of({ TEXT("mesquite"), TEXT("paloverde") });
		return K;
	}
	bool Conifer(const FString& K)
	{
		static const TSet<FString> C = { TEXT("pine"), TEXT("loblolly"), TEXT("redcedar"), TEXT("douglasfir"), TEXT("ponderosa"), TEXT("spruce"), TEXT("cypress"), TEXT("baldcypress"), TEXT("hemlock"), TEXT("juniper"), TEXT("holly") };
		return C.Contains(K);
	}
}

otg::FQuickResult otg::QuickUpdate(const FString& Dir, const FOTGCourseSource& Before, const FOTGCourseSource& After)
{
	FQuickResult R;
	const double T0 = FPlatformTime::Seconds();
	auto Fail = [&](const FString& Why) { R.bOk = false; R.Message = Why; UE_LOG(LogOTGQuick, Warning, TEXT("Quick update: %s"), *Why); return R; };
	const TSharedPtr<FJsonObject> Play = ReadJsonObject(FPaths::Combine(Dir, TEXT("play.json")));
	const TSharedPtr<FJsonObject> Meta = ReadJsonObject(FPaths::Combine(Dir, TEXT("course.json")));
	if (!Play.IsValid() || !Meta.IsValid()) return Fail(TEXT("the course isn't in the game yet (put it in the game first)"));

	// ---- the frames: the heightmap's, the play surfaces', the masks'
	const TSharedPtr<FJsonObject> Hm = Play->GetObjectField(TEXT("heightmap"));
	const double X0 = Hm->GetNumberField(TEXT("x0")), Z1 = Hm->GetNumberField(TEXT("z1")), Step = Hm->GetNumberField(TEXT("metresPerSample"));
	const double Base = Hm->GetNumberField(TEXT("heightBaseMetres")), Range = Hm->GetNumberField(TEXT("heightRangeMetres"));
	int32 N = 0, NH = 0;
	TArray64<uint8> HRaw;
	if (!otg::ReadGreyPng(FPaths::Combine(Dir, Hm->GetStringField(TEXT("file"))), 16, N, NH, HRaw)) return Fail(TEXT("its heightmap doesn't read"));
	TArray<uint16> Hts;
	Hts.SetNumUninitialized(N * N);
	FMemory::Memcpy(Hts.GetData(), HRaw.GetData(), Hts.Num() * sizeof(uint16));
	HRaw.Empty();
	const double Span = (N - 1) * Step;
	auto GroundAt = [&](double X, double Z) {
		const double U = FMath::Clamp((X - X0) / Step, 0.0, N - 1.001), V = FMath::Clamp((Z1 - Z) / Step, 0.0, N - 1.001);
		const int32 I = FMath::FloorToInt32(U), J = FMath::FloorToInt32(V);
		const double FU = U - I, FV = V - J;
		auto H = [&](int32 A, int32 B) { return double(Hts[B * N + A]); };
		return Base + FMath::Lerp(FMath::Lerp(H(I, J), H(I + 1, J), FU), FMath::Lerp(H(I, J + 1), H(I + 1, J + 1), FU), FV) * Range / 65535.0;
	};
	const TSharedPtr<FJsonObject> Sf = Play->GetObjectField(TEXT("surfaces"));
	const double SMinX = Sf->GetNumberField(TEXT("minX")), SMaxZ = Sf->GetNumberField(TEXT("maxZ")), SRes = Sf->GetNumberField(TEXT("res"));
	int32 SW = 0, SH = 0;
	TArray64<uint8> Codes;
	if (!otg::ReadGreyPng(FPaths::Combine(Dir, Sf->GetStringField(TEXT("file"))), 8, SW, SH, Codes)) return Fail(TEXT("its surfaces don't read"));

	// ---- what changed
	auto Kinds = [](const FOTGCourseSource& S, const TCHAR* K) { FKind O; for (const FOTGSourceShape& Sh : S.Shapes) if (Sh.Kind == K) O.Add(Sh); return O; };
	TArray<FBox2D> Changed;                 // (round the shapes that changed: their old and new places)
	TArray<const FOTGSourceShape*> NewBunkers;
	TSet<FString> Full;
	auto Diff = [&](const TCHAR* K, bool bPatch) {
		TArray<const FOTGSourceShape*> A, B;
		for (const FOTGSourceShape& S : Before.Shapes) if (S.Kind == K) A.Add(&S);
		for (const FOTGSourceShape& S : After.Shapes) if (S.Kind == K) B.Add(&S);
		bool bAny = false;
		for (const FOTGSourceShape* S : A) if (!B.ContainsByPredicate([&](const FOTGSourceShape* T) { return Same(*S, *T); })) { bAny = true; if (bPatch) Changed.Add(BoxOf(S->Pts)); }
		for (const FOTGSourceShape* S : B) if (!A.ContainsByPredicate([&](const FOTGSourceShape* T) { return Same(*S, *T); }))
		{
			bAny = true;
			if (bPatch) Changed.Add(BoxOf(S->Pts));
			if (FCString::Strcmp(K, TEXT("bunker")) == 0 && !S->bInner) NewBunkers.Add(S);
		}
		return bAny;
	};
	for (const TCHAR* K : { TEXT("green"), TEXT("fairway"), TEXT("tee"), TEXT("bunker") }) Diff(K, true);
	// ...and what the game's folder has that the source no longer does, or lacks that it has: a change
	// saved that never reached the game (saved only, or the editor closed first) - found by looking at
	// the surfaces, not by what changed since the editor opened
	int32 Healed = 0;
	{
		const FKind Gr0 = Kinds(After, TEXT("green")), Bu0 = Kinds(After, TEXT("bunker")), Te0 = Kinds(After, TEXT("tee"));
		const double Tol = SRes * 1.5 + 0.3, TileM = 24;
		TSet<FIntPoint> Tiles;
		// (sand, green or tee where the source has none)
		for (int32 j = 0; j < SH; j += 2)
			for (int32 i = 0; i < SW; i += 2)
			{
				const uint8 C = Codes[int64(j) * SW + i];
				const FKind* K = C == Sand ? &Bu0 : C == Green ? &Gr0 : C == Tee ? &Te0 : nullptr;
				if (!K) continue;
				const double X = SMinX + (i + 0.5) * SRes, Z = SMaxZ - (j + 0.5) * SRes;
				if (K->Inside(X, Z) || K->OutsideDist(X, Z, Tol) < Tol) continue;
				Tiles.Add(FIntPoint(FMath::FloorToInt32((X - SMinX) / TileM), FMath::FloorToInt32((SMaxZ - Z) / TileM)));
			}
		for (const FIntPoint& T : Tiles)
			Changed.Add(FBox2D(FVector2D(SMinX + T.X * TileM, SMaxZ - (T.Y + 1) * TileM), FVector2D(SMinX + (T.X + 1) * TileM, SMaxZ - T.Y * TileM)));
		Healed += Tiles.Num();
		// (a source bunker, green or tee not drawn so yet: a quarter of its inside, or more, something else)
		for (const FOTGSourceShape& S : After.Shapes)
		{
			const bool bBunker = S.Kind == TEXT("bunker"), bGreen = S.Kind == TEXT("green"), bTee = S.Kind == TEXT("tee");
			if (S.bInner || !(bBunker || bGreen || bTee) || S.Pts.Num() < 3) continue;
			const FBox2D B = BoxOf(S.Pts);
			const int32 I0 = FMath::Max(0, FMath::FloorToInt32((B.Min.X - SMinX) / SRes)), I1 = FMath::Min(SW - 1, FMath::CeilToInt32((B.Max.X - SMinX) / SRes));
			const int32 J0 = FMath::Max(0, FMath::FloorToInt32((SMaxZ - B.Max.Y) / SRes)), J1 = FMath::Min(SH - 1, FMath::CeilToInt32((SMaxZ - B.Min.Y) / SRes));
			int32 Seen = 0, Miss = 0;
			for (int32 j = J0; j <= J1; j += 2)
				for (int32 i = I0; i <= I1; i += 2)
				{
					const double X = SMinX + (i + 0.5) * SRes, Z = SMaxZ - (j + 0.5) * SRes;
					if (!InPoly(S.Pts, X, Z) || EdgeDist(S.Pts, X, Z) < Tol) continue;
					const uint8 C = Codes[int64(j) * SW + i];
					if (C == None || C == Oob || C == Water || C == Path) continue;
					Seen++;
					const bool bOk = bBunker ? C == Sand : bGreen ? (C == Green || C == Sand) : (C == Tee || C == Sand || C == Green || C == Fringe);
					if (!bOk) Miss++;
				}
			if (Miss >= 3 && Miss * 4 >= Seen)
			{
				Changed.Add(B);
				if (bBunker) NewBunkers.AddUnique(&S);
				Healed++;
			}
		}
	}
	if (Diff(TEXT("water"), false)) Full.Add(TEXT("water"));
	if (Diff(TEXT("oob"), false)) Full.Add(TEXT("the boundary"));
	if (Diff(TEXT("penalty"), false)) Full.Add(TEXT("penalty areas"));
	{
		// tree areas: new ones planted here; changed or taken away: the full export
		TArray<const FOTGSourceShape*> A, B;
		for (const FOTGSourceShape& S : Before.Shapes) if (S.Kind == TEXT("trees")) A.Add(&S);
		for (const FOTGSourceShape& S : After.Shapes) if (S.Kind == TEXT("trees")) B.Add(&S);
		for (const FOTGSourceShape* S : A) if (!B.ContainsByPredicate([&](const FOTGSourceShape* T) { return Same(*S, *T); })) Full.Add(TEXT("tree areas changed or taken away"));
	}
	if (Before.BuildingTypes.OrderIndependentCompareEqual(After.BuildingTypes) == false) Full.Add(TEXT("building types"));
	if (!(Before.TreesOff.Num() == After.TreesOff.Num() && Before.TreesOff.Includes(After.TreesOff))) Full.Add(TEXT("tree species"));
	if (Before.bPhotoFairway != After.bPhotoFairway || Before.bPhotoGreen != After.bPhotoGreen || Before.bPhotoRough != After.bPhotoRough || Before.MowFairway != After.MowFairway
		|| Before.MowGreen != After.MowGreen || !(Before.PlantsOff.Num() == After.PlantsOff.Num() && Before.PlantsOff.Includes(After.PlantsOff))
		|| !(Before.PlantsExtra.Num() == After.PlantsExtra.Num() && Before.PlantsExtra.Includes(After.PlantsExtra)) || Before.PlantAmount != After.PlantAmount)
		Full.Add(TEXT("the course's looks"));

	// ---- the play surfaces, round what changed (course.js surfaceAt, for the map's shapes)
	const FKind Gr = Kinds(After, TEXT("green")), Fw = Kinds(After, TEXT("fairway")), Te = Kinds(After, TEXT("tee")), Bu = Kinds(After, TEXT("bunker")), Wa = Kinds(After, TEXT("water"));
	int32 Cells = 0;
	for (FBox2D B : Changed)
	{
		B = B.ExpandBy(FirstCutM + SdfM + 1);
		const int32 I0 = FMath::Max(0, FMath::FloorToInt32((B.Min.X - SMinX) / SRes)), I1 = FMath::Min(SW - 1, FMath::CeilToInt32((B.Max.X - SMinX) / SRes));
		const int32 J0 = FMath::Max(0, FMath::FloorToInt32((SMaxZ - B.Max.Y) / SRes)), J1 = FMath::Min(SH - 1, FMath::CeilToInt32((SMaxZ - B.Min.Y) / SRes));
		for (int32 j = J0; j <= J1; j++)
			for (int32 i = I0; i <= I1; i++)
			{
				const double X = SMinX + (i + 0.5) * SRes, Z = SMaxZ - (j + 0.5) * SRes;
				uint8& C = Codes[int64(j) * SW + i];
				const uint8 Was = C;
				if (Was == None || Was == Oob || Was == Water) continue;   // (off the course, out of bounds, the water: as they were)
				uint8 Now;
				if (Bu.Inside(X, Z)) Now = Sand;
				else if (Gr.Inside(X, Z)) Now = Green;
				else if (Was == Path) Now = Path;
				else if (Gr.OutsideDist(X, Z, FringeM + 0.1) < FringeM) Now = Fringe;
				else if (Te.Inside(X, Z)) Now = Tee;
				else if (Fw.Inside(X, Z)) Now = Fairway;
				else if (Fw.OutsideDist(X, Z, FirstCutM + 0.1) < FirstCutM) Now = FirstCut;
				else Now = (Was == Sand || Was == Green || Was == Fringe || Was == Tee || Was == Fairway || Was == FirstCut) ? uint8(Rough) : Was;
				if (Now != Was) { C = Now; Cells++; }
			}
	}

	// ---- tall grass painted or rubbed out (strokes added since: the export has the ones before)
	bool bStrokes = After.TallGrass.Num() > Before.TallGrass.Num();
	for (int32 k = 0; k < Before.TallGrass.Num() && bStrokes; k++)
		if (!After.TallGrass.IsValidIndex(k) || After.TallGrass[k].Pts != Before.TallGrass[k].Pts || After.TallGrass[k].bErase != Before.TallGrass[k].bErase) bStrokes = false;
	if (After.TallGrass.Num() != Before.TallGrass.Num() && !bStrokes) Full.Add(TEXT("tall grass taken back"));
	if (bStrokes)
		for (int32 k = Before.TallGrass.Num(); k < After.TallGrass.Num(); k++)
		{
			const FOTGGrassStroke& St = After.TallGrass[k];
			for (int32 p = 0; p < St.Pts.Num(); p++)
			{
				// (each piece of the stroke, as wide as the brush)
				const FVector2D A = St.Pts[p], Bp = St.Pts[FMath::Min(p + 1, St.Pts.Num() - 1)];
				FBox2D Bx(ForceInit); Bx += A; Bx += Bp; Bx = Bx.ExpandBy(St.R);
				const int32 I0 = FMath::Max(0, FMath::FloorToInt32((Bx.Min.X - SMinX) / SRes)), I1 = FMath::Min(SW - 1, FMath::CeilToInt32((Bx.Max.X - SMinX) / SRes));
				const int32 J0 = FMath::Max(0, FMath::FloorToInt32((SMaxZ - Bx.Max.Y) / SRes)), J1 = FMath::Min(SH - 1, FMath::CeilToInt32((SMaxZ - Bx.Min.Y) / SRes));
				for (int32 j = J0; j <= J1; j++)
					for (int32 i = I0; i <= I1; i++)
					{
						const FVector2D Q(SMinX + (i + 0.5) * SRes, SMaxZ - (j + 0.5) * SRes);
						const FVector2D D = Bp - A;
						const double T = D.SizeSquared() > 0 ? FMath::Clamp(FVector2D::DotProduct(Q - A, D) / D.SizeSquared(), 0.0, 1.0) : 0.0;
						if (FVector2D::Distance(Q, A + D * T) > St.R) continue;
						uint8& C = Codes[int64(j) * SW + i];
						if (!St.bErase && (C == Rough || C == DeepRough)) { C = Native; Cells++; }
						else if (St.bErase && (C == Native || C == DeepRough)) { C = Rough; Cells++; }
					}
			}
		}
	if (!WritePng(FPaths::Combine(Dir, Sf->GetStringField(TEXT("file"))), Codes.GetData(), Codes.Num(), SW, SH, ERGBFormat::Gray, 8)) return Fail(TEXT("couldn't write its surfaces"));

	// ---- the surface masks, round what changed (exact distances: 0.5 on the outline, 0 and 1 3 m out and in)
	if (Changed.Num())
	{
		TMap<FString, TArray64<uint8>> L;
		int32 M = 0;
		for (const TCHAR* K : { TEXT("green"), TEXT("fairway"), TEXT("tee"), TEXT("bunker"), TEXT("water"), TEXT("rough") })
		{
			int32 W = 0, H = 0;
			TArray64<uint8>& Px = L.Add(K);
			if (!otg::ReadGreyPng(FPaths::Combine(Dir, FString::Printf(TEXT("layer_%s.png"), K)), 8, W, H, Px) || W != H) return Fail(FString::Printf(TEXT("its %s mask doesn't read"), K));
			M = W;
		}
		const double Ms = Span / M;
		auto Field = [](double Signed) { return FMath::Clamp(0.5 + Signed / (2 * SdfM), 0.0, 1.0); };
		for (FBox2D B : Changed)
		{
			B = B.ExpandBy(SdfM + FringeM + 1);
			const int32 I0 = FMath::Max(0, FMath::FloorToInt32((B.Min.X - X0) / Ms)), I1 = FMath::Min(M - 1, FMath::CeilToInt32((B.Max.X - X0) / Ms));
			const int32 J0 = FMath::Max(0, FMath::FloorToInt32((Z1 - B.Max.Y) / Ms)), J1 = FMath::Min(M - 1, FMath::CeilToInt32((Z1 - B.Min.Y) / Ms));
			for (int32 j = J0; j <= J1; j++)
				for (int32 i = I0; i <= I1; i++)
				{
					const double X = X0 + (i + 0.5) * Ms, Z = Z1 - (j + 0.5) * Ms;
					const int64 Q = int64(j) * M + i;
					const double G = Field(Gr.Signed(X, Z, SdfM + FringeM)), T = Field(Te.Signed(X, Z, SdfM)), Bk = Field(Bu.Signed(X, Z, SdfM));
					// (the fairways, and a fringe of fairway-cut grass round every green, where it isn't green)
					const double Fr = FMath::Clamp(0.5 + (Gr.Signed(X, Z, SdfM + FringeM) + FringeM) / (2 * SdfM), 0.0, 1.0);
					const double F = FMath::Max(Field(Fw.Signed(X, Z, SdfM)), FMath::Min(Fr, 1 - G));
					const double Wt = L[TEXT("water")][Q] / 255.0;
					const double Ro = 1 - FMath::Max(FMath::Max(F, G), FMath::Max(FMath::Max(T, Bk), Wt));
					auto Put = [&](const TCHAR* K, double V) { L[K][Q] = uint8(FMath::Clamp(FMath::RoundToInt(V * 255), 0, 255)); };
					Put(TEXT("green"), G); Put(TEXT("fairway"), F); Put(TEXT("tee"), T); Put(TEXT("bunker"), Bk); Put(TEXT("rough"), Ro);
				}
		}
		for (const TCHAR* K : { TEXT("green"), TEXT("fairway"), TEXT("tee"), TEXT("bunker"), TEXT("rough") })
			if (!WritePng(FPaths::Combine(Dir, FString::Printf(TEXT("layer_%s.png"), K)), L[K].GetData(), L[K].Num(), M, M, ERGBFormat::Gray, 8)) return Fail(FString::Printf(TEXT("couldn't write its %s mask"), K));
	}

	// ---- a new or moved bunker's hollow: up to 45 cm down, easing in over its first 1.5 m
	if (NewBunkers.Num())
	{
		for (const FOTGSourceShape* S : NewBunkers)
		{
			FKind One; One.Add(*S);
			const FBox2D B = BoxOf(S->Pts);
			const int32 I0 = FMath::Max(0, FMath::FloorToInt32((B.Min.X - X0) / Step)), I1 = FMath::Min(N - 1, FMath::CeilToInt32((B.Max.X - X0) / Step));
			const int32 J0 = FMath::Max(0, FMath::FloorToInt32((Z1 - B.Max.Y) / Step)), J1 = FMath::Min(N - 1, FMath::CeilToInt32((Z1 - B.Min.Y) / Step));
			for (int32 j = J0; j <= J1; j++)
				for (int32 i = I0; i <= I1; i++)
				{
					const double D = One.Signed(X0 + i * Step, Z1 - j * Step, 2);
					if (D <= 0) continue;
					const double Down = 0.45 * FMath::SmoothStep(0.0, 1.5, D);
					uint16& V = Hts[j * N + i];
					V = uint16(FMath::Clamp(int32(V) - FMath::RoundToInt(Down / Range * 65535), 0, 65535));
				}
		}
		if (!WritePng(FPaths::Combine(Dir, Hm->GetStringField(TEXT("file"))), Hts.GetData(), int64(Hts.Num()) * 2, N, N, ERGBFormat::Gray, 16)) return Fail(TEXT("couldn't write its heightmap"));
	}

	// ---- the trees: removed ones out, your own and new tree areas' in (trees.json and play.json alike)
	TArray<TSharedPtr<FJsonValue>> PlayTrees = Play->GetArrayField(TEXT("trees"));
	const TSharedPtr<FJsonValue> TJ = ReadJsonValue(FPaths::Combine(Dir, TEXT("trees.json")));
	TArray<TSharedPtr<FJsonValue>> DrawTrees;
	if (TJ.IsValid() && TJ->Type == EJson::Array) DrawTrees = TJ->AsArray();
	const bool bTreesPaired = DrawTrees.Num() == PlayTrees.Num();
	int32 TreesOut = 0, TreesIn = 0;
	{
		// (a tree in the game's lists within D metres of P)
		auto InGame = [&](const FVector2D& P, double D) {
			for (const TSharedPtr<FJsonValue>& V : PlayTrees)
			{
				const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
				if (A.Num() >= 2 && FVector2D::DistSquared(P, FVector2D(A[0]->AsNumber(), A[1]->AsNumber())) < D * D) return true;
			}
			return false;
		};
		TArray<FVector2D> Gone;   // (where trees go: removed ones the game still has, your own moved or taken away)
		for (const FVector2D& P : After.RemovedTrees) if (InGame(P, 0.5)) Gone.Add(P);
		for (const FVector2D& P : Before.RemovedTrees) if (!After.RemovedTrees.ContainsByPredicate([&](const FVector2D& Q) { return FVector2D::Distance(P, Q) < 0.2; })) Full.Add(TEXT("trees put back"));
		TArray<FOTGSourceTree> Added;
		for (const FOTGSourceTree& T : Before.UserTrees)
			if (!After.UserTrees.ContainsByPredicate([&](const FOTGSourceTree& U) { return U.P == T.P && U.Size == T.Size && U.Kind == T.Kind; })) Gone.Add(T.P);
		// (your own: new or changed since, or not in the game yet)
		for (const FOTGSourceTree& T : After.UserTrees)
			if (!Before.UserTrees.ContainsByPredicate([&](const FOTGSourceTree& U) { return U.P == T.P && U.Size == T.Size && U.Kind == T.Kind; }) || !InGame(T.P, 1.0)) Added.Add(T);
		// (new tree areas: planted on a grid as dense as the area, jittered, kept off the mown grass)
		for (const FOTGSourceShape& S : After.Shapes)
		{
			if (S.Kind != TEXT("trees") || Before.Shapes.ContainsByPredicate([&](const FOTGSourceShape& T) { return Same(S, T); })) continue;
			const double Sp = S.Density == TEXT("sparse") ? 14 : S.Density == TEXT("dense") ? 6 : 9;
			const FBox2D B = BoxOf(S.Pts);
			for (double Z = B.Min.Y + Sp / 2; Z < B.Max.Y; Z += Sp)
				for (double X = B.Min.X + Sp / 2; X < B.Max.X; X += Sp)
				{
					const double JX = X + (FOTGTreeDesign::Hash01(X, Z, 1) - 0.5) * Sp * 0.8, JZ = Z + (FOTGTreeDesign::Hash01(X, Z, 2) - 0.5) * Sp * 0.8;
					if (!InPoly(S.Pts, JX, JZ)) continue;
					const int32 I = FMath::FloorToInt32((JX - SMinX) / SRes), J = FMath::FloorToInt32((SMaxZ - JZ) / SRes);
					const uint8 C = I >= 0 && J >= 0 && I < SW && J < SH ? Codes[int64(J) * SW + I] : uint8(Rough);
					if (C == Water || C == Green || C == Fringe || C == Fairway || C == Tee || C == FirstCut || C == Path || C == Sand) continue;
					FOTGSourceTree T;
					T.P = FVector2D(JX, JZ);
					const double Lo = FMath::IsFinite(S.HMin) ? S.HMin : 10, Hi = FMath::IsFinite(S.HMax) ? S.HMax : 18;
					T.Size = FString::SanitizeFloat(FMath::Lerp(Lo, Hi, FOTGTreeDesign::Hash01(JX, JZ, 3)));   // (a height, m)
					T.Kind = S.Species;
					Added.Add(T);
				}
		}
		// out: the nearest tree to each place (within 2 m), from both lists at its index
		for (const FVector2D& P : Gone)
		{
			int32 Best = INDEX_NONE;
			double BestD = 2.0;
			for (int32 i = 0; i < PlayTrees.Num(); i++)
			{
				const TArray<TSharedPtr<FJsonValue>>& A = PlayTrees[i]->AsArray();
				const double D = FVector2D::Distance(P, FVector2D(A[0]->AsNumber(), A[1]->AsNumber()));
				if (D < BestD) { BestD = D; Best = i; }
			}
			if (Best == INDEX_NONE) continue;
			PlayTrees.RemoveAt(Best);
			if (bTreesPaired) DrawTrees.RemoveAt(Best);
			TreesOut++;
		}
		// in: each as the game shapes its trees for the ball (a trunk, and a crown: an ellipsoid, or a cone for conifers)
		for (const FOTGSourceTree& T : Added)
		{
			const double X = T.P.X, Z = T.P.Y, G = GroundAt(X, Z);
			const FString Kind = KindFor(T.Kind, X, Z);
			double H = T.Size == TEXT("small") ? 8 : T.Size == TEXT("large") ? 20 : T.Size == TEXT("medium") ? 13 : FCString::Atod(*T.Size);
			if (!(H > 1)) H = 13;
			const bool bCone = Conifer(Kind);
			const double Rc = H * (bCone ? 0.22 : 0.32);
			auto Num = [](double V) { return MakeShared<FJsonValueNumber>(FMath::RoundToDouble(V * 1000) / 1000); };
			TArray<TSharedPtr<FJsonValue>> A = { Num(X), Num(Z), Num(G + H * (bCone ? 0.2 : 0.45)), Num(FMath::Max(0.15, H * 0.025)), MakeShared<FJsonValueString>(Kind), Num(bCone ? 1 : 0) };
			if (bCone) A.Append({ Num(G + H * 0.15), Num(H * 0.85), Num(Rc) });
			else A.Append({ Num(G + H * 0.62), Num(Rc), Num(H * 0.38) });
			PlayTrees.Add(MakeShared<FJsonValueArray>(A));
			const TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
			D->SetNumberField(TEXT("x"), FMath::RoundToDouble((X - X0) * 100));
			D->SetNumberField(TEXT("y"), FMath::RoundToDouble((Z1 - Z) * 100));
			D->SetNumberField(TEXT("z"), FMath::RoundToDouble((G - Base) * 100));
			D->SetNumberField(TEXT("h"), FMath::RoundToDouble(H * 100));
			D->SetNumberField(TEXT("r"), FMath::RoundToDouble(Rc * 100));
			D->SetStringField(TEXT("kind"), Kind);
			if (bTreesPaired) DrawTrees.Add(MakeShared<FJsonValueObject>(D));
			TreesIn++;
		}
		if (!bTreesPaired && (TreesOut || TreesIn)) Full.Add(TEXT("trees drawn (its tree lists disagree)"));
		Play->SetArrayField(TEXT("trees"), PlayTrees);
		if (bTreesPaired && (TreesOut || TreesIn) && !WriteJson(FPaths::Combine(Dir, TEXT("trees.json")), MakeShared<FJsonValueArray>(DrawTrees), false)) return Fail(TEXT("couldn't write its trees"));
	}

	// ---- the holes (course.js: the path from the back tee through the aim points to the green; the
	// green's radius from its pins), in play.json and holes.json
	TArray<TSharedPtr<FJsonValue>> PH, HJ;
	TArray<FOTGSourceHole> Holes = After.Holes;
	Holes.StableSort([](const FOTGSourceHole& A, const FOTGSourceHole& B) { return A.Number < B.Number; });
	auto PT = [&](const FVector2D& P) {
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), FMath::RoundToDouble(P.X * 1000) / 1000);
		O->SetNumberField(TEXT("y"), FMath::RoundToDouble(GroundAt(P.X, P.Y) * 1000) / 1000);
		O->SetNumberField(TEXT("z"), FMath::RoundToDouble(P.Y * 1000) / 1000);
		return MakeShared<FJsonValueObject>(O);
	};
	auto UE = [&](const FVector2D& P) {
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), FMath::RoundToDouble((P.X - X0) * 100));
		O->SetNumberField(TEXT("y"), FMath::RoundToDouble((Z1 - P.Y) * 100));
		O->SetNumberField(TEXT("z"), FMath::RoundToDouble((GroundAt(P.X, P.Y) - Base) * 100));
		return MakeShared<FJsonValueObject>(O);
	};
	for (const FOTGSourceHole& H : Holes)
	{
		// (the back tee: the longest)
		const FVector2D* Back = nullptr;
		double Longest = -1;
		for (const auto& T : H.Tees)
		{
			TArray<FVector2D> L = { T.Value }; L.Append(H.Aims); L.Add(H.Green);
			double D = 0; for (int32 k = 1; k < L.Num(); k++) D += FVector2D::Distance(L[k - 1], L[k]);
			if (D > Longest) { Longest = D; Back = &T.Value; }
		}
		double MaxPin = 0;
		for (const auto& P : H.Pins) MaxPin = FMath::Max(MaxPin, FVector2D::Distance(P.Value, H.Green));
		const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>(), U = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("number"), H.Number); U->SetNumberField(TEXT("number"), H.Number);
		O->SetNumberField(TEXT("par"), H.Par); U->SetNumberField(TEXT("par"), H.Par);
		if (H.Index > 0) { O->SetNumberField(TEXT("index"), H.Index); U->SetNumberField(TEXT("index"), H.Index); }
		else { O->SetField(TEXT("index"), MakeShared<FJsonValueNull>()); }
		O->SetNumberField(TEXT("greenR"), FMath::RoundToDouble(FMath::Clamp(MaxPin / 0.82 + 3, 12.0, 24.0) * 1000) / 1000);
		const TSharedRef<FJsonObject> Tees = MakeShared<FJsonObject>(), UTees = MakeShared<FJsonObject>(), Pins = MakeShared<FJsonObject>(), UPins = MakeShared<FJsonObject>();
		for (const FString& T : FOTGCourseSource::TeesOrder()) if (const FVector2D* P = H.Tees.Find(T)) { Tees->SetField(T, PT(*P)); UTees->SetField(T, UE(*P)); }
		for (const FString& D : FOTGCourseSource::PinDays()) { const FVector2D P = H.Pins.Contains(D) ? H.Pins[D] : H.Green; Pins->SetField(D, PT(P)); UPins->SetField(D, UE(P)); }
		O->SetObjectField(TEXT("tees"), Tees); U->SetObjectField(TEXT("tees"), UTees);
		O->SetObjectField(TEXT("pins"), Pins); U->SetObjectField(TEXT("pins"), UPins);
		O->SetField(TEXT("greenCenter"), PT(H.Green)); U->SetField(TEXT("green"), UE(H.Green));
		TArray<TSharedPtr<FJsonValue>> Aims, UAims, Path;
		for (const FVector2D& A : H.Aims) { Aims.Add(PT(A)); UAims.Add(UE(A)); }
		if (Back) Path.Add(PT(*Back));
		Path.Append(Aims);
		Path.Add(PT(H.Green));
		O->SetArrayField(TEXT("aims"), Aims); U->SetArrayField(TEXT("aims"), UAims);
		O->SetArrayField(TEXT("path"), Path);
		PH.Add(MakeShared<FJsonValueObject>(O));
		HJ.Add(MakeShared<FJsonValueObject>(U));
	}
	Play->SetArrayField(TEXT("holes"), PH);
	Play->SetStringField(TEXT("name"), After.Name);
	Play->SetStringField(TEXT("location"), After.Location);
	Play->SetNumberField(TEXT("defaultStimp"), After.Stimp);
	if (!WriteJson(FPaths::Combine(Dir, TEXT("holes.json")), MakeShared<FJsonValueArray>(HJ), true)) return Fail(TEXT("couldn't write its holes"));

	// ---- the masks derived from the play data (derive-layers.js): native grass and under the trees'
	// crowns, the fringes, the mowing directions
	{
		const int32 S = int32(LayerSize);
		const double Mpp = Span / S;
		TArray<uint8> Sub;   // (each pixel's surface code; 255 off the raster)
		Sub.SetNumUninitialized(S * S);
		TArray<int32> Col;
		Col.SetNum(S);
		for (int32 i = 0; i < S; i++) { const int32 C = FMath::FloorToInt32((X0 + (i + 0.5) * Mpp - SMinX) / SRes); Col[i] = C >= 0 && C < SW ? C : -1; }
		for (int32 j = 0; j < S; j++)
		{
			const int32 Rw = FMath::FloorToInt32((SMaxZ - (Z1 - (j + 0.5) * Mpp)) / SRes);
			for (int32 i = 0; i < S; i++) Sub[j * S + i] = Rw < 0 || Rw >= SH || Col[i] < 0 ? 255 : Codes[int64(Rw) * SW + Col[i]];
		}
		auto Bytes = [](const TArray<float>& F) { TArray<uint8> B; B.SetNumUninitialized(F.Num()); for (int32 q = 0; q < F.Num(); q++) B[q] = uint8(FMath::Clamp(FMath::FloorToInt32(F[q] * 255 + 0.5f), 0, 255)); return B; };
		// native: deep rough and native grass, and under each crown (0.85 of it), never on the mown, a path, sand or water
		TArray<float> Nat;
		Nat.SetNumZeroed(S * S);
		for (int32 q = 0; q < S * S; q++) Nat[q] = Sub[q] == DeepRough || Sub[q] == Native ? 1.f : 0.f;
		for (const TSharedPtr<FJsonValue>& V : PlayTrees)
		{
			const TArray<TSharedPtr<FJsonValue>>& T = V->AsArray();
			const double X = T[0]->AsNumber(), Z = T[1]->AsNumber(), Rr = (T[5]->AsNumber() ? T[8]->AsNumber() : T[7]->AsNumber()) * 0.85;
			if (!(Rr > 0)) continue;
			const double Ci = (X - X0) / Mpp, Cj = (Z1 - Z) / Mpp, Rp = Rr / Mpp;
			const int32 I0 = FMath::Max(0, int32(Ci - Rp)), I1 = FMath::Min(S - 1, int32(Ci + Rp) + 1), J0 = FMath::Max(0, int32(Cj - Rp)), J1 = FMath::Min(S - 1, int32(Cj + Rp) + 1);
			for (int32 j = J0; j <= J1; j++) for (int32 i = I0; i <= I1; i++) if (FMath::Square(i + 0.5 - Ci) + FMath::Square(j + 0.5 - Cj) <= Rp * Rp) Nat[j * S + i] = 1.f;
		}
		for (int32 q = 0; q < S * S; q++)
		{
			const uint8 C = Sub[q];
			if (C != 255 && (C == Water || C == Green || C == Fringe || C == Fairway || C == Tee || C == FirstCut || C == Path || C == Sand)) Nat[q] = 0;
		}
		const int32 Rb = FMath::Max(1, FMath::RoundToInt32(1.2 / Mpp));
		for (int32 k = 0; k < 3; k++) BoxBlur(Nat, S, Rb);
		{
			const TArray<uint8> B = Bytes(Nat);
			if (!WritePng(FPaths::Combine(Dir, TEXT("layer_native.png")), B.GetData(), B.Num(), S, S, ERGBFormat::Gray, 8)) return Fail(TEXT("couldn't write its native grass"));
		}
		// fringes, crisp edged
		{
			TArray<float> Fr;
			Fr.SetNumZeroed(S * S);
			for (int32 q = 0; q < S * S; q++) Fr[q] = Sub[q] == Fringe ? 1.f : 0.f;
			for (int32 k = 0; k < 3; k++) BoxBlur(Fr, S, 1);
			const TArray<uint8> B = Bytes(Fr);
			if (!WritePng(FPaths::Combine(Dir, TEXT("layer_fringe.png")), B.GetData(), B.Num(), S, S, ERGBFormat::Gray, 8)) return Fail(TEXT("couldn't write its fringes"));
		}
		// mowing: on and near the mown surfaces, along the nearest hole's line (from its farthest tee)
		{
			TArray<FVector4d> Segs;   // (ax, ay, vx, vy in metres east and south of the corner)
			for (const FOTGSourceHole& H : Holes)
			{
				if (!H.Tees.Num()) continue;
				FVector2D Far = H.Tees.CreateConstIterator()->Value;
				for (const auto& T : H.Tees) if (FVector2D::DistSquared(T.Value, H.Green) > FVector2D::DistSquared(Far, H.Green)) Far = T.Value;
				TArray<FVector2D> P = { Far }; P.Append(H.Aims); P.Add(H.Green);
				for (int32 k = 0; k + 1 < P.Num(); k++) Segs.Add(FVector4d(P[k].X - X0, Z1 - P[k].Y, P[k + 1].X - P[k].X, -(P[k + 1].Y - P[k].Y)));
			}
			TArray<float> Mown;
			Mown.SetNumZeroed(S * S);
			for (int32 q = 0; q < S * S; q++) Mown[q] = Sub[q] == Fairway || Sub[q] == Green || Sub[q] == Tee || Sub[q] == Fringe ? 1.f : 0.f;
			BoxBlur(Mown, S, 6);
			TArray<uint8> RGBA;
			RGBA.SetNumUninitialized(S * S * 4);
			for (int32 q = 0; q < S * S; q++) { RGBA[q * 4] = 255; RGBA[q * 4 + 1] = 128; RGBA[q * 4 + 2] = 128; RGBA[q * 4 + 3] = 255; }
			auto Rnd = [](double V) { const double F = FMath::FloorToDouble(V), D = V - F; return int32(D > 0.5 || (D == 0.5 && FMath::Fmod(F, 2.0) != 0) ? F + 1 : F); };
			if (Segs.Num())
				for (int32 j = 0; j < S; j++)
					for (int32 i = 0; i < S; i++)
					{
						const int32 q = j * S + i;
						if (!(Mown[q] > 0)) continue;
						const double Px = (i + 0.5) * Mpp, Py = (j + 0.5) * Mpp;
						double Best = 1e18, Dx = 1, Dy = 0;
						for (const FVector4d& Sg : Segs)
						{
							const double L2 = FMath::Max(Sg.Z * Sg.Z + Sg.W * Sg.W, 1e-6);
							const double T = FMath::Clamp(((Px - Sg.X) * Sg.Z + (Py - Sg.Y) * Sg.W) / L2, 0.0, 1.0);
							const double D2 = FMath::Square(Px - Sg.X - T * Sg.Z) + FMath::Square(Py - Sg.Y - T * Sg.W);
							if (D2 < Best) { Best = D2; const double L = FMath::Sqrt(L2); Dx = Sg.Z / L; Dy = Sg.W / L; }
						}
						RGBA[q * 4] = uint8(FMath::Clamp(Rnd(127.5 + 127.5 * Dx), 0, 255));
						RGBA[q * 4 + 1] = uint8(FMath::Clamp(Rnd(127.5 + 127.5 * Dy), 0, 255));
					}
			if (!WritePng(FPaths::Combine(Dir, TEXT("layer_mow.png")), RGBA.GetData(), RGBA.Num(), S, S, ERGBFormat::RGBA, 8)) return Fail(TEXT("couldn't write its mowing"));
		}
	}

	// ---- play.json last (the game reads it first)
	if (!WriteJson(FPaths::Combine(Dir, TEXT("play.json")), MakeShared<FJsonValueObject>(Play.ToSharedRef()), false)) return Fail(TEXT("couldn't write its play data"));
	R.bOk = true;
	R.NeedsFull = Full.Array();
	R.Seconds = FPlatformTime::Seconds() - T0;
	R.Message = FString::Printf(TEXT("%d holes, %d surface cells, %d shapes' masks (%d not in the game before), %d trees out, %d in, %d bunker hollows"), Holes.Num(), Cells, Changed.Num(), Healed, TreesOut, TreesIn, NewBunkers.Num());
	UE_LOG(LogOTGQuick, Display, TEXT("Quick update %s: %s in %.1f s%s%s"), *Dir, *R.Message, R.Seconds, R.NeedsFull.Num() ? TEXT("; needs the full export for: ") : TEXT(""), *FString::Join(R.NeedsFull, TEXT(", ")));
	return R;
}
