// See OTGShotTracerComponent.h.

#include "Tracer/OTGShotTracerComponent.h"

void UOTGShotTracerComponent::Draw(const TArray<FVector>& Path, const FVector& Head, const FVector& CameraPos)
{
	// The path so far, thinned to about a point a metre, as a ribbon facing the camera
	Points.Reset();
	for (const FVector& At : Path)
	{
		const FVector W = At + FVector(0, 0, 2);
		if (!Points.Num() || FVector::DistSquared(W, Points.Last()) > 100 * 100) Points.Add(W);
	}
	Points.Add(Head + FVector(0, 0, 2));
	if (Points.Num() < 2) return;
	TArray<FVector> V, Nrm;
	TArray<int32> Tri;
	TArray<FVector2D> UV;
	TArray<FLinearColor> Col;
	TArray<FProcMeshTangent> Tan;
	const int32 Num = Points.Num();
	for (int32 i = 0; i < Num; i++)
	{
		const FVector P = Points[i];
		const FVector Along = (Points[FMath::Min(i + 1, Num - 1)] - Points[FMath::Max(i - 1, 0)]).GetSafeNormal();
		const FVector ToCam = (CameraPos - P).GetSafeNormal();
		const FVector SideV = FVector::CrossProduct(Along, ToCam).GetSafeNormal();
		const float W = FMath::Clamp(FVector::Dist(CameraPos, P) * 0.0022f, 1.5f, 90.f);
		V.Add(P - SideV * W * 0.5f);
		V.Add(P + SideV * W * 0.5f);
		Nrm.Add(ToCam); Nrm.Add(ToCam);
		UV.Add(FVector2D(i / float(Num), 0)); UV.Add(FVector2D(i / float(Num), 1));
		Col.Add(FLinearColor::White); Col.Add(FLinearColor::White);
		if (i > 0)
		{
			const int32 A = (i - 1) * 2, B = i * 2;
			Tri.Append({ A, B, A + 1, A + 1, B, B + 1 });
			Tri.Append({ A, A + 1, B, A + 1, B + 1, B }); // (both faces)
		}
	}
	CreateMeshSection_LinearColor(0, V, Tri, Nrm, UV, Col, Tan, false);
	SetMaterial(0, RibbonMaterial);
}

void UOTGShotTracerComponent::Clear()
{
	ClearAllMeshSections();
	Points.Reset();
}
