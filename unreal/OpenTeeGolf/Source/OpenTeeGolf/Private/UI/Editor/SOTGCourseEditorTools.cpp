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
	else if (M == EMode::Paint || M == EMode::Erase || M == EMode::Patch) Tab = 3;
	else if (M == EMode::Forest || M == EMode::Join) Tab = 2;
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

// The outline of a grid's cells that are in (Occ, GW x GH, row by row): their outer edges walked round,
// the longest loop, to metres (ToM: a cell corner's), smoothed of its steps (points under Tol off the line dropped)
static TArray<FVector2D> OutlineOf(const TArray<uint8>& Occ, int32 GW, int32 GH, TFunctionRef<FVector2D(const FIntPoint&)> ToM, double Tol)
{
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
		for (int32 Guard = 0; Guard < 4000000; Guard++)
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
	for (const FIntPoint& Q : Outer) Hull.Add(ToM(Q));
	TFunction<void(int32, int32, TArray<bool>&)> Keep = [&](int32 A, int32 B, TArray<bool>& K) {
		double Far = 0; int32 FarI = INDEX_NONE;
		const FVector2D D = Hull[B] - Hull[A];
		for (int32 k = A + 1; k < B; k++)
		{
			const double Off = D.IsNearlyZero() ? FVector2D::Distance(Hull[k], Hull[A]) : FMath::Abs(FVector2D::CrossProduct(D, Hull[k] - Hull[A])) / D.Size();
			if (Off > Far) { Far = Off; FarI = k; }
		}
		if (FarI != INDEX_NONE && Far > Tol) { K[FarI] = true; Keep(A, FarI, K); Keep(FarI, B, K); }
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
	return Hull;
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
	ForestOf(In);
}

// A grid's cells closed up (Occ, GW x GH): each gap of up to 2 x Most cells bridged - grown by R cells
// and shrunk back, the least R that leaves it in one piece. False if none does (left as it was)
static bool CloseUp(TArray<uint8>& Occ, int32 GW, int32 GH, int32 Most)
{
	auto Pieces = [&](const TArray<uint8>& O) {
		TArray<uint8> Seen;
		Seen.SetNumZeroed(O.Num());
		int32 N = 0;
		for (int32 s = 0; s < O.Num(); s++)
		{
			if (!O[s] || Seen[s]) continue;
			N++;
			TArray<int32> Stack = { s };
			Seen[s] = 1;
			while (Stack.Num())
			{
				const int32 Q = Stack.Pop(EAllowShrinking::No);
				const int32 I = Q % GW, J = Q / GW;
				for (int32 dj = -1; dj <= 1; dj++)
					for (int32 di = -1; di <= 1; di++)
					{
						const int32 I2 = I + di, J2 = J + dj;
						if (I2 < 0 || J2 < 0 || I2 >= GW || J2 >= GH) continue;
						if (O[J2 * GW + I2] && !Seen[J2 * GW + I2]) { Seen[J2 * GW + I2] = 1; Stack.Add(J2 * GW + I2); }
					}
			}
		}
		return N;
	};
	// (grown or shrunk by R: a square R cells out, along the rows then the columns)
	auto Spread = [&](const TArray<uint8>& O, int32 R, bool bGrow) {
		TArray<uint8> T = O, U = O;
		for (int32 J = 0; J < GH; J++)
			for (int32 I = 0; I < GW; I++)
			{
				uint8 V = bGrow ? 0 : 1;
				for (int32 d = -R; d <= R; d++)
				{
					const int32 I2 = I + d;
					const uint8 X = (I2 < 0 || I2 >= GW) ? 0 : O[J * GW + I2];
					V = bGrow ? (V | X) : (V & X);
				}
				T[J * GW + I] = V;
			}
		for (int32 J = 0; J < GH; J++)
			for (int32 I = 0; I < GW; I++)
			{
				uint8 V = bGrow ? 0 : 1;
				for (int32 d = -R; d <= R; d++)
				{
					const int32 J2 = J + d;
					const uint8 X = (J2 < 0 || J2 >= GH) ? 0 : T[J2 * GW + I];
					V = bGrow ? (V | X) : (V & X);
				}
				U[J * GW + I] = V;
			}
		return U;
	};
	if (Pieces(Occ) <= 1) return true;
	for (int32 R = 1; R <= Most; R++)
	{
		TArray<uint8> C = Spread(Spread(Occ, R, true), R, false);
		// (what was in stays in)
		for (int32 k = 0; k < C.Num(); k++) C[k] |= Occ[k];
		if (Pieces(C) <= 1) { Occ = MoveTemp(C); return true; }
	}
	return false;
}

static FString FreeForestName(const TArray<FOTGForest>& Forests)
{
	for (int32 n = 1; ; n++)
	{
		const FString Name = FString::Printf(TEXT("Forest %d"), n);
		if (!Forests.ContainsByPredicate([&](const FOTGForest& F) { return F.Name == Name; })) return Name;
	}
}

// A forest of these found trees: a grid of 4 m cells, those within 8 m of one of its trees in (gaps
// up to 40 m between them closed up); the outline of that, smoothed of its steps
void SOTGCourseEditor::ForestOf(const TArray<int32>& In)
{
	if (GameDir.IsEmpty()) { Status = TEXT("Put the course in the game first: forests are kept with it"); Changed(); return; }
	if (In.Num() < 2) { Status = TEXT("Pick at least 2 trees for a forest"); Changed(); return; }
	constexpr double CellM = 4, Crown = 8;
	constexpr int32 Most = 5;
	FBox2D Box(ForceInit);
	for (int32 i : In) if (Found.IsValidIndex(i)) Box += Found[i].P;
	Box = Box.ExpandBy(Crown + CellM * (Most + 2));
	const int32 GW = FMath::CeilToInt32(Box.GetSize().X / CellM), GH = FMath::CeilToInt32(Box.GetSize().Y / CellM);
	TArray<uint8> Occ;
	Occ.SetNumZeroed(GW * GH);
	const int32 R = FMath::CeilToInt32(Crown / CellM);
	for (int32 i : In)
	{
		if (!Found.IsValidIndex(i)) continue;
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
	if (!CloseUp(Occ, GW, GH, Most)) { Status = TEXT("Those trees are too far apart for one forest (more than 40 m between groups): pick them closer, or draw one round them"); Changed(); return; }
	TArray<FVector2D> Hull = OutlineOf(Occ, GW, GH, [&](const FIntPoint& Q) { return FVector2D(Box.Min.X + Q.X * CellM, Box.Min.Y + Q.Y * CellM); }, 4.0);
	if (Hull.Num() < 3) { Status = TEXT("Couldn't draw round those trees: draw the forest instead"); Changed(); return; }
	FOTGForest F;
	F.Name = FreeForestName(Design.Forests);
	F.Pts = Hull;
	Design.Forests.Add(F);
	ClearPick();
	SelForest = Design.Forests.Num() - 1;
	SetMode(EMode::None);
	SaveForests(FString::Printf(TEXT("%s: a forest of %d trees"), *F.Name, In.Num()));
}

// The forest at P joined to the picked one: both drawn into a grid of 4 m cells (those inside either),
// a gap of up to 40 m between them closed up, round it an outline. It keeps the picked one's name
// and settings
void SOTGCourseEditor::JoinForest(const FVector2D& P)
{
	if (!Design.Forests.IsValidIndex(SelForest)) { Status = TEXT("Pick a forest first, then join another to it"); SetMode(EMode::None); return; }
	int32 Other = INDEX_NONE;
	for (int32 f = Design.Forests.Num() - 1; f >= 0; f--) if (f != SelForest && Design.Forests[f].Contains(P.X, P.Y)) { Other = f; break; }
	if (Other == INDEX_NONE) { Status = TEXT("No other forest there: click inside the one to join"); Changed(); return; }
	const FOTGForest& A = Design.Forests[SelForest];
	const FOTGForest& B = Design.Forests[Other];
	constexpr double CellM = 4;
	constexpr int32 Most = 5;
	FBox2D Box(ForceInit);
	for (const FVector2D& Q : A.Pts) Box += Q;
	for (const FVector2D& Q : B.Pts) Box += Q;
	Box = Box.ExpandBy(CellM * (Most + 2));
	const int32 GW = FMath::CeilToInt32(Box.GetSize().X / CellM), GH = FMath::CeilToInt32(Box.GetSize().Y / CellM);
	if (int64(GW) * GH > 4 * 1024 * 1024) { Status = TEXT("Those forests are too big to join"); Changed(); return; }
	TArray<uint8> Occ;
	Occ.SetNumZeroed(GW * GH);
	for (int32 J = 0; J < GH; J++)
		for (int32 I = 0; I < GW; I++)
		{
			const double X = Box.Min.X + (I + 0.5) * CellM, Z = Box.Min.Y + (J + 0.5) * CellM;
			if (A.Contains(X, Z) || B.Contains(X, Z)) Occ[J * GW + I] = 1;
		}
	if (!CloseUp(Occ, GW, GH, Most)) { Status = FString::Printf(TEXT("%s and %s are more than 40 m apart: draw one round both instead"), *A.Name, *B.Name); Changed(); return; }
	TArray<FVector2D> Hull = OutlineOf(Occ, GW, GH, [&](const FIntPoint& Q) { return FVector2D(Box.Min.X + Q.X * CellM, Box.Min.Y + Q.Y * CellM); }, 3.0);
	if (Hull.Num() < 3) { Status = TEXT("Couldn't draw round those two: draw one round both instead"); Changed(); return; }
	const FString Say = FString::Printf(TEXT("%s joined to %s"), *B.Name, *A.Name);
	Design.Forests[SelForest].Pts = Hull;
	Design.Forests.RemoveAt(Other);
	const int32 Kept = Other < SelForest ? SelForest - 1 : SelForest;
	ClearPick();
	SelForest = Kept;
	SetMode(EMode::None);
	SaveForests(Say);
}

// A patch of tall grass marked by a click: the tall grass as the game has it (what it found in the
// aerial photo, then each stroke painted on or rubbed out in turn), the cells joined to the one
// clicked (corners touching count), round them an outline - an area of the biome picked
void SOTGCourseEditor::MarkPatch(const FVector2D& P)
{
	if (GameDir.IsEmpty()) { Status = TEXT("Put the course in the game first: its grass biomes are kept with it"); Changed(); return; }
	const FOTGBiome* Bio = OTGLibrary::FindIn(LibBiomes, AreaBiome);
	if (!Bio) { Status = TEXT("Pick a biome for it first (beside Draw an area)"); Changed(); return; }
	if (!HeatherPx.Num() || HeatherRes <= 0 || HeatherPxW <= 0 || HeatherPxH <= 0)
	{
		Status = TEXT("The tall grass the game found shows once the course has been played: draw an area instead");
		Changed();
		return;
	}
	const int32 W = HeatherPxW, H = HeatherPxH;
	const double Res = HeatherRes, X0 = HeatherX0, Z1 = HeatherZ1;
	auto Centre = [&](int32 I, int32 J) { return FVector2D(X0 + (I + 0.5) * Res, Z1 - (J + 0.5) * Res); };
	TArray<uint8> Grass = HeatherPx;
	for (const FOTGGrassStroke& St : Src.TallGrass)
	{
		const int32 N = St.Pts.Num();
		const uint8 V = St.bErase ? 0 : 1;
		for (int32 k = 0; k < FMath::Max(1, N - 1) && N; k++)
		{
			const FVector2D A = St.Pts[k], B = St.Pts[FMath::Min(k + 1, N - 1)];
			const int32 I0 = FMath::Max(0, FMath::FloorToInt32((FMath::Min(A.X, B.X) - St.R - X0) / Res)), I1 = FMath::Min(W - 1, FMath::FloorToInt32((FMath::Max(A.X, B.X) + St.R - X0) / Res));
			const int32 J0 = FMath::Max(0, FMath::FloorToInt32((Z1 - FMath::Max(A.Y, B.Y) - St.R) / Res)), J1 = FMath::Min(H - 1, FMath::FloorToInt32((Z1 - FMath::Min(A.Y, B.Y) + St.R) / Res));
			for (int32 J = J0; J <= J1; J++)
				for (int32 I = I0; I <= I1; I++)
				{
					const FVector2D C = Centre(I, J);
					if (FVector2D::Distance(C, FMath::ClosestPointOnSegment2D(C, A, B)) <= St.R) Grass[J * W + I] = V;
				}
		}
	}
	// (the cell clicked, or the nearest tall grass within 6 m)
	const int32 CI = FMath::FloorToInt32((P.X - X0) / Res), CJ = FMath::FloorToInt32((Z1 - P.Y) / Res);
	const int32 Reach = FMath::Max(1, FMath::CeilToInt32(6.0 / Res));
	int32 Seed = INDEX_NONE;
	double Best = TNumericLimits<double>::Max();
	for (int32 J = FMath::Max(0, CJ - Reach); J <= FMath::Min(H - 1, CJ + Reach); J++)
		for (int32 I = FMath::Max(0, CI - Reach); I <= FMath::Min(W - 1, CI + Reach); I++)
			if (Grass[J * W + I]) { const double D = FVector2D::DistSquared(Centre(I, J), P); if (D < Best) { Best = D; Seed = J * W + I; } }
	if (Seed == INDEX_NONE) { Status = TEXT("No tall grass there: click inside an amber patch"); Changed(); return; }
	TArray<uint8> In;
	In.SetNumZeroed(W * H);
	TArray<int32> Stack = { Seed };
	In[Seed] = 1;
	int32 Count = 0, IMin = W, IMax = 0, JMin = H, JMax = 0;
	while (Stack.Num())
	{
		const int32 Q = Stack.Pop(EAllowShrinking::No);
		const int32 I = Q % W, J = Q / W;
		Count++;
		IMin = FMath::Min(IMin, I); IMax = FMath::Max(IMax, I); JMin = FMath::Min(JMin, J); JMax = FMath::Max(JMax, J);
		for (int32 dj = -1; dj <= 1; dj++)
			for (int32 di = -1; di <= 1; di++)
			{
				const int32 I2 = I + di, J2 = J + dj;
				if (I2 < 0 || J2 < 0 || I2 >= W || J2 >= H) continue;
				const int32 Q2 = J2 * W + I2;
				if (Grass[Q2] && !In[Q2]) { In[Q2] = 1; Stack.Add(Q2); }
			}
	}
	const double AreaM2 = Count * Res * Res;
	if (AreaM2 < 30) { Status = FString::Printf(TEXT("That patch is only %.0f m2: paint it bigger, or draw an area"), AreaM2); Changed(); return; }
	// (to a grid of cells about 2 m a side - one in if any of its tall grass is - with a cell spare round it)
	const int32 K = FMath::Max(1, FMath::RoundToInt32(2.0 / Res));
	const int32 GI0 = IMin / K - 1, GJ0 = JMin / K - 1;
	const int32 GW = IMax / K - IMin / K + 3, GH = JMax / K - JMin / K + 3;
	TArray<uint8> Occ;
	Occ.SetNumZeroed(GW * GH);
	for (int32 J = JMin; J <= JMax; J++)
		for (int32 I = IMin; I <= IMax; I++)
			if (In[J * W + I]) Occ[(J / K - GJ0) * GW + (I / K - GI0)] = 1;
	const TArray<FVector2D> Hull = OutlineOf(Occ, GW, GH, [&](const FIntPoint& Q) { return FVector2D(X0 + (Q.X + GI0) * K * Res, Z1 - (Q.Y + GJ0) * K * Res); }, FMath::Max(1.5, K * Res * 0.75));
	if (Hull.Num() < 3) { Status = TEXT("Couldn't draw round that patch: draw an area instead"); Changed(); return; }
	Biomes.Use(*Bio);
	Biomes.Areas.Add({ Bio->Name, Hull });
	ClearPick();
	SelArea = Biomes.Areas.Num() - 1;
	SetMode(EMode::None);
	SaveBiomes(AreaM2 >= 10000 ? FString::Printf(TEXT("%s: a patch of %.1f ha marked"), *Bio->Name, AreaM2 / 10000) : FString::Printf(TEXT("%s: a patch of %.0f m2 marked"), *Bio->Name, AreaM2));
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
