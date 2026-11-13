// See OTGPlayers.h (public/js/players.js and players-menu.js).

#include "Game/OTGPlayers.h"

#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

namespace
{
	FString Dir() { return FPaths::ProjectSavedDir() / TEXT("OpenTeeGolf"); }
	FLinearColor Hex(const TCHAR* H) { return FLinearColor(FColor::FromHex(H)); }
	FString Fmt(int32 D) { return D == 0 ? TEXT("E") : D > 0 ? FString::Printf(TEXT("+%d"), D) : FString::FromInt(D); }
}

// ------------------------------------------------------------------ profiles

TArray<FLinearColor> FOTGPlayers::Colours()
{
	return { Hex(TEXT("e53935")), Hex(TEXT("1e88e5")), Hex(TEXT("fdd835")), Hex(TEXT("43a047")), Hex(TEXT("8e24aa")), Hex(TEXT("fb8c00")), Hex(TEXT("00acc1")), Hex(TEXT("ec407a")) };
}

FOTGPlayerProfile FOTGPlayers::NewProfile(const FString& Name, int32 Index)
{
	FOTGPlayerProfile P;
	P.Id = FGuid::NewGuid().ToString(EGuidFormats::Short);
	P.Name = Name;
	const TArray<FLinearColor> C = Colours();
	P.Colour = C[Index % C.Num()];
	return P;
}

TArray<FOTGPlayerProfile> FOTGPlayers::Load()
{
	FOTGPlayerFile File;
	FString Text;
	if (FFileHelper::LoadFileToString(Text, *(Dir() / TEXT("players.json")))) FJsonObjectConverter::JsonObjectStringToUStruct(Text, &File);
	return File.Players;
}

