// Port of reference/web-game/game-vehicles.js and the vehicle list of game-main.js (lines 74-121).
#include "HKVehicles.h"
#include <limits>
#include "HKAircraft.h"
#include "HKData.h"
#include "HKVehicleMath.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogHKVehicles, Log, All);

namespace
{
	const uint32 GCarColors[7] = {0x111214, 0x9e1b20, 0xb7bcc2, 0xf0f0ec, 0x1f3a63, 0x2f5d3a, 0x6b6f75};
	const TCHAR* GCarNames[7] = {TEXT("Black"), TEXT("Red"), TEXT("Silver"), TEXT("White"), TEXT("Blue"), TEXT("Green"), TEXT("Grey")};
	const uint32 GBikeColors[6] = {0x16171a, 0x9e1b20, 0xa9afb5, 0x1f3a63, 0xe9e9e4, 0x56657c};
	TFunction<double()> GRandom;
	TFunction<double()> GClock;
	FHKAudio& Snd() { return FHKAudio::Get(); }
	double HeightAt(double X, double Z) { return FHKData::Get().HeightWorld(X, Z); }
	double At(const TArray<double>& A, int32 I) { return A.IsValidIndex(I) && !FMath::IsNaN(A[I]) ? A[I] : 0; }
}

double HKVehicles::NowMs() { return GClock ? GClock() : FPlatformTime::Seconds() * 1000.0; }
void HKVehicles::SetClock(TFunction<double()> Fn) { GClock = MoveTemp(Fn); }

double HKVehicles::Random() { return GRandom ? GRandom() : FMath::FRand(); }
void HKVehicles::SetRandom(TFunction<double()> Fn) { GRandom = MoveTemp(Fn); }

FLinearColor HKVehicles::ColorFromHex(uint32 Hex)
{
	return FLinearColor::FromSRGBColor(FColor((Hex >> 16) & 255, (Hex >> 8) & 255, Hex & 255, 255));
}

double HKVehicles::LocalHeadingToYaw(double H)
{
	// const dx = Math.sin(h), ds = Math.cos(h), wx = dx * Math.cos(theta) + ds * Math.sin(theta), wz = -dx * Math.sin(theta) + ds * Math.cos(theta);
	const double Theta = FHKData::Get().Theta;
	const double dx = std::sin(H), ds = std::cos(H), wx = dx * std::cos(Theta) + ds * std::sin(Theta), wz = -dx * std::sin(Theta) + ds * std::cos(Theta);
	return std::atan2(-wx, -wz);
}

uint32 HKVehicles::CarColor(int32 Model) { return GCarColors[((Model % 7) + 7) % 7]; }
FString HKVehicles::CarName(int32 Model) { return GCarNames[((Model % 7) + 7) % 7]; }
uint32 HKVehicles::BikeColor(int32 Index) { return GBikeColors[((Index % 6) + 6) % 6]; }
HKVehicles::FAir HKVehicles::HeliHome() { return {26, 14, .6}; }
HKVehicles::FAir HKVehicles::PlaneHome() { return {-116.5, 0, .5236}; }

