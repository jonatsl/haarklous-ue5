#pragma once

#include "CoreMinimal.h"
#include "HKCollision.h"
#include "HKMath.h"
#include "HKNode.h"
#include "HKViewmodel.h"
#include <limits>

class UCanvas;
class USceneComponent;
struct FHKInputFrame;

/*
 * The hostage-rescue mission: port of the web's case-game.js, line by line (same constants, same order of operations
 * and of random numbers), in web space (metres, y up). The hostage is in the 4th-floor apartment, 3-5 hostiles are split
 * between the stairwell and the apartment; they turn toward the player when they see him, wait a reaction time and shoot
 * back; four hits put the player down.
 *
 * Pure helpers (namespace HKCase) are the web module's exports: createRng, planCase, applyHit, hostileHitChance,
 * damagePlayer, outcome, insidePolygon. FHKMission is mount(ctx): everything it needs from the main loop comes through
 * FHKMissionContext (the web's ctx object), filled in by AHKGame.
 *
 * How AHKGame drives it (game-main.js):
 *   - the prompt "□ Start mission: rescue the hostage (4th floor)" near the marker (4.8, 14.2) calls Start(false);
 *   - every frame, after reading input and before the player update: RouteInput(Frame) (lines 408-414 and the canvas
 *     mouse handlers); when it returns true the mission was left: player.mode = 'foot';
 *   - while IsActive(): the 'options' pause menu, the map (touchpad), photo camera and gear (triangle), running on R2 and
 *     □ interact stay off; ✕ / ○ / L3 work only when GetState() == "play"; move speed x MoveScale(), pad look x
 *     LookScale(), no movement unless CanMove(); the field of view is the mission's (Ctx.SetFov); the prompt only lists
 *     doors (findPrompt's mission branch) and is hidden unless playing; the dynamic walk obstacles get ActorObstacles()
 *     every frame (updateDynamic); toggling a door refuses when its sweep overlaps ActorObstacles(); music 'mission';
 *     the world crosshair is hidden and the prompt sits at 24 % from the bottom (style.css #game[data-case]);
 *   - after its own frame update: Tick(real frame seconds), every frame (it does nothing while off);
 *   - the HUD: DrawHud(Canvas) after the main HUD (the panels cover it); HandleClick(ui) for a mouse cursor if shown;
 *   - visuals: spawn one AHKMissionFx and Bind(&Mission, GameActor): figures, pistol and hands, flash lights, bullet holes.
 */
namespace HKCase
{
	constexpr int32 MAGAZINE = 15;
	constexpr int32 RESERVE = 45;
	constexpr double RELOAD_SECONDS = 1.45;
	constexpr int32 HOSTILE_HEALTH = 2;
	constexpr int32 PLAYER_HEALTH = 100;
	constexpr int32 HOSTILE_DAMAGE = 25;
	constexpr double HOSTILE_REACTION_MIN = .75, HOSTILE_REACTION_MAX = 1.2;
	constexpr double HOSTILE_INTERVAL_MIN = 1.0, HOSTILE_INTERVAL_MAX = 1.6;

	// createRng(seed): the web's mulberry32 generator, values in [0, 1).
	struct HAARKLOUS_API FRng
	{
		uint32 A;
		explicit FRng(uint32 Seed) : A(Seed ? Seed : 1u) {}
		double Next();
		double operator()() { return Next(); }
	};

