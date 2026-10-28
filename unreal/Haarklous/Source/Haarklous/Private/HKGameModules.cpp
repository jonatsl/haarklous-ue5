// game-main.js, the parts that drive the modules: vehicles (enter/exit, driving, passengers), aircraft, the FPV drone,
// the minigames and the firepit. The modules themselves are ports of game-vehicles.js, game-aircraft.js,
// game-minigames.js and game-fire.js (HKVehicles, HKAircraft, HKMinigames, HKFire).
#include "HKGame.h"
#include "HKAircraft.h"
#include "HKAudio.h"
#include "HKData.h"
#include "HKFire.h"
#include "HKMath.h"
#include "HKMinigames.h"
#include "HKMission.h"
#include "HKMissionFx.h"
#include "HKRigActor.h"
#include "HKUi.h"
#include "HKVehicles.h"
#include "Haarklous.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SceneView.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const TCHAR* AirHelpHeli = TEXT("Helicopter (Mode 2) · left stick ↑↓ collective (centre = hover), ←→ pedals · right stick pitch/roll · R3 / V chase camera · the rotor needs a few seconds to spool up");
	const TCHAR* AirHelpPlane = TEXT("Plane (Mode 2) · left stick ↑ to add throttle (it stays), ←→ rudder/nose wheel · right stick pitch/roll · pull back at about 85 km/h · L2 brakes · R3 / V view");
	FString Verb(const FHKVehicle* V)
	{
		if (!V->VerbText.IsEmpty()) return V->VerbText;
		return V->Kind == TEXT("motorcycle") ? TEXT("Ride the") : V->Kind == TEXT("excavator") ? TEXT("Climb into the") : V->bAircraft ? TEXT("Fly the") : TEXT("Drive the");
	}
	bool Stopped(const FHKVehicle* V) { return V->bAircraft ? V->bGrounded && V->Speed < 3 : FMath::Abs(V->Speed) < 3; }
	FString RigNameOf(const FHKVehicle* V)
	{
		if (!V->RigFile.IsEmpty()) return V->RigFile;
		return V->Kind == TEXT("motorcycle") ? TEXT("motorcycle") : V->Kind == TEXT("excavator") ? TEXT("excavator") : V->Kind == TEXT("helicopter") ? TEXT("helicopter") : V->Kind == TEXT("plane") ? TEXT("plane") : TEXT("car");
	}
}

int32 AHKGame::IndexOf(const FHKVehicle* V) const
{
	for (int32 i = 0; i < Vehicles.Num(); i++) if (Vehicles[i].Get() == V) return i;
	return -1;
}

void AHKGame::SetClip(double NearM)
{
	// camera.near: .05 walking, .12 flying, .03 for the drone.
	GNearClippingPlane = (float)(NearM * 100);
}

void AHKGame::AddDirtPile(double X, double Z, double R)
{
	// "new T.Mesh(new T.ConeGeometry(r, r * .7, 14), lib.concrete(0x6b5541)); m.position.set(x, heightAt(x, z) + r * .3, z)"
	static UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this);
	M->SetStaticMesh(Cone);
	M->SetMobility(EComponentMobility::Movable);
	M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	M->RegisterComponent();
	M->SetWorldLocation(HK::ToUE(FVector(X, FHKData::Get().GroundWorld(X, Z) + R * .3, Z)));
	M->SetWorldScale3D(FVector(R * 2, R * 2, R * .7));   // engine cone: 100 tall, radius 50
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Haarklous/Materials/M_HK_Master.M_HK_Master")))
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, M);
		Mid->SetVectorParameterValue(FName("Color"), HKVehicles::ColorFromHex(0x6b5541));
		Mid->SetScalarParameterValue(FName("Roughness"), .95f);
		M->SetMaterial(0, Mid);
	}
	DirtPiles.Add(M);
}