// ---------------------------------------------------------------------------------------------------------------------
// Vehicle list (game-main.js)
// ---------------------------------------------------------------------------------------------------------------------
void HKVehicles::CreateAll(TArray<TUniquePtr<FHKVehicle>>& Out)
{
	FHKData& Data = FHKData::Get();
	const TSharedPtr<FJsonObject> DV = HKJson::Obj(Data.D, TEXT("vehicles"));
	// D.vehicles.cars.forEach(c => {const p = toWorld(c.x, c.s);vehicles.push(new VH.Car(p.x, p.z, localHeadingToYaw(c.heading), c.model));});
	for (const TSharedPtr<FJsonValue>& C : HKJson::Arr(DV, TEXT("cars")))
	{
		const TSharedPtr<FJsonObject> O = C->AsObject();
		const FVector2D P = Data.ToWorld(HKJson::Num(O, TEXT("x")), HKJson::Num(O, TEXT("s")));
		Out.Add(MakeUnique<FHKCar>(P.X, P.Y, LocalHeadingToYaw(HKJson::Num(O, TEXT("heading"))), (int32)HKJson::Num(O, TEXT("model"))));
	}
	{
		const TSharedPtr<FJsonObject> M = HKJson::Obj(DV, TEXT("motorcycle"));
		const FVector2D P = Data.ToWorld(HKJson::Num(M, TEXT("x")), HKJson::Num(M, TEXT("s")));
		Out.Add(MakeUnique<FHKMotorcycle>(P.X, P.Y, HK::Pi / 2 + .3));
	}
	{
		const TSharedPtr<FJsonObject> E = HKJson::Obj(DV, TEXT("excavator"));
		const FVector2D P = Data.ToWorld(HKJson::Num(E, TEXT("x")), HKJson::Num(E, TEXT("s")));
		Out.Add(MakeUnique<FHKExcavator>(P.X, P.Y, -.4));
	}
	const FAir Heli = HeliHome(), Plane = PlaneHome();
	Out.Add(MakeUnique<FHKHelicopter>(Heli.X, Heli.Z, Heli.Yaw));
	Out.Add(MakeUnique<FHKPlane>(Plane.X, Plane.Z, Plane.Yaw));

	// More parked cars and motorcycles along the streets within about 330 m (seeded, same checks as the web).
	{
		HK::FParkMiller Rng(1707);
		auto Rnd = [&Rng]() { return Rng.Next(); };
		static const TCHAR* Kinds[] = {TEXT("residential"), TEXT("tertiary"), TEXT("secondary"), TEXT("unclassified"), TEXT("service"), TEXT("living_street")};
		struct FSeg { int32 Ri; double W; double A[2], B[2]; };
		TArray<FSeg> Segs;
		int32 Ri = 0;
		for (const TSharedPtr<FJsonValue>& RV : HKJson::Arr(Data.D, TEXT("roads")))
		{
			const TSharedPtr<FJsonObject> R = RV->AsObject();
			const FString K = HKJson::Str(R, TEXT("k"));
			bool bKind = false;
			for (const TCHAR* Kd : Kinds) bKind |= K == Kd;
			if (!bKind) continue;
			const TArray<double> Pts = HKJson::Nums(R, TEXT("p"));
			const double Wd = HKJson::Num(R, TEXT("w"), std::numeric_limits<double>::quiet_NaN());
			for (int32 k = 0; k + 3 < Pts.Num(); k += 2) Segs.Add({Ri, Wd, {Pts[k], Pts[k + 1]}, {Pts[k + 2], Pts[k + 3]}});
			Ri++;
		}
		auto SegDist = [](const double P[2], const FSeg& S)
		{
			const double dx = S.B[0] - S.A[0], ds = S.B[1] - S.A[1];
			double L = dx * dx + ds * ds;
			if (L == 0) L = 1;
			const double t = FMath::Max(0.0, FMath::Min(1.0, ((P[0] - S.A[0]) * dx + (P[1] - S.A[1]) * ds) / L));
			return HKV::Hypot(P[0] - S.A[0] - dx * t, P[1] - S.A[1] - ds * t);
		};
		const FSeg Runway = {-1, 0, {Plane.X, Plane.Z}, {Plane.X - std::sin(Plane.Yaw) * 280, Plane.Z - std::cos(Plane.Yaw) * 280}};
		struct FKeep { double X, Z, R; };
		TArray<FKeep> Keep = {{Heli.X, Heli.Z, 14}, {-2.7, 34.7, 9}};
		for (const TUniquePtr<FHKVehicle>& V : Out) Keep.Add({V->X, V->Z, 7});
		FHKWalkWorld& W = Data.VehicleWorld(1.25);
		// The web generates the list before any dynamic obstacles are set; keep it that way whatever the caller did.
		TArray<FHKObstacle> SavedDynamic = W.Dynamic;
		W.SetDynamic({});
		int32 Cars = 0, Bikes = 0;
		for (const FSeg& S : Segs)
		{
			const double dx = S.B[0] - S.A[0], ds = S.B[1] - S.A[1], L = HKV::Hypot(dx, ds);
			if (L < 20) continue;
			for (double t = 9 + Rnd() * 10; t < L - 9 && Cars + Bikes < 32; t += 26 + Rnd() * 16)
			{
				const double Side = Rnd() < .5 ? 1 : -1, ux = dx / L, us = ds / L, Off = S.W / 2 + 1.25;
				const double Lp[2] = {S.A[0] + ux * t - us * Off * Side, S.A[1] + us * t + ux * Off * Side};
				if (HKV::Hypot(Lp[0], Lp[1]) > 330) continue;
				bool bNear = false;
				for (const FSeg& O : Segs) if (O.Ri != S.Ri && SegDist(Lp, O) < 10) { bNear = true; break; }
				if (bNear) continue;
				const FVector2D P2 = Data.ToWorld(Lp[0], Lp[1]);
				const FHKPt P(P2.X, P2.Y);
				const double H = Data.HeightWorld(P.X, P.Z);
				bool bKept = false;
				for (const FKeep& Kp : Keep) if (HKV::Hypot(Kp.X - P.X, Kp.Z - P.Z) < Kp.R) { bKept = true; break; }
				const double Pw[2] = {P.X, P.Z};
				if (bKept || SegDist(Pw, Runway) < 12) continue;
				const double Yaw = LocalHeadingToYaw(std::atan2(ux * Side, us * Side)), Fx = -std::sin(Yaw), Fz = -std::cos(Yaw);
				if (FMath::Abs(Data.HeightWorld(P.X + Fx * 2, P.Z + Fz * 2) - Data.HeightWorld(P.X - Fx * 2, P.Z - Fz * 2)) > 1.1) continue;
				if (!W.CanStand(P, 1.4, H) || !W.CanStand(FHKPt(P.X + Fx * 1.6, P.Z + Fz * 1.6), 1.4, H) || !W.CanStand(FHKPt(P.X - Fx * 1.6, P.Z - Fz * 1.6), 1.4, H)) continue;
				const bool bBike = Bikes < 7 && (Cars + Bikes) % 4 == 3;
				if (bBike) Out.Add(MakeUnique<FHKMotorcycle>(P.X, P.Z, Yaw + .25, BikeColor(Bikes)));
				else Out.Add(MakeUnique<FHKCar>(P.X, P.Z, Yaw, (int32)std::floor(Rnd() * 7)));
				if (bBike) Bikes++; else Cars++;
				Keep.Add({P.X, P.Z, 7});
			}
		}
		W.SetDynamic(MoveTemp(SavedDynamic));
	}
	// vehicles.forEach(v => {v.spawn = v.netGet();});
	for (const TUniquePtr<FHKVehicle>& V : Out) V->Spawn = V->NetGet();
}

