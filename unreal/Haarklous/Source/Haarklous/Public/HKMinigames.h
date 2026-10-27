#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HKAudio.h"
#include "HKData.h"
#include "HKNode.h"

class UCanvas;

/*
 * Port of game-minigames.js: disc golf (two par-3 holes of about 70 m in Torshovdalen) and petanque against Arne,
 * who always waits at the court. Web space (metres, y up), every constant, formula and branch as in the JavaScript.
 *
 * The main loop (game-main.js) uses each game like this:
 *   prompt:  Game.Near(FVector(player.x, player.feet, player.z))         -> "□ Play ..." or unset
 *   start:   Pose = Game.Start(same)  -> place(Pose.X, Pose.Z, Pose.Yaw); player.mode = 'minigame'
 *   frame:   FHKMiniInput In; In.AimYaw/AimPitch/bAlt/Hyzer/bCancel/Drunk = ...; Game.TakeCharge(Dt, R2 > .5, In);
 *            R = Game.Update(Dt, In) -> R.bTeleport (place), R.bStand, R.bFace (yaw = Face, pitch = -.12), R.bDone
 *            (notice(Done, 6), back to foot), R.bCamera (camera at CameraPos looking at CameraLookAt), else first person
 *   HUD:     Game.DrawHud(Canvas) every frame (draws nothing while the game's HUD is hidden)
 *   visuals: Game.Rig (static parts in the root's mesh; the disc, Arne and the balls are nodes moved by the code),
 *            shown by an AHKRigActor (rig name "discgolf" / "petanque").
 * Math.random and performance.now() come from the context, so tests can use the same seeded generator as the recording.
 */

// The input object game-main.js passes to update(): {aimYaw, aimPitch, charge, power, release, alt, hyzer, cancel, drunk}
struct FHKMiniInput
{
	double AimYaw = 0, AimPitch = 0;
	double Charge = 0, Power = 0;
	bool bRelease = false, bAlt = false, bCancel = false;
	double Hyzer = 0, Drunk = 0;
};

struct FHKMiniPose
{
	double X = 0, Z = 0, Yaw = 0;
};

// What update() returns: null (game not active) or {teleport, stand, face, done, camera: {x, y, z, lookAt}, lock}.
struct FHKMiniResult
{
	bool bNull = true;
	bool bTeleport = false;
	FHKMiniPose Teleport;
	bool bStand = false;
	FVector Stand = FVector::ZeroVector;          // (x, y, z), y = feet
	bool bFace = false;
	double Face = 0;
	bool bDone = false;
	FString Done;
	bool bCamera = false;
	FVector CameraPos = FVector::ZeroVector, CameraLookAt = FVector::ZeroVector;
	bool bLock = false;
};

// The create({...}) arguments: heightAt, toWorld, data, trees, audio; plus Math.random and performance.now.
struct HAARKLOUS_API FHKMinigamesContext
{
	TFunction<double(double, double)> HeightAt;            // world.heightWorld(x, z)
	TFunction<FVector2D(double, double)> ToWorld;          // world.toWorld(x, s) -> (x, z)
	TFunction<void(const FString&, const FHKSfxOpts&)> Sfx; // audio.sfx(name, {gain})
	TFunction<double()> Random;                            // Math.random(): [0, 1)
	TFunction<double()> NowMs;                             // performance.now()
	TSharedPtr<FJsonObject> Data;                          // surroundings data D (discgolf, petanque)
	TArray<FHKData::FTree> Trees;                          // world.trees (local frame x, s, h)
	FString RigDir;                                        // folder of discgolf.json / petanque.json
	// Everything from the running game: FHKData::Get(), FHKAudio::Get(), a random stream, FPlatformTime.
	static FHKMinigamesContext FromGame();
};

class HAARKLOUS_API FHKMinigame
{
public:
	explicit FHKMinigame(const FHKMinigamesContext& InCtx) : Ctx(InCtx) {}
	virtual ~FHKMinigame() {}
	FHKMinigame(const FHKMinigame&) = delete;
	FHKMinigame& operator=(const FHKMinigame&) = delete;

	FHKRig Rig;                 // the JS group
	bool bActive = false;
	double Charge = 0;          // g.charge: kept by the main loop while R2 / left mouse is held
	FString Message;

