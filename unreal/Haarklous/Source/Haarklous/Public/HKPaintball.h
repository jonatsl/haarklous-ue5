#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HKCollision.h"

class UCanvas;

/*
 * The urban paintball / reball course (new feature, asked for by the user on 28 Oct 2026; not in the web game).
 * Content/Data/paintball.json (tools/make-paintball.py): a 64 x 48 m walled yard in the park by the construction site,
 * a 60 x 40 m play area inside a net, the lobby on the lawn outside the gate.
 *
 * Lobby: walking into it joins the paintball group; the first one in is the admin (earliest join time, then the lowest
 * player id), who picks the mode (team deathmatch or capture the flag), the ammunition (paint or reball), respawns on
 * or off (team deathmatch only) and starts a game for everyone in the lobby, up to 6 against 6. Players can ask for a
 * team; the admin balances the rest. Somebody who comes later waits in the lobby for the next game.
 *
 * Rules (normal paintball): a ball that breaks on you (or on your marker) puts you out, also from your own team; a ball
 * that bounces does not (reball balls count whenever they hit). Semi-automatic markers, at most 10 balls a second,
 * 90 m/s at the muzzle, 200 balls in the hopper and pods to reload from.
 *   Team deathmatch: with respawns, back in at your base after 5 s; the first team to 20 eliminations (or the most
 *   after 10 minutes) wins. Without respawns, rounds: out is out until one team is left; first to 3 rounds.
 *   Capture the flag: take the other team's flag from their base to yours; a carrier who is hit drops it, the owners
 *   send it home by touching it (or it goes home after 30 s); back in after 8 s; first to 3 captures (or 10 minutes).
 *
 * Online: everything travels in the players' own state messages ('st', field 'pb'), which the lobby server relays
 * unchanged and web players ignore: each player's lobby time, team wish, game and life, eliminations suffered and the
 * balls it fired; the admin also sends the game (mode, teams, time, scores, flags). Each player decides whether a ball
 * hit him (the victim's own view, so lag never puts anybody out unseen); the admin keeps the score and the flags.
 * Offline it all works the same with the player alone (practice).
 */
namespace HKPb
{
	constexpr double V0 = 90;              // m/s at the muzzle (field limit 300 ft/s)
	constexpr double Drag = .022;          // 1/m: a .68 calibre ball loses half its speed in about 30 m
	constexpr double Rate = 10;            // balls a second at most
	constexpr int32 Hopper = 200;          // balls in the hopper; a pod refills it
	constexpr double ReloadS = 2.4;
	constexpr int32 TeamMax = 6;
	constexpr double CountS = 5, EndS = 10;
	constexpr double RespawnTdm = 5, RespawnCtf = 8, FlagHomeS = 30;
	constexpr double BodyR = .25;          // hit radius round the body (with the marker and the gear)
}

struct FHKPbBox { FVector C = FVector::ZeroVector; FVector H = FVector::ZeroVector; double Yaw = 0; };   // centre, half sizes, yaw (web)
struct FHKPbBall { int32 Owner = 0; int32 Team = 0; FVector P = FVector::ZeroVector, V = FVector::ZeroVector; double Age = 0; int32 Bounces = 0; };
struct FHKPbSplat { FVector P = FVector::ZeroVector, N = FVector::UpVector; int32 Team = 0; int32 OnFigure = 0; };

/* A member of the paintball group (in the lobby or in a game), as its state says. */
struct HAARKLOUS_API FHKPbMember
{
	int32 Id = 0;
	FString Name;
	int32 Slot = 0;
	bool bLocal = false;
	double Lobby = 0;          // lobby clock (ms) when it joined; 0 = not a member
	int32 Req = -1;            // team asked for: -1 any, 0 blue, 1 red
	int32 Ses = 0;             // the game it plays in (0 = none)
	bool bAlive = true;
	int32 Outs = 0, By = 0;    // times put out in that game, and by whom the last time
	int32 Round = 0;
	FVector Feet = FVector::ZeroVector;
	double Yaw = 0;
	FString Stance = TEXT("stand");
	TArray<TArray<double>> Shots;   // [seq, t, ox, oy, oz, vx, vy, vz]
	TSharedPtr<FJsonObject> Ses_;   // the game, when it is the admin
	static bool FromJson(const TSharedPtr<FJsonObject>& O, FHKPbMember& Out);
};