// ---------------------------------------------------------------------------------------------------------------------
// class Vehicle
// ---------------------------------------------------------------------------------------------------------------------
FHKGroundVehicle::FHKGroundVehicle(double InX, double InZ, double InYaw, double InRadius, double InLength)
{
	// constructor(x, z, yaw, radius, length) {this.group = new T.Group();this.x = x;this.z = z;this.yaw = yaw;this.speed = 0;...this.y = heightAt(x, z);this.active = false;}
	X = InX; Z = InZ; Yaw = InYaw; Speed = 0; Radius = InRadius; Length = InLength; Pitch = 0; Roll = 0;
	Y = HeightAt(InX, InZ);
	bActive = false;
}

void FHKGroundVehicle::LoadRig(const FString& RigName)
{
	FString Error;
	if (!Rig.LoadFromJson(FHKData::DataDir() / TEXT("rigs") / (RigName + TEXT(".json")), &Error)) UE_LOG(LogHKVehicles, Error, TEXT("%s"), *Error);
	// The web's group starts as a new Group at the origin; place() sets it from x, y, z, yaw, pitch, roll.
	Rig.Root->SetPos(0, 0, 0);
	Rig.Root->SetRotation(0, 0, 0);
	Rig.Root->SetScale(1, 1, 1);
	Rig.Root->bVisible = true;
}

FHKNode* FHKGroundVehicle::N(const FString& NodeName)
{
	if (FHKNode* Found = Rig.Find(NodeName)) return Found;
	UE_LOG(LogHKVehicles, Error, TEXT("%s rig has no node '%s'"), *Kind, *NodeName);
	return Rig.Create(NodeName, Rig.Root);
}

bool FHKGroundVehicle::Clear(double NX, double NZ) const
{
	// const w = worldFor(this.radius), f = forward, h = this.length / 2 - this.radius, y = heightAt(x, z);
	FHKData& Data = FHKData::Get();
	const FHKWalkWorld& W = Data.VehicleWorld(Radius);
	const FVector2D F = Forward();
	const double H = Length / 2 - Radius, Yh = Data.HeightWorld(NX, NZ);
	auto Stand = [&](double K) { const FHKPt P(NX + F.X * K, NZ + F.Y * K); return W.CanStand(P, 1.2, Data.HeightWorld(P.X, P.Z)); };
	if (H > 0) { if (!Stand(-H) || !Stand(H)) return false; }
	else if (!Stand(0)) return false;
	return FMath::Abs(Yh - Y) < 1.5;
}

void FHKGroundVehicle::Settle(double Dt)
{
	const FVector2D F = Forward();
	const double Rx = -F.Y, Rz = F.X, L = Length * .4, Wd = .6;
	const double Hf = HeightAt(X + F.X * L, Z + F.Y * L), Hb = HeightAt(X - F.X * L, Z - F.Y * L), Hl = HeightAt(X - Rx * Wd, Z - Rz * Wd), Hr = HeightAt(X + Rx * Wd, Z + Rz * Wd);
	Y = (Hf + Hb) / 2;
	const double K = FMath::Min(1.0, Dt * 8);
	Pitch += (std::atan2(Hf - Hb, 2 * L) - Pitch) * K;
	TerrainRoll = std::atan2(Hl - Hr, 2 * Wd);
}

void FHKGroundVehicle::Move(double Dt)
{
	const FVector2D F = Forward();
	const double NX = X + F.X * Speed * Dt, NZ = Z + F.Y * Speed * Dt;
	if (FMath::Abs(Speed) < .001 || Clear(NX, NZ)) { X = NX; Z = NZ; }
	else
	{
		if (FMath::Abs(Speed) > 2.5) Snd().Sfx(TEXT("hit"), FHKSfxOpts(FMath::Min(1.0, FMath::Abs(Speed) / 10)));
		Speed = 0;
	}
}

void FHKGroundVehicle::Place()
{
	// this.group.position.set(x, y, z);this.group.rotation.set(0, 0, 0);this.group.rotateY(yaw);rotateX(pitch);rotateZ(roll || 0);
	FHKNode* G = Rig.Root;
	G->SetPos(X, Y, Z);
	G->SetRotation(0, 0, 0);
	FQuat Q = G->Quat();
	Q = HKV::QMul(Q, HKV::AxisAngle(FVector(0, 1, 0), Yaw));
	Q = HKV::QMul(Q, HKV::AxisAngle(FVector(1, 0, 0), Pitch));
	Q = HKV::QMul(Q, HKV::AxisAngle(FVector(0, 0, 1), FMath::IsNaN(Roll) ? 0 : Roll));
	G->SetQuat(Q);
}

