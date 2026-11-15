// See SOTGEditor.h. (The map itself: SOTGEditCanvas.cpp.)

#include "SOTGEditor.h"

#include "OTGUi.h"
#include "OTGTextures.h"
#include "OTGCourseData.h"
#include "OTGCourseSubsystem.h"
#include "Game/OTGHelperSubsystem.h"
#include "Game/OTGTreeDesign.h"
#include "OTGQuickUpdate.h"
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

namespace
{
	UGameInstance* GI() { return GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetGameInstance() : nullptr; }
	constexpr double MToYd = 1.09361;
	const TArray<FString> DrawKinds = { TEXT("green"), TEXT("fairway"), TEXT("bunker"), TEXT("tee"), TEXT("water"), TEXT("penalty"), TEXT("oob") };
	FString KindLabel(const FString& K)
	{
		if (K == TEXT("green")) return TEXT("Green");
		if (K == TEXT("fairway")) return TEXT("Fairway");
		if (K == TEXT("bunker")) return TEXT("Bunker");
		if (K == TEXT("tee")) return TEXT("Tee box");
		if (K == TEXT("water")) return TEXT("Water");
		if (K == TEXT("penalty")) return TEXT("Penalty area");
		if (K == TEXT("oob")) return TEXT("Out of bounds");
		if (K == TEXT("trees")) return TEXT("Tree area");
		return K;
	}
	const TArray<TPair<FString, FString>>& BuildingTypes()
	{
		static const TArray<TPair<FString, FString>> T = { { TEXT("house"), TEXT("House") }, { TEXT("clubhouse"), TEXT("Clubhouse") }, { TEXT("garage"), TEXT("Garage") },
			{ TEXT("shed"), TEXT("Shed") }, { TEXT("barn"), TEXT("Barn") }, { TEXT("porch"), TEXT("Porch / shelter") }, { TEXT("commercial"), TEXT("Commercial") } };
		return T;
	}
	double PolyArea(const TArray<FVector2D>& P)
	{
		double A = 0;
		for (int32 i = 0, j = P.Num() - 1; i < P.Num(); j = i++) A += P[j].X * P[i].Y - P[i].X * P[j].Y;
		return FMath::Abs(A / 2);
	}
	FVector2D Centroid(const TArray<FVector2D>& P)
	{
		double A = 0, X = 0, Y = 0;
		const int32 N = P.Num() - (P.Num() > 1 && P[0] == P.Last() ? 1 : 0);
		for (int32 i = 0; i < N; i++)
		{
			const FVector2D& P0 = P[i]; const FVector2D& P1 = P[(i + 1) % N];
			const double C = P0.X * P1.Y - P1.X * P0.Y;
			A += C; X += (P0.X + P1.X) * C; Y += (P0.Y + P1.Y) * C;
		}
		if (FMath::Abs(A) < 1e-6) { FVector2D S = FVector2D::ZeroVector; for (int32 i = 0; i < N; i++) S += P[i]; return S / FMath::Max(1, N); }
		return FVector2D(X / (3 * A), Y / (3 * A));
	}
	bool InPoly(const TArray<FVector2D>& P, const FVector2D& Q)
	{
		bool In = false;
		for (int32 i = 0, j = P.Num() - 1; i < P.Num(); j = i++)
			if ((P[i].Y > Q.Y) != (P[j].Y > Q.Y) && Q.X < (P[j].X - P[i].X) * (Q.Y - P[i].Y) / (P[j].Y - P[i].Y) + P[i].X) In = !In;
		return In;
	}
	TSharedRef<SWidget> Section(const FString& Title, TSharedRef<SWidget> Body)
	{
		return Box(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)[Text(Title, 15, Gold, "Bold")]
			+ SVerticalBox::Slot().AutoHeight()[Body], FMargin(12, 8), Solid);
	}
	TSharedRef<SWidget> Row(const FString& Label, TSharedRef<SWidget> W, float LabelW = 120)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(LabelW)[Text(Label, 12, Soft)]]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[W];
	}
	TSharedRef<SWidget> Note(const FString& S)
	{
		return SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(400).Text(FText::FromString(S));
	}
	// the next of a list, at a click
	template <typename T> T Next(const TArray<T>& L, const T& V) { const int32 I = L.IndexOfByKey(V); return L[(I + 1) % L.Num()]; }
}

UOTGHelperSubsystem* SOTGCourseEditor::Helper() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UOTGHelperSubsystem>() : nullptr; }

void SOTGCourseEditor::Construct(const FArguments&, const FString& CourseId)
{
	Id = CourseId;
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[SAssignNew(Top, SBox)]
		+ SVerticalBox::Slot().FillHeight(1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 12, 0)[SAssignNew(Canvas, SOTGEditCanvas, this)]
			+ SHorizontalBox::Slot().AutoWidth()[SAssignNew(Side, SBox).WidthOverride(440)]
		]
	];
	Status = TEXT("Loading the course…");
	Rebuild();
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SOTGCourseEditor::Pulse));
	UOTGHelperSubsystem* H = Helper();
	if (!H) { Status = TEXT("No course helper"); Rebuild(); return; }
	TWeakPtr<SOTGCourseEditor> Weak = SharedThis(this);
	H->LoadSource(Id, [Weak](TSharedPtr<FJsonObject> Course, TSharedPtr<FJsonObject> Real, const FString& Error) {
		TSharedPtr<SOTGCourseEditor> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		FString Why = Error;
		if (Why.IsEmpty() && !Self->Src.Load(Self->Id, Course, Real, &Why)) {}
		if (!Why.IsEmpty()) { Self->Status = TEXT("Couldn't open the course: ") + Why; Self->Rebuild(); return; }
		Self->Loaded();
	});
}

void SOTGCourseEditor::Loaded()
{
	bLoaded = true;
	Applied = Src;
	Status = Src.CourseJson.IsValid() && Src.CourseJson->HasField(TEXT("GolfSimBuilder")) && Src.CourseJson->GetObjectField(TEXT("GolfSimBuilder"))->HasTypedField<EJson::Boolean>(TEXT("needsReview"))
		&& Src.CourseJson->GetObjectField(TEXT("GolfSimBuilder"))->GetBoolField(TEXT("needsReview")) ? FString(TEXT("Some parts were guessed: check the tees, pins and par on each hole.")) : FString();
	MakeShade();
	LoadExtras();
	FitHole();
	Changed();
}

void SOTGCourseEditor::MakeShade()
{
	// the ground's shading (editor.js makeHillshade), for courses without their photo in the game
	const FBox2D B = Src.GridBounds();
	if (!B.bIsValid || !Src.HasHeights()) return;
	const int32 W = 512, H = FMath::Clamp(int32(512 * B.GetSize().Y / FMath::Max(1.0, B.GetSize().X)), 64, 1024);
	const double Dx = B.GetSize().X / W, Dz = B.GetSize().Y / H;
	TArray<FColor> Px;
	Px.SetNumUninitialized(W * H);
	for (int32 j = 0; j < H; j++)
		for (int32 i = 0; i < W; i++)
		{
			const double X = B.Min.X + (i + 0.5) * Dx, Z = B.Max.Y - (j + 0.5) * Dz;
			const double Gx = (Src.HeightAt(X + Dx, Z) - Src.HeightAt(X - Dx, Z)) / (2 * Dx), Gz = (Src.HeightAt(X, Z + Dz) - Src.HeightAt(X, Z - Dz)) / (2 * Dz);
			const FVector N = FVector(-Gx * 3, -Gz * 3, 1).GetSafeNormal();
			const double L = FMath::Clamp(FVector::DotProduct(N, FVector(-0.5, 0.5, 0.7).GetSafeNormal()), 0.0, 1.0);
			const uint8 V = uint8(FMath::Clamp(40 + 150 * L, 0.0, 255.0));
			Px[j * W + i] = FColor(uint8(V * 0.75), V, uint8(V * 0.7), 255);
		}
	UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!T) return;
	void* Data = T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Px.GetData(), Px.Num() * sizeof(FColor));
	T->GetPlatformData()->Mips[0].BulkData.Unlock();
	T->UpdateResource();
	Shade.Reset(T);
	ShadeBrush = MakeShared<FSlateBrush>();
	ShadeBrush->SetResourceObject(T);
	ShadeBrush->ImageSize = FVector2D(W, H);
	ShadeX0 = B.Min.X; ShadeZ1 = B.Max.Y; ShadeW = B.GetSize().X; ShadeH = B.GetSize().Y;
}

