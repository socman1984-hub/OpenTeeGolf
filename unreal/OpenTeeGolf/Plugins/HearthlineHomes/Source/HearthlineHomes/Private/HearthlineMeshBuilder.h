// Copyright __PUBLISHER__. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

/**
 * Small immediate-mode mesh writer used by the Hearthline generators.
 * Every face gets its own vertices, a flat normal, a vertex colour (used by the materials as tint)
 * and UVs projected in actor space at 1 UV unit per metre, so textures line up across separate parts.
 *
 * Winding rule for callers: Quad(A,B,C,D) and Tri(A,B,C) must be ordered so that (B-A) x (last-A) points out of the surface.
 */
class FHearthlineMeshBuilder
{
public:
	FHearthlineMeshBuilder()
	{
		Mesh.EnableAttributes();
		Mesh.Attributes()->SetNumUVLayers(1);
		Mesh.Attributes()->EnablePrimaryColors();
		Mesh.Attributes()->EnableMaterialID();
		Stack.Add(FTransform::Identity);
	}

	UE::Geometry::FDynamicMesh3 Mesh;

	/** Material slot written to new faces. */
	int32 Material = 0;
	/** Tint written to new faces (linear). Alpha is free for material use. */
	FLinearColor Color = FLinearColor::White;
	/** Added to the UVs of new faces; used to vary window backings. */
	FVector2f UVOffset = FVector2f::ZeroVector;
	/** Swaps U and V on new faces, turning the texture a quarter turn (vertical boards). */
	bool bSwapUV = false;

	void Push(const FTransform& Local) { Stack.Add(Local * Stack.Last()); }
	void Pop() { if (Stack.Num() > 1) { Stack.Pop(); } }
	const FTransform& Top() const { return Stack.Last(); }
	FVector ToActor(const FVector& P) const { return Stack.Last().TransformPosition(P); }

	/** Frame whose X runs along Dir, Y points out of the wall and Z is up. Dir must be horizontal. */
	static FTransform WallFrame(const FVector& Origin, const FVector& Dir)
	{
		const FVector X = Dir.GetSafeNormal2D();
		const FVector Z = FVector::UpVector;
		const FVector Y = FVector::CrossProduct(Z, X);
		return FTransform(FMatrix(X, Y, Z, Origin));
	}

	void Set(int32 InMaterial, const FLinearColor& InColor) { Material = InMaterial; Color = InColor; }

	void Tri(const FVector& A, const FVector& B, const FVector& C)
	{
		const FVector P[3] = { ToActor(A), ToActor(B), ToActor(C) };
		const FVector N = FVector::CrossProduct(P[1] - P[0], P[2] - P[0]).GetSafeNormal();
		if (N.IsNearlyZero()) { return; }
		int32 V[3], NE[3], UE_[3], CE[3];
		FVector U, W; Axes(N, U, W);
		for (int32 i = 0; i < 3; ++i) { AddCorner(P[i], N, U, W, V[i], NE[i], UE_[i], CE[i]); }
		AddTri(V[0], V[2], V[1], NE[0], NE[2], NE[1], UE_[0], UE_[2], UE_[1], CE[0], CE[2], CE[1]);
	}

