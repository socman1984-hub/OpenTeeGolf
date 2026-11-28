// See OTGShotTracerComponent.h.

#include "Tracer/OTGShotTracerComponent.h"

void UOTGShotTracerComponent::Draw(const TArray<FVector>& Path, const FVector& Head, const FVector& CameraPos, float Fade)
{
	// The path so far, thinned to about a point a metre, as a ribbon facing the camera
	Points.Reset();
	for (const FVector& At : Path)
	{
		const FVector W = At + FVector(0, 0, 2);
		if (!Points.Num() || FVector::DistSquared(W, Points.Last()) > 100 * 100) Points.Add(W);
	}
	Points.Add(Head + FVector(0, 0, 2));
	Build(CameraPos, Fade);
}

void UOTGShotTracerComponent::SetFade(float Fade, const FVector& CameraPos)
{
	Build(CameraPos, Fade);
}

void UOTGShotTracerComponent::Build(const FVector& CameraPos, float Fade)
{
	if (Fade <= 0.f) { ClearAllMeshSections(); return; }
	// (fading: the tail - the tee end - runs in towards the landing, and what's left thins)
	const int32 First = FMath::Clamp(FMath::FloorToInt((1.f - Fade) * (Points.Num() - 1)), 0, FMath::Max(0, Points.Num() - 2));
	const float Thin = FMath::Clamp(Fade * 1.5f, 0.f, 1.f);
	if (Points.Num() - First < 2) { ClearAllMeshSections(); return; }
	TArray<FVector> V, Nrm;
	TArray<int32> Tri;
	TArray<FVector2D> UV;
	TArray<FLinearColor> Col;
	TArray<FProcMeshTangent> Tan;
	const int32 Num = Points.Num() - First;
	for (int32 i = 0; i < Num; i++)
	{
		const FVector P = Points[First + i];
		const FVector Along = (Points[First + FMath::Min(i + 1, Num - 1)] - Points[First + FMath::Max(i - 1, 0)]).GetSafeNormal();
		const FVector ToCam = (CameraPos - P).GetSafeNormal();
		const FVector SideV = FVector::CrossProduct(Along, ToCam).GetSafeNormal();
		const float W = FMath::Clamp(FVector::Dist(CameraPos, P) * 0.0022f, 1.5f, 90.f) * Thin;
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
