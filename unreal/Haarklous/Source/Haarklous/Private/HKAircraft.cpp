// Port of reference/web-game/game-aircraft.js.
#include "HKAircraft.h"
#include <limits>
#include "HKData.h"
#include "HKVehicleMath.h"

DEFINE_LOG_CATEGORY_STATIC(LogHKAircraft, Log, All);

namespace
{
	constexpr double G = 9.81;
	constexpr double RHO = 1.225;
	FHKAudio& Snd() { return FHKAudio::Get(); }
	double HeightAt(double X, double Z) { return FHKData::Get().GroundWorld(X, Z); }
	double At(const TArray<double>& A, int32 I) { return A.IsValidIndex(I) ? A[I] : 0; }
	const FVector XAxis(1, 0, 0), YAxis(0, 1, 0), ZAxis(0, 0, 1), NegZ(0, 0, -1);
	// Right hand around a vertical stick grip (stickGrip): only the meshes use it; nothing to do here.
}

// ---------------------------------------------------------------------------------------------------------------------
// class Craft
// ---------------------------------------------------------------------------------------------------------------------
FHKCraft::FHKCraft(double InX, double InZ, double InYaw, double InGearH)
{
	// this.group = new T.Group();this.gearH = gearH;this.home = {x, z, yaw};...this.aircraft = true;this.active = false;this.engine = null;this.arms = [];this.rpm = 0;
	GearH = InGearH; HomeX = InX; HomeZ = InZ; HomeYaw = InYaw;
	bAircraft = true; bActive = false; Rpm = 0;
}

void FHKCraft::LoadRig(const FString& RigName)
{
	FString Error;
	if (!Rig.LoadFromJson(FHKData::DataDir() / TEXT("rigs") / (RigName + TEXT(".json")), &Error)) UE_LOG(LogHKAircraft, Error, TEXT("%s"), *Error);
	Rig.Root->SetPos(0, 0, 0);
	Rig.Root->SetRotation(0, 0, 0);
	Rig.Root->SetScale(1, 1, 1);
	Rig.Root->bVisible = true;
}

FHKNode* FHKCraft::N(const FString& NodeName)
{
	if (FHKNode* Found = Rig.Find(NodeName)) return Found;
	UE_LOG(LogHKAircraft, Error, TEXT("%s rig has no node '%s'"), *Kind, *NodeName);
	return Rig.Create(NodeName, Rig.Root);
}

void FHKCraft::AddArm(FHKNode* Hand, const FString& WristName, const FString& MeshName, const FVector& Elbow)
{
	// addArm(handGroup, elbow): {wrist: handGroup.userData.wrist, elbow, mesh} (the mesh lives in armGroup)
	Arms.Add({N(WristName), Elbow, N(MeshName)});
}

void FHKCraft::Reset()
{
	Pos = FVector(HomeX, HeightAt(HomeX, HomeZ) + GearH, HomeZ); Vel = FVector::ZeroVector; W = FVector::ZeroVector;
	Q = HKV::Euler(0, HomeYaw, 0, TEXT("YXZ"));
	bGrounded = true; bBroken = false; CrashT = 0;
	PlaceCraft();
	SyncBase();
}

double FHKCraft::GetSpeed() const { return HKV::Len(Vel); }
double FHKCraft::GetYaw() const { return HKV::ToEuler(Q, TEXT("YXZ")).Y; }

FHKCraft::FAxes FHKCraft::Axes() const
{
	return {HKV::ApplyQ(NegZ, Q), HKV::ApplyQ(YAxis, Q), HKV::ApplyQ(XAxis, Q)};
}

FVector2D FHKCraft::CraftForward() const
{
	const FVector F = HKV::ApplyQ(NegZ, Q);
	double L = HKV::Hypot(F.X, F.Z);
	if (L == 0 || FMath::IsNaN(L)) L = 1;
	return FVector2D(F.X / L, F.Z / L);
}

void FHKCraft::Rotate(double Dt)
{
	Q = HKV::QNorm(HKV::QMul(Q, HKV::Euler(W.X * Dt, W.Y * Dt, W.Z * Dt, TEXT("XYZ"))));
}

void FHKCraft::Attitude(double& OutPitch, double& OutBank) const
{
	const FAxes A = Axes();
	OutPitch = std::asin(HKV::Clamp(A.F.Y, -1, 1));
	OutBank = -std::asin(HKV::Clamp(A.R.Y, -1, 1));
}

FVector FHKCraft::BodyVel() const { return HKV::ApplyQ(Vel, HKV::QInv(Q)); }

void FHKCraft::Crash()
{
	bBroken = true; CrashT = 3; Vel = FVector::ZeroVector; W = FVector::ZeroVector;
	Snd().Sfx(TEXT("crash"));
	if (OnCrash) OnCrash(this);
}