void SOTGCourseEditor::LoadExtras()
{
	// the course as the game has it (exported): its aerial photo, and the trees it plants
	UGameInstance* G = GI();
	UOTGCourseSubsystem* C = G ? G->GetSubsystem<UOTGCourseSubsystem>() : nullptr;
	if (C)
	{
		const FString Dir = FPaths::Combine(C->CoursesFolder(), Id);
		FString Text;
		TSharedPtr<FJsonObject> J;
		if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(Dir, TEXT("course.json"))) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) && J.IsValid())
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (J->TryGetObjectField(TEXT("courseOrigin"), O) && J->TryGetNumberField(TEXT("sizeMetres"), PhotoSizeM))
			{
				PhotoX0 = (*O)->GetNumberField(TEXT("x")); PhotoZ1 = (*O)->GetNumberField(TEXT("z"));
				if (UTexture2D* T = otg::LoadTexture(FPaths::Combine(Dir, TEXT("photo.jpg")), otg::ETexKind::Photo, 4096, GetTransientPackage()))
				{
					Photo.Reset(T);
					PhotoBrush = MakeShared<FSlateBrush>();
					PhotoBrush->SetResourceObject(T);
					PhotoBrush->ImageSize = FVector2D(1024, 1024);
				}
			}
		}
		if (UOTGCourseData* D = C->LoadCourse(Id))
			for (const FOTGTreeShape& T : D->Trees)
			{
				FFound F;
				F.P = FVector2D(T.X, T.Z);
				F.R = T.bCone ? T.C : T.B;
				F.Kind = T.Kind;
				Found.Add(F);
			}
	}
	// the tall grass the game found in the aerial photo ({ minX, maxZ, res, w, h, runs: [start, length, ...] }), amber
	if (UOTGHelperSubsystem* H = Helper())
	{
		TWeakPtr<SOTGCourseEditor> Weak = SharedThis(this);
		H->Get(FString::Printf(TEXT("/api/courses/%s/heather"), *Id), [Weak](int32 Code, TSharedPtr<FJsonValue> J, const FString&) {
			TSharedPtr<SOTGCourseEditor> Self = Weak.Pin();
			const TSharedPtr<FJsonObject> O = J.IsValid() ? J->AsObject() : nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Runs = nullptr;
			if (!Self.IsValid() || Code != 200 || !O.IsValid() || !O->TryGetArrayField(TEXT("runs"), Runs)) return;
			const int32 W = O->GetIntegerField(TEXT("w")), Hh = O->GetIntegerField(TEXT("h"));
			const double Res = O->GetNumberField(TEXT("res"));
			if (W <= 0 || Hh <= 0 || int64(W) * Hh > 16 * 1024 * 1024) return;
			TArray<FColor> Px;
			Px.Init(FColor(0, 0, 0, 0), W * Hh);
			for (int32 k = 0; k + 1 < Runs->Num(); k += 2)
			{
				const int32 A = int32((*Runs)[k]->AsNumber()), L = int32((*Runs)[k + 1]->AsNumber());
				for (int32 q = A; q < A + L && q < Px.Num(); q++) if (q >= 0) Px[q] = FColor(240, 160, 40, 95);   // (amber)
			}
			UTexture2D* T = UTexture2D::CreateTransient(W, Hh, PF_B8G8R8A8);
			if (!T) return;
			void* Data = T->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(Data, Px.GetData(), Px.Num() * sizeof(FColor));
			T->GetPlatformData()->Mips[0].BulkData.Unlock();
			T->Filter = TF_Nearest;
			T->UpdateResource();
			Self->Heather.Reset(T);
			Self->HeatherBrush = MakeShared<FSlateBrush>();
			Self->HeatherBrush->SetResourceObject(T);
			Self->HeatherBrush->ImageSize = FVector2D(W, Hh);
			Self->HeatherX0 = O->GetNumberField(TEXT("minX")); Self->HeatherZ1 = O->GetNumberField(TEXT("maxZ"));
			Self->HeatherW = W * Res; Self->HeatherH = Hh * Res;
			Self->Changed();
		});
	}
	// the buildings round the course
	if (UOTGHelperSubsystem* H = Helper())
	{
		TWeakPtr<SOTGCourseEditor> Weak = SharedThis(this);
		H->Get(FString::Printf(TEXT("/api/courses/%s/buildings"), *Id), [Weak](int32 Code, TSharedPtr<FJsonValue> J, const FString&) {
			TSharedPtr<SOTGCourseEditor> Self = Weak.Pin();
			const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
			if (!Self.IsValid() || Code != 200 || !J.IsValid() || !J->TryGetArray(L)) return;
			for (const TSharedPtr<FJsonValue>& V : *L)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
				if (!O.IsValid() || !O->TryGetArrayField(TEXT("pts"), P) || P->Num() < 4) continue;
				FBuilding B;
				for (const TSharedPtr<FJsonValue>& Q : *P) { const TArray<TSharedPtr<FJsonValue>>& A = Q->AsArray(); if (A.Num() >= 2) B.Pts.Add(FVector2D(A[0]->AsNumber(), A[1]->AsNumber())); }
				O->TryGetStringField(TEXT("name"), B.Name);
				O->TryGetStringField(TEXT("kind"), B.Kind);
				// (its key: its centre to the metre, as buildingtypes.js buildingKey)
				const FVector2D Ct = Centroid(B.Pts);
				B.Key = FString::Printf(TEXT("%d,%d"), FMath::FloorToInt32(Ct.X + 0.5), FMath::FloorToInt32(Ct.Y + 0.5));
				B.Area = PolyArea(B.Pts);
				Self->Buildings.Add(B);
			}
			Self->Changed();
		});
	}
}

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
	SelShape = SelTree = SelFound = SelRemoved = SelBuilding = INDEX_NONE;
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
		if (FVector2D::Distance(F.P, P) <= CutR && !Src.RemovedTrees.ContainsByPredicate([&](const FVector2D& R) { return FVector2D::Distance(R, F.P) < 0.5; }))
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
	for (int32 i : SelMany) if (Found.IsValidIndex(i)) Src.RemovedTrees.Add(Found[i].P);
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
		if (Src.RemovedTrees.ContainsByPredicate([&](const FVector2D& R) { return FVector2D::Distance(R, Found[i].P) < 0.5; })) continue;
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

// ------------------------------------------------------------------ saving

