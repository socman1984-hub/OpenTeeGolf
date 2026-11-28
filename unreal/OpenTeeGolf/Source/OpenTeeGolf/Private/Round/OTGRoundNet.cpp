// An online game's round (FOTGRoundNet, OTGRoundParts.h; the connection: UOTGNetSubsystem). Every
// shot goes to the others as what was hit (the launch monitor's numbers with the lie's effect), from
// where, the aim, the wind and air it was flown in and its luck's seed - the numbers as their exact
// bits, so each game flies it the same - then where it finished, and the hole's score.

#include "Round/OTGRound.h"
#include "App/OTGNetSubsystem.h"
#include "Tracer/OTGShotTracerComponent.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGRoundNet, Log, All);

#include "Round/OTGRoundShared.h"

namespace
{
	// (a double as its 64 bits in hex: JSON's numbers don't keep every bit)
	FString NetBits(double V)
	{
		uint64 U = 0;
		FMemory::Memcpy(&U, &V, sizeof(U));
		return FString::Printf(TEXT("%016llx"), U);
	}
	double NetUnbits(const FString& S)
	{
		const uint64 U = FCString::Strtoui64(*S, nullptr, 16);
		double V = 0;
		FMemory::Memcpy(&V, &U, sizeof(V));
		return V;
	}
	TArray<TSharedPtr<FJsonValue>> NetArr(std::initializer_list<double> Vs)
	{
		TArray<TSharedPtr<FJsonValue>> A;
		for (double V : Vs) A.Add(MakeShared<FJsonValueString>(NetBits(V)));
		return A;
	}
	TArray<double> NetDoubles(const TSharedPtr<FJsonObject>& M, const TCHAR* Field, int32 Want)
	{
		TArray<double> Out;
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (M->TryGetArrayField(Field, A)) for (const TSharedPtr<FJsonValue>& V : *A) Out.Add(NetUnbits(V->AsString()));
		Out.SetNumZeroed(FMath::Max(Out.Num(), Want));
		return Out;
	}
	otg::V3 NetV3(const TSharedPtr<FJsonObject>& M, const TCHAR* Field)
	{
		const TArray<double> D = NetDoubles(M, Field, 3);
		return otg::V3{ D[0], D[1], D[2] };
	}
	int32 NetInt(const TSharedPtr<FJsonObject>& M, const TCHAR* Field) { double V = 0; M->TryGetNumberField(Field, V); return int32(V); }

	std::function<double()> NetSeeded(int32 Seed)
	{
		TSharedRef<FRandomStream> R = MakeShared<FRandomStream>(Seed);
		return [R]() { return double(R->FRand()); };
	}

	// what was hit, as the round flies it
	otg::Shot NetPlay(const TSharedPtr<FJsonObject>& M)
	{
		const TArray<double> P = NetDoubles(M, TEXT("play"), 5);
		otg::Shot S;
		S.speedMph = P[0]; S.vla = P[1]; S.hla = P[2]; S.backSpin = P[3]; S.sideSpin = P[4];
		return S;
	}
	// the wind and air it was flown in (the course's trees, collider and pin: each game's own)
	otg::ShotOptions NetOptions(const TSharedPtr<FJsonObject>& M)
	{
		const TArray<double> D = NetDoubles(M, TEXT("opts"), 10);
		otg::ShotOptions O;
		O.heading = D[0]; O.altitudeM = D[1]; O.tempC = D[2]; O.humidity = D[3]; O.pressureHpa = D[4];
		O.windMps = D[5]; O.windFromDeg = D[6]; O.wet = D[7]; O.stimp = D[8]; O.rainMm = D[9];
		return O;
	}
}

// ------------------------------------------------------------------ setting up

