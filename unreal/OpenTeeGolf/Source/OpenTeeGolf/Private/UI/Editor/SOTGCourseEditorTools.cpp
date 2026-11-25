// The course editor's changes (see SOTGCourseEditor.h): undo, the tools, drawing and deleting shapes,
// trees cut and restored, fitting holes, auto pins, the review.

#include "UI/Editor/SOTGCourseEditor.h"

#include "UI/OTGUi.h"
#include "Course/OTGTextures.h"
#include "Course/OTGCourseData.h"
#include "Course/OTGCourseSubsystem.h"
#include "App/OTGHelperSubsystem.h"
#include "World/OTGTreeDesign.h"
#include "Course/OTGQuickUpdate.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

using namespace otg::ui;

#include "UI/Editor/SOTGEditorShared.h"

// ------------------------------------------------------------------ changes

SOTGCourseEditor::FSnap SOTGCourseEditor::Snap() const
{
	return FSnap{ Src.Holes, Src.Shapes, Src.UserTrees, Src.RemovedTrees, Src.TallGrass, Src.BuildingTypes, Src.Name, Src.Location, Src.Stimp,
		Src.bPhotoFairway, Src.bPhotoGreen, Src.bPhotoRough, Src.MowFairway, Src.MowGreen, Src.RoughScale, Src.NativeScale, Src.TreesOff, Src.PlantsOff, Src.PlantsExtra, Src.PlantAmount };
}

void SOTGCourseEditor::Restore(const FSnap& S)
{
	Src.Holes = S.Holes; Src.Shapes = S.Shapes; Src.UserTrees = S.UserTrees; Src.RemovedTrees = S.RemovedTrees; Src.TallGrass = S.TallGrass; Src.BuildingTypes = S.BuildingTypes;
	Src.Name = S.Name; Src.Location = S.Location; Src.Stimp = S.Stimp; Src.bPhotoFairway = S.bPF; Src.bPhotoGreen = S.bPG; Src.bPhotoRough = S.bPR;
	Src.MowFairway = S.MowF; Src.MowGreen = S.MowG; Src.RoughScale = S.RS; Src.NativeScale = S.NS; Src.TreesOff = S.TreesOff; Src.PlantsOff = S.PlantsOff; Src.PlantsExtra = S.PlantsExtra; Src.PlantAmount = S.PlantAmount;
}

void SOTGCourseEditor::Change()
{
	UndoStack.Add(Snap());
	if (UndoStack.Num() > 100) UndoStack.RemoveAt(0);
	bDirty = true;
}

void SOTGCourseEditor::Undo()
{
	if (!UndoStack.Num()) { Status = TEXT("Nothing to undo"); Changed(); return; }
	Restore(UndoStack.Pop());
	ClearPick();
	HoleI = FMath::Clamp(HoleI, 0, FMath::Max(0, Src.Holes.Num() - 1));
	Status = TEXT("Undone");
	Changed();
}

void SOTGCourseEditor::Changed()
{
	PanelKey++;
}

void SOTGCourseEditor::ClearPick()
{
	SelShape = SelTree = SelFound = SelRemoved = SelBuilding = SelForest = SelArea = INDEX_NONE;
	SelMany.Reset();
}

void SOTGCourseEditor::SetMode(EMode M, const FString& Kind, const FString& Key)
{
	Mode = M; ModeKind = Kind; ModeKey = Key;
	DrawPts.Reset();
	// (its section shown: the hole's points, the shapes, the trees, the tall grass)
	if (M == EMode::Place) Tab = 0;
	else if (M == EMode::Draw) Tab = Kind == TEXT("trees") ? 2 : 1;
	else if (M == EMode::AddTree || M == EMode::Cut) Tab = 2;
	else if (M == EMode::Paint || M == EMode::Erase) Tab = 3;
	Changed();
}

