// Construction-site machines (new feature; see HKSiteVehicles.h).
#include "HKSiteVehicles.h"
#include "HKData.h"
#include "HKSite.h"
#include "HKVehicleMath.h"

FHKDirtField* FHKSiteMachine::Site = nullptr;

namespace
{
	FHKAudio& Snd() { return FHKAudio::Get(); }
	double Ground(double X, double Z) { return FHKData::Get().GroundWorld(X, Z); }
	double At(const TArray<double>& A, int32 I) { return A.IsValidIndex(I) && !FMath::IsNaN(A[I]) ? A[I] : 0; }
	// The sticks already have the controller's dead zone (HKInput); this only drops noise so fine control stays possible.
	double Dz(double V, double D = .03) { return FMath::Abs(V) < D ? 0.0 : V; }
	const double G = 9.81, SoilDensity = 1700;   // kg/m3 of loose soil

	template<typename TRam>
	void PoseRams(FHKNode* Root, TArray<TRam>& Rams)
	{
		Root->UpdateMatrixWorld();
		const FHKMat Inv = Root->MatrixWorld.Inverse();
		for (TRam& R : Rams)
		{
			const FVector Pa = Inv.TransformPoint(R.PA->LocalToWorld(R.A)), Pb = Inv.TransformPoint(R.PB->LocalToWorld(R.B));
			FVector D = Pb - Pa;
			const double Len = HKV::Len(D);
			D = HKV::Norm(D);
			const FQuat Q = HKV::FromUnitVectors(FVector(0, 1, 0), D);
			R.Barrel->SetScale(1, Len * .58, 1); R.Barrel->Position = HKV::AddScaled(Pa, D, Len * .29); R.Barrel->SetQuat(Q);
			R.Rod->SetScale(1, Len * .5, 1); R.Rod->Position = HKV::AddScaled(Pb, D, -Len * .25); R.Rod->SetQuat(Q);
		}
	}

	// A node's local direction in world space (the rig's world matrices must be up to date).
	FVector WorldDir(FHKNode* N, const FVector& Local)
	{
		return HKV::Norm(N->LocalToWorld(Local) - N->LocalToWorld(FVector::ZeroVector));
	}

	FHKDirtField::FTool Tool(const FVector& C, const FVector2D& F, double HalfL, double HalfW, double Bottom)
	{
		FHKDirtField::FTool T;
		T.C = FVector2D(C.X, C.Z); T.F = F.GetSafeNormal(); T.HalfL = HalfL; T.HalfW = HalfW; T.Bottom = Bottom;
		return T;
	}

	// Place a hook / wire pair (nodes under the rig root) for a hook at world point H hanging from Top.
	void PoseHook(FHKNode* Root, FHKNode* HookNode, FHKNode* Wire, const FVector& Top, const FVector& H)
	{
		Root->UpdateMatrixWorld();
		const FHKMat Inv = Root->MatrixWorld.Inverse();
		const FVector Lh = Inv.TransformPoint(H), Lt = Inv.TransformPoint(Top);
		HookNode->Position = Lh;
		HookNode->SetQuat(HKV::QInv(Root->GetWorldQuaternion()));     // the hook hangs upright
		const FVector A = Lh + FVector(0, .45, 0);
		FVector D = Lt - A;
		const double L = FMath::Max(.01, HKV::Len(D));
		Wire->Position = A;
		Wire->SetQuat(HKV::FromUnitVectors(FVector(0, 1, 0), HKV::Norm(D)));
		Wire->SetScale(1, L, 1);
	}
}

TArray<FHKSiteMachine*>& FHKSiteMachine::All() { static TArray<FHKSiteMachine*> A; return A; }
TArray<FHKSiteLoad*>& FHKSiteMachine::Loads() { static TArray<FHKSiteLoad*> A; return A; }

FHKSiteMachine::FHKSiteMachine(double InX, double InZ, double InYaw, double InRadius, double InLength, const TCHAR* InRig, const TCHAR* InKind, const TCHAR* InName)
	: FHKGroundVehicle(InX, InZ, InYaw, InRadius, InLength)
{
	Kind = InKind; Name = InName; RigFile = InRig; VerbText = TEXT("Climb into the");
	LoadRig(InRig);
	Body = Rig.Root;
	All().Add(this);
}

void FHKSiteMachine::PourSoil(const FVector& At, double Volume, double Spread)
{
	// At: where the soil leaves the bucket or body (a truck under it catches it; otherwise it lands on the ground below).
	if (Volume <= 0) return;
	for (FHKSiteMachine* M : All())
		if (M != this) { Volume -= M->AcceptSoil(At, Volume); if (Volume <= 1e-6) return; }
	if (Site && Site->Contains(At.X, At.Z, .5))
	{
		FHKDirtField::FTool T = Tool(At, Forward(), Spread, Spread, 0);
		Site->Deposit(T, Volume);
		return;
	}
	if (OnSpill) OnSpill(At.X, At.Z, Volume);
}

FHKSeatPose FHKSiteMachine::Seat()
{
	Rig.Root->UpdateMatrixWorld();
	const FVector P = (SeatNode ? SeatNode : Rig.Root)->LocalToWorld(SeatOffset);
	FHKSeatPose S;
	S.X = P.X; S.Y = P.Y; S.Z = P.Z; S.Yaw = Yaw; S.Pitch = Pitch; S.Roll = FMath::IsNaN(TerrainRoll) ? 0 : TerrainRoll;
	return S;
}

void FHKSiteMachine::Enter()
{
	bActive = true;
	Snd().Sfx(TEXT("carDoor")); Snd().Sfx(TEXT("starter"));
	Engine = Snd().Engine(TEXT("diesel"));
}

FVector2D FHKSiteMachine::Exit()
{
	bActive = false;
	if (Engine) Engine->Stop();
	Engine = nullptr;
	Snd().Sfx(TEXT("carDoor"));
	return ExitPoint(-1);
}

FString FHKSiteMachine::Hud() const
{
	return FString::Printf(TEXT("%d km/h%s"), (int32)HKV::JsRound(FMath::Abs(Speed) * 3.6), *GearText());
}

FString FHKSiteMachine::GearText() const
{
	if (bTracked) return FString::Printf(TEXT(" · %s"), Speed < -.05 ? TEXT("R") : TEXT("F"));
	return FString::Printf(TEXT(" · gear %s%d · %d rpm"), Speed < -.05 ? TEXT("R") : TEXT(""), FMath::Max(1, Gear), (int32)(HKV::JsRound(Rpm / 50) * 50));
}

double FHKSiteMachine::GroundResistance() const
{
	// Rolling resistance coefficients: loose soil, grass, gravel, asphalt.
	if (Site && Site->Contains(X, Z)) return Site->LooseAt(X, Z) > .15 ? .12 : .07;
	const FString S = FHKData::Get().SurfaceAt(X, Z);
	return S == TEXT("asphalt") ? .015 : S == TEXT("gravel") ? .035 : S == TEXT("sand") ? .1 : .05;
}