bool FOTGRoundNet::Begin(FOTGRoundSetup& Setup, TArray<FOTGGamePlayer>& Players)
{
	UOTGNetSubsystem* N = UOTGNetSubsystem::Get(Round);
	if (!N || !N->IsRoundOn()) return false;
	bOn = true;
	bTogether = N->Together();
	bHost = N->IsHost();
	Players.Reset();
	PlayerOfSide.Reset();
	TArray<FOTGNetPlayer> L = N->Players();
	L.Sort([](const FOTGNetPlayer& A, const FOTGNetPlayer& B) { return A.Id < B.Id; });
	for (const FOTGNetPlayer& P : L)
	{
		if (!P.bConnected) continue;
		FOTGGamePlayer G;
		// (this computer's player: their profile, so the round's kept with it; the others: theirs, not here)
		const bool bHere = P.Id == N->LocalId();
		G.Id = bHere && !P.ProfileId.IsEmpty() ? P.ProfileId : FString::Printf(TEXT("net:%d"), P.Id);
		G.Name = P.Name;
		G.Colour = P.Colour;
		G.Handicap = P.Handicap;
		G.bLeftHanded = P.bLeftHanded;
		G.Tee = P.Tee;
		G.Team = TEXT("A");
		if (bHere) LocalSide = Players.Num();
		Players.Add(G);
		PlayerOfSide.Add(P.Id);
	}
	// (games of one ball a player: stroke play, Stableford, skins, match play between two)
	const FOTGFormat& F = FOTGGame::FormatOf(Setup.Format);
	TArray<FString> Ids;
	for (const FOTGGamePlayer& G : Players) Ids.Add(G.Id);
	if (F.bTeams || !F.Shared.IsEmpty() || !FOTGGame::Problem(Setup.Format, Ids, TMap<FString, FString>()).IsEmpty()) Setup.Format = TEXT("stroke");
	UE_LOG(LogOTGRoundNet, Display, TEXT("Online round: %d players, %s, %s; this computer's player is side %d"), Players.Num(), bTogether ? TEXT("everyone at once") : TEXT("taking turns"), *Setup.Format, LocalSide);
	return true;
}

void FOTGRoundNet::AfterSetup()
{
	if (!bOn) return;
	const int32 N = Round->Turns.Game->Sides.Num();
	Flights.SetNum(N);
	// (everyone at once: a tracer for each of the others, in their colour)
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	for (int32 s = 0; s < N; s++)
	{
		UOTGShotTracerComponent* T = nullptr;
		if (bTogether && s != LocalSide)
		{
			T = NewObject<UOTGShotTracerComponent>(Round);
			T->SetupAttachment(Round->GetRootComponent());
			T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			T->SetCastShadow(false);
			T->bUseAsyncCooking = true;
			T->RegisterComponent();
			if (Base)
			{
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, Round);
				M->SetVectorParameterValue(TEXT("Color"), Round->Turns.Game->Sides[s].Colour);
				T->SetMaterial(0, M);
				T->RibbonMaterial = M;
			}
		}
		Tracers.Add(T);
	}
	WxAt = 0.5;
	bBot = FParse::Param(FCommandLine::Get(), TEXT("netbot"));
}

void FOTGRoundNet::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(Tracers);
}

void FOTGRoundNet::OnStartHole()
{
	if (!bOn) return;
	bRemoteShot = false;
	bWaiting = false;
	PendingRest.Reset();
	for (FFlight& F : Flights) { F.bFlying = false; F.Rest.Reset(); }
	for (UOTGShotTracerComponent* T : Tracers) if (T) T->Clear();
	PlaceOthers();
}

// ------------------------------------------------------------------ who's who

int32 FOTGRoundNet::SideOf(int32 PlayerId) const { return PlayerOfSide.IndexOfByKey(PlayerId); }

bool FOTGRoundNet::Connected(int32 Side) const
{
	if (!bOn || Side == LocalSide) return true;
	UOTGNetSubsystem* N = UOTGNetSubsystem::Get(Round);
	return N && PlayerOfSide.IsValidIndex(Side) && N->IsConnected(PlayerOfSide[Side]);
}

FString FOTGRoundNet::SideName(int32 Side) const
{
	return Round->Turns.Game && Round->Turns.Game->Sides.IsValidIndex(Side) ? Round->Turns.Game->Sides[Side].Name : FString(TEXT("someone"));
}

bool FOTGRoundNet::MayHit() const
{
	return !bOn || bTogether || Round->Turns.SideIdx == LocalSide;
}

bool FOTGRoundNet::HoldFinish() const
{
	return bOn && !bTogether && bRemoteShot && !PendingRest.IsValid() && Connected(Round->Turns.SideIdx);
}

