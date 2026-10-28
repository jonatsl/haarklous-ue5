#include "HKPaintball.h"
#include "HKUi.h"
#include "Haarklous.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	const FLinearColor TeamCol[2] = {FLinearColor(.25f, .55f, 1.f), FLinearColor(1.f, .35f, .25f)};
	const TCHAR* TeamName[2] = {TEXT("Blue"), TEXT("Red")};

	double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double Def = 0) { double V; return O.IsValid() && O->TryGetNumberField(K, V) ? V : Def; }
	TArray<double> Nums(const TSharedPtr<FJsonValue>& V)
	{
		TArray<double> Out;
		if (V.IsValid() && V->Type == EJson::Array) for (const TSharedPtr<FJsonValue>& E : V->AsArray()) Out.Add(E.IsValid() && E->Type == EJson::Number ? E->AsNumber() : 0);
		return Out;
	}
	TSharedPtr<FJsonValue> NumArr(const TArray<double>& A)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (double D : A) Out.Add(MakeShared<FJsonValueNumber>(D));
		return MakeShared<FJsonValueArray>(Out);
	}
	double R3(double V) { return FMath::RoundToDouble(V * 1000) / 1000; }

	// Closest distance between segments AB and CD; S = parameter on AB.
	double SegSeg(const FVector& A, const FVector& B, const FVector& C, const FVector& D, double& S)
	{
		const FVector D1 = B - A, D2 = D - C, R = A - C;
		const double a = D1.SizeSquared(), e = D2.SizeSquared(), f = FVector::DotProduct(D2, R);
		double s = 0, t = 0;
		if (a < 1e-12 && e < 1e-12) { S = 0; return R.Size(); }
		if (a < 1e-12) { s = 0; t = FMath::Clamp(f / e, 0.0, 1.0); }
		else
		{
			const double c = FVector::DotProduct(D1, R);
			if (e < 1e-12) { t = 0; s = FMath::Clamp(-c / a, 0.0, 1.0); }
			else
			{
				const double b = FVector::DotProduct(D1, D2), den = a * e - b * b;
				s = den > 1e-12 ? FMath::Clamp((b * f - c * e) / den, 0.0, 1.0) : 0;
				t = (b * s + f) / e;
				if (t < 0) { t = 0; s = FMath::Clamp(-c / a, 0.0, 1.0); }
				else if (t > 1) { t = 1; s = FMath::Clamp((b - c) / a, 0.0, 1.0); }
			}
		}
		S = s;
		return ((A + D1 * s) - (C + D2 * t)).Size();
	}

	// A body: a capsule from the feet up (lying along the facing when prone).
	void Body(const FVector& Feet, double Yaw, const FString& Stance, FVector& A, FVector& B)
	{
		if (Stance == TEXT("prone"))
		{
			const FVector F(-FMath::Sin(Yaw), 0, -FMath::Cos(Yaw));
			A = Feet + FVector(0, .2, 0) + F * .75; B = Feet + FVector(0, .2, 0) - F * .65;
			return;
		}
		const double Top = Stance == TEXT("crouch") ? 1.15 : Stance == TEXT("sit") ? 1.0 : 1.72;
		A = Feet + FVector(0, .15, 0); B = Feet + FVector(0, Top, 0);
	}

	// Chance that a ball breaks (and marks) at this speed along the surface normal (m/s): bounces when slow or glancing.
	double BreakChance(double Vn) { return FMath::Clamp(.15 + (Vn - 25) / 40, .05, .95); }
}

// ---------------------------------------------------------------------------------------------------------------
// Session and member states (JSON on the wire, short keys)
// ---------------------------------------------------------------------------------------------------------------
TSharedPtr<FJsonObject> FHKPbSession::ToJson() const
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("id"), Id);
	O->SetStringField(TEXT("m"), Mode);
	O->SetNumberField(TEXT("rb"), bReball ? 1 : 0);
	O->SetNumberField(TEXT("rs"), bRespawn ? 1 : 0);
	O->SetStringField(TEXT("st"), State);
	O->SetNumberField(TEXT("t0"), T0);
	O->SetNumberField(TEXT("du"), Dur);
	O->SetNumberField(TEXT("li"), Lim);
	TSharedPtr<FJsonObject> Tm = MakeShared<FJsonObject>(), Ou = MakeShared<FJsonObject>();
	for (const auto& KV : Team) Tm->SetNumberField(FString::FromInt(KV.Key), KV.Value);
	for (const auto& KV : Outs) Ou->SetNumberField(FString::FromInt(KV.Key), KV.Value);
	O->SetObjectField(TEXT("tm"), Tm);
	O->SetObjectField(TEXT("ou"), Ou);
	TArray<TSharedPtr<FJsonValue>> Fl;
	for (const FFlag& F : Flag)
	{
		TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("s"), F.S); J->SetNumberField(TEXT("by"), F.By); J->SetField(TEXT("p"), NumArr({R3(F.P.X), R3(F.P.Y), R3(F.P.Z)})); J->SetNumberField(TEXT("t"), F.T);
		Fl.Add(MakeShared<FJsonValueObject>(J));
	}
	O->SetArrayField(TEXT("fl"), Fl);
	O->SetField(TEXT("ca"), NumArr({(double)Caps[0], (double)Caps[1]}));
	O->SetField(TEXT("ro"), NumArr({(double)Rounds[0], (double)Rounds[1]}));
	O->SetNumberField(TEXT("rn"), Round);
	O->SetNumberField(TEXT("et"), EndT);
	O->SetNumberField(TEXT("wi"), Winner);
	return O;
}

bool FHKPbSession::FromJson(const TSharedPtr<FJsonObject>& O, FHKPbSession& Out)
{
	if (!O.IsValid()) return false;
	FHKPbSession S;
	S.Id = (int32)Num(O, TEXT("id"));
	O->TryGetStringField(TEXT("m"), S.Mode);
	S.bReball = Num(O, TEXT("rb")) != 0;
	S.bRespawn = Num(O, TEXT("rs"), 1) != 0;
	O->TryGetStringField(TEXT("st"), S.State);
	S.T0 = Num(O, TEXT("t0")); S.Dur = Num(O, TEXT("du"), 600); S.Lim = (int32)Num(O, TEXT("li"), 20);
	const TSharedPtr<FJsonObject>* Tm; const TSharedPtr<FJsonObject>* Ou;
	if (O->TryGetObjectField(TEXT("tm"), Tm)) for (const auto& KV : (*Tm)->Values) S.Team.Add(FCString::Atoi(*KV.Key), (int32)KV.Value->AsNumber());
	if (O->TryGetObjectField(TEXT("ou"), Ou)) for (const auto& KV : (*Ou)->Values) S.Outs.Add(FCString::Atoi(*KV.Key), (int32)KV.Value->AsNumber());
	const TArray<TSharedPtr<FJsonValue>>* Fl;
	if (O->TryGetArrayField(TEXT("fl"), Fl))
		for (int32 i = 0; i < 2 && i < Fl->Num(); i++)
		{
			const TSharedPtr<FJsonObject> J = (*Fl)[i]->AsObject();
			if (!J) continue;
			J->TryGetStringField(TEXT("s"), S.Flag[i].S); S.Flag[i].By = (int32)Num(J, TEXT("by")); S.Flag[i].T = Num(J, TEXT("t"));
			const TArray<double> P = Nums(J->TryGetField(TEXT("p")));
			if (P.Num() == 3) S.Flag[i].P = FVector(P[0], P[1], P[2]);
		}
	const TArray<double> Ca = Nums(O->TryGetField(TEXT("ca"))), Ro = Nums(O->TryGetField(TEXT("ro")));
	for (int32 i = 0; i < 2; i++) { S.Caps[i] = Ca.IsValidIndex(i) ? (int32)Ca[i] : 0; S.Rounds[i] = Ro.IsValidIndex(i) ? (int32)Ro[i] : 0; }
	S.Round = (int32)Num(O, TEXT("rn"), 1); S.EndT = Num(O, TEXT("et")); S.Winner = (int32)Num(O, TEXT("wi"), -1);
	Out = S;
	return true;
}