void FHKSiteMachine::Drive(double Dt, double Fwd, double Back, double ExtraResist)
{
	// Wheeled machine: power-limited traction up to the grip limit; brakes; rolling resistance; the slope.
	const double M = MassKg + Payload * SoilDensity, V = Speed, AV = FMath::Abs(V);
	const double Pull = FMath::Min(Grip * M * G, PowerKw * 1000 / FMath::Max(AV, 1.2));
	double A = -G * FMath::Sin(Pitch);                                         // the slope (nose up pulls back)
	if (Fwd > .02 && V > -.3) A += Fwd * Pull / M;
	else if (Back > .02 && V < .3) A -= Back * Pull / M * .7;                  // reverse
	if (Back > .02 && V > .3) A -= Back * .5 * G;                                 // service brakes
	if (Fwd > .02 && V < -.3) A += Fwd * .5 * G;
	const double Resist = (GroundResistance() + ExtraResist) * G + (Fwd < .02 ? .25 : 0);   // + engine braking
	if (AV > .03) A -= HKV::Sign(V) * FMath::Min(Resist, AV / Dt);
	Speed = HKV::Clamp(V + A * Dt, -VRev, VMax);
	// Parked: no pedal and nearly still -> the brakes hold.
	if (Fwd < .02 && Back < .02 && FMath::Abs(Speed) < .15) Speed = 0;
	// Automatic gearbox (only for the engine speed and the sound).
	const double Frac = FMath::Abs(Speed) / VMax;
	Gear = FMath::Clamp(1 + (int32)(Frac * Gears * .999), 1, Gears);
	const double InGear = Frac * Gears - (Gear - 1);
	Rpm = 700 + InGear * 1300 + FMath::Max(Fwd, Back) * 250;
	EngineLoad = FMath::Max(Fwd, Back);
}

void FHKSiteMachine::Hydrostatic(double Dt, double Target, double Accel)
{
	// Tracks: the levers set the speed; the pump slows the machine on a climb.
	const double Up = FMath::Max(0.0, FMath::Sin(Pitch) * HKV::Sign(Target));
	Target *= 1 - FMath::Min(.6, Up * 1.6);
	Speed += HKV::Clamp(Target - Speed, -Accel * 1.6 * Dt, Accel * Dt);
	if (FMath::Abs(Target) < .01 && FMath::Abs(Speed) < .05) Speed = 0;
	Rpm = 900 + FMath::Abs(Target) / FMath::Max(.1, VMax) * 1200;
}

void FHKSiteMachine::SettleBig(double Dt)
{
	const FVector2D F = Forward();
	const double Rx = -F.Y, Rz = F.X;
	const double Hf = Ground(X + F.X * HalfL, Z + F.Y * HalfL), Hb = Ground(X - F.X * HalfL, Z - F.Y * HalfL);
	const double Hl = Ground(X - Rx * HalfW, Z - Rz * HalfW), Hr = Ground(X + Rx * HalfW, Z + Rz * HalfW), Hc = Ground(X, Z);
	// The machine rests on the higher of its centre and the average of its ends (it bridges small holes).
	Y = FMath::Max((Hf + Hb) / 2, Hc - .15);
	const double K = FMath::Min(1.0, Dt * 4);
	Pitch += (std::atan2(Hf - Hb, 2 * HalfL) - Pitch) * K;
	TerrainRoll += (std::atan2(Hl - Hr, 2 * HalfW) - TerrainRoll) * K;
	Roll = TerrainRoll;
}

void FHKSiteMachine::EngineSound(double InRpm, double InLoad)
{
	if (Engine) Engine->Set(InRpm, InLoad);
}

void FHKSiteMachine::SpinWheels(const TArray<FHKNode*>& W, double WheelR, double Dt)
{
	WheelAngle -= Speed / WheelR * Dt;
	for (FHKNode* Wn : W) Wn->SetRotX(WheelAngle);
}

// ---------------------------------------------------------------------------------------------------------------------
// Dozer
FHKDozer::FHKDozer(double InX, double InZ, double InYaw)
	: FHKSiteMachine(InX, InZ, InYaw, 1.6, 5.2, TEXT("site_dozer"), TEXT("dozer"), TEXT("Dozer"))
{
	Width = 3.6; Height = 3.2; MassKg = 21000; VMax = 2.9; VRev = 3.3; bTracked = true; HalfL = 1.3; HalfW = 1.0;
	Arm = N(TEXT("bladeArm")); Blade = N(TEXT("blade")); Edge = N(TEXT("bladeEdge"));
	Tracks = {N(TEXT("tracks[0]")), N(TEXT("tracks[1]"))};
	// Lift cylinders: from beside the radiator down to the back of the blade.
	for (int32 i = 0; i < 2; i++)
	{
		const double Sx = i ? 1 : -1;
		Rams.Add({Rig.Root, FVector(Sx * .8, 1.6, -1.6), Blade, FVector(Sx * .8, .8, .3), N(FString::Printf(TEXT("rams[%d].barrel"), i)), N(FString::Printf(TEXT("rams[%d].rod"), i))});
	}
	SeatNode = Rig.Root; SeatOffset = FVector(0, 2.68, .45); CamPitch = -.2;   // forward and up: the blade shows over the hood
	bRightStickTool = true;
	HelpText = TEXT("Dozer · R2 forward, L2 reverse, left stick steer · right stick back / forward: blade up / down, ←→ tilt · R3 view · □ get out");
	Capacity = 4.0;
	Pose(); Place();
}

void FHKDozer::Pose()
{
	Arm->SetRotX(Lift);
	Blade->SetRotZ(Tilt);
	Edge->SetRotZ(Tilt);
	PoseRams(Rig.Root, Rams);
}