void AHKGame::SetupModules()
{
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// ---- Vehicles: the web's list and order (multiplayer uses the indices) ----
	HKVehicles::CreateAll(Vehicles);
	for (TUniquePtr<FHKVehicle>& V : Vehicles)
	{
		VehicleList.Add(V.Get());
		AHKRigActor* A = GetWorld()->SpawnActor<AHKRigActor>(AHKRigActor::StaticClass(), FTransform::Identity, SP);
		A->Init(&V->Rig, RigNameOf(V.Get()));
		if (V->bHasPaint) A->SetPaint(V->PaintColor);
		A->SetCullDistance(20000);
		VehicleActors.Add(A);
		FHKVehicle* Raw = V.Get();
		if (V->bAircraft) V->OnCrash = [this](FHKVehicle* C) { if (Player.Vehicle == C) Notice(TEXT("Crashed! The aircraft is put back on its pad in a moment."), 3); };
		if (V->Kind == TEXT("excavator"))
		{
			// "const f = ex.forward(); pile(ex.x + f.x * 6, ex.z + f.z * 6, 1.4); ex.onDump = (x, z) => pile(x, z, .55);"
			// (The first pile is in the level geometry already: the web's scene export carries it.)
			V->OnDump = [this](double X, double Z) { AddDirtPile(X, Z, .55); };
		}
	}
	Drone = MakeUnique<FHKDrone>();
	StartDrone = [this]() { StartDroneNow(); };
	// ---- Minigames ----
	FHKMinigamesContext MC = FHKMinigamesContext::FromGame();
	MC.NowMs = [this]() { return Time * 1000; };
	Mini = MakeUnique<FHKMinigames>(MC);
	DiscActor = GetWorld()->SpawnActor<AHKRigActor>(AHKRigActor::StaticClass(), FTransform::Identity, SP);
	DiscActor->Init(&Mini->DiscGolf.Rig, TEXT("discgolf"));
	PetanqueActor = GetWorld()->SpawnActor<AHKRigActor>(AHKRigActor::StaticClass(), FTransform::Identity, SP);
	PetanqueActor->Init(&Mini->Petanque.Rig, TEXT("petanque"));
	MinigamePrompt = [this](FHKPrompt& Out)
	{
		for (FHKMinigame* G : {(FHKMinigame*)&Mini->DiscGolf, (FHKMinigame*)&Mini->Petanque})
		{
			const TOptional<FString> T = G->Near(FVector(Player.X, Player.Feet, Player.Z));
			if (T.IsSet()) { Out = {T.GetValue(), [this, G]() { StartMinigame(G); }}; return true; }
		}
		return false;
	};
	// ---- Firepit ----
	FirePit = MakeUnique<FHKFire>(FirePos.X, FirePos.Y, FHKFireContext::FromGame());
	FireActor = GetWorld()->SpawnActor<AHKRigActor>(AHKRigActor::StaticClass(), FTransform::Identity, SP);
	FireActor->Init(&FirePit->Rig, TEXT("fire"));
	FireFx = GetWorld()->SpawnActor<AHKFireEffects>(AHKFireEffects::StaticClass(), FTransform::Identity, SP);
	FireFx->Init(FirePit->Charred ? FireActor->MeshOf(FirePit->Charred) : nullptr);
	ThrowLog = [this](const FVector& From) { FirePit->ThrowLog(From); };
	// ---- Vehicles in prompts, the mode updates, music ----
	VehiclePrompt = [this](FHKPrompt& Out)
	{
		FHKVehicle* V = NearestVehicle();
		if (!V) return false;
		const int32 i = IndexOf(V), Drv = Occupant ? Occupant(i, TEXT("d")) : 0, Pas = Occupant ? Occupant(i, TEXT("p")) : 0;
		const bool bCanRide = V->HipAnchor(TEXT("p")).bValid;
		const FString DName = NameOf ? NameOf(Drv) : FString(TEXT("someone"));
		if (!Drv) { Out = {FString::Printf(TEXT("□ %s %s"), *Verb(V), *V->Name), [this, V]() { TryDrive(V); }}; return true; }
		if (bCanRide && !Pas) { Out = {FString::Printf(TEXT("□ Ride along with %s (%s)"), *DName, V->Kind == TEXT("motorcycle") ? TEXT("pillion") : TEXT("passenger")), [this, V]() { TryRide(V); }}; return true; }
		Out = {FString::Printf(TEXT("%s is %s the %s"), *DName, V->bAircraft ? TEXT("flying") : TEXT("driving"), *V->Name), []() {}};
		return true;
	};
	ModePrompt = [this](FHKPrompt& Out)
	{
		if (Player.Mode == TEXT("vehicle") && Player.Vehicle)
		{
			if (!Stopped(Player.Vehicle)) return true;   // null prompt
			Out = {FString::Printf(TEXT("□ %s"), Player.Vehicle->Kind == TEXT("motorcycle") ? TEXT("Get off") : TEXT("Get out")), [this]() { ExitVehicle(); }};
			return true;
		}
		if (Player.Mode == TEXT("passenger") && Player.Vehicle)
		{
			if (!Stopped(Player.Vehicle)) return true;
			Out = {FString::Printf(TEXT("□ %s"), Player.Vehicle->Kind == TEXT("motorcycle") ? TEXT("Get off") : TEXT("Get out")), [this]() { ExitPassenger(); }};
			return true;
		}
		return false;
	};
	ModeUpdate = [this](double Dt, double Dl)
	{
		if (Player.Mode == TEXT("vehicle")) UpdateVehicle(Dt, Dl);
		else if (Player.Mode == TEXT("passenger")) UpdatePassenger(Dt, Dl);
		else if (Player.Mode == TEXT("drone")) UpdateDrone(Dt, Dl);
		else if (Player.Mode == TEXT("minigame")) UpdateMinigame(Dt, Dl);
	};
	PostModeUpdate = [this](double Dt) { UpdateOthers(Dt); };
	MusicTrack = [this]()
	{
		// "if (caseGame?.active) return 'mission'; if (player.mode === 'minigame') return discgolf / petanque; return 'main';"
		if (MissionActive && MissionActive()) return FString(TEXT("mission"));
		if (Player.Mode == TEXT("minigame") && ActiveMinigame) return ActiveMinigame->Name();
		return FString(TEXT("main"));
	};
}

// ---------- Vehicles: enter / exit ----------
FHKVehicle* AHKGame::NearestVehicle()
{
	FHKVehicle* Best = nullptr;
	double Bd = 2.6;
	for (FHKVehicle* V : VehicleList)
	{
		const FVector2D F = V->Forward();
		const double Dx = Player.X - V->X, Dz = Player.Z - V->Z, Along = Dx * F.X + Dz * F.Y, Side = Dx * -F.Y + Dz * F.X;
		const double D = HK::Hypot(FMath::Max(0.0, FMath::Abs(Along) - V->Length / 2), FMath::Max(0.0, FMath::Abs(Side) - V->Width / 2));
		if (D < Bd && FMath::Abs(Player.Feet - V->Y) < 2) { Bd = D; Best = V; }
	}
	return Best;
}

