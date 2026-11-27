// The course's bridges, as its course designer left them (SOTGBridgeDesigner; saved in the course's
// folder as bridges.json): each from one clicked point to another (course metres - say a cart path's
// ends on either bank of a pond), its deck on the ground's height at each end, flat between them or
// arched, with rails or without, of wood, stone or white-painted boards, so wide.
//
// Drawn by the runtime course (AOTGRuntimeCourse::SetBridges: a mesh each, made here, BuildMesh), and
// met by the ball: its deck is the ground where it's higher (otg::PlayCourse::decks, a path), its
// rails walls (Rails: AOTGRound's collider).

#pragma once

#include "CoreMinimal.h"

struct FMeshDescription;

struct FOTGBridge
{
	// The ends (course metres: x east, z north) and the deck's top there (y up)
	double AX = 0, AZ = 0, BX = 0, BZ = 0;
	double AY = 0, BY = 0;
	bool bArch = false;             // (flat: straight from end to end; arched: rising Rise in the middle)
	bool bRails = true;
	FString Look = TEXT("wood");    // wood, stone, white
	float Width = 2.6f;             // m (the deck's)
	float Rise = 1.2f;              // m (an arch's, over the straight line between the ends)
	static constexpr float WidthLo = 1.2f, WidthHi = 6.f, RiseLo = 0.3f, RiseHi = 5.f;

	static const TArray<FString>& Looks();
	static FString LookLabel(const FString& Look);

	double Span() const { return FVector2D(BX - AX, BZ - AZ).Size(); }
	// (the arch a bridge this long gets at first: a tenth of its span, 0.5..3 m)
	static float RiseFor(double SpanM) { return float(FMath::Clamp(SpanM * 0.1, 0.5, 3.0)); }
	// The deck's top at T (0 at A .. 1 at B), course metres
	double TopAt(double T) const { return FMath::Lerp(AY, BY, T) + (bArch ? Rise * 4 * T * (1 - T) : 0.0); }
	// How far either side of its line the ball meets it (the deck, and its rails)
	double HalfReach() const { return Width / 2 + (bRails ? 0.2 : 0.0); }
	// Over (X, Z)? T along it, and the deck's top there
	bool Over(double X, double Z, double& OutT, double& OutTop) const;

	// The rails' outlines for the ball (course metres x z, a metre or so each), each with its foot's
	// height and its top's: walls the ball meets
	struct FRail { TArray<FVector2D> Pts; double Bottom = 0, Top = 0; };
	void Rails(TArray<FRail>& Out) const;

	// The mesh (the level's cm, relative to Origin): the deck, its beams or stones, rails, posts
	// down to the ground (GroundAt: course metres -> metres). Course to the level: CourseToWorld.
	// Colours in the UVs, as the prop materials read them (M_Prop_bridge)
	void BuildMesh(FMeshDescription& MD, TFunctionRef<FVector(const FVector&)> CourseToWorld, const FVector& Origin, TFunctionRef<double(double, double)> GroundAt) const;

	TSharedRef<class FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<class FJsonObject>& O, FOTGBridge& Out);
};

struct OPENTEEGOLF_API FOTGBridgeDesign
{
	TArray<FOTGBridge> Bridges;
	static FOTGBridgeDesign Load(const FString& Dir);
	bool Save(const FString& Dir) const;   // (none: the file goes)
	// The bridge whose deck is over (X, Z) and the highest there, if any (-1)
	int32 At(double X, double Z, double* OutTop = nullptr) const;
};

namespace otg
{
	// A marker post (the level's cm, from Base up Height cm, gold): where a bridge's first end is
	OPENTEEGOLF_API void BridgeMarkerMesh(FMeshDescription& MD, double HeightCm);
}