	virtual FString Name() const = 0;                              // 'discgolf' / 'petanque' (music track)
	virtual TOptional<FString> Near(const FVector& P) const = 0;    // P = (x, feet, z)
	virtual TOptional<FHKMiniPose> Start(const FVector& P) = 0;
	virtual FHKMiniResult Update(double Dt, const FHKMiniInput& In) = 0;
	virtual void Stop() = 0;
	virtual void DrawHud(UCanvas* C) const = 0;
	virtual bool HudVisible() const = 0;

	// game-main.js updateMinigame(): holding R2 charges for 1.2 s, letting go releases with that power.
	void TakeCharge(double Dt, bool bHolding, FHKMiniInput& In);

protected:
	FHKMinigamesContext Ctx;
	double Gauss() const;
	double Rand() const { return Ctx.Random(); }
	void Sfx(const FString& Name, double Gain = 1) const { if (Ctx.Sfx) Ctx.Sfx(Name, FHKSfxOpts(Gain)); }
	void LoadRig(const FString& RigName);
};

/* ======================= DISC GOLF ======================= */
class HAARKLOUS_API FHKDiscGolf : public FHKMinigame
{
public:
	explicit FHKDiscGolf(const FHKMinigamesContext& InCtx);

	struct FHole
	{
		int32 N = 0;
		double Par = 3;
		FVector Tee = FVector::ZeroVector, Basket = FVector::ZeroVector;   // (x, y, z)
		double Length = 0;
	};
	struct FDisc
	{
		FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector;
		double Bank = 0, Time = 0;
		bool bFlying = false, bRest = false, bPutt = false;
	};
	struct FPending
	{
		bool bNext = false, bLie = false;
		double T = 0;
	};
	// hud.discgolf(state)
	struct FHud
	{
		int32 Hole = 0, Holes = 0;
		double Par = 3;
		TArray<int32> Strokes;
		double Distance = 0, Charge = 0;
		FString Message;
	};

	TArray<FHole> Holes;
	int32 Hole = 0;
	TArray<int32> Strokes;
	bool bHasLie = false;
	FVector Lie = FVector::ZeroVector;          // (x, y, z)
	FDisc Disc;
	bool bHasPending = false;
	FPending Pending;
	double FollowT = 0;
	FHKNode* DiscNode = nullptr;                // the disc mesh
	bool bHasHud = false;
	FHud Hud;                                   // last hud.discgolf(...) (bHasHud false = hidden)

	virtual FString Name() const override { return TEXT("discgolf"); }
	virtual TOptional<FString> Near(const FVector& P) const override;
	virtual TOptional<FHKMiniPose> Start(const FVector& P) override;
	virtual FHKMiniResult Update(double Dt, const FHKMiniInput& In) override;
	virtual void Stop() override;
	virtual void DrawHud(UCanvas* C) const override;
	virtual bool HudVisible() const override { return bHasHud; }
	FHud State(double InCharge = 0) const;

private:
	struct FTreeW { double X, Z, H; };
	TArray<FTreeW> TreeWorld;
	void ThrowDisc(const FHKMiniInput& In);
	int32 Step(double Dt);   // 0 = null, 1 = 'in', 2 = 'rest'
	void SetHud(const FHud& S) { Hud = S; bHasHud = true; }
};

/* ======================= PETANQUE ======================= */
enum class EHKSide : uint8 { None, You, Npc };
enum class EHKBallOwner : uint8 { Jack, You, Npc };

class HAARKLOUS_API FHKPetanque : public FHKMinigame
{
public:
	explicit FHKPetanque(const FHKMinigamesContext& InCtx);

	enum class EPhase : uint8 { Jack, First, Play };
	enum class EAim : uint8 { None, Jack, Boule };
	struct FFlight
	{
		double FromU = 0, FromV = 0, FromY = 1;
		double ToU = 0, ToV = 0;
		double T = 0, Total = 1, Peak = 1;
		double RollU = 0, RollV = 0;
	};
	struct FBall
	{
		EHKBallOwner Owner = EHKBallOwner::Jack;
		double R = .037;
		FHKNode* Mesh = nullptr;
		double U = 0, V = 0, VU = 0, VV = 0, Y = 0;
		bool bAlive = false, bDead = false;
		bool bFlight = false;
		FFlight Flight;
	};
	struct FMarker
	{
		FVector Pos = FVector::ZeroVector;
		FString Label;
		EHKBallOwner Owner = EHKBallOwner::Jack;
		bool bBest = false;
	};
	// hud.petanque(state)
	struct FHud
	{
		int32 ScoreYou = 0, ScoreNpc = 0, Mene = 0, You = 0, Npc = 0;
		EHKSide Turn = EHKSide::None;
		EAim Aim = EAim::None;
		EHKSide Hold = EHKSide::None;
		double Margin = 0, Charge = 0;
		FString Message, NpcName;
		TArray<FMarker> Markers;
		bool bShoot = false;
	};