void FOTGPlayers::Save(const TArray<FOTGPlayerProfile>& Profiles)
{
	FOTGPlayerFile File;
	File.Players = Profiles;
	FString Text;
	FJsonObjectConverter::UStructToJsonObjectString(File, Text);
	FFileHelper::SaveStringToFile(Text, *(Dir() / TEXT("players.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

FOTGRoundSetup FOTGPlayers::LoadSetup()
{
	FOTGRoundSetup S;
	FString Text;
	if (FFileHelper::LoadFileToString(Text, *(Dir() / TEXT("setup.json")))) FJsonObjectConverter::JsonObjectStringToUStruct(Text, &S);
	return S;
}

void FOTGPlayers::SaveSetup(const FOTGRoundSetup& Setup)
{
	FString Text;
	FJsonObjectConverter::UStructToJsonObjectString(Setup, Text);
	FFileHelper::SaveStringToFile(Text, *(Dir() / TEXT("setup.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

// ------------------------------------------------------------------ formats

const TArray<FOTGFormat>& FOTGGame::Formats()
{
	static const TArray<FOTGFormat> F = {
		{ TEXT("stroke"), TEXT("Stroke play"), TEXT("Everyone plays their own ball. Fewest strokes wins."), false, TEXT(""), 1 },
		{ TEXT("stableford"), TEXT("Stableford"), TEXT("Points on every hole: 1 for a bogey, 2 for par, 3 for a birdie, 4 for an eagle. Most points wins, and a bad hole costs nothing."), false, TEXT(""), 1 },
		{ TEXT("match"), TEXT("Match play"), TEXT("Win holes, not strokes: the lower score takes each hole. Two players, or teams A and B with their best ball."), false, TEXT(""), 2 },
		{ TEXT("skins"), TEXT("Skins"), TEXT("Every hole is worth a skin, won by an outright lowest score. Ties carry the skin over to the next hole."), false, TEXT(""), 2 },
		{ TEXT("bestball"), TEXT("Best ball (four-ball)"), TEXT("Teams A and B. Everyone plays their own ball; each hole the team counts its best score."), true, TEXT(""), 2 },
		{ TEXT("scramble"), TEXT("Scramble"), TEXT("Teams: everyone hits, the team picks the best shot and everyone plays the next one from there."), true, TEXT("scramble"), 1 },
		{ TEXT("alternate"), TEXT("Alternate shot (foursomes)"), TEXT("Teams of two share one ball and take turns hitting it, and take turns teeing off."), true, TEXT("alternate"), 2 },
	};
	return F;
}

const FOTGFormat& FOTGGame::FormatOf(const FString& Key)
{
	for (const FOTGFormat& F : Formats()) if (F.Key == Key) return F;
	return Formats()[0];
}

FString FOTGGame::Problem(const FString& Format, const TArray<FString>& Picked, const TMap<FString, FString>& Teams)
{
	const FOTGFormat& F = FormatOf(Format);
	const int32 N = Picked.Num();
	if (!N) return FString();
	if (N < F.Min) return FString::Printf(TEXT("%s needs at least %d players."), *F.Name, F.Min);
	TSet<FString> Used;
	for (const FString& Id : Picked) Used.Add(Teams.Contains(Id) ? Teams[Id] : TEXT("A"));
	if (F.Key == TEXT("match") && N > 2 && Used.Num() < 2) return TEXT("With more than two players, put them in teams A and B.");
	if (F.bTeams && F.Key != TEXT("scramble") && Used.Num() < 2) return TEXT("Put the players in two teams (A / B).");
	if (F.Key == TEXT("alternate"))
		for (const FString& T : Used)
		{
			int32 Count = 0;
			for (const FString& Id : Picked) if ((Teams.Contains(Id) ? Teams[Id] : TEXT("A")) == T) Count++;
			if (Count != 2) return TEXT("Alternate shot needs two players in each team.");
		}
	return FString();
}

// ------------------------------------------------------------------ handicaps

TArray<int32> OTGStrokesByHole(double Handicap, const TArray<FOTGHoleInfo>& Holes)
{
	const int32 N = Holes.Num();
	TArray<int32> Out;
	Out.Init(0, N);
	if (!N) return Out;
	const int32 Ch = FMath::RoundToInt(Handicap * N / 18.0);
	TArray<int32> Order;
	for (int32 i = 0; i < N; i++) Order.Add(i);
	Order.Sort([&](int32 A, int32 B) {
		const int32 IA = Holes[A].Index ? Holes[A].Index : A + 1, IB = Holes[B].Index ? Holes[B].Index : B + 1;
		return IA != IB ? IA < IB : A < B;
	});
	const int32 Abs = FMath::Abs(Ch);
	for (int32 Rank = 0; Rank < N; Rank++)
	{
		const int32 I = Order[Rank];
		Out[I] = Abs / N + (Rank < Abs % N ? 1 : 0);
		if (Ch < 0) Out[I] = -Out[I];
	}
	return Out;
}

// ------------------------------------------------------------------ the game

FOTGGame::FOTGGame(const TArray<FOTGGamePlayer>& InPlayers, const FString& InFormat, bool bInHandicaps, const TArray<FOTGHoleInfo>& InHoles)
	: F(FormatOf(InFormat)), bHandicaps(bInHandicaps), Holes(InHoles)
{
	const TArray<FLinearColor> C = FOTGPlayers::Colours();
	const bool bTeamsOn = F.bTeams || (F.Key == TEXT("match") && InPlayers.Num() > 2);
	for (int32 i = 0; i < InPlayers.Num(); i++)
	{
		FOTGGamePlayer P = InPlayers[i];
		P.Team = bTeamsOn ? (P.Team == TEXT("B") ? TEXT("B") : TEXT("A")) : FString();
		Players.Add(P);
	}
	for (const FOTGGamePlayer& P : Players) bTeamed |= !P.Team.IsEmpty();
	const FLinearColor TeamColour[2] = { FLinearColor(FColor::FromHex(TEXT("e53935"))), FLinearColor(FColor::FromHex(TEXT("1e88e5"))) };
	// sides: one per team for shared-ball formats, else one per player
	if (!F.Shared.IsEmpty())
	{
		for (int32 t = 0; t < 2; t++)
		{
			const FString Team = t ? TEXT("B") : TEXT("A");
			FOTGSide S;
			for (int32 i = 0; i < Players.Num(); i++) if (Players[i].Team == Team) S.Members.Add(i);
			if (!S.Members.Num()) continue;
			S.Name = FString::Printf(TEXT("Team %s"), *Team);
			S.Team = Team;
			S.Colour = TeamColour[t];
			Sides.Add(S);
		}
	}
	else
		for (int32 i = 0; i < Players.Num(); i++)
		{
			FOTGSide S;
			S.Name = Players[i].Name;
			S.Team = Players[i].Team;
			S.Colour = Players[i].Colour;
			S.Members.Add(i);
			Sides.Add(S);
		}
	double Low = 1e9;
	for (FOTGSide& S : Sides)
	{
		S.Scores.Init(-1, Holes.Num());
		// (a team's handicap: a tenth of everyone's together in a scramble, half in alternate shot)
		double Sum = 0;
		for (int32 M : S.Members) Sum += Players[M].Handicap;
		S.Handicap = S.Members.Num() == 1 ? Sum : Sum * (F.Shared == TEXT("scramble") ? 0.1 : 0.5);
		Low = FMath::Min(Low, S.Handicap);
	}
	// (match play gives strokes off the lowest handicap)
	for (FOTGSide& S : Sides)
	{
		if (bHandicaps) S.Received = OTGStrokesByHole(F.Key == TEXT("match") ? S.Handicap - Low : S.Handicap, Holes);
		else S.Received.Init(0, Holes.Num());
	}
	for (int32 i = 0; i < Sides.Num(); i++) Order.Add(i);
}

void FOTGGame::StartHole(int32 H, TFunctionRef<otg::V3(const FOTGSide&)> TeeAt)
{
	Hole = H;
	for (FOTGSide& S : Sides)
	{
		S.Ball = S.PrevBall = TeeAt(S);
		S.Lie = otg::Surface::Tee;
		S.Strokes = S.Shots = 0;
		S.bHoled = S.bPicked = false;
		S.Attempts.Reset();
	}
	// (alternate shot: the players take turns teeing off, hole by hole)
	TeeTurn = H;
}

int32 FOTGGame::Next(TFunctionRef<double(const otg::V3&)> DistToPin) const
{
	TArray<int32> Open;
	for (int32 i : Order) if (!Sides[i].bHoled) Open.Add(i);
	if (!Open.Num()) return -1;
	for (int32 i : Open) if (Sides[i].Strokes == 0 && !Sides[i].Attempts.Num()) return i;
	// (a scramble side part way through its round of shots keeps going)
	for (int32 i : Open) if (Sides[i].Attempts.Num()) return i;
	int32 Best = Open[0];
	for (int32 i : Open) if (DistToPin(Sides[i].Ball) > DistToPin(Sides[Best].Ball)) Best = i;
	return Best;
}

int32 FOTGGame::Hitter(const FOTGSide& S) const
{
	if (F.Shared == TEXT("scramble")) return S.Members[S.Attempts.Num() % S.Members.Num()];
	if (F.Shared == TEXT("alternate")) return S.Members[(S.Shots + TeeTurn) % S.Members.Num()];
	return S.Members[0];
}

TOptional<FOTGAttempt> FOTGGame::ScrambleAttempt(FOTGSide& S, const FOTGAttempt& A, TFunctionRef<double(const otg::V3&)> DistToPin)
{
	FOTGAttempt Mine = A;
	Mine.By = Hitter(S);
	S.Attempts.Add(Mine);
	if (A.bHoled)
	{
		S.Attempts.Reset();
		return Mine;
	}
	if (S.Attempts.Num() < S.Members.Num()) return {};
	auto LieCost = [](otg::Surface L) {
		switch (L)
		{
		case otg::Surface::Green: return 0.0;
		case otg::Surface::Fringe: return 0.3;
		case otg::Surface::Fairway: case otg::Surface::Tee: return 0.5;
		case otg::Surface::FirstCut: return 0.8;
		case otg::Surface::Rough: return 1.5;
		case otg::Surface::DeepRough: return 2.5;
		case otg::Surface::Native: case otg::Surface::Sand: return 3.0;
		case otg::Surface::Path: return 1.0;
		default: return 2.0;
		}
	};
	auto Score = [&](const FOTGAttempt& X) { return X.Strokes * 1000 + DistToPin(X.Ball) * (1 + LieCost(X.Lie) * 0.12); };
	FOTGAttempt Best = S.Attempts[0];
	for (const FOTGAttempt& X : S.Attempts) if (Score(X) < Score(Best)) Best = X;
	S.Attempts.Reset();
	return Best;
}

void FOTGGame::HoleOut(FOTGSide& S, int32 Strokes, bool bPicked)
{
	S.bHoled = true;
	S.bPicked = bPicked;
	S.Scores[Hole] = Strokes;
}

void FOTGGame::EndHole()
{
	const int32 H = Hole;
	Order.StableSort([&](int32 A, int32 B) { return Net(Sides[A], H).Get(99) < Net(Sides[B], H).Get(99); });
}

TOptional<int32> FOTGGame::Net(const FOTGSide& S, int32 H) const
{
	if (!S.Scores.IsValidIndex(H) || S.Scores[H] < 0) return {};
	return S.Scores[H] - (S.Received.IsValidIndex(H) ? S.Received[H] : 0);
}

TOptional<int32> FOTGGame::StablefordPoints(const FOTGSide& S, int32 H) const
{
	const TOptional<int32> N = Net(S, H);
	if (!N) return {};
	return FMath::Max(0, 2 + Holes[H].Par - *N);
}

FString FOTGGame::SideLabel(int32 I) const
{
	const FOTGSide& S = Sides[I];
	const FOTGGamePlayer& P = Players[Hitter(S)];
	return S.Members.Num() > 1 ? FString::Printf(TEXT("%s (%s)"), *P.Name, *S.Name) : P.Name;
}

// ---- scoring

TArray<FOTGGame::FTeamScores> FOTGGame::TeamScores() const
{
	TArray<FTeamScores> Out;
	const FLinearColor TeamColour[2] = { FLinearColor(FColor::FromHex(TEXT("e53935"))), FLinearColor(FColor::FromHex(TEXT("1e88e5"))) };
	for (int32 t = 0; t < 2; t++)
	{
		const FString Team = t ? TEXT("B") : TEXT("A");
		TArray<const FOTGSide*> Members;
		for (const FOTGSide& S : Sides) if (S.Team == Team) Members.Add(&S);
		if (!Members.Num()) continue;
		FTeamScores T{ Team, FString::Printf(TEXT("Team %s"), *Team), TeamColour[t] };
		for (int32 H = 0; H < Holes.Num(); H++)
		{
			TOptional<int32> Best;
			bool bAll = true;
			for (const FOTGSide* S : Members)
			{
				const TOptional<int32> N = Net(*S, H);
				if (!N) { bAll = false; break; }
				Best = Best ? FMath::Min(*Best, *N) : *N;
			}
			T.Scores.Add(bAll ? Best : TOptional<int32>());
		}
		Out.Add(T);
	}
	return Out;
}

bool FOTGGame::MatchSides(TArray<FTeamScores>& Out) const
{
	if (Sides.Num() == 2 && !bTeamed)
	{
		Out.Reset();
		for (const FOTGSide& S : Sides)
		{
			FTeamScores T{ S.Team, S.Name, S.Colour };
			for (int32 H = 0; H < Holes.Num(); H++) T.Scores.Add(Net(S, H));
			Out.Add(T);
		}
		return true;
	}
	Out = TeamScores();
	return Out.Num() == 2;
}

bool FOTGGame::MatchStatus(FMatchStatus& Out, TArray<FTeamScores>& M) const
{
	if (!MatchSides(M)) return false;
	int32 Lead = 0, Played = 0;
	bool bDecided = false;
	int32 DecLead = 0, DecLeft = 0;
	for (int32 H = 0; H < Holes.Num(); H++)
	{
		const TOptional<int32> A = M[0].Scores[H], B = M[1].Scores[H];
		if (!A || !B) continue;
		Played++;
		Lead += *A < *B ? 1 : *A > *B ? -1 : 0;
		const int32 Left = Holes.Num() - Played;
		if (!bDecided && FMath::Abs(Lead) > Left) { bDecided = true; DecLead = Lead; DecLeft = Left; }
	}
	const int32 Left = Holes.Num() - Played;
	const FTeamScores& Who = Lead > 0 ? M[0] : M[1];
	if (bDecided) Out.Text = FString::Printf(TEXT("%s won %d%s"), *(DecLead > 0 ? M[0] : M[1]).Name, FMath::Abs(DecLead), DecLeft ? *FString::Printf(TEXT("&%d"), DecLeft) : TEXT(" up"));
	else if (!Lead) Out.Text = Left ? TEXT("All square") : TEXT("Halved");
	else Out.Text = FString::Printf(TEXT("%s %d up%s"), *Who.Name, FMath::Abs(Lead), Left && FMath::Abs(Lead) == Left ? TEXT(" (dormie)") : TEXT(""));
	Out.Lead = Lead; Out.Played = Played; Out.Left = Left; Out.bDecided = bDecided || !Left;
	return true;
}

FOTGGame::FSkins FOTGGame::Skins() const
{
	FSkins K;
	K.Won.Init(0, Sides.Num());
	int32 Carry = 0;
	for (int32 H = 0; H < Holes.Num(); H++)
	{
		TArray<int32> V;
		bool bAll = true;
		for (const FOTGSide& S : Sides) { const TOptional<int32> N = Net(S, H); if (!N) { bAll = false; break; } V.Add(*N); }
		if (!bAll) { K.ByHole.Emplace(-2, 0); continue; }
		Carry++;
		int32 Low = V[0];
		for (int32 X : V) Low = FMath::Min(Low, X);
		TArray<int32> At;
		for (int32 i = 0; i < V.Num(); i++) if (V[i] == Low) At.Add(i);
		if (At.Num() == 1) { K.Won[At[0]] += Carry; K.ByHole.Emplace(At[0], Carry); Carry = 0; }
		else K.ByHole.Emplace(-1, Carry);
	}
	K.Carry = Carry;
	return K;
}

TArray<FOTGStanding> FOTGGame::Standings(FString& Summary) const
{
	auto Thru = [](const TArray<TOptional<int32>>& S) { int32 N = 0; for (const TOptional<int32>& X : S) if (X) N++; return N; };
	auto ToPar = [&](const TArray<TOptional<int32>>& S) { int32 D = 0; for (int32 H = 0; H < S.Num(); H++) if (S[H]) D += *S[H] - Holes[H].Par; return D; };
	auto Nets = [&](const FOTGSide& S) { TArray<TOptional<int32>> N; for (int32 H = 0; H < Holes.Num(); H++) N.Add(Net(S, H)); return N; };
	TArray<FOTGStanding> Rows;
	Summary.Reset();
	int32 Carry = 0;
	if (F.Key == TEXT("stableford"))
	{
		for (const FOTGSide& S : Sides)
		{
			int32 Pts = 0;
			for (int32 H = 0; H < Holes.Num(); H++) Pts += StablefordPoints(S, H).Get(0);
			Rows.Add({ S.Name, FString::Printf(TEXT("%d pts"), Pts), FString::Printf(TEXT("thru %d"), Thru(Nets(S))), S.Colour, double(-Pts) });
		}
	}
	else if (F.Key == TEXT("skins"))
	{
		const FSkins K = Skins();
		for (int32 i = 0; i < Sides.Num(); i++)
			Rows.Add({ Sides[i].Name, FString::Printf(TEXT("%d skin%s"), K.Won[i], K.Won[i] == 1 ? TEXT("") : TEXT("s")), FString::Printf(TEXT("%s thru %d"), *Fmt(ToPar(Nets(Sides[i]))), Thru(Nets(Sides[i]))), Sides[i].Colour, double(-K.Won[i]) });
		Carry = K.Carry;
	}
	else if (F.Key == TEXT("match"))
	{
		FMatchStatus M;
		TArray<FTeamScores> MS;
		if (MatchStatus(M, MS))
		{
			for (int32 i = 0; i < 2; i++)
				Rows.Add({ MS[i].Name, M.Lead == 0 ? FString(TEXT("AS")) : ((i == 0) == (M.Lead > 0) ? FString::Printf(TEXT("%d UP"), FMath::Abs(M.Lead)) : FString()), FString(), MS[i].Colour, double(i == 0 ? -M.Lead : M.Lead) });
			Summary = M.Text;
		}
	}
	else if (F.Key == TEXT("bestball"))
	{
		for (const FTeamScores& T : TeamScores())
		{
			const int32 D = ToPar(T.Scores);
			Rows.Add({ T.Name, Fmt(D), FString::Printf(TEXT("thru %d"), Thru(T.Scores)), T.Colour, double(D) });
		}
	}
	if (!Rows.Num())
		for (const FOTGSide& S : Sides)
		{
			const int32 D = ToPar(Nets(S));
			int32 Sum = 0;
			for (int32 X : S.Scores) if (X > 0) Sum += X;
			Rows.Add({ S.Name, Fmt(D), FString::Printf(TEXT("%s thru %d%s"), Sum ? *FString::FromInt(Sum) : TEXT(""), Thru(Nets(S)), bHandicaps ? TEXT(" net") : TEXT("")).TrimStart(), S.Colour, double(D) });
		}
	Rows.StableSort([](const FOTGStanding& A, const FOTGStanding& B) { return A.Sort < B.Sort; });
	if (Summary.IsEmpty() && Carry) Summary = FString::Printf(TEXT("%d skin%s carried over"), Carry, Carry == 1 ? TEXT("") : TEXT("s"));
	return Rows;
}

FString FOTGGame::HoleNews(int32 H) const
{
	if (F.Key == TEXT("match"))
	{
		FMatchStatus M;
		TArray<FTeamScores> MS;
		if (!MatchStatus(M, MS) || !MS[0].Scores[H] || !MS[1].Scores[H]) return FString();
		const int32 A = *MS[0].Scores[H], B = *MS[1].Scores[H];
		return FString::Printf(TEXT("%s  ·  %s"), A == B ? TEXT("Hole halved") : *FString::Printf(TEXT("%s wins the hole"), *(A < B ? MS[0] : MS[1]).Name), *M.Text);
	}
	if (F.Key == TEXT("skins"))
	{
		const FSkins K = Skins();
		if (!K.ByHole.IsValidIndex(H) || K.ByHole[H].Key == -2) return FString();
		const TPair<int32, int32>& B = K.ByHole[H];
		return B.Key >= 0 ? FString::Printf(TEXT("%s takes %d skin%s"), *Sides[B.Key].Name, B.Value, B.Value == 1 ? TEXT("") : TEXT("s"))
			: FString::Printf(TEXT("Tied: %d skin%s carry over"), B.Value, B.Value == 1 ? TEXT("") : TEXT("s"));
	}
	TArray<FString> Parts;
	if (F.Key == TEXT("stableford")) for (const FOTGSide& S : Sides) Parts.Add(FString::Printf(TEXT("%s %d pts"), *S.Name, StablefordPoints(S, H).Get(0)));
	else if (F.Key == TEXT("bestball")) for (const FTeamScores& T : TeamScores()) Parts.Add(FString::Printf(TEXT("%s %s"), *T.Name, T.Scores[H] ? *FString::FromInt(*T.Scores[H]) : TEXT("-")));
	else for (const FOTGSide& S : Sides) Parts.Add(FString::Printf(TEXT("%s %s"), *S.Name, S.Scores[H] >= 0 ? *FString::FromInt(S.Scores[H]) : TEXT("-")));
	return FString::Join(Parts, TEXT("  ·  "));
}
