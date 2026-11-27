// See SOTGCourseEditor.h: opening a course, its pictures and extras, and saving.

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

UOTGHelperSubsystem* SOTGCourseEditor::Helper() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UOTGHelperSubsystem>() : nullptr; }

void SOTGCourseEditor::Construct(const FArguments&, const FString& CourseId)
{
	Id = CourseId;
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[SAssignNew(Top, SBox)]
			+ SVerticalBox::Slot().FillHeight(1)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 12, 0)[SAssignNew(Canvas, SOTGEditCanvas, this)]
				+ SHorizontalBox::Slot().AutoWidth()[SAssignNew(Side, SBox).WidthOverride(440)]
			]
		]
		// (a popup over it all: what's changed, before a save or on closing)
		+ SOverlay::Slot()[SAssignNew(DialogHost, SBox).Visibility(EVisibility::Collapsed)]
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
	Saved = Src;
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
		// (and what the game's tree designer did to them: taken out ones crossed, added ones there too)
		GameDir = Dir;
		Design = FOTGTreeDesign::Load(Dir);
		Biomes = FOTGCourseBiomes::Load(Dir);
		if (UOTGCourseData* D = C->LoadCourse(Id))
		{
			for (const FOTGTreeShape& T : D->Trees)
			{
				FFound F;
				F.P = FVector2D(T.X, T.Z);
				F.R = T.bCone ? T.C : T.B;
				F.Kind = T.Kind;
				const FOTGTreeEdit* E = Design.TreeEditAt(T.X, T.Z);
				if (E && !E->Kind.IsEmpty()) F.Kind = E->Kind;
				F.Design = E && E->bRemoved ? 1 : Design.Copies(T.Kind, T.X, T.Z) == 0 ? 2 : 0;
				Found.Add(F);
			}
			for (const FOTGTreeEdit& E : Design.Trees)
			{
				if (!E.bAdded || E.bRemoved) continue;
				const FOTGTreeShape S = UOTGCourseData::AddedShape(E);
				FFound F;
				F.P = FVector2D(E.X, E.Z);
				F.R = S.bCone ? S.C : S.B;
				F.Kind = E.Kind;
				F.Design = 3;
				Found.Add(F);
			}
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
			Self->HeatherPx.Init(0, W * Hh);
			for (int32 k = 0; k + 1 < Runs->Num(); k += 2)
			{
				const int32 A = int32((*Runs)[k]->AsNumber()), L = int32((*Runs)[k + 1]->AsNumber());
				for (int32 q = A; q < A + L && q < Px.Num(); q++) if (q >= 0) { Px[q] = FColor(240, 160, 40, 95); Self->HeatherPx[q] = 1; }   // (amber)
			}
			Self->HeatherPxW = W; Self->HeatherPxH = Hh; Self->HeatherRes = Res;
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

// ------------------------------------------------------------------ saving

void SOTGCourseEditor::Save(int32 Then)
{
	UOTGHelperSubsystem* H = Helper();
	if (!H || !bLoaded) return;
	if (!Src.Holes.Num()) { Status = TEXT("A course needs at least one hole"); Changed(); return; }
	Status = TEXT("Saving…");
	Changed();
	TWeakPtr<SOTGCourseEditor> Weak = SharedThis(this);
	TSharedRef<FOTGCourseSource> Sent = MakeShared<FOTGCourseSource>(Src);
	H->SaveSource(Id, Src.SaveBody(), [Weak, Then, Sent](int32 Code, TSharedPtr<FJsonValue> J, const FString& Body) {
		TSharedPtr<SOTGCourseEditor> Self = Weak.Pin();
		if (!Self.IsValid()) return;
		if (Code != 200) { Self->Status = FString::Printf(TEXT("Save failed (%d): %s"), Code, *Body.Left(200)); Self->Changed(); return; }
		Self->Saved = *Sent;
		Self->bDirty = Self->ChangeList().Num() > 0;   // (changed again while it saved)
		Self->Status = FString::Printf(TEXT("Saved %s"), *FDateTime::Now().ToString(TEXT("%H:%M:%S")));
		if (Then == 3) { if (Self->OnClose) Self->OnClose(Self->bExported); return; }
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
		else if (double Z = 0; TestStep == 1 && FParse::Value(FCommandLine::Get(), TEXT("editortestzoom="), Z) && Z > 0 && FMath::Abs(Canvas->Scale - Z) > 1e-3) { Canvas->Scale = Z; Changed(); TestAt = FPlatformTime::Seconds() + 1; }
		// (-editortestat=X,Z: the map centred there, course metres)
		else if (FString At; TestStep == 1 && FParse::Value(FCommandLine::Get(), TEXT("editortestat="), At, false) && At.Contains(TEXT(","))
			&& FVector2D(Canvas->Cx, Canvas->Cz) != FVector2D(FCString::Atod(*At.Left(At.Find(TEXT(",")))), FCString::Atod(*At.Mid(At.Find(TEXT(",")) + 1))))
		{
			Canvas->Cx = FCString::Atod(*At.Left(At.Find(TEXT(",")))); Canvas->Cz = FCString::Atod(*At.Mid(At.Find(TEXT(",")) + 1)); Changed(); TestAt = FPlatformTime::Seconds() + 1;
		}
		else if (FString Fo; TestStep == 1 && !bForestTested && FParse::Value(FCommandLine::Get(), TEXT("editortestforest="), Fo, false))
		{
			bForestTested = true;
			TArray<FString> A;
			Fo.Replace(TEXT("+"), TEXT(" ")).ParseIntoArray(A, TEXT(","));
			const FVector2D Mark = A.Num() >= 2 ? FVector2D(FCString::Atod(*A[0]), FCString::Atod(*A[1])) : FVector2D::ZeroVector;
			MarkForest(Mark);
			if (Design.Forests.IsValidIndex(SelForest))
			{
				FOTGForest& F = Design.Forests[SelForest];
				const TArray<FOTGForestPreset> Lib = OTGLibrary::Forests();
				if (A.Num() >= 3)
					if (const FOTGForestPreset* P = Lib.FindByPredicate([&](const FOTGForestPreset& X) { return X.Name == A[2]; })) { F.Preset = *P; F.bFill = true; }
				F.Amount = 1.5f;
				SaveForests(TEXT("editortest: forest"));
			}
			FString Bi;
			if (FParse::Value(FCommandLine::Get(), TEXT("editortestbiome="), Bi, false))
				if (const FOTGBiome* B = OTGLibrary::FindIn(LibBiomes = OTGLibrary::Biomes(), Bi.Replace(TEXT("+"), TEXT(" "))))
				{
					Biomes.Use(*B);
					Biomes.Areas.Add({ B->Name, { Mark + FVector2D(-70, -70), Mark + FVector2D(70, -70), Mark + FVector2D(70, 70), Mark + FVector2D(-70, 70) } });
					SaveBiomes(TEXT("editortest: biome area"));
				}
			UE_LOG(LogTemp, Display, TEXT("editortest: %d forests, %d biome areas; %s"), Design.Forests.Num(), Biomes.Areas.Num(), *Status);
			Canvas->Cx = Mark.X; Canvas->Cz = Mark.Y; Canvas->Scale = 1.6; Tab = 2;
			Changed();
			TestAt = FPlatformTime::Seconds() + 2;
		}
		// (-editortestjoin=X1,Z1,X2,Z2: the forest at the first joined by the one at the second;
		// -editortestpatch=X,Z[,biome]: the tall grass patch there marked; -editortestdialog: a change, then Close)
		else if (FString Jo; TestStep == 1 && !bExtraTested && (FParse::Value(FCommandLine::Get(), TEXT("editortestjoin="), Jo, false) || FParse::Value(FCommandLine::Get(), TEXT("editortestpatch="), Jo, false)))
		{
			bExtraTested = true;
			TArray<FString> A;
			if (FParse::Value(FCommandLine::Get(), TEXT("editortestjoin="), Jo, false))
			{
				Jo.ParseIntoArray(A, TEXT(","));
				if (A.Num() >= 4)
				{
					const FVector2D P1(FCString::Atod(*A[0]), FCString::Atod(*A[1])), P2(FCString::Atod(*A[2]), FCString::Atod(*A[3]));
					const int32 N0 = Design.Forests.Num();
					for (int32 f = 0; f < Design.Forests.Num(); f++) if (Design.Forests[f].Contains(P1.X, P1.Y)) SelForest = f;
					SetMode(EMode::Join);
					JoinForest(P2);
					UE_LOG(LogTemp, Display, TEXT("editortest: join: %d -> %d forests; %s"), N0, Design.Forests.Num(), *Status);
					if (Design.Forests.IsValidIndex(SelForest)) Canvas->Fit(Design.Forests[SelForest].Pts, 80);
					Tab = 2;
				}
			}
			if (FParse::Value(FCommandLine::Get(), TEXT("editortestpatch="), Jo, false))
			{
				A.Reset();
				Jo.Replace(TEXT("+"), TEXT(" ")).ParseIntoArray(A, TEXT(","));
				if (A.Num() >= 2)
				{
					if (!LibBiomes.Num()) LibBiomes = OTGLibrary::Biomes();
					if (A.Num() >= 3) AreaBiome = A[2]; else if (LibBiomes.Num()) AreaBiome = LibBiomes[0].Name;
					const FVector2D P(FCString::Atod(*A[0]), FCString::Atod(*A[1]));
					MarkPatch(P);
					UE_LOG(LogTemp, Display, TEXT("editortest: patch: %d areas, %d points; %s"), Biomes.Areas.Num(), Biomes.Areas.Num() ? Biomes.Areas.Last().Pts.Num() : 0, *Status);
					Canvas->Cx = P.X; Canvas->Cz = P.Y; Tab = 3;
				}
			}
			Changed();
			TestAt = FPlatformTime::Seconds() + 2;
		}
		else if (TestStep == 1 && FParse::Param(FCommandLine::Get(), TEXT("editortestdialog")) && !bDialogTested)
		{
			bDialogTested = true;
			Change(); Src.Stimp += 1;
			Change(); FOTGGrassStroke St; St.Pts = { FVector2D(0, 0), FVector2D(10, 0) }; St.R = 4; Src.TallGrass.Add(St);
			AskClose();
			UE_LOG(LogTemp, Display, TEXT("editortest: close asks about: %s"), *FString::Join(ChangeList(), TEXT(" | ")));
			TestAt = FPlatformTime::Seconds() + 1;
		}
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