FVector2D FHKGroundVehicle::ExitPoint(double Side) const
{
	const FVector2D F = Forward();
	const double Rx = -F.Y, Rz = F.X, D = Width / 2 + .55;
	return FVector2D(X + Rx * D * Side, Z + Rz * D * Side);
}

TArray<FHKObstacle> FHKGroundVehicle::Footprint() const
{
	const FVector2D F = Forward();
	const double Rx = -F.Y, Rz = F.X, Hl = Length / 2, Hw = Width / 2;
	FHKObstacle O;
	for (const FVector2D& AB : {FVector2D(Hl, Hw), FVector2D(Hl, -Hw), FVector2D(-Hl, -Hw), FVector2D(-Hl, Hw)})
		O.Points.Add(FHKPt(X + F.X * AB.X + Rx * AB.Y, Z + F.Y * AB.X + Rz * AB.Y));
	O.MinY = Y - .2;
	O.MaxY = Y + Height;
	O.Kind = TEXT("vehicle");
	O.Prepare();
	return {O};
}

TArray<double> FHKGroundVehicle::NetGet() const
{
	TArray<double> A = {X, Y, Z, Yaw, Pitch, FMath::IsNaN(Roll) ? 0 : Roll, Speed};
	A.Append(NetExtra());
	for (double& V : A) V = HKV::RoundTo(V, 1000);
	return A;
}

void FHKGroundVehicle::NetSet(const TArray<double>& A)
{
	X = At(A, 0); Y = At(A, 1); Z = At(A, 2); Yaw = At(A, 3); Pitch = At(A, 4); Roll = At(A, 5); Speed = At(A, 6);
	TArray<double> Rest;
	for (int32 i = 7; i < A.Num(); i++) Rest.Add(A[i]);
	NetApply(Rest);
	Place();
}

FHKHipAnchor FHKGroundVehicle::HipAnchor(const FString& Seat)
{
	const FHip* H = Seat == TEXT("d") ? &HipD : Seat == TEXT("p") ? &HipP : nullptr;
	FHKHipAnchor R;
	if (!H || !H->bValid) return R;
	R.bValid = true;
	R.Obj = H->Obj ? H->Obj : Rig.Root;
	R.Pos = H->Pos;
	R.Stance = H->Stance;
	return R;
}

// ---------------------------------------------------------------------------------------------------------------------
// 2005 Honda CBF600S
// ---------------------------------------------------------------------------------------------------------------------
FHKMotorcycle::FHKMotorcycle(double InX, double InZ, double InYaw, uint32 InColor)
	: FHKGroundVehicle(InX, InZ, InYaw, .45, 2.1)
{
	Width = .8; Height = 1.3; Kind = TEXT("motorcycle"); Name = TEXT("Honda CBF600S"); Lean = 0; Bar = 0; Gear = 1; Rpm = 1300; Engine = nullptr; WheelSpin = 0;
	HipD = {true, nullptr, FVector(0, 1.0, .28), TEXT("ride")};
	HipP = {true, nullptr, FVector(0, 1.06, .66), TEXT("pillion")};
	PassengerEye = FVector(0, 1.74, .76);
	ColorHex = InColor; PaintColor = HKVehicles::ColorFromHex(InColor); bHasPaint = true;
	LoadRig(TEXT("motorcycle"));
	Body = N(TEXT("body")); Front = N(TEXT("front")); Fw = N(TEXT("fw")); Rw = N(TEXT("rw")); Steer = N(TEXT("steer")); Bars = N(TEXT("bars"));
	Hands = N(TEXT("hands")); Arms = N(TEXT("arms"));
	Hands->bVisible = false; Arms->bVisible = false;
	Settle(1); Place();
}

FHKSeatPose FHKMotorcycle::Seat()
{
	const FVector2D F = Forward();
	const double Up = 1.58;
	FHKSeatPose S;
	S.X = X - F.X * .36; S.Y = Y + Up * std::cos(Lean); S.Z = Z - F.Y * .36; S.Lateral = std::sin(Lean) * Up;
	S.Yaw = Yaw; S.Pitch = Pitch; S.Roll = Lean;
	return S;
}

void FHKMotorcycle::Enter()
{
	bActive = true; Hands->bVisible = true; Arms->bVisible = true;
	Snd().Sfx(TEXT("starter"));
	Engine = Snd().Engine(TEXT("bike"));
	Rpm = 1300;
}

FVector2D FHKMotorcycle::Exit()
{
	bActive = false; Hands->bVisible = false; Arms->bVisible = false;
	if (Engine) Engine->Stop();
	Engine = nullptr; Speed = 0; Lean = 0;
	return ExitPoint(-1);
}