void AHKGame::EnterVehicle(FHKVehicle* V)
{
	WatchHide();
	Player.Mode = TEXT("vehicle"); Player.Vehicle = V; Player.LookYaw = 0; Player.LookPitch = 0; Player.bChase = false; Player.bHasChaseCam = false;
	V->Enter();
	UpdateDynamic();
	if (V->bAircraft) SetClip(.12);
	if (!V->HelpText.IsEmpty()) { Notice(V->HelpText, 9); return; }
	Notice(V->bAircraft ? FString(V->Kind == TEXT("helicopter") ? AirHelpHeli : AirHelpPlane) : V->Kind == TEXT("excavator") ? FString(TEXT("Tracks: L1/L2 left, R1/R2 right · Left stick: swing / stick · Right stick: boom / bucket · □ get out")) : FString(TEXT("R2 throttle · L2 brake / reverse · Left stick steer · Right stick look · □ get off")), V->bAircraft ? 9 : 6);
}

void AHKGame::TryDrive(FHKVehicle* V)
{
	const int32 i = IndexOf(V);
	auto Done = [this, V, i](bool bOk, int32 By, bool bTimeout)
	{
		if (Player.Mode != TEXT("foot")) { if (bOk && Release) Release(i, TEXT("d"), nullptr); return; }
		if (bOk) EnterVehicle(V);
		else Notice(bTimeout ? FString(TEXT("No answer from the lobby – try again.")) : FString::Printf(TEXT("%s just took it."), NameOf ? *NameOf(By) : TEXT("someone")), 3);
	};
	if (Claim) Claim(i, TEXT("d"), Done); else Done(true, 0, false);
}

void AHKGame::TryRide(FHKVehicle* V)
{
	const int32 i = IndexOf(V);
	auto Done = [this, V, i](bool bOk, int32 By, bool bTimeout)
	{
		if (Player.Mode != TEXT("foot")) { if (bOk && Release) Release(i, TEXT("p"), nullptr); return; }
		if (bOk) EnterPassenger(V);
		else Notice(bTimeout ? TEXT("No answer from the lobby – try again.") : TEXT("That seat is taken."), 3);
	};
	if (Claim) Claim(i, TEXT("p"), Done); else Done(true, 0, false);
}

void AHKGame::EnterPassenger(FHKVehicle* V)
{
	WatchHide();
	if (Photo.bActive) PhotoToggle();
	Player.Mode = TEXT("passenger"); Player.Vehicle = V; Player.LookYaw = 0; Player.LookPitch = 0;
	if (V->bAircraft) SetClip(.12);
	const FString K = V->Kind == TEXT("motorcycle") ? TEXT("bike") : V->Kind == TEXT("car") ? TEXT("car") : V->Kind == TEXT("helicopter") ? TEXT("heli") : V->Kind == TEXT("plane") ? TEXT("plane") : TEXT("car");
	PassengerEngine = FHKAudio::Get().Engine(K);
	const int32 Drv = Occupant ? Occupant(IndexOf(V), TEXT("d")) : 0;
	Notice(FString::Printf(TEXT("Riding along with %s · look around with the right stick · □ get out when stopped"), NameOf ? *NameOf(Drv) : TEXT("someone")), 5);
}

void AHKGame::ExitPassenger()
{
	FHKVehicle* V = Player.Vehicle;
	const int32 i = IndexOf(V);
	if (Release) Release(i, TEXT("p"), nullptr);
	if (PassengerEngine) PassengerEngine->Stop();
	PassengerEngine.Reset();
	Player.Mode = TEXT("foot"); Player.Vehicle = nullptr;
	SetClip(.05); SpeedoText.Empty(); bSpeedoAir = false;
	UpdateDynamic();
	const FVector2D F = V->Forward(), R(-F.Y, F.X);
	const double D = (V->Width ? V->Width : 1.8) / 2 + .7;
	const FVector2D P = V->bAircraft ? FVector2D(V->X + R.X * 1.8 - F.X * 1.8, V->Z + R.Y * 1.8 - F.Y * 1.8) : FVector2D(V->X + R.X * D, V->Z + R.Y * D);
	FVector Sp;
	FHKData& Data = FHKData::Get();
	if (Data.WalkWorld.FindSafeSpawn(FHKPt(P.X, P.Y), Player.Eye, Sp, 6, .08, Data.GroundWorld(P.X, P.Y))) { Player.X = Sp.X; Player.Z = Sp.Y; Player.Feet = Sp.Z; }
	Player.Yaw = V->Yaw; Player.Pitch = 0;
}

void AHKGame::ExitVehicle()
{
	FHKVehicle* V = Player.Vehicle;
	const int32 i = IndexOf(V);
	const FVector2D P = V->Exit();
	if (Release) { const TArray<double> Final = V->NetGet(); Release(i, TEXT("d"), &Final); }
	Player.Mode = TEXT("foot"); Player.Vehicle = nullptr;
	SetClip(.05); SpeedoText.Empty(); bSpeedoAir = false;
	UpdateDynamic();
	FHKData& Data = FHKData::Get();
	FVector Sp;
	if (Data.WalkWorld.FindSafeSpawn(FHKPt(P.X, P.Y), Player.Eye, Sp, 5, .08, Data.GroundWorld(P.X, P.Y))) { Player.X = Sp.X; Player.Z = Sp.Y; Player.Feet = Sp.Z; }
	Player.Yaw = V->Yaw; Player.Pitch = 0;
	// "for (const [, w] of vehicleWorlds) collision.setDynamic(w, []); updateDynamic();"
	Data.ForEachVehicleWorld([](FHKWalkWorld& W) { W.SetDynamic({}); });
	UpdateDynamic();
}