void FHKDozer::Update(double Dt, const FHKVehicleInput& In)
{
	double Target = 0;
	if (bActive)
	{
		Lift = HKV::Clamp(Lift + Dz(In.RY) * .35 * Dt, -.16, .5);              // stick back raises the blade
		Tilt = HKV::Clamp(Tilt + Dz(In.RX) * .25 * Dt, -.14, .14);
		Target = In.Throttle * VMax - In.Brake * VRev;
		Steer = In.Steer;
	}
	else Steer = 0;
	Pose();
	Rig.Root->UpdateMatrixWorld();
	// Blade: below the surface while moving forward it cuts what is above its edge and pushes it into a heap in front.
	const FVector E = Edge->GetWorldPosition();
	const FVector2D F = Forward();
	double SurfaceAtEdge = Ground(E.X, E.Z);
	Ahead = 0;
	double Resist = 0;
	if (Site && Site->Contains(E.X, E.Z, 1.5))
	{
		// What is piled in front of the blade (for the speed and the read-out).
		const double Sum = Site->VolumeAbove(Tool(E + FVector(F.X, 0, F.Y) * .55, F, .55, 1.75, E.Y + .05));
		Ahead = Sum;
		if (Speed > .05 && E.Y < SurfaceAtEdge + .03)
		{
			const double Step = FMath::Max(.12, Speed * Dt);
			const double Cut = Site->Cut(Tool(E - FVector(F.X, 0, F.Y) * (Step / 2), F, Step / 2 + .1, 1.78, E.Y), 2.5 * Dt + Speed * Dt * 1.8);
			if (Cut > 0) Site->Deposit(Tool(E + FVector(F.X, 0, F.Y) * .4, F, .3, 1.7, 0), Cut);
		}
		Resist = FMath::Min(.55, Ahead / 9.0) + (E.Y < SurfaceAtEdge - .25 ? .4 : 0);
	}
	const double Turn = FMath::Abs(Steer);
	Hydrostatic(Dt, Target * (1 - Resist) * (1 - Turn * .35), 1.0);
	Yaw -= Steer * (.42 + .18 * FMath::Abs(Speed) / VMax) * Dt;
	Move(Dt); SettleBig(Dt);
	TrackPhase += Dt;
	const double Vl = Speed + Steer * .6, Vr = Speed - Steer * .6;
	Tracks[0]->Position.Z = std::fmod(Vl * TrackPhase, .2); Tracks[1]->Position.Z = std::fmod(Vr * TrackPhase, .2);
	Place();
	EngineSound(bActive ? Rpm + Resist * 400 : 0, bActive ? FMath::Max(Resist, FMath::Abs(Target) / VMax) : 0);
	Payload = Ahead;
}

void FHKDozer::NetApply(const TArray<double>& A) { Lift = At(A, 0); Tilt = At(A, 1); Pose(); }

FString FHKDozer::Hud() const
{
	return FString::Printf(TEXT("%s km/h%s · blade %s m · pushing %s m³"), *HKV::ToFixed(FMath::Abs(Speed) * 3.6, 1), *GearText(),
		*HKV::ToFixed(Lift * 1.9, 1), *HKV::ToFixed(Ahead, 1));
}

// ---------------------------------------------------------------------------------------------------------------------
// Excavator
FHKSiteExcavator::FHKSiteExcavator(double InX, double InZ, double InYaw)
	: FHKSiteMachine(InX, InZ, InYaw, 1.6, 4.9, TEXT("site_excavator"), TEXT("excavator30"), TEXT("Excavator (30 t)"))
{
	Width = 3.2; Height = 3.2; MassKg = 30000; VMax = 1.5; VRev = 1.5; bTracked = true; HalfL = 1.9; HalfW = 1.3;
	Tracks = {N(TEXT("tracks[0]")), N(TEXT("tracks[1]"))};
	Upper = N(TEXT("upper")); Cab = N(TEXT("cab"));
	BoomPivot = N(TEXT("boomPivot")); StickPivot = N(TEXT("stickPivot")); BucketPivot = N(TEXT("bucketPivot"));
	Tip = N(TEXT("tip")); Dirt = N(TEXT("dirt"));
	auto Ram = [this](FHKNode* PA, const FVector& A, FHKNode* PB, const FVector& B, int32 i)
	{
		Rams.Add({PA, A, PB, B, N(FString::Printf(TEXT("rams[%d].barrel"), i)), N(FString::Printf(TEXT("rams[%d].rod"), i))});
	};
	Ram(Upper, FVector(.15, .45, -.55), BoomPivot, FVector(0, -.25, -2.3), 0);
	Ram(BoomPivot, FVector(0, .75, -2.4), StickPivot, FVector(0, .55, .55), 1);
	Ram(StickPivot, FVector(0, .3, -.55), BucketPivot, FVector(0, .3, .2), 2);
	SeatNode = Cab; SeatOffset = FVector(0, 1.3, .2); CamPitch = -.3; CamYaw = -.12;
	bRightStickTool = true;
	HelpText = TEXT("Excavator · tracks: L1/L2 left, R1/R2 right · left stick: swing / stick · right stick: boom / bucket · R3 view · □ get out");
	Capacity = 1.9;
	Pose(); Place();
}

void FHKSiteExcavator::Pose()
{
	Upper->SetRotY(Swing); BoomPivot->SetRotX(Boom); StickPivot->SetRotX(StickA); BucketPivot->SetRotX(BucketA);
	PoseRams(Rig.Root, Rams);
	Dirt->bVisible = Payload > .1;
	const double S = FMath::Max(.15, FMath::Min(1.0, Payload / Capacity));
	Dirt->SetScale(S, S, S);
}

FHKSeatPose FHKSiteExcavator::Seat()
{
	FHKSeatPose S = FHKSiteMachine::Seat();
	S.Yaw = Yaw + Swing;
	return S;
}