int32 FHKPbSession::Score(int32 T) const
{
	if (Mode == TEXT("ctf")) return Caps[T];
	if (!bRespawn) return Rounds[T];
	int32 N = 0;   // eliminations of the other team's players
	for (const auto& KV : Outs) if (TeamOf(KV.Key) == 1 - T) N += KV.Value;
	return N;
}

bool FHKPbMember::FromJson(const TSharedPtr<FJsonObject>& O, FHKPbMember& M)
{
	if (!O.IsValid()) return false;
	M.Lobby = Num(O, TEXT("l"));
	M.Req = (int32)Num(O, TEXT("q"), -1);
	M.Ses = (int32)Num(O, TEXT("s"));
	M.bAlive = Num(O, TEXT("a"), 1) != 0;
	M.Outs = (int32)Num(O, TEXT("o"));
	M.By = (int32)Num(O, TEXT("b"));
	M.Round = (int32)Num(O, TEXT("r"));
	M.Shots.Reset();
	const TArray<TSharedPtr<FJsonValue>>* H;
	if (O->TryGetArrayField(TEXT("h"), H)) for (const TSharedPtr<FJsonValue>& V : *H) { TArray<double> S = Nums(V); if (S.Num() == 8) M.Shots.Add(S); }
	const TSharedPtr<FJsonObject>* Se;
	M.Ses_ = O->TryGetObjectField(TEXT("ses"), Se) ? *Se : nullptr;
	return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Course
// ---------------------------------------------------------------------------------------------------------------
bool FHKPaintball::Load(const FString& Path)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path)) return false;
	TSharedPtr<FJsonObject> J;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) || !J.IsValid()) return false;
	const TSharedPtr<FJsonObject> Rc = J->GetObjectField(TEXT("rect")), Pl = J->GetObjectField(TEXT("play")), Lb = J->GetObjectField(TEXT("lobby")), C = J->GetObjectField(TEXT("centre"));
	const double Cx = C->GetNumberField(TEXT("x")), Cs = C->GetNumberField(TEXT("s"));
	const TArray<double> Wc = Nums(C->TryGetField(TEXT("world")));
	Centre = FVector2D(Wc[0], Wc[1]);
	Theta = C->GetNumberField(TEXT("theta"));
	Floor = J->GetNumberField(TEXT("floor"));
	Rect0 = FVector2D(Rc->GetNumberField(TEXT("x0")) - Cx, Rc->GetNumberField(TEXT("s0")) - Cs);
	Rect1 = Rect0 + FVector2D(Rc->GetNumberField(TEXT("w")), Rc->GetNumberField(TEXT("d")));
	PlayHalfU = Pl->GetNumberField(TEXT("w")) / 2; PlayHalfV = Pl->GetNumberField(TEXT("d")) / 2;
	Lobby0 = FVector2D(Lb->GetNumberField(TEXT("x0")) - Cx, Lb->GetNumberField(TEXT("s0")) - Cs);
	Lobby1 = Lobby0 + FVector2D(Lb->GetNumberField(TEXT("w")), Lb->GetNumberField(TEXT("d")));
	const TArray<double> G = Nums(J->TryGetField(TEXT("gate")));
	GateU0 = G[0] - Cx; GateU1 = G[1] - Cx;
	// Cover boxes (balls) and obstacles (walking).
	auto Ob = [this](double U, double V, double W, double D, double Yaw, double Y0, double Y1)
	{
		FHKObstacle O;
		const double C = FMath::Cos(Yaw), S = FMath::Sin(Yaw);
		for (const FVector2D& K : {FVector2D(-1, -1), FVector2D(1, -1), FVector2D(1, 1), FVector2D(-1, 1)})
		{
			const double X = K.X * W / 2, Z = K.Y * D / 2;
			const FVector P = World(U + X * C + Z * S, 0, V - X * S + Z * C);
			O.Points.Add(FHKPt(P.X, P.Z));
		}
		O.MinY = Y0; O.MaxY = Y1; O.Kind = TEXT("paintball");
		O.Prepare();
		Obst.Add(O);
	};
	const TArray<TSharedPtr<FJsonValue>>* Bx;
	if (J->TryGetArrayField(TEXT("boxes"), Bx))
		for (const TSharedPtr<FJsonValue>& V : *Bx)
		{
			const TArray<double> B = Nums(V);   // u, v, w, d, y0, h, yaw
			FHKPbBox Box;
			Box.C = World(B[0], Floor + B[4] + B[5] / 2, B[1]);
			Box.H = FVector(B[2] / 2, B[5] / 2, B[3] / 2);
			Box.Yaw = WorldYaw(B[6]);
			BoxList.Add(Box);
			if (B[4] < 1.2) Ob(B[0], B[1], B[2], B[3], B[6], Floor + B[4], Floor + B[4] + B[5]);
		}
	// The net round the play area and the yard's walls (a gap for the gate).
	const double Hu = PlayHalfU, Hv = PlayHalfV;
	Ob(0, -Hv, 2 * Hu, .12, 0, Floor - 1, Floor + 5); Ob(0, Hv, 2 * Hu, .12, 0, Floor - 1, Floor + 5);
	Ob(-Hu, 0, .12, 2 * Hv, 0, Floor - 1, Floor + 5); Ob(Hu, 0, .12, 2 * Hv, 0, Floor - 1, Floor + 5);
	const double U0 = Rect0.X, U1 = Rect1.X, V0 = Rect0.Y, V1 = Rect1.Y;
	Ob((U0 + U1) / 2, V0, U1 - U0, .3, 0, Floor - 12, Floor + 14);
	Ob(U0, (V0 + V1) / 2, .3, V1 - V0, 0, Floor - 12, Floor + 14); Ob(U1, (V0 + V1) / 2, .3, V1 - V0, 0, Floor - 12, Floor + 14);
	Ob((U0 + GateU0) / 2, V1, GateU0 - U0, .3, 0, Floor - 12, Floor + 14); Ob((GateU1 + U1) / 2, V1, U1 - GateU1, .3, 0, Floor - 12, Floor + 14);
	// Bases.
	const TArray<TSharedPtr<FJsonValue>>* Ba;
	if (J->TryGetArrayField(TEXT("bases"), Ba))
		for (int32 i = 0; i < 2 && i < Ba->Num(); i++)
		{
			const TSharedPtr<FJsonObject> B = (*Ba)[i]->AsObject();
			const TArray<double> F = Nums(B->TryGetField(TEXT("flag")));
			Flags[i] = FVector2D(F[0], F[1]);
			for (const TSharedPtr<FJsonValue>& S : B->GetArrayField(TEXT("spawns"))) { const TArray<double> P = Nums(S); Spawns[i].Add(FVector(P[0], P[1], P[2])); }
		}
	// Lobby furniture on the lawn outside the gate (course coordinates; the board faces the lawn, the sign the path).
	const double Gu = (GateU0 + GateU1) / 2, Gv = V1;
	const struct { const TCHAR* K; double U, V, Yaw; } L[] = {
		{TEXT("Shelter"), Gu - 8, Gv + 7.5, 0}, {TEXT("Bench"), Gu - 9, Gv + 6.8, 0}, {TEXT("Bench"), Gu - 6.5, Gv + 6.8, 0},
		{TEXT("Rack"), Gu - 4, Gv + 8.5, 0}, {TEXT("Board"), Gu + 5.5, Gv + 8.5, PI}, {TEXT("Sign"), Gu + 3.2, Gv + 1.2, 0},
		{TEXT("Bench"), Gu + 6, Gv + 4.5, 0}};
	for (const auto& E : L)
	{
		const FVector P = World(E.U, 0, E.V);
		LobbyProps.Add({E.K, P.X, P.Z, WorldYaw(E.Yaw)});
	}
	UE_LOG(LogHaarklous, Display, TEXT("Paintball course: %d boxes, %d obstacles, floor %.2f m, centre (%.1f, %.1f)"), BoxList.Num(), Obst.Num(), Floor, Centre.X, Centre.Y);
	return true;
}

