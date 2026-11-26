// The course builder's cards (see SOTGCourseBuilder.h): the build under way, the course picked, and
// the courses built.

#include "UI/Builder/SOTGCourseBuilder.h"

#include "UI/OTGUi.h"
#include "Course/OTGTextures.h"
#include "Course/OTGCourseSubsystem.h"
#include "App/OTGHelperSubsystem.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Notifications/SProgressBar.h"

using namespace otg::ui;

#include "UI/Builder/SOTGBuilderShared.h"

TSharedRef<SWidget> SOTGCourseBuilder::JobCard()
{
	UOTGHelperSubsystem* H = Helper();
	if (!H) return SNullWidget::NullWidget;
	const TArray<FOTGHelperJob>& Q = H->GetQueue();
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	// (one that couldn't even be added: why)
	const FOTGHelperJob& Last = H->GetJob();
	if (!Q.Num())
	{
		if (Last.QueueId.IsEmpty() && Last.State == TEXT("failed"))
			return Box(SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Warn).WrapTextAt(400).Text(FText::FromString(TEXT("It couldn't be added: ") + Last.Message)), FMargin(12, 10), Solid);
		return SNullWidget::NullWidget;
	}
	const int32 Active = H->ActiveCount();
	V->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(Active ? FString::Printf(TEXT("Being built (%d)"), Active) : FString(TEXT("Built lately")), 15, Gold, "Bold")]
		+ SHorizontalBox::Slot().AutoWidth()[Active < Q.Num() ? StaticCastSharedRef<SWidget>(Button(TEXT("Clear finished"), [this]() { if (UOTGHelperSubsystem* H2 = Helper()) H2->ClearFinished(); Rebuild(); }, 11)) : StaticCastSharedRef<SWidget>(SNullWidget::NullWidget)]
	];
	// (going and waiting ones in their order, then the finished, newest first)
	TArray<int32> Order;
	for (int32 i = 0; i < Q.Num(); i++) if (Q[i].IsRunning()) Order.Add(i);
	for (int32 i = 0; i < Q.Num(); i++) if (Q[i].IsWaiting()) Order.Add(i);
	for (int32 i = Q.Num() - 1, n = 0; i >= 0 && n < 6; i--) if (!Q[i].IsActive()) { Order.Add(i); n++; }
	for (int32 i : Order)
	{
		const FOTGHelperJob& J = Q[i];
		const FString QId = J.QueueId, CourseId = J.CourseId;
		const bool bRun = J.IsRunning(), bFailed = J.State == TEXT("failed"), bDone = J.State == TEXT("done");
		const FString Step = J.Kind == TEXT("export") ? FString(TEXT("getting it ready for the game")) : J.Kind == TEXT("update") ? FString(TEXT("updating from OpenStreetMap")) : FString(TEXT("building"));
		TSharedRef<SVerticalBox> R = SNew(SVerticalBox);
		// its name, the step and a clock
		R->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Font(Font(13, "Bold")).ColorAndOpacity(bFailed ? Warn : bDone ? Green : Ink).WrapTextAt(330)
				.Text(FText::FromString(J.Name + (bRun ? TEXT(": ") + Step : bDone && !J.bNeedsReview ? FString(TEXT(": in the game")) : bFailed ? FString(TEXT(": it didn't work")) : FString())))]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Soft).Text_Lambda([this, QId]() {
				const UOTGHelperSubsystem* H2 = Helper();
				const FOTGHelperJob* Jn = H2 ? H2->GetQueue().FindByPredicate([&](const FOTGHelperJob& X) { return X.QueueId == QId; }) : nullptr;
				if (!Jn || !Jn->StartedAt) return FText::GetEmpty();
				const int32 S = FMath::Max(0, FMath::RoundToInt32((Jn->IsActive() ? FPlatformTime::Seconds() : Jn->EndedAt) - Jn->StartedAt));
				return FText::FromString(FString::Printf(TEXT("%d:%02d"), S / 60, S % 60));
			})]
		];
		if (J.Kind == TEXT("export") && bRun)
		{
			R->AddSlot().AutoHeight().Padding(0, 4, 0, 2)
			[
				SNew(SBox).HeightOverride(6)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.15f))]
					+ SOverlay::Slot().HAlign(HAlign_Left)[SNew(SBox).WidthOverride(400.f * FMath::Clamp(J.Frac, 0.f, 1.f))[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Green)]]
				]
			];
		}
		if (!J.Message.IsEmpty() && !(bDone && J.Message == TEXT("In the game")))
			R->AddSlot().AutoHeight().Padding(0, 2, 0, 0)[SNew(STextBlock).Font(Font(11, bRun ? "Bold" : "Regular")).ColorAndOpacity(bFailed ? Warn : bRun ? Ink : Soft).WrapTextAt(400).Text(FText::FromString(J.Message))];
		// (a running one: what it did just before)
		if (bRun)
		{
			const int32 From = FMath::Max(0, J.Log.Num() - 4);
			for (int32 k = J.Log.Num() - 2; k >= From; k--)
				if (!J.Log[k].IsEmpty() && J.Log[k] != J.Message)
					R->AddSlot().AutoHeight().Padding(0, 1, 0, 0)[SNew(STextBlock).Font(Font(10)).ColorAndOpacity(FLinearColor(0.8f, 0.84f, 0.78f, 0.7f)).WrapTextAt(400).Text(FText::FromString(J.Log[k]))];
			if (J.Message.Contains(TEXT("OpenStreetMap")))
				R->AddSlot().AutoHeight().Padding(0, 2, 0, 0)[SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Soft).WrapTextAt(400)
					.Text(FText::FromString(TEXT("OpenStreetMap's public servers are busy at times: it tries the next one, then the main OpenStreetMap site.")))];
		}
		// what can be done with it
		TSharedRef<SHorizontalBox> B = SNew(SHorizontalBox);
		auto Add = [&](const TCHAR* L, TFunction<void()> F, bool bMain = false) { B->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[Button(L, MoveTemp(F), 11, bMain)]; };
		if (bDone && J.bNeedsReview && !CourseId.IsEmpty()) Add(TEXT("Look over its holes"), [this, CourseId]() { if (OnEdit) OnEdit(CourseId); }, true);
		else if (bDone && !CourseId.IsEmpty()) Add(TEXT("Play it"), [this, CourseId]() { if (OnReady) OnReady(CourseId); }, true);
		if (bFailed) Add(TEXT("Try again"), [this, QId]() { if (UOTGHelperSubsystem* H2 = Helper()) H2->Retry(QId); Rebuild(); }, true);
		if (J.IsWaiting() || bFailed) Add(TEXT("Remove"), [this, QId]() { if (UOTGHelperSubsystem* H2 = Helper()) H2->Remove(QId); Rebuild(); });
		if (bRun) Add(J.Kind == TEXT("export") ? TEXT("Stop") : TEXT("Stop after this step"), [this, QId]() { if (UOTGHelperSubsystem* H2 = Helper()) H2->Remove(QId); Rebuild(); });
		R->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[B];
		V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[R];
	}
	if (Active)
		V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(400)
			.Text(FText::FromString(TEXT("Go and play meanwhile: they carry on in the background (two built at a time, one got ready for the game at a time), you're told in the round when one is ready, and what's left carries on next time if you close the game.")))];
	return Box(V, FMargin(12, 10), Solid);
}