void SOTGCourseEditor::Save(int32 Then)
{
	UOTGHelperSubsystem* H = Helper();
	if (!H || !bLoaded) return;
	if (!Src.Holes.Num()) { Status = TEXT("A course needs at least one hole"); Changed(); return; }
	Status = TEXT("Saving…");
	Changed();
	TWeakPtr<SOTGCourseEditor> Weak = SharedThis(this);
	H->SaveSource(Id, Src.SaveBody(), [Weak, Then](int32 Code, TSharedPtr<FJsonValue> J, const FString& Body) {
		TSharedPtr<SOTGCourseEditor> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		if (Code != 200) { Self->Status = FString::Printf(TEXT("Save failed (%d): %s"), Code, *Body.Left(200)); Self->Changed(); return; }
		Self->bDirty = false;
		Self->Status = FString::Printf(TEXT("Saved %s"), *FDateTime::Now().ToString(TEXT("%H:%M:%S")));
		if (Then == 1) Self->Quick();
		if (Then == 2)
		{
			if (UOTGHelperSubsystem* H2 = Self->Helper())
			{
				H2->Export(Self->Id); Self->bExporting = true; Self->Applied = Self->Src;
				Self->Status = TEXT("Saved, and queued to be put in the game");
			}
		}
		Self->Changed();
	});
}

void SOTGCourseEditor::Quick()
{
	UGameInstance* G = GI();
	UOTGCourseSubsystem* C = G ? G->GetSubsystem<UOTGCourseSubsystem>() : nullptr;
	if (!C || bQuicking) return;
	const FString Dir = FPaths::Combine(C->CoursesFolder(), Id);
	if (!FPaths::FileExists(FPaths::Combine(Dir, TEXT("play.json")))) { Status = TEXT("Saved. It isn't in the game yet: put it in the game (full) once"); Changed(); return; }
	bQuicking = true;
	Status = TEXT("Saved. Updating the game…");
	Changed();
	TWeakPtr<SOTGCourseEditor> Weak = SharedThis(this);
	const FOTGCourseSource Before = Applied, After = Src;
	Async(EAsyncExecution::Thread, [Weak, Dir, Before, After]() {
		const otg::FQuickResult R = otg::QuickUpdate(Dir, Before, After);
		AsyncTask(ENamedThreads::GameThread, [Weak, R, After]() {
			TSharedPtr<SOTGCourseEditor> Self = Weak.Pin();
			if (!Self.IsValid()) return;
			Self->bQuicking = false;
			if (R.bOk)
			{
				Self->Applied = After;
				// (the game reads the course afresh next time it's played)
				if (UGameInstance* G2 = GI()) if (UOTGCourseSubsystem* C2 = G2->GetSubsystem<UOTGCourseSubsystem>()) C2->LastExport = nullptr;
				Self->Status = FString::Printf(TEXT("In the game (%.1f s)."), R.Seconds) + (R.NeedsFull.Num() ? TEXT(" Still to come with Put in the game (full): ") + FString::Join(R.NeedsFull, TEXT(", ")) : FString());
				Self->bExported = true;
			}
			else Self->Status = TEXT("Saved, but the game couldn't be updated: ") + R.Message;
			Self->Changed();
		});
	});
}

EActiveTimerReturnType SOTGCourseEditor::Pulse(double Time, float Dt)
{
	if (bExporting)
		if (UOTGHelperSubsystem* H = Helper())
		{
			const FOTGHelperJob* Jp = H->FindJob(Id);
			const FOTGHelperJob& J = Jp ? *Jp : H->GetJob();
			const FString Now = FString::Printf(TEXT("%s %.0f %s"), *J.State, J.Frac * 100, *J.Message);
			if (Now != JobShown)
			{
				JobShown = Now;
				if (J.Kind == TEXT("export") && J.CourseId == Id)
				{
					if (J.IsWaiting()) Status = TEXT("Queued to be put in the game: ") + J.Message;
					else if (J.IsRunning()) Status = FString::Printf(TEXT("Putting it in the game: %d%%  %s"), FMath::RoundToInt(J.Frac * 100), *J.Message);
					else { bExporting = false; bExported = J.State == TEXT("done"); Status = bExported ? FString(TEXT("In the game: play it from the menu")) : TEXT("Putting it in the game didn't work: ") + J.Message; }
					if (Top.IsValid()) Top->SetContent(TopBar());
				}
			}
		}
	if (PanelKey != PanelBuilt) Rebuild();
	// (the test)
	FString T;
	if (TestStep == 0 && bLoaded && FParse::Value(FCommandLine::Get(), TEXT("editortest="), T) && T == Id) { TestStep = 1; TestAt = FPlatformTime::Seconds() + 4; }
	if (TestStep > 0 && TestStep < 9 && FPlatformTime::Seconds() >= TestAt)
	{
		auto Shot = [](const TCHAR* N) { if (GEngine) GEngine->Exec(nullptr, *FString::Printf(TEXT("OTG.Shot %s"), N)); };
		int32 WantTab = -1;
		if (TestStep == 1 && FParse::Value(FCommandLine::Get(), TEXT("editortesttab="), WantTab) && Tab != WantTab) { Tab = WantTab; Changed(); TestAt = FPlatformTime::Seconds() + 1; }
		else if (TestStep == 1 && FParse::Param(FCommandLine::Get(), TEXT("editortestlook")) && !bColourSpecies) { bColourSpecies = true; Canvas->Scale = 2.5; Changed(); TestAt = FPlatformTime::Seconds() + 2; }
		else if (TestStep == 1) { Shot(TEXT("editor_open")); TestStep = 2; TestAt = FPlatformTime::Seconds() + 2; }
		else if (TestStep == 2)
		{
			if (FParse::Param(FCommandLine::Get(), TEXT("editortestall")) && Src.Holes.IsValidIndex(HoleI))
			{
				// each tool's change, as the mouse would make it: a bunker drawn by the green, a tree,
				// a tall grass stroke, a found tree removed, a building named, a species turned off
				const FVector2D G = Src.Holes[HoleI].Green;
				SetMode(EMode::Draw, TEXT("bunker"));
				DrawPts = { G + FVector2D(12, 0), G + FVector2D(18, 3), G + FVector2D(17, 9), G + FVector2D(11, 7) };
				FinishDraw();
				Change(); FOTGSourceTree NewTree; NewTree.P = G + FVector2D(-30, 10); NewTree.Size = TEXT("large"); NewTree.Kind = TEXT("oak"); Src.UserTrees.Add(NewTree);
				Change(); FOTGGrassStroke St; St.Pts = { G + FVector2D(-40, -20), G + FVector2D(-30, -25), G + FVector2D(-20, -22) }; St.R = 4; Src.TallGrass.Add(St);
				if (Found.Num()) { ClearPick(); SelFound = 0; RemoveFound(false); }
				if (Buildings.Num()) { Change(); Src.BuildingTypes.Add(Buildings[0].Key, TEXT("clubhouse")); }
				if (Found.Num()) { Change(); Src.TreesOff.Add(Found.Last().Kind); }
				UE_LOG(LogTemp, Display, TEXT("editortest: all tools: %d shapes, %d own trees, %d strokes, %d removed, %d building types, %d species off; undo steps %d"),
					Src.Shapes.Num(), Src.UserTrees.Num(), Src.TallGrass.Num(), Src.RemovedTrees.Num(), Src.BuildingTypes.Num(), Src.TreesOff.Num(), UndoStack.Num());
				Undo(); Undo();
				UE_LOG(LogTemp, Display, TEXT("editortest: after two undos: %d species off, %d building types"), Src.TreesOff.Num(), Src.BuildingTypes.Num());
				Changed();
				TestStep = 3; TestAt = FPlatformTime::Seconds() + 3;
				return EActiveTimerReturnType::Continue;
			}
			if (!FParse::Param(FCommandLine::Get(), TEXT("editortestfix"))) { TestStep = 9; FPlatformMisc::RequestExit(false, TEXT("editortest")); return EActiveTimerReturnType::Continue; }
			for (int32 i = 0; i < Src.Holes.Num(); i++)
				if (Src.Holes[i].Par >= 10)
				{
					HoleI = i; FitHole(); Change();
					UE_LOG(LogTemp, Display, TEXT("editortest: hole %d par %d -> 4"), Src.Holes[i].Number, Src.Holes[i].Par);
					Src.Holes[i].Par = 4; Changed();
				}
			TestStep = 3; TestAt = FPlatformTime::Seconds() + 2;
		}
		else if (TestStep == 3) { Shot(TEXT("editor_fixed")); Save(FParse::Param(FCommandLine::Get(), TEXT("editortestquick")) ? 1 : 0); TestStep = 4; TestAt = FPlatformTime::Seconds() + 5; }
		else if (TestStep == 4 && (bQuicking || Status.StartsWith(TEXT("Saving")))) TestAt = FPlatformTime::Seconds() + 1;
		else if (TestStep == 4) { UE_LOG(LogTemp, Display, TEXT("editortest: %s (dirty %d)"), *Status, bDirty ? 1 : 0); TestStep = 9; FPlatformMisc::RequestExit(false, TEXT("editortest")); }
	}
	return EActiveTimerReturnType::Continue;
}