void FHKSiteExcavator::Update(double Dt, const FHKVehicleInput& In)
{
	double Flow = 0, SwingIn = 0, StickIn = 0, BoomIn = 0, BucketIn = 0;
	if (bActive)
	{
		const double Tl = (In.L1 ? 1 : 0) - In.L2, Tr = (In.R1 ? 1 : 0) - In.R2, K = FMath::Min(1.0, Dt * 1.6);
		Vl += (Tl * VMax - Vl) * K; Vr += (Tr * VMax - Vr) * K;
		SwingIn = Dz(In.LX); StickIn = Dz(In.LY); BoomIn = Dz(In.RY); BucketIn = Dz(In.RX);
		Swing -= SwingIn * .65 * Dt;
		StickA = HKV::Clamp(StickA - StickIn * .55 * Dt, -2.65, -.3);
		Boom = HKV::Clamp(Boom + BoomIn * .38 * Dt, -.95, 1.05);
		BucketA = HKV::Clamp(BucketA + BucketIn * .95 * Dt, -2.7, .9);
		Flow = FMath::Min(1.0, FMath::Abs(SwingIn) + FMath::Abs(StickIn) + FMath::Abs(BoomIn) + FMath::Abs(BucketIn));
	}
	else { Vl = Vr = 0; }
	Speed = (Vl + Vr) / 2;
	Yaw += (Vl - Vr) / 2.6 * Dt;
	Pose();
	Rig.Root->UpdateMatrixWorld();
	const FVector TipW = Tip->GetWorldPosition();
	const FVector Moved = TipW - LastTip;
	LastTip = TipW;
	// Digging: teeth below the soil while the bucket curls, the stick pulls in (stick back) or the boom breaks out.
	const double GroundY = Ground(TipW.X, TipW.Z);
	if (Site && Site->Contains(TipW.X, TipW.Z, .8) && TipW.Y < GroundY - .02 && Payload < Capacity && (BucketIn < -.15 || StickIn > .15 || BoomIn > .15))
	{
		const FVector2D Dir = FVector2D(Moved.X, Moved.Z).Size() > .002 ? FVector2D(Moved.X, Moved.Z) : FVector2D(-FMath::Sin(Yaw + Swing), -FMath::Cos(Yaw + Swing));
		const double Cut = Site->Cut(Tool(TipW, Dir, .3 + FVector2D(Moved.X, Moved.Z).Size(), .72, TipW.Y), FMath::Min(Capacity - Payload, 1.4 * Dt));
		Payload += Cut;
		if (Cut > 0 && HKVehicles::Random() < Dt * 5) Snd().Sfx(TEXT("scrape"), FHKSfxOpts(.45));
	}
	// Deep in the ground with a full bucket the boom cannot go further down (like the web excavator).
	if (TipW.Y < GroundY - .5) { Boom = FMath::Min(1.05, Boom + .3 * Dt); }
	// Spilling: the bucket's opening (its local -y: toward the machine when the teeth point down, up when it is curled)
	// turned below the horizontal pours the soil out, faster the steeper it is; not while the bucket is in the ground.
	if (Payload > 0 && TipW.Y > GroundY + .1)
	{
		const FVector Mouth = WorldDir(BucketPivot, FVector(0, -.91, -.42));
		const double Rate = 4.0 * FMath::Clamp((-.12 - Mouth.Y) / .6, 0.0, 1.0);
		if (Rate > 0)
		{
			const double Out = FMath::Min(Payload, Rate * Dt);
			Payload -= Out;
			PourSoil(BucketPivot->LocalToWorld(FVector(0, .35, -.8)), Out, .7);
			if (HKVehicles::Random() < Dt * 3) Snd().Sfx(TEXT("dump"), FHKSfxOpts(.4));
		}
	}
	Move(Dt); SettleBig(Dt);
	TrackPhase += Dt;
	Tracks[0]->Position.Z = std::fmod(Vl * TrackPhase, .17); Tracks[1]->Position.Z = std::fmod(Vr * TrackPhase, .17);
	Place();
	Pose();
	if (Engine) { Engine->Set(950 + 1150 * FMath::Max(Flow, FMath::Abs(Speed) / VMax), FMath::Max(Flow, FMath::Abs(Speed) / VMax)); Engine->Hydraulics(Flow); }
}

void FHKSiteExcavator::NetApply(const TArray<double>& A)
{
	Swing = At(A, 0); Boom = At(A, 1); StickA = At(A, 2); BucketA = At(A, 3); Payload = At(A, 4);
	Pose();
}

FString FHKSiteExcavator::Hud() const
{
	return FString::Printf(TEXT("%s km/h · bucket %s / %s m³"), *HKV::ToFixed(FMath::Abs(Speed) * 3.6, 1), *HKV::ToFixed(Payload, 1), *HKV::ToFixed(Capacity, 1));
}

// ---------------------------------------------------------------------------------------------------------------------
// Articulated steering
void FHKArticulation::Init(double HX, double HZ, double YawRear, double InPhi)
{
	Phi = InPhi; YawB = YawRear; YawA = YawRear + InPhi;
	PA = FVector2D(HX - FMath::Sin(YawA) * La, HZ - FMath::Cos(YawA) * La);
	PB = FVector2D(HX + FMath::Sin(YawB) * Lb, HZ + FMath::Cos(YawB) * Lb);
}

FVector2D FHKArticulation::Hinge() const
{
	return PA + FVector2D(FMath::Sin(YawA), FMath::Cos(YawA)) * La;
}

void FHKArticulation::Step(double Dist, double PhiNow)
{
	// The front axle rolls along the front unit's heading; the rear unit follows the hinge like a trailer; the
	// steering cylinders then hold the two units at the articulation angle.
	PA += FVector2D(-FMath::Sin(YawA), -FMath::Cos(YawA)) * Dist;
	FVector2D H = PA + FVector2D(FMath::Sin(YawA), FMath::Cos(YawA)) * La;
	FVector2D D = H - PB;
	if (D.Size() > 1e-6) { D.Normalize(); YawB = std::atan2(-D.X, -D.Y); }
	PB = H - FVector2D(-FMath::Sin(YawB), -FMath::Cos(YawB)) * Lb;
	Phi = PhiNow;
	const double NewA = YawB + Phi;
	// Turning the front unit about the hinge.
	YawA = NewA;
	PA = H - FVector2D(FMath::Sin(YawA), FMath::Cos(YawA)) * La;
}

// ---------------------------------------------------------------------------------------------------------------------
// Wheel loader
FHKWheelLoader::FHKWheelLoader(double InX, double InZ, double InYaw)
	: FHKSiteMachine(InX, InZ, InYaw, 1.35, 8.2, TEXT("site_loader"), TEXT("loader"), TEXT("Wheel loader"))
{
	Width = 2.9; Height = 3.4; MassKg = 20500; PowerKw = 200; VMax = 10.5; VRev = 6; Gears = 4; HalfL = 1.6; HalfW = 1.0;
	Front = N(TEXT("front")); Arm = N(TEXT("liftArm")); Bucket = N(TEXT("bucket")); Lip = N(TEXT("lip")); Dirt = N(TEXT("dirt"));
	Wheels = {N(TEXT("rwheels[0]")), N(TEXT("rwheels[1]")), N(TEXT("fwheels[0]")), N(TEXT("fwheels[1]"))};
	Art.La = 1.6; Art.Lb = 1.6;
	Art.Init(X, Z, Yaw, 0);
	// Two lift cylinders from low on the front frame to under the lift arms, and the tilt cylinder to the bucket's back.
	auto Ram = [this](FHKNode* PA, const FVector& A, FHKNode* PB, const FVector& B, int32 i)
	{
		Rams.Add({PA, A, PB, B, N(FString::Printf(TEXT("rams[%d].barrel"), i)), N(FString::Printf(TEXT("rams[%d].rod"), i))});
	};
	Ram(Front, FVector(-.62, .75, -1.9), Arm, FVector(-.62, -.95, -1.0), 0);
	Ram(Front, FVector(.62, .75, -1.9), Arm, FVector(.62, -.95, -1.0), 1);
	Ram(Front, FVector(0, 2.05, -.95), Bucket, FVector(0, .75, .3), 2);
	SeatNode = Rig.Root; SeatOffset = FVector(0, 2.9, .75); CamPitch = -.24;   // high and looking down: the bucket's edge shows
	bRightStickTool = true;
	HelpText = TEXT("Wheel loader · R2 throttle, L2 brake / reverse, left stick steer · right stick back / forward: lift / lower, ← roll back, → dump · R3 view · □ get out");
	Capacity = 3.6;
	Pose(); Place();
}