bool FHKCraft::Obstacles(const FVector& Prev, double Bottom)
{
	FHKData& Data = FHKData::Get();
	// Rotor strike (helicopter): anything inside the rotor disc at rotor height is a crash, whatever the speed.
	if (RotorR)
	{
		Rig.Root->Position = Pos; Rig.Root->SetQuat(Q); Rig.Root->UpdateMatrixWorld();
		const FVector C = Rotor->GetWorldPosition();
		if (Data.VehicleWorld(RotorR).HitsSolid(FHKPt(C.X, C.Z), C.Y - .25, C.Y + .25)) { Crash(); return true; }
	}
	if (!Data.VehicleWorld(Radius).HitsSolid(FHKPt(Pos.X, Pos.Z), Bottom + .35, Pos.Y + Top)) return false;
	if (GetSpeed() > CrashSpeed) { Crash(); return true; }
	Pos = Prev; Vel = HKV::Mul(Vel, -.3);
	Snd().Sfx(TEXT("hit"), FHKSfxOpts(.5));
	return false;
}

void FHKCraft::SolveArms()
{
	if (!bActive) return;
	Rig.Root->UpdateMatrixWorld();
	for (FArm& A : Arms)
	{
		const FVector Wp = Rig.Root->WorldToLocal(A.Wrist->GetWorldPosition()), D = HKV::Norm(Wp - A.Elbow);
		A.Mesh->Position = HKV::AddScaled(Wp, D, -.15);
		A.Mesh->SetQuat(HKV::FromUnitVectors(YAxis, D));
	}
}

void FHKCraft::PlaceCraft()
{
	Rig.Root->Position = Pos;
	Rig.Root->SetQuat(Q);
}

void FHKCraft::EnterCommon()
{
	bActive = true;
	for (FHKNode* H : Hands) H->bVisible = true;
	ArmGroup->bVisible = true;
}

FVector2D FHKCraft::ExitPoint() const
{
	const FVector2D F = CraftForward();
	const double Rx = -F.Y, Rz = F.X, D = ExitSide;
	return FVector2D(Pos.X - Rx * D + F.X * ExitFwd, Pos.Z - Rz * D + F.Y * ExitFwd);
}

void FHKCraft::SyncBase()
{
	X = Pos.X; Y = GetY(); Z = Pos.Z; Yaw = GetYaw(); Speed = GetSpeed();
}

void FHKCraft::Update(double Dt, const FHKVehicleInput& In)
{
	Step(Dt, In);
	SyncBase();
}

FHKSeatPose FHKCraft::Seat()
{
	const FVector P = HKV::ApplyQ(SeatLocal, Q) + Pos;
	FHKSeatPose S;
	S.X = P.X; S.Y = P.Y; S.Z = P.Z; S.Yaw = GetYaw();
	double Pt, Bk;
	Attitude(Pt, Bk);
	S.Pitch = Pt; S.Roll = Bk;
	return S;
}

FVector2D FHKCraft::Exit()
{
	bActive = false;
	for (FHKNode* H : Hands) H->bVisible = false;
	ArmGroup->bVisible = false;
	if (Engine) Engine->Stop();
	Engine = nullptr;
	return ExitPoint();
}

TArray<FHKObstacle> FHKCraft::Footprint() const
{
	const FVector2D F = CraftForward();
	const double Rx = -F.Y, Rz = F.X;
	TArray<FHKObstacle> Out;
	for (const FRect& R : Rects)
	{
		FHKObstacle O;
		for (const FVector2D& AB : {FVector2D(R.L / 2, R.Wd / 2), FVector2D(R.L / 2, -R.Wd / 2), FVector2D(-R.L / 2, -R.Wd / 2), FVector2D(-R.L / 2, R.Wd / 2)})
			O.Points.Add(FHKPt(Pos.X + F.X * (AB.X + R.A0) + Rx * (AB.Y + R.B0), Pos.Z + F.Y * (AB.X + R.A0) + Rz * (AB.Y + R.B0)));
		O.MinY = GetY() - .2;
		O.MaxY = GetY() + Height;
		O.Kind = TEXT("vehicle");
		O.Prepare();
		Out.Add(O);
	}
	return Out;
}

TArray<double> FHKCraft::NetGet() const
{
	TArray<double> A = {Pos.X, Pos.Y, Pos.Z, Q.X, Q.Y, Q.Z, Q.W, GetSpeed(), SpinValue(), FMath::IsNaN(Throttle) ? 0 : Throttle, bGrounded ? 1.0 : 0.0};
	for (double& V : A) V = HKV::RoundTo(V, 10000);
	return A;
}