// ------------------------------------------------------------------ the panels

// A mowing pattern, light and dark as the ground's material draws it (mowing.js mowShade), 1 px = 0.25 m
void SOTGMowSwatch::Construct(const FArguments& A) { Id = A._Pattern; Width = A._WidthM; }

int32 SOTGMowSwatch::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const int32 N = 16;
	const double Cell = 64.0 / N, M = 0.25 * 64 / N * 2;   // (each cell this many metres)
	auto S = [this](double A) { return FMath::Clamp(FMath::Sin(A / Width * PI) * 6, -1.0, 1.0); };
	for (int32 j = 0; j < N; j++)
		for (int32 i = 0; i < N; i++)
		{
			const double U = i * M, V = j * M, D1 = (U + V) / UE_SQRT_2, D2 = (V - U) / UE_SQRT_2;
			const double Sh = Id == TEXT("stripes") ? S(U) : Id == TEXT("diagonal") ? S(D1) : Id == TEXT("checker") ? S(U) * S(V) : Id == TEXT("diamond") ? S(D1) * S(D2) : 0;
			const float L = float(0.32 + 0.06 * Sh);
			FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2D(Cell + 0.5, Cell + 0.5), FSlateLayoutTransform(FVector2D(i * Cell, j * Cell))), White, ESlateDrawEffect::None, FLinearColor(L * 0.55f, L, L * 0.4f, 1));
		}
	return Layer;
}

void SOTGCourseEditor::Rebuild()
{
	PanelBuilt = PanelKey;
	if (Top.IsValid()) Top->SetContent(TopBar());
	if (!Side.IsValid()) return;
	if (!bLoaded) { Side->SetContent(SNullWidget::NullWidget); return; }
	// the tabs: one section at a time, no scrolling past the others
	static const TCHAR* Names[] = { TEXT("Hole"), TEXT("Shapes"), TEXT("Trees"), TEXT("Tall grass"), TEXT("Course"), TEXT("Plants"), TEXT("Buildings") };
	TSharedRef<SWrapBox> Tabs = SNew(SWrapBox).UseAllottedSize(true);
	for (int32 t = 0; t < UE_ARRAY_COUNT(Names); t++)
		Tabs->AddSlot().Padding(0, 0, 4, 4)[Button(Names[t], [this, t]() { Tab = t; Changed(); }, 13, Tab == t)];
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	auto Add = [&](TSharedRef<SWidget> W) { V->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[W]; };
	// (what's picked on the map, over the tab: only while something is)
	const bool bPicked = Src.Shapes.IsValidIndex(SelShape) || Src.UserTrees.IsValidIndex(SelTree) || Found.IsValidIndex(SelFound) || SelMany.Num()
		|| Src.RemovedTrees.IsValidIndex(SelRemoved) || Buildings.IsValidIndex(SelBuilding);
	if (bPicked) Add(SelectionPanel());
	switch (Tab)
	{
	case 0: Add(HolePanel()); break;
	case 1: Add(ShapePanel()); break;
	case 2: Add(TreePanel()); break;
	case 3: Add(GrassPanel()); break;
	case 4: Add(CoursePanel()); break;
	case 5: Add(PlantPanel()); break;
	default: Add(BuildingPanel()); break;
	}
	if (!bPicked)
		Add(Note(TEXT("Click something on the map to pick it: a shape, a tree, a building. Shift-drag a box round found trees. The mouse wheel zooms; drag (or right-drag) moves the map.")));
	Side->SetContent(SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[Box(Tabs, FMargin(8, 8, 4, 4), Solid)]
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot()[V]]);
}

TSharedRef<SWidget> SOTGCourseEditor::TopBar()
{
	auto Hint = [this]() -> FString {
		switch (Mode)
		{
		case EMode::Place: return FString::Printf(TEXT("Click the map to place: %s %s  (Esc to cancel)"), *ModeKind, *ModeKey);
		case EMode::Draw: return FString::Printf(TEXT("Drawing a %s: click to add points, double-click or Enter to finish, Backspace takes the last off, Esc cancels"), *KindLabel(ModeKind).ToLower());
		case EMode::AddTree: return TEXT("Click the map to add trees, one per click  (Esc when done)");
		case EMode::Cut: return FString::Printf(TEXT("Remove trees: click or drag over them (%.0f m wide)  (Esc when done)"), CutR * 2);
		case EMode::Paint: return FString::Printf(TEXT("Paint tall grass: hold the button and drag (%.0f m wide); right-drag moves the map  (Esc when done)"), BrushR * 2);
		case EMode::Erase: return FString::Printf(TEXT("Rub tall grass out: hold the button and drag (%.0f m wide)  (Esc when done)"), BrushR * 2);
		default: return Status;
		}
	};
	return Box(SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 16, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Text(bLoaded ? Src.Name : Id, 18, Ink, "Bold")]
			+ SVerticalBox::Slot().AutoHeight()[Text(bDirty ? TEXT("● unsaved changes") : TEXT("Course editor"), 11, bDirty ? Gold : Soft)]
		]
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Mode != EMode::None ? Gold : Soft).AutoWrapText(true).Text(FText::FromString(Hint()))]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[Button(TEXT("Undo"), [this]() { Undo(); }, 12)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[Button(TEXT("Hole"), [this]() { FitHole(); }, 12)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[Button(TEXT("Course"), [this]() { FitAll(); }, 12)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[Button(bShowPhoto && PhotoBrush.IsValid() ? TEXT("Photo") : TEXT("Shading"), [this]() { bShowPhoto = !bShowPhoto; Changed(); }, 12)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 4, 0)[SNew(SBox).ToolTipText(FText::FromString(TEXT("Keep your changes (the course's source). The game doesn't see them until you update it.")))[Button(TEXT("Save"), [this]() { Save(0); }, 14, bDirty)]]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[SNew(SBox).ToolTipText(FText::FromString(TEXT("Save, then patch the course the game plays in a few seconds: shapes, trees, tall grass, the ground. Anything it can't patch is named afterwards: that needs Put in the game (full).")))[Button(bQuicking ? TEXT("Updating the game…") : TEXT("Save and update the game"), [this]() { if (!bQuicking && !bExporting) Save(1); }, 14, !bQuicking && !bExporting)]]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[SNew(SBox).ToolTipText(FText::FromString(TEXT("Save, then rebuild the whole course for the game from scratch (5 to 15 minutes, in the background): needed the first time, and for anything the quick update can't do.")))[Button(bExporting ? TEXT("Putting it in the game…") : TEXT("Put in the game (full)"), [this]() { if (!bExporting && !bQuicking) Save(2); }, 12)]]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)[Button(bDirty && Status == TEXT("Close without saving?") ? TEXT("Yes, close") : TEXT("Close"), [this]() {
			if (bDirty && Status != TEXT("Close without saving?")) { Status = TEXT("Close without saving?"); Changed(); return; }
			if (OnClose) OnClose(bExported);
		}, 13)], FMargin(14, 8), Solid);
}