void FHKMotorcycle::Update(double Dt, const FHKVehicleInput& In)
{
	if (bActive)
	{
		const double V = Speed, Wobble = In.Drunk ? std::sin(HKVehicles::NowMs() / 380) * .35 * In.Drunk : 0;
		const double Accel = In.Throttle * FMath::Max(1.2, 7.5 - V * .12) - In.Brake * 9 * HKV::Sign(V != 0 && !FMath::IsNaN(V) ? V : 1) - V * .02 - (V > 0 ? .25 : 0);
		// Holding L2 when (almost) stopped walks the bike backwards at about 1.5 m/s instead of braking against it.
		const bool bReversing = In.Brake > .5 && V < .3 && In.Throttle < .1;
		Speed = bReversing ? FMath::Max(-1.5, FMath::Min(0.0, V) - 2.5 * Dt) : FMath::Max(V > .3 ? 0.0 : -1.5, FMath::Min(55.0, V + Accel * Dt));
		const double SteerIn = FMath::Max(-1.0, FMath::Min(1.0, In.Steer + Wobble));
		if (FMath::Abs(Speed) < 4)
		{
			// Low speed: steer the bars directly.
			Bar += (SteerIn * .55 - Bar) * FMath::Min(1.0, Dt * 6);
			Yaw -= Speed * std::tan(Bar) / 1.48 * Dt;
			Lean += (Bar * Speed * .08 - Lean) * FMath::Min(1.0, Dt * 4);
		}
		else
		{
			// Speed: the stick sets the lean; the turn rate follows from lean and speed (counter-steer happens in the bars).
			const double TargetLean = SteerIn * .75;
			Lean += (TargetLean - Lean) * FMath::Min(1.0, Dt * 3.2);
			const double YawRate = 9.81 * std::tan(Lean) / Speed;
			Yaw -= YawRate * Dt;
			Bar += (std::atan(1.48 * YawRate / Speed) * .8 - (TargetLean - Lean) * .25 - Bar) * FMath::Min(1.0, Dt * 8);
		}
		Roll = -Lean;
		// Gearbox model for the engine note: six gears, change up at 10 000 rpm, down at 5 000.
		static const double Tops[6] = {55, 85, 115, 145, 175, 215};
		const double Kmh = FMath::Abs(Speed) * 3.6;
		if (Rpm > 10000 && Gear < 6) Gear++;
		else if (Rpm < 5000 && Gear > 1) Gear--;
		const double Target = FMath::Max(1300.0, 1300 + (Kmh / Tops[Gear - 1]) * 10200) + In.Throttle * (Kmh < 4 ? 3500 : 400);
		Rpm += (FMath::Min(11800.0, Target) - Rpm) * FMath::Min(1.0, Dt * 6);
		if (Engine) Engine->Set(Rpm, In.Throttle);
	}
	else
	{
		Speed *= FMath::Max(0.0, 1 - Dt * 3);
		Lean += (-.12 - Lean) * FMath::Min(1.0, Dt * 3);
		Roll = -Lean;
	}
	Move(Dt); Settle(Dt);
	WheelSpin += Speed * Dt / .3;
	Fw->SetRotX(-WheelSpin); Rw->SetRotX(-WheelSpin);
	Bars->SetRotY(-Bar); Front->SetRotY(-Bar * .9);
	Place();
}

void FHKMotorcycle::NetApply(const TArray<double>& A)
{
	Bar = At(A, 0); Lean = At(A, 1);
}

void FHKMotorcycle::NetAnimate(double Dt)
{
	WheelSpin += Speed * Dt / .3;
	Fw->SetRotX(-WheelSpin); Rw->SetRotX(-WheelSpin);
	Bars->SetRotY(-Bar); Front->SetRotY(-Bar * .9);
}

// ---------------------------------------------------------------------------------------------------------------------
// Hatchback car (left-hand drive)
// ---------------------------------------------------------------------------------------------------------------------
FHKCar::FHKCar(double InX, double InZ, double InYaw, int32 InModel)
	: FHKGroundVehicle(InX, InZ, InYaw, .95, 4.2)
{
	Model = InModel;
	Width = 1.8; Height = 1.45; Kind = TEXT("car"); Name = HKVehicles::CarName(InModel) + TEXT(" hatchback"); SteerAngle = 0; Rpm = 800; Engine = nullptr; WheelSpin = 0;
	HipD = {true, nullptr, FVector(-.37, .62, .28), TEXT("drive")};
	HipP = {true, nullptr, FVector(.37, .62, .28), TEXT("sit")};
	PassengerEye = FVector(.37, 1.24, .22);
	PaintColor = HKVehicles::ColorFromHex(HKVehicles::CarColor(InModel)); bHasPaint = true;
	LoadRig(TEXT("car"));
	Body = N(TEXT("body"));
	const bool Fronts[4] = {true, true, false, false};
	for (int32 i = 0; i < 4; i++) Wheels.Add({N(FString::Printf(TEXT("wheels[%d].holder"), i)), N(FString::Printf(TEXT("wheels[%d].wheel"), i)), Fronts[i]});
	WheelPivot = N(TEXT("wheelPivot")); SteeringWheel = N(TEXT("steeringWheel")); Hands = N(TEXT("hands")); Arms = N(TEXT("arms"));
	for (int32 i = 0; i < 2; i++) { Wrists.Add(N(FString::Printf(TEXT("wrists[%d]"), i))); Forearms.Add(N(FString::Printf(TEXT("forearms[%d]"), i))); }
	Hands->bVisible = false; Arms->bVisible = false;
	Settle(1); Place();
}

