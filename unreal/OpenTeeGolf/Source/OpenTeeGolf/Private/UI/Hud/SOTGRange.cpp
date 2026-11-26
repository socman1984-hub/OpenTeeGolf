// See SOTGRange.h.

#include "UI/Hud/SOTGRange.h"

#include "UI/OTGUi.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"

using namespace otg::ui;

namespace
{
	constexpr int32 RowsShown = 8;
	FString Side(double V, const TCHAR* Unit, int32 Places = 0)
	{
		const double R = Places ? FMath::RoundToDouble(FMath::Abs(V) * 10) / 10 : FMath::RoundToDouble(FMath::Abs(V));
		if (R == 0) return FString::Printf(TEXT("0%s"), Unit);
		const TCHAR* Way = V > 0 ? TEXT("R") : TEXT("L");
		return Places ? FString::Printf(TEXT("%.1f%s %s"), R, Unit, Way) : FString::Printf(TEXT("%.0f%s %s"), R, Unit, Way);
	}
	FString Whole(double V) { return FText::AsNumber(FMath::RoundToInt(V)).ToString(); }
}

void SOTGRangePanel::Construct(const FArguments&)
{
	ChildSlot
	[
		SNew(SBox).WidthOverride(360)
		[
			Box(SAssignNew(Body, SVerticalBox), FMargin(16, 12))
		]
	];
	Show({}, FString(), FString());
}

void SOTGRangePanel::Show(const TArray<FOTGRangeShot>& Shots, const FString& Club, const FString& ClubName)
{
	Body->ClearChildren();
	// the last shot
	Body->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Text(Shots.Num() ? FString::Printf(TEXT("Last shot  ·  %s"), *Shots.Last().ClubName) : FString(TEXT("Driving range")), 15, Soft, "Bold")];
	if (!Shots.Num())
	{
		Body->AddSlot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("Hit a ball with your launch monitor, or press T for the test shot panel (Space for a quick one). Each shot's numbers show here, with your recent shots and averages for the club in hand.")))
			.Font(Font(13)).ColorAndOpacity(Ink).AutoWrapText(true)];
		return;
	}
	const FOTGRangeShot& L = Shots.Last();
	TSharedRef<SGridPanel> Grid = SNew(SGridPanel);
	const TPair<FString, FString> Cells[] = {
		{ TEXT("Carry"), Whole(L.CarryYd) + TEXT(" yd") }, { TEXT("Total"), Whole(L.TotalYd) + TEXT(" yd") }, { TEXT("Offline"), Side(L.OfflineYd, TEXT(" yd")) },
		{ TEXT("Ball speed"), FString::Printf(TEXT("%.1f mph"), L.Mph) }, { TEXT("Launch"), FString::Printf(TEXT("%.1f°"), L.Launch) }, { TEXT("Apex"), Whole(L.ApexFt) + TEXT(" ft") },
		{ TEXT("Spin"), L.EarlySpin >= 0 ? Whole(L.EarlySpin) + TEXT(" first\n") + Whole(L.Spin) + TEXT(" final") : Whole(L.Spin) + TEXT(" rpm") }, { TEXT("Spin axis"), Side(L.Axis, TEXT("°"), 1) }, { TEXT("Direction"), Side(L.Direction, TEXT("°"), 1) },
	};
	for (int32 i = 0; i < UE_ARRAY_COUNT(Cells); i++)
	{
		Grid->AddSlot(i % 3, i / 3).Padding(0, 0, 14, 8)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Text(Cells[i].Key, 11, Soft)]
			+ SVerticalBox::Slot().AutoHeight()[Text(Cells[i].Value, i < 3 ? 20 : 16, i < 3 ? Gold : Ink, "Bold")]
		];
	}
	Body->AddSlot().AutoHeight()[Grid];

	// the club in hand's shots
	TArray<const FOTGRangeShot*> Mine;
	for (int32 i = Shots.Num() - 1; i >= 0; i--) if (Shots[i].Club == Club) Mine.Add(&Shots[i]);
	Body->AddSlot().AutoHeight().Padding(0, 8, 0, 4)[Text(FString::Printf(TEXT("%s  ·  %d shot%s"), *ClubName, Mine.Num(), Mine.Num() == 1 ? TEXT("") : TEXT("s")), 15, Soft, "Bold")];
	if (!Mine.Num())
	{
		Body->AddSlot().AutoHeight()[Text(TEXT("No shots with this club yet"), 13, Soft)];
		return;
	}
	TSharedRef<SGridPanel> Table = SNew(SGridPanel).FillColumn(0, 0.6f);
	const TCHAR* Head[] = { TEXT("#"), TEXT("Carry"), TEXT("Total"), TEXT("Offline"), TEXT("Speed"), TEXT("Spin") };
	for (int32 c = 0; c < UE_ARRAY_COUNT(Head); c++) Table->AddSlot(c, 0).Padding(0, 0, 10, 3)[Text(Head[c], 11, Soft)];
	auto Row = [&](int32 R, const FString& Label, const FOTGRangeShot& S, const FLinearColor& Colour, const char* Weight) {
		const FString V[] = { Label, Whole(S.CarryYd), Whole(S.TotalYd), Side(S.OfflineYd, TEXT("")), FString::Printf(TEXT("%.0f"), S.Mph), Whole(S.Spin) };
		for (int32 c = 0; c < UE_ARRAY_COUNT(V); c++) Table->AddSlot(c, R).Padding(0, 1, 10, 1)[Text(V[c], 13, Colour, Weight)];
	};
	for (int32 i = 0; i < FMath::Min(RowsShown, Mine.Num()); i++) Row(i + 1, FString::FromInt(Mine.Num() - i), *Mine[i], i == 0 ? Ink : Soft, "Regular");
	// the averages (all of this club's shots), and how wide they spread
	FOTGRangeShot A;
	double Lo = 1e9, Hi = -1e9;
	for (const FOTGRangeShot* S : Mine)
	{
		A.CarryYd += S->CarryYd; A.TotalYd += S->TotalYd; A.OfflineYd += S->OfflineYd; A.Mph += S->Mph; A.Spin += S->Spin;
		A.Launch += S->Launch; A.ApexFt += S->ApexFt;
		Lo = FMath::Min(Lo, S->OfflineYd); Hi = FMath::Max(Hi, S->OfflineYd);
	}
	const double K = 1.0 / Mine.Num();
	A.CarryYd *= K; A.TotalYd *= K; A.OfflineYd *= K; A.Mph *= K; A.Spin *= K; A.Launch *= K; A.ApexFt *= K;
	Row(FMath::Min(RowsShown, Mine.Num()) + 1, TEXT("Avg"), A, Gold, "Bold");
	Body->AddSlot().AutoHeight()[Table];
	if (Mine.Num() > 1)
		Body->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
		[
			Text(FString::Printf(TEXT("Launch %.1f°  ·  apex %s ft  ·  spread %s yd"), A.Launch, *Whole(A.ApexFt), *Whole(Hi - Lo)), 12, Soft)
		];
}
