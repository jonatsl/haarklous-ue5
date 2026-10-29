// Scripted play-through "mouseflight" (-hktest=mouseflight): flying with keyboard and mouse like Battlefield (web issue
// #16 / PR #19, issue #13 here). The helicopter hovering 80 m up, the FPV drone in angle mode and the plane at 45 m/s
// are flown with simulated mouse motion; Shift and the right mouse button look around. Game time runs in fixed 1/60 s
// frames (AHKGame::FixedDt) and the mouse moves the same amount every frame, so the numbers do not depend on how fast
// this PC draws.
#include "HKTestDriver.h"
#include "HKAircraft.h"
#include "HKData.h"
#include "HKGame.h"
#include "HKInput.h"
#include "HKMath.h"

namespace HKFlightTest
{
	constexpr double Fdt = 1.0 / 60;
	TSharedPtr<FHKInputProcessor> In() { return FHKInputProcessor::Get(); }
	FHKCraft* Craft(const AHKGame* G, const FString& Kind)
	{
		for (FHKVehicle* V : G->VehicleList) if (V->Kind == Kind) return static_cast<FHKCraft*>(V);
		return nullptr;
	}
	FHKCraft* Flown(const AHKGame* G) { return G->Player.Mode == TEXT("vehicle") && G->Player.Vehicle && G->Player.Vehicle->bAircraft ? static_cast<FHKCraft*>(G->Player.Vehicle) : nullptr; }
	double PitchOf(const FHKCraft* C) { double P = 0, B = 0; if (C) C->Attitude(P, B); return P; }
	double BankOf(const FHKCraft* C) { double P = 0, B = 0; if (C) C->Attitude(P, B); return B; }
	// Where the camera looks, relative to the aircraft's nose (horizontal angle, + = to the left).
	double ViewFromNose(const AHKGame* G)
	{
		const FHKCraft* C = Flown(G);
		if (!C) return 0;
		const FVector F = G->CamQuat.RotateVector(FVector(0, 0, -1)), N = C->Q.RotateVector(FVector(0, 0, -1));
		const double A = FMath::Atan2(-F.X, -F.Z) - FMath::Atan2(-N.X, -N.Z);
		return FMath::Atan2(FMath::Sin(A), FMath::Cos(A));
	}
	FString State(const AHKGame* G)
	{
		const FHKCraft* C = Flown(G);
		if (!C) return TEXT("(not flying)");
		return FString::Printf(TEXT("pitch %.3f, bank %.3f rad, %.0f km/h, %.0f m above the ground, hold %s"), PitchOf(C), BankOf(C), C->GetSpeed() * 3.6,
			C->Pos.Y - C->GearH - FHKData::Get().GroundWorld(C->Pos.X, C->Pos.Z), G->MouseFly.bHold ? TEXT("on") : TEXT("off"));
	}
	// Seconds of game time (fixed frames).
	void GameWait(AHKTestDriver& T, double Seconds)
	{
		TSharedPtr<int32> N = MakeShared<int32>(0);
		const int32 Frames = FMath::Max(1, FMath::RoundToInt(Seconds / Fdt));
		T.Steps.Add([N, Frames](double) { return ++*N >= Frames; });
	}
	// Move the mouse (Dx, Dy) px over Seconds of game time, the same amount every frame (y down, as the browser's movementY).
	void MouseMove(AHKTestDriver& T, double Dx, double Dy, double Seconds)
	{
		TSharedPtr<int32> N = MakeShared<int32>(0);
		const int32 Frames = FMath::Max(1, FMath::RoundToInt(Seconds / Fdt));
		T.Steps.Add([N, Frames, Dx, Dy](double) { if (auto I = In()) I->SimulateMouse(Dx / Frames, Dy / Frames); return ++*N >= Frames; });
		GameWait(T, 2 * Fdt);   // the last move reaches the game's next frame
	}
	// Run Seconds of game time watching Fn: Out = (smallest, largest, last value).
	void Track(AHKTestDriver& T, double Seconds, TFunction<double()> Fn, TSharedPtr<FVector> Out)
	{
		TSharedPtr<int32> N = MakeShared<int32>(0);
		const int32 Frames = FMath::Max(1, FMath::RoundToInt(Seconds / Fdt));
		T.Steps.Add([N, Frames, Fn, Out](double)
		{
			const double V = Fn();
			if (*N == 0) *Out = FVector(V, V, V);
			*Out = FVector(FMath::Min(Out->X, V), FMath::Max(Out->Y, V), V);
			return ++*N >= Frames;
		});
	}
	// In the aircraft of this kind (getting out of another one), level and still Up metres above the ground below it,
	// the engine running; the plane flies at Speed along its nose with the throttle at Throttle.
	void Airborne(AHKTestDriver& T, const FString& Kind, double Up, double Speed = 0, double Throttle = 0)
	{
		T.Do([&T, Kind, Up, Speed, Throttle]()
		{
			AHKGame* G = T.Game();
			FHKCraft* C = Craft(G, Kind);
			if (!C) { UE_LOG(LogTemp, Display, TEXT("HK-TEST no %s"), *Kind); return; }
			if (FHKCraft* Old = Flown(G); Old && Old != C) { G->ExitVehicle(); Old->Reset(); }
			if (G->Player.Mode != TEXT("vehicle")) G->EnterVehicle(C);
			const double Yaw = C->GetYaw();
			C->Q = HK::EulerToQuat(0, Yaw, 0, TEXT("YXZ"));
			C->Pos.Y = FHKData::Get().GroundWorld(C->Pos.X, C->Pos.Z) + C->GearH + Up;
			C->Vel = FVector(-FMath::Sin(Yaw), 0, -FMath::Cos(Yaw)) * Speed;
			C->W = FVector::ZeroVector;
			C->bGrounded = false; C->bBroken = false;
			if (Kind == TEXT("helicopter")) C->Rpm = 1;
			else { C->Throttle = Throttle; C->Prop = 700 + 2050 * Throttle; }
			C->PlaceCraft();
			C->SyncBase();
		});
	}