FVector2D FHKPaintball::Course(double X, double Z) const
{
	const double C = FMath::Cos(Theta), S = FMath::Sin(Theta), Dx = X - Centre.X, Dz = Z - Centre.Y;
	return FVector2D(Dx * C - Dz * S, Dx * S + Dz * C);
}

FVector FHKPaintball::World(double U, double Y, double V) const
{
	const double C = FMath::Cos(Theta), S = FMath::Sin(Theta);
	return FVector(Centre.X + U * C + V * S, Y, Centre.Y - U * S + V * C);
}

bool FHKPaintball::InYard(double X, double Z, double Margin) const
{
	const FVector2D P = Course(X, Z);
	return P.X > Rect0.X + Margin && P.X < Rect1.X - Margin && P.Y > Rect0.Y + Margin && P.Y < Rect1.Y - Margin;
}

bool FHKPaintball::Ground(double X, double Z, double& OutY) const
{
	if (!InYard(X, Z)) return false;
	OutY = Floor;
	return true;
}

bool FHKPaintball::InLobby(double X, double Z) const
{
	const FVector2D P = Course(X, Z);
	return P.X > Lobby0.X && P.X < Lobby1.X && P.Y > Lobby0.Y && P.Y < Lobby1.Y;
}

bool FHKPaintball::InPlay(double X, double Z, double Margin) const
{
	const FVector2D P = Course(X, Z);
	return FMath::Abs(P.X) < PlayHalfU - Margin && FMath::Abs(P.Y) < PlayHalfV - Margin;
}

FVector FHKPaintball::SpawnPoint(int32 Team, int32 Index, double& OutYaw) const
{
	const FVector S = Spawns[Team][Index % Spawns[Team].Num()];
	OutYaw = WorldYaw(S.Z);
	return World(S.X, Floor, S.Y);
}

FVector FHKPaintball::BaseFlag(int32 Team) const { return World(Flags[Team].X, Floor + .27, Flags[Team].Y); }

// ---------------------------------------------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------------------------------------------
double FHKPaintball::Now() const { return Ctx.ServerNow ? Ctx.ServerNow() : Clock * 1000; }
double FHKPaintball::Rand() const { return Ctx.Random ? Ctx.Random() : FMath::FRand(); }
bool FHKPaintball::InGame() const { return MySes != 0 && Ses.Id == MySes && Ses.State != TEXT("lobby"); }
bool FHKPaintball::Armed() const { return InGame() && (Ses.State == TEXT("play") || Ses.State == TEXT("count")); }
bool FHKPaintball::Frozen() const { return InGame() && Ses.State == TEXT("count"); }
int32 FHKPaintball::MyTeam() const { return InGame() ? Ses.TeamOf(You()) : -1; }
int32 FHKPaintball::TeamOfMember(int32 Id) const { return Ses.TeamOf(Id); }

bool FHKPaintball::MemberPlaying(int32 Id) const
{
	if (Ses.Id == 0 || Ses.State == TEXT("lobby") || Ses.TeamOf(Id) < 0) return false;
	for (const FHKPbMember& M : All) if (M.Id == Id) return M.Ses == Ses.Id;
	return false;
}

void FHKPaintball::Feed(const FString& Text)
{
	FeedLines.Add({Text, Clock});
	if (FeedLines.Num() > 5) FeedLines.RemoveAt(0);
}

void FHKPaintball::Update(double Dt, const FInput& In)
{
	Clock += Dt;
	HitFlash = FMath::Max(0.0, HitFlash - Dt);
	NewFired.Reset();
	// Our balls that nobody took (offline, or nobody else in the lobby).
	Outbox.RemoveAll([this](const TArray<double>& S) { return Now() - S[1] > 2000; });
	ReadMembers();
	// The admin: the earliest member (then the lowest id). Its game is everybody's game.
	AdminId = 0;
	double Best = 0;
	for (const FHKPbMember& M : All)
		if (M.Lobby > 0 && (Best == 0 || M.Lobby < Best || (M.Lobby == Best && M.Id < AdminId))) { Best = M.Lobby; AdminId = M.Id; }
	if (AdminId != You())
		for (const FHKPbMember& M : All)
			if (M.Id == AdminId && M.Ses_.IsValid()) FHKPbSession::FromJson(M.Ses_, Ses);
	if (Best > 0 && AdminId == You()) RunAdmin(Dt, In);
	// Balls the others fired (each once, caught up to now).
	for (const FHKPbMember& M : All)
	{
		if (M.bLocal) continue;
		int32& Last = SeenShot.FindOrAdd(M.Id);
		for (const TArray<double>& S : M.Shots)
		{
			if ((int32)S[0] <= Last) continue;
			Last = (int32)S[0];
			const double Age = (Now() - S[1]) / 1000;
			if (Age > 1.5 || M.Ses == 0 || M.Ses != Ses.Id) continue;
			FHKPbBall B;
			B.Owner = M.Id; B.Team = FMath::Max(0, Ses.TeamOf(M.Id));
			B.P = FVector(S[2], S[3], S[4]); B.V = FVector(S[5], S[6], S[7]);
			NewFired.Add(B.Team);
			// Catch up on the time it has been flying (with the same collisions).
			TArray<FHKPbBall> Keep = MoveTemp(Balls);
			Balls.Reset();
			Balls.Add(B);
			if (Age > 0) StepBalls(Age);
			Keep.Append(Balls);
			Balls = MoveTemp(Keep);
		}
	}
	// The feed: who put whom out.
	for (const FHKPbMember& M : All)
	{
		int32& Seen = SeenOuts.FindOrAdd(M.Id);
		if (M.Ses == Ses.Id && Ses.Id != 0 && M.Outs > Seen)
		{
			FString By = TEXT("?");
			for (const FHKPbMember& K : All) if (K.Id == M.By) By = K.Name;
			Feed(FString::Printf(TEXT("%s  hit  %s"), *By, *M.Name));
			if (M.By == You() && !M.bLocal && Ctx.Notice) Ctx.Notice(FString::Printf(TEXT("Hit! %s is out."), *M.Name), 2);
		}
		Seen = M.Ses == Ses.Id ? M.Outs : 0;
	}
	Local(Dt, In);
	StepBalls(Dt);
}