void FHKWheelLoader::Pose()
{
	Front->SetRotY(Art.Phi);
	Arm->SetRotX(LiftA);
	Bucket->SetRotX(TiltA - LiftA);                 // the linkage keeps the bucket's angle as the arm rises
	PoseRams(Rig.Root, Rams);
	Dirt->bVisible = Payload > .15;
	const double S = FMath::Max(.2, FMath::Min(1.0, Payload / Capacity));
	Dirt->SetScale(1, S, S);
}

void FHKWheelLoader::Update(double Dt, const FHKVehicleInput& In)
{
	double Fwd = 0, Back = 0, PhiT = Art.Phi;
	if (bActive)
	{
		Fwd = In.Throttle; Back = In.Brake;
		PhiT = HKV::Clamp(Art.Phi - In.Steer * .55 * Dt, -.7, .7);
		LiftA = HKV::Clamp(LiftA + Dz(In.RY) * .32 * Dt, -.08, 1.05);           // stick back lifts
		TiltA = HKV::Clamp(TiltA - Dz(In.RX) * .65 * Dt, -1.1, .8);
	}
	Pose();
	Rig.Root->UpdateMatrixWorld();
	const FVector L = Lip->GetWorldPosition();
	const FVector2D F(-FMath::Sin(Art.YawA), -FMath::Cos(Art.YawA));
	const double Gl = Ground(L.X, L.Z);
	double Resist = 0;
	// Scooping: the lip in the soil while driving forward fills the bucket (and the pile pushes back).
	if (Site && Site->Contains(L.X, L.Z, .8) && L.Y < Gl - .04 && Speed > .1 && Payload < Capacity)
	{
		const double Cut = Site->Cut(Tool(L + FVector(F.X, 0, F.Y) * .25, F, .3 + Speed * Dt, 1.42, L.Y), FMath::Min(Capacity - Payload, (1.0 + Speed) * Dt * 1.2));
		Payload += Cut;
		Resist = .25 + FMath::Min(.5, (Gl - L.Y) * .8);
		if (Cut > 0 && HKVehicles::Random() < Dt * 4) Snd().Sfx(TEXT("scrape"), FHKSfxOpts(.4));
	}
	// Spilling: the bucket's floor (its local -z) tipped more than about 27 degrees below the horizontal.
	if (Payload > 0)
	{
		const FVector Floor = WorldDir(Bucket, FVector(0, 0, -1));
		const double Rate = 5.0 * FMath::Clamp((-.45 - Floor.Y) / .5, 0.0, 1.0);
		if (Rate > 0)
		{
			const double Out = FMath::Min(Payload, Rate * Dt);
			Payload -= Out;
			PourSoil(Bucket->LocalToWorld(FVector(0, -.1, -1.2)), Out, 1.0);
			if (HKVehicles::Random() < Dt * 3) Snd().Sfx(TEXT("dump"), FHKSfxOpts(.4));
		}
	}
	Drive(Dt, Fwd, Back, Resist);
	// Articulated motion of the two frames; the root is the rear frame at the hinge.
	const FVector2D Old = Art.Hinge();
	Art.Step(Speed * Dt, PhiT);
	FVector2D H = Art.Hinge();
	// Collision: keep the old position if the machine would run into something.
	const double OldX = X, OldZ = Z, OldYaw = Yaw;
	X = H.X; Z = H.Y; Yaw = Art.YawB;
	if (FMath::Abs(Speed) > .001 && !Clear(X, Z)) { X = OldX; Z = OldZ; Yaw = OldYaw; Art.Init(X, Z, Yaw, Art.Phi); if (FMath::Abs(Speed) > 2.5) Snd().Sfx(TEXT("hit")); Speed = 0; }
	SettleBig(Dt);
	Spin += Speed * Dt;
	SpinWheels(Wheels, .8, Dt);
	Place();
	EngineSound(bActive ? Rpm + Resist * 300 : 0, bActive ? FMath::Max(EngineLoad, Resist) : 0);
}

void FHKWheelLoader::NetApply(const TArray<double>& A)
{
	Art.Phi = At(A, 0); LiftA = At(A, 1); TiltA = At(A, 2); Payload = At(A, 3);
	Art.Init(X, Z, Yaw, Art.Phi);
	Pose();
}

FString FHKWheelLoader::Hud() const
{
	return FString::Printf(TEXT("%d km/h%s · bucket %s / %s m³"), (int32)HKV::JsRound(FMath::Abs(Speed) * 3.6), *GearText(), *HKV::ToFixed(Payload, 1), *HKV::ToFixed(Capacity, 1));
}

// ---------------------------------------------------------------------------------------------------------------------
// Dump trucks
FHKDumpTruck::FHKDumpTruck(double InX, double InZ, double InYaw, bool bArticulated)
	: FHKSiteMachine(InX, InZ, InYaw, 1.4, bArticulated ? 10.5 : 9.2, bArticulated ? TEXT("site_hauler") : TEXT("site_tipper"),
		bArticulated ? TEXT("hauler") : TEXT("tipper"), bArticulated ? TEXT("Articulated hauler") : TEXT("Tipper truck"))
{
	bArt = bArticulated;
	VerbText = TEXT("Drive the");
	Width = bArt ? 2.95 : 2.55; Height = 3.4;
	MassKg = bArt ? 23500 : 14500; PowerKw = bArt ? 265 : 330; VMax = bArt ? 15 : 22; VRev = 4; Gears = bArt ? 6 : 12; Grip = bArt ? .6 : .55;
	Capacity = bArt ? 17.5 : 16.0;
	Bed = N(TEXT("body")); LoadNode = N(TEXT("load"));
	if (bArt)
	{
		Trailer = N(TEXT("trailer"));
		Wheels = {N(TEXT("fwheels[0]")), N(TEXT("fwheels[1]")), N(TEXT("rwheels[0]")), N(TEXT("rwheels[1]")), N(TEXT("rwheels[2]")), N(TEXT("rwheels[3]"))};
		Art.La = 2.1; Art.Lb = 3.1;
		Art.Init(X, Z, Yaw, 0);
		SeatNode = Rig.Root; SeatOffset = FVector(0, 3.2, -1.65);
		HalfL = 3.0; HalfW = 1.1;
		Rams.Add({Trailer, FVector(0, 1.0, 1.4), Bed, FVector(0, .1, -3.0), N(TEXT("rams[0].barrel")), N(TEXT("rams[0].rod"))});
		Rams.Add({Trailer, FVector(0, 1.0, 1.5), Bed, FVector(0, .1, -2.9), N(TEXT("rams[1].barrel")), N(TEXT("rams[1].rod"))});
		HelpText = TEXT("Articulated hauler · R2 throttle, L2 brake / reverse, left stick steer · right stick back: tip the body, forward: lower it · R3 view · □ get out");
	}
	else
	{
		for (int32 i = 0; i < 4; i++) Wheels.Add(N(FString::Printf(TEXT("swheels[%d]"), i)));
		for (int32 i = 0; i < 4; i++) Wheels.Add(N(FString::Printf(TEXT("rwheels[%d]"), i)));
		Steers = {N(TEXT("steer[0]")), N(TEXT("steer[1]"))};
		SeatNode = Rig.Root; SeatOffset = FVector(-.55, 2.75, -5.45);
		HalfL = 3.0; HalfW = 1.0; WheelBase = 4.2;
		Rams.Add({Rig.Root, FVector(0, 1.2, -3.85), Bed, FVector(0, .05, -5.3), N(TEXT("rams[0].barrel")), N(TEXT("rams[0].rod"))});
		HelpText = TEXT("Tipper truck · R2 throttle, L2 brake / reverse, left stick steer · right stick back: tip the body, forward: lower it · R3 view · □ get out");
	}
	bRightStickTool = true;
	CamPitch = -.05;
	Pose(); Place();
}

