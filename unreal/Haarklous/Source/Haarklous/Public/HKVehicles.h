#pragma once

#include "CoreMinimal.h"
#include "HKAudio.h"
#include "HKVehicle.h"

/*
 * Port of game-vehicles.js (line by line, same constants and order of operations):
 *   FHKGroundVehicle  = class Vehicle   (terrain following, collision with the vehicle worlds, multiplayer state)
 *   FHKMotorcycle     = class Motorcycle (2005 Honda CBF600S: lean physics, 6-gear rpm model)
 *   FHKCar            = class Car        (hatchback, bicycle-model steering, left-hand drive, two-bone arms)
 *   FHKExcavator      = class Excavator  (differential tracks, swing / boom / stick / bucket with rams, digging and dumping)
 * plus the vehicle list of game-main.js (HKVehicles::CreateAll).
 * Web space throughout (metres, y up). The model of each kind is the rig Content/Data/rigs/<kind>.json; the code moves
 * its nodes exactly like the JavaScript moves its Object3Ds, and AHKRigActor shows them.
 * Terrain: FHKData::Get().HeightWorld (heightAt); collision: FHKData::Get().VehicleWorld(radius) (worldFor), whose dynamic
 * obstacles (the other vehicles' footprints) the main loop keeps up to date like game-main.js updateDynamic().
 */
namespace HKVehicles
{
	// performance.now() in milliseconds (only used for the drunk wobble); tests can plug in a scripted clock.
	HAARKLOUS_API double NowMs();
	HAARKLOUS_API void SetClock(TFunction<double()> Fn);
	// Math.random() (only the excavator's scrape sound uses it); tests can plug in a seeded generator.
	HAARKLOUS_API double Random();
	HAARKLOUS_API void SetRandom(TFunction<double()> Fn);
	// Colour of a web hex value (sRGB) as the linear colour three.js uses for rendering.
	HAARKLOUS_API FLinearColor ColorFromHex(uint32 Hex);
	// game-main.js localHeadingToYaw(h): a heading in the map's local frame to a world yaw.
	HAARKLOUS_API double LocalHeadingToYaw(double H);
	// Car paint colours and names (index = model % 7), motorcycle colours of the generated bikes.
	HAARKLOUS_API uint32 CarColor(int32 Model);
	HAARKLOUS_API FString CarName(int32 Model);
	HAARKLOUS_API uint32 BikeColor(int32 Index);
	/*
	 * game-main.js lines 74-121: the game's vehicles in the web's order (multiplayer identifies them by index):
	 * the parked cars of the map data, the Honda, the excavator, the helicopter (AIR.heli), the plane (AIR.plane), then
	 * the seeded street parking (seed 1707: up to 25 cars + 7 motorcycles). Sets every vehicle's Spawn = NetGet().
	 * Does not set the excavator's OnDump or the aircraft's OnCrash (the main loop does, as game-main.js does).
	 */
	HAARKLOUS_API void CreateAll(TArray<TUniquePtr<FHKVehicle>>& Out);
	// game-main.js AIR (aircraft homes) and FIRE_POS (kept clear of generated vehicles).
	struct FAir { double X, Z, Yaw; };
	HAARKLOUS_API FAir HeliHome();
	HAARKLOUS_API FAir PlaneHome();
}

/* class Vehicle (game-vehicles.js): base of the motorcycle, the car and the excavator. */
class HAARKLOUS_API FHKGroundVehicle : public FHKVehicle
{
public:
	FHKGroundVehicle(double InX, double InZ, double InYaw, double InRadius, double InLength);

	double TerrainRoll = 0;                    // this.terrainRoll (set by Settle)
	double WheelSpin = 0;
	TSharedPtr<IHKEngineSound> Engine;         // this.engine (null when nobody drives)
	FHKNode* Body = nullptr;                   // this.body
	struct FHip { bool bValid = false; FHKNode* Obj = nullptr; FVector Pos = FVector::ZeroVector; FString Stance; };
	FHip HipD, HipP;                           // this.hips.d / this.hips.p

	// Two circles (front and rear) must be clear of buildings, trees and hedges.
	bool Clear(double NX, double NZ) const;
	void Settle(double Dt);
	void Move(double Dt);
	void Place();
	FVector2D ExitPoint(double Side = -1) const;