bool FOTGRoundNet::AllDone()
{
	if (!bOn) return true;
	FOTGGame& G = *Round->Turns.Game;
	for (int32 s = 0; s < G.Sides.Num(); s++)
	{
		FOTGSide& S = G.Sides[s];
		if (S.bHoled) continue;
		// (gone: picked up)
		if (!Connected(s))
		{
			G.HoleOut(S, Round->MaxStrokes(), true);
			S.Ball = Round->Pin();
			Round->HudView.Toast(FString::Printf(TEXT("%s has left the game: picked up"), *S.Name), 3.f);
			continue;
		}
		return false;
	}
	return true;
}

FString FOTGRoundNet::WaitingFor() const
{
	TArray<FString> Names;
	const FOTGGame& G = *Round->Turns.Game;
	for (int32 s = 0; s < G.Sides.Num(); s++) if (!G.Sides[s].bHoled && Connected(s)) Names.Add(G.Sides[s].Name);
	return Names.Num() ? FString::Join(Names, TEXT(", ")) : FString(TEXT("the others"));
}

// ------------------------------------------------------------------ sending

void FOTGRoundNet::SendShot(const FOTGShot& Shot, const otg::Shot& Play, const otg::ShotOptions& O, double FxSpeed, double FxSpin, int32 Seed)
{
	UOTGNetSubsystem* N = bOn ? UOTGNetSubsystem::Get(Round) : nullptr;
	if (!N) return;
	TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("k"), TEXT("shot"));
	M->SetNumberField(TEXT("hole"), Round->HoleIndex);
	M->SetNumberField(TEXT("stroke"), Round->Strokes);
	M->SetArrayField(TEXT("from"), NetArr({ Round->Ball.x, Round->Ball.y, Round->Ball.z }));
	M->SetStringField(TEXT("heading"), NetBits(Round->Heading));
	M->SetArrayField(TEXT("play"), NetArr({ Play.speedMph, Play.vla, Play.hla, Play.backSpin, Play.sideSpin }));
	M->SetArrayField(TEXT("opts"), NetArr({ O.heading, O.altitudeM, O.tempC, O.humidity, O.pressureHpa, O.windMps, O.windFromDeg, O.wet, O.stimp, O.rainMm }));
	M->SetArrayField(TEXT("fx"), NetArr({ FxSpeed, FxSpin }));
	M->SetNumberField(TEXT("seed"), Seed);
	M->SetStringField(TEXT("club"), Round->Club);
	TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
	FJsonObjectConverter::UStructToJsonObject(FOTGShot::StaticStruct(), &Shot, S);
	M->SetObjectField(TEXT("shot"), S);
	N->SendGame(M);
}

void FOTGRoundNet::SendUpdate(const otg::Shot& Play, int32 Seed)
{
	UOTGNetSubsystem* N = bOn ? UOTGNetSubsystem::Get(Round) : nullptr;
	if (!N) return;
	TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("k"), TEXT("upd"));
	M->SetNumberField(TEXT("hole"), Round->HoleIndex);
	M->SetArrayField(TEXT("play"), NetArr({ Play.speedMph, Play.vla, Play.hla, Play.backSpin, Play.sideSpin }));
	M->SetNumberField(TEXT("seed"), Seed);
	N->SendGame(M);
}

void FOTGRoundNet::SendRest()
{
	UOTGNetSubsystem* N = bOn ? UOTGNetSubsystem::Get(Round) : nullptr;
	if (!N) return;
	const FOTGSide& S = Round->Turns.Game->Sides[Round->Turns.SideIdx];
	TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("k"), TEXT("rest"));
	M->SetNumberField(TEXT("hole"), Round->HoleIndex);
	M->SetArrayField(TEXT("ball"), NetArr({ Round->Ball.x, Round->Ball.y, Round->Ball.z }));
	M->SetNumberField(TEXT("lie"), int32(Round->Lie));
	M->SetNumberField(TEXT("strokes"), Round->Strokes);
	M->SetBoolField(TEXT("holed"), S.bHoled);
	N->SendGame(M);
}