void FHKDumpTruck::Pose()
{
	if (bArt) Trailer->SetRotY(-Art.Phi);
	else for (FHKNode* S : Steers) S->SetRotY(-SteerA);
	Bed->SetRotX(Tip);
	LoadNode->bVisible = Payload > .2;
	const double S = FMath::Max(.15, FMath::Min(1.0, Payload / Capacity));
	LoadNode->SetScale(1, S, .4 + .6 * S);
	PoseRams(Rig.Root, Rams);
}

double FHKDumpTruck::AcceptSoil(const FVector& AtP, double Volume)
{
	if (Payload >= Capacity || Tip > .1) return 0;
	Rig.Root->UpdateMatrixWorld();
	const FVector L = Bed->WorldToLocal(AtP);
	const double Len = bArt ? 5.0 : 5.5, Wd = bArt ? 2.95 : 2.5;
	if (L.Z > .3 || L.Z < -Len - .2 || FMath::Abs(L.X) > Wd / 2 + .2 || L.Y < -.5 || L.Y > 6) return 0;
	const double Take = FMath::Min(Volume, Capacity - Payload);
	Payload += Take;
	return Take;
}

void FHKDumpTruck::Update(double Dt, const FHKVehicleInput& In)
{
	double Fwd = 0, Back = 0;
	if (bActive)
	{
		Fwd = In.Throttle; Back = In.Brake;
		Tip = HKV::Clamp(Tip + Dz(In.RY) * (In.RY > 0 ? .22 : .3) * Dt, 0, bArt ? 1.2 : .95);   // stick back tips
	}
	// Tipping: past about 25 degrees the soil slides out at the back.
	if (Payload > 0 && Tip > .42)
	{
		const double Out = FMath::Min(Payload, (Tip - .42) * 6.0 * Dt);
		Payload -= Out;
		Rig.Root->UpdateMatrixWorld();
		PourSoil(Bed->LocalToWorld(FVector(0, 0, 1.2)), Out, 1.1);
	}
	if (Tip > .05) Fwd *= .25;                                                   // a raised body: only creep
	Drive(Dt, Fwd, Back);
	const double OldX = X, OldZ = Z, OldYaw = Yaw;
	if (bArt)
	{
		const double PhiT = bActive ? HKV::Clamp(Art.Phi - In.Steer * .5 * Dt, -.75, .75) : Art.Phi;
		Art.Step(Speed * Dt, PhiT);
		const FVector2D H = Art.Hinge();
		X = H.X; Z = H.Y; Yaw = Art.YawA;
	}
	else
	{
		const double Max = .6 * FMath::Max(.3, 1 - FMath::Abs(Speed) / 25);
		SteerA += ((bActive ? In.Steer : 0) * Max - SteerA) * FMath::Min(1.0, Dt * 2.5);
		Yaw -= Speed * std::tan(SteerA) / WheelBase * Dt;
		const FVector2D F = Forward();
		X += F.X * Speed * Dt; Z += F.Y * Speed * Dt;
	}
	if (FMath::Abs(Speed) > .001 && !Clear(X, Z))
	{
		X = OldX; Z = OldZ; Yaw = OldYaw;
		if (bArt) Art.Init(X, Z, Yaw - Art.Phi, Art.Phi);
		if (FMath::Abs(Speed) > 2.5) Snd().Sfx(TEXT("hit"));
		Speed = 0;
	}
	SettleBig(Dt);
	SpinWheels(Wheels, bArt ? .78 : .52, Dt);
	Place();
	Pose();
	EngineSound(bActive ? Rpm : 0, bActive ? EngineLoad : 0);
	if (Engine) Engine->Hydraulics(bActive && FMath::Abs(In.RY) > .12 ? 1 : 0);
}

void FHKDumpTruck::NetApply(const TArray<double>& A)
{
	if (bArt) { Art.Phi = At(A, 0); Art.Init(X, Z, Yaw - Art.Phi, Art.Phi); } else SteerA = At(A, 0);
	Tip = At(A, 1); Payload = At(A, 2);
	Pose();
}

FString FHKDumpTruck::Hud() const
{
	return FString::Printf(TEXT("%d km/h%s · load %s / %s m³%s"), (int32)HKV::JsRound(FMath::Abs(Speed) * 3.6), *GearText(), *HKV::ToFixed(Payload, 1),
		*HKV::ToFixed(Capacity, 1), Tip > .03 ? *FString::Printf(TEXT(" · body %d°"), (int32)HKV::JsRound(FMath::RadiansToDegrees(Tip))) : TEXT(""));
}

// ---------------------------------------------------------------------------------------------------------------------
// Hook
void FHKHook::Step(double Dt, const FVector& Top)
{
	// A damped pendulum on a stiff wire; the load (if any) hangs under the hook and rests on the ground when lowered.
	const FVector Prev = P;
	Vel.Y -= G * Dt;
	P += Vel * Dt;
	FVector D = P - Top;
	const double L = HKV::Len(D);
	if (L > Wire) P = Top + HKV::Mul(HKV::Norm(D), Wire);
	const double Below = Load ? Load->Height + .6 : .55;
	const double Gy = Ground(P.X, P.Z) + Below;
	if (P.Y < Gy) { P.Y = Gy; Vel = FVector::ZeroVector; }
	Vel = HKV::Mul(P - Prev, 1 / FMath::Max(Dt, 1e-4)) * .992;
	if (Load)
	{
		Load->P = P - FVector(0, Below, 0);
		Load->bHooked = true;
	}
}