// ---------- Driving ----------
void AHKGame::UpdateVehicle(double Dt, double Dl)
{
	FHKVehicle* V = Player.Vehicle;
	if (V->bAircraft) { UpdateAircraft(Dt, Dl); return; }
	if (Frame.WasPressed("square") && FMath::Abs(V->Speed) < 3) { ExitVehicle(); return; }
	FHKVehicleInput In;
	In.bEmpty = false;
	In.Throttle = Frame.R2; In.Brake = Frame.L2; In.Steer = FMath::Abs(Frame.Move.X) > .08 ? Frame.Move.X : 0; In.Drunk = Dl;
	In.L1 = Frame.IsHeld("l1"); In.L2 = Frame.L2; In.R1 = Frame.IsHeld("r1"); In.R2 = Frame.R2;
	In.LX = Frame.Move.X; In.LY = Frame.Move.Y; In.RX = Frame.Look.X; In.RY = Frame.Look.Y;
	In.bCross = Frame.WasPressed("cross");
	V->Update(Dt, In);
	// "Look around from the seat (right stick / mouse); excavator uses the right stick for the boom, so mouse only there."
	const bool bEx = V->Kind == TEXT("excavator") || V->bRightStickTool;
	const double LookX = bEx ? 0 : Frame.Look.X, LookY = bEx ? 0 : Frame.Look.Y;
	Player.LookYaw = Player.LookYaw - Frame.Mouse.Dx * .0024 * Sensitivity.Mouse - LookX * 2.2 * Dt;
	Player.LookPitch = HK::Clamp(Player.LookPitch - Frame.Mouse.Dy * .0024 * Sensitivity.Mouse - LookY * 1.6 * Dt, -1.2, 1);
	if (!Frame.Mouse.Dx && !LookX) Player.LookYaw *= FMath::Max(0.0, 1 - Dt * 1.2);
	const FHKSeatPose S = V->Seat();
	const double Sway = Dl ? FMath::Sin(Time * 1000 / 1300) * .08 * Dl : 0;
	CamPos = FVector(S.X + S.Lateral * FMath::Cos(V->Yaw), S.Y, S.Z - S.Lateral * FMath::Sin(V->Yaw));
	const bool bWebEx = V->Kind == TEXT("excavator");
	SetCameraFromEuler(S.Pitch + Player.LookPitch - (V->Kind == TEXT("motorcycle") ? .12 : bWebEx ? .38 : .05) + V->CamPitch, S.Yaw + Player.LookYaw - (bWebEx ? .22 : 0) + V->CamYaw, -S.Roll + Sway);
	Fov = 70;
	Player.X = V->X; Player.Z = V->Z; Player.Feet = V->Y; Player.Yaw = V->Yaw;
	bSpeedoAir = false;
	const FString MachineHud = V->Hud();
	if (!MachineHud.IsEmpty()) SpeedoText = MachineHud;
	else if (bWebEx) SpeedoText = FString::Printf(TEXT("%s km/h · bucket %d %%"), *HKJs::ToFixed(FMath::Abs(V->Speed) * 3.6, 1), (int32)HK::JsRound(V->Load * 100));
	else SpeedoText = FString::Printf(TEXT("%d km/h%s"), (int32)HK::JsRound(FMath::Abs(V->Speed) * 3.6), V->Kind == TEXT("motorcycle") ? *FString::Printf(TEXT(" · gear %d · %d rpm"), V->Gear, (int32)(HK::JsRound(V->Rpm / 100) * 100)) : TEXT(""));
}

FHKVehicleInput AHKGame::FlightSticks(double Dl) const
{
	// "Mode 2 sticks for the aircraft and the drone; the keyboard uses W/S, A/D and the arrow keys."
	auto Dz = [](double A) { return FMath::Abs(A) < .07 ? 0.0 : A; };
	auto Kb = [this](const char* A, const char* B) { return (Frame.IsHeld(A) ? 1.0 : 0.0) - (Frame.IsHeld(B) ? 1.0 : 0.0); };
	const double Wob = Dl ? FMath::Sin(Time * 1000 / 480) * .3 * Dl : 0;
	FHKVehicleInput In;
	In.bEmpty = false;
	In.LX = Dz(Frame.Move.X) + Wob * .5;
	In.LY = Dz(Frame.Move.Y);
	In.RX = HK::Clamp(Dz(Frame.Look.X) + Kb("right", "left") + Wob, -1, 1);
	In.RY = HK::Clamp(Dz(Frame.Look.Y) + Kb("down", "up") + Wob * .4, -1, 1);
	In.Brake = Frame.L2;
	return In;
}