	struct FRegister
	{
		FRegister()
		{
			FHKTestScripts::All().Add(TEXT("mouseflight"), [](AHKTestDriver& T)
			{
				T.Wait(3);
				T.Press(0);
				T.Wait(1);
				// Mouse sensitivity 1 (the web's default) for the numbers; the player's own setting comes back at the end.
				TSharedPtr<double> OldSens = MakeShared<double>(1);
				T.Do([&T, OldSens]() { AHKGame* G = T.Game(); *OldSens = G->Sensitivity.Mouse; G->Sensitivity.Mouse = 1; G->FixedDt = Fdt; });
				T.Log([OldSens]() { return FString::Printf(TEXT("mouse sensitivity %.2f -> 1 for the test, fixed 1/60 s frames"), *OldSens); });
				TSharedPtr<FVector> R = MakeShared<FVector>();
				TSharedPtr<FVector> R2 = MakeShared<FVector>();
				TSharedPtr<FVector2D> A0 = MakeShared<FVector2D>();
				auto Game = [&T]() { return T.Game(); };

				// ---- Helicopter hovering 80 m up ----
				Airborne(T, TEXT("helicopter"), 80);
				GameWait(T, 1);
				T.Log([Game]() { return TEXT("helicopter hovering: ") + State(Game()); });
				T.Shot(TEXT("01-helicopter-help"));
				T.Expect(TEXT("the helicopter's help names the keyboard + mouse controls"), [Game]() { return Game()->LastNoticeText().Contains(TEXT("Keyboard + mouse: W/S collective, A/D pedals, mouse pitch/roll (up = nose down), hold Shift to look around")); });
				// 300 px of mouse up in 0.4 s: the nose goes down and stays down (web: -0.37 rad, still about -0.4 rad 2 s later).
				MouseMove(T, 0, -300, .4);
				T.Log([Game]() { return TEXT("helicopter after 300 px up in 0.4 s: ") + State(Game()); });
				T.Expect(TEXT("helicopter: 300 px up tilts the nose down about 0.37 rad (-0.25 .. -0.5 at the end of the move)"), [Game]() { const double P = PitchOf(Flown(Game())); return P < -.25 && P > -.5; });
				Track(T, 2, [Game]() { return PitchOf(Flown(Game())); }, R);
				T.Log([Game, R]() { return FString::Printf(TEXT("helicopter 2 s later: %s (pitch lowest %.3f)"), *State(Game()), R->X); });
				T.Expect(TEXT("helicopter: still nose down about 0.4 rad 2 s later (-0.3 .. -0.5)"), [R]() { return R->Z < -.3 && R->Z > -.5; });
				T.Expect(TEXT("helicopter: flying forward (over 30 km/h)"), [Game]() { const FHKCraft* C = Flown(Game()); return C && C->GetSpeed() * 3.6 > 30; });
				T.Shot(TEXT("02-helicopter-nose-down"));
				// 300 px right: it banks about +0.49 rad and stays.
				Airborne(T, TEXT("helicopter"), 80);
				GameWait(T, 1);
				MouseMove(T, 300, 0, .4);
				T.Log([Game]() { return TEXT("helicopter after 300 px right in 0.4 s: ") + State(Game()); });
				T.Expect(TEXT("helicopter: 300 px right banks about +0.49 rad (0.39 .. 0.6 at the end of the move)"), [Game]() { const double B = BankOf(Flown(Game())); return B > .39 && B < .6; });
				Track(T, 2, [Game]() { return BankOf(Flown(Game())); }, R);
				T.Log([Game, R]() { return FString::Printf(TEXT("helicopter 2 s later: %s (bank highest %.3f)"), *State(Game()), R->Y); });
				T.Expect(TEXT("helicopter: the bank stays (0.39 .. 0.6 rad 2 s later)"), [R]() { return R->Z > .39 && R->Z < .6; });
				T.Shot(TEXT("03-helicopter-bank"));
				// Free look with Shift: 400 px left turns the view 0.96 rad from the nose; the helicopter does not move.
				Airborne(T, TEXT("helicopter"), 80);
				GameWait(T, 1);
				T.Do([Game, A0]() { const FHKCraft* C = Flown(Game()); *A0 = FVector2D(PitchOf(C), BankOf(C)); });
				T.Do([]() { if (auto I = In()) I->SimulateKey(TEXT("ShiftLeft"), true); });
				MouseMove(T, -400, 0, .4);
				T.Log([Game]() { return FString::Printf(TEXT("Shift + 400 px left: view %.3f rad from the nose (look yaw %.3f, free look %s); %s"), ViewFromNose(Game()), Game()->Player.LookYaw, Game()->Frame.bFreeLook ? TEXT("on") : TEXT("off"), *State(Game())); });
				T.Expect(TEXT("Shift + 400 px left turns the view about 0.96 rad from the nose"), [Game]() { return FMath::Abs(ViewFromNose(Game()) - .96) < .05; });
				T.Expect(TEXT("Shift free look: the helicopter's attitude is unchanged (within 0.02 rad)"), [Game, A0]() { const FHKCraft* C = Flown(Game()); return FMath::Abs(PitchOf(C) - A0->X) < .02 && FMath::Abs(BankOf(C) - A0->Y) < .02; });
				T.Shot(TEXT("04-free-look-shift"));
				T.Do([]() { if (auto I = In()) I->SimulateKey(TEXT("ShiftLeft"), false); });
				Track(T, .5, [Game]() { return ViewFromNose(Game()); }, R);
				T.Log([R]() { return FString::Printf(TEXT("Shift let go: view %.3f rad from the nose after 0.5 s"), R->Z); });
				Track(T, .5, [Game]() { return ViewFromNose(Game()); }, R);
				T.Log([R]() { return FString::Printf(TEXT("Shift let go: view %.3f rad from the nose after 1 s"), R->Z); });
				T.Expect(TEXT("let go of Shift: the view is straight ahead again within about a second (under 0.02 rad)"), [R]() { return FMath::Abs(R->Z) < .02; });
				T.Shot(TEXT("05-free-look-back"));
				// The same with the right mouse button, which no longer brakes while flying.
				T.Do([Game, A0]() { const FHKCraft* C = Flown(Game()); *A0 = FVector2D(PitchOf(C), BankOf(C)); });
				T.Do([]() { if (auto I = In()) I->SimulateMouse(0, 0, 2, true); });
				MouseMove(T, -400, 0, .4);
				T.Log([Game]() { return FString::Printf(TEXT("right mouse button + 400 px left: view %.3f rad from the nose (look yaw %.3f), L2 %.0f; %s"), ViewFromNose(Game()), Game()->Player.LookYaw, Game()->Frame.L2, *State(Game())); });
				T.Expect(TEXT("right mouse button + 400 px left turns the view about 0.96 rad from the nose"), [Game]() { return FMath::Abs(ViewFromNose(Game()) - .96) < .05; });
				T.Expect(TEXT("right mouse button free look: the attitude is unchanged (within 0.02 rad)"), [Game, A0]() { const FHKCraft* C = Flown(Game()); return FMath::Abs(PitchOf(C) - A0->X) < .02 && FMath::Abs(BankOf(C) - A0->Y) < .02; });
				T.Expect(TEXT("the right mouse button does not brake while flying (L2 = 0)"), [Game]() { return Game()->Frame.L2 == 0; });
				T.Do([]() { if (auto I = In()) I->SimulateMouse(0, 0, 2, false); });
				Track(T, 1, [Game]() { return ViewFromNose(Game()); }, R);
				T.Log([R]() { return FString::Printf(TEXT("right mouse button let go: view %.3f rad from the nose after 1 s"), R->Z); });
				T.Expect(TEXT("let go of the right mouse button: the view is straight ahead again within about a second"), [R]() { return FMath::Abs(R->Z) < .02; });
				// The arrow keys still pitch and roll, and using them turns the mouse's hold off.
				T.Expect(TEXT("flying by mouse: hold is on"), [Game]() { return Game()->MouseFly.bHold; });
				T.Key(TEXT("ArrowUp"), .05);
				T.Expect(TEXT("an arrow key turns hold off"), [Game]() { return !Game()->MouseFly.bHold; });

				// ---- FPV drone (angle mode), 40 m above the lawn in Torshovdalen ----
				T.Do([Game]()
				{
					AHKGame* G = Game();
					if (FHKCraft* C = Flown(G)) { G->ExitVehicle(); C->Reset(); }
					const FVector2D W = FHKData::Get().ToWorld(110, 40);
					G->Place(W.X, W.Y, 0);
				});
				GameWait(T, .3);
				T.Do([Game]() { Game()->StartDroneNow(); });
				GameWait(T, .2);
				T.Expect(TEXT("flying the FPV drone"), [Game]() { return Game()->Player.Mode == TEXT("drone"); });
				T.Do([Game]()
				{
					FHKDrone* D = Game()->Drone.Get();
					if (D->Mode != TEXT("angle")) D->SetMode(TEXT("angle"));
					D->Pos.Y += 40; D->Vel = FVector::ZeroVector; D->bLanded = false; D->PitchA = D->RollA = 0;
				});
				GameWait(T, 1);
				auto DroneLog = [Game](const FString& What) { const FHKDrone* D = Game()->Drone.Get(); return FString::Printf(TEXT("drone %s: pitch %.3f, roll %.3f rad, %.0f km/h, hold %s"), *What, D->PitchA, D->RollA, D->Vel.Size() * 3.6, Game()->MouseFly.bHold ? TEXT("on") : TEXT("off")); };
				MouseMove(T, 0, -250, .4);
				T.Log([DroneLog]() { return DroneLog(TEXT("right at the end of 250 px up in 0.4 s")); });
				GameWait(T, .3);
				T.Log([DroneLog]() { return DroneLog(TEXT("0.3 s after 250 px up")); });
				Track(T, 1.5, [Game]() { return Game()->Drone->PitchA; }, R);
				T.Log([DroneLog, R]() { return DroneLog(FString::Printf(TEXT("1.8 s after 250 px up (pitch %.3f .. %.3f over the last 1.5 s)"), R->X, R->Y)); });
				T.Expect(TEXT("drone: 250 px up tilts it about -0.55 rad (-0.5 .. -0.6)"), [R]() { return R->Z < -.5 && R->Z > -.6; });
				T.Expect(TEXT("drone: the tilt holds (no pull back to level)"), [R]() { return R->Y - R->X < .01; });
				T.Shot(TEXT("06-drone-tilted"));
				MouseMove(T, 0, 250, .4);
				GameWait(T, .3);
				T.Log([DroneLog]() { return DroneLog(TEXT("after 250 px down")); });
				T.Expect(TEXT("drone: 250 px down levels it (under 0.02 rad)"), [Game]() { return FMath::Abs(Game()->Drone->PitchA) < .02; });
				MouseMove(T, 200, 0, .4);
				T.Log([DroneLog]() { return DroneLog(TEXT("right at the end of 200 px right")); });
				GameWait(T, .3);
				T.Log([DroneLog]() { return DroneLog(TEXT("0.3 s after 200 px right")); });
				T.Expect(TEXT("drone: 200 px right rolls it about +0.41 rad (0.36 .. 0.5)"), [Game]() { return Game()->Drone->RollA > .36 && Game()->Drone->RollA < .5; });
				T.Do([Game]() { Game()->StopDrone(); });

				// ---- Plane at 45 m/s, 150 m up ----
				Airborne(T, TEXT("plane"), 150, 45, .4);
				GameWait(T, 1.5);
				T.Do([Game, A0]() { *A0 = FVector2D(PitchOf(Flown(Game())), 0); });
				T.Log([Game]() { return TEXT("plane flying: ") + State(Game()); });
				MouseMove(T, 0, 250, .4);
				Track(T, 1.5, [Game]() { return PitchOf(Flown(Game())); }, R2);
				T.Log([A0, R2, Game]() { return FString::Printf(TEXT("plane: 250 px down in 0.4 s raises the nose from %.3f to %.3f rad (web 0.03 -> 0.28); now %s"), A0->X, R2->Y, *State(Game())); });
				T.Expect(TEXT("plane: mouse down raises the nose (by more than 0.15 rad)"), [A0, R2]() { return R2->Y - A0->X > .15; });
				T.Shot(TEXT("07-plane-nose-up"));
				T.Do([Game]() { AHKGame* G = Game(); if (FHKCraft* C = Flown(G)) { G->ExitVehicle(); C->Reset(); } });

				// ---- On foot: Shift still toggles running, the right mouse button is still L2 ----
				T.Do([Game]() { const FVector2D W = FHKData::Get().ToWorld(110, 40); Game()->Place(W.X, W.Y, 0); });
				GameWait(T, .3);
				// Walking (the run toggle clears when you stand still, as in the web).
				TSharedPtr<bool> Run0 = MakeShared<bool>(false);
				T.Do([]() { if (auto I = In()) I->SimulateKey(TEXT("KeyW"), true); });
				GameWait(T, .2);
				T.Do([Game, Run0]() { *Run0 = Game()->Player.bRun; });
				T.Key(TEXT("ShiftLeft"), .05);
				T.Expect(TEXT("on foot: Shift toggles running"), [Game, Run0]() { return Game()->Player.bRun != *Run0 && !Game()->Frame.bFreeLook; });
				T.Do([]() { if (auto I = In()) I->SimulateKey(TEXT("KeyW"), false); });
				T.Do([]() { if (auto I = In()) I->SimulateMouse(0, 0, 2, true); });
				GameWait(T, .1);
				T.Expect(TEXT("on foot: the right mouse button is still L2"), [Game]() { return Game()->Frame.L2 > .5; });
				T.Do([]() { if (auto I = In()) I->SimulateMouse(0, 0, 2, false); });
				T.Do([&T, OldSens]() { AHKGame* G = T.Game(); G->Sensitivity.Mouse = *OldSens; G->FixedDt = 0; });
			});
		}
	} GFlightRegister;
}