bool FHKHook::Toggle()
{
	if (Load)
	{
		Load->bHooked = false;
		Load->P.Y = Ground(Load->P.X, Load->P.Z);
		Load = nullptr;
		Snd().Sfx(TEXT("thud"), FHKSfxOpts(.5));
		return true;
	}
	FHKSiteLoad* Best = nullptr;
	double Bd = 1.6;
	for (FHKSiteLoad* L : FHKSiteMachine::Loads())
	{
		if (L->bHooked) continue;
		const double D = HKV::Len(L->P + FVector(0, L->Height, 0) - P);
		if (D < Bd) { Bd = D; Best = L; }
	}
	if (!Best) return false;
	Load = Best;
	Snd().Sfx(TEXT("carDoor"), FHKSfxOpts(.4));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Mobile crane
FHKMobileCrane::FHKMobileCrane(double InX, double InZ, double InYaw)
	: FHKSiteMachine(InX, InZ, InYaw, 1.3, 11.4, TEXT("site_crane"), TEXT("crane"), TEXT("Mobile crane"))
{
	VerbText = TEXT("Drive the");
	Width = 2.55; Height = 3.8; MassKg = 36000; PowerKw = 330; VMax = 19; VRev = 3; Gears = 12; HalfL = 2.6; HalfW = 1.1;
	Super = N(TEXT("super")); BoomBase = N(TEXT("boomBase")); TipNode = N(TEXT("tip")); HookNode = N(TEXT("hook")); Wire = N(TEXT("wire"));
	for (int32 i = 0; i < 3; i++) Tele.Add(N(FString::Printf(TEXT("tele[%d]"), i)));
	for (int32 i = 0; i < 4; i++) Outriggers.Add(N(FString::Printf(TEXT("out[%d]"), i)));
	for (int32 i = 0; i < 4; i++) Wheels.Add(N(FString::Printf(TEXT("swheels[%d]"), i)));
	Wheels.Add(N(TEXT("rwheels[0]"))); Wheels.Add(N(TEXT("rwheels[1]")));
	Steers = {N(TEXT("steer[0]")), N(TEXT("steer[1]"))};
	Rams.Add({Super, FVector(0, .45, -.6), BoomBase, FVector(0, -.4, -3.6), N(TEXT("rams[0].barrel")), N(TEXT("rams[0].rod"))});
	SeatNode = Rig.Root; SeatOffset = FVector(-.65, 2.6, -5.0);
	HelpText = TEXT("Mobile crane · drive: R2 / L2 / left stick · L1 outriggers down / up · crane: left stick ←→ slew, ↑↓ telescope out / in, right stick back / forward boom up / down, R2 / L2 hoist up / down, ✕ hook or set down a load · R3 view · □ get out");
	Pose(); Place();
	Rig.Root->UpdateMatrixWorld();
	Hook.P = TipNode->GetWorldPosition() - FVector(0, Hook.Wire, 0);
}

void FHKMobileCrane::Pose()
{
	Super->SetRotY(Slew);
	BoomBase->SetRotX(Luff);
	for (int32 i = 0; i < 3; i++) Tele[i]->Position.Z = (i == 0 ? -.3 : -.2) - Ext / 3.0;
	for (int32 i = 0; i < 4; i++)
	{
		const double Sx = (i % 2 == 0) ? -1 : 1;
		Outriggers[i]->Position.X = Sx * (.9 + 2.0 * FMath::Min(1.0, Deploy * 1.6));
		Outriggers[i]->Position.Y = 1.05 - .35 * FMath::Clamp((Deploy - .62) / .38, 0.0, 1.0);
	}
	for (FHKNode* S : Steers) S->SetRotY(-SteerA);
	PoseRams(Rig.Root, Rams);
}

FHKSeatPose FHKMobileCrane::Seat()
{
	if (Deploy > .99)
	{
		SeatNode = Super; SeatOffset = FVector(-1.05, 1.45, -.45);
		FHKSeatPose S = FHKSiteMachine::Seat();
		S.Yaw = Yaw + Slew; S.Pitch = Pitch + Luff * .45;
		SeatNode = Rig.Root; SeatOffset = FVector(-.65, 2.6, -5.0);
		return S;
	}
	return FHKSiteMachine::Seat();
}

void FHKMobileCrane::Update(double Dt, const FHKVehicleInput& In)
{
	double Fwd = 0, Back = 0;
	const bool bCraneMode = Deploy > .99;
	bRightStickTool = Deploy > .01;
	if (bActive)
	{
		// L1 (pressed): outriggers down when stopped, up again (the boom folds down first).
		if (In.L1 && !bPrevL1 && FMath::Abs(Speed) < .3) DeployTarget = DeployTarget > .5 ? 0 : 1;
		bPrevL1 = In.L1;
		if (bCraneMode)
		{
			Slew -= Dz(In.LX) * .22 * Dt;
			Ext = HKV::Clamp(Ext - Dz(In.LY) * .7 * Dt, 0, 23.0);
			Luff = HKV::Clamp(Luff + Dz(In.RY) * .1 * Dt, 0, 1.36);             // stick back raises the boom
			Hook.Wire = HKV::Clamp(Hook.Wire - In.R2 * 1.2 * Dt + In.L2 * 1.2 * Dt, 1.0, 48.0);
			if (In.bCross) Hook.Toggle();
		}
		else if (Deploy < .01) { Fwd = In.Throttle; Back = In.Brake; }
	}
	// Outriggers: out and down over about 4 s; before they go up the boom is lowered and retracted.
	if (DeployTarget < .5 && (Luff > .01 || Ext > .05))
	{
		Luff = FMath::Max(0.0, Luff - .15 * Dt); Ext = FMath::Max(0.0, Ext - 2.0 * Dt); Slew *= FMath::Max(0.0, 1 - Dt);
	}
	else Deploy = HKV::Clamp(Deploy + (DeployTarget > .5 ? 1 : -1) * Dt / 4.0, 0, 1);
	Drive(Dt, Fwd, Back);
	const double Max = .55 * FMath::Max(.3, 1 - FMath::Abs(Speed) / 20);
	SteerA += ((bActive && Deploy < .01 ? In.Steer : 0) * Max - SteerA) * FMath::Min(1.0, Dt * 2.5);
	Yaw -= Speed * std::tan(SteerA) / 4.6 * Dt;
	Move(Dt);
	SettleBig(Dt);
	if (Deploy > .9) { Pitch *= FMath::Max(0.0, 1 - Dt * 2); TerrainRoll *= FMath::Max(0.0, 1 - Dt * 2); Roll = TerrainRoll; }   // levelled on the outriggers
	SpinWheels(Wheels, .62, Dt);
	Place();
	Pose();
	Rig.Root->UpdateMatrixWorld();
	const FVector TipW = TipNode->GetWorldPosition();
	Hook.Step(Dt, TipW);
	PoseHook(Rig.Root, HookNode, Wire, TipW, Hook.P);
	EngineSound(bActive ? (bCraneMode ? 1100 : Rpm) : 0, bActive ? (bCraneMode ? .3 : EngineLoad) : 0);
}

void FHKMobileCrane::NetApply(const TArray<double>& A)
{
	SteerA = At(A, 0); Slew = At(A, 1); Luff = At(A, 2); Ext = At(A, 3); Deploy = At(A, 4); Hook.Wire = FMath::Max(1.0, At(A, 5));
	Pose();
}

FString FHKMobileCrane::Hud() const
{
	if (Deploy < .01) return FHKSiteMachine::Hud();
	if (Deploy < .99) return FString::Printf(TEXT("Outriggers %d %%"), (int32)HKV::JsRound(Deploy * 100));
	const double BoomLen = 10.6 + Ext, Reach = BoomLen * FMath::Cos(Luff);
	return FString::Printf(TEXT("Boom %s m at %d° · radius %s m · hook %s m%s"), *HKV::ToFixed(BoomLen, 1), (int32)HKV::JsRound(FMath::RadiansToDegrees(Luff)),
		*HKV::ToFixed(Reach, 1), *HKV::ToFixed(Hook.Wire, 1), Hook.Load ? *FString::Printf(TEXT(" · load %s t"), *HKV::ToFixed(Hook.Load->Mass / 1000, 1)) : TEXT(""));
}

// ---------------------------------------------------------------------------------------------------------------------
// Tower crane
FHKTowerCrane::FHKTowerCrane(double InX, double InZ, double InYaw)
	: FHKSiteMachine(InX, InZ, InYaw, 1.2, 2.2, TEXT("site_tower"), TEXT("tower"), TEXT("Tower crane"))
{
	Width = 2.2; Height = 32; MassKg = 1e9;
	SlewNode = N(TEXT("slew")); TrolleyNode = N(TEXT("trolley")); HookNode = N(TEXT("hook")); Wire = N(TEXT("wire"));
	SeatNode = SlewNode; SeatOffset = FVector(-1.4, 1.35, -1.75); CamPitch = -.45;
	bRightStickTool = true;
	HelpText = TEXT("Tower crane · left stick ←→ slew, ↑↓ trolley out / in · right stick back / forward hoist up / down · ✕ hook or set down a load · R3 view · □ climb down");
	Hook.Wire = 20;
	Pose(); Place();
	Rig.Root->UpdateMatrixWorld();
	Hook.P = TrolleyNode->GetWorldPosition() - FVector(0, Hook.Wire, 0);
}

void FHKTowerCrane::Pose()
{
	SlewNode->SetRotY(Slew);
	TrolleyNode->Position.Z = -Trolley;
}

FHKSeatPose FHKTowerCrane::Seat()
{
	FHKSeatPose S = FHKSiteMachine::Seat();
	S.Yaw = Yaw + Slew; S.Pitch = 0; S.Roll = 0;
	return S;
}

FVector2D FHKTowerCrane::Exit()
{
	bActive = false;
	if (Engine) Engine->Stop();
	Engine = nullptr;
	return FVector2D(X + 2.2, Z + 2.2);
}

TArray<FHKObstacle> FHKTowerCrane::Footprint() const
{
	FHKObstacle O;
	for (const FVector2D& C : {FVector2D(1, 1), FVector2D(1, -1), FVector2D(-1, -1), FVector2D(-1, 1)}) O.Points.Add(FHKPt(X + C.X * .9, Z + C.Y * .9));
	O.MinY = Y - .2; O.MaxY = Y + 30; O.Kind = TEXT("vehicle");
	O.Prepare();
	return {O};
}

void FHKTowerCrane::Update(double Dt, const FHKVehicleInput& In)
{
	if (bActive)
	{
		// Slewing has inertia: the jib speeds up and slows down gently (about 0.7 rpm at full stick).
		SlewV += (-Dz(In.LX) * .075 - SlewV) * FMath::Min(1.0, Dt * .8);
		Trolley = HKV::Clamp(Trolley - Dz(In.LY) * .9 * Dt, 3.5, 38.5);
		Hook.Wire = HKV::Clamp(Hook.Wire - Dz(In.RY) * 1.1 * Dt, 1.5, 31.0);   // stick back hoists up
		if (In.bCross) Hook.Toggle();
	}
	else SlewV *= FMath::Max(0.0, 1 - Dt);
	Slew += SlewV * Dt;
	Speed = 0;
	Pose();
	Place();
	Rig.Root->UpdateMatrixWorld();
	const FVector Top = TrolleyNode->GetWorldPosition() - FVector(0, .35, 0);
	Hook.Step(Dt, Top);
	PoseHook(Rig.Root, HookNode, Wire, Top, Hook.P);
	if (Engine) Engine->Set(bActive ? 800 + FMath::Abs(SlewV) * 4000 : 0, .1);
}

void FHKTowerCrane::Enter()
{
	bActive = true;
	Snd().Sfx(TEXT("carDoor"));
	Engine = Snd().Engine(TEXT("diesel"));
}

void FHKTowerCrane::NetApply(const TArray<double>& A)
{
	Slew = At(A, 0); Trolley = FMath::Max(3.5, At(A, 1)); Hook.Wire = FMath::Max(1.5, At(A, 2));
	Pose();
}

FString FHKTowerCrane::Hud() const
{
	return FString::Printf(TEXT("Radius %s m · hook %s m below the jib%s"), *HKV::ToFixed(Trolley, 1), *HKV::ToFixed(Hook.Wire, 1),
		Hook.Load ? *FString::Printf(TEXT(" · load %s t"), *HKV::ToFixed(Hook.Load->Mass / 1000, 1)) : TEXT(""));
}

// ---------------------------------------------------------------------------------------------------------------------
void HKSiteVehicles::CreateAll(const FHKDirtField& Site, TArray<TUniquePtr<FHKVehicle>>& Out)
{
	for (const FHKDirtField::FPlaced& P : Site.Machines)
	{
		if (P.Kind == TEXT("dozer")) Out.Add(MakeUnique<FHKDozer>(P.X, P.Z, P.Yaw));
		else if (P.Kind == TEXT("excavator")) Out.Add(MakeUnique<FHKSiteExcavator>(P.X, P.Z, P.Yaw));
		else if (P.Kind == TEXT("loader")) Out.Add(MakeUnique<FHKWheelLoader>(P.X, P.Z, P.Yaw));
		else if (P.Kind == TEXT("hauler")) Out.Add(MakeUnique<FHKDumpTruck>(P.X, P.Z, P.Yaw, true));
		else if (P.Kind == TEXT("tipper")) Out.Add(MakeUnique<FHKDumpTruck>(P.X, P.Z, P.Yaw, false));
		else if (P.Kind == TEXT("crane")) Out.Add(MakeUnique<FHKMobileCrane>(P.X, P.Z, P.Yaw));
		else if (P.Kind == TEXT("tower")) Out.Add(MakeUnique<FHKTowerCrane>(P.X, P.Z, P.Yaw));
	}
	for (TUniquePtr<FHKVehicle>& V : Out) V->Spawn = V->NetGet();
}
