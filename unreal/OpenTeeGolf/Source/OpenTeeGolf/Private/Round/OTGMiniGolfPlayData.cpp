// The putt-putt's play data (see OTGMiniGolf.h): the course data the round plays, made from the
// layout (OTGMiniGolfLayout.h) - heights, surfaces, holes.

#include "Round/OTGMiniGolf.h"

#include "Algo/Reverse.h"
#include "Async/ParallelFor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Course/OTGCourseData.h"
#include "Course/OTGCourseSubsystem.h"
#include "Physics/OTGBallPhysics.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGMiniGolf, Log, All);

#include "Round/OTGMiniGolfLayout.h"

// ------------------------------------------------------------------ the play data

UOTGCourseData* AOTGMiniGolf::MakeCourse(UObject* Outer)
{
	const double T0 = FPlatformTime::Seconds();
	Holes();   // (made here, before the heights and surfaces are worked out on several threads)
	UOTGCourseData* D = NewObject<UOTGCourseData>(Outer);
	D->CourseId = CourseId;
	D->CourseName = TEXT("Putt-Putt");
	D->Location = TEXT("Nine holes of mini golf");
	D->DefaultStimp = 9;
	D->Generated = CourseId;
	D->NormalSpanM = 0.12;   // (the ramps and humps are short)
	D->Level = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/OpenTeeGolf/Game/L_Course.L_Course")));
	D->LevelOrigin = FVector::ZeroVector;
	D->MinX = GridX0; D->MaxX = GridX1; D->MinZ = GridZ0; D->MaxZ = GridZ1;
	D->HeightSamples = GridN;
	D->HeightX0 = GridX0; D->HeightZ1 = GridZ1; D->MetresPerSample = GridStep;
	D->HeightBase = HeightBase; D->HeightRange = HeightRange;
	D->Heights.SetNumUninitialized(GridN * GridN);
	ParallelFor(GridN, [D](int32 J) {
		for (int32 I = 0; I < GridN; I++)
		{
			const double H = HeightAt(GridX0 + I * GridStep, GridZ1 - J * GridStep);
			D->Heights[J * GridN + I] = uint16(FMath::Clamp(FMath::RoundToInt((H - HeightBase) / HeightRange * 65535.0), 0, 65535));
		}
	});
	D->SurfaceW = SurfW; D->SurfaceH = SurfH; D->SurfaceMinX = SurfX0; D->SurfaceMaxZ = SurfZ1; D->SurfaceRes = SurfRes;
	D->Surfaces.SetNumUninitialized(SurfW * SurfH);
	ParallelFor(SurfH, [D](int32 J) {
		for (int32 I = 0; I < SurfW; I++) D->Surfaces[J * SurfW + I] = uint8(SurfaceAt(SurfX0 + (I + 0.5) * SurfRes, SurfZ1 - (J + 0.5) * SurfRes));
	});
	const TArray<FMiniHole>& All = Holes();
	for (int32 i = 0; i < All.Num(); i++)
	{
		const FMiniHole& M = All[i];
		FOTGHole H;
		H.Number = i + 1;
		H.Par = M.Par;
		H.Index = i + 1;
		H.Name = M.Name;
		H.GreenRadius = 1;
		auto At = [](const P2& P) { return FVector(P.X, HeightAt(P.X, P.Y), P.Y); };
		const FVector Tee = At(M.At), Pin = At(M.Abs(M.Cup));
		H.Tees.Add(TEXT("White"), Tee);
		for (const TCHAR* Day : { TEXT("Thursday"), TEXT("Friday"), TEXT("Saturday"), TEXT("Sunday") }) H.Pins.Add(Day, Pin);
		H.GreenCenter = Pin;
		H.Path.Add(Tee);
		for (const P2& A : M.Aims) { H.Aims.Add(At(M.Abs(A))); H.Path.Add(H.Aims.Last()); }
		H.Path.Add(Pin);
		D->Holes.Add(H);

		// What the ball bounces off (physics.js OBSTACLES 'bumper'): the boards, each side in pieces
		// half a metre long, a little thicker on the inside by the ball's radius (the collisions use
		// the ball's centre); posts; blocks; the windmill's walls
		auto Solid = [D](const TArray<P2>& Pts) {
			FOTGSolid S;
			S.Height = float(0.5 + WallUp);
			S.Kind = TEXT("bumper");
			S.Footprint = Pts;
			D->Solids.Add(S);
		};
		for (const FLaneEdge& E : Edges(M.LaneAbs))
		{
			const double Len = (E.B - E.A).Size();
			const int32 K = FMath::Max(1, FMath::CeilToInt32(Len / 0.5));
			const P2 U = (E.B - E.A) / Len;
			const double Ext = BallR + 0.01;
			for (int32 s = 0; s < K; s++)
			{
				const double S0 = s * Len / K - (s == 0 ? Ext : 0), S1 = (s + 1) * Len / K + (s == K - 1 ? Ext : 0);
				auto P = [&](double Along, double Off) { return E.A + U * Along + E.N * Off; };
				Solid({ P(S0, -BallR), P(S1, -BallR), P(S1, WallT), P(S0, WallT) });
			}
		}
		for (const P2& Q : M.Posts)
		{
			const double R = (0.06 + BallR) / FMath::Cos(PI / 8);
			TArray<P2> O;
			for (int32 k = 0; k < 8; k++) O.Add(M.Abs(Q) + P2(R * FMath::Cos(k * PI / 4), R * FMath::Sin(k * PI / 4)));
			Solid(O);
		}
		for (const TArray<P2>& B : M.Blocks)
		{
			TArray<P2> A;
			for (const P2& Q : B) A.Add(M.Abs(Q));
			Solid(Grow(A, BallR));
		}
		if (M.bMill)
		{
			const double Z0 = M.At.Y + M.MillZ, Z1 = Z0 + M.MillDepth;
			Solid(RectPts(M.At.X - 0.9, Z0, M.At.X - M.MillHalf - 0.01, Z1));
			Solid(RectPts(M.At.X + M.MillHalf + 0.01, Z0, M.At.X + 0.9, Z1));
		}
	}
	UE_LOG(LogOTGMiniGolf, Display, TEXT("Putt-putt: %d holes, %d boards and posts, made in %.0f ms"), D->Holes.Num(), D->Solids.Num(), (FPlatformTime::Seconds() - T0) * 1000);
	return D;
}