void AHKGame::UpdateAircraft(double Dt, double Dl)
{
	FHKVehicle* V = Player.Vehicle;
	if (Frame.WasPressed("square") && V->bGrounded && V->Speed < 3) { ExitVehicle(); return; }
	if (Frame.WasPressed("r3") || Frame.WasPressed("view")) { Player.bChase = !Player.bChase; Player.bHasChaseCam = false; }
	V->Update(Dt, FlightSticks(Dl));
	const FHKCameraPose Pose = V->CameraPose(Player.bChase, Player.bHasChaseCam ? &Player.ChaseCam : nullptr);
	if (Player.bChase) { Player.ChaseCam = Pose.Position; Player.bHasChaseCam = true; }
	CamPos = Pose.Position;
	CamQuat = Pose.Quaternion;
	if (!Player.bChase)
	{
		Player.LookYaw = Player.LookYaw - Frame.Mouse.Dx * .0024 * Sensitivity.Mouse;
		Player.LookPitch = HK::Clamp(Player.LookPitch - Frame.Mouse.Dy * .0024 * Sensitivity.Mouse, -1.1, 1);
		if (!Frame.Mouse.Dx) Player.LookYaw *= FMath::Max(0.0, 1 - Dt * .8);
		if (!Frame.Mouse.Dy) Player.LookPitch *= FMath::Max(0.0, 1 - Dt * .8);
		CamQuat = CamQuat * HK::AxisAngle(FVector(0, 1, 0), Player.LookYaw) * HK::AxisAngle(FVector(1, 0, 0), Player.LookPitch);
	}
	Fov = 72;
	Player.X = V->X; Player.Z = V->Z; Player.Feet = V->Y; Player.Yaw = V->Yaw;
	bSpeedoAir = true;
	SpeedoText = V->bBroken ? FString(TEXT("CRASHED")) : V->Hud();
}

void AHKGame::UpdatePassenger(double Dt, double Dl)
{
	FHKVehicle* V = Player.Vehicle;
	if (Frame.WasPressed("square") && FMath::Abs(V->Speed) < 3 && (!V->bAircraft || V->bGrounded)) { ExitPassenger(); return; }
	Player.LookYaw = HK::Clamp(Player.LookYaw - Frame.Mouse.Dx * .0024 * Sensitivity.Mouse - Frame.Look.X * 2.2 * Dt, -2.6, 2.6);
	Player.LookPitch = HK::Clamp(Player.LookPitch - Frame.Mouse.Dy * .0024 * Sensitivity.Mouse - Frame.Look.Y * 1.6 * Dt, -1.2, 1);
	V->Rig.Root->UpdateMatrixWorld();
	CamPos = V->Rig.Root->LocalToWorld(V->PassengerEye);
	CamQuat = V->Rig.Root->GetWorldQuaternion() * HK::EulerToQuat(Player.LookPitch - (V->Kind == TEXT("motorcycle") ? .1 : .05), Player.LookYaw, 0, TEXT("YXZ"));
	if (Dl) CamQuat = CamQuat * HK::AxisAngle(FVector(0, 0, 1), FMath::Sin(Time * 1000 / 1300) * .08 * Dl);
	Fov = 72;
	Player.X = V->X; Player.Z = V->Z; Player.Feet = V->Y; Player.Yaw = V->Yaw + Player.LookYaw;
	const double Kmh = FMath::Abs(V->Speed) * 3.6;
	const int32 Drv = Occupant ? Occupant(IndexOf(V), TEXT("d")) : 0;
	if (PassengerEngine)
	{
		if (V->Kind == TEXT("helicopter")) PassengerEngine->Set(V->Rpm, .5);
		else if (V->Kind == TEXT("plane")) PassengerEngine->Set(V->Prop ? V->Prop : 700, V->Throttle);
		else if (V->Kind == TEXT("motorcycle")) PassengerEngine->Set(1300 + FMath::Fmod(Kmh, 50.0) / 50 * 8500, Drv ? .5 : 0);
		else PassengerEngine->Set(800 + FMath::Fmod(Kmh * 38, 3600.0), Drv ? .4 : 0);
	}
	bSpeedoAir = V->bAircraft;
	SpeedoText = FString::Printf(TEXT("Passenger%s · %d km/h%s"), Drv ? *FString::Printf(TEXT(" · %s%s"), NameOf ? *NameOf(Drv) : TEXT("someone"), V->bAircraft ? TEXT(" flying") : TEXT(" driving")) : TEXT(""),
		(int32)HK::JsRound(Kmh), V->bAircraft ? *FString::Printf(TEXT(" · alt %s m"), *HKJs::ToFixed(FMath::Max(0.0, V->Y - FHKData::Get().GroundWorld(V->X, V->Z)), 0)) : TEXT(""));
}

// ---------- FPV drone ----------
void AHKGame::StartDroneNow()
{
	FHKData& Data = FHKData::Get();
	const FVector2D F(-FMath::Sin(Player.Yaw), -FMath::Cos(Player.Yaw));
	const FVector P(Player.X + F.X * 1.1, 0, Player.Z + F.Y * 1.1);
	const TOptional<FHKDroneFloor> Fl = FHKDrone::FloorBelow(Data.WalkWorld, P, Player.Feet + .4);
	if (!Fl.IsSet() || FMath::Abs(Fl->Y - Player.Feet) > .6 || Data.WalkWorld.HitsSolid(FHKPt(P.X, P.Z), Fl->Y, Fl->Y + .35)) { Notice(TEXT("No room to launch the drone here – face an open space.")); return; }
	WatchHide();
	if (Photo.bActive) PhotoToggle();
	Drone->Launch(P.X, Fl->Y, P.Z, Player.Yaw);
	Player.Mode = TEXT("drone");
	SetClip(.03);
	Notice(TEXT("FPV drone · left stick ↑↓ thrust, ←→ yaw · right stick tilt · L1 / Q angle ↔ acro · ✕ reset after a crash · ○ / △ pack it away"), 8);
}

void AHKGame::StopDrone()
{
	Drone->Stop();
	Player.Mode = TEXT("foot");
	SetClip(.05);
	Osd.bShow = false;
}