void FOTGRoundNet::SendHoled(int32 Strokes, bool bPicked)
{
	UOTGNetSubsystem* N = bOn ? UOTGNetSubsystem::Get(Round) : nullptr;
	if (!N) return;
	TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("k"), TEXT("holed"));
	M->SetNumberField(TEXT("hole"), Round->HoleIndex);
	M->SetNumberField(TEXT("strokes"), Strokes);
	M->SetBoolField(TEXT("picked"), bPicked);
	N->SendGame(M);
}

void FOTGRoundNet::SendWeather()
{
	UOTGNetSubsystem* N = UOTGNetSubsystem::Get(Round);
	if (!N) return;
	const FOTGWeather& W = Round->Weather;
	TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("k"), TEXT("wx"));
	M->SetStringField(TEXT("key"), W.Key);
	M->SetStringField(TEXT("label"), W.Label);
	M->SetArrayField(TEXT("w"), NetArr({ W.WindMph, W.GustMph, W.WindFromMet, W.TempC, W.Humidity, W.PressureHpa, W.RainMm, W.Cloud, W.Fog, Round->SunHour }));
	M->SetBoolField(TEXT("rel"), W.bRelative);
	M->SetBoolField(TEXT("live"), W.bLive);
	TArray<TSharedPtr<FJsonValue>> Rel;
	for (double R : Round->WindRel) Rel.Add(MakeShared<FJsonValueString>(NetBits(R)));
	M->SetArrayField(TEXT("windRel"), Rel);
	FString Text;
	FJsonSerializer::Serialize(M, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
	if (Text == WxSent) return;
	WxSent = Text;
	N->SendGame(M);
}

void FOTGRoundNet::ApplyWeather(const TSharedPtr<FJsonObject>& M)
{
	FOTGWeather& W = Round->Weather;
	M->TryGetStringField(TEXT("key"), W.Key);
	M->TryGetStringField(TEXT("label"), W.Label);
	const TArray<double> D = NetDoubles(M, TEXT("w"), 10);
	W.WindMph = D[0]; W.GustMph = D[1]; W.WindFromMet = D[2]; W.TempC = D[3]; W.Humidity = D[4];
	W.PressureHpa = D[5]; W.RainMm = D[6]; W.Cloud = D[7]; W.Fog = D[8];
	M->TryGetBoolField(TEXT("rel"), W.bRelative);
	M->TryGetBoolField(TEXT("live"), W.bLive);
	const TArray<double> Rel = NetDoubles(M, TEXT("windRel"), 0);
	if (Rel.Num()) Round->WindRel = Rel;
	if (D[9] != Round->SunHour) Round->SetSunHour(D[9]);
	Round->Scene.ApplyWeatherToScene();
	Round->HudView.UpdateHudInfo();
	Round->HudView.UpdateWindBox();
}

// ------------------------------------------------------------------ every frame