	struct FSpot
	{
		double X = 0, Z = 0, Y = 0;
		FString Area;
	};
	// One planned actor: {id, role ('hostage' | 'hostile'), zone ('apartment' | 'stair'), x, z, y, area, facing, health, alive}
	struct FPlanActor
	{
		FString Id, Role, Zone, Area;
		double X = 0, Z = 0, Y = 0, Facing = 0;
		int32 Health = 1;
		bool bAlive = true;
	};
	struct FPlan
	{
		int32 HostileCount = 0;
		bool bHasHostage = true;
		TArray<FPlanActor> Actors;
	};
	struct FPlanOptions
	{
		double MinSeparation = 1.1;
		int32 HostilesMin = 3, HostilesMax = 5;
		int32 MaxTries = 600;
	};
	// sampler(rng, zone) -> a free spot or null (false).
	using FSampler = TFunctionRef<bool(FRng& Rng, const FString& Zone, FSpot& Out)>;
	// planCase(rng, sampler, options). Returns false (with the web's error text) where the web code throws.
	HAARKLOUS_API bool PlanCase(FRng& Rng, FSampler Sampler, FPlan& Out, FString* Error = nullptr, const FPlanOptions& Options = FPlanOptions());
	// applyHit(actor, head): hostiles take 2 body hits or 1 head hit.
	HAARKLOUS_API void ApplyHit(int32& Health, bool& bAlive, bool bHead);
	// hostileHitChance(distance, playerSpeed, stance): 0..1 chance that one hostile shot hits the player.
	HAARKLOUS_API double HostileHitChance(double Distance, double PlayerSpeed = 0, const FString& Stance = TEXT("stand"));
	HAARKLOUS_API void DamagePlayer(int32& Health, bool& bAlive, int32 Amount = HOSTILE_DAMAGE);
	// outcome(actors, player): "fail" (a hostage down or the player down), "success" (all hostiles down) or "" (null).
	struct FRoleAlive { FString Role; bool bAlive = true; };
	HAARKLOUS_API FString Outcome(const TArray<FRoleAlive>& Actors, const bool* bPlayerAlive = nullptr);
	HAARKLOUS_API bool InsidePolygon(double X, double Z, const TArray<FHKPt>& Points);
	HAARKLOUS_API double PolygonArea(const TArray<FHKPt>& Points);

	// JavaScript number helpers the port needs to give the same answers as V8.
	HAARKLOUS_API double JsHypot(double A, double B);
	HAARKLOUS_API double JsHypot(double A, double B, double C);
	HAARKLOUS_API FString JsToFixed(double V, int32 Digits);       // Number.prototype.toFixed (Digits 0..3)
	HAARKLOUS_API FString Clock(double T);                         // case-game.js clock(t): "mm:ss.s"
}

// ctx.spawnAreas(): the apartment's rooms and balcony where actors may stand.
struct FHKSpawnArea
{
	FString Name;
	TArray<FHKPt> Points;
	double CeilingHeight = std::numeric_limits<double>::infinity();
};
// ctx.stairSpawns(): flat landings of the own stairwell (game-main.js stairSpawns()).
struct FHKStairSpawn
{
	FString Name;
	TArray<FHKPt> Points;
	double Y = 0;
};
// One apartment door (ctx.doors()), closed pose: enough to sweep its leaf like makeSampler() does.
struct FHKMissionDoor
{
	FString Name, Motion;              // motion 'swing' or 'slide'
	double OpenAngle = 0, ClosedX = 0, Travel = 0;
	double LeafWidth = 0;              // d.leaf.geometry.parameters.width
	FHKMat ParentWorld;                // hinge.parent.matrixWorld
	FVector HingePos = FVector::ZeroVector;
	FQuat HingeQuat = FQuat::Identity;
	FVector HingeScale = FVector::OneVector;
	FHKMat LeafLocal;                  // leaf pose in the hinge frame
};
// ctx.missionStart(): where the mission starts (outside the street door), facing the door.
struct FHKMissionStart
{
	double X = .25, Z = 5.83, Y = 0, Yaw = -HK::Pi / 2;
};
// Answer of ctx.Shoot(): the nearest hit on the world geometry the web mission shoots at (apartment and own stairwell).
struct FHKShotHit
{
	double Distance = 0;
	FVector Point = FVector::ZeroVector;     // web space
	FVector Normal = FVector(0, 1, 0);       // web space, world
	TWeakObjectPtr<USceneComponent> Component;   // what was hit (bullet holes stick to it, e.g. a door leaf)
};

/* The mission's fixed data from Content/Data/game.json (the web export), for AHKGame to put into the context. */
namespace HKMissionData
{
	HAARKLOUS_API TArray<FHKSpawnArea> SpawnAreas();      // apt.spawnAreas
	// game-main.js stairSpawns(): the own stairwell's flat floors except the street door threshold and half landing 0.5.
	HAARKLOUS_API TArray<FHKStairSpawn> StairSpawns();
	HAARKLOUS_API FVector Entrance();                     // the entrance door leaf's world position
	HAARKLOUS_API TArray<FHKMissionDoor> Doors();         // apt.doors (closed pose)
	// game-main.js missionStart(): D.vehicles.missionStart on the terrain, facing the street door of 17A.
	HAARKLOUS_API FHKMissionStart MissionStart();
}