TSharedRef<SWidget> SOTGCourseBuilder::PickedCard()
{
	if (!Courses.IsValidIndex(Picked))
		return Box(SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Soft).WrapTextAt(400)
			.Text(FText::FromString(TEXT("Find a course: search for it, or drag and zoom the map (the mouse wheel) and click its dot. Every dot is a golf course mapped in OpenStreetMap; gold ones you've built already."))), FMargin(12, 10), Solid);
	const FOTGMapCourse& M = Courses[Picked];
	UOTGHelperSubsystem* H = Helper();
	const FOTGHelperJob* Queued = H ? H->ActiveFor(M.BuiltId, M.OsmId) : nullptr;
	const bool bBusy = Queued != nullptr;
	const bool bLine = H && H->ActiveCount() > 0;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	V->AddSlot().AutoHeight()[SNew(STextBlock).Font(Font(18, "Bold")).ColorAndOpacity(Ink).WrapTextAt(400).Text(FText::FromString(M.Name))];
	if (!M.Location.IsEmpty()) V->AddSlot().AutoHeight()[SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Soft).WrapTextAt(400).Text(FText::FromString(M.Location))];
	if (!M.BuiltId.IsEmpty())
	{
		const FBuilt* B = Built.FindByPredicate([&](const FBuilt& X) { return X.Id == M.BuiltId; });
		V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[Text(B && B->bInGame ? TEXT("Built, and in the game.") : TEXT("Built (not in the game yet)."), 13, Green)];
		TSharedRef<SHorizontalBox> Btns = SNew(SHorizontalBox);
		const FString Id = M.BuiltId;
		// (a course in the game is changed from the Play page - pick it, Edit this course - not here)
		if (B && B->bInGame) Btns->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Play it"), [this, Id]() { if (OnReady) OnReady(Id); }, 13, true)];
		else if (!bBusy)
		{
			Btns->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Put it in the game"), [this, Id]() { if (UOTGHelperSubsystem* H2 = Helper()) H2->Export(Id); Rebuild(); }, 13, true)];
			Btns->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Look over its holes"), [this, Id]() { if (OnEdit) OnEdit(Id); }, 13)];
		}
		V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Btns];
		if (B && B->bInGame) V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(400)
			.Text(FText::FromString(TEXT("To change it, pick it on the Play page and choose Edit this course.")))];
		if (Queued) V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[Text(TEXT("In the queue: ") + Queued->Message, 12, Soft)];
		return Box(V, FMargin(12, 10), Solid);
	}
	// what's mapped there: looked at when asked (OpenStreetMap, through the helper: from a few seconds
	// to a minute when its servers are busy), a moving bar and the seconds while it looks
	if (!bChecking && !bCheckFailed && !Checked.IsValid())
	{
		V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("See what's mapped there"), [this]() { Status.Empty(); Check(); }, 14, true)]
			+ SHorizontalBox::Slot().AutoWidth()[bBusy ? StaticCastSharedRef<SWidget>(SNullWidget::NullWidget) : StaticCastSharedRef<SWidget>(Button(TEXT("Build without looking"), [this]() { StartBuild(); }, 13))]
		];
		V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(400)
			.Text(FText::FromString(TEXT("Asks OpenStreetMap what it has of this course (holes, greens, fairways, bunkers, water, trees) and which set of holes to build, before you build it.")))];
	}
	if (bChecking)
	{
		V->AddSlot().AutoHeight().Padding(0, 10, 0, 4)[Text(TEXT("Looking at what's mapped there…"), 13, Ink)];
		V->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(10)[SNew(SProgressBar).FillColorAndOpacity(FLinearColor(0.3f, 0.75f, 0.4f, 1.f))]];   // (no percent: it moves to and fro)
		V->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(400)
			.Text_Lambda([this]() {
				const int32 S = FMath::FloorToInt32(FPlatformTime::Seconds() - CheckStarted);
				return FText::FromString(FString::Printf(TEXT("%d s · asking OpenStreetMap for the course's holes, greens, fairways, bunkers, water and trees%s"), S,
					S >= 20 ? TEXT(" (its servers are slow at times: it tries the next one)") : TEXT(" (usually 5 to 20 s)")));
			})];
	}
	if (bCheckFailed && !Checked.IsValid())
	{
		// (OpenStreetMap busy: again, or build without seeing what's mapped)
		V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Warn).WrapTextAt(400).Text(FText::FromString(TEXT("Couldn't see what's mapped there: ") + Status))];
		V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[Button(TEXT("Check again"), [this]() { Status.Empty(); Check(); }, 13)]
			+ SHorizontalBox::Slot().AutoWidth()[bBusy ? StaticCastSharedRef<SWidget>(SNullWidget::NullWidget) : StaticCastSharedRef<SWidget>(Button(TEXT("Build anyway"), [this]() { StartBuild(); }, 13, true))]
		];
	}
	if (Checked.IsValid())
	{
		auto N = [this](const TCHAR* K) { return int32(Checked->GetNumberField(K)); };
		V->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Ink).WrapTextAt(400).Text(FText::FromString(FString::Printf(
			TEXT("Mapped: %d holes, %d greens, %d fairways, %d tees, %d bunkers, %d water, %d trees%s"), N(TEXT("holes")), N(TEXT("greens")), N(TEXT("fairways")), N(TEXT("tees")), N(TEXT("bunkers")), N(TEXT("water")), N(TEXT("trees")),
			Checked->GetBoolField(TEXT("hasBoundary")) ? TEXT(", the boundary") : TEXT(""))))];
		const FString Q = Checked->GetStringField(TEXT("quality"));
		for (const TSharedPtr<FJsonValue>& Note : Checked->GetArrayField(TEXT("notes")))
			V->AddSlot().AutoHeight().Padding(0, 3, 0, 0)[SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Q == TEXT("good") ? Green : Q == TEXT("partial") ? Gold : Warn).WrapTextAt(400).Text(FText::FromString(Note->AsString()))];
		// (a club with more than one set of holes: which to build)
		const TArray<TSharedPtr<FJsonValue>>& Groups = Checked->GetArrayField(TEXT("groups"));
		if (Groups.Num() > 1)
		{
			V->AddSlot().AutoHeight().Padding(0, 8, 0, 2)[Text(TEXT("Which holes"), 13, Soft)];
			TSharedRef<SVerticalBox> GB = SNew(SVerticalBox);
			for (const TSharedPtr<FJsonValue>& GV : Groups)
			{
				const TSharedPtr<FJsonObject> G = GV->AsObject();
				const FString Name = G->GetStringField(TEXT("name"));
				const FString Label = FString::Printf(TEXT("%s (%d holes, par %d)"), Name.IsEmpty() ? TEXT("The holes") : *Name, int32(G->GetNumberField(TEXT("holes"))), int32(G->GetNumberField(TEXT("par"))));
				GB->AddSlot().AutoHeight().Padding(0, 2)[Button(Label, [this, Name]() { Group = Name; Rebuild(); }, 12, Group == Name)];
			}
			V->AddSlot().AutoHeight()[GB];
		}
		V->AddSlot().AutoHeight().Padding(0, 10, 0, 2)[Text(TEXT("Its name in the game"), 12, Soft)];
		V->AddSlot().AutoHeight()[SAssignNew(NameBox, SEditableTextBox).Text(FText::FromString(NameDraft)).Font(Font(14))
			.OnTextChanged_Lambda([this](const FText& T) { NameDraft = T.ToString(); })];
		V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)
		[
			bBusy ? StaticCastSharedRef<SWidget>(Text(TEXT("In the queue: ") + Queued->Message, 12, Soft))
				: StaticCastSharedRef<SWidget>(Button(bLine ? TEXT("Add it to the queue") : TEXT("Build this course"), [this]() { StartBuild(); }, 15, true))
		];
		V->AddSlot().AutoHeight().Padding(0, 6, 0, 0)[SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Soft).WrapTextAt(400)
			.Text(FText::FromString(TEXT("It downloads the map data, the ground's heights and aerial photos, builds the course, then gets it ready for the game - about 5 to 20 minutes, most of it the last step.")))];
	}
	return Box(V, FMargin(12, 10), Solid);
}

