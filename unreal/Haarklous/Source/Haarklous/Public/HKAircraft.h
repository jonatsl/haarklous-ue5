#pragma once

#include "CoreMinimal.h"
#include "HKAudio.h"
#include "HKVehicle.h"
#include "Misc/Optional.h"

/*
 * Port of game-aircraft.js (line by line, same constants and order of operations):
 *   FHKCraft       = class Craft       (shared rigid body: position, velocity, orientation quaternion, body rates)
 *   FHKHelicopter  = class Helicopter  (Airbus H125 style, about 2 250 kg, 10.7 m rotor)
 *   FHKPlane       = class Plane       (light two-seat sport plane, about 700 kg, 8 m span, 14 m2 wing)
 *   FHKDrone       = class Drone       (5-inch FPV quad, angle and acro modes; not a vehicle of the list)
 * Controls are "Mode 2" (FHKVehicleInput LX/LY/RX/RY as game-main.js flightSticks(), plus Brake = L2 for the plane):
 *   left stick up/down collective (heli: centre = hover) / throttle (plane: it stays where you leave it) / thrust (drone);
 *   left stick left/right yaw; right stick up/down pitch (pull back = nose up), left/right roll.
 * Body rates W: x pitch up, y yaw left, z roll left. Web space throughout (metres, y up).
 *
 * The FHKVehicle read-outs X, Y, Z, Yaw, Speed are kept equal to the web's getters (x = pos.x, y = pos.y - gearH,
 * yaw from the quaternion, speed = |vel|) after every call that changes the state (Update, Reset, NetSet).
 */
class HAARKLOUS_API FHKCraft : public FHKVehicle
{
public:
	FHKCraft(double InX, double InZ, double InYaw, double InGearH);

	double GearH = 0;
	double HomeX = 0, HomeZ = 0, HomeYaw = 0;          // this.home
	FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector, W = FVector::ZeroVector;
	FQuat Q = FQuat::Identity;
	double CrashT = 0;
	TSharedPtr<IHKEngineSound> Engine;
	// Walkers bump into the parked aircraft: rectangles [along forward, to the right, length, width].
	struct FRect { double A0, B0, L, Wd; };
	TArray<FRect> Rects;
	double RotorR = 0, Top = 0, CrashSpeed = 9, ExitSide = 0, ExitFwd = 0, ChaseDist = 15, SeatPitch = 0;
	FVector SeatLocal = FVector::ZeroVector;           // this.seat (pilot's eye in the craft frame)
	FVector HipDPos = FVector::ZeroVector, HipPPos = FVector::ZeroVector;
	struct FArm { FHKNode* Wrist = nullptr; FVector Elbow = FVector::ZeroVector; FHKNode* Mesh = nullptr; };
	TArray<FArm> Arms;
	TArray<FHKNode*> Hands;
	FHKNode* ArmGroup = nullptr;
	FHKNode* Rotor = nullptr;                          // helicopter: rotor strike test point
	// Material opacities the web code animates (bladeMat.opacity, disc/pdisc material opacity): for AHKRigActor / effects.
	double BladeOpacity = 1, DiscOpacity = 0;

	// The web's getters and helpers.
	double GetY() const { return Pos.Y - GearH; }
	double GetSpeed() const;
	double GetYaw() const;
	struct FAxes { FVector F, U, R; };
	FAxes Axes() const;
	FVector2D CraftForward() const;                    // forward(): horizontal nose direction
	void Rotate(double Dt);
	void Attitude(double& OutPitch, double& OutBank) const;
	FVector BodyVel() const;
	void Crash();
	bool Obstacles(const FVector& Prev, double Bottom);
	void SolveArms();
	void PlaceCraft();                                 // place()
	void EnterCommon();
	FVector2D ExitPoint() const;
	void SyncBase();
	virtual double SpinValue() const = 0;
	virtual void SetSpin(double V) = 0;
	virtual void Spin(double Dt) = 0;