/* Everything the mission needs from the main loop (the web's ctx in game-main.js lines 198-207). */
struct FHKMissionContext
{
	// Doors and walk collision.
	TFunction<void()> CloseAllDoors;                    // closeAllDoors() (also rebuilds the dynamic list)
	TFunction<void()> RebuildWalkWorld;                 // updateDynamic(): doors + vehicles + ActorObstacles() while active
	TFunction<FHKWalkWorld*()> WalkWorld;               // the player's walk world
	TFunction<TArray<FHKMissionDoor>()> Doors;          // apt.doors
	TFunction<TArray<FHKSpawnArea>()> SpawnAreas;       // apt.spawnAreas
	TFunction<TArray<FHKStairSpawn>()> StairSpawns;     // stairSpawns()
	TFunction<FHKMissionStart()> MissionStart;          // missionStart()
	TFunction<FVector()> Entrance;                      // world position of the entrance door leaf
	// Player and camera (web space).
	TFunction<FVector()> CameraPosition;                // camera.position
	TFunction<FQuat()> CameraQuaternion;                // camera.quaternion
	TFunction<double()> CameraAspect;                   // camera.aspect (width / height)
	TFunction<double()> GetFov;                         // camera.fov (vertical, degrees)
	TFunction<void(double)> SetFov;                     // camera.fov = v; the main loop leaves it alone while the mission runs
	TFunction<FString()> Stance;                        // player.stance: 'stand', 'crouch', 'prone'
	TFunction<void(double X, double Z, double Yaw, double Tilt, double Y)> SetPose;   // place(x, z, yaw, y)
	TFunction<bool()> UseDoorAhead;                     // interact(true): opens / closes a door in reach
	TFunction<void()> Leave;                            // after stop(): keep the player where he is, notice 'Mission over'
	// Look sensitivity (ctx.sensitivity): get(kind), set(kind, value), step(kind, +-1); kind 'pad' or 'mouse'.
	TFunction<double(const FString&)> GetSensitivity;
	TFunction<void(const FString&, double)> SetSensitivity;
	TFunction<void(const FString&, int32)> StepSensitivity;
	double SensitivityMin = .25, SensitivityMax = 3;
	// The world geometry the mission shoots at and looks through: apartment and own-stairwell meshes that are visible
	// and opaque (game-main.js shootTargets); the nearest hit within MaxDist, false if none. Unreal line trace.
	TFunction<bool(const FVector& Origin, const FVector& Dir, double MaxDist, FHKShotHit& Hit)> Shoot;
	// Math.random(); left empty: FMath::FRand.
	TFunction<double()> Random;
};

/* An actor of the running case: the planned data plus its figure (the web's buildActor() result). */
struct FHKMissionActor
{
	FString Id, Role, Zone, Area;
	double X = 0, Z = 0, Y = 0, Facing = 0;
	int32 Health = 1;
	bool bAlive = true;
	// Figure: rig "mission_hostile" / "mission_hostage". Root at the feet (world pose), Body tips over, MuzzleFlash (hostile).
	TUniquePtr<FHKRig> Rig;
	FHKNode* Root = nullptr;
	FHKNode* Body = nullptr;
	FHKNode* MuzzleFlash = nullptr;
	// The meshes the web raycasts against (boxes and the head sphere), with their world matrices as of the last render.
	struct FLimb
	{
		FVector Pos = FVector::ZeroVector;      // in Body
		FVector Box = FVector::ZeroVector;      // width, height, depth (BoxGeometry)
		double Radius = 0;                      // SphereGeometry(r, 18, 14) when > 0
		bool bHead = false;
		FHKMat MatrixWorld;
		const TArray<FVector>* Tris = nullptr;  // geometry triangles (float32 vertices, three.js order)
	};
	TArray<FLimb> Limbs;
	double Fall = 0, HitFlash = 0, FlashT = 0;
	bool bEngaged = false;
	double FireIn = 0;
	bool bSees = false;
};

class HAARKLOUS_API FHKMission
{
public:
	explicit FHKMission(const FHKMissionContext& InCtx);
	~FHKMission();
	FHKMission(const FHKMission&) = delete;
	FHKMission& operator=(const FHKMission&) = delete;
	FHKMissionContext Ctx;

