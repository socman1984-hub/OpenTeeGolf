// See OTGCourseData.h.

#include "OTGCourseData.h"

#include "Physics/OTGPlayCourse.h"

void UOTGCourseData::FillPlayCourse(otg::PlayCourse& Out) const
{
	Out.N = HeightSamples;
	Out.x0 = HeightX0; Out.z1 = HeightZ1; Out.step = MetresPerSample; Out.base = HeightBase; Out.range = HeightRange;
	Out.heights.assign(Heights.GetData(), Heights.GetData() + Heights.Num());
	Out.w = SurfaceW; Out.h = SurfaceH; Out.sMinX = SurfaceMinX; Out.sMaxZ = SurfaceMaxZ; Out.res = SurfaceRes;
	Out.surfaces.assign(Surfaces.GetData(), Surfaces.GetData() + Surfaces.Num());
	Out.minX = MinX; Out.maxX = MaxX; Out.minZ = MinZ; Out.maxZ = MaxZ;
}

void UOTGCourseData::FillTrees(otg::PlayTrees& Out) const
{
	Out.all.clear();
	Out.all.reserve(Trees.Num());
	for (const FOTGTreeShape& S : Trees)
	{
		otg::Tree T;
		T.x = S.X; T.z = S.Z; T.trunkTop = S.TrunkTop; T.trunkR = S.TrunkR; T.kind = TCHAR_TO_UTF8(*S.Kind);
		T.cone = S.bCone;
		if (S.bCone) { T.coneBase = S.A; T.coneH = S.B; T.coneR = S.C; }
		else { T.cy = S.A; T.rh = S.B; T.rv = S.C; }
		Out.all.push_back(T);
	}
	Out.Build();
}

FVector UOTGCourseData::CourseToWorld(const FVector& P) const
{
	return LevelOrigin + FVector((P.X - HeightX0) * 100.0, (HeightZ1 - P.Z) * 100.0, (P.Y - HeightBase) * 100.0);
}

FVector UOTGCourseData::WorldToCourse(const FVector& W) const
{
	const FVector L = W - LevelOrigin;
	return FVector(HeightX0 + L.X / 100.0, HeightBase + L.Z / 100.0, HeightZ1 - L.Y / 100.0);
}