void FHKPaintball::ReadMembers()
{
	All.Reset();
	FHKPbMember Me;
	Me.Id = You(); Me.bLocal = true;
	Me.Name = Ctx.YourName ? Ctx.YourName() : TEXT("You");
	Me.Slot = Ctx.YourSlot ? Ctx.YourSlot() : 0;
	Me.Lobby = LobbyT; Me.Req = Req; Me.Ses = InGame() ? MySes : 0; Me.bAlive = bAlive; Me.Outs = Outs; Me.By = OutBy; Me.Round = MyRound;
	Me.Feet = Ctx.Feet ? Ctx.Feet() : FVector::ZeroVector;
	Me.Yaw = Ctx.Yaw ? Ctx.Yaw() : 0;
	Me.Stance = Ctx.Stance ? Ctx.Stance() : TEXT("stand");
	All.Add(Me);
	if (Ctx.Others)
	{
		TArray<FHKPbMember> O;
		Ctx.Others(O);
		for (FHKPbMember& M : O) if (M.Lobby > 0) All.Add(M);
	}
}

void FHKPaintball::RunAdmin(double Dt, const FInput& In)
{
	const double T = Now();
	if (Ses.State == TEXT("lobby"))
	{
		if (!bInLobby) return;
		// ←→ the game: team deathmatch with respawns, team deathmatch in rounds, capture the flag. △ paint or reball.
		if (In.bLeft || In.bRight)
		{
			const int32 K = Ses.Mode == TEXT("ctf") ? 2 : Ses.bRespawn ? 0 : 1, N = (K + (In.bRight ? 1 : 2)) % 3;
			Ses.Mode = N == 2 ? TEXT("ctf") : TEXT("tdm");
			Ses.bRespawn = N != 1;
			Ses.Lim = N == 0 ? 20 : 3;
			if (Ctx.Sound) Ctx.Sound(TEXT("beep"), .4);
		}
		if (In.bTriangle) { Ses.bReball = !Ses.bReball; if (Ctx.Sound) Ctx.Sound(TEXT("beep"), .4); }
		if (In.bCrossHeld && StartHold == 0) UE_LOG(LogHaarklous, Display, TEXT("Paintball: the admin holds the start button"));
		StartHold = In.bCrossHeld ? StartHold + Dt : 0;
		if (StartHold > 1) { StartHold = 0; StartGame(); }
		return;
	}
	// Nobody is in the game any more (everyone left with △ or went offline): it ends at once, back to the lobby, so a
	// new game can start (user, 28 Oct 2026: an empty game used to run on until its time was up).
	if (Ses.State == TEXT("count") || Ses.State == TEXT("play"))
	{
		bool bAnyone = false;
		for (const FHKPbMember& M : All) if (Ses.TeamOf(M.Id) >= 0 && M.Ses == Ses.Id) bAnyone = true;
		EmptyT = bAnyone ? 0 : EmptyT + Dt;
		if (EmptyT > 3)
		{
			EmptyT = 0;
			Ses.State = TEXT("lobby");
			UE_LOG(LogHaarklous, Display, TEXT("Paintball: game %d has nobody left in it; back to the lobby"), Ses.Id);
			return;
		}
	}
	if (Ses.State == TEXT("count")) { if (T >= Ses.T0) { Ses.State = TEXT("play"); } return; }
	if (Ses.State == TEXT("end")) { if (T >= Ses.EndT + HKPb::EndS * 1000) Ses.State = TEXT("lobby"); return; }
	// Playing.
	TArray<const FHKPbMember*> In0, In1;
	for (const FHKPbMember& M : All)
	{
		const int32 Tm = Ses.TeamOf(M.Id);
		if (Tm < 0 || M.Ses != Ses.Id) continue;
		int32& O = Ses.Outs.FindOrAdd(M.Id);
		O = FMath::Max(O, M.Outs);
		(Tm == 0 ? In0 : In1).Add(&M);
	}
	bool bEnd = T >= Ses.T0 + Ses.Dur * 1000;
	if (Ses.Mode == TEXT("tdm") && Ses.bRespawn) bEnd |= Ses.Score(0) >= Ses.Lim || Ses.Score(1) >= Ses.Lim;
	if (Ses.Mode == TEXT("tdm") && !Ses.bRespawn && In0.Num() && In1.Num())
	{
		// A round: out is out until one team is left (players not yet in the new round count as alive).
		auto Down = [this](const TArray<const FHKPbMember*>& L) { for (const FHKPbMember* M : L) if (M->bAlive || M->Round != Ses.Round) return false; return true; };
		const bool D0 = Down(In0), D1 = Down(In1);
		if (D0 || D1)
		{
			const int32 W = D0 && D1 ? -1 : D0 ? 1 : 0;
			if (W >= 0) Ses.Rounds[W]++;
			if (W >= 0 && Ses.Rounds[W] >= Ses.Lim) bEnd = true;
			else { Ses.Round++; if (Ctx.Sound) Ctx.Sound(TEXT("whistle"), .6); }
		}
	}
	if (Ses.Mode == TEXT("ctf"))
	{
		for (int32 F = 0; F < 2; F++)
		{
			FHKPbSession::FFlag& Fl = Ses.Flag[F];
			const TArray<const FHKPbMember*>& Owners = F == 0 ? In0 : In1;
			const TArray<const FHKPbMember*>& Takers = F == 0 ? In1 : In0;
			if (Fl.S == TEXT("carried"))
			{
				const FHKPbMember* C = nullptr;
				for (const FHKPbMember* M : Takers) if (M->Id == Fl.By) C = M;
				if (!C || !C->bAlive) { Fl.S = TEXT("dropped"); Fl.T = T; Fl.By = 0; if (C) Fl.P = C->Feet + FVector(0, .05, 0); continue; }
				Fl.P = C->Feet;
				// Home: the carrier's own base (beyond its base line).
				const double U = Course(C->Feet.X, C->Feet.Z).X;
				if ((F == 0 && U > 25) || (F == 1 && U < -25))
				{
					Ses.Caps[1 - F]++;
					Fl = FHKPbSession::FFlag(); Fl.P = BaseFlag(F);
					if (Ctx.Sound) Ctx.Sound(TEXT("cheer"), .5);
					if (Ses.Caps[1 - F] >= Ses.Lim) bEnd = true;
				}
				continue;
			}
			if (Fl.S == TEXT("dropped"))
			{
				bool bBack = T - Fl.T > HKPb::FlagHomeS * 1000;
				for (const FHKPbMember* M : Owners) if (M->bAlive && FVector::Dist(M->Feet, Fl.P) < 1.4) bBack = true;
				if (bBack) { Fl = FHKPbSession::FFlag(); Fl.P = BaseFlag(F); continue; }
			}
			if (Fl.S == TEXT("home")) Fl.P = BaseFlag(F);
			for (const FHKPbMember* M : Takers)
				if (M->bAlive && FVector::Dist2D(FVector(M->Feet.X, M->Feet.Z, 0), FVector(Fl.P.X, Fl.P.Z, 0)) < 1.4 && FMath::Abs(M->Feet.Y - Fl.P.Y) < 1.5)
				{
					Fl.S = TEXT("carried"); Fl.By = M->Id;
					break;
				}
		}
	}
	if (bEnd)
	{
		Ses.State = TEXT("end");
		Ses.EndT = T;
		const int32 A = Ses.Score(0), B = Ses.Score(1);
		Ses.Winner = A > B ? 0 : B > A ? 1 : -1;
	}
}