	// ---- What game-main.js calls ----
	void Start(bool bViaGesture = false);           // startMission(): from the marker beside the entrance
	void Stop();                                     // leave the mission (calls Ctx.Leave)
	void Pause();
	bool PadButton(const FString& Name);             // true when the mission used the button
	void Triggers(double L2, double R2);             // gamepad triggers (L2 aim, R2 fire)
	bool Key(const FString& KeyName, const FString& Code, bool bRepeat = false);   // keydown (e.key, e.code, e.repeat)
	bool Mouse(int32 Button, bool bDown);            // 0 left (fire), 2 right (aim)
	void Say(const FString& Text);
	// Every frame, after the main loop's own update (the web's requestAnimationFrame tick; it does nothing while off).
	// RealDt: the real frame time in seconds (unclamped: the mission's timers run on real time like setTimeout).
	void Tick(double RealDt);
	// game-main.js lines 408-414 plus the canvas mouse handlers: routes one frame of input while the mission runs.
	// Returns true when the mission ended during this call; then circle, cross, square and options have been removed
	// from In.Pressed and the main loop sets player.mode = 'foot'. Call every frame (it also tracks the right button).
	bool RouteInput(FHKInputFrame& In);
	void DrawHud(UCanvas* Canvas);                   // mission HUD and panels (only while active)
	bool HandleClick(const FVector2D& UiPos);        // a click on a panel button or slider (UI pixels), when a cursor shows

	bool IsActive() const { return bActive; }
	const FString& GetState() const { return State; }
	bool CanMove() const { return !bActive || State == TEXT("play"); }
	double LookScale() const { return 1 - .55 * AimT; }   // scales the pad look speed
	double MoveScale() const { return !bActive ? 1 : State == TEXT("play") ? 1 - .45 * AimT : 0; }
	// Alive actors as walk obstacles (0.48 m squares, 1.8 m tall): add them to the walk world's dynamic list.
	TArray<FHKObstacle> ActorObstacles() const;
	TArray<FHKObstacle> Obstacles() const { return bActive ? ActorObstacles() : TArray<FHKObstacle>(); }

	// ---- Visuals ----
	// Figures of the running case (one rig each); RigsVersion changes whenever they are rebuilt or removed.
	TArray<FHKRig*> ActorRigs() const;
	int32 RigsVersion = 0;
	static FString RigNameOf(const FHKMissionActor& A) { return A.Role == TEXT("hostile") ? TEXT("mission_hostile") : TEXT("mission_hostage"); }
	// Pistol and hands (shown while the mission is active); Tick() places its root at the camera.
	FHKViewmodel Viewmodel;
	bool ShowViewmodel() const { return bActive; }
	// The two point lights of the web mission (constant count; dark until a shot). Colour, range, decay are fixed.
	struct FFlashLight
	{
		FVector Position = FVector::ZeroVector;   // web space
		double Intensity = 0;                     // candela (three.js physically based units)
		FLinearColor Color;
		double Distance = 0, Decay = 2;
	};
	FFlashLight FlashLight, EnemyFlashLight;
	// Bullet holes (CircleGeometry r 0.011, colour 0x1b1b1b, 2 mm off the surface); at most 80, the oldest go first.
	struct FDecal
	{
		FVector Point = FVector::ZeroVector, Normal = FVector(0, 1, 0);
		TWeakObjectPtr<USceneComponent> Component;
		int32 Serial = 0;
	};
	TArray<FDecal> Decals;
	int32 DecalSerial = 0;

	// ---- State (read-only use outside; names follow case-game.js) ----
	FString State = TEXT("off");
	HKCase::FPlan Plan;
	bool bHasPlan = false;
	uint32 Seed = 0;
	FString CaseId;
	double Elapsed = 0;
	int32 Shots = 0, Hits = 0;
	int32 Mag = HKCase::MAGAZINE, Reserve = HKCase::RESERVE;
	double Reload = 0, AimT = 0, PadAim = 0;
	bool bMouseAim = false, bTriggerHeld = false;
	double Recoil = 0, FlashT = 0, BobPhase = 0, HitMarkerT = 0, LosTimer = 0;
	int32 PlayerHealth = HKCase::PLAYER_HEALTH;
	bool bPlayerAlive = true;
	double PlayerSpeed = 0, DamageT = 0, DamageDirT = 0, EnemyFlashT = 0;
	bool bHasDamageFrom = false;
	FVector2D DamageFrom = FVector2D::ZeroVector;
	bool bInvulnerable = false;                       // debug.invulnerable
	TArray<TUniquePtr<FHKMissionActor>> Actors;       // the web's Map, in insertion order
	FHKMissionActor* FindActor(const FString& Id) const;
	// HUD values exactly as the web writes them to the page: updateHud() and the tick.
	struct FHud
	{
		FString Id, Objective, Health = TEXT("100"), Mag = TEXT("15"), Reserve = TEXT("/ 45");
		bool bHealthLow = false, bMagLow = false;
		double HealthBar = 100;
		FString Time = TEXT("00:00.0"), Reloading;
		double Gap = 16, CrossOpacity = 1, HitMarker = 0, Damage = 0, ArrowAngle = 0, ArrowOpacity = 0;
	} Hud;
	FString FeedText, FeedKind;
	FString PanelState;      // which panel shows ('' none, 'briefing', 'paused', 'success', 'fail'): viewport.dataset.casePanel
	FString Result;          // viewport.dataset.caseResult