	// Court frame (constant).
	FVector2D Centre = FVector2D::ZeroVector, Along = FVector2D::ZeroVector, Across = FVector2D::ZeroVector;
	double L = 15, W = 4, BaseY = 0, Ang = 0;
	FVector NpcHome = FVector::ZeroVector;
	FHKNode* Npc = nullptr;
	FHKNode* ArmL = nullptr;
	FHKNode* ArmR = nullptr;

	// Game state (game.* and the module's let variables).
	int32 ScoreYou = 0, ScoreNpc = 0, Mene = 0;
	FString NpcName = TEXT("Arne");
	bool bHasBalls = false;          // false until the first mène (jack/boules undefined)
	TArray<FBall> Balls;             // [jack, you x3, npc x3]
	EHKSide Thrower = EHKSide::None;
	EPhase Phase = EPhase::Jack;
	bool bHasCircle = false;
	FVector2D Circle = FVector2D::ZeroVector;    // (u, v)
	double AiTimer = 0, Waiting = 0, NpcThrowAnim = 0;
	EHKSide Current = EHKSide::None;
	EAim AimingFor = EAim::None;
	bool bHasFinish = false;
	double Finish = 0;
	EHKSide NextFirst = EHKSide::None;
	int32 TurnId = 0;                // game.turnId (undefined counts as 0)
	bool bHasFaced = false;
	int32 Faced = 0;
	bool bHasHud = false;
	FHud Hud;

	virtual FString Name() const override { return TEXT("petanque"); }
	virtual TOptional<FString> Near(const FVector& P) const override;
	virtual TOptional<FHKMiniPose> Start(const FVector& P) override;
	virtual FHKMiniResult Update(double Dt, const FHKMiniInput& In) override;
	virtual void Stop() override;
	virtual void DrawHud(UCanvas* C) const override;
	virtual bool HudVisible() const override { return bHasHud; }
	// game.debug(): {jack, boules, circle, phase, current, aimingFor}
	TSharedPtr<FJsonObject> Debug() const;

	FVector2D CourtPoint(double U, double V) const { return FVector2D(Centre.X + Along.X * U + Across.X * V, Centre.Y + Along.Y * U + Across.Y * V); }
	FVector2D ToCourt(double X, double Z) const;
	FBall& Jack() { return Balls[0]; }
	const FBall& Jack() const { return Balls[0]; }
	static FString SideName(EHKSide S) { return S == EHKSide::You ? TEXT("you") : S == EHKSide::Npc ? TEXT("npc") : FString(); }

private:
	TArray<FHKNode*> BallNodes;     // 7 nodes made from the templates (jack, boule_you, boule_npc)
	void NewMene(EHKSide First);
	void Launch(FBall& Ball, double DirU, double DirV, double Distance, bool bShoot, double Drunk = 0);
	bool Physics(double Dt);
	double Dist(const FBall& B) const;
	double Best(EHKSide Owner) const;
	EHKSide Holding() const;
	int32 Left(EHKSide Owner) const;
	bool NextThrower(EHKSide& Out);   // false: the web code would throw (left(null))
	EHKSide EndMene();
	void NpcTurn();
	int32 FirstFree(EHKSide Owner) const;   // boules[owner].find(b => !b.alive && !b.dead), -1 if none
	static int32 BallIndex(EHKSide Owner, int32 K) { return Owner == EHKSide::You ? 1 + K : 4 + K; }
	void SetHud(const FHud& S) { Hud = S; bHasHud = true; }
};

/* create({...}) -> {discgolf, petanque} */
struct HAARKLOUS_API FHKMinigames
{
	explicit FHKMinigames(const FHKMinigamesContext& Ctx) : DiscGolf(Ctx), Petanque(Ctx) {}
	FHKDiscGolf DiscGolf;
	FHKPetanque Petanque;
};

namespace HKJs
{
	// Math.hypot exactly as V8 computes it (scaled, Kahan-compensated sum).
	HAARKLOUS_API double Hypot(double A, double B);
	// Number.prototype.toFixed and String(number) for the messages.
	HAARKLOUS_API FString ToFixed(double V, int32 Digits);
	HAARKLOUS_API FString Num(double V);
}