void FHKPaintball::StartGame()
{
	// Everybody in the lobby, in the order they came; wishes first (6 a team at most), the rest balanced.
	TArray<const FHKPbMember*> L;
	for (const FHKPbMember& M : All) if (M.Lobby > 0) L.Add(&M);
	L.Sort([](const FHKPbMember& A, const FHKPbMember& B) { return A.Lobby < B.Lobby || (A.Lobby == B.Lobby && A.Id < B.Id); });
	Ses.Team.Reset(); Ses.Outs.Reset();
	int32 N[2] = {0, 0};
	for (const FHKPbMember* M : L) if (M->Req >= 0 && N[M->Req] < HKPb::TeamMax) { Ses.Team.Add(M->Id, M->Req); N[M->Req]++; }
	for (const FHKPbMember* M : L)
	{
		if (Ses.Team.Contains(M->Id)) continue;
		const int32 T = N[0] <= N[1] ? 0 : 1;
		if (N[T] >= HKPb::TeamMax) continue;   // full: waits for the next game
		Ses.Team.Add(M->Id, T); N[T]++;
	}
	Ses.Id = (int32)FMath::Fmod(Now() / 1000, 1e8) + 1;
	Ses.State = TEXT("count");
	Ses.T0 = Now() + HKPb::CountS * 1000;
	Ses.Dur = 600;
	Ses.Lim = Ses.Mode == TEXT("tdm") && Ses.bRespawn ? 20 : 3;
	for (int32 i = 0; i < 2; i++) { Ses.Caps[i] = Ses.Rounds[i] = 0; Ses.Flag[i] = FHKPbSession::FFlag(); Ses.Flag[i].P = BaseFlag(i); }
	Ses.Round = 1; Ses.EndT = 0; Ses.Winner = -1;
	UE_LOG(LogHaarklous, Display, TEXT("Paintball: game %d (%s%s) started by the admin: %d blue, %d red"), Ses.Id, *Ses.Mode, Ses.bReball ? TEXT(", reball") : TEXT(""), N[0], N[1]);
}

void FHKPaintball::Spawn(bool bFirst)
{
	const int32 T = Ses.TeamOf(You());
	if (T < 0 || !Spawns[T].Num()) return;
	TArray<int32> Ids;
	for (const auto& KV : Ses.Team) if (KV.Value == T) Ids.Add(KV.Key);
	Ids.Sort();
	const FVector S = Spawns[T][FMath::Max(0, Ids.IndexOfByKey(You())) % Spawns[T].Num()];
	const FVector P = World(S.X, Floor, S.Y);
	if (Ctx.Teleport) Ctx.Teleport(P.X, P.Z, WorldYaw(S.Z));
	bAlive = true; RespawnT = 0; Mag = HKPb::Hopper; ReloadT = 0;
	if (bFirst && Ctx.Notice)
		Ctx.Notice(FString::Printf(TEXT("You play for %s. %s"), TeamName[T], Ses.Mode == TEXT("ctf") ? TEXT("Bring their flag to your base.") :
			Ses.bRespawn ? TEXT("Put out as many of them as you can.") : TEXT("Out is out until the round is won.")), 5);
}

void FHKPaintball::ToLobby()
{
	const FVector P = World((GateU0 + GateU1) / 2 + (Rand() - .5) * 10, 0, Rect1.Y + 4 + Rand() * 4);
	if (Ctx.Teleport) Ctx.Teleport(P.X, P.Z, WorldYaw(0));
	bAlive = true; RespawnT = 0;
}

void FHKPaintball::Local(double Dt, const FInput& In)
{
	const FVector F = Ctx.Feet ? Ctx.Feet() : FVector::ZeroVector;
	bInLobby = InLobby(F.X, F.Z);
	const int32 T = Ses.TeamOf(You());
	// Out of a finished game: back to the lobby, still a member (the admin stays the admin).
	if (MySes != 0 && (Ses.Id != MySes || Ses.State == TEXT("lobby") || T < 0)) { MySes = 0; ToLobby(); bInLobby = true; }
	// Joining and leaving the group.
	if (bInLobby && LobbyT == 0)
	{
		LobbyT = FMath::Max(1.0, Now());
		if (Ctx.Notice) Ctx.Notice(TEXT("Paintball lobby. Wait here for the others; the first one here runs the games."), 5);
	}
	if (!bInLobby && !InGame() && LobbyT > 0) { LobbyT = 0; Req = -1; }
	if (bInLobby && Ses.State == TEXT("lobby"))
	{
		if (In.bUp) Req = Req == -1 ? 0 : Req == 0 ? 1 : -1;
		if (In.bDown) Req = Req == -1 ? 1 : Req == 1 ? 0 : -1;
	}
	// Into a new game, out of a finished one.
	if (Ses.Id != 0 && Ses.State != TEXT("lobby") && T >= 0 && MySes != Ses.Id && LeftSes != Ses.Id && LobbyT > 0)
	{
		MySes = Ses.Id; Outs = 0; OutBy = 0; MyRound = Ses.Round;
		Spawn(true);
		SeenState.Reset();
	}
	if (!InGame()) { AimT = 0; return; }
	if (SeenState != Ses.State)
	{
		UE_LOG(LogHaarklous, Display, TEXT("Paintball: game %d is now '%s'"), Ses.Id, *Ses.State);
		if (Ses.State == TEXT("play") && Ctx.Sound) Ctx.Sound(TEXT("whistle"), .8);
		if (Ses.State == TEXT("end") && Ctx.Sound) Ctx.Sound(TEXT("whistle"), .8);
		SeenState = Ses.State;
	}
	if (Ses.Round != MyRound) { MyRound = Ses.Round; Spawn(false); }
	if (!bAlive && RespawnT > 0 && (RespawnT -= Dt) <= 0) Spawn(false);
	// Leave the game: hold △ for 2 s.
	LeaveHold = In.bTriangleHeld ? LeaveHold + Dt : 0;
	if (LeaveHold > 2)
	{
		LeaveHold = 0; LeftSes = MySes; MySes = 0;
		ToLobby();
		bInLobby = true;
		if (Ctx.Notice) Ctx.Notice(TEXT("You left the game."), 3);
		return;
	}
	// The marker.
	CoolT -= Dt; RecoilT = FMath::Max(0.0, RecoilT - Dt);
	if (ReloadT > 0 && (ReloadT -= Dt) <= 0) { ReloadT = 0; Mag = HKPb::Hopper; if (Ctx.Sound) Ctx.Sound(TEXT("click"), .5); }
	const bool bCan = Ses.State == TEXT("play") && bAlive;
	if (In.bReload && bCan && Mag < HKPb::Hopper && ReloadT <= 0) { ReloadT = HKPb::ReloadS; if (Ctx.Sound) Ctx.Sound(TEXT("chains"), .25); }
	AimT += ((In.L2 > .3 && bCan && ReloadT <= 0 ? 1.0 : 0.0) - AimT) * FMath::Min(1.0, Dt * 10);
	if (In.R2 > .55 && !bTrigger)
	{
		bTrigger = true;
		if (bCan && ReloadT <= 0 && CoolT <= 0)
		{
			if (Mag > 0) Fire(Ctx.CameraFwd ? Ctx.CameraFwd() : FVector(0, 0, -1));
			else { if (Ctx.Sound) Ctx.Sound(TEXT("pistolClick"), .6); ReloadT = HKPb::ReloadS; }
		}
	}
	else if (In.R2 < .35) bTrigger = false;
}