	// ---- Test hooks ----
	// The full answer of each raycast the mission makes (in order): actor id hit ('' = world), head, distance.
	struct FRayLog { FVector Origin, Dir; double Far = 0; bool bHit = false; FString ActorId; bool bHead = false; double Distance = 0; };
	TArray<FRayLog>* RayLog = nullptr;
	bool bTickedLastFrame = false;
	// The web's debug.samplePlans / explain / probeArea through the real sampler (current context).
	TArray<HKCase::FPlan> SamplePlans(int32 N);
	struct FExplain { double Entrance = 0, Door = 0; bool bStand = false; };
	FExplain Explain(double X, double Z);
	TArray<FExplain> ExplainAll(const TArray<FVector2D>& Points);   // one sampler for many points
	int32 ProbeArea(const FString& Name, int32 N = 400);
	void DebugFinish(const FString& InResult) { Finish(InResult); }   // show the result panel (screenshots)

private:
	class FSampler;
	bool bActive = false;
	// requestAnimationFrame(tick) bookkeeping: the tick runs from the frame after the one that asked for it.
	bool bFrameScheduled = false;
	uint64 ScheduledAt = 0, FrameCounter = 0;
	// lastTime / clock (seconds of real time) and setTimeout.
	double Clock = 0, LastTime = 0;
	bool bHasLastTime = false;
	struct FTimer { int32 Id = 0; double Due = 0; TFunction<void()> Fn; };
	TArray<FTimer> Timers;
	int32 NextTimerId = 1, FeedTimer = 0;
	bool bHasLastPos = false;
	FVector2D LastPos = FVector2D::ZeroVector;
	double VmAspect = 1;
	bool bPrevRight = false;
	// Presentation easing (CSS transitions: hit marker 80 ms, health bar 200 ms).
	double HitMarkerShown = 0, HealthBarShown = 100;
	struct FClickTarget { FBox2D Box; FString Action; FString Slider; double Min = 0, Max = 0; };
	TArray<FClickTarget> ClickTargets;

	double Rand();
	double Between(double A, double B) { return A + Rand() * (B - A); }
	int32 SetTimeout(TFunction<void()> Fn, double Ms);
	void ClearTimeout(int32 Id);
	void RunTimers();

	void BuildActor(const HKCase::FPlanActor& A);
	void ClearActors();
	void RenderPoint();
	void NewCase();
	void Briefing();
	void Begin(bool bViaGesture);
	void Finish(const FString& InResult);
	void Action(const FString& Name, bool bViaGesture = false);
	void ShowPanel();
	void HidePanel();
	void Feed(const FString& Text, const FString& Kind = FString());
	void UpdateHud();
	void Sound(const FString& Kind, double Distance = 1);
	void StartReload();
	void Fire();
	bool HostileFire(FHKMissionActor& A);
	void HostileLooks(double Dt);
	void TickFrame();
	// raycaster.intersectObjects(shootables) with the world from Ctx.Shoot and the actor meshes tested here.
	struct FRayHit { bool bHit = false; FHKMissionActor* Actor = nullptr; bool bHead = false; double Distance = 0; FHKShotHit World; };
	FRayHit Raycast(const FVector& Origin, const FVector& Dir, double Far, bool bActorsToo);
	void AddDecal(const FHKShotHit& Hit);
	FVector CameraDir() const;
	TArray<FVector2D> HudArrow() const;
};