void SOTGCourseBuilder::Rebuild()
{
	if (!Lower.IsValid()) return;
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	// what the search found
	if (Found.Num())
	{
		TSharedRef<SVerticalBox> F = SNew(SVerticalBox);
		for (int32 I : Found)
		{
			if (!Courses.IsValidIndex(I)) continue;
			const FOTGMapCourse& M = Courses[I];
			F->AddSlot().AutoHeight().Padding(0, 2)
			[
				SNew(SButton).ButtonStyle(&ButtonStyle(I == Picked)).ContentPadding(FMargin(10, 4))
				.OnClicked_Lambda([this, I]() { Pick(I, true); return FReply::Handled(); })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Text(M.Name + (M.BuiltId.IsEmpty() ? FString() : FString(TEXT("  (built)"))), 13, Ink, "Bold")]
					+ SVerticalBox::Slot().AutoHeight()[Text(M.Location, 11, Soft)]
				]
			];
		}
		V->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[SNew(SBox).MaxDesiredHeight(180)[SNew(SScrollBox) + SScrollBox::Slot()[F]]];
	}
	TSharedRef<SVerticalBox> Rest = SNew(SVerticalBox);
	Rest->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[PickedCard()];
	Rest->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[JobCard()];
	V->AddSlot().FillHeight(1)[SNew(SScrollBox) + SScrollBox::Slot()[Rest]];
	Lower->SetContent(V);
}