	void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D)
	{
		const FVector P[4] = { ToActor(A), ToActor(B), ToActor(C), ToActor(D) };
		const FVector N = FVector::CrossProduct(P[1] - P[0], P[3] - P[0]).GetSafeNormal();
		if (N.IsNearlyZero()) { return; }
		int32 V[4], NE[4], UE_[4], CE[4];
		FVector U, W; Axes(N, U, W);
		for (int32 i = 0; i < 4; ++i) { AddCorner(P[i], N, U, W, V[i], NE[i], UE_[i], CE[i]); }
		AddTri(V[0], V[2], V[1], NE[0], NE[2], NE[1], UE_[0], UE_[2], UE_[1], CE[0], CE[2], CE[1]);
		AddTri(V[0], V[3], V[2], NE[0], NE[3], NE[2], UE_[0], UE_[3], UE_[2], CE[0], CE[3], CE[2]);
	}

	/** Quad whose winding is chosen so that it faces Toward (a direction in the current frame). */
	void QuadF(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Toward)
	{
		if (FVector::DotProduct(FVector::CrossProduct(B - A, D - A), Toward) < 0) { Quad(A, D, C, B); } else { Quad(A, B, C, D); }
	}

	/** Triangle whose winding is chosen so that it faces Toward. */
	void TriF(const FVector& A, const FVector& B, const FVector& C, const FVector& Toward)
	{
		if (FVector::DotProduct(FVector::CrossProduct(B - A, C - A), Toward) < 0) { Tri(A, C, B); } else { Tri(A, B, C); }
	}

	/** Fully specified vertex for RawTri / RawQuad (used for smooth-shaded limbs and foliage cards). */
	struct FRawVertex
	{
		FVector P = FVector::ZeroVector;
		FVector N = FVector::UpVector;
		FVector2f UV = FVector2f::ZeroVector;
		FLinearColor C = FLinearColor::White;
	};

	/** Triangle with caller-supplied normals, UVs and colours. Positions and normals are in the current frame. */
	void RawTri(const FRawVertex& A, const FRawVertex& Bv, const FRawVertex& Cv)
	{
		const FRawVertex* Src[3] = { &A, &Cv, &Bv };
		int32 V[3], NE[3], UVE[3], CE[3];
		for (int32 i = 0; i < 3; ++i)
		{
			V[i] = Mesh.AppendVertex(FVector3d(ToActor(Src[i]->P)));
			NE[i] = Mesh.Attributes()->PrimaryNormals()->AppendElement(FVector3f(Stack.Last().TransformVectorNoScale(Src[i]->N).GetSafeNormal()));
			UVE[i] = Mesh.Attributes()->PrimaryUV()->AppendElement(Src[i]->UV);
			CE[i] = Mesh.Attributes()->PrimaryColors()->AppendElement(FVector4f(Src[i]->C.R, Src[i]->C.G, Src[i]->C.B, Src[i]->C.A));
		}
		AddTri(V[0], V[1], V[2], NE[0], NE[1], NE[2], UVE[0], UVE[1], UVE[2], CE[0], CE[1], CE[2]);
	}

	void RawQuad(const FRawVertex& A, const FRawVertex& Bv, const FRawVertex& Cv, const FRawVertex& D)
	{
		RawTri(A, Bv, Cv);
		RawTri(A, Cv, D);
	}

	enum EFace : uint8 { XP = 1, XN = 2, YP = 4, YN = 8, ZP = 16, ZN = 32, All = 63 };

	/** Axis aligned box in the current frame. Faces picks which sides are written. */
	void Box(const FVector& A, const FVector& B, uint8 Faces = All)
	{
		const FVector Lo = A.ComponentMin(B), Hi = A.ComponentMax(B);
		if (Faces & XP) { Quad(FVector(Hi.X, Lo.Y, Lo.Z), FVector(Hi.X, Hi.Y, Lo.Z), FVector(Hi.X, Hi.Y, Hi.Z), FVector(Hi.X, Lo.Y, Hi.Z)); }
		if (Faces & XN) { Quad(FVector(Lo.X, Hi.Y, Lo.Z), FVector(Lo.X, Lo.Y, Lo.Z), FVector(Lo.X, Lo.Y, Hi.Z), FVector(Lo.X, Hi.Y, Hi.Z)); }
		if (Faces & YP) { Quad(FVector(Hi.X, Hi.Y, Lo.Z), FVector(Lo.X, Hi.Y, Lo.Z), FVector(Lo.X, Hi.Y, Hi.Z), FVector(Hi.X, Hi.Y, Hi.Z)); }
		if (Faces & YN) { Quad(FVector(Lo.X, Lo.Y, Lo.Z), FVector(Hi.X, Lo.Y, Lo.Z), FVector(Hi.X, Lo.Y, Hi.Z), FVector(Lo.X, Lo.Y, Hi.Z)); }
		if (Faces & ZP) { Quad(FVector(Lo.X, Lo.Y, Hi.Z), FVector(Hi.X, Lo.Y, Hi.Z), FVector(Hi.X, Hi.Y, Hi.Z), FVector(Lo.X, Hi.Y, Hi.Z)); }
		if (Faces & ZN) { Quad(FVector(Lo.X, Hi.Y, Lo.Z), FVector(Hi.X, Hi.Y, Lo.Z), FVector(Hi.X, Lo.Y, Lo.Z), FVector(Lo.X, Lo.Y, Lo.Z)); }
	}

	/** Rectangle on a wall plane (frame from WallFrame), facing out (+Y), at depth Y. */
	void WallQuad(double S0, double S1, double Z0, double Z1, double Y = 0.0)
	{
		if (S1 - S0 < 0.01 || Z1 - Z0 < 0.01) { return; }
		Quad(FVector(S1, Y, Z0), FVector(S0, Y, Z0), FVector(S0, Y, Z1), FVector(S1, Y, Z1));
	}

	/** Box written between two arbitrary points along its length, with a rectangular section. Used for sloped rails and posts. */
	void Beam(const FVector& From, const FVector& To, double Width, double Height)
	{
		const FVector Dir = (To - From);
		const double Len = Dir.Size();
		if (Len < 0.01) { return; }
		const FVector X = Dir / Len;
		FVector Y = FVector::CrossProduct(FVector::UpVector, X);
		if (Y.IsNearlyZero()) { Y = FVector::RightVector; }
		Y.Normalize();
		const FVector Z = FVector::CrossProduct(X, Y);
		Push(FTransform(FMatrix(X, Y, Z, From)));
		Box(FVector(0, -Width * 0.5, -Height * 0.5), FVector(Len, Width * 0.5, Height * 0.5));
		Pop();
	}