void SOTGCourseEditor::FinishDraw()
{
	TArray<FVector2D> P;
	for (const FVector2D& Q : DrawPts) if (!P.Num() || FVector2D::Distance(P.Last(), Q) > 0.3) P.Add(Q);
	if (P.Num() < 3) { Status = TEXT("A shape needs at least 3 points"); Changed(); return; }
	// (a forest, or an area of the tall grass with a biome of its own: kept in the game's folder)
	if (ModeKind == TEXT("forest"))
	{
		FOTGForest F;
		F.Name = FString::Printf(TEXT("Forest %d"), Design.Forests.Num() + 1);
		F.Pts = P;
		Design.Forests.Add(F);
		ClearPick();
		SelForest = Design.Forests.Num() - 1;
		SetMode(EMode::None);
		SaveForests(TEXT("Forest drawn"));
		return;
	}
	if (ModeKind == TEXT("biome"))
	{
		const FOTGBiome* B = OTGLibrary::FindIn(LibBiomes, AreaBiome);
		if (!B) { Status = TEXT("Pick a biome for it first"); Changed(); return; }
		Biomes.Use(*B);
		Biomes.Areas.Add({ B->Name, P });
		ClearPick();
		SelArea = Biomes.Areas.Num() - 1;
		SetMode(EMode::None);
		SaveBiomes(FString::Printf(TEXT("%s area drawn"), *B->Name));
		return;
	}
	Change();
	// (only one boundary)
	if (ModeKind == TEXT("oob")) Src.Shapes.RemoveAll([](const FOTGSourceShape& S) { return S.Kind == TEXT("oob"); });
	FOTGSourceShape S;
	S.Kind = ModeKind;
	S.Pts = P;
	S.bEdited = true;
	if (ModeKind == TEXT("trees")) { S.Density = AreaDensity; S.Species = AreaKind; S.HMin = AreaHMin; S.HMax = AreaHMax; }
	Src.Shapes.Add(S);
	ClearPick();
	SelShape = Src.Shapes.Num() - 1;
	SetMode(EMode::None);
}

void SOTGCourseEditor::PlacePoint(const FVector2D& P)
{
	if (!Src.Holes.IsValidIndex(HoleI)) return;
	Change();
	FOTGSourceHole& H = Src.Holes[HoleI];
	if (ModeKind == TEXT("tee")) H.Tees.Add(ModeKey, P);
	else if (ModeKind == TEXT("aim")) { const int32 K = FMath::Min(FCString::Atoi(*ModeKey), H.Aims.Num()); if (K < H.Aims.Num()) H.Aims[K] = P; else H.Aims.Add(P); }
	else if (ModeKind == TEXT("green")) H.Green = P;
	else if (ModeKind == TEXT("pin")) H.Pins.Add(ModeKey, P);
	SetMode(EMode::None);
}

void SOTGCourseEditor::DeleteSelected()
{
	if (Src.UserTrees.IsValidIndex(SelTree)) { Change(); Src.UserTrees.RemoveAt(SelTree); }
	else if (SelMany.Num()) { RemoveMany(); return; }
	else if (Found.IsValidIndex(SelFound)) { RemoveFound(false); return; }
	else if (Src.Shapes.IsValidIndex(SelShape)) { Change(); Src.Shapes.RemoveAt(SelShape); }
	else return;
	ClearPick();
	Changed();
}

int32 SOTGCourseEditor::CutTrees(const FVector2D& P)
{
	// any tree under the brush: your own taken away, found ones marked removed
	int32 N = Src.UserTrees.RemoveAll([&](const FOTGSourceTree& T) { return FVector2D::Distance(T.P, P) <= CutR; });
	for (const FFound& F : Found)
		if (F.Design != 3 && FVector2D::Distance(F.P, P) <= CutR && !Src.RemovedTrees.ContainsByPredicate([&](const FVector2D& R) { return FVector2D::Distance(R, F.P) < 0.5; }))
		{
			Src.RemovedTrees.Add(F.P);
			N++;
		}
	return N;
}

void SOTGCourseEditor::RemoveFound(bool bReplace)
{
	if (!Found.IsValidIndex(SelFound)) return;
	Change();
	const FFound& F = Found[SelFound];
	Src.RemovedTrees.Add(F.P);
	if (bReplace)
	{
		// (your own in its place: the size nearest its height)
		FOTGSourceTree T; T.P = F.P; T.Size = F.R < 3.5f ? TEXT("small") : F.R > 5.5f ? TEXT("large") : TEXT("medium");
		Src.UserTrees.Add(T);
		ClearPick();
		SelTree = Src.UserTrees.Num() - 1;
	}
	else ClearPick();
	Changed();
}