void FOTGRoundNet::Tick(float Dt)
{
	if (!bOn) return;
	UOTGNetSubsystem* N = UOTGNetSubsystem::Get(Round);
	// what's come (the weather at once; the rest in turn)
	if (N)
	{
		TSharedPtr<FJsonObject> M;
		while (N->PopGame(M))
		{
			FString K;
			M->TryGetStringField(TEXT("k"), K);
			if (K == TEXT("wx")) { if (!bHost) ApplyWeather(M); }
			else Waiting.Add(M);
		}
	}
	if (bHost && (WxAt -= Dt) <= 0) { WxAt = 2; SendWeather(); }

	// (someone's already on the next hole - the scorecard's waiting here: on to it)
	if (Round->Phase == AOTGRound::EPhase::Holed)
		for (const TSharedPtr<FJsonObject>& M : Waiting)
			if (NetInt(M, TEXT("hole")) > Round->HoleIndex) { Round->NextHole(); break; }

	// each player's messages in their order: one that can't be dealt with yet holds up the rest of theirs
	TSet<int32> Held;
	for (int32 i = 0; i < Waiting.Num();)
	{
		const TSharedPtr<FJsonObject> M = Waiting[i];
		const int32 P = NetInt(M, TEXT("p"));
		if (Held.Contains(P)) { i++; continue; }
		const int32 Side = SideOf(P);
		const int32 Hole = NetInt(M, TEXT("hole"));
		FString K;
		M->TryGetStringField(TEXT("k"), K);
		if (Side < 0 || Side == LocalSide || Hole < Round->HoleIndex) { Waiting.RemoveAt(i); continue; }
		if (bTogether ? TakeTogether(M, Side, K, Hole) : TakeTurns(M, Side, K, Hole)) Waiting.RemoveAt(i);
		else { Held.Add(P); i++; }
	}

	if (bTogether)
	{
		// the others' balls in the air
		const std::vector<otg::PathPoint>* Pts = nullptr;
		for (int32 s = 0; s < Flights.Num(); s++)
		{
			FFlight& F = Flights[s];
			if (!F.bFlying || !Round->Turns.OtherBalls.IsValidIndex(s)) continue;
			F.T += Dt;
			Pts = &F.Res.points;
			int32 Upto = 0;
			while (Upto < int32(Pts->size()) && (*Pts)[Upto].t <= F.T) Upto++;
			otg::V3 At = F.Res.rest;
			if (Upto < int32(Pts->size()) && Upto > 0)
			{
				const otg::PathPoint& A = (*Pts)[Upto - 1];
				const otg::PathPoint& B = (*Pts)[Upto];
				const double K = (F.T - A.t) / FMath::Max(1e-6, B.t - A.t);
				At = otg::V3{ A.x + (B.x - A.x) * K, A.y + (B.y - A.y) * K, A.z + (B.z - A.z) * K };
			}
			else if (Upto > 0) At = otg::V3{ Pts->back().x, Pts->back().y, Pts->back().z };
			UStaticMeshComponent* Ball = Round->Turns.OtherBalls[s];
			const FVector W = Round->World(At);
			// (bigger the further off it is, as the player's own ball in flight)
			const double Scale = 0.0427 * 2 * FMath::Clamp(FVector::Dist(Round->Camera.CamPos, W) / 2500.0, 1.0, 6.0);
			Ball->SetVisibility(true);
			Ball->SetWorldScale3D(FVector(Scale));
			Ball->SetWorldLocation(W + FVector(0, 0, 2.135 * Scale / 0.0427));
			if (Tracers.IsValidIndex(s) && Tracers[s] && !Pts->empty() && !Round->TracerFrom(otg::V3{ (*Pts)[0].x, (*Pts)[0].y, (*Pts)[0].z }))
				Tracers[s]->Clear();
			else if (Tracers.IsValidIndex(s) && Tracers[s])
			{
				// (as the player's own: to where it landed, then fading out - AOTGRound::TracerFadeS)
				const bool bDown = F.Res.hasCarryPoint && F.T >= F.Res.carryPoint.t;
				int32 To = Upto;
				FVector Head = W;
				if (bDown)
				{
					To = 0;
					while (To < int32(Pts->size()) && (*Pts)[To].t <= F.Res.carryPoint.t) To++;
					Head = Round->World(otg::V3{ F.Res.carryPoint.x, F.Res.carryPoint.y, F.Res.carryPoint.z });
				}
				TArray<FVector> Path;
				for (int32 j = 0; j < To && j < int32(Pts->size()); j++) Path.Add(Round->World(otg::V3{ (*Pts)[j].x, (*Pts)[j].y, (*Pts)[j].z }));
				const double Fade = bDown ? 1.0 - (F.T - F.Res.carryPoint.t) / AOTGRound::TracerFadeS : 1.0;
				if (Fade <= 0) Tracers[s]->Clear();
				else Tracers[s]->Draw(Path, Head, Round->Camera.CamPos, float(Fade));
			}
			if (Upto >= int32(Pts->size()))
			{
				F.bFlying = false;
				if (F.Res.hasCarryPoint && Tracers.IsValidIndex(s) && Tracers[s]) Tracers[s]->Clear();   // (what's left of its fade)
				if (F.Rest.IsValid())
				{
					const TSharedPtr<FJsonObject> R = F.Rest;
					F.Rest.Reset();
					TakeTogether(R, s, TEXT("rest"), Round->HoleIndex);
				}
			}
		}
		PlaceOthers();
	}
	// (taking turns: whoever's turn it is has left - picked up)
	else if (Round->Phase == AOTGRound::EPhase::Ready && !IsLocalSide(Round->Turns.SideIdx) && !Connected(Round->Turns.SideIdx))
	{
		Round->HudView.Toast(FString::Printf(TEXT("%s has left the game: picked up"), *SideName(Round->Turns.SideIdx)), 3.f);
		Round->HoleOut(true, TEXT("Left the game"));
	}
	// (testing: hit when it's ours to; at the end, back to the lobby)
	if (bBot)
	{
		const bool bReady = Round->Phase == AOTGRound::EPhase::Ready && MayHit() && !Round->bPaused;
		if (!bReady && Round->Phase != AOTGRound::EPhase::Done) BotIn = FMath::FRandRange(1.0, 2.5);
		else if ((BotIn -= Dt) <= 0)
		{
			BotIn = 5;
			if (Round->Phase == AOTGRound::EPhase::Done) Round->QuitToMenu();
			else Round->TestShot();
		}
	}
}

