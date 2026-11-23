// The houses round a course drawn by the Hearthline Homes plugin (Plugins/HearthlineHomes) in place
// of the export's plain blocks (props_building.glb, AOTGRuntimeCourse::BuildProps).
//
// - Which buildings: play.json's solids (the map's footprints, the ones the ball hits) that are
//   houses, guessed as the web version's buildingtypes.js classifyBuildings guesses them - the map's
//   building tag (house, detached, residential ...; or none), not the clubhouse (of the big ones,
//   the closest to where the round starts and turns), not a shed (tiny), not a car (low) - and
//   that a house fits: 60 to 650 m², 6 to 40 m a side, mostly filling the rectangle round it.
// - Each one: the rectangle round its footprint (the smallest), its front the side facing away
//   from the course (gardens back onto the fairways); a Hearthline style for its shape and height
//   (wide: ranch, colonial, Cape Cod, modern, split level; deep: craftsman, farmhouse, cottage), the
//   style's own house made from a seed of the building's key (so it's the same house every time),
//   then stretched to the rectangle. It stands on the lowest ground under it, its foundation
//   raised to the highest. Foundation shrubs; no lot (the photo has the lawn and the drive), no
//   collision (the ball's buildings are play.json's).
// - Hearthline makes a house as a dynamic mesh. Drawn so, each would be a dynamic mesh in the ray
//   tracing scene and the Lumen scene left out of both (OTGWater.cpp's procedural meshes: ~100 ms
//   a frame). So each is made once on one Hearthline actor, its mesh copied out, and the houses
//   merged - by the 300 m square they stand in - into static meshes made here, one section per
//   Hearthline surface.
// - The export's own blocks under them go (BuildProps); the other buildings stay as they were.
//
// -oldhouses: the export's own buildings everywhere, as before.

#include "World/OTGRuntimeCourse.h"
#include "Course/OTGCourseData.h"

#include "HearthlineHouse.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "MeshDescription.h"
#include "Misc/CommandLine.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGHouses, Log, All);

namespace
{
	constexpr double MinAreaM2 = 60, MaxAreaM2 = 650;   // (a house's footprint)
	constexpr double MinSideM = 6, MaxSideM = 40;
	constexpr double MinFill = 0.55;                    // (of the rectangle round it: L and T shapes, not odd ones)
	constexpr double MinHeightM = 2.5;                  // (lower: a parked car's box)
	constexpr double TwoStoreyM = 7.5;                  // (taller: a two-storey style only)
	constexpr double CellM = 300;                       // (the houses merged by squares this size)
	constexpr double MaxRiseCm = 250;                   // (the foundation's most: on steeper ground the block stays)