void SOTGCourseEditor::RemoveMany()
{
	if (!SelMany.Num()) return;
	Change();
	for (int32 i : SelMany) if (Found.IsValidIndex(i) && Found[i].Design != 3) Src.RemovedTrees.Add(Found[i].P);
	Status = FString::Printf(TEXT("%d trees removed"), SelMany.Num());
	ClearPick();
	Changed();
}

void SOTGCourseEditor::RestoreRemoved()
{
	if (!Src.RemovedTrees.IsValidIndex(SelRemoved)) return;
	Change();
	Src.RemovedTrees.RemoveAt(SelRemoved);
	ClearPick();
	Changed();
}

void SOTGCourseEditor::SaveForests(const FString& Say)
{
	if (GameDir.IsEmpty()) { Status = TEXT("Put the course in the game first: forests are kept with it"); Changed(); return; }
	Status = Design.Save(GameDir) ? Say + TEXT(" (saved for the game)") : FString(TEXT("Couldn't save the forests"));
	// (the game reads the course afresh next time it's played)
	if (UGameInstance* G = GI()) if (UOTGCourseSubsystem* C = G->GetSubsystem<UOTGCourseSubsystem>()) C->LastExport = nullptr;
	Changed();
}

void SOTGCourseEditor::SaveBiomes(const FString& Say)
{
	if (GameDir.IsEmpty()) { Status = TEXT("Put the course in the game first: its grass biomes are kept with it"); Changed(); return; }
	const int32 Was = SelArea;
	Biomes.Tidy();
	if (!Biomes.Areas.IsValidIndex(Was)) SelArea = INDEX_NONE;
	Status = Biomes.Save(GameDir) ? Say + TEXT(" (saved for the game)") : FString(TEXT("Couldn't save the grass biomes"));
	if (UGameInstance* G = GI()) if (UOTGCourseSubsystem* C = G->GetSubsystem<UOTGCourseSubsystem>()) C->LastExport = nullptr;
	Changed();
}