FHKSeatPose FHKCar::Seat()
{
	const FVector2D F = Forward();
	const double Rx = -F.Y, Rz = F.X;
	FHKSeatPose S;
	S.X = X + Rx * -.37 - F.X * .22; S.Y = Y + 1.24; S.Z = Z + Rz * -.37 - F.Y * .22;
	S.Yaw = Yaw; S.Pitch = Pitch; S.Roll = FMath::IsNaN(TerrainRoll) ? 0 : TerrainRoll;
	return S;
}

void FHKCar::Enter()
{
	bActive = true; Hands->bVisible = true; Arms->bVisible = true;
	Snd().Sfx(TEXT("carDoor")); Snd().Sfx(TEXT("starter"));
	Engine = Snd().Engine(TEXT("car"));
}

FVector2D FHKCar::Exit()
{
	bActive = false; Hands->bVisible = false; Arms->bVisible = false;
	if (Engine) Engine->Stop();
	Engine = nullptr; Speed = 0;
	Snd().Sfx(TEXT("carDoor"));
	return ExitPoint(-1);
}

void FHKCar::Update(double Dt, const FHKVehicleInput& In)
{
	if (bActive)
	{
		const double V = Speed, Wobble = In.Drunk ? std::sin(HKVehicles::NowMs() / 420) * .4 * In.Drunk : 0, Steer = FMath::Max(-1.0, FMath::Min(1.0, In.Steer + Wobble));
		const double ForwardAcc = In.Throttle * FMath::Max(1.0, 4.2 - FMath::Abs(V) * .08), Braking = In.Brake * (V > .3 ? 8 : 0), Reverse = In.Brake * (V <= .3 ? 2.2 : 0);
		Speed = FMath::Max(-5.0, FMath::Min(36.0, V + (ForwardAcc - Braking - Reverse - V * .03 - HKV::Sign(V) * .2) * Dt));
		if (FMath::Abs(Speed) < .05 && In.Throttle < .05 && In.Brake < .05) Speed = 0;
		SteerAngle += (Steer * .6 * FMath::Max(.35, 1 - FMath::Abs(V) / 30) - SteerAngle) * FMath::Min(1.0, Dt * 5);
		Yaw -= Speed * std::tan(SteerAngle) / 2.63 * Dt;
		// const target = 800 + Math.abs(this.speed) * 3.6 * 38 % 3600 + input.throttle * 1500;
		const double Target = 800 + std::fmod(FMath::Abs(Speed) * 3.6 * 38, 3600.0) + In.Throttle * 1500;
		Rpm += (FMath::Min(6200.0, Target) - Rpm) * FMath::Min(1.0, Dt * 5);
		if (Engine) Engine->Set(Rpm, In.Throttle);
	}
	else Speed *= FMath::Max(0.0, 1 - Dt * 4);
	Move(Dt); Settle(Dt); Roll = FMath::IsNaN(TerrainRoll) ? 0 : TerrainRoll;
	WheelSpin += Speed * Dt / .31;
	for (FWheel& Wh : Wheels) { Wh.Wheel->SetRotX(-WheelSpin); if (Wh.bFront) Wh.Holder->SetRotY(SteerAngle); }
	SteeringWheel->SetRotY(-SteerAngle * 1.6);
	Place();
	if (bActive) SolveArms();
}

void FHKCar::NetApply(const TArray<double>& A)
{
	SteerAngle = At(A, 0);
}

void FHKCar::NetAnimate(double Dt)
{
	WheelSpin += Speed * Dt / .31;
	for (FWheel& Wh : Wheels) { Wh.Wheel->SetRotX(-WheelSpin); if (Wh.bFront) Wh.Holder->SetRotY(SteerAngle); }
	SteeringWheel->SetRotY(-SteerAngle * 1.6);
}