	// buildingtypes.js FROM_TAG: what the map's tag says it is ("" - nothing, or a building of no kind)
	FString TagType(const FString& Tag)
	{
		static const TMap<FString, FString> M = {
			{ TEXT("house"), TEXT("house") }, { TEXT("detached"), TEXT("house") }, { TEXT("residential"), TEXT("house") }, { TEXT("semidetached_house"), TEXT("house") },
			{ TEXT("terrace"), TEXT("house") }, { TEXT("bungalow"), TEXT("house") }, { TEXT("cabin"), TEXT("house") }, { TEXT("villa"), TEXT("house") },
			{ TEXT("farm"), TEXT("house") }, { TEXT("static_caravan"), TEXT("house") },
			{ TEXT("apartments"), TEXT("commercial") }, { TEXT("dormitory"), TEXT("commercial") }, { TEXT("garage"), TEXT("garage") }, { TEXT("garages"), TEXT("garage") },
			{ TEXT("shed"), TEXT("shed") }, { TEXT("hut"), TEXT("shed") }, { TEXT("storage_tank"), TEXT("shed") }, { TEXT("service"), TEXT("shed") },
			{ TEXT("toilets"), TEXT("shed") }, { TEXT("kiosk"), TEXT("shed") }, { TEXT("barn"), TEXT("barn") }, { TEXT("farm_auxiliary"), TEXT("barn") },
			{ TEXT("stable"), TEXT("barn") }, { TEXT("cowshed"), TEXT("barn") }, { TEXT("greenhouse"), TEXT("barn") }, { TEXT("roof"), TEXT("porch") },
			{ TEXT("carport"), TEXT("porch") }, { TEXT("shelter"), TEXT("porch") }, { TEXT("pavilion"), TEXT("porch") }, { TEXT("canopy"), TEXT("porch") },
			{ TEXT("clubhouse"), TEXT("clubhouse") },
			{ TEXT("commercial"), TEXT("commercial") }, { TEXT("retail"), TEXT("commercial") }, { TEXT("industrial"), TEXT("commercial") }, { TEXT("warehouse"), TEXT("commercial") },
			{ TEXT("office"), TEXT("commercial") }, { TEXT("school"), TEXT("commercial") }, { TEXT("hotel"), TEXT("commercial") }, { TEXT("church"), TEXT("commercial") },
			{ TEXT("civic"), TEXT("commercial") }, { TEXT("public"), TEXT("commercial") }, { TEXT("hospital"), TEXT("commercial") }, { TEXT("university"), TEXT("commercial") },
			{ TEXT("kindergarten"), TEXT("commercial") }, { TEXT("sports_hall"), TEXT("commercial") }, { TEXT("government"), TEXT("commercial") }, { TEXT("supermarket"), TEXT("commercial") },
		};
		const FString* T = M.Find(Tag.ToLower());
		return T ? *T : FString();   // (yes, building, anything else: guessed from its size, as the web version does)
	}

	struct FFoot
	{
		TArray<FVector2D> Pts;   // (course metres, x east, z north; not closed)
		double Area = 0, H = 0;
		FVector2D Centroid = FVector2D::ZeroVector;
		FString Type;
	};

	void Info(FFoot& F)
	{
		double A = 0, Cx = 0, Cz = 0;
		const int32 N = F.Pts.Num();
		for (int32 i = 0; i < N; i++)
		{
			const FVector2D& P = F.Pts[i];
			const FVector2D& Q = F.Pts[(i + 1) % N];
			const double C = P.X * Q.Y - Q.X * P.Y;
			A += C; Cx += (P.X + Q.X) * C; Cz += (P.Y + Q.Y) * C;
		}
		if (FMath::Abs(A) < 1e-6)
		{
			FVector2D S = FVector2D::ZeroVector;
			for (const FVector2D& P : F.Pts) S += P;
			F.Centroid = S / FMath::Max(N, 1);
			F.Area = 0;
			return;
		}
		F.Area = FMath::Abs(A) / 2;
		F.Centroid = FVector2D(Cx / (3 * A), Cz / (3 * A));
	}

	// The smallest rectangle round the points: centre, its axis U (V square to it), half sizes along each
	struct FRect { FVector2D C, U, V; double HalfU = 0, HalfV = 0; };

