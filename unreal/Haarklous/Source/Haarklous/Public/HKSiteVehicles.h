#pragma once

#include "CoreMinimal.h"
#include "HKVehicles.h"

class FHKDirtField;

/*
 * Construction-site machines (new feature, asked for by the user on 28 Oct 2026; not in the web game, so they are
 * added after the web's vehicle list and web players do not see them). Models: tools/blender/build_machines.py
 * (rigs site_*), soil: FHKDirtField. Web frame, metres, forward -z, yaw decreases when turning right.
 *
 * Driving is a simple but physical model of a heavy machine: mass (plus its payload), engine power, tractive force
 * limited by grip, rolling resistance by ground (soil, grass, gravel, asphalt), the slope pulling back, service brakes,
 * an automatic gearbox for the rpm; tracked machines have hydrostatic drives (speed set by the levers, slower uphill
 * and when pushing soil); the wheel loader and the hauler steer by articulation, the trucks and the crane by their
 * front axles. Soil is conserved: buckets and blades take what they cut and put it into a truck body under them, on
 * the site's soil, or (outside the site) on a heap like the web excavator's.
 */
struct FHKSiteLoad
{
	FString Kind;                  // bricks | beams | skip
	FVector P = FVector::ZeroVector;   // bottom centre (web frame)
	double Yaw = 0, Mass = 1000, Height = 1.2;
	bool bHooked = false;
	FVector Vel = FVector::ZeroVector;
};

class HAARKLOUS_API FHKSiteMachine : public FHKGroundVehicle
{
public:
	FHKSiteMachine(double InX, double InZ, double InYaw, double InRadius, double InLength, const TCHAR* InRig, const TCHAR* InKind, const TCHAR* InName);

	static FHKDirtField* Site;                              // set by AHKGame::SetupSite
	static TArray<FHKSiteMachine*>& All();
	static TArray<FHKSiteLoad*>& Loads();
	TFunction<void(double, double, double)> OnSpill;        // soil that lands outside the site: (x, z, m3)

	double Payload = 0, Capacity = 0;                      // soil carried (m3)
	// Trucks: take soil poured at a world point over their body; returns the volume taken.
	virtual double AcceptSoil(const FVector& At, double Volume) { return 0; }
	// Put soil down at a world point: into a truck under it, onto the site's soil, or into a web-style heap.
	void PourSoil(const FVector& At, double Volume, double Spread = .8);

	virtual FHKSeatPose Seat() override;
	virtual void Enter() override;
	virtual FVector2D Exit() override;
	virtual FHKHipAnchor HipAnchor(const FString& SeatName) override { return FHKHipAnchor(); }
	virtual FString Hud() const override;

protected:
	// Drivetrain.
	double MassKg = 20000, PowerKw = 120, VMax = 10, VRev = 4, Grip = .65, Steer = 0;
	int32 Gears = 4;
	bool bTracked = false;
	double EngineLoad = 0;
	FHKNode* SeatNode = nullptr;
	FVector SeatOffset = FVector::ZeroVector;
	double HalfL = 2, HalfW = 1;                              // footprint for pitch and roll
	void Drive(double Dt, double Fwd, double Back, double ExtraResist = 0);
	void Hydrostatic(double Dt, double Target, double Accel);
	double GroundResistance() const;
	void SettleBig(double Dt);
	void EngineSound(double Rpm, double Load);
	double WheelAngle = 0;
	void SpinWheels(const TArray<FHKNode*>& W, double WheelR, double Dt);
	FString GearText() const;
};

/* Cat D6 class crawler dozer: hydrostatic tracks, a semi-U blade that cuts, pushes and spreads soil. */
class HAARKLOUS_API FHKDozer : public FHKSiteMachine
{
public:
	FHKDozer(double InX, double InZ, double InYaw);
	double Lift = .1, Tilt = 0, TrackPhase = 0, Ahead = 0;
	FHKNode *Arm = nullptr, *Blade = nullptr, *Edge = nullptr;
	TArray<FHKNode*> Tracks;
	struct FRam { FHKNode* PA; FVector A; FHKNode* PB; FVector B; FHKNode* Barrel; FHKNode* Rod; };
	TArray<FRam> Rams;
	virtual void Update(double Dt, const FHKVehicleInput& In) override;
	virtual TArray<double> NetExtra() const override { return {Lift, Tilt}; }
	virtual void NetApply(const TArray<double>& A) override;
	virtual FString Hud() const override;
	void Pose();
};

/* 30 t crawler excavator: swing, boom, stick and bucket (ISO controls like the web excavator), digs the site soil. */
class HAARKLOUS_API FHKSiteExcavator : public FHKSiteMachine
{
public:
	FHKSiteExcavator(double InX, double InZ, double InYaw);
	double Swing = 0, Boom = .35, StickA = -1.35, BucketA = -1.2, Vl = 0, Vr = 0, TrackPhase = 0;
	FHKNode *Upper = nullptr, *Cab = nullptr, *BoomPivot = nullptr, *StickPivot = nullptr, *BucketPivot = nullptr, *Tip = nullptr, *Dirt = nullptr;
	TArray<FHKNode*> Tracks;
	struct FRam { FHKNode* PA; FVector A; FHKNode* PB; FVector B; FHKNode* Barrel; FHKNode* Rod; };
	TArray<FRam> Rams;
	FVector LastTip = FVector::ZeroVector;
	virtual void Update(double Dt, const FHKVehicleInput& In) override;
	virtual FHKSeatPose Seat() override;
	virtual TArray<double> NetExtra() const override { return {Swing, Boom, StickA, BucketA, Payload}; }
	virtual void NetApply(const TArray<double>& A) override;
	virtual FString Hud() const override;
	void Pose();
};