void FHKCraft::NetSet(const TArray<double>& A)
{
	Pos = FVector(At(A, 0), At(A, 1), At(A, 2));
	Q = HKV::QNorm(FQuat(At(A, 3), At(A, 4), At(A, 5), At(A, 6)));
	Vel = HKV::Mul(HKV::ApplyQ(NegZ, Q), At(A, 7));
	SetSpin(At(A, 8));
	Throttle = At(A, 9);
	bGrounded = At(A, 10) != 0;
	bBroken = false;
	PlaceCraft();
	SyncBase();
}

FHKHipAnchor FHKCraft::HipAnchor(const FString& Seat)
{
	FHKHipAnchor R;
	if (Seat != TEXT("d") && Seat != TEXT("p")) return R;
	R.bValid = true; R.Obj = Rig.Root; R.Pos = Seat == TEXT("d") ? HipDPos : HipPPos; R.Stance = TEXT("sit");
	return R;
}

FHKCameraPose FHKCraft::CameraPose(bool bChase, const FVector* PrevChase)
{
	FHKCameraPose Out;
	if (!bChase)
	{
		Out.Position = HKV::ApplyQ(SeatLocal, Q) + Pos;
		Out.Quaternion = HKV::QMul(Q, HKV::Euler(SeatPitch, 0, 0));
		return Out;
	}
	const FVector2D F = CraftForward();
	const FVector Want(Pos.X - F.X * ChaseDist, Pos.Y + ChaseDist * .3, Pos.Z - F.Y * ChaseDist);
	FVector P = PrevChase ? HKV::Lerp(*PrevChase, Want, .12) : Want;
	const double Gh = HeightAt(P.X, P.Z) + 1;
	if (P.Y < Gh) P.Y = Gh;
	const FVector Target = HKV::AddScaled(Pos + FVector(0, 1.2, 0), FVector(F.X, 0, F.Y), 6);
	Out.Position = P;
	Out.Quaternion = HK::LookAtQuat(P, Target, YAxis, true);
	return Out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Helicopter (Airbus H125 / AS350 class)
// ---------------------------------------------------------------------------------------------------------------------
FHKHelicopter::FHKHelicopter(double InX, double InZ, double InYaw)
	: FHKCraft(InX, InZ, InYaw, 0)
{
	Kind = TEXT("helicopter"); Name = TEXT("Airbus H125 helicopter"); Length = 6; Width = 2.6; Height = 3.4;
	Rects = {{-.2, 0, 4.2, 2.6}, {-4.4, 0, 5.6, .7}}; Radius = 1.7; RotorR = 5.2; Top = 2.4; CrashSpeed = 9; ExitSide = 2.6; ExitFwd = 0;
	SeatLocal = FVector(.42, 1.86, -.32); SeatPitch = -.12; ChaseDist = 15; RotorAngle = 0;
	HipDPos = FVector(.42, 1.15, .25); HipPPos = FVector(-.42, 1.15, .25); PassengerEye = FVector(-.42, 1.86, -.32);
	LoadRig(TEXT("helicopter"));
	Cyclic = N(TEXT("cyclic")); Collective = N(TEXT("collective")); ArmGroup = N(TEXT("armGroup")); ArmGroup->bVisible = false;
	Hands = {N(TEXT("hands[0]")), N(TEXT("hands[1]"))};
	for (FHKNode* H : Hands) H->bVisible = false;
	AddArm(Hands[0], TEXT("arms[0].wrist"), TEXT("arms[0].mesh"), FVector(.62, 1.2, .1));
	AddArm(Hands[1], TEXT("arms[1].wrist"), TEXT("arms[1].mesh"), FVector(.05, 1.25, .45));
	Rotor = N(TEXT("rotor")); Disc = N(TEXT("disc")); TailRotor = N(TEXT("tailRotor"));
	BladeOpacity = 1; DiscOpacity = 0;
	Reset();
}

void FHKHelicopter::Enter()
{
	EnterCommon();
	Engine = Snd().Engine(TEXT("heli"));
	Snd().Sfx(TEXT("starter"));
}

void FHKHelicopter::Spin(double Dt)
{
	RotorAngle += Dt * 2 * HK::Pi * 6.4 * Rpm;
	Rotor->SetRotY(RotorAngle);
	TailRotor->SetRotX(RotorAngle * 5.1);
	const double B = HKV::Clamp((Rpm - .3) / .5, 0, 1);
	BladeOpacity = 1 - .82 * B;
	DiscOpacity = .13 * B;
}

void FHKHelicopter::Step(double Dt, const FHKVehicleInput& Inp)
{
	const bool bOn = bActive && !bBroken;
	Rpm += ((bOn ? 1 : 0) - Rpm) * FMath::Min(1.0, Dt * (bOn ? .35 : .18));
	if (Rpm > .002 || bActive) Spin(Dt);
	if (bBroken)
	{
		CrashT -= Dt;
		if (CrashT <= 0) Reset();
		if (Engine) Engine->Set(0, 0);
		return;
	}
	if (!bActive && bGrounded && Rpm < .01) return;
	const double lx = Inp.LX, ly = bOn ? Inp.LY : 1, rx = bOn ? Inp.RX : 0, ry = bOn ? Inp.RY : 0;
	const FVector Prev = Pos, U = Axes().U;
	FVector& V = Vel;
	const double Sp = HKV::Len(V), Gh = HeightAt(Pos.X, Pos.Z);
	// Collective: centre holds a hover in the air; on the ground you lift it a little to get light on the skids.
	const double Coll = HKV::Clamp(-ly - (bGrounded ? .18 : 0), -1, 1), Agl = Pos.Y - Gh;
	const double Lift = Rpm * Rpm * G * (Coll >= 0 ? 1 + Coll * .85 : 1 + Coll * .8) * (1 + .08 * FMath::Max(0.0, 1 - Agl / 4));
	FVector Acc = HKV::Mul(U, Lift);
	Acc.Y -= G;
	Acc = HKV::AddScaled(Acc, V, -(.00072 * Sp + .04));
	Acc.Y -= V.Y * .45;
	V = HKV::AddScaled(V, Acc, Dt);
	// Cyclic and pedals command rates; with the stick centred it gently levels itself. At speed the tail fin
	// weathervanes into the airflow and a bank turns the nose.
	double Pitch_, Bank;
	Attitude(Pitch_, Bank);
	const FVector Vb = BodyVel();
	const double Fwd = -Vb.Z;
	FVector Tw(ry * 1.25 - Pitch_ * 1.2 * (1 - FMath::Min(1.0, FMath::Abs(ry) * 2)), -lx * 1.3, -rx * 1.7 + Bank * 1.2 * (1 - FMath::Min(1.0, FMath::Abs(rx) * 2)));
	if (Fwd > 5) Tw.Y += -std::atan2(Vb.X, Fwd) * FMath::Min(1.0, Fwd / 30) * 1.2 - Bank * FMath::Min(1.0, Sp / 25) * .55;
	const bool bLight = Lift > G * .95;
	if (bGrounded && !bLight) Tw = FVector(0, bLight ? Tw.Y : 0, 0);
	W = HKV::Lerp(W, Tw, FMath::Min(1.0, Dt * 5));
	Rotate(Dt);
	Pos = HKV::AddScaled(Pos, V, Dt);
	const double G2 = HeightAt(Pos.X, Pos.Z);
	if (Pos.Y - GearH <= G2)
	{
		if (!bGrounded && (-V.Y > 7 || HKV::Hypot(V.X, V.Z) > 14 || Axes().U.Y < .8)) { Pos.Y = G2 + GearH; Crash(); return; }
		if (!bGrounded && -V.Y > 1) Snd().Sfx(TEXT("thud"), FHKSfxOpts(.5));
		Pos.Y = G2 + GearH;
		if (V.Y < 0) V.Y = 0;
		const double K = FMath::Max(0.0, 1 - Dt * (bLight ? 1 : 4));
		V.X *= K; V.Z *= K;
		bGrounded = true;
		if (!bLight)
		{
			const FVector E = HKV::ToEuler(Q, TEXT("YXZ"));
			Q = HK::Slerp(Q, HKV::Euler(0, E.Y, 0, TEXT("YXZ")), FMath::Min(1.0, Dt * 4));
		}
	}
	else if (Pos.Y - GearH > G2 + .08) bGrounded = false;
	if (Obstacles(Prev, FMath::Max(Pos.Y - GearH, G2))) return;
	PlaceCraft();
	Cyclic->SetRotation(ry * .22, 0, -rx * .22);
	Collective->SetRotX(HKV::Clamp(-ly, -1, 1) * .2);
	SolveArms();
	if (Engine) Engine->Set(Rpm, HKV::Clamp(Coll * .5 + .5, 0, 1));
}

FString FHKHelicopter::Hud() const
{
	const double Gh = HeightAt(Pos.X, Pos.Z);
	// `${Math.round(this.speed * 3.6)} km/h · alt ${Math.max(0, this.pos.y - gh).toFixed(0)} m · climb ${...}${this.vel.y.toFixed(1)} m/s · rotor ${Math.round(this.rpm * 100)} %${...}`
	return FString::Printf(TEXT("%s km/h · alt %s m · climb %s%s m/s · rotor %s %%%s"),
		*HKV::ToFixed(HKV::JsRound(GetSpeed() * 3.6), 0), *HKV::ToFixed(FMath::Max(0.0, Pos.Y - Gh), 0), Vel.Y >= 0 ? TEXT("+") : TEXT(""), *HKV::ToFixed(Vel.Y, 1),
		*HKV::ToFixed(HKV::JsRound(Rpm * 100), 0), Rpm < .95 && bActive ? TEXT(" (spooling up)") : TEXT(""));
}

// ---------------------------------------------------------------------------------------------------------------------
// Light sport plane
// ---------------------------------------------------------------------------------------------------------------------
FHKPlane::FHKPlane(double InX, double InZ, double InYaw)
	: FHKCraft(InX, InZ, InYaw, 1.13)
{
	Kind = TEXT("plane"); Name = TEXT("two-seat sport plane"); Length = 6.4; Width = 8; Height = 2.3;
	Rects = {{0, 0, 6.4, 1.2}, {.15, 0, 1.3, 8}}; Radius = 3.6; Top = 1.4; CrashSpeed = 9; ExitSide = 1.6; ExitFwd = -1.8;
	Mass = 700; S = 14; Tmax = 3600; Throttle = 0; Prop = 0;
	SeatLocal = FVector(0, .84, .3); SeatPitch = -.06; ChaseDist = 14;
	HipDPos = FVector(0, -.04, .45); HipPPos = FVector(0, -.02, 1.32); PassengerEye = FVector(0, .9, 1.22);
	LoadRig(TEXT("plane"));
	Ailerons = {N(TEXT("ailerons[0]")), N(TEXT("ailerons[1]"))};
	Elevator = N(TEXT("elevator")); Rudder = N(TEXT("rudder")); PropG = N(TEXT("propG")); PDisc = N(TEXT("pdisc"));
	Stick = N(TEXT("stick")); Lever = N(TEXT("lever")); ArmGroup = N(TEXT("armGroup")); ArmGroup->bVisible = false;
	Hands = {N(TEXT("hands[0]")), N(TEXT("hands[1]"))};
	for (FHKNode* H : Hands) H->bVisible = false;
	AddArm(Hands[0], TEXT("arms[0].wrist"), TEXT("arms[0].mesh"), FVector(.2, .2, .32));
	AddArm(Hands[1], TEXT("arms[1].wrist"), TEXT("arms[1].mesh"), FVector(-.3, .2, .38));
	BladeOpacity = 1; DiscOpacity = 0;
	Reset();
}

void FHKPlane::Reset()
{
	FHKCraft::Reset();
	Throttle = 0;
}

void FHKPlane::Enter()
{
	EnterCommon();
	Engine = Snd().Engine(TEXT("plane"));
	Snd().Sfx(TEXT("starter"));
	Prop = 700;
}

void FHKPlane::Spin(double Dt)
{
	PropG->SetRotZ(PropG->Rot().Z + Prop / 60 * 2 * HK::Pi * Dt * .5);
	const double Blur = HKV::Clamp((Prop - 500) / 900, 0, 1);
	BladeOpacity = 1 - .85 * Blur;
	DiscOpacity = .12 * Blur;
}

void FHKPlane::Step(double Dt, const FHKVehicleInput& Inp)
{
	const bool bOn = bActive && !bBroken;
	// Throttle lever: left stick up adds power, down takes it off, and it stays where you leave it (like an RC transmitter).
	Throttle = bOn ? HKV::Clamp(Throttle - Inp.LY * Dt * .7, 0, 1) : FMath::Max(0.0, Throttle - Dt);
	Prop += ((bOn ? 700 + 2050 * Throttle : 0) - Prop) * FMath::Min(1.0, Dt * 2);
	if (Prop > 5 || bActive) Spin(Dt);
	if (bBroken)
	{
		CrashT -= Dt;
		if (CrashT <= 0) Reset();
		if (Engine) Engine->Set(0, 0);
		return;
	}
	if (!bActive && bGrounded && GetSpeed() < .01 && Prop < 5) return;
	const double lx = bOn ? Inp.LX : 0, rx = bOn ? Inp.RX : 0, ry = bOn ? Inp.RY : 0, Brake = bOn ? Inp.Brake : 1;
	const FVector Prev = Pos;
	const FAxes Ax = Axes();
	const FVector F = Ax.F, U = Ax.U, R = Ax.R;
	FVector& V = Vel;
	const double Vs = HKV::Len(V);
	const FVector Vb = BodyVel();
	const double Alpha = Vs > 2 ? std::atan2(-Vb.Y, -Vb.Z) : 0, Beta = Vs > 2 ? std::atan2(Vb.X, -Vb.Z) : 0, QS = .5 * RHO * Vs * Vs * S;
	// Lift with a stall above about 15 degrees, induced drag, a side force that stops sideslip, and propeller thrust that fades with speed.
	const double A0 = .3, ClMax = .25 + 4.8 * A0;
	double CL = FMath::Abs(Alpha) < A0 ? .25 + 4.8 * Alpha : HKV::Sign(Alpha) * ClMax * FMath::Max(.45, 1 - (FMath::Abs(Alpha) - A0) * 2.5);
	if (Vb.Z > 0) CL = 0;
	const FVector Vhat = Vs > .1 ? HKV::Div(V, Vs) : F;
	FVector Fo = FVector::ZeroVector;
	Fo = HKV::AddScaled(Fo, HKV::Norm(HKV::Cross(R, Vhat)), QS * CL);
	Fo = HKV::AddScaled(Fo, Vhat, -QS * (.032 + .055 * CL * CL));
	Fo = HKV::AddScaled(Fo, R, -QS * 1.2 * Beta);
	Fo = HKV::AddScaled(Fo, F, Throttle * Tmax * FMath::Max(0.0, 1 - Vs / 95));
	FVector Acc = HKV::Div(Fo, Mass);
	Acc.Y -= G;
	if (bGrounded)
	{
		// Wheels: rolling resistance, brakes (L2), no sliding sideways.
		const FVector Hv(V.X, 0, V.Z);
		const double Hs = HKV::Len(Hv), Dec = (.25 + 4.5 * Brake) * Dt;
		if (Hs > 0) V = HKV::AddScaled(V, Hv, -FMath::Min(1.0, Dec / Hs));
		const FVector Side = HKV::Norm(FVector(R.X, 0, R.Z));
		V = HKV::AddScaled(V, Side, -HKV::Dot(V, Side) * FMath::Min(1.0, Dt * 8));
	}
	V = HKV::AddScaled(V, Acc, Dt);
	// Controls command pitch/roll/yaw rates (they get authority with airspeed); the plane weathervanes into the airflow.
	// The stick commands angle of attack: held back the plane keeps pulling up, centred it settles into gentle level flight.
	const double Auth = HKV::Clamp(Vs / 28, 0, 1), Elev = HKV::Clamp((Vs - 8) / 18, 0, 1), AlphaCmd = .045 + ry * (ry > 0 ? .3 : .2);
	FVector Tw((AlphaCmd - Alpha) * 3.2 * Auth + ry * .3 * Elev, (-lx * .5 - Beta * 2.5) * Auth, -rx * 2.6 * Elev);
	if (bGrounded)
	{
		double Pt, Bk;
		Attitude(Pt, Bk);
		const double Want = ry > .05 && Vs > 18 ? ry * .22 : 0;
		Tw.X = (Want - Pt) * 3;
		Tw.Y = -lx * HKV::Clamp(Vs / 5, 0, 1) * .55 * (Vs > 1 ? 1 : 0);
		Tw.Z = 0;
	}
	W = HKV::Lerp(W, Tw, FMath::Min(1.0, Dt * 6));
	Rotate(Dt);
	Pos = HKV::AddScaled(Pos, V, Dt);
	const double G2 = HeightAt(Pos.X, Pos.Z);
	if (Pos.Y - GearH <= G2)
	{
		if (!bGrounded && (-V.Y > 5 || Axes().U.Y < .85 || Vs > 55)) { Pos.Y = G2 + GearH; Crash(); return; }
		if (!bGrounded) Snd().Sfx(TEXT("thud"), FHKSfxOpts(.5));
		Pos.Y = G2 + GearH;
		if (V.Y < 0) V.Y = 0;
		bGrounded = true;
		FVector E = HKV::ToEuler(Q, TEXT("YXZ"));
		E.X = HKV::Clamp(E.X, 0, .22);
		E.Z = 0;
		Q = HKV::Euler(E.X, E.Y, E.Z, TEXT("YXZ"));
	}
	else if (Pos.Y - GearH > G2 + .12) bGrounded = false;
	if (Obstacles(Prev, FMath::Max(Pos.Y - GearH, G2))) return;
	// Leaving the area: a gentle turn back toward the neighbourhood.
	if (HKV::Hypot(Pos.X, Pos.Z) > 1700)
	{
		const double Want = std::atan2(Pos.X, Pos.Z), Yw = GetYaw(), D = std::atan2(std::sin(Want - Yw), std::cos(Want - Yw));
		W.Y += D * Dt * .5;
	}
	PlaceCraft();
	Stick->SetRotation(ry * .25, 0, -rx * .25);
	Lever->SetRotX(-Throttle * .6);
	Elevator->SetRotX(ry * .35); Rudder->SetRotY(-lx * .4); Ailerons[0]->SetRotX(-rx * .3); Ailerons[1]->SetRotX(rx * .3);
	SolveArms();
	if (Engine) Engine->Set(Prop, Throttle);
}

FString FHKPlane::Hud() const
{
	const double Gh = HeightAt(Pos.X, Pos.Z), Sp = GetSpeed();
	return FString::Printf(TEXT("%s km/h · alt %s m · throttle %s %% · climb %s%s m/s%s"),
		*HKV::ToFixed(HKV::JsRound(Sp * 3.6), 0), *HKV::ToFixed(FMath::Max(0.0, Pos.Y - GearH - Gh), 0), *HKV::ToFixed(HKV::JsRound(Throttle * 100), 0),
		Vel.Y >= 0 ? TEXT("+") : TEXT(""), *HKV::ToFixed(Vel.Y, 1), bGrounded && Sp < 26 && Throttle > .5 ? TEXT(" · rotate at 85 km/h") : TEXT(""));
}

// ---------------------------------------------------------------------------------------------------------------------
// FPV drone (5-inch quad, about 650 g)
// ---------------------------------------------------------------------------------------------------------------------
void FHKDrone::Launch(double X, double Y, double Z, double Yaw)
{
	Pos = FVector(X, Y + .06, Z); Home = Pos; Vel = FVector::ZeroVector; YawA = Yaw; PitchA = RollA = 0;
	Q = HKV::Euler(0, Yaw, 0, TEXT("YXZ"));
	Battery = 1; bArmed = true; bCrashed = false; Time = 0; bLanded = true;
	Snd().Sfx(TEXT("arm"));
	Engine = Snd().Engine(TEXT("drone"));
}

void FHKDrone::Stop()
{
	if (Engine) Engine->Stop();
	Engine = nullptr; bArmed = false;
}

void FHKDrone::SetMode(const FString& M)
{
	Mode = M;
	const FVector E = HKV::ToEuler(Q, TEXT("YXZ"));
	YawA = E.Y; PitchA = HKV::Clamp(E.X, -.8, .8); RollA = HKV::Clamp(-E.Z, -.8, .8);
}

void FHKDrone::Reset()
{
	bCrashed = false; Vel = FVector::ZeroVector; PitchA = RollA = 0;
	Q = HKV::Euler(0, YawA, 0, TEXT("YXZ"));
	bArmed = Battery > 0; bLanded = true;
	Snd().Sfx(TEXT("arm"));
}

void FHKDrone::Crash()
{
	bCrashed = true; Vel = FVector::ZeroVector;
	Snd().Sfx(TEXT("crash"), FHKSfxOpts(.45));
}

void FHKDrone::Update(double Dt, const FHKVehicleInput& Inp, const FHKDroneEnv& Env)
{
	if (bCrashed) { if (Engine) Engine->Set(0, 0); return; }
	const double lx = Inp.LX, ly = Inp.LY, rx = Inp.RX, ry = Inp.RY;
	Time += Dt;
	double Thr;
	if (Mode == TEXT("angle"))
	{
		YawA -= lx * 3.2 * Dt;
		PitchA += (ry * .75 - PitchA) * FMath::Min(1.0, Dt * 9);
		RollA += (rx * .75 - RollA) * FMath::Min(1.0, Dt * 9);
		Q = HKV::Euler(PitchA, YawA, -RollA, TEXT("YXZ"));
		// Left stick sets the climb rate (+-7 m/s); centred it holds altitude.
		Thr = HKV::Clamp(G + (-ly * 7 - Vel.Y) * 3, 0, 3.4 * G) / FMath::Max(.6, std::cos(PitchA) * std::cos(RollA));
	}
	else
	{
		Q = HKV::QNorm(HKV::QMul(Q, HKV::Euler(ry * 7 * Dt, -lx * 4.5 * Dt, -rx * 7 * Dt, TEXT("XYZ"))));
		Thr = G * FMath::Max(0.0, 1 + -ly * 2.4);
	}
	if (!bArmed) Thr = 0;
	Battery = FMath::Max(0.0, Battery - Dt / 300 * (.35 + .65 * HKV::Clamp(Thr / (2.5 * G), 0, 1)));
	if (Battery <= 0) bArmed = false;
	if (bLanded && Thr < G * 1.02) Thr = FMath::Min(Thr, G);
	Thrust = HKV::Clamp(Thr / (3.4 * G), 0, 1);
	const FVector U = HKV::ApplyQ(FVector(0, 1, 0), Q);
	FVector& V = Vel;
	const double Sp = HKV::Len(V);
	const FVector Prev = Pos;
	FVector Acc = HKV::Mul(U, Thr);
	Acc.Y -= G;
	Acc = HKV::AddScaled(Acc, V, -(.18 + .012 * Sp));
	V = HKV::AddScaled(V, Acc, Dt);
	Pos = HKV::AddScaled(Pos, V, Dt);
	// Ground, floors and ceilings (works indoors too), then walls, trees, buildings and vehicles.
	const TOptional<FHKDroneFloor> Fl = Env.Floor ? Env.Floor(Pos, Prev.Y + .05) : TOptional<FHKDroneFloor>();
	if (Fl.IsSet() && Pos.Y < Fl->Y + .05)
	{
		const bool bHard = -V.Y > 4.5 || HKV::Hypot(V.X, V.Z) > 7 || HKV::ApplyQ(FVector(0, 1, 0), Q).Y < .5;
		Pos.Y = Fl->Y + .05;
		if (bHard) { Crash(); return; }
		if (V.Y < 0) V.Y = 0;
		V.X *= .8; V.Z *= .8;
		bLanded = true;
		if (Mode == TEXT("acro"))
		{
			const FVector E = HKV::ToEuler(Q, TEXT("YXZ"));
			Q = HKV::Euler(0, E.Y, 0, TEXT("YXZ"));
		}
	}
	else if (!Fl.IsSet() || Pos.Y > Fl->Y + .1) bLanded = false;
	if (Fl.IsSet() && Fl->Ceiling < std::numeric_limits<double>::infinity() && Pos.Y > Fl->Y + Fl->Ceiling - .12)
	{
		Pos.Y = Fl->Y + Fl->Ceiling - .12;
		if (V.Y > 3.5) { Crash(); return; }
		V.Y = FMath::Min(0.0, V.Y);
	}
	if (Env.Solid && Env.Solid(Pos, Pos.Y - .06, Pos.Y + .08))
	{
		if (Sp > 3) { Crash(); return; }
		Pos = Prev;
		V = HKV::Mul(V, -.4);
	}
	if (Engine)
	{
		const FVector Dd = Pos - Env.Listener;
		const double D = Env.bHasListener ? std::sqrt(Dd.X * Dd.X + Dd.Y * Dd.Y + Dd.Z * Dd.Z) : 0;
		Engine->Set(bArmed ? Thrust : 0, HKV::Clamp(14 / (D + 6), .05, 1));
	}
}

FHKCameraPose FHKDrone::CameraPose() const
{
	const double Tilt = Mode == TEXT("acro") ? .45 : .2;
	FHKCameraPose Out;
	Out.Position = Pos + FVector(0, .03, 0);
	Out.Quaternion = HKV::QMul(Q, HKV::Euler(Tilt, 0, 0));
	return Out;
}

FHKDroneOsd FHKDrone::Osd(double GroundY) const
{
	const double Cells = 4, Volts = Cells * (3.5 + .7 * Battery);
	FHKDroneOsd O;
	O.Mode = Mode == TEXT("angle") ? TEXT("ANGLE") : TEXT("ACRO");
	O.Battery = (int32)HKV::JsRound(Battery * 100);
	O.Volts = HKV::ToFixed(Volts, 1);
	O.Alt = FMath::Max(0.0, Pos.Y - GroundY);
	O.Speed = HKV::Len(Vel) * 3.6;
	O.Time = Time;
	O.Dist = HKV::Hypot(Pos.X - Home.X, Pos.Z - Home.Z);
	O.bArmed = bArmed; O.bCrashed = bCrashed; O.Thrust = Thrust;
	return O;
}

TOptional<FHKDroneFloor> FHKDrone::FloorBelow(const FHKWalkWorld& World, const FVector& P, double Y)
{
	// for (const f of walkWorld.floors) {const b = f.bounds;if (outside b || !inside(p, f.points)) continue;const h = f.surface(p);if (h <= y + .05 && (!best || h > best.y)) best = {y: h, ceiling: f.ceilingHeight ?? Infinity};}
	TOptional<FHKDroneFloor> Best;
	for (const FHKFloor& F : World.Floors)
	{
		const FHKBounds& B = F.Bounds;
		if (P.X < B.MinX || P.X > B.MaxX || P.Z < B.MinZ || P.Z > B.MaxZ) continue;
		// game-main.js inside(p, pts) (even-odd, no edge tolerance)
		bool bIn = false;
		const TArray<FHKPt>& Pts = F.Points;
		for (int32 i = 0, j = Pts.Num() - 1; i < Pts.Num(); j = i++)
		{
			const FHKPt& A = Pts[i];
			const FHKPt& Bp = Pts[j];
			if ((A.Z > P.Z) != (Bp.Z > P.Z) && P.X < (Bp.X - A.X) * (P.Z - A.Z) / (Bp.Z - A.Z) + A.X) bIn = !bIn;
		}
		if (!bIn) continue;
		const double H = F.Surface(FHKPt(P.X, P.Z));
		if (H <= Y + .05 && (!Best.IsSet() || H > Best->Y)) { FHKDroneFloor Fl; Fl.Y = H; Fl.Ceiling = F.CeilingHeight; Best = Fl; }
	}
	return Best;
}
