// Making a course in the game (the menu's "New course" page), as the web version's course builder
// does it, through the course helper (UOTGHelperSubsystem):
//
//  - SOTGWorldMap: a map of the world to find a course on - OpenStreetMap's map or the aerial
//    photos (tiles fetched and kept by the helper), dragged and zoomed with the mouse, every golf
//    course mapped in OpenStreetMap a dot (gold: built already), a click picks one
//  - SOTGCourseBuilder: the map, a search, and the course picked: what's mapped there (holes, greens,
//    fairways ...; which set of holes on a club with more than one), its name, then Build - the map
//    data, heights and aerial photos downloaded and the course built, then exported to the game's
//    courses folder - with how far it is; then Play. Below: the courses built, to export again
//    (after editing), bring up to date from OpenStreetMap, or delete.

#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class SBox;
class STextBlock;
class SEditableTextBox;
class UTexture2D;
class UOTGHelperSubsystem;
class FJsonObject;

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

class SOTGCourseBuilder : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOTGCourseBuilder) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	// (each frame, before the layout - not in Tick, which runs as it's drawn: panels made there
	// are drawn once unmeasured, piled on each other)
	EActiveTimerReturnType Pulse(double Time, float Dt);
	TFunction<void(const FString& CourseId)> OnReady;   // (a course built and exported: play it)
	TFunction<void(const FString& CourseId)> OnEdit;    // (a built course to edit)
	void Refresh() { LoadBuilt(); }

private:
	UOTGHelperSubsystem* Helper() const;
	void Rebuild();
	TSharedRef<SWidget> PickedCard();
	TSharedRef<SWidget> JobCard();
	TSharedRef<SWidget> BuiltList();
	void Pick(int32 I, bool bFly);
	// (one of the map key's switches: its dot and name, ticked when shown; a click turns it off or on)
	TSharedRef<SWidget> KeySwitch(const FString& Label, const FSlateBrush* Dot, float DotSize, bool SOTGWorldMap::* Flag);
	void LoadWorld();
	void LoadBuilt();
	void Search();
	void Check();
	void StartBuild();
	void JobDone(const struct FOTGHelperJob& Job);

	TSharedPtr<SOTGWorldMap> Map;
	TSharedPtr<SBox> Side, Lower;
	FString NameDraft;
	TSharedPtr<SEditableTextBox> SearchBox, NameBox;
	TArray<FOTGMapCourse> Courses;        // (the world's, then what a search found)
	TMap<int64, int32> ById;              // (osm id -> index in Courses)
	TArray<int32> Found;                  // (the last search's)
	int32 Picked = INDEX_NONE;
	int32 WorldFrom = 0;
	bool bWorldDone = false, bWorldAsking = false;
	double NextWorld = 0;
	float WorldProgress = 0;
	int32 WorldSeeded = 0;   // (courses from the list that comes with the game)
	FString Status;
	// the course picked: what's mapped there
	TSharedPtr<FJsonObject> Checked;
	bool bChecking = false, bCheckFailed = false;
	FString Group;
	// the courses built (the helper's), and the ones the game has
	struct FBuilt { FString Id, Name, Location; int64 OsmId = 0; bool bInGame = false; };
	TArray<FBuilt> Built;
	FString ConfirmDelete;
	// a build going on: export it when it's done
	FString ReadyId;
	FDelegateHandle JobHandle;
	FString JobShown;
	// (-buildertest=<words>: search, pick the first, a screenshot; -buildertestbuild: build it too,
	// and quit when it's in the game - testing without the mouse)
	FString TestQuery;
	int32 TestStep = 0;
	double TestAt = 0;
	bool bTestBuild = false;
};