void FHKPaintball::TestFire(const FVector& Dir) { Fire(Dir); }

void FHKPaintball::Fire(const FVector& Dir)
{
	// The ball leaves the barrel just under the eye, with the marker's spread (less when aiming down the barrel).
	const double Spread = FMath::DegreesToRadians(AimT > .5 ? .5 : 1.2);
	FVector D = Dir.GetSafeNormal();
	const FVector Side = FVector::CrossProduct(D, FVector(0, 1, 0)).GetSafeNormal(), Up = FVector::CrossProduct(Side, D);
	const double A = Rand() * 2 * PI, R = FMath::Sqrt(-2 * FMath::Loge(FMath::Max(1e-6, Rand()))) * .5 * Spread;
	D = (D + Side * FMath::Cos(A) * R + Up * FMath::Sin(A) * R).GetSafeNormal();
	const FVector Cam = Ctx.CameraPos ? Ctx.CameraPos() : FVector::ZeroVector;
	FHKPbBall B;
	B.Owner = You(); B.Team = FMath::Max(0, MyTeam());
	B.P = Cam + D * .45 - FVector(0, .07, 0);
	B.V = D * HKPb::V0 * (Ses.bReball ? .94 : 1) * (1 + (Rand() - .5) * .04);
	Balls.Add(B);
	Outbox.Add({(double)++Seq, FMath::RoundToDouble(Now()), R3(B.P.X), R3(B.P.Y), R3(B.P.Z), R3(B.V.X), R3(B.V.Y), R3(B.V.Z)});
	Mag--; CoolT = 1 / HKPb::Rate; RecoilT = .08;
	if (Ctx.Sound) Ctx.Sound(TEXT("pbShot"), .7);
}

void FHKPaintball::Out(int32 By, int32 Team)
{
	bAlive = false; Outs++; OutBy = By; HitFlash = 1.5; HitTeam = Team;
	RespawnT = Ses.Mode == TEXT("ctf") ? HKPb::RespawnCtf : Ses.bRespawn ? HKPb::RespawnTdm : 0;
	AimT = 0; ReloadT = 0;
	FString Name = TEXT("someone");
	for (const FHKPbMember& M : All) if (M.Id == By) Name = M.bLocal ? FString(TEXT("yourself")) : M.Name;
	if (Ctx.Sound) { Ctx.Sound(TEXT("pbSplat"), 1); Ctx.Sound(TEXT("hit"), .6); }
	if (Ctx.Notice)
		Ctx.Notice(RespawnT > 0 ? FString::Printf(TEXT("HIT by %s! Out - back in at your base in %.0f s."), *Name, RespawnT)
			: FString::Printf(TEXT("HIT by %s! Out until the next round."), *Name), 4);
}

bool FHKPaintball::Trace(const FVector& A, const FVector& B, double& T, FVector& N, int32& Figure) const
{
	T = 2; Figure = 0;
	// The ground (the yard floor or the park).
	const double Ga = A.Y - (Ctx.Ground ? Ctx.Ground(A.X, A.Z) : Floor), Gb = B.Y - (Ctx.Ground ? Ctx.Ground(B.X, B.Z) : Floor);
	if (Gb < 0 && Ga >= 0) { T = Ga / FMath::Max(1e-9, Ga - Gb); N = FVector(0, 1, 0); }
	// Cover.
	for (const FHKPbBox& X : BoxList)
	{
		const FVector Da = A - X.C, Db = B - X.C;
		if (FMath::Abs(Da.X) > 8 && FMath::Abs(Db.X) > 8 && FMath::Sign(Da.X) == FMath::Sign(Db.X)) continue;
		if (FMath::Abs(Da.Z) > 8 && FMath::Abs(Db.Z) > 8 && FMath::Sign(Da.Z) == FMath::Sign(Db.Z)) continue;
		const double C = FMath::Cos(X.Yaw), S = FMath::Sin(X.Yaw);
		auto ToLocal = [C, S](const FVector& V) { return FVector(V.X * C - V.Z * S, V.Y, V.X * S + V.Z * C); };
		const FVector La = ToLocal(Da), Lb = ToLocal(Db), D = Lb - La;
		double T0 = 0, T1 = 1;
		int32 Axis = -1; double Sign = 0;
		bool bMiss = false;
		for (int32 k = 0; k < 3 && !bMiss; k++)
		{
			const double P = La[k], Dk = D[k], H = X.H[k];
			if (FMath::Abs(Dk) < 1e-12) { if (P < -H || P > H) bMiss = true; continue; }
			double Ta = (-H - P) / Dk, Tb = (H - P) / Dk, Sg = -1;
			if (Ta > Tb) { Swap(Ta, Tb); Sg = 1; }
			if (Ta > T0) { T0 = Ta; Axis = k; Sign = Sg; }
			T1 = FMath::Min(T1, Tb);
			if (T0 > T1) bMiss = true;
		}
		if (bMiss || Axis < 0 || T0 >= T) continue;
		FVector Ln(0, 0, 0); Ln[Axis] = Sign;
		T = T0;
		N = FVector(Ln.X * C + Ln.Z * S, Ln.Y, -Ln.X * S + Ln.Z * C);   // back to the world (the box's yaw)
	}
	// The net catches what crosses it below its top.
	{
		const FVector2D Ca = Course(A.X, A.Z), Cb = Course(B.X, B.Z);
		const bool Ia = FMath::Abs(Ca.X) < PlayHalfU && FMath::Abs(Ca.Y) < PlayHalfV, Ib = FMath::Abs(Cb.X) < PlayHalfU && FMath::Abs(Cb.Y) < PlayHalfV;
		if (Ia != Ib)
		{
			double Tn = 1;
			for (int32 k = 0; k < 2; k++)
			{
				const double H = k == 0 ? PlayHalfU : PlayHalfV, Pa = k == 0 ? Ca.X : Ca.Y, Pb = k == 0 ? Cb.X : Cb.Y;
				for (const double E : {-H, H}) if ((Pa - E) * (Pb - E) < 0) Tn = FMath::Min(Tn, (E - Pa) / (Pb - Pa));
			}
			const double Y = A.Y + (B.Y - A.Y) * Tn;
			if (Tn < T && Y < Floor + 5) { T = Tn; N = FVector(0, 1, 0); Figure = -1; }
		}
	}
	return T <= 1;
}