// A forest marked by a click: the found trees joined to the nearest one (each within 16 m of
// another), round them an outline (their hull, 5 m out)
void SOTGCourseEditor::MarkForest(const FVector2D& P)
{
	if (GameDir.IsEmpty() || !Found.Num()) { Status = TEXT("The trees show once the course is in the game: put it there first, or draw a forest"); Changed(); return; }
	constexpr double Link = 16, Reach = 25;
	int32 Seed = INDEX_NONE;
	double Best = Reach;
	for (int32 i = 0; i < Found.Num(); i++) { const double D = FVector2D::Distance(Found[i].P, P); if (D < Best) { Best = D; Seed = i; } }
	if (Seed == INDEX_NONE) { Status = TEXT("No trees there: click inside a group of them"); Changed(); return; }
	TMultiMap<FIntPoint, int32> Grid;
	auto Cell = [&](const FVector2D& Q) { return FIntPoint(FMath::FloorToInt32(Q.X / Link), FMath::FloorToInt32(Q.Y / Link)); };
	for (int32 i = 0; i < Found.Num(); i++) Grid.Add(Cell(Found[i].P), i);
	TArray<bool> Seen;
	Seen.SetNumZeroed(Found.Num());
	TArray<int32> In = { Seed };
	Seen[Seed] = true;
	for (int32 n = 0; n < In.Num(); n++)
	{
		const FVector2D Q = Found[In[n]].P;
		const FIntPoint C = Cell(Q);
		for (int32 dz = -1; dz <= 1; dz++)
			for (int32 dx = -1; dx <= 1; dx++)
				for (auto It = Grid.CreateConstKeyIterator(C + FIntPoint(dx, dz)); It; ++It)
					if (!Seen[It.Value()] && FVector2D::Distance(Found[It.Value()].P, Q) <= Link) { Seen[It.Value()] = true; In.Add(It.Value()); }
	}
	if (In.Num() < 4) { Status = FString::Printf(TEXT("Only %d trees there: a forest is a group of them (or draw one round them)"), In.Num()); Changed(); return; }
	// (round them: a grid of 4 m cells, those within 8 m of one of its trees in; the outline of that
	// - its cells' outer edges walked round, the longest loop - smoothed of its steps)
	constexpr double CellM = 4, Crown = 8;
	FBox2D Box(ForceInit);
	for (int32 i : In) Box += Found[i].P;
	Box = Box.ExpandBy(Crown + CellM * 2);
	const int32 GW = FMath::CeilToInt32(Box.GetSize().X / CellM), GH = FMath::CeilToInt32(Box.GetSize().Y / CellM);
	TArray<uint8> Occ;
	Occ.SetNumZeroed(GW * GH);
	const int32 R = FMath::CeilToInt32(Crown / CellM);
	for (int32 i : In)
	{
		const int32 CI = FMath::FloorToInt32((Found[i].P.X - Box.Min.X) / CellM), CJ = FMath::FloorToInt32((Found[i].P.Y - Box.Min.Y) / CellM);
		for (int32 dj = -R; dj <= R; dj++)
			for (int32 di = -R; di <= R; di++)
			{
				const int32 I2 = CI + di, J2 = CJ + dj;
				if (I2 < 0 || J2 < 0 || I2 >= GW || J2 >= GH) continue;
				const FVector2D C(Box.Min.X + (I2 + 0.5) * CellM, Box.Min.Y + (J2 + 0.5) * CellM);
				if (FVector2D::Distance(C, Found[i].P) <= Crown) Occ[J2 * GW + I2] = 1;
			}
	}
	auto IsIn = [&](int32 I2, int32 J2) { return I2 >= 0 && J2 >= 0 && I2 < GW && J2 < GH && Occ[J2 * GW + I2]; };
	// (each in cell's edges on an out one, going round it anticlockwise: from a corner to the next)
	TMultiMap<FIntPoint, FIntPoint> Next;
	for (int32 J2 = 0; J2 < GH; J2++)
		for (int32 I2 = 0; I2 < GW; I2++)
		{
			if (!IsIn(I2, J2)) continue;
			if (!IsIn(I2, J2 - 1)) Next.Add(FIntPoint(I2, J2), FIntPoint(I2 + 1, J2));
			if (!IsIn(I2 + 1, J2)) Next.Add(FIntPoint(I2 + 1, J2), FIntPoint(I2 + 1, J2 + 1));
			if (!IsIn(I2, J2 + 1)) Next.Add(FIntPoint(I2 + 1, J2 + 1), FIntPoint(I2, J2 + 1));
			if (!IsIn(I2 - 1, J2)) Next.Add(FIntPoint(I2, J2 + 1), FIntPoint(I2, J2));
		}
	TArray<FIntPoint> Outer;
	while (Next.Num())
	{
		TArray<FIntPoint> Loop;
		FIntPoint At = Next.CreateConstIterator().Key();
		const FIntPoint Start = At;
		for (int32 Guard = 0; Guard < 200000; Guard++)
		{
			FIntPoint* To = Next.Find(At);
			if (!To) break;
			const FIntPoint T = *To;
			Next.RemoveSingle(At, T);
			Loop.Add(At);
			At = T;
			if (At == Start) break;
		}
		if (Loop.Num() > Outer.Num()) Outer = MoveTemp(Loop);
	}
	// (to metres; the stairs' corners dropped where the line runs on, then points under 3 m off the line through their neighbours)
	TArray<FVector2D> Hull;
	for (const FIntPoint& Q : Outer) Hull.Add(FVector2D(Box.Min.X + Q.X * CellM, Box.Min.Y + Q.Y * CellM));
	TFunction<void(int32, int32, TArray<bool>&)> Keep = [&](int32 A, int32 B, TArray<bool>& K) {
		double Far = 0; int32 FarI = INDEX_NONE;
		const FVector2D D = Hull[B] - Hull[A];
		for (int32 k = A + 1; k < B; k++)
		{
			const double Off = D.IsNearlyZero() ? FVector2D::Distance(Hull[k], Hull[A]) : FMath::Abs(FVector2D::CrossProduct(D, Hull[k] - Hull[A])) / D.Size();
			if (Off > Far) { Far = Off; FarI = k; }
		}
		if (FarI != INDEX_NONE && Far > 4.0) { K[FarI] = true; Keep(A, FarI, K); Keep(FarI, B, K); }
	};
	if (Hull.Num() > 8)
	{
		const FVector2D First = Hull[0];   // (a copy: Add from its own element asserts)
		Hull.Add(First);
		TArray<bool> K;
		K.SetNumZeroed(Hull.Num());
		K[0] = K.Last() = true;
		const int32 Mid = Hull.Num() / 2;
		K[Mid] = true;
		Keep(0, Mid, K);
		Keep(Mid, Hull.Num() - 1, K);
		TArray<FVector2D> Kept;
		for (int32 k = 0; k < Hull.Num() - 1; k++) if (K[k]) Kept.Add(Hull[k]);
		// (and its corners cut, once: a quarter of the way along each side from either end)
		Hull.Reset();
		for (int32 k = 0; k < Kept.Num(); k++)
		{
			const FVector2D A = Kept[k], B = Kept[(k + 1) % Kept.Num()];
			Hull.Add(A * 0.75 + B * 0.25);
			Hull.Add(A * 0.25 + B * 0.75);
		}
	}
	if (Hull.Num() < 3) { Status = TEXT("Couldn't draw round those trees: draw the forest instead"); Changed(); return; }
	FOTGForest F;
	F.Name = FString::Printf(TEXT("Forest %d"), Design.Forests.Num() + 1);
	F.Pts = Hull;
	Design.Forests.Add(F);
	ClearPick();
	SelForest = Design.Forests.Num() - 1;
	SetMode(EMode::None);
	SaveForests(FString::Printf(TEXT("A forest of %d trees marked"), In.Num()));
}