TSharedRef<SWidget> SOTGCourseEditor::CoursePanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Name"), SNew(SEditableTextBox).Text(FText::FromString(Src.Name)).Font(Font(13))
		.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { if (T.ToString() != Src.Name) { Change(); Src.Name = T.ToString(); Changed(); } }))];
	V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Location"), SNew(SEditableTextBox).Text(FText::FromString(Src.Location)).Font(Font(13))
		.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { if (T.ToString() != Src.Location) { Change(); Src.Location = T.ToString(); Changed(); } }))];
	V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Green speed (stimp)"), SNew(SBox).WidthOverride(100)[SNew(SSpinBox<double>).MinValue(6).MaxValue(14).Delta(0.5).Value(Src.Stimp)
		.OnValueCommitted_Lambda([this](double X, ETextCommit::Type) { if (X != Src.Stimp) { Change(); Src.Stimp = FMath::Clamp(X, 6.0, 14.0); Changed(); } })])];
	auto Look = [this, &V](const FString& Label, bool* Photo) {
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(Label, Button(*Photo ? TEXT("Aerial photo colours") : TEXT("Game colours (even, groomed)"), [this, Photo]() { Change(); *Photo = !*Photo; Changed(); }, 12))];
	};
	Look(TEXT("Fairways & tees"), &Src.bPhotoFairway);
	Look(TEXT("Greens"), &Src.bPhotoGreen);
	Look(TEXT("Rough"), &Src.bPhotoRough);
	auto Slide = [this, &V](const FString& Label, float* Val, float Lo, float Hi, TFunction<FString(float)> Fmt) {
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(Label, SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1)[SNew(SSlider).Value_Lambda([Val, Lo, Hi]() { return (*Val - Lo) / (Hi - Lo); })
				.OnMouseCaptureBegin_Lambda([this]() { Change(); })
				.OnValueChanged_Lambda([Val, Lo, Hi](float X) { *Val = FMath::RoundToFloat((Lo + (Hi - Lo) * X) * 20) / 20; })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)[SNew(SBox).WidthOverride(60)[SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Ink).Text_Lambda([Val, Fmt]() { return FText::FromString(Fmt(*Val)); })]])];
	};
	Slide(TEXT("Rough height"), &Src.RoughScale, 0.3f, 1.6f, [](float S) { return FString::Printf(TEXT("%d cm"), FOTGCourseSource::RoughCm(S)); });
	Slide(TEXT("Tall grass height"), &Src.NativeScale, 0.4f, 1.8f, [](float S) { return FString::Printf(TEXT("%d cm"), FOTGCourseSource::NativeCm(S)); });
	auto Mow = [this, &V](const FString& Label, FString* Id, double WidthM) {
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(Label, SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Button(FOTGCourseSource::MowLabel(*Id), [this, Id]() { Change(); *Id = Next(FOTGCourseSource::MowPatterns(), *Id); Changed(); }, 12)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[SNew(SOTGMowSwatch).Pattern(*Id).WidthM(WidthM)])];
	};
	Mow(TEXT("Fairway mowing"), &Src.MowFairway, 3.5);
	Mow(TEXT("Green mowing"), &Src.MowGreen, 1);
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Note(TEXT("The game's grass designer (R in a round) sets what the rough and tall grass are made of."))];
	return Section(TEXT("Course"), V);
}

TSharedRef<SWidget> SOTGCourseEditor::HolePanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	// the holes: one to edit, a new one, this one gone
	TSharedRef<SWrapBox> Btns = SNew(SWrapBox).UseAllottedSize(true);
	for (int32 i = 0; i < Src.Holes.Num(); i++)
		Btns->AddSlot().Padding(0, 0, 4, 4)[Button(FString::FromInt(Src.Holes[i].Number), [this, i]() { HoleI = i; SetMode(EMode::None); FitHole(); Changed(); }, 12, i == HoleI)];
	V->AddSlot().AutoHeight()[Btns];
	V->AddSlot().AutoHeight().Padding(0, 2, 0, 6)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("+ Add hole"), [this]() {
			Change();
			FOTGSourceHole H;
			for (const FOTGSourceHole& O : Src.Holes) H.Number = FMath::Max(H.Number, O.Number + 1);
			const FVector2D C(Canvas->Cx, Canvas->Cz);
			H.Green = C + FVector2D(0, 120);
			H.Tees.Add(TEXT("White"), C - FVector2D(0, 120));
			AutoPins(H);
			Src.Holes.Add(H);
			HoleI = Src.Holes.Num() - 1;
			Status = TEXT("A new hole in the middle of the map: place its tees, green and pins");
			Changed();
		}, 12)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(Status == TEXT("Delete this hole?") ? TEXT("Yes, delete it") : TEXT("Delete hole"), [this]() {
			if (!Src.Holes.IsValidIndex(HoleI)) return;
			if (Status != TEXT("Delete this hole?")) { Status = TEXT("Delete this hole?"); Changed(); return; }
			Change();
			Src.Holes.RemoveAt(HoleI);
			HoleI = FMath::Clamp(HoleI, 0, FMath::Max(0, Src.Holes.Num() - 1));
			Status.Empty();
			Changed();
		}, 12)]
	];
	if (!Src.Holes.IsValidIndex(HoleI)) { V->AddSlot().AutoHeight()[Note(TEXT("No holes yet: add one."))]; return Section(TEXT("Hole"), V); }
	FOTGSourceHole& H = Src.Holes[HoleI];
	auto Num = [this](const FString& Label, int32 Val, int32 Lo, int32 Hi, TFunction<void(int32)> Set) {
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)[Text(Label, 12, Soft)]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(60)[SNew(SSpinBox<int32>).MinValue(Lo).MaxValue(Hi).Value(Val)
				.OnValueCommitted_Lambda([Set](int32 X, ETextCommit::Type) { Set(X); })]];
	};
	V->AddSlot().AutoHeight().Padding(0, 2, 0, 6)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Num(TEXT("Hole #"), H.Number, 1, 36, [this](int32 X) {
			if (!Src.Holes.IsValidIndex(HoleI) || X == Src.Holes[HoleI].Number) return;
			if (Src.Holes.ContainsByPredicate([X](const FOTGSourceHole& O) { return O.Number == X; })) { Status = FString::Printf(TEXT("Hole %d already exists"), X); Changed(); return; }
			Change(); Src.Holes[HoleI].Number = X; Changed();
		})]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Num(TEXT("Par"), H.Par, 3, 6, [this](int32 X) { if (Src.Holes.IsValidIndex(HoleI) && X != Src.Holes[HoleI].Par) { Change(); Src.Holes[HoleI].Par = X; Changed(); } })]
		+ SHorizontalBox::Slot().AutoWidth()[Num(TEXT("Handicap"), FMath::Max(1, H.Index), 1, 18, [this](int32 X) { if (Src.Holes.IsValidIndex(HoleI) && X != Src.Holes[HoleI].Index) { Change(); Src.Holes[HoleI].Index = X; Changed(); } })]
	];
	// a point: where it is, Place, and a cross to take it away
	auto PointRow = [this, &V](const FLinearColor& Swatch, const FString& Name, const FString& Val, const FString& Kind, const FString& Key, bool bRemovable) {
		const bool bPlacing = Mode == EMode::Place && ModeKind == Kind && ModeKey == Key;
		TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)[SNew(SBox).WidthOverride(12).HeightOverride(12)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Swatch)]]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(110)[Text(Name, 12, Ink)]]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(Val, 12, Soft)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 1)[Button(TEXT("Place"), [this, Kind, Key, bPlacing]() { SetMode(bPlacing ? EMode::None : EMode::Place, Kind, Key); }, 11, bPlacing)];
		if (bRemovable)
			R->AddSlot().AutoWidth().Padding(2, 1)[Button(TEXT("✕"), [this, Kind, Key]() {
				if (!Src.Holes.IsValidIndex(HoleI)) return;
				FOTGSourceHole& Ho = Src.Holes[HoleI];
				if (Kind == TEXT("tee") && Ho.Tees.Num() <= 1) { Status = TEXT("A hole needs at least one tee"); Changed(); return; }
				Change();
				if (Kind == TEXT("tee")) Ho.Tees.Remove(Key); else if (Kind == TEXT("aim") && Ho.Aims.IsValidIndex(FCString::Atoi(*Key))) Ho.Aims.RemoveAt(FCString::Atoi(*Key));
				Changed();
			}, 11)];
		V->AddSlot().AutoHeight().Padding(0, 1)[R];
	};
	auto Teal = [](const FString& T) {
		static const TMap<FString, FLinearColor> C = { { TEXT("Black"), FLinearColor(0.02f, 0.02f, 0.02f) }, { TEXT("Blue"), FLinearColor(0.03f, 0.12f, 0.8f) }, { TEXT("White"), FLinearColor::White },
			{ TEXT("Green"), FLinearColor(0.02f, 0.4f, 0.03f) }, { TEXT("Yellow"), FLinearColor(0.85f, 0.6f, 0.02f) }, { TEXT("Red"), FLinearColor(0.72f, 0.02f, 0.02f) },
			{ TEXT("Junior"), FLinearColor(1, 0.25f, 0) }, { TEXT("Par3"), FLinearColor(0.32f, 0.32f, 0.32f) } };
		return C.Contains(T) ? C[T] : FLinearColor::White;
	};
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 2)[Text(TEXT("Tees"), 13, Gold, "Bold")];
	for (const FString& T : FOTGCourseSource::TeesOrder())
	{
		const FVector2D* P = H.Tees.Find(T);
		FString Yd = TEXT("not set");
		if (P) { TArray<FVector2D> L = { *P }; L.Append(H.Aims); L.Add(H.Green); double D = 0; for (int32 k = 1; k < L.Num(); k++) D += FVector2D::Distance(L[k - 1], L[k]); Yd = FString::Printf(TEXT("%d yd"), FMath::RoundToInt(D * MToYd)); }
		PointRow(Teal(T), T, Yd, TEXT("tee"), T, P != nullptr);
	}
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 2)[Text(TEXT("Aim points (doglegs)"), 13, Gold, "Bold")];
	for (int32 k = 0; k < 2; k++) PointRow(FLinearColor(1, 0.7f, 0.07f), FString::Printf(TEXT("Aim %d"), k + 1), H.Aims.IsValidIndex(k) ? TEXT("set") : TEXT("not set"), TEXT("aim"), FString::FromInt(k), H.Aims.IsValidIndex(k));
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 2)[Text(TEXT("Green and pins"), 13, Gold, "Bold")];
	PointRow(FLinearColor(0.6f, 0.6f, 0.6f), TEXT("Green centre"), TEXT("set"), TEXT("green"), TEXT("0"), false);
	for (const FString& D : FOTGCourseSource::PinDays()) PointRow(FLinearColor(0.8f, 0.02f, 0.03f), D + TEXT(" pin"), H.Pins.Contains(D) ? TEXT("set") : TEXT("not set"), TEXT("pin"), D, false);
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[Button(TEXT("Spread pins across green"), [this]() { if (Src.Holes.IsValidIndex(HoleI)) { Change(); AutoPins(Src.Holes[HoleI]); Changed(); } }, 12)];
	return Section(FString::Printf(TEXT("Hole %d  ·  par %d"), H.Number, H.Par), V);
}