	FRect MinRect(const TArray<FVector2D>& In)
	{
		// (the convex hull: monotone chain)
		TArray<FVector2D> P = In;
		P.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
		auto Cross = [](const FVector2D& O, const FVector2D& A, const FVector2D& B) { return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X); };
		TArray<FVector2D> H;
		for (int32 i = 0; i < P.Num(); i++)
		{
			while (H.Num() >= 2 && Cross(H[H.Num() - 2], H.Last(), P[i]) <= 0) H.Pop();
			H.Add(P[i]);
		}
		for (int32 i = P.Num() - 2, Lo = H.Num() + 1; i >= 0; i--)
		{
			while (H.Num() >= Lo && Cross(H[H.Num() - 2], H.Last(), P[i]) <= 0) H.Pop();
			H.Add(P[i]);
		}
		if (H.Num() > 1) H.Pop();
		FRect Best;
		double BestA = TNumericLimits<double>::Max();
		for (int32 i = 0; i < H.Num(); i++)
		{
			const FVector2D E = H[(i + 1) % H.Num()] - H[i];
			if (E.SizeSquared() < 1e-6) continue;
			const FVector2D U = E.GetSafeNormal(), V(-U.Y, U.X);
			double U0 = TNumericLimits<double>::Max(), U1 = -U0, V0 = U0, V1 = -U0;
			for (const FVector2D& Q : H)
			{
				const double a = FVector2D::DotProduct(Q, U), b = FVector2D::DotProduct(Q, V);
				U0 = FMath::Min(U0, a); U1 = FMath::Max(U1, a); V0 = FMath::Min(V0, b); V1 = FMath::Max(V1, b);
			}
			const double Ar = (U1 - U0) * (V1 - V0);
			if (Ar < BestA)
			{
				BestA = Ar;
				Best.U = U; Best.V = V; Best.HalfU = (U1 - U0) / 2; Best.HalfV = (V1 - V0) / 2;
				Best.C = U * ((U0 + U1) / 2) + V * ((V0 + V1) / 2);
			}
		}
		return Best;
	}

	// (FHearthlineMaterialSet's Get and SlotName, which the plugin doesn't export: its surfaces in HearthlineSlot's order)
	UMaterialInterface* SlotMat(const FHearthlineMaterialSet& S, int32 Slot)
	{
		UMaterialInterface* const M[FHearthlineMaterialSet::NumSlots] = { S.LapSiding, S.BoardAndBatten, S.Brick, S.Stucco, S.FieldStone, S.Concrete,
			S.Shingles, S.Trim, S.Wood, S.Glass, S.Metal, S.Asphalt, S.Grass, S.Foliage };
		return M[FMath::Clamp(Slot, 0, FHearthlineMaterialSet::NumSlots - 1)];
	}
	FName SlotName(int32 Slot)
	{
		static const TCHAR* N[FHearthlineMaterialSet::NumSlots] = { TEXT("LapSiding"), TEXT("BoardAndBatten"), TEXT("Brick"), TEXT("Stucco"), TEXT("FieldStone"), TEXT("Concrete"),
			TEXT("Shingles"), TEXT("Trim"), TEXT("Wood"), TEXT("Glass"), TEXT("Metal"), TEXT("Asphalt"), TEXT("Grass"), TEXT("Foliage") };
		return FName(N[FMath::Clamp(Slot, 0, FHearthlineMaterialSet::NumSlots - 1)]);
	}

	bool InPoly(const TArray<FVector2D>& Poly, const FVector2D& P)
	{
		bool bIn = false;
		for (int32 i = 0, j = Poly.Num() - 1; i < Poly.Num(); j = i++)
			if ((Poly[i].Y > P.Y) != (Poly[j].Y > P.Y) && P.X < (Poly[j].X - Poly[i].X) * (P.Y - Poly[i].Y) / (Poly[j].Y - Poly[i].Y) + Poly[i].X) bIn = !bIn;
		return bIn;
	}

	double DistToPoly(const TArray<FVector2D>& Poly, const FVector2D& P)
	{
		double D = TNumericLimits<double>::Max();
		for (int32 i = 0, j = Poly.Num() - 1; i < Poly.Num(); j = i++)
		{
			const FVector2D A = Poly[j], E = Poly[i] - A;
			const double T = E.SizeSquared() > 0 ? FMath::Clamp(FVector2D::DotProduct(P - A, E) / E.SizeSquared(), 0.0, 1.0) : 0;
			D = FMath::Min(D, FVector2D::Distance(P, A + E * T));
		}
		return D;
	}
}

// (BuildProps: the export's own building triangles under a Hearthline house - in the glb's frame,
// metres x east, z south - go)
bool FOTGCourseProps::UnderHouse(const FVector2D& GlbXZ) const
{
	for (const FOTGCourseProps::FHouseFoot& F : HouseFeet)
		if (F.Box.IsInside(GlbXZ) && (InPoly(F.Pts, GlbXZ) || DistToPoly(F.Pts, GlbXZ) < 1.5)) return true;
	return false;
}