void AHKGame::UpdateDrone(double Dt, double Dl)
{
	FHKData& Data = FHKData::Get();
	if (Frame.WasPressed("circle") || Frame.WasPressed("triangle")) { StopDrone(); Notice(TEXT("Drone packed away."), 2); return; }
	if (Frame.WasPressed("l1"))
	{
		Drone->SetMode(Drone->Mode == TEXT("angle") ? TEXT("acro") : TEXT("angle"));
		Notice(Drone->Mode == TEXT("acro") ? TEXT("ACRO: the right stick sets rotation rates – nothing self-levels. Flips and rolls!") : TEXT("ANGLE: the drone levels itself and holds altitude with the stick centred."), 4);
	}
	if (Frame.WasPressed("cross") && (Drone->bCrashed || !Drone->bArmed))
	{
		if (Drone->Battery > 0) Drone->Reset();
		else Notice(TEXT("Battery empty – ○ to pack the drone away, then launch it again with a fresh pack."), 4);
	}
	FHKDroneEnv Env;
	Env.Floor = [&Data](const FVector& P, double Y) { return FHKDrone::FloorBelow(Data.WalkWorld, P, Y); };
	Env.Solid = [&Data](const FVector& P, double Y0, double Y1) { return Data.WalkWorld.HitsSolid(FHKPt(P.X, P.Z), Y0, Y1); };
	Env.bHasListener = true;
	Env.Listener = FVector(Player.X, Player.Feet + 1.6, Player.Z);
	Drone->Update(Dt, FlightSticks(Dl), Env);
	const FHKCameraPose Pose = Drone->CameraPose();
	CamPos = Pose.Position; CamQuat = Pose.Quaternion;
	Fov = 100;
	if ((OsdT -= Dt) > 0) return;
	OsdT = .1;
	const TOptional<FHKDroneFloor> Fl = FHKDrone::FloorBelow(Data.WalkWorld, Drone->Pos, Drone->Pos.Y + .1);
	const FHKDroneOsd O = Drone->Osd(Fl.IsSet() ? Fl->Y : Data.GroundWorld(Drone->Pos.X, Drone->Pos.Z));
	const int32 Mm = (int32)FMath::FloorToDouble(O.Time / 60);
	const FString Ss = FString::Printf(TEXT("%02d"), (int32)FMath::FloorToDouble(FMath::Fmod(O.Time, 60.0)));
	Osd.bShow = true;
	Osd.Tl = FString::Printf(TEXT("%s\n%s"), *O.Mode, O.bArmed ? TEXT("ARMED") : TEXT("DISARMED"));
	Osd.Tr = FString::Printf(TEXT("%s V · %d %%\n%d:%s"), *O.Volts, O.Battery, Mm, *Ss);
	Osd.Battery = O.Battery;
	Osd.Bl = FString::Printf(TEXT("ALT %s m\nTHR %d %%"), *HKJs::ToFixed(O.Alt, 1), (int32)HK::JsRound(O.Thrust * 100));
	Osd.Br = FString::Printf(TEXT("%d km/h\nHOME %d m"), (int32)HK::JsRound(O.Speed), (int32)HK::JsRound(O.Dist));
	Osd.Warn = O.bCrashed ? TEXT("CRASHED\n✕ reset · ○ pack away") : O.Battery <= 0 ? TEXT("BATTERY EMPTY") : O.Battery < 20 ? TEXT("LOW BATTERY") : TEXT("");
}

// ---------- Minigames ----------
void AHKGame::StartMinigame(FHKMinigame* G)
{
	const TOptional<FHKMiniPose> S = G->Start(FVector(Player.X, Player.Feet, Player.Z));
	Player.Mode = TEXT("minigame");
	ActiveMinigame = G;
	if (S.IsSet()) Place(S->X, S->Z, S->Yaw);
}

void AHKGame::UpdateMinigame(double Dt, double Dl)
{
	FHKMinigame* G = ActiveMinigame;
	if (!G) { Player.Mode = TEXT("foot"); return; }
	const double MouseScale = .0024 * Sensitivity.Mouse;
	auto Curve = [](double V) { return HK::Sign(V) * V * V; };
	Player.Yaw -= Frame.Mouse.Dx * MouseScale + Curve(Frame.Look.X) * 2 * Sensitivity.Pad * Dt;
	Player.Pitch = HK::Clamp(Player.Pitch - Frame.Mouse.Dy * MouseScale - Curve(Frame.Look.Y) * 1.4 * Sensitivity.Pad * Dt, -1.2, 1.2);
	FHKMiniInput In;
	In.AimYaw = Player.Yaw + (Dl ? FMath::Sin(Time * 1000 / 600) * .05 * Dl : 0);
	In.AimPitch = Player.Pitch;
	In.bAlt = Frame.L2 > .5;
	In.Hyzer = Frame.Move.X;
	In.bCancel = Frame.WasPressed("circle");
	In.Drunk = Dl;
	G->TakeCharge(Dt, Frame.R2 > .5, In);
	const FHKMiniResult R = G->Update(Dt, In);
	if (R.bTeleport) Place(R.Teleport.X, R.Teleport.Z, R.Teleport.Yaw);
	if (R.bStand) { Player.X = R.Stand.X; Player.Z = R.Stand.Z; Player.Feet = R.Stand.Y; }
	if (R.bFace) { Player.Yaw = R.Face; Player.Pitch = -.12; }
	if (R.bDone) { Notice(R.Done, 6); Player.Mode = TEXT("foot"); ActiveMinigame = nullptr; }
	if (R.bCamera) { CamPos = R.CameraPos; CamQuat = HK::LookAtQuat(R.CameraPos, R.CameraLookAt, FVector(0, 1, 0), true); }
	else { CamPos = FVector(Player.X, Player.Feet + Player.Eye, Player.Z); SetCameraFromEuler(Player.Pitch, Player.Yaw, 0); }
	Fov = 70;
}