/* A game as the admin runs it. */
struct HAARKLOUS_API FHKPbSession
{
	int32 Id = 0;
	FString Mode = TEXT("tdm");   // tdm | ctf
	bool bReball = false;
	bool bRespawn = true;
	FString State = TEXT("lobby");   // lobby | count | play | end
	double T0 = 0;                // lobby clock (ms) when play starts (the countdown before it)
	double Dur = 600;             // s
	int32 Lim = 20;               // eliminations (tdm with respawns), rounds (without) or captures (ctf)
	TMap<int32, int32> Team;      // player id -> 0 blue, 1 red
	TMap<int32, int32> Outs;      // player id -> times put out (kept when a player leaves)
	struct FFlag { FString S = TEXT("home"); int32 By = 0; FVector P = FVector::ZeroVector; double T = 0; } Flag[2];
	int32 Caps[2] = {0, 0}, Rounds[2] = {0, 0};
	int32 Round = 1;
	double EndT = 0;
	int32 Winner = -1;            // -1 none / a draw
	TSharedPtr<FJsonObject> ToJson() const;
	static bool FromJson(const TSharedPtr<FJsonObject>& O, FHKPbSession& Out);
	int32 TeamOf(int32 Player) const { const int32* T = Team.Find(Player); return T ? *T : -1; }
	int32 Score(int32 T) const;   // what decides the game for team T in the current mode
};

struct FHKPaintballContext
{
	TFunction<double()> ServerNow;                       // the lobby's clock (ms); offline the local clock
	TFunction<int32()> You;                              // our player id (0 offline)
	TFunction<FString()> YourName;
	TFunction<int32()> YourSlot;
	TFunction<void(TArray<FHKPbMember>&)> Others;        // the other players with their 'pb' states and positions
	TFunction<FVector()> Feet;                           // player feet (web frame)
	TFunction<double()> Yaw;
	TFunction<FString()> Stance;
	TFunction<void(double X, double Z, double Yaw)> Teleport;
	TFunction<FVector()> CameraPos;
	TFunction<FVector()> CameraFwd;
	TFunction<double(double X, double Z)> Ground;        // FHKData::GroundWorld
	TFunction<void(const FString&, double)> Notice;
	TFunction<void(const FString& /*Sfx*/, double /*Gain*/)> Sound;
	TFunction<double()> Random;
};

class HAARKLOUS_API FHKPaintball
{
public:
	bool Load(const FString& Path);
	FHKPaintballContext Ctx;

	// ---- the course (web frame) ----
	FVector2D Centre = FVector2D::ZeroVector;   // play area centre (world x, z)
	double Theta = 0, Floor = 0;                // map rotation; yard floor height
	double PlayHalfU = 30, PlayHalfV = 20;
	FVector2D Course(double X, double Z) const;                 // world -> course (u, v)
	FVector World(double U, double Y, double V) const;          // course -> world
	double WorldYaw(double CourseYaw) const { return CourseYaw + Theta; }
	bool InYard(double X, double Z, double Margin = 0) const;
	bool Ground(double X, double Z, double& OutY) const;        // the yard floor
	bool InLobby(double X, double Z) const;
	bool InPlay(double X, double Z, double Margin = 0) const;
	const TArray<FHKObstacle>& Obstacles() const { return Obst; }
	const TArray<FHKPbBox>& Boxes() const { return BoxList; }
	struct FLobbySpot { FString Kind; double X = 0, Z = 0, Yaw = 0; };
	TArray<FLobbySpot> LobbyProps;                               // shelter, benches, rack, board, sign (world)
	FVector BaseFlag(int32 Team) const;                          // flag stand (world, on the floor)
	FVector RootPos() const { return World(0, 0, 0); }

	// ---- per frame ----
	struct FInput
	{
		double R2 = 0, L2 = 0;
		bool bReload = false, bUp = false, bDown = false, bLeft = false, bRight = false, bTriangle = false, bCross = false;
		bool bCrossHeld = false, bTriangleHeld = false;
	};
	void Update(double Dt, const FInput& In);
	TSharedPtr<FJsonObject> LocalState();   // 'pb' for our next 'st' (takes the balls fired since the last one)