void FHKPaintball::StepBalls(double Dt)
{
	const double H = 1.0 / 120;
	int32 Steps = FMath::Clamp(FMath::CeilToInt32(Dt / H), 1, 240);
	const double h = Dt / Steps;
	const FVector Cam = Ctx.CameraPos ? Ctx.CameraPos() : FVector::ZeroVector;
	// Bodies balls can hit: ours and the others' who are in this game.
	struct FFig { int32 Id; FVector A, B; };
	TArray<FFig> Figs;
	for (const FHKPbMember& M : All)
	{
		if (M.bLocal ? !(InGame() && bAlive) : !(M.Ses == Ses.Id && Ses.Id != 0 && M.bAlive)) continue;
		FFig F; F.Id = M.Id;
		Body(M.Feet, M.Yaw, M.Stance, F.A, F.B);
		Figs.Add(F);
	}
	for (int32 s = 0; s < Steps; s++)
		for (int32 i = Balls.Num() - 1; i >= 0; i--)
		{
			FHKPbBall& B = Balls[i];
			const FVector A = B.P;
			B.V.Y -= 9.81 * h;
			B.V -= B.V * (HKPb::Drag * B.V.Size() * h);
			const FVector E = A + B.V * h;
			double T; FVector N; int32 Fig;
			bool bHit = Trace(A, E, T, N, Fig);
			// Bodies.
			bool bBody = false;
			int32 Victim = 0;
			for (const FFig& F : Figs)
			{
				if (F.Id == B.Owner && B.Age < .3) continue;
				double S;
				if (SegSeg(A, E, F.A, F.B, S) < HKPb::BodyR && S < T) { T = S; bBody = true; Victim = F.Id; Fig = 0; N = -B.V.GetSafeNormal(); bHit = true; }
			}
			if (!bHit)
			{
				B.P = E; B.Age += h;
				if (B.Age > 3 || B.P.Y < Floor - 25) Balls.RemoveAt(i);
				continue;
			}
			const FVector P = A + (E - A) * T;
			const double Vn = FMath::Abs(FVector::DotProduct(B.V, N));
			const bool bNear = FVector::Dist(P, Cam) < 30;
			if (Fig == -1) { Balls.RemoveAt(i); continue; }   // the net
			if (bBody)
			{
				const bool bBreaks = Ses.bReball || Rand() < BreakChance(B.V.Size() * .9);
				if (Victim == You())
				{
					if (B.Owner != You() || B.Age >= .3)
					{
						if (bBreaks && bAlive && Ses.State == TEXT("play") && InGame()) Out(B.Owner, B.Team);
						else if (!bBreaks && Ctx.Sound) Ctx.Sound(TEXT("pbBounce"), .5);
					}
				}
				else
				{
					if (OnBallHitsFigure) OnBallHitsFigure(B.Owner, Victim);
					if (bBreaks && !Ses.bReball) NewSplats.Add({P, N, B.Team, Victim});
				}
				Balls.RemoveAt(i);
				continue;
			}
			if (!Ses.bReball && Rand() < BreakChance(Vn))
			{
				NewSplats.Add({P + N * .004, N, B.Team, 0});
				if (bNear && Ctx.Sound) Ctx.Sound(TEXT("pbSplat"), FMath::Clamp(1 - FVector::Dist(P, Cam) / 30, .1, .6));
				Balls.RemoveAt(i);
				continue;
			}
			// A bounce: what is left of the speed goes off the surface; it soon drops.
			B.V = (B.V - N * (2 * FVector::DotProduct(B.V, N))) * .3;
			B.P = P + N * .01;
			if (++B.Bounces > 2 || B.V.Size() < 4) Balls.RemoveAt(i);
			if (bNear && Ctx.Sound && B.Bounces <= 1) Ctx.Sound(TEXT("pbBounce"), .3);
		}
}

TSharedPtr<FJsonObject> FHKPaintball::LocalState()
{
	if (LobbyT <= 0 && !InGame()) { Outbox.Reset(); return nullptr; }
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("l"), FMath::RoundToDouble(LobbyT));
	O->SetNumberField(TEXT("q"), Req);
	O->SetNumberField(TEXT("s"), InGame() ? MySes : 0);
	O->SetNumberField(TEXT("a"), bAlive ? 1 : 0);
	O->SetNumberField(TEXT("o"), Outs);
	O->SetNumberField(TEXT("b"), OutBy);
	O->SetNumberField(TEXT("r"), MyRound);
	if (Outbox.Num())
	{
		TArray<TSharedPtr<FJsonValue>> H;
		for (const TArray<double>& S : Outbox) H.Add(NumArr(S));
		O->SetArrayField(TEXT("h"), H);
		Outbox.Reset();
	}
	if (IsAdmin()) O->SetObjectField(TEXT("ses"), Ses.ToJson());
	return O;
}

FVector FHKPaintball::FlagPos(int32 Team, int32& Carrier) const
{
	const FHKPbSession::FFlag& F = Ses.Flag[Team];
	Carrier = F.S == TEXT("carried") ? F.By : -1;   // (0 is a player id offline)
	if (F.S == TEXT("home") || F.P.IsZero()) return BaseFlag(Team);
	return F.P;
}

