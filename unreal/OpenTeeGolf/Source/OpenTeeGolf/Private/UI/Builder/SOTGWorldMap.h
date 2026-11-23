// A map of the world to find a course on (the menu's New course page, SOTGCourseBuilder): OpenStreetMap's
// map or the aerial photos (tiles fetched and kept by the course helper), dragged and zoomed with the
// mouse, every golf course mapped in OpenStreetMap a dot (gold: built already), a click picks one.

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

class UTexture2D;
class UOTGHelperSubsystem;

struct FOTGMapCourse
{
	double Lat = 0, Lon = 0;
	FString Name, Location;
	FString OsmType;     // node, way, relation
	int64 OsmId = 0;
	FString BuiltId;     // (built already: its course id)
	bool bInGame = false;  // (built and in the game's courses folder)
	bool bFound = false;   // (in the last search's results)
};

class SOTGWorldMap : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGWorldMap) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(600, 500); }
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) const override;

	void SetCourses(const TArray<FOTGMapCourse>* InCourses) { Courses = InCourses; }
	void SetPicked(int32 I) { Picked = I; }
	void FlyTo(double Lat, double Lon, double Zoom);
	void SetLayer(const FString& L) { Layer_ = L; }
	FString GetLayer() const { return Layer_; }
	TFunction<void(int32 Index)> OnPick;   // (a course's dot clicked)
	// What's shown: the ones built, the rest, a search's results (the key's switches)
	bool bShowBuilt = true, bShowNotBuilt = true, bShowFound = true;

private:
	// What's drawn (builder.js's map): the courses not built yet in clusters - a count in a blue disc,
	// within 60 px of each other, until zoom 12 - each alone a blue dot; the ones you've built green
	// with a yellow ring (white: not in the game yet), a search's results orange, all on top
	struct FDot { FVector2D P; int32 N = 1; int32 Index = INDEX_NONE; FVector2D Sum = FVector2D::ZeroVector; };
	void Layout(const FGeometry& G) const;
	mutable TArray<FDot> Dots;
	FVector2D ToPx(double Lat, double Lon, double Z) const;      // world pixels at zoom Z
	void FromPx(const FVector2D& P, double Z, double& Lat, double& Lon) const;
	FVector2D ScreenOf(const FGeometry& G, double Lat, double Lon) const;
	int32 HitCourse(const FGeometry& G, const FVector2D& Local) const;
	const FSlateBrush* Tile(int32 Z, int32 X, int32 Y) const;   // (none yet: asked for)

	double Lat = 30, Lon = -40, Zoom = 2.6;
	FString Layer_ = TEXT("map");
	const TArray<FOTGMapCourse>* Courses = nullptr;
	int32 Picked = INDEX_NONE;
	bool bDragging = false, bMoved = false;
	FVector2D DragFrom, DragLast;
	struct FTile { TStrongObjectPtr<UTexture2D> Tex; TSharedPtr<FSlateBrush> Brush; bool bAsked = false; bool bFailed = false; double Used = 0; };
	mutable TMap<FString, FTile> Tiles;
	mutable int32 Asking = 0;
};