private:
	TArray<FTransform> Stack;

	/** Texture axes for a face normal: U horizontal, V down the surface (or actor X/Y on flat faces). */
	static void Axes(const FVector& N, FVector& U, FVector& W)
	{
		if (FMath::Abs(N.Z) > 0.999)
		{
			U = FVector::ForwardVector; W = FVector::RightVector;
		}
		else
		{
			U = FVector::CrossProduct(FVector::UpVector, N).GetSafeNormal();
			W = FVector::CrossProduct(U, N).GetSafeNormal();
			if (W.Z > 0) { W = -W; }
		}
	}

	void AddCorner(const FVector& P, const FVector& N, const FVector& U, const FVector& W, int32& V, int32& NE, int32& UVE, int32& CE)
	{
		V = Mesh.AppendVertex(FVector3d(P));
		NE = Mesh.Attributes()->PrimaryNormals()->AppendElement(FVector3f(N));
		const FVector2f UV0(float(FVector::DotProduct(P, U) * 0.01), float(FVector::DotProduct(P, W) * 0.01));
		UVE = Mesh.Attributes()->PrimaryUV()->AppendElement((bSwapUV ? FVector2f(UV0.Y, UV0.X) : UV0) + UVOffset);
		CE = Mesh.Attributes()->PrimaryColors()->AppendElement(FVector4f(Color.R, Color.G, Color.B, Color.A));
	}

	void AddTri(int32 A, int32 B, int32 C, int32 NA, int32 NB, int32 NC, int32 UA, int32 UB, int32 UC, int32 CA, int32 CB, int32 CC)
	{
		const int32 T = Mesh.AppendTriangle(A, B, C);
		if (T < 0) { return; }
		Mesh.Attributes()->PrimaryNormals()->SetTriangle(T, UE::Geometry::FIndex3i(NA, NB, NC));
		Mesh.Attributes()->PrimaryUV()->SetTriangle(T, UE::Geometry::FIndex3i(UA, UB, UC));
		Mesh.Attributes()->PrimaryColors()->SetTriangle(T, UE::Geometry::FIndex3i(CA, CB, CC));
		Mesh.Attributes()->GetMaterialID()->SetValue(T, Material);
	}
};