	virtual TArray<FHKObstacle> Footprint() const override;
	virtual TArray<double> NetGet() const override;
	virtual void NetSet(const TArray<double>& A) override;
	virtual FHKHipAnchor HipAnchor(const FString& Seat) override;
	virtual TArray<double> NetExtra() const { return {}; }
	virtual void NetApply(const TArray<double>& A) {}

protected:
	// Loads Content/Data/rigs/<RigName>.json into Rig and resolves the nodes the code moves.
	void LoadRig(const FString& RigName);
	FHKNode* N(const FString& NodeName);
};

/* 2005 Honda CBF600S. */
class HAARKLOUS_API FHKMotorcycle : public FHKGroundVehicle
{
public:
	FHKMotorcycle(double InX, double InZ, double InYaw, uint32 InColor = 0x56657c);
	uint32 ColorHex = 0x56657c;
	double Lean = 0, Bar = 0;                 // gear and rpm: FHKVehicle::Gear / Rpm
	FHKNode *Front = nullptr, *Fw = nullptr, *Rw = nullptr, *Steer = nullptr, *Bars = nullptr, *Hands = nullptr, *Arms = nullptr;

	virtual FHKSeatPose Seat() override;
	virtual void Enter() override;
	virtual FVector2D Exit() override;
	virtual void Update(double Dt, const FHKVehicleInput& In) override;
	virtual TArray<double> NetExtra() const override { return {Bar, Lean}; }
	virtual void NetApply(const TArray<double>& A) override;
	virtual void NetAnimate(double Dt) override;
};

/* Hatchback car (left-hand drive). */
class HAARKLOUS_API FHKCar : public FHKGroundVehicle
{
public:
	FHKCar(double InX, double InZ, double InYaw, int32 InModel = 0);
	int32 Model = 0;
	double SteerAngle = 0;
	struct FWheel { FHKNode* Holder = nullptr; FHKNode* Wheel = nullptr; bool bFront = false; };
	TArray<FWheel> Wheels;
	FHKNode *WheelPivot = nullptr, *SteeringWheel = nullptr, *Hands = nullptr, *Arms = nullptr;
	TArray<FHKNode*> Wrists, Forearms;

	virtual FHKSeatPose Seat() override;
	virtual void Enter() override;
	virtual FVector2D Exit() override;
	virtual void Update(double Dt, const FHKVehicleInput& In) override;
	virtual TArray<double> NetExtra() const override { return {SteerAngle}; }
	virtual void NetApply(const TArray<double>& A) override;
	virtual void NetAnimate(double Dt) override;
	// Two-bone arm: shoulder fixed beside the seat, elbow bends down and outward, forearm ends at the wrist.
	void SolveArms();
};

/* Tracked excavator (about 14 t). Load (bucket fill 0..1) is FHKVehicle::Load. */
class HAARKLOUS_API FHKExcavator : public FHKGroundVehicle
{
public:
	FHKExcavator(double InX, double InZ, double InYaw);
	double Swing = 0, Boom = .35, StickA = -1.35, BucketA = -1.0, Vl = 0, Vr = 0, TrackPhase = 0;
	TArray<FHKNode*> Tracks, Levers;
	TArray<TArray<FHKNode*>> LeverHands;      // levers[i].userData.hand: the rig parts "levers[i]#k"
	FHKNode *Upper = nullptr, *Cab = nullptr, *BoomPivot = nullptr, *StickPivot = nullptr, *BucketPivot = nullptr, *Dirt = nullptr, *Tip = nullptr;
	struct FRam { FHKNode* ParentA = nullptr; FVector A; FHKNode* ParentB = nullptr; FVector B; FHKNode* Barrel = nullptr; FHKNode* Rod = nullptr; };
	TArray<FRam> Rams;
	double LastFlow = 0;                       // hydraulic flow of the last update (the engine's hydraulics(flow) sound)

	void Pose();
	virtual FHKSeatPose Seat() override;
	virtual void Enter() override;
	virtual FVector2D Exit() override;
	virtual void Update(double Dt, const FHKVehicleInput& In) override;
	virtual FHKHipAnchor HipAnchor(const FString& Seat) override;
	virtual TArray<double> NetExtra() const override { return {Swing, Boom, StickA, BucketA, Load, Vl, Vr}; }
	virtual void NetApply(const TArray<double>& A) override;
	virtual void NetAnimate(double Dt) override;
	bool HandsVisible() const;
private:
	void SetHandsVisible(bool bV);
	void ShowDirt();
};