void SOTGCourseEditor::DesignUndo(int32 FoundI)
{
	if (!Found.IsValidIndex(FoundI) || GameDir.IsEmpty()) return;
	const FFound F = Found[FoundI];
	if (F.Design == 2)
	{
		// (thinned by its species' "how many": all of the species back to as many as mapped)
		const FString Kind = F.Kind;
		if (FOTGSpeciesLook* L = Design.Species.Find(Kind)) L->Density = FMath::Max(L->Density, 1.f);
		Design.AllDensity = FMath::Max(Design.AllDensity, 1.f);
		for (FFound& G : Found) if (G.Design == 2 && Design.Copies(G.Kind, G.P.X, G.P.Y) > 0) G.Design = 0;
	}
	else
	{
		Design.ClearTreeEdit(F.P.X, F.P.Y);
		if (F.Design == 3) Found.RemoveAt(FoundI);
		else Found[FoundI].Design = Design.Copies(F.Kind, F.P.X, F.P.Y) == 0 ? 2 : 0;
	}
	if (!Design.Save(GameDir)) { Status = TEXT("Couldn't save the tree designer's changes"); Changed(); return; }
	// (the game reads the course afresh next time it's played)
	if (UGameInstance* G = GI()) if (UOTGCourseSubsystem* C = G->GetSubsystem<UOTGCourseSubsystem>()) C->LastExport = nullptr;
	Status = F.Design == 3 ? TEXT("Taken out of the game") : TEXT("Back in the game");
	ClearPick();
	Changed();
}

TArray<FVector2D> SOTGCourseEditor::HoleLine(const FOTGSourceHole& H) const
{
	// (from the back tee in use through the aim points to the green)
	TArray<FVector2D> L;
	for (const FString& T : FOTGCourseSource::TeesOrder()) if (const FVector2D* P = H.Tees.Find(T)) { L.Add(*P); break; }
	L.Append(H.Aims);
	L.Add(H.Green);
	return L;
}