	// ---- what the game and the visuals need ----
	bool Member() const { return LobbyT > 0; }
	bool InLobbyNow() const { return bInLobby; }
	bool InGame() const;                    // in a game (countdown, play or the result)
	bool Armed() const;                     // carrying the marker (in a game)
	bool Frozen() const;                    // the countdown: stand still
	bool Alive() const { return bAlive; }
	bool Aiming() const { return AimT > .5; }
	double AimAmount() const { return AimT; }
	bool IsAdmin() const { return AdminId == You(); }
	int32 MyTeam() const;
	int32 Ammo() const { return Mag; }
	double ReloadLeft() const { return ReloadT; }
	double Recoil() const { return RecoilT; }
	const FHKPbSession& Session() const { return Ses; }
	const TArray<FHKPbMember>& Members() const { return All; }
	int32 TeamOfMember(int32 Id) const;      // in the current game (-1 none)
	bool MemberPlaying(int32 Id) const;      // in the current game and in its state
	FVector FlagPos(int32 Team, int32& Carrier) const;   // Carrier -1: not carried
	TArray<FHKPbBall> Balls;
	TArray<FHKPbSplat> NewSplats;           // the visuals take them
	TArray<int32> NewFired;                 // teams of balls fired by others this frame (sound)
	double HitFlash = 0;                    // seconds of paint on our goggles (HUD)
	int32 HitTeam = 0;
	void DrawHud(UCanvas* C) const;

	// ---- tests ----
	TFunction<void(int32 /*Owner*/, int32 /*Victim*/)> OnBallHitsFigure;   // a ball reached another player's body
	void ForceAdminOptions(const FString& Mode, bool bReball, bool bRespawn) { if (IsAdmin()) { Ses.Mode = Mode; Ses.bReball = bReball; Ses.bRespawn = bRespawn; } }
	void TestFire(const FVector& Dir);       // fire a ball now along Dir (ignores the trigger)
	void TestSession(TFunction<void(FHKPbSession&)> Fn) { if (IsAdmin()) Fn(Ses); }
	FVector SpawnPoint(int32 Team, int32 Index, double& OutYaw) const;   // world feet and yaw of a team's spawn point

private:
	int32 You() const { return Ctx.You ? Ctx.You() : 0; }
	double Now() const;
	double Rand() const;
	void ReadMembers();
	void RunAdmin(double Dt, const FInput& In);
	void StartGame();
	void Local(double Dt, const FInput& In);
	void Fire(const FVector& Dir);
	void Spawn(bool bFirst);
	void ToLobby();
	void StepBalls(double Dt);
	bool Trace(const FVector& A, const FVector& B, double& T, FVector& N, int32& Figure) const;
	void Out(int32 By, int32 Team);
	void Feed(const FString& Text);

	// course
	FVector2D Rect0, Rect1, Lobby0, Lobby1;     // course coordinates
	double GateU0 = 0, GateU1 = 0;
	TArray<FHKPbBox> BoxList;
	TArray<FHKObstacle> Obst;
	FVector2D Flags[2];
	TArray<FVector> Spawns[2];                 // u, v, course yaw

	// group and game
	TArray<FHKPbMember> All;                   // everyone (us first)
	int32 AdminId = 0;
	FHKPbSession Ses;
	bool bInLobby = false;
	double LobbyT = 0;
	int32 Req = -1;
	int32 MySes = 0;                           // the game we play in
	int32 LeftSes = 0;                         // a game we left (we do not go back into it)
	int32 MyRound = 0;
	FString SeenState;
	int32 SeenSes = 0;
	bool bAlive = true;
	int32 Outs = 0, OutBy = 0;
	double RespawnT = 0;
	int32 Mag = HKPb::Hopper;
	double ReloadT = 0, CoolT = 0, AimT = 0, RecoilT = 0, StartHold = 0, LeaveHold = 0;
	bool bTrigger = false;
	int32 Seq = 0;
	TArray<TArray<double>> Outbox;             // our balls not sent yet
	TMap<int32, int32> SeenShot;               // owner -> last ball seq
	TMap<int32, int32> SeenOuts;               // member -> outs (for the feed)
	struct FFeedLine { FString Text; double T = 0; };
	TArray<FFeedLine> FeedLines;
	double Clock = 0;
};