TSharedRef<SWidget> SOTGCourseEditor::ShapePanel()
{
	TSharedRef<SWrapBox> W = SNew(SWrapBox).UseAllottedSize(true);
	for (const FString& K : DrawKinds)
	{
		const bool bOn = Mode == EMode::Draw && ModeKind == K;
		W->AddSlot().Padding(0, 0, 4, 4)[Button(KindLabel(K), [this, K, bOn]() { SetMode(bOn ? EMode::None : EMode::Draw, K); }, 12, bOn)];
	}
	return Section(TEXT("Draw a shape"), SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[W]
		+ SVerticalBox::Slot().AutoHeight()[Note(TEXT("Click a shape on the map to pick it: drag its corners, drag a corner's middle for a new one, drag inside it to move it."))]);
}

TSharedRef<SWidget> SOTGCourseEditor::SelectionPanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	if (Src.UserTrees.IsValidIndex(SelTree))
	{
		FOTGSourceTree& T = Src.UserTrees[SelTree];
		static const TArray<FString> Sizes = { TEXT("small"), TEXT("medium"), TEXT("large") };
		TArray<FString> Kinds = FOTGCourseSource::Mixes(); Kinds.Append(FOTGCourseSource::Species());
		V->AddSlot().AutoHeight()[Row(TEXT("Size"), Button(T.Size, [this]() { Change(); Src.UserTrees[SelTree].Size = Next(Sizes, Src.UserTrees[SelTree].Size); Changed(); }, 12))];
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Species"), Button(T.Kind == TEXT("auto") || FOTGCourseSource::Mixes().Contains(T.Kind) ? FOTGCourseSource::MixLabel(T.Kind) : FOTGTreeDesign::SpeciesLabel(T.Kind),
			[this, Kinds]() { Change(); Src.UserTrees[SelTree].Kind = Next(Kinds, Src.UserTrees[SelTree].Kind); Changed(); }, 12))];
		V->AddSlot().AutoHeight()[Note(TEXT("Drag it to move it."))];
		V->AddSlot().AutoHeight().Padding(0, 4)[Button(TEXT("Delete tree"), [this]() { DeleteSelected(); }, 12)];
		return Section(TEXT("Your tree"), V);
	}
	if (Buildings.IsValidIndex(SelBuilding))
	{
		const FBuilding& B = Buildings[SelBuilding];
		const FString* T = Src.BuildingTypes.Find(B.Key);
		FString Label = TEXT("The game's guess");
		for (const auto& BT : BuildingTypes()) if (T && BT.Key == *T) Label = BT.Value;
		V->AddSlot().AutoHeight()[Row(TEXT("It's a"), Button(Label, [this]() {
			Change();
			const FString Key = Buildings[SelBuilding].Key;
			const FString* Cur = Src.BuildingTypes.Find(Key);
			int32 I = INDEX_NONE;
			for (int32 k = 0; k < BuildingTypes().Num(); k++) if (Cur && BuildingTypes()[k].Key == *Cur) I = k;
			// (the guess, then each type)
			if (I + 1 >= BuildingTypes().Num()) Src.BuildingTypes.Remove(Key); else Src.BuildingTypes.Add(Key, BuildingTypes()[I + 1].Key);
			Changed();
		}, 12))];
		V->AddSlot().AutoHeight()[Note(FString::Printf(TEXT("%s%s about %d m²%s"), *B.Name, B.Name.IsEmpty() ? TEXT("") : TEXT(" ·"), FMath::RoundToInt(B.Area), B.Kind.IsEmpty() || B.Kind == TEXT("yes") ? TEXT("") : *(TEXT(" · mapped as ") + B.Kind)))];
		return Section(TEXT("Building"), V);
	}
	if (SelMany.Num())
	{
		V->AddSlot().AutoHeight()[Note(FString::Printf(TEXT("%d found trees picked. Shift-click a tree to add or drop it."), SelMany.Num()))];
		V->AddSlot().AutoHeight().Padding(0, 4)[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(FString::Printf(TEXT("Remove %d trees"), SelMany.Num()), [this]() { RemoveMany(); }, 12)]
			+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Clear"), [this]() { ClearPick(); Changed(); }, 12)]];
		return Section(TEXT("Trees"), V);
	}
	if (Found.IsValidIndex(SelFound))
	{
		const FFound& F = Found[SelFound];
		V->AddSlot().AutoHeight()[Note(FString::Printf(TEXT("A %s the game planted (from the tree-height map, OpenStreetMap or the aerial photo), about %d m across."), *FOTGTreeDesign::SpeciesLabel(F.Kind).ToLower(), FMath::RoundToInt(F.R * 2)))];
		V->AddSlot().AutoHeight().Padding(0, 4)[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Remove tree"), [this]() { RemoveFound(false); }, 12)]
			+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Replace with my own"), [this]() { RemoveFound(true); }, 12)]];
		if (Review.IsValidIndex(ReviewAt))
			V->AddSlot().AutoHeight().Padding(0, 4)[SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Text(FString::Printf(TEXT("In play: %d of %d"), ReviewAt + 1, Review.Num()), 12, Gold)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)[Button(TEXT("Previous"), [this]() { ReviewAt = FMath::Max(0, ReviewAt - 1); ShowReview(); }, 11)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)[Button(TEXT("Keep, next"), [this]() { ReviewAt++; if (!Review.IsValidIndex(ReviewAt)) { ReviewAt = INDEX_NONE; Status = TEXT("Review done"); } ShowReview(); }, 11)]
				+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Remove, next"), [this]() { RemoveFound(false); ReviewAt++; if (!Review.IsValidIndex(ReviewAt)) { ReviewAt = INDEX_NONE; Status = TEXT("Review done"); } ShowReview(); }, 11)]];
		return Section(TEXT("Found tree"), V);
	}
	if (Src.RemovedTrees.IsValidIndex(SelRemoved))
	{
		V->AddSlot().AutoHeight()[Note(TEXT("A tree the game found was taken out here."))];
		V->AddSlot().AutoHeight().Padding(0, 4)[Button(TEXT("Put it back"), [this]() { RestoreRemoved(); }, 12)];
		return Section(TEXT("Removed tree"), V);
	}
	if (!Src.Shapes.IsValidIndex(SelShape)) return SNullWidget::NullWidget;
	FOTGSourceShape& S = Src.Shapes[SelShape];
	TArray<FString> Kinds = DrawKinds; Kinds.Add(TEXT("trees"));
	V->AddSlot().AutoHeight()[Row(TEXT("It's a"), Button(KindLabel(S.Kind), [this, Kinds]() {
		Change();
		FOTGSourceShape& Sh = Src.Shapes[SelShape];
		Sh.Kind = Next(Kinds, Sh.Kind);
		if (Sh.Kind == TEXT("oob")) { const FOTGSourceShape Keep = Sh; Src.Shapes.RemoveAll([](const FOTGSourceShape& X) { return X.Kind == TEXT("oob"); }); Src.Shapes.Add(Keep); SelShape = Src.Shapes.Num() - 1; }
		Changed();
	}, 12))];
	if (S.Kind == TEXT("trees"))
	{
		static const TArray<FString> Dens = { TEXT("sparse"), TEXT("medium"), TEXT("dense") };
		TArray<FString> Sp = FOTGCourseSource::Mixes(); Sp.Append(FOTGCourseSource::Species());
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Tree density"), Button(S.Density, [this]() { Change(); Src.Shapes[SelShape].Density = Next(Dens, Src.Shapes[SelShape].Density); Changed(); }, 12))];
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Species"), Button(FOTGCourseSource::Mixes().Contains(S.Species) ? FOTGCourseSource::MixLabel(S.Species) : FOTGTreeDesign::SpeciesLabel(S.Species),
			[this, Sp]() { Change(); Src.Shapes[SelShape].Species = Next(Sp, Src.Shapes[SelShape].Species); Changed(); }, 12))];
		auto H = [this](double* Val) {
			return SNew(SBox).WidthOverride(70)[SNew(SSpinBox<double>).MinValue(2).MaxValue(40).Delta(1).Value(FMath::IsFinite(*Val) ? *Val : 0)
				.OnValueCommitted_Lambda([this, Val](double X, ETextCommit::Type) { Change(); *Val = X; Changed(); })];
		};
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Heights (m)"), SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[H(&S.HMin)] + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)[Text(TEXT("to"), 12, Soft)] + SHorizontalBox::Slot().AutoWidth()[H(&S.HMax)]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0)[Button(TEXT("As measured"), [this]() { Change(); Src.Shapes[SelShape].HMin = Src.Shapes[SelShape].HMax = NAN; Changed(); }, 11)])];
	}
	if (S.Kind == TEXT("water") && !S.bInner)
	{
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Water"), Button(S.bFlow ? TEXT("A stream or river") : TEXT("A pond or lake"), [this]() { Change(); Src.Shapes[SelShape].bFlow = !Src.Shapes[SelShape].bFlow; Changed(); }, 12))];
		V->AddSlot().AutoHeight()[Note(S.bFlow ? TEXT("Runs downhill: the water slopes with its banks.") : TEXT("Lies flat."))];
	}
	if (S.Kind == TEXT("penalty"))
		V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("Inside it"), Button(S.bInnerOOB ? TEXT("Out of bounds") : TEXT("A penalty area"), [this]() { Change(); Src.Shapes[SelShape].bInnerOOB = !Src.Shapes[SelShape].bInnerOOB; Changed(); }, 12))];
	V->AddSlot().AutoHeight().Padding(0, 4)[Note(FString::Printf(TEXT("%d points · %s m²%s"), S.Pts.Num(), *FText::AsNumber(FMath::RoundToInt(PolyArea(S.Pts))).ToString(), S.bInner ? TEXT(" · a cut-out") : TEXT("")))];
	V->AddSlot().AutoHeight().Padding(0, 4)[Button(TEXT("Delete shape"), [this]() { DeleteSelected(); }, 12)];
	return Section(TEXT("Shape"), V);
}