// ---------- Everything else each frame ----------
void AHKGame::UpdateOthers(double Dt)
{
	// "Other vehicles: parked ones idle (far ones are hidden and skipped), ones driven by other players are posed by the lobby."
	VehicleCullT -= Dt;
	if (VehicleCullT <= 0)
	{
		VehicleCullT = .4;
		bool bAnyRemote = false;
		for (int32 i = 0; i < VehicleList.Num(); i++)
		{
			FHKVehicle* V = VehicleList[i];
			const bool bNear = HK::Hypot(V->X - CamPos.X, V->Z - CamPos.Z) < 170;
			V->bVisible = bNear || V == Player.Vehicle;
			V->Rig.Root->bVisible = V->bVisible;
			if (RemoteDriven && RemoteDriven(i)) bAnyRemote = true;
		}
		if (bAnyRemote) UpdateDynamic();
	}
	for (int32 i = 0; i < VehicleList.Num(); i++)
	{
		FHKVehicle* V = VehicleList[i];
		if (V == Player.Vehicle && Player.Mode == TEXT("vehicle")) continue;
		if (RemoteDriven && RemoteDriven(i)) continue;
		if (!V->bVisible && FMath::Abs(V->Speed) < .01 && !V->bAircraft) continue;
		V->Update(Dt, FHKVehicleInput());
	}
	// Firepit: flames, embers, smoke, light, thrown logs.
	if (FirePit)
	{
		FirePit->Update(Dt, FireLevel(), CamPos);
		if (FireFx) FireFx->Sync(*FirePit, CamPos);
	}
	SyncModuleVisuals();
}

void AHKGame::SyncModuleVisuals()
{
	// Rotor blades fade into a disc as they spin up (the web's blade material opacity and disc opacity).
	for (int32 i = 0; i < VehicleList.Num(); i++)
	{
		FHKCraft* C = VehicleList[i]->bAircraft ? static_cast<FHKCraft*>(VehicleList[i]) : nullptr;
		if (!C || !VehicleActors.IsValidIndex(i)) continue;
		for (const TCHAR* Name : {TEXT("disc"), TEXT("pdisc")})
			if (FHKNode* N = C->Rig.Find(Name)) N->bVisible = C->DiscOpacity > .02;
		for (const TCHAR* Name : {TEXT("rotor"), TEXT("tailRotor"), TEXT("propG")})
			if (FHKNode* N = C->Rig.Find(Name))
				if (UStaticMeshComponent* M = VehicleActors[i]->MeshOf(N)) M->SetVisibility(C->BladeOpacity > .25 && N->IsVisibleInTree(), false);
	}
}

void AHKGame::DrawModuleHud(UCanvas* C)
{
	if (Mini)
	{
		Mini->DiscGolf.DrawHud(C);
		Mini->Petanque.DrawHud(C);
	}
	// FPV drone on-screen display (#osd).
	if (Player.Mode == TEXT("drone") && Osd.bShow)
	{
		const FVector2D Sz = FHKUi::Size(C);
		const FLinearColor Ink(FColor(0xf4, 0xf7, 0xf0));
		FHKUi::Text(C, Osd.Tl, 18, 14, 15, Ink, EHKAlign::Left, true);
		FHKUi::Text(C, Osd.Tr, Sz.X - 18, 14, 15, Ink, EHKAlign::Right, true);
		FHKUi::Frame(C, Sz.X - 18 - 70, 40, 70, 8, 1, Ink);
		FHKUi::Rect(C, Sz.X - 18 - 69, 41, 68 * Osd.Battery / 100, 6, FLinearColor(FColor(0x9b, 0xe3, 0x7a)));
		FHKUi::Text(C, Osd.Bl, 18, Sz.Y - 52, 15, Ink, EHKAlign::Left, true);
		FHKUi::Text(C, Osd.Br, Sz.X - 18, Sz.Y - 52, 15, Ink, EHKAlign::Right, true);
		FHKUi::Text(C, TEXT("-+-"), Sz.X / 2, Sz.Y / 2 - 14, 22, FLinearColor(Ink.R, Ink.G, Ink.B, .85f), EHKAlign::Center, true);
		if (!Osd.Warn.IsEmpty()) FHKUi::Text(C, Osd.Warn, Sz.X / 2, Sz.Y * .3f, 20, FLinearColor(FColor(0xff, 0x5a, 0x4f)), EHKAlign::Center, true);
	}
}