	// FHKVehicle
	virtual void Update(double Dt, const FHKVehicleInput& In) override final;
	// Not in the web code (aircraft have no seat()): the pilot's eye in the world with yaw, pitch and bank.
	virtual FHKSeatPose Seat() override;
	virtual FVector2D Exit() override;
	virtual TArray<FHKObstacle> Footprint() const override;
	virtual TArray<double> NetGet() const override;
	virtual void NetSet(const TArray<double>& A) override;
	virtual void NetAnimate(double Dt) override { Spin(Dt); }
	virtual FHKHipAnchor HipAnchor(const FString& Seat) override;
	// First person from the seat, or a chase camera behind and above (PrevChase: last frame's chase position or null).
	virtual FHKCameraPose CameraPose(bool bChase, const FVector* PrevChase) override;
	virtual void Reset() override;

protected:
	virtual void Step(double Dt, const FHKVehicleInput& In) = 0;   // the web's update(dt, inp)
	void LoadRig(const FString& RigName);
	FHKNode* N(const FString& NodeName);
	void AddArm(FHKNode* Hand, const FString& WristName, const FString& MeshName, const FVector& Elbow);
};

class HAARKLOUS_API FHKHelicopter : public FHKCraft
{
public:
	FHKHelicopter(double InX, double InZ, double InYaw);
	double RotorAngle = 0;                             // rpm (0..1) is FHKVehicle::Rpm
	FHKNode *Disc = nullptr, *TailRotor = nullptr, *Cyclic = nullptr, *Collective = nullptr;
	virtual void Enter() override;
	virtual double SpinValue() const override { return Rpm; }
	virtual void SetSpin(double V) override { Rpm = V; }
	virtual void Spin(double Dt) override;
	virtual FString Hud() const override;
protected:
	virtual void Step(double Dt, const FHKVehicleInput& In) override;
};

class HAARKLOUS_API FHKPlane : public FHKCraft
{
public:
	FHKPlane(double InX, double InZ, double InYaw);
	double Mass = 700, S = 14, Tmax = 3600;            // throttle and prop are FHKVehicle::Throttle / Prop
	FHKNode *PropG = nullptr, *PDisc = nullptr, *Elevator = nullptr, *Rudder = nullptr, *Stick = nullptr, *Lever = nullptr;
	TArray<FHKNode*> Ailerons;
	virtual void Enter() override;
	virtual void Reset() override;
	virtual double SpinValue() const override { return Prop; }
	virtual void SetSpin(double V) override { Prop = V; }
	virtual void Spin(double Dt) override;
	virtual FString Hud() const override;
protected:
	virtual void Step(double Dt, const FHKVehicleInput& In) override;
};

/* What the drone needs from the game (game-main.js updateDrone: floorBelow, walk-world hitsSolid, the player's ears). */
struct FHKDroneFloor
{
	double Y = 0;
	double Ceiling = INFINITY;   // f.ceilingHeight ?? Infinity
};
struct FHKDroneEnv
{
	TFunction<TOptional<FHKDroneFloor>(const FVector& P, double Y)> Floor;   // floor(p, y) -> {y, ceiling} | null
	TFunction<bool(const FVector& P, double Y0, double Y1)> Solid;          // solid(p, y0, y1)
	bool bHasListener = false;
	FVector Listener = FVector::ZeroVector;
};
// drone.osd(groundY)
struct FHKDroneOsd
{
	FString Mode, Volts;
	int32 Battery = 0;
	double Alt = 0, Speed = 0, Time = 0, Dist = 0, Thrust = 0;
	bool bArmed = false, bCrashed = false;
};

class HAARKLOUS_API FHKDrone
{
public:
	FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector, Home = FVector::ZeroVector;
	FQuat Q = FQuat::Identity;
	FString Mode = TEXT("angle");                      // 'angle' | 'acro'
	double Battery = 1, YawA = 0, PitchA = 0, RollA = 0, Time = 0, Thrust = 0;
	bool bArmed = false, bCrashed = false, bLanded = false;
	TSharedPtr<IHKEngineSound> Engine;

	void Launch(double X, double Y, double Z, double Yaw);
	void Stop();
	void SetMode(const FString& M);
	void Reset();
	void Update(double Dt, const FHKVehicleInput& In, const FHKDroneEnv& Env);
	void Crash();
	// FPV camera tilted up (more in acro).
	FHKCameraPose CameraPose() const;
	FHKDroneOsd Osd(double GroundY) const;
	// game-main.js floorBelow(p, y) over a walk world's floors (the game passes FHKData::Get().WalkWorld).
	static TOptional<FHKDroneFloor> FloorBelow(const FHKWalkWorld& World, const FVector& P, double Y);
};
