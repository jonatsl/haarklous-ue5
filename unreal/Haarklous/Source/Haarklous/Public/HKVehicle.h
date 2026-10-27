#pragma once

#include "CoreMinimal.h"
#include "HKCollision.h"
#include "HKNode.h"

/*
 * Base of every drivable machine (port of the web's class Vehicle in game-vehicles.js and class Craft in
 * game-aircraft.js). Web space: metres, y up, yaw about +y, forward = (-sin yaw, -cos yaw).
 * The model is an FHKRig loaded from Content/Data/rigs/<kind>.json; the C++ code moves its nodes and
 * AHKRigActor shows them. Multiplayer identifies vehicles by their index in the game's vehicle list.
 * The main loop (AHKGame, port of game-main.js) only uses what is declared here.
 */
struct FHKVehicleInput
{
	// Ground vehicles (game-main.js updateVehicle): throttle = R2, brake = L2, steer = left stick x (dead zone .08).
	double Throttle = 0, Brake = 0, Steer = 0, Drunk = 0;
	bool L1 = false, R1 = false;
	double L2 = 0, R2 = 0;
	// sticks {lx, ly, rx, ry}; for aircraft and the drone these are the Mode 2 flight sticks (flightSticks()).
	double LX = 0, LY = 0, RX = 0, RY = 0;
	bool bEmpty = true;   // update(dt, {}) for parked / idle vehicles
};

struct FHKSeatPose
{
	double X = 0, Y = 0, Z = 0, Yaw = 0, Pitch = 0, Roll = 0, Lateral = 0;
};

struct FHKHipAnchor
{
	bool bValid = false;
	FHKNode* Obj = nullptr;     // the node whose frame Pos is in
	FVector Pos = FVector::ZeroVector;
	FString Stance;             // 'ride', 'pillion', 'drive', 'sit' ...
};

struct FHKCameraPose
{
	FVector Position = FVector::ZeroVector;
	FQuat Quaternion = FQuat::Identity;
};

class HAARKLOUS_API FHKVehicle
{
public:
	virtual ~FHKVehicle() {}
	FHKRig Rig;                       // Rig.Root is the web's this.group
	FString Kind, Name;               // 'motorcycle', 'car', 'excavator', 'helicopter', 'plane'
	bool bAircraft = false;
	double X = 0, Y = 0, Z = 0, Yaw = 0, Speed = 0, Pitch = 0, Roll = 0;
	double Radius = 1, Length = 4, Width = 1.8, Height = 1.5;
	bool bActive = false;
	bool bVisible = true;             // group.visible (far vehicles are hidden and skipped)
	// Read-outs used by the HUD and the passenger's engine sound (0 when the vehicle has none).
	double Rpm = 0, Load = 0, Prop = 0, Throttle = 0;
	int32 Gear = 0;
	bool bGrounded = true, bBroken = false;
	FVector PassengerEye = FVector::ZeroVector;   // vehicle frame
	TArray<double> Spawn;                          // netGet() at start (multiplayer resets to it)
	TArray<double> NetCur;                         // smoothing state for remote-driven vehicles (game-multiplayer.js)
	bool bHasNetCur = false;
	FLinearColor PaintColor = FLinearColor::White; // body colour (cars, motorcycles); AHKRigActor applies it
	bool bHasPaint = false;
	TFunction<void(FHKVehicle*)> OnCrash;          // aircraft
	TFunction<void(double, double)> OnDump;        // excavator: dumped a pile at (x, z)

	FVector2D Forward() const { return FVector2D(-FMath::Sin(Yaw), -FMath::Cos(Yaw)); }
	virtual void Update(double Dt, const FHKVehicleInput& In) = 0;
	virtual FHKSeatPose Seat() = 0;
	virtual void Enter() = 0;
	virtual FVector2D Exit() = 0;                  // returns the exit point (x, z)
	virtual TArray<FHKObstacle> Footprint() const = 0;
	virtual TArray<double> NetGet() const = 0;
	virtual void NetSet(const TArray<double>& A) = 0;
	virtual void NetAnimate(double Dt) {}
	virtual FHKHipAnchor HipAnchor(const FString& Seat) = 0;
	// Aircraft only.
	virtual FHKCameraPose CameraPose(bool bChase, const FVector* PrevChase) { return FHKCameraPose(); }
	virtual FString Hud() const { return FString(); }
	virtual void Reset() {}
};