// (everyone at once: the others' balls where they lie, while not in the air)
void FOTGRoundNet::PlaceOthers()
{
	if (!bOn || !bTogether) return;
	const FOTGGame& G = *Round->Turns.Game;
	for (int32 s = 0; s < G.Sides.Num() && s < Round->Turns.OtherBalls.Num(); s++)
	{
		if (s == LocalSide || (Flights.IsValidIndex(s) && Flights[s].bFlying)) continue;
		const FOTGSide& S = G.Sides[s];
		UStaticMeshComponent* Ball = Round->Turns.OtherBalls[s];
		// (not one on the same spot as this computer's: the same tee - where it would hide it)
		const bool bShow = !S.bHoled && Connected(s) && Dist(S.Ball, Round->Ball) > 1.0;
		Ball->SetVisibility(bShow);
		if (!bShow) continue;
		Ball->SetWorldScale3D(FVector(0.0427 * 2));
		Ball->SetWorldLocation(Round->World(S.Ball) + FVector(0, 0, 2.135 * 2));
	}
}

// ------------------------------------------------------------------ taking turns

bool FOTGRoundNet::TakeTurns(const TSharedPtr<FJsonObject>& M, int32 Side, const FString& K, int32 Hole)
{
	if (Hole > Round->HoleIndex) return false;   // (once this game's on that hole)
	const bool bTheirs = Round->Turns.SideIdx == Side;
	if (K == TEXT("shot"))
	{
		// (once it's their turn here, the last shot done with)
		if (Round->Phase != AOTGRound::EPhase::Ready || !bTheirs) return false;
		const otg::V3 From = NetV3(M, TEXT("from"));
		if (Dist(From, Round->Ball) > 0.05) UE_LOG(LogOTGRoundNet, Warning, TEXT("%s's ball was %.2f m from where it is here"), *SideName(Side), Dist(From, Round->Ball));
		Round->Ball = From;
		Round->Heading = NetUnbits(M->GetStringField(TEXT("heading")));
		FString Club;
		if (M->TryGetStringField(TEXT("club"), Club) && !Club.IsEmpty()) Round->Club = Club;
		FOTGShot Shot;
		const TSharedPtr<FJsonObject>* S = nullptr;
		if (M->TryGetObjectField(TEXT("shot"), S)) FJsonObjectConverter::JsonObjectToUStruct((*S).ToSharedRef(), &Shot);
		Round->LastShot = Shot;
		const TArray<double> Fx = NetDoubles(M, TEXT("fx"), 2);
		bRemoteShot = true;
		PendingRest.Reset();
		Round->PendingShot.Reset();
		Round->StartShot(NetPlay(M), NetOptions(M), Fx[0], Fx[1], NetInt(M, TEXT("seed")));
		return true;
	}
	if (K == TEXT("upd"))
	{
		if (bRemoteShot && bTheirs && Round->Phase == AOTGRound::EPhase::Flying) Round->RefineShot(NetPlay(M), NetInt(M, TEXT("seed")));
		return true;
	}
	if (K == TEXT("rest"))
	{
		// (their shot still in the air here: where it finished, for when it does)
		if (bRemoteShot && bTheirs && Round->Phase == AOTGRound::EPhase::Flying) { PendingRest = M; return true; }
		// (finished here already: set right, if it isn't)
		if (!M->GetBoolField(TEXT("holed")) && !bTheirs && Round->Turns.Game->Sides.IsValidIndex(Side))
		{
			FOTGSide& S = Round->Turns.Game->Sides[Side];
			if (!S.bHoled) { S.Ball = NetV3(M, TEXT("ball")); S.Strokes = NetInt(M, TEXT("strokes")); S.Lie = otg::Surface(NetInt(M, TEXT("lie"))); }
		}
		return true;
	}
	return true;   // (holed: every game holes them out itself)
}