/* Two-unit articulated steering (wheel loader, hauler): the front unit's axle leads, the rear follows the hinge. */
struct FHKArticulation
{
	double La = 1.6, Lb = 1.6;          // hinge to the front axle, hinge to the rear axle
	FVector2D PA, PB;                   // axle points (world x, z)
	double YawA = 0, YawB = 0, Phi = 0; // headings and the articulation angle (YawA - YawB)
	void Init(double HX, double HZ, double YawRear, double InPhi);
	void Step(double Dist, double PhiNow);
	FVector2D Hinge() const;
};

/* Volvo L120 class wheel loader: articulated, lift arm and self-levelling bucket, scoops soil from piles. */
class HAARKLOUS_API FHKWheelLoader : public FHKSiteMachine
{
public:
	FHKWheelLoader(double InX, double InZ, double InYaw);
	FHKArticulation Art;
	double LiftA = 0, TiltA = .15, Spin = 0;
	FHKNode *Front = nullptr, *Arm = nullptr, *Bucket = nullptr, *Lip = nullptr, *Dirt = nullptr;
	TArray<FHKNode*> Wheels;
	struct FRam { FHKNode* PA; FVector A; FHKNode* PB; FVector B; FHKNode* Barrel; FHKNode* Rod; };
	TArray<FRam> Rams;
	virtual void Update(double Dt, const FHKVehicleInput& In) override;
	virtual TArray<double> NetExtra() const override { return {Art.Phi, LiftA, TiltA, Payload}; }
	virtual void NetApply(const TArray<double>& A) override;
	virtual FString Hud() const override;
	void Pose();
};

/* Dump trucks: the articulated hauler (Volvo A30 class) and the 8x4 tipper. */
class HAARKLOUS_API FHKDumpTruck : public FHKSiteMachine
{
public:
	FHKDumpTruck(double InX, double InZ, double InYaw, bool bArticulated);
	bool bArt = false;
	FHKArticulation Art;
	double Tip = 0, SteerA = 0, Spin = 0, WheelBase = 4.2;
	FHKNode *Trailer = nullptr, *Bed = nullptr, *LoadNode = nullptr;
	TArray<FHKNode*> Wheels, Steers;
	struct FRam { FHKNode* PA; FVector A; FHKNode* PB; FVector B; FHKNode* Barrel; FHKNode* Rod; };
	TArray<FRam> Rams;
	virtual void Update(double Dt, const FHKVehicleInput& In) override;
	virtual double AcceptSoil(const FVector& At, double Volume) override;
	virtual TArray<double> NetExtra() const override { return {bArt ? Art.Phi : SteerA, Tip, Payload}; }
	virtual void NetApply(const TArray<double>& A) override;
	virtual FString Hud() const override;
	void Pose();
};

/* Hook on a wire: a damped pendulum under a moving top point; carries a site load. */
struct FHKHook
{
	FVector P = FVector::ZeroVector, Vel = FVector::ZeroVector;
	double Wire = 6;
	FHKSiteLoad* Load = nullptr;
	void Step(double Dt, const FVector& Top);
	bool Toggle();                       // ✕: pick up the nearest load under the hook, or set the load down
};

/* Three-axle all-terrain crane: drives like a truck; with the outriggers down (L1) it slews, luffs, telescopes and hoists. */
class HAARKLOUS_API FHKMobileCrane : public FHKSiteMachine
{
public:
	FHKMobileCrane(double InX, double InZ, double InYaw);
	double SteerA = 0, Spin = 0, Slew = 0, Luff = 0, Ext = 0, Deploy = 0, DeployTarget = 0;
	bool bPrevL1 = false;
	FHKHook Hook;
	FHKNode *Super = nullptr, *BoomBase = nullptr, *TipNode = nullptr, *HookNode = nullptr, *Wire = nullptr;
	TArray<FHKNode*> Tele, Outriggers, Wheels, Steers;
	struct FRam { FHKNode* PA; FVector A; FHKNode* PB; FVector B; FHKNode* Barrel; FHKNode* Rod; };
	TArray<FRam> Rams;
	virtual void Update(double Dt, const FHKVehicleInput& In) override;
	virtual FHKSeatPose Seat() override;
	virtual TArray<double> NetExtra() const override { return {SteerA, Slew, Luff, Ext, Deploy, Hook.Wire}; }
	virtual void NetApply(const TArray<double>& A) override;
	virtual FString Hud() const override;
	void Pose();
};

/* Tower crane: 30 m mast, 40 m jib; climb in at the foot of the mast. */
class HAARKLOUS_API FHKTowerCrane : public FHKSiteMachine
{
public:
	FHKTowerCrane(double InX, double InZ, double InYaw);
	double Slew = 0, SlewV = 0, Trolley = 14;
	FHKHook Hook;
	FHKNode *SlewNode = nullptr, *TrolleyNode = nullptr, *HookNode = nullptr, *Wire = nullptr;
	virtual void Update(double Dt, const FHKVehicleInput& In) override;
	virtual FHKSeatPose Seat() override;
	virtual void Enter() override;
	virtual FVector2D Exit() override;
	virtual TArray<FHKObstacle> Footprint() const override;
	virtual TArray<double> NetExtra() const override { return {Slew, Trolley, Hook.Wire}; }
	virtual void NetApply(const TArray<double>& A) override;
	virtual FString Hud() const override;
	void Pose();
};

namespace HKSiteVehicles
{
	// The machines in site.json order (excavator, hauler, tipper, dozer, loader, crane, tower).
	HAARKLOUS_API void CreateAll(const FHKDirtField& Site, TArray<TUniquePtr<FHKVehicle>>& Out);
}