TSharedRef<SWidget> SOTGCourseEditor::TreePanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	static const TArray<FString> Sizes = { TEXT("small"), TEXT("medium"), TEXT("large") };
	static const TArray<FString> Dens = { TEXT("sparse"), TEXT("medium"), TEXT("dense") };
	TArray<FString> Kinds = FOTGCourseSource::Mixes(); Kinds.Append(FOTGCourseSource::Species());
	auto KLabel = [](const FString& K) { return FOTGCourseSource::Mixes().Contains(K) ? FOTGCourseSource::MixLabel(K) : FOTGTreeDesign::SpeciesLabel(K); };
	V->AddSlot().AutoHeight()[Row(TEXT("On the map"), SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(bShowTrees ? TEXT("Shown") : TEXT("Hidden"), [this]() { bShowTrees = !bShowTrees; Changed(); }, 12, bShowTrees)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Colour by species"), [this]() { bColourSpecies = !bColourSpecies; Changed(); }, 12, bColourSpecies)])];
	if (!Found.Num()) V->AddSlot().AutoHeight()[Note(TEXT("The trees the game plants show once the course is in the game (Save and put in the game)."))];
	V->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Add trees"), [this]() { SetMode(Mode == EMode::AddTree ? EMode::None : EMode::AddTree); }, 12, Mode == EMode::AddTree)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TreeSize, [this]() { TreeSize = Next(Sizes, TreeSize); Changed(); }, 12)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(KLabel(TreeKind), [this, Kinds]() { TreeKind = Next(Kinds, TreeKind); Changed(); }, 12)]
	];
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Remove trees"), [this]() { SetMode(Mode == EMode::Cut ? EMode::None : EMode::Cut); }, 12, Mode == EMode::Cut)]
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SSlider).Value_Lambda([this]() { return float((CutR - 1) / 29.0); }).OnValueChanged_Lambda([this](float X) { CutR = FMath::RoundToDouble(1 + 29 * X); })]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.0f m"), CutR * 2)); })]
	];
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Draw tree area"), [this]() { const bool bOn = Mode == EMode::Draw && ModeKind == TEXT("trees"); SetMode(bOn ? EMode::None : EMode::Draw, TEXT("trees")); }, 12, Mode == EMode::Draw && ModeKind == TEXT("trees"))]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(AreaDensity, [this]() { AreaDensity = Next(Dens, AreaDensity); Changed(); }, 12)]
		+ SHorizontalBox::Slot().AutoWidth()[Button(KLabel(AreaKind), [this, Kinds]() { AreaKind = Next(Kinds, AreaKind); Changed(); }, 12)]
	];
	auto H = [this](double* Val) { return SNew(SBox).WidthOverride(70)[SNew(SSpinBox<double>).MinValue(2).MaxValue(40).Delta(1).Value(*Val).OnValueCommitted_Lambda([Val](double X, ETextCommit::Type) { *Val = X; })]; };
	V->AddSlot().AutoHeight().Padding(0, 2)[Row(TEXT("New areas' heights"), SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[H(&AreaHMin)] + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)[Text(TEXT("to"), 12, Soft)] + SHorizontalBox::Slot().AutoWidth()[H(&AreaHMax)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)[Text(TEXT("m"), 12, Soft)])];
	if (Found.Num()) V->AddSlot().AutoHeight().Padding(0, 4, 0, 2)[Button(TEXT("Review trees in the line of play"), [this]() { StartReview(); }, 12)];
	// the species on this course: one turned off becomes others
	TMap<FString, int32> Count;
	for (const FFound& F : Found) Count.FindOrAdd(F.Kind)++;
	for (const FString& Off : Src.TreesOff) Count.FindOrAdd(Off);
	if (Count.Num())
	{
		int32 Off = 0;
		for (const auto& KV : Count) Off += Src.TreesOff.Contains(KV.Key) ? 1 : 0;
		V->AddSlot().AutoHeight().Padding(0, 8, 0, 2)[Button(FString::Printf(TEXT("%s Tree species on this course (%d%s)"), bSpeciesOpen ? TEXT("\u25BE") : TEXT("\u25B8"), Count.Num(),
			Off ? *FString::Printf(TEXT(", %d off"), Off) : TEXT("")), [this]() { bSpeciesOpen = !bSpeciesOpen; Changed(); }, 12)];
		if (!bSpeciesOpen) return Section(TEXT("Trees"), V);
		Count.ValueSort([](int32 A, int32 B) { return A > B; });
		TSharedRef<SWrapBox> W = SNew(SWrapBox).UseAllottedSize(true);
		for (const auto& KV : Count)
		{
			const FString K = KV.Key;
			const bool bOff = Src.TreesOff.Contains(K);
			W->AddSlot().Padding(0, 0, 4, 4)[Button(FString::Printf(TEXT("%s %d"), *FOTGTreeDesign::SpeciesLabel(K), KV.Value), [this, K]() { Change(); if (!Src.TreesOff.Remove(K)) Src.TreesOff.Add(K); Changed(); }, 11, !bOff)];
		}
		V->AddSlot().AutoHeight()[W];
		V->AddSlot().AutoHeight()[Note(TEXT("Lit: grown on this course. A species turned off has its trees become others."))];
	}
	return Section(TEXT("Trees"), V);
}