// ---------- Hostage rescue mission (case-game.js mount(ctx)) ----------
bool AHKGame::ShootQuery(const FVector& Origin, const FVector& Dir, double MaxDist, FHKShotHit& Hit) const
{
	// The web raycasts against the visible, opaque meshes of the apartment and the own stairwell (shootTargets).
	TArray<FHitResult> Hits;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(HKShoot), true);
	Q.bReturnFaceIndex = true;
	const FVector From = HK::ToUE(Origin), To = From + HK::DirToUE(Dir.GetSafeNormal()) * MaxDist * 100;
	GetWorld()->LineTraceMultiByChannel(Hits, From, To, ECC_Visibility, Q);
	Hits.Sort([](const FHitResult& A, const FHitResult& B) { return A.Distance < B.Distance; });
	const AHKRigActor* AptA = ActorOfRig.FindRef(TEXT("apartment"));
	const AHKRigActor* StairA = ActorOfRig.FindRef(TEXT("stair_own"));
	for (const FHitResult& H : Hits)
	{
		UPrimitiveComponent* C = H.GetComponent();
		if (!C || !C->IsVisible()) continue;
		const AActor* A = H.GetActor();
		bool bOurs = A == AptA || A == StairA;
		if (const UStaticMeshComponent* M = Cast<UStaticMeshComponent>(C))
		{
			const FString Path = M->GetStaticMesh() ? M->GetStaticMesh()->GetPathName() : FString();
			if (Cast<AStaticMeshActor>(A) && (Path.Contains(TEXT("/Meshes/apartment/")) || Path.Contains(TEXT("/Meshes/stair_own/")))) bOurs = true;
			if (bOurs && H.FaceIndex >= 0)
			{
				int32 Section = 0;
				const UMaterialInterface* Mat = M->GetMaterialFromCollisionFaceIndex(H.FaceIndex, Section);
				if (Mat && IsTranslucentBlendMode(Mat->GetBlendMode())) continue;   // glass does not stop the ray
			}
		}
		if (!bOurs) continue;
		Hit.Distance = H.Distance / 100;
		Hit.Point = HK::FromUE(H.ImpactPoint);
		Hit.Normal = HK::DirFromUE(H.ImpactNormal);
		Hit.Component = C;
		return true;
	}
	return false;
}

void AHKGame::SetupMission()
{
	FHKData& Data = FHKData::Get();
	FHKMissionContext C;
	C.CloseAllDoors = [this]() { CloseAllDoors(); };
	C.RebuildWalkWorld = [this]() { UpdateDynamic(); };
	C.WalkWorld = [&Data]() { return &Data.WalkWorld; };
	C.Doors = []() { return HKMissionData::Doors(); };
	C.SpawnAreas = []() { return HKMissionData::SpawnAreas(); };
	C.StairSpawns = []() { return HKMissionData::StairSpawns(); };
	C.MissionStart = []() { return HKMissionData::MissionStart(); };
	C.Entrance = []() { return HKMissionData::Entrance(); };
	C.CameraPosition = [this]() { return CamPos; };
	C.CameraQuaternion = [this]() { return CamQuat; };
	C.CameraAspect = []() { FVector2D Vp(1920, 1080); if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Vp); return Vp.Y > 0 ? Vp.X / Vp.Y : 16.0 / 9; };
	C.GetFov = [this]() { return Fov; };
	C.SetFov = [this](double V) { Fov = V; };
	C.Stance = [this]() { return Player.Stance; };
	C.SetPose = [this](double X, double Z, double Yaw, double Tilt, double Y) { Place(X, Z, Yaw, Y); };
	C.UseDoorAhead = [this]() { return Interact(true); };
	C.Leave = [this]()
	{
		// "leave: () => {const yaw = player.yaw; place(player.x, player.z, yaw, player.feet); updateDynamic(); notice(...)}"
		const double Yaw = Player.Yaw;
		Place(Player.X, Player.Z, Yaw, Player.Feet);
		UpdateDynamic();
		Fov = 70;
		Notice(InApartment() ? TEXT("Mission over \u2013 you are home. Make yourself comfortable.") : TEXT("Mission over."), 4);
	};
	C.GetSensitivity = [this](const FString& K) { return K == TEXT("pad") ? Sensitivity.Pad : Sensitivity.Mouse; };
	C.SetSensitivity = [this](const FString& K, double V) { SetSens(K, V); };
	C.StepSensitivity = [this](const FString& K, int32 D) { SetSens(K, (K == TEXT("pad") ? Sensitivity.Pad : Sensitivity.Mouse) + D * .1); };
	C.Shoot = [this](const FVector& O, const FVector& D, double Max, FHKShotHit& H) { return ShootQuery(O, D, Max, H); };
	Mission = MakeUnique<FHKMission>(C);
	MissionActive = [this]() { return Mission->IsActive(); };
	MissionPlaying = [this]() { return Mission->GetState() == TEXT("play"); };
	MissionCanMove = [this]() { return Mission->CanMove(); };
	MissionMoveScale = [this]() { return Mission->MoveScale(); };
	LookScaleExtra = [this]() { return Mission->LookScale(); };
	PromptBlocked = [this]() { return Mission->IsActive() && Mission->GetState() != TEXT("play"); };
	ExtraDynamic = [this]() { return Mission->Obstacles(); };
	HideCrosshair = [this]() { return Mission->IsActive(); };
	StartMission = [this]()
	{
		// "if (photo.active) photo.toggle(); caseGame.start(false); notice('');"
		if (Photo.bActive) PhotoToggle();
		Mission->Start(false);
		Notice(FString());
	};
	MissionRoute = [this]()
	{
		if (!Mission->IsActive()) return;
		if (Mission->RouteInput(Frame)) Player.Mode = TEXT("foot");
	};
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	MissionFx = GetWorld()->SpawnActor<AHKMissionFx>(AHKMissionFx::StaticClass(), FTransform::Identity, SP);
	MissionFx->Bind(Mission.Get(), this);
}