void FOTGRoundNet::ApplyRest()
{
	bRemoteShot = false;
	if (!PendingRest.IsValid()) return;
	const TSharedPtr<FJsonObject> M = PendingRest;
	PendingRest.Reset();
	// (holed here, or picked up: the hole's own way on)
	if (M->GetBoolField(TEXT("holed")) || Round->Phase != AOTGRound::EPhase::Resting) return;
	const otg::V3 B = NetV3(M, TEXT("ball"));
	const int32 Strokes = NetInt(M, TEXT("strokes"));
	if (Dist(B, Round->Ball) > 0.05 || Strokes != Round->Strokes)
	{
		UE_LOG(LogOTGRoundNet, Warning, TEXT("%s's ball finished %.2f m from here (%d strokes, here %d): put where their game has it"), *SideName(Round->Turns.SideIdx), Dist(B, Round->Ball), Strokes, Round->Strokes);
		Round->Ball = B;
		Round->Strokes = Strokes;
		Round->Lie = otg::Surface(NetInt(M, TEXT("lie")));
		Round->PlaceBall(B);
		Round->HudView.UpdateHudInfo();
	}
}

// ------------------------------------------------------------------ everyone at once

bool FOTGRoundNet::TakeTogether(const TSharedPtr<FJsonObject>& M, int32 Side, const FString& K, int32 Hole)
{
	if (Hole > Round->HoleIndex || !Flights.IsValidIndex(Side)) return false;
	FFlight& F = Flights[Side];
	FOTGSide& S = Round->Turns.Game->Sides[Side];
	if (K == TEXT("shot"))
	{
		if (F.bFlying) return false;   // (their last still in the air here)
		F.From = NetV3(M, TEXT("from"));
		F.Opts = NetOptions(M);
		F.Opts.hasPin = true;
		F.Opts.pin = Round->Pin();
		F.Opts.trees = &Round->Trees;
		F.Opts.collider = &Round->Collider;
		F.Opts.random = NetSeeded(NetInt(M, TEXT("seed")));
		F.Res = otg::SimulateShot(Round->Course, F.From, NetPlay(M), F.Opts);
		F.T = 0;
		F.bFlying = true;
		F.Rest.Reset();
		if (Tracers.IsValidIndex(Side) && Tracers[Side]) Tracers[Side]->Clear();
		if (bWaiting) Round->HudView.Toast(FString::Printf(TEXT("%s hits"), *S.Name), 1.5f);
		return true;
	}
	if (K == TEXT("upd"))
	{
		if (F.bFlying)
		{
			F.Opts.random = NetSeeded(NetInt(M, TEXT("seed")));
			F.Res = otg::SimulateShot(Round->Course, F.From, NetPlay(M), F.Opts);
		}
		return true;
	}
	if (K == TEXT("rest"))
	{
		if (F.bFlying) { F.Rest = M; return true; }
		if (!S.bHoled)
		{
			S.Ball = NetV3(M, TEXT("ball"));
			S.Strokes = NetInt(M, TEXT("strokes"));
			S.Lie = otg::Surface(NetInt(M, TEXT("lie")));
		}
		return true;
	}
	if (K == TEXT("holed"))
	{
		if (F.bFlying) return false;   // (once it's dropped here)
		if (!S.bHoled)
		{
			const int32 Strokes = NetInt(M, TEXT("strokes"));
			const bool bPicked = M->GetBoolField(TEXT("picked"));
			Round->Turns.Game->HoleOut(S, Strokes, bPicked);
			S.Ball = Round->Pin();
			const FOTGHole& H = Round->Hole();
			Round->HudView.Toast(FString::Printf(TEXT("%s: %s (%d)"), *S.Name, bPicked ? TEXT("picked up") : *ScoreName(Strokes, H.Par), Strokes), 3.f);
		}
		return true;
	}
	return true;
}