// ---------------------------------------------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------------------------------------------
void FHKPaintball::DrawHud(UCanvas* C) const
{
	if (!Member() && !InGame()) return;
	const FVector2D Sz = FHKUi::Size(C);
	const double T = Now();
	auto ModeName = [this]() { return Ses.Mode == TEXT("ctf") ? FString(TEXT("Capture the flag")) : Ses.bRespawn ? FString(TEXT("Team deathmatch")) : FString(TEXT("Team deathmatch - rounds")); };
	// Paint on the goggles.
	if (HitFlash > 0)
	{
		const FLinearColor P = HitTeam == 0 ? FLinearColor(.1f, .35f, .95f, .55f * (float)FMath::Min(1.0, HitFlash)) : FLinearColor(.95f, .3f, .05f, .55f * (float)FMath::Min(1.0, HitFlash));
		const float Cx = Sz.X * .5f, Cy = Sz.Y * .45f;
		FHKUi::Disc(C, FVector2D(Cx, Cy), Sz.Y * .16f, P, 40);
		for (int32 k = 0; k < 9; k++) FHKUi::Disc(C, FVector2D(Cx + FMath::Cos(k * 2.1f) * Sz.Y * (.12f + .05f * (k % 3)), Cy + FMath::Sin(k * 2.1f) * Sz.Y * (.1f + .04f * (k % 2))), Sz.Y * (.03f + .012f * (k % 4)), P, 20);
	}
	// Scores and the clock.
	if (InGame() || (Ses.State != TEXT("lobby") && Ses.Id != 0))
	{
		const float W = 360, X = (Sz.X - W) / 2, Y = 16;
		FHKUi::Panel(C, X, Y, W, 64);
		const double Left = Ses.State == TEXT("count") ? (Ses.T0 - T) / 1000 : Ses.State == TEXT("play") ? Ses.Dur - (T - Ses.T0) / 1000 : 0;
		FHKUi::Text(C, FString::FromInt(Ses.Score(0)), X + 40, Y + 10, 36, TeamCol[0], EHKAlign::Center, true);
		FHKUi::Text(C, FString::FromInt(Ses.Score(1)), X + W - 40, Y + 10, 36, TeamCol[1], EHKAlign::Center, true);
		FHKUi::Text(C, FString::Printf(TEXT("%d:%02d"), (int32)FMath::Max(0.0, Left) / 60, (int32)FMath::Max(0.0, Left) % 60), X + W / 2, Y + 6, 26, FHKUi::TextMain, EHKAlign::Center, true);
		FHKUi::Text(C, ModeName() + (Ses.Mode == TEXT("ctf") || !Ses.bRespawn ? FString::Printf(TEXT(" · first to %d"), Ses.Lim) : FString::Printf(TEXT(" · first to %d"), Ses.Lim)), X + W / 2, Y + 38, 15, FHKUi::TextDim, EHKAlign::Center);
		if (Ses.State == TEXT("count")) FHKUi::Text(C, FString::Printf(TEXT("%d"), (int32)FMath::CeilToDouble(FMath::Max(0.0, Left))), Sz.X / 2, Sz.Y * .3f, 90, FHKUi::TextMain, EHKAlign::Center, true);
		if (Ses.State == TEXT("end"))
		{
			const FString R = Ses.Winner < 0 ? TEXT("A draw") : FString::Printf(TEXT("%s team wins"), TeamName[Ses.Winner]);
			FHKUi::Panel(C, Sz.X / 2 - 220, Sz.Y * .3f - 20, 440, 100);
			FHKUi::Text(C, R, Sz.X / 2, Sz.Y * .3f, 40, Ses.Winner < 0 ? FHKUi::TextMain : TeamCol[Ses.Winner], EHKAlign::Center, true);
			FHKUi::Text(C, FString::Printf(TEXT("%d : %d · back to the lobby soon"), Ses.Score(0), Ses.Score(1)), Sz.X / 2, Sz.Y * .3f + 50, 18, FHKUi::TextDim, EHKAlign::Center);
		}
		if (Ses.Mode == TEXT("ctf"))
			for (int32 F = 0; F < 2; F++)
			{
				const FString S = Ses.Flag[F].S == TEXT("home") ? TEXT("home") : Ses.Flag[F].S == TEXT("dropped") ? TEXT("dropped") : TEXT("taken");
				FHKUi::Text(C, FString::Printf(TEXT("%s flag: %s"), TeamName[F], *S), F == 0 ? X + 8 : X + W - 8, Y + 70, 15, TeamCol[F], F == 0 ? EHKAlign::Left : EHKAlign::Right);
			}
	}
	// Our marker.
	if (InGame())
	{
		const int32 Tm = MyTeam();
		FHKUi::Text(C, Mag > 0 ? FString::Printf(TEXT("%d"), Mag) : FString(TEXT("EMPTY")), Sz.X - 28, Sz.Y - 70, 40, FHKUi::TextMain, EHKAlign::Right, true);
		FHKUi::Text(C, ReloadT > 0 ? TEXT("RELOADING") : Ses.bReball ? TEXT("reballs") : TEXT("paintballs"), Sz.X - 28, Sz.Y - 26, 15, FHKUi::TextDim, EHKAlign::Right);
		if (Tm >= 0) FHKUi::Text(C, FString::Printf(TEXT("%s team"), TeamName[Tm]), 24, Sz.Y - 60, 22, TeamCol[Tm], EHKAlign::Left, true);
		FHKUi::Text(C, bAlive ? TEXT("R2 fire · L2 aim · □ reload · hold △ leave") : RespawnT > 0 ? *FString::Printf(TEXT("OUT · back in %.0f s"), RespawnT) : TEXT("OUT · wait for the next round"), 24, Sz.Y - 30, 15, bAlive ? FHKUi::TextDim : FHKUi::Warn);
		if (bAlive && Ses.State == TEXT("play") && AimT < .5)
		{
			const FVector2D M(Sz.X / 2, Sz.Y / 2);
			FHKUi::Circle(C, M, 9, 1.5f, FLinearColor(1, 1, 1, .7f), 20);
		}
	}
	// The lobby.
	if (bInLobby && !InGame())
	{
		const float W = 420, X = Sz.X - W - 20, Y = 110;
		int32 Rows = 0;
		for (const FHKPbMember& M : All) if (M.Lobby > 0) Rows++;
		const float H = 150 + Rows * 24;
		FHKUi::Panel(C, X, Y, W, H);
		FHKUi::Text(C, TEXT("PAINTBALL LOBBY"), X + 18, Y + 12, 22, FHKUi::Accent, EHKAlign::Left, true);
		FHKUi::Text(C, ModeName() + (Ses.bReball ? TEXT(" · reball") : TEXT(" · paint")), X + 18, Y + 42, 17, FHKUi::TextMain);
		FString Status = Ses.State == TEXT("lobby") ? TEXT("Waiting for the admin to start") : TEXT("A game is on: you play in the next one");
		FHKUi::Text(C, Status, X + 18, Y + 66, 15, FHKUi::TextDim);
		float Ry = Y + 94;
		TArray<const FHKPbMember*> L;
		for (const FHKPbMember& M : All) if (M.Lobby > 0) L.Add(&M);
		L.Sort([](const FHKPbMember& A, const FHKPbMember& B) { return A.Lobby < B.Lobby; });
		for (const FHKPbMember* M : L)
		{
			const FString Team = M->Req < 0 ? TEXT("any team") : TeamName[M->Req];
			FHKUi::Text(C, FString::Printf(TEXT("%s%s"), *M->Name, M->Id == AdminId ? TEXT("  (admin)") : TEXT("")), X + 18, Ry, 16, M->bLocal ? FHKUi::TextMain : FHKUi::TextDim);
			FHKUi::Text(C, Team, X + W - 18, Ry, 16, M->Req < 0 ? FHKUi::TextDim : TeamCol[M->Req], EHKAlign::Right);
			Ry += 24;
		}
		const FString Keys = IsAdmin() ? FString::Printf(TEXT("↑↓ team · ←→ game · △ %s · hold ✕ start"), Ses.bReball ? TEXT("paint") : TEXT("reball"))
			: FString(TEXT("↑↓ team · the admin starts the game"));
		FHKUi::Text(C, Keys, X + 18, Y + H - 34, 15, FHKUi::TextDim);
		if (StartHold > 0) FHKUi::Meter(C, X + 18, Y + H - 10, W - 36, 4, (float)StartHold);
	}
	// The feed.
	float Fy = 100;
	for (const FFeedLine& L : FeedLines)
		if (Clock - L.T < 6) { FHKUi::Text(C, L.Text, 24, Fy, 16, FHKUi::TextMain); Fy += 22; }
}