void SOTGCourseEditor::FitHole()
{
	if (!Src.Holes.IsValidIndex(HoleI) || !Canvas.IsValid()) { FitAll(); return; }
	const FOTGSourceHole& H = Src.Holes[HoleI];
	TArray<FVector2D> P = HoleLine(H);
	for (const auto& T : H.Tees) P.Add(T.Value);
	Canvas->Fit(P, 40);
}

void SOTGCourseEditor::FitAll()
{
	TArray<FVector2D> P;
	for (const FOTGSourceHole& H : Src.Holes) P.Append(HoleLine(H));
	for (const FOTGSourceShape& S : Src.Shapes) if (S.Kind == TEXT("oob")) P.Append(S.Pts);
	if (Canvas.IsValid()) Canvas->Fit(P, 60);
}

void SOTGCourseEditor::AutoPins(FOTGSourceHole& H)
{
	// four pins spread over the green the hole's centre is in (editor.js autoPins)
	const FOTGSourceShape* G = Src.Shapes.FindByPredicate([&](const FOTGSourceShape& S) { return S.Kind == TEXT("green") && !S.bInner && InPoly(S.Pts, H.Green); });
	FVector2D Back = H.Aims.Num() ? H.Aims.Last() : H.Green - FVector2D(0, 100);
	if (!H.Aims.Num()) for (const FString& T : FOTGCourseSource::TeesOrder()) if (const FVector2D* P = H.Tees.Find(T)) { Back = *P; break; }
	const FVector2D Fwd = (H.Green - Back).GetSafeNormal(), Right(Fwd.Y, -Fwd.X);
	const FVector2D C = G ? Centroid(G->Pts) : H.Green;
	const double R = G ? FMath::Sqrt(PolyArea(G->Pts) / PI) : 10;
	const TArray<TPair<FString, FVector2D>> Off = { { TEXT("Thursday"), FVector2D(0, 0) }, { TEXT("Friday"), FVector2D(-0.45, -0.35) }, { TEXT("Saturday"), FVector2D(0.45, 0.35) }, { TEXT("Sunday"), FVector2D(0.4, -0.4) } };
	for (const auto& O : Off)
	{
		double S = 1;
		FVector2D P;
		do { P = C + (Fwd * O.Value.X + Right * O.Value.Y) * R * S; S *= 0.7; } while (G && !InPoly(G->Pts, P) && S > 0.05);
		H.Pins.Add(O.Key, P);
	}
}

void SOTGCourseEditor::StartReview()
{
	// the found trees standing in the line of play: within 30 m of a hole's line, and not removed
	Review.Reset();
	for (int32 i = 0; i < Found.Num(); i++)
	{
		if (Found[i].Design != 0 || Src.RemovedTrees.ContainsByPredicate([&](const FVector2D& R) { return FVector2D::Distance(R, Found[i].P) < 0.5; })) continue;
		for (const FOTGSourceHole& H : Src.Holes)
		{
			const TArray<FVector2D> L = HoleLine(H);
			bool bIn = false;
			for (int32 k = 0; k + 1 < L.Num() && !bIn; k++)
			{
				const FVector2D D = L[k + 1] - L[k];
				const double T = FMath::Clamp(FVector2D::DotProduct(Found[i].P - L[k], D) / FMath::Max(1.0, D.SizeSquared()), 0.0, 1.0);
				bIn = FVector2D::Distance(Found[i].P, L[k] + D * T) < 30;
			}
			if (bIn) { Review.Add(i); break; }
		}
	}
	ReviewAt = Review.Num() ? 0 : INDEX_NONE;
	Status = Review.Num() ? FString::Printf(TEXT("%d trees in the line of play"), Review.Num()) : FString(TEXT("No found trees in the line of play"));
	ShowReview();
}

void SOTGCourseEditor::ShowReview()
{
	if (!Review.IsValidIndex(ReviewAt)) { Changed(); return; }
	ClearPick();
	SelFound = Review[ReviewAt];
	Canvas->Cx = Found[SelFound].P.X; Canvas->Cz = Found[SelFound].P.Y;
	Canvas->Scale = FMath::Max(Canvas->Scale, 3.0);
	Changed();
}
