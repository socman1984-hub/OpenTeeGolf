// Players and games: the saved player profiles, what the menu sets up for a round (course, tees,
// pins, players, the game), and the game between the players - public/js/players.js's Round in C++:
// whose turn it is, every side's scores, and who's winning, in each format (stroke play,
// Stableford, match play, skins, best ball, scramble, alternate shot), with handicaps.
//
// A "side" is whatever has its own ball: a player, or in scramble and alternate shot a team.
//
// Saved in Saved/OpenTeeGolf/players.json (the profiles) and setup.json (the menu's choices).

#pragma once

#include "CoreMinimal.h"
#include "Physics/OTGBallPhysics.h"
#include "OTGPlayers.generated.h"

USTRUCT()
struct FOTGPlayedRound
{
	GENERATED_BODY()
	UPROPERTY() FDateTime At;
	UPROPERTY() FString Course;
	UPROPERTY() int32 Holes = 0;
	UPROPERTY() int32 Strokes = 0;
	UPROPERTY() int32 Par = 0;
	UPROPERTY() FString Format;
};

USTRUCT()
struct FOTGPlayerProfile
{
	GENERATED_BODY()
	UPROPERTY() FString Id;
	UPROPERTY() FString Name;
	UPROPERTY() FLinearColor Colour = FLinearColor::Red;
	UPROPERTY() double Handicap = 0;
	UPROPERTY() bool bLeftHanded = false;
	UPROPERTY() FString Tee;  // (empty: the round's)
	UPROPERTY() TArray<FOTGPlayedRound> Rounds;
};

USTRUCT()
struct FOTGPlayerFile
{
	GENERATED_BODY()
	UPROPERTY() TArray<FOTGPlayerProfile> Players;
};

USTRUCT()
struct FOTGRoundSetup
{
	GENERATED_BODY()
	UPROPERTY() FString CourseId;
	UPROPERTY() FString Tee = TEXT("White");
	UPROPERTY() FString Pin = TEXT("Saturday");
	UPROPERTY() int32 StartHole = 1;
	UPROPERTY() int32 Holes = 18;      // 18, or 9 (the front or back nine from StartHole)
	UPROPERTY() FString Format = TEXT("stroke");
	UPROPERTY() bool bHandicaps = false;
	UPROPERTY() TArray<FString> Picked;            // profile ids, in playing order (none: a round on your own)
	UPROPERTY() TMap<FString, FString> Teams;      // profile id -> A / B
};

struct OPENTEEGOLF_API FOTGPlayers
{
	static TArray<FLinearColor> Colours();
	static TArray<FOTGPlayerProfile> Load();
	static void Save(const TArray<FOTGPlayerProfile>& Profiles);
	static FOTGRoundSetup LoadSetup();
	static void SaveSetup(const FOTGRoundSetup& Setup);
	static FOTGPlayerProfile NewProfile(const FString& Name, int32 Index);
};

// ------------------------------------------------------------------ the game

struct FOTGFormat
{
	FString Key, Name, About;
	bool bTeams = false;
	FString Shared;       // scramble / alternate: the team plays one ball
	int32 Min = 1;
};

struct FOTGGamePlayer
{
	FString Id, Name, Tee, Team;
	FLinearColor Colour;
	double Handicap = 0;
	bool bLeftHanded = false;
};

struct FOTGHoleInfo { int32 Par = 4, Index = 0; };

struct FOTGAttempt
{
	otg::V3 Ball;
	otg::Surface Lie = otg::Surface::Rough;
	int32 Strokes = 0;
	bool bHoled = false;
	int32 By = 0;          // player index
};

struct FOTGSide
{
	FString Name, Team;
	FLinearColor Colour;
	TArray<int32> Members;     // player indices
	TArray<int32> Scores;      // per hole (-1: not played)
	TArray<int32> Received;    // handicap strokes per hole
	double Handicap = 0;
	// on the hole being played
	otg::V3 Ball, PrevBall;
	otg::Surface Lie = otg::Surface::Tee;
	int32 Strokes = 0, Shots = 0;
	bool bHoled = false, bPicked = false;
	TArray<FOTGAttempt> Attempts;
};

struct FOTGStanding
{
	FString Name, Value, Sub;
	FLinearColor Colour;
	double Sort = 0;
};

class OPENTEEGOLF_API FOTGGame
{
public:
	static const TArray<FOTGFormat>& Formats();
	static const FOTGFormat& FormatOf(const FString& Key);
	// (null: fine; else what the players need for the game)
	static FString Problem(const FString& Format, const TArray<FString>& Picked, const TMap<FString, FString>& Teams);

	FOTGGame(const TArray<FOTGGamePlayer>& InPlayers, const FString& InFormat, bool bInHandicaps, const TArray<FOTGHoleInfo>& InHoles);

	bool Single() const { return Sides.Num() == 1 && Sides[0].Members.Num() == 1; }
	void StartHole(int32 H, TFunctionRef<otg::V3(const FOTGSide&)> TeeAt);
	// Who plays next: anyone still to tee off (in the order of honour), then whoever's furthest from
	// the hole; -1 when everyone has holed out
	int32 Next(TFunctionRef<double(const otg::V3&)> DistToPin) const;
	int32 Hitter(const FOTGSide& S) const;       // which of the side's players hits now
	// A scramble side's shot from one of its players: the chosen one once everyone's hit
	TOptional<FOTGAttempt> ScrambleAttempt(FOTGSide& S, const FOTGAttempt& A, TFunctionRef<double(const otg::V3&)> DistToPin);
	void HoleOut(FOTGSide& S, int32 Strokes, bool bPicked);
	void EndHole();                              // the honour on the next tee to the best score here
	TOptional<int32> Net(const FOTGSide& S, int32 H) const;
	TOptional<int32> StablefordPoints(const FOTGSide& S, int32 H) const;
	TArray<FOTGStanding> Standings(FString& Summary) const;
	FString HoleNews(int32 H) const;
	FString SideLabel(int32 SideIndex) const;    // "Sam" or "Sam (Team A)"

	TArray<FOTGGamePlayer> Players;
	TArray<FOTGSide> Sides;
	FOTGFormat F;
	bool bHandicaps = false, bTeamed = false;
	TArray<FOTGHoleInfo> Holes;
	TArray<int32> Order;      // honour on the tee
	int32 Hole = 0, TeeTurn = 0;

private:
	struct FTeamScores { FString Team, Name; FLinearColor Colour; TArray<TOptional<int32>> Scores; };
	TArray<FTeamScores> TeamScores() const;
	bool MatchSides(TArray<FTeamScores>& Out) const;
	struct FMatchStatus { int32 Lead = 0, Played = 0, Left = 0; FString Text; bool bDecided = false; };
	bool MatchStatus(FMatchStatus& Out, TArray<FTeamScores>& Sides2) const;
	struct FSkins { TArray<int32> Won; TArray<TPair<int32, int32>> ByHole; int32 Carry = 0; }; // ByHole: (side or -1 tie or -2 unplayed, skins / carry)
	FSkins Skins() const;
};

// Strokes a player gets on each hole: the handicap shared over the holes, hardest first (stroke
// index), scaled for a 9-hole course
OPENTEEGOLF_API TArray<int32> OTGStrokesByHole(double Handicap, const TArray<FOTGHoleInfo>& Holes);