void FHKCar::SolveArms()
{
	Rig.Root->UpdateMatrixWorld();
	const double L1 = .3, L2 = .27;
	for (int32 i = 0; i < Wrists.Num(); i++)
	{
		const double Side = i ? 1 : -1;
		const FVector S(-.37 + Side * .2, 1.05, .27), Wp = Body->WorldToLocal(Wrists[i]->GetWorldPosition());
		const FVector D = Wp - S, Dir = HKV::Norm(D);
		const double d = FMath::Min(L1 + L2 - .001, FMath::Max(.05, HKV::Len(D)));
		const double a = (L1 * L1 - L2 * L2 + d * d) / (2 * d), h = std::sqrt(FMath::Max(0.0, L1 * L1 - a * a));
		FVector Pole(Side, -1.3, .2);
		Pole = HKV::Norm(HKV::AddScaled(Pole, Dir, -HKV::Dot(Pole, Dir)));
		const FVector E = HKV::AddScaled(HKV::AddScaled(S, Dir, a), Pole, h), Fd = HKV::Norm(Wp - E);
		FHKNode* M = Forearms[i];
		M->Position = HKV::AddScaled(Wp, Fd, -.15);
		M->SetQuat(HKV::FromUnitVectors(FVector(0, 1, 0), Fd));
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Tracked excavator
// ---------------------------------------------------------------------------------------------------------------------
FHKExcavator::FHKExcavator(double InX, double InZ, double InYaw)
	: FHKGroundVehicle(InX, InZ, InYaw, 1.6, 4.4)
{
	Width = 2.5; Height = 3; Kind = TEXT("excavator"); Name = TEXT("Excavator"); Swing = 0; Boom = .35; StickA = -1.35; BucketA = -1.0; Load = 0; Engine = nullptr; Vl = 0; Vr = 0; TrackPhase = 0;
	LoadRig(TEXT("excavator"));
	Body = N(TEXT("body"));
	for (int32 i = 0; i < 2; i++) Tracks.Add(N(FString::Printf(TEXT("tracks[%d]"), i)));
	Upper = N(TEXT("upper")); Cab = N(TEXT("cab"));
	for (int32 i = 0; i < 2; i++)
	{
		Levers.Add(N(FString::Printf(TEXT("levers[%d]"), i)));
		// The gloved hand on each lever (hidden until someone drives): its meshes are the rig parts "levers[i]#k".
		TArray<FHKNode*> Parts;
		const FString Prefix = FString::Printf(TEXT("levers[%d]#"), i);
		for (const TUniquePtr<FHKNode>& Nd : Rig.All()) if (Nd->Name.StartsWith(Prefix)) Parts.Add(Nd.Get());
		LeverHands.Add(Parts);
	}
	BoomPivot = N(TEXT("boomPivot")); StickPivot = N(TEXT("stickPivot")); BucketPivot = N(TEXT("bucketPivot"));
	Dirt = N(TEXT("dirt")); Tip = N(TEXT("tip"));
	Dirt->SetScale(1.1, .55, .9); Dirt->bVisible = false;
	SetHandsVisible(false);
	// Hydraulic cylinders (rods follow the joints every frame).
	auto Ram = [this](FHKNode* PA, const FVector& A, FHKNode* PB, const FVector& B, int32 i)
	{
		Rams.Add({PA, A, PB, B, N(FString::Printf(TEXT("rams[%d].barrel"), i)), N(FString::Printf(TEXT("rams[%d].rod"), i))});
	};
	Ram(Upper, FVector(.25, .45, -.7), BoomPivot, FVector(0, .45, -1.4), 0);
	Ram(BoomPivot, FVector(0, .62, -1.9), StickPivot, FVector(0, .4, .25), 1);
	Ram(StickPivot, FVector(0, .25, -.3), BucketPivot, FVector(0, .15, .1), 2);
	Pose(); Settle(1); Place();
}

void FHKExcavator::SetHandsVisible(bool bV)
{
	for (TArray<FHKNode*>& Parts : LeverHands) for (FHKNode* P : Parts) P->bVisible = bV;
}

bool FHKExcavator::HandsVisible() const
{
	bool bAll = LeverHands.Num() > 0;
	for (const TArray<FHKNode*>& Parts : LeverHands) for (const FHKNode* P : Parts) bAll &= P->bVisible;
	return bAll;
}

void FHKExcavator::ShowDirt()
{
	Dirt->bVisible = Load > .15;
	Dirt->SetScale(1.1 * Load, .55 * Load, .9 * Load);
}

void FHKExcavator::Pose()
{
	Upper->SetRotY(Swing); BoomPivot->SetRotX(Boom); StickPivot->SetRotX(StickA); BucketPivot->SetRotX(BucketA);
	Rig.Root->UpdateMatrixWorld();
	const FHKMat Inv = Rig.Root->MatrixWorld.Inverse();
	for (FRam& R : Rams)
	{
		const FVector Pa = Inv.TransformPoint(R.ParentA->LocalToWorld(R.A)), Pb = Inv.TransformPoint(R.ParentB->LocalToWorld(R.B));
		FVector D = Pb - Pa;
		const double Len = HKV::Len(D);
		D = HKV::Norm(D);   // d.normalize() also changes d for the two lines below
		const FQuat Q = HKV::FromUnitVectors(FVector(0, 1, 0), D);
		R.Barrel->SetScale(1, Len * .55, 1); R.Barrel->Position = HKV::AddScaled(Pa, D, Len * .275); R.Barrel->SetQuat(Q);
		R.Rod->SetScale(1, Len * .5, 1); R.Rod->Position = HKV::AddScaled(Pb, D, -Len * .25); R.Rod->SetQuat(Q);
	}
}

FHKSeatPose FHKExcavator::Seat()
{
	Rig.Root->UpdateMatrixWorld();
	const FVector P = Cab->LocalToWorld(FVector(0, 1.22, .1));
	FHKSeatPose S;
	S.X = P.X; S.Y = P.Y; S.Z = P.Z; S.Yaw = Yaw + Swing; S.Pitch = Pitch; S.Roll = FMath::IsNaN(TerrainRoll) ? 0 : TerrainRoll;
	return S;
}

void FHKExcavator::Enter()
{
	bActive = true; SetHandsVisible(true);
	Snd().Sfx(TEXT("carDoor")); Snd().Sfx(TEXT("starter"));
	Engine = Snd().Engine(TEXT("excavator"));
}

FVector2D FHKExcavator::Exit()
{
	bActive = false; SetHandsVisible(false);
	if (Engine) Engine->Stop();
	Engine = nullptr; Vl = Vr = 0;
	Snd().Sfx(TEXT("carDoor"));
	return ExitPoint(-1);
}

void FHKExcavator::Update(double Dt, const FHKVehicleInput& In)
{
	double Flow = 0;
	if (bActive)
	{
		// Tracks: L1 left forward, L2 left back, R1 right forward, R2 right back.
		const double Tl = (In.L1 ? 1 : 0) - In.L2, Tr = (In.R1 ? 1 : 0) - In.R2, K = FMath::Min(1.0, Dt * 2.5);
		Vl += (Tl * 1.6 - Vl) * K; Vr += (Tr * 1.6 - Vr) * K;
		Speed = (Vl + Vr) / 2; Yaw += (Vl - Vr) / 2.0 * Dt * .9;
		// ISO joystick pattern: left stick left/right swings, forward pushes the stick (dipper) out, back pulls it in;
		// right stick forward lowers the boom, back raises it; right stick left curls the bucket in, right dumps it.
		auto Dz = [](double V) { return FMath::Abs(V) < .12 ? 0.0 : V; };
		const double SwingIn = Dz(In.LX), StickIn = Dz(In.LY), BoomIn = Dz(In.RY), BucketIn = Dz(In.RX);
		Swing -= SwingIn * .55 * Dt;
		StickA = FMath::Max(-2.7, FMath::Min(-.15, StickA - StickIn * .7 * Dt));
		Boom = FMath::Max(-1.05, FMath::Min(.95, Boom + BoomIn * .45 * Dt));
		BucketA = FMath::Max(-2.6, FMath::Min(.8, BucketA + BucketIn * 1.1 * Dt));
		Flow = FMath::Min(1.0, FMath::Abs(SwingIn) + FMath::Abs(StickIn) + FMath::Abs(BoomIn) + FMath::Abs(BucketIn));
		Levers[0]->SetRotation(StickIn * .35, 0, -SwingIn * .35); Levers[1]->SetRotation(-BoomIn * .35, 0, -BucketIn * .35);
		if (Engine)
		{
			Engine->Set(1000 + 1000 * FMath::Max(Flow, FMath::Abs(Speed) / 1.6), FMath::Max(Flow, FMath::Abs(Speed) / 1.6));
			Engine->Hydraulics(Flow);   // this.engine.hydraulics(flow)
		}
		LastFlow = Flow;
		Pose();
		// Digging: the bucket fills when its teeth are in the ground while curling in; dumping drops a pile.
		const FVector TipW = Tip->GetWorldPosition();
		const double GroundY = HeightAt(TipW.X, TipW.Z);
		if (TipW.Y < GroundY + .12 && BucketIn < -.2 && Load < 1)
		{
			Load = FMath::Min(1.0, Load + Dt * 1.8);
			if (HKVehicles::Random() < Dt * 6) Snd().Sfx(TEXT("scrape"), FHKSfxOpts(.5));
		}
		if (TipW.Y < GroundY - .45) { Boom = FMath::Min(.95, Boom + .5 * Dt); StickA = FMath::Max(-2.5, StickA - .3 * Dt); }
		if (Load > .25 && BucketA > .1)
		{
			Load = 0;
			Snd().Sfx(TEXT("dump"));
			if (OnDump) OnDump(TipW.X, TipW.Z);
		}
		ShowDirt();
	}
	else { Vl = Vr = Speed = 0; LastFlow = 0; }
	Move(Dt); Settle(Dt); Roll = FMath::IsNaN(TerrainRoll) ? 0 : TerrainRoll;
	TrackPhase += Dt;
	Tracks[0]->Position.Z = std::fmod(Vl * TrackPhase, .156);
	Tracks[1]->Position.Z = std::fmod(Vr * TrackPhase, .156);
	Place();
	if (!bActive) Pose();
}

FHKHipAnchor FHKExcavator::HipAnchor(const FString& Seat)
{
	FHKHipAnchor R;
	if (Seat != TEXT("d")) return R;
	R.bValid = true; R.Obj = Cab; R.Pos = FVector(0, .6, .28); R.Stance = TEXT("drive");
	return R;
}

void FHKExcavator::NetApply(const TArray<double>& A)
{
	Swing = At(A, 0); Boom = At(A, 1); StickA = At(A, 2); BucketA = At(A, 3); Load = At(A, 4); Vl = At(A, 5); Vr = At(A, 6);
	Pose();
	ShowDirt();
}

void FHKExcavator::NetAnimate(double Dt)
{
	TrackPhase += Dt;
	Tracks[0]->Position.Z = std::fmod(Vl * TrackPhase, .156);
	Tracks[1]->Position.Z = std::fmod(Vr * TrackPhase, .156);
}