void FOTGCourseProps::BuildHouses()
{
	HouseFeet.Reset();
	if (FParse::Param(FCommandLine::Get(), TEXT("oldhouses")) || !Course->Data || !Course->Data->Solids.Num()) return;
	const double T0 = FPlatformTime::Seconds();

	// the footprints and what each one is
	TArray<FFoot> Feet;
	for (const FOTGSolid& S : Course->Data->Solids)
	{
		FFoot F;
		F.Pts = S.Footprint;
		if (F.Pts.Num() > 1 && F.Pts[0].Equals(F.Pts.Last(), 1e-4)) F.Pts.Pop();
		if (F.Pts.Num() < 3) continue;
		F.H = S.Height;
		Info(F);
		F.Type = TagType(S.Kind);
		if (F.H < MinHeightM) F.Type = TEXT("car");
		Feet.Add(MoveTemp(F));
	}
	// the clubhouse, when the map doesn't say: of the buildings 150 m² or more within 350 m of where
	// the round starts and turns (hole 1's and 10's tees, 9's and 18's greens), the closest
	TArray<FVector2D> Start;
	auto Tee = [&](int32 i) { if (Course->Data->Holes.IsValidIndex(i) && Course->Data->Holes[i].Tees.Num()) { const FVector T = Course->Data->Holes[i].Tees.CreateConstIterator()->Value; Start.Add(FVector2D(T.X, T.Z)); } };
	auto Green = [&](int32 i) { if (Course->Data->Holes.IsValidIndex(i)) Start.Add(FVector2D(Course->Data->Holes[i].GreenCenter.X, Course->Data->Holes[i].GreenCenter.Z)); };
	Tee(0); Green(8); Tee(9); Green(17);
	if (Start.Num() && !Feet.ContainsByPredicate([](const FFoot& F) { return F.Type == TEXT("clubhouse"); }))
	{
		int32 Best = -1;
		double BestD = 350;
		for (int32 i = 0; i < Feet.Num(); i++)
		{
			if ((!Feet[i].Type.IsEmpty() && Feet[i].Type != TEXT("house") && Feet[i].Type != TEXT("commercial")) || Feet[i].Area < 150) continue;
			double D = 0;
			for (const FVector2D& P : Start) D += FVector2D::Distance(P, Feet[i].Centroid);
			D /= Start.Num();
			if (D < BestD) { BestD = D; Best = i; }
		}
		if (Best >= 0) Feet[Best].Type = TEXT("clubhouse");
	}
	for (FFoot& F : Feet) if (F.Type.IsEmpty()) F.Type = F.Area < 20 ? TEXT("shed") : TEXT("house");

	// where the course is, to turn the houses' backs to it
	TArray<FVector2D> CoursePts;
	for (const FOTGHole& H : Course->Data->Holes)
	{
		for (const auto& T : H.Tees) CoursePts.Add(FVector2D(T.Value.X, T.Value.Z));
		CoursePts.Add(FVector2D(H.GreenCenter.X, H.GreenCenter.Z));
		for (const FVector& P : H.Path) CoursePts.Add(FVector2D(P.X, P.Z));
		for (const FVector& P : H.Aims) CoursePts.Add(FVector2D(P.X, P.Z));
	}

	// one Hearthline house, made again for each
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SP.ObjectFlags |= RF_Transient;
	auto NewHouse = [&]() -> AHearthlineHouse*
	{
		AHearthlineHouse* H = Course->GetWorld()->SpawnActor<AHearthlineHouse>(AHearthlineHouse::StaticClass(), FTransform::Identity, SP);
		if (!H || !H->ShellComponent) return nullptr;
		H->SetActorHiddenInGame(true);
		H->ShellComponent->SetVisibleInRayTracing(false);
		H->ShellComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return H;
	};
	AHearthlineHouse* House = NewHouse();
	if (!House) { UE_LOG(LogOTGHouses, Warning, TEXT("Houses: no Hearthline house - the export's buildings as they were")); return; }
	const FHearthlineMaterialSet Set = House->Materials;

	struct FCell { TArray<FVector3f> P, N; TArray<FVector2f> UV; TArray<FVector4f> C; TArray<int32> Mat; FVector Origin = FVector::ZeroVector; int32 Houses = 0; };
	TMap<FIntPoint, FCell> Cells;
	int32 Made = 0, Skipped = 0;
	TMap<EHearthlineStyle, int32> Styles;
	for (const FFoot& F : Feet)
	{
		if (F.Type != TEXT("house") || F.Area < MinAreaM2 || F.Area > MaxAreaM2) continue;
		const FRect R = MinRect(F.Pts);
		const double SideA = R.HalfU * 2, SideB = R.HalfV * 2;
		if (FMath::Min(SideA, SideB) < MinSideM || FMath::Max(SideA, SideB) > MaxSideM || F.Area / (SideA * SideB) < MinFill) { Skipped++; continue; }

		// its front: of the rectangle's four sides, the one facing furthest from the nearest of the course
		FVector2D Away = FVector2D::ZeroVector;
		double Near = TNumericLimits<double>::Max();
		for (const FVector2D& P : CoursePts) { const double D = FVector2D::DistSquared(P, R.C); if (D < Near) { Near = D; Away = R.C - P; } }
		Away = Away.GetSafeNormal();
		const FVector2D Sides[4] = { R.U, -R.U, R.V, -R.V };
		int32 Front = 0;
		for (int32 k = 1; k < 4; k++) if (FVector2D::DotProduct(Sides[k], Away) > FVector2D::DotProduct(Sides[Front], Away)) Front = k;
		const FVector2D Fwd = Sides[Front];
		const double DepthCm = (Front < 2 ? SideA : SideB) * 100, WidthCm = (Front < 2 ? SideB : SideA) * 100;

		// its style, from its shape and height, and its seed from where it stands (the same house every time)
		const int32 Seed = int32(GetTypeHash(FIntPoint(FMath::RoundToInt(F.Centroid.X), FMath::RoundToInt(F.Centroid.Y))) & 0x7fffffff) | 1;
		FRandomStream Rs(Seed);
		const bool bWide = WidthCm >= DepthCm * 1.15, bTall = F.H >= TwoStoreyM;
		static const EHearthlineStyle WideLow[] = { EHearthlineStyle::Ranch, EHearthlineStyle::Ranch, EHearthlineStyle::CapeCod, EHearthlineStyle::Colonial, EHearthlineStyle::SplitLevel, EHearthlineStyle::Modern };
		static const EHearthlineStyle WideTall[] = { EHearthlineStyle::Colonial, EHearthlineStyle::Colonial, EHearthlineStyle::SplitLevel, EHearthlineStyle::Modern };
		static const EHearthlineStyle Deep[] = { EHearthlineStyle::Craftsman, EHearthlineStyle::Farmhouse, EHearthlineStyle::Cottage, EHearthlineStyle::Craftsman };
		static const EHearthlineStyle Small[] = { EHearthlineStyle::Cottage, EHearthlineStyle::CapeCod };
		EHearthlineStyle Style;
		if (F.Area < 110) Style = Small[Rs.RandRange(0, UE_ARRAY_COUNT(Small) - 1)];
		else if (!bWide) Style = bTall ? EHearthlineStyle::Farmhouse : Deep[Rs.RandRange(0, UE_ARRAY_COUNT(Deep) - 1)];
		else Style = bTall ? WideTall[Rs.RandRange(0, UE_ARRAY_COUNT(WideTall) - 1)] : WideLow[Rs.RandRange(0, UE_ARRAY_COUNT(WideLow) - 1)];

		// the ground under it: it stands on the lowest, its foundation up to the highest
		const FVector2D Ax = R.U * (R.HalfU + 1), Ay = R.V * (R.HalfV + 1);
		auto LocalCm = [&](const FVector2D& P) { return FVector2D((P.X - Course->Data->HeightX0) * 100.0, (Course->Data->HeightZ1 - P.Y) * 100.0); };
		double Lo = TNumericLimits<double>::Max(), Hi = -Lo;
		for (const FVector2D& P : { R.C, R.C + Ax + Ay, R.C + Ax - Ay, R.C - Ax + Ay, R.C - Ax - Ay, R.C + Ax, R.C - Ax, R.C + Ay, R.C - Ay })
		{
			const FVector2D L = LocalCm(P);
			const double G = Course->Ground.GroundAt(L.X, L.Y);
			Lo = FMath::Min(Lo, G); Hi = FMath::Max(Hi, G);
		}
		if (Hi - Lo > MaxRiseCm) { Skipped++; continue; }

		House->Style = Style;
		House->Seed = Seed;
		House->ApplyStyle();
		// (the style's house stretched to the rectangle: its boxes' extent, front to back and across)
		double X0 = TNumericLimits<double>::Max(), X1 = -X0, Y0 = X0, Y1 = -X0;
		for (const FHearthlineVolume& V : House->Volumes)
		{
			X0 = FMath::Min(X0, V.Offset.X - V.Depth / 2); X1 = FMath::Max(X1, V.Offset.X + V.Depth / 2);
			Y0 = FMath::Min(Y0, V.Offset.Y - V.Width / 2); Y1 = FMath::Max(Y1, V.Offset.Y + V.Width / 2);
		}
		// (the porches stand out in front of the walls: the walls a porch's depth back from the footprint's edge)
		double PorchCm = 0;
		for (const FHearthlinePorch& P : House->Porches) if (P.Side == EHearthlineSide::Front) PorchCm = FMath::Max(PorchCm, double(P.Depth));
		const double WallsDepth = FMath::Max(DepthCm - PorchCm * 0.5, DepthCm * 0.75);
		const double Sx = WallsDepth / FMath::Max(X1 - X0, 100.0), Sy = WidthCm / FMath::Max(Y1 - Y0, 100.0);
		const double Cx = (X0 + X1) / 2, Cy = (Y0 + Y1) / 2;
		for (FHearthlineVolume& V : House->Volumes)
		{
			V.Offset = FVector2D((V.Offset.X - Cx) * Sx - (DepthCm - WallsDepth) / 2, (V.Offset.Y - Cy) * Sy);
			V.Depth = FMath::Max(float(V.Depth * Sx), 250.f);
			V.Width = FMath::Max(float(V.Width * Sy), 250.f);
		}
		House->FoundationHeight = FMath::Clamp(float(House->FoundationHeight + (Hi - Lo)), 0.f, 300.f);
		House->bLot = false;
		House->bFence = false;
		House->LotTrees = 0;
		House->bGenerateCollision = false;
		House->LitWindowFraction = 0;
		House->Rebuild();
		const UE::Geometry::FDynamicMesh3* Src = House->ShellComponent->GetMesh();
		if (!Src || !Src->TriangleCount()) { Skipped++; continue; }

		// its triangles copied out - each corner's position, normal, uv and colour, its surface (the
		// merged mesh's sections) - into its square's mesh. (Not FDynamicMeshEditor::AppendMesh: with
		// a hundred houses it read past the end and the game went down. Corners whose normal, uv or
		// colour Hearthline left unset get the face's normal, no uv, white)
		const FIntPoint Key(FMath::FloorToInt(F.Centroid.X / CellM), FMath::FloorToInt(F.Centroid.Y / CellM));
		FCell& Cell = Cells.FindOrAdd(Key);
		const FVector2D Lc = LocalCm(R.C);
		const FVector At = Course->Data->LevelOrigin + FVector(Lc.X, Lc.Y, Lo);
		if (!Cell.Houses) Cell.Origin = At;
		// (the house's front, +X, along Fwd: course x east, z north - the level's X east, Y south)
		const FTransform Xf(FRotator(0, FMath::RadiansToDegrees(FMath::Atan2(-Fwd.Y, Fwd.X)), 0), At - Cell.Origin);
		TArray<int32> ToSlot;
		for (int32 m = 0; m < House->ShellComponent->GetNumMaterials(); m++)
		{
			UMaterialInterface* Mi = House->ShellComponent->GetMaterial(m);
			int32 Slot = 0;
			for (int32 s = 0; s < FHearthlineMaterialSet::NumSlots; s++) if (SlotMat(Set, s) == Mi) { Slot = s; break; }
			ToSlot.Add(Slot);
		}
		const UE::Geometry::FDynamicMeshAttributeSet* A = Src->HasAttributes() ? Src->Attributes() : nullptr;
		const UE::Geometry::FDynamicMeshNormalOverlay* No = A ? A->PrimaryNormals() : nullptr;
		const UE::Geometry::FDynamicMeshUVOverlay* Uo = A && A->NumUVLayers() ? A->PrimaryUV() : nullptr;
		const UE::Geometry::FDynamicMeshColorOverlay* Co = A && A->HasPrimaryColors() ? A->PrimaryColors() : nullptr;
		const UE::Geometry::FDynamicMeshMaterialAttribute* Ma = A && A->HasMaterialID() ? A->GetMaterialID() : nullptr;
		for (int32 t : Src->TriangleIndicesItr())
		{
			const UE::Geometry::FIndex3i Tri = Src->GetTriangle(t);
			if (!Src->IsVertex(Tri.A) || !Src->IsVertex(Tri.B) || !Src->IsVertex(Tri.C)) continue;
			const FVector3d P[3] = { Src->GetVertex(Tri.A), Src->GetVertex(Tri.B), Src->GetVertex(Tri.C) };
			const FVector3d Face = ((P[1] - P[0]) ^ (P[2] - P[0])).GetSafeNormal();
			const UE::Geometry::FIndex3i Ne = No && No->IsSetTriangle(t) ? No->GetTriangle(t) : UE::Geometry::FIndex3i::Invalid();
			const UE::Geometry::FIndex3i Ue = Uo && Uo->IsSetTriangle(t) ? Uo->GetTriangle(t) : UE::Geometry::FIndex3i::Invalid();
			const UE::Geometry::FIndex3i Ce = Co && Co->IsSetTriangle(t) ? Co->GetTriangle(t) : UE::Geometry::FIndex3i::Invalid();
			for (int32 k = 0; k < 3; k++)
			{
				Cell.P.Add(FVector3f(Xf.TransformPosition(P[k])));
				const FVector3d N = Ne[k] >= 0 && No->IsElement(Ne[k]) ? FVector3d(No->GetElement(Ne[k])) : Face;
				Cell.N.Add(FVector3f(Xf.TransformVectorNoScale(N)));
				Cell.UV.Add(Ue[k] >= 0 && Uo->IsElement(Ue[k]) ? Uo->GetElement(Ue[k]) : FVector2f::ZeroVector);
				Cell.C.Add(Ce[k] >= 0 && Co->IsElement(Ce[k]) ? Co->GetElement(Ce[k]) : FVector4f::One());
			}
			const int32 m = Ma ? Ma->GetValue(t) : 0;
			Cell.Mat.Add(ToSlot.IsValidIndex(m) ? ToSlot[m] : 0);
		}
		Cell.Houses++;
		Made++;
		if (Made <= 6) UE_LOG(LogOTGHouses, Display, TEXT("House %d: %s, %.0f m2, at %.0f %.0f %.0f, facing %.0f"), Made, *StaticEnum<EHearthlineStyle>()->GetNameStringByValue(int64(Style)), F.Area, At.X, At.Y, At.Z, Xf.Rotator().Yaw);
		Styles.FindOrAdd(Style)++;

		FOTGCourseProps::FHouseFoot& Gone = HouseFeet.AddDefaulted_GetRef();
		for (const FVector2D& P : F.Pts) { Gone.Pts.Add(FVector2D(P.X - Course->Data->HeightX0, Course->Data->HeightZ1 - P.Y)); Gone.Box += Gone.Pts.Last(); }
		Gone.Box = Gone.Box.ExpandBy(2.0);
	}
	if (House) House->Destroy();

	// each square's houses as one static mesh: one section a surface
	int32 Tris = 0;
	for (auto& KV : Cells)
	{
		const FCell& Cell = KV.Value;
		if (!Cell.Mat.Num()) continue;
		FMeshDescription MD;
		FStaticMeshAttributes Attr(MD);
		Attr.Register();
		auto Pos = Attr.GetVertexPositions();
		auto Nrm = Attr.GetVertexInstanceNormals();
		auto UV = Attr.GetVertexInstanceUVs();
		auto Col = Attr.GetVertexInstanceColors();
		auto SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
		TArray<int32> Used;
		TMap<int32, FPolygonGroupID> Groups;
		for (int32 s : Cell.Mat)
			if (!Groups.Contains(s))
			{
				const FPolygonGroupID G = MD.CreatePolygonGroup();
				SlotNames[G] = SlotName(s);
				Groups.Add(s, G);
				Used.Add(s);
			}
		MD.ReserveNewVertices(Cell.P.Num());
		MD.ReserveNewVertexInstances(Cell.P.Num());
		MD.ReserveNewTriangles(Cell.Mat.Num());
		TArray<FVertexInstanceID> Inst;
		Inst.SetNum(Cell.P.Num());
		for (int32 i = 0; i < Cell.P.Num(); i++)
		{
			const FVertexID V = MD.CreateVertex();
			Pos[V] = Cell.P[i];
			const FVertexInstanceID VI = MD.CreateVertexInstance(V);
			Nrm[VI] = Cell.N[i];
			UV.Set(VI, 0, Cell.UV[i]);
			// (Hearthline's colours are linear; the static mesh build stores them as sRGB itself - as its own bake)
			Col[VI] = Cell.C[i];
			Inst[i] = VI;
		}
		for (int32 t = 0; t < Cell.Mat.Num(); t++)
			MD.CreateTriangle(Groups[Cell.Mat[t]], { Inst[t * 3], Inst[t * 3 + 1], Inst[t * 3 + 2] });
		// (tangents for the surfaces' normal maps)
		FStaticMeshOperations::ComputeTriangleTangentsAndNormals(MD);
		FStaticMeshOperations::ComputeTangentsAndNormals(MD, EComputeNTBsFlags::Tangents | EComputeNTBsFlags::IgnoreDegenerateTriangles);
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Course);
		for (int32 s : Used) Mesh->GetStaticMaterials().Add(FStaticMaterial(SlotMat(Set, s), SlotName(s)));
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bBuildSimpleCollision = false;
		Params.bFastBuild = true;
		Params.bMarkPackageDirty = false;
		Mesh->BuildFromMeshDescriptions({ &MD }, Params);
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Course);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetMobility(EComponentMobility::Movable);
		C->SetupAttachment(Course->GetRootComponent());
		C->RegisterComponent();
		C->SetWorldLocation(Cell.Origin);
		Course->Parts.Add(C);
		Tris += Cell.Mat.Num();
	}
	FString Kinds;
	for (const auto& KV : Styles) Kinds += FString::Printf(TEXT(" %s %d"), *StaticEnum<EHearthlineStyle>()->GetNameStringByValue(int64(KV.Key)), KV.Value);
	UE_LOG(LogOTGHouses, Display, TEXT("Houses: %d Hearthline houses (%s ), %d left as blocks, %d meshes, %d triangles, made in %.0f ms"),
		Made, *Kinds, Skipped, Cells.Num(), Tris, (FPlatformTime::Seconds() - T0) * 1000);
}