TSharedRef<SWidget> SOTGCourseEditor::PlantPanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight()[Row(TEXT("How many"), SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1)[SNew(SSlider).Value_Lambda([this]() { return Src.PlantAmount / 2.f; }).OnMouseCaptureBegin_Lambda([this]() { Change(); })
			.OnValueChanged_Lambda([this](float X) { Src.PlantAmount = FMath::RoundToFloat(X * 40) / 20; })]
		+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)[SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Ink).Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Src.PlantAmount * 100))); })])];
	FString Group;
	TSharedPtr<SWrapBox> W;
	for (const FOTGCourseSource::FPlant& P : FOTGCourseSource::Plants())
	{
		if (Group != P.Group)
		{
			Group = P.Group;
			V->AddSlot().AutoHeight().Padding(0, 6, 0, 2)[Text(Group, 12, Soft, "Bold")];
			V->AddSlot().AutoHeight()[SAssignNew(W, SWrapBox).UseAllottedSize(true)];
		}
		const FString Pid = P.Id;
		const bool bOff = Src.PlantsOff.Contains(Pid), bExtra = Src.PlantsExtra.Contains(Pid);
		W->AddSlot().Padding(0, 0, 4, 4)[Button(FString(P.Label) + (bOff ? TEXT(": off") : bExtra ? TEXT(": added") : TEXT("")), [this, Pid, bOff, bExtra]() {
			// (as the region has it, then off, then added)
			Change();
			if (!bOff && !bExtra) Src.PlantsOff.Add(Pid);
			else if (bOff) { Src.PlantsOff.Remove(Pid); Src.PlantsExtra.Add(Pid); }
			else Src.PlantsExtra.Remove(Pid);
			Changed();
		}, 11, bExtra)];
	}
	V->AddSlot().AutoHeight()[Note(TEXT("Each as the course's region grows it, turned off, or added. (Drawn by the web version's game; the Unreal game doesn't draw them yet.)"))];
	return Section(TEXT("Shrubs, flowers and grasses"), V);
}

TSharedRef<SWidget> SOTGCourseEditor::GrassPanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight()[Row(TEXT("On the map"), Button(bShowGrass ? TEXT("Shown") : TEXT("Hidden"), [this]() { bShowGrass = !bShowGrass; Changed(); }, 12, bShowGrass))];
	V->AddSlot().AutoHeight().Padding(0, 4, 0, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Paint tall grass"), [this]() { SetMode(Mode == EMode::Paint ? EMode::None : EMode::Paint); }, 12, Mode == EMode::Paint)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Rub out"), [this]() { SetMode(Mode == EMode::Erase ? EMode::None : EMode::Erase); }, 12, Mode == EMode::Erase)]
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SSlider).Value_Lambda([this]() { return float((BrushR - 0.5) / 29.5); }).OnValueChanged_Lambda([this](float X) { BrushR = FMath::RoundToDouble((0.5 + 29.5 * X) * 2) / 2; })]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.0f m"), BrushR * 2)); })]
	];
	V->AddSlot().AutoHeight()[Note(FString::Printf(TEXT("%d strokes (amber: painted, grey: rubbed out), on top of the tall grass the game finds in the aerial photo. Rubbed-out grass plays as ordinary rough."), Src.TallGrass.Num()))];
	return Section(TEXT("Tall grass"), V);
}

TSharedRef<SWidget> SOTGCourseEditor::BuildingPanel()
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight()[Row(TEXT("On the map"), Button(bShowBuildings ? TEXT("Shown") : TEXT("Hidden"), [this]() { bShowBuildings = !bShowBuildings; Changed(); }, 12, bShowBuildings))];
	V->AddSlot().AutoHeight()[Note(FString::Printf(TEXT("%d buildings round the course. Click one to say what it is (it changes how the game draws it)."), Buildings.Num()))];
	return Section(TEXT("Buildings"), V);
}
