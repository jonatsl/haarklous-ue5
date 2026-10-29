// Automation tests for the vehicle and aircraft port (HKVehicles, HKAircraft).
// Reference answers: tools/web-export/reference-vehicles.js -> Content/Data/tests/vehicles.json, recorded from the web
// game's own classes driven with fixed steps (1/60 s) and scripted inputs. The tests replay the same inputs on the C++
// classes and compare the state after every recorded step (every 3rd update), node poses every 9th.
// Run: powershell -File tools\run-tests.ps1 -Filter Haarklous.Vehicles
#include "Misc/AutomationTest.h"
#include "HKAircraft.h"
#include "HKData.h"
#include "HKVehicleMath.h"
#include "HKVehicles.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TSharedPtr<FJsonObject> LoadVehiclesRef(FAutomationTestBase& Test)
	{
		static TSharedPtr<FJsonObject> Cached;
		if (Cached) return Cached;
		FString Error;
		Cached = HKJson::LoadFile(FHKData::DataDir() / TEXT("tests") / TEXT("vehicles.json"), &Error);
		if (!Cached) Test.AddError(Error);
		return Cached;
	}

	// The recording ran without dynamic obstacles in the vehicle worlds and the walk world; do the same here.
	struct FNoDynamic
	{
		TArray<TPair<FHKWalkWorld*, TArray<FHKObstacle>>> Saved;
		FNoDynamic()
		{
			FHKData& D = FHKData::Get();
			TArray<FHKWalkWorld*> Worlds = {&D.WalkWorld};
			for (double R : {.45, .95, 1.6, 1.7, 5.2, 3.6, 1.25}) Worlds.Add(&D.VehicleWorld(R));
			for (FHKWalkWorld* W : Worlds) { Saved.Add({W, W->Dynamic}); W->SetDynamic({}); }
		}
		~FNoDynamic() { for (auto& S : Saved) S.Key->SetDynamic(S.Value); }
	};

	// Records the sound calls (names) so a test can check them.
	class FRecordingAudio : public FHKAudio
	{
	public:
		TArray<FString> Calls;
		virtual void Sfx(const FString& Name, const FHKSfxOpts& Opts) override { Calls.Add(Name); }
		virtual TSharedPtr<IHKEngineSound> Engine(const FString& Kind) override { Calls.Add(TEXT("engine:") + Kind); return FHKAudio::Engine(Kind); }
	};

	FHKVehicleInput InputOf(const TSharedPtr<FJsonValue>& V)
	{
		FHKVehicleInput In;
		if (!V.IsValid() || V->IsNull()) return In;
		const TArray<double> A = HKJson::Nums(V);
		if (A.Num() < 11) return In;
		In.bEmpty = false;
		In.Throttle = A[0]; In.Brake = A[1]; In.Steer = A[2]; In.L1 = A[3] != 0; In.R1 = A[4] != 0; In.L2 = A[5]; In.R2 = A[6];
		In.LX = A[7]; In.LY = A[8]; In.RX = A[9]; In.RY = A[10];
		if (A.Num() > 11) In.Drunk = A[11];
		return In;
	}

	void AddPose(TArray<double>& Out, const FHKCameraPose& P)
	{
		Out.Append({P.Position.X, P.Position.Y, P.Position.Z, P.Quaternion.X, P.Quaternion.Y, P.Quaternion.Z, P.Quaternion.W});
	}
	void AddFoot(TArray<double>& Out, const TArray<FHKObstacle>& F)
	{
		for (const FHKObstacle& O : F) { for (const FHKPt& P : O.Points) Out.Append({P.X, P.Z}); Out.Append({O.MinY, O.MaxY}); }
	}
	void AddHip(TArray<double>& Out, const FHKHipAnchor& H)
	{
		if (!H.bValid) Out.Append({0, 0, 0, 0}); else Out.Append({1, H.Pos.X, H.Pos.Y, H.Pos.Z});
	}

	// Per field: largest difference and how many values were compared.
	struct FFieldStat { double MaxDiff = 0; int32 MaxStep = -1; int32 Count = 0; int32 Bad = 0; int32 RoundFlips = 0; };

	struct FScenarioRun
	{
		FAutomationTestBase& Test;
		FString Name, Kind;
		TSharedPtr<FJsonObject> Sc;
		TUniquePtr<FHKVehicle> Vehicle;
		TUniquePtr<FHKDrone> Drone;
		FHKDroneEnv Env;
		FVector Chase = FVector::ZeroVector;
		bool bHasChase = false;
		TArray<FVector> Dumps;   // x, z, step
		TMap<FString, FFieldStat> Stats;
		int32 Errors = 0, HudBad = 0, HudCount = 0, NodeCount = 0;
		double NodeMaxPos = 0, NodeMaxQuat = 0, NodeMaxScale = 0;
		int32 VisBad = 0;

		FScenarioRun(FAutomationTestBase& T) : Test(T) {}

		void Error(const FString& S) { if (Errors++ < 12) Test.AddError(Name + TEXT(": ") + S); }

		FHKGroundVehicle* Ground() const { return static_cast<FHKGroundVehicle*>(Vehicle.Get()); }
		FHKCraft* Craft() const { return static_cast<FHKCraft*>(Vehicle.Get()); }

		TArray<double> Field(const FString& F)
		{
			TArray<double> O;
			if (Drone)
			{
				FHKDrone& D = *Drone;
				if (F == TEXT("pos")) O = {D.Pos.X, D.Pos.Y, D.Pos.Z};
				else if (F == TEXT("vel")) O = {D.Vel.X, D.Vel.Y, D.Vel.Z};
				else if (F == TEXT("q")) O = {D.Q.X, D.Q.Y, D.Q.Z, D.Q.W};
				else if (F == TEXT("yawA")) O = {D.YawA};
				else if (F == TEXT("pitchA")) O = {D.PitchA};
				else if (F == TEXT("rollA")) O = {D.RollA};
				else if (F == TEXT("battery")) O = {D.Battery};
				else if (F == TEXT("armed")) O = {D.bArmed ? 1.0 : 0.0};
				else if (F == TEXT("crashed")) O = {D.bCrashed ? 1.0 : 0.0};
				else if (F == TEXT("landed")) O = {D.bLanded ? 1.0 : 0.0};
				else if (F == TEXT("thrust")) O = {D.Thrust};
				else if (F == TEXT("time")) O = {D.Time};
				else if (F == TEXT("home")) O = {D.Home.X, D.Home.Y, D.Home.Z};
				else if (F == TEXT("cam")) AddPose(O, D.CameraPose());
				else if (F == TEXT("osd"))
				{
					const FHKDroneOsd S = D.Osd(GroundBelow(D.Pos));
					O = {(double)S.Battery, S.Alt, S.Speed, S.Time, S.Dist, S.bArmed ? 1.0 : 0.0, S.bCrashed ? 1.0 : 0.0, S.Thrust};
				}
				else Error(TEXT("unknown field ") + F);
				return O;
			}
			FHKVehicle& V = *Vehicle;
			if (V.bAircraft)
			{
				FHKCraft& C = *Craft();
				if (F == TEXT("pos")) O = {C.Pos.X, C.Pos.Y, C.Pos.Z};
				else if (F == TEXT("vel")) O = {C.Vel.X, C.Vel.Y, C.Vel.Z};
				else if (F == TEXT("q")) O = {C.Q.X, C.Q.Y, C.Q.Z, C.Q.W};
				else if (F == TEXT("w")) O = {C.W.X, C.W.Y, C.W.Z};
				else if (F == TEXT("x")) O = {C.X};
				else if (F == TEXT("y")) O = {C.Y};
				else if (F == TEXT("z")) O = {C.Z};
				else if (F == TEXT("yaw")) O = {C.Yaw};
				else if (F == TEXT("speed")) O = {C.Speed};
				else if (F == TEXT("grounded")) O = {C.bGrounded ? 1.0 : 0.0};
				else if (F == TEXT("broken")) O = {C.bBroken ? 1.0 : 0.0};
				else if (F == TEXT("crashT")) O = {C.CrashT};
				else if (F == TEXT("active")) O = {C.bActive ? 1.0 : 0.0};
				else if (F == TEXT("net")) O = C.NetGet();
				else if (F == TEXT("cam")) AddPose(O, C.CameraPose(false, nullptr));
				else if (F == TEXT("chase")) { const FHKCameraPose P = C.CameraPose(true, bHasChase ? &Chase : nullptr); Chase = P.Position; bHasChase = true; AddPose(O, P); }
				else if (F == TEXT("foot")) AddFoot(O, C.Footprint());
				else if (F == TEXT("exitPt")) { const FVector2D E = C.ExitPoint(); O = {E.X, E.Y}; }
				else if (F == TEXT("hipD")) AddHip(O, C.HipAnchor(TEXT("d")));
				else if (F == TEXT("hipP")) AddHip(O, C.HipAnchor(TEXT("p")));
				else if (F == TEXT("rpm")) O = {C.Rpm};
				else if (F == TEXT("rotorAngle")) O = {static_cast<FHKHelicopter&>(C).RotorAngle};
				else if (F == TEXT("bladeOpacity")) O = {C.BladeOpacity};
				else if (F == TEXT("discOpacity")) O = {C.DiscOpacity};
				else if (F == TEXT("throttle")) O = {C.Throttle};
				else if (F == TEXT("prop")) O = {C.Prop};
				else Error(TEXT("unknown field ") + F);
				return O;
			}
			FHKGroundVehicle& G = *Ground();
			if (F == TEXT("x")) O = {G.X};
			else if (F == TEXT("y")) O = {G.Y};
			else if (F == TEXT("z")) O = {G.Z};
			else if (F == TEXT("yaw")) O = {G.Yaw};
			else if (F == TEXT("speed")) O = {G.Speed};
			else if (F == TEXT("pitch")) O = {G.Pitch};
			else if (F == TEXT("roll")) O = {G.Roll};
			else if (F == TEXT("terrainRoll")) O = {G.TerrainRoll};
			else if (F == TEXT("active")) O = {G.bActive ? 1.0 : 0.0};
			else if (F == TEXT("seat")) { const FHKSeatPose S = G.Seat(); O = {S.X, S.Y, S.Z, S.Yaw, S.Pitch, S.Roll, S.Lateral}; }
			else if (F == TEXT("net")) O = G.NetGet();
			else if (F == TEXT("foot")) AddFoot(O, G.Footprint());
			else if (F == TEXT("hipD")) AddHip(O, G.HipAnchor(TEXT("d")));
			else if (F == TEXT("hipP")) AddHip(O, G.HipAnchor(TEXT("p")));
			else if (F == TEXT("wheelSpin")) O = {G.WheelSpin};
			else if (F == TEXT("rpm")) O = {G.Rpm};
			else if (F == TEXT("gear")) O = {(double)G.Gear};
			else if (F == TEXT("lean")) O = {static_cast<FHKMotorcycle&>(G).Lean};
			else if (F == TEXT("bar")) O = {static_cast<FHKMotorcycle&>(G).Bar};
			else if (F == TEXT("steerAngle")) O = {static_cast<FHKCar&>(G).SteerAngle};
			else if (F == TEXT("air")) O = {G.Vy, G.AirT, G.bAirborne ? 1.0 : 0.0, G.Squash};
			else if (G.Kind == TEXT("excavator"))
			{
				FHKExcavator& E = static_cast<FHKExcavator&>(G);
				if (F == TEXT("swing")) O = {E.Swing};
				else if (F == TEXT("boom")) O = {E.Boom};
				else if (F == TEXT("stickA")) O = {E.StickA};
				else if (F == TEXT("bucketA")) O = {E.BucketA};
				else if (F == TEXT("load")) O = {E.Load};
				else if (F == TEXT("vl")) O = {E.Vl};
				else if (F == TEXT("vr")) O = {E.Vr};
				else if (F == TEXT("trackPhase")) O = {E.TrackPhase};
				else if (F == TEXT("tip")) { const FVector T = E.Tip->GetWorldPosition(); O = {T.X, T.Y, T.Z}; }
				else if (F == TEXT("dirtVisible")) O = {E.Dirt->bVisible ? 1.0 : 0.0};
				else if (F == TEXT("handsVisible")) O = {E.HandsVisible() ? 1.0 : 0.0};
				else Error(TEXT("unknown field ") + F);
			}
			else Error(TEXT("unknown field ") + F);
			return O;
		}

		static double GroundBelow(const FVector& P)
		{
			const TOptional<FHKDroneFloor> Fl = FHKDrone::FloorBelow(FHKData::Get().WalkWorld, P, P.Y + .1);
			return Fl.IsSet() ? Fl->Y : FHKData::Get().HeightWorld(P.X, P.Z);
		}

		FString DroneText() const
		{
			const FHKDroneOsd O = Drone->Osd(GroundBelow(Drone->Pos));
			return O.Mode + TEXT(" ") + O.Volts;
		}

		bool Create(const TArray<double>& C)
		{
			if (Kind == TEXT("motorcycle")) Vehicle = MakeUnique<FHKMotorcycle>(C[0], C[1], C[2], (uint32)C[3]);
			else if (Kind == TEXT("car")) Vehicle = MakeUnique<FHKCar>(C[0], C[1], C[2], (int32)C[3]);
			else if (Kind == TEXT("excavator")) Vehicle = MakeUnique<FHKExcavator>(C[0], C[1], C[2]);
			else if (Kind == TEXT("helicopter")) Vehicle = MakeUnique<FHKHelicopter>(C[0], C[1], C[2]);
			else if (Kind == TEXT("plane")) Vehicle = MakeUnique<FHKPlane>(C[0], C[1], C[2]);
			else if (Kind == TEXT("drone"))
			{
				Drone = MakeUnique<FHKDrone>();
				const TArray<double> L = HKJson::Nums(Sc, TEXT("listener"));
				Env.Floor = [](const FVector& P, double Y) { return FHKDrone::FloorBelow(FHKData::Get().WalkWorld, P, Y); };
				Env.Solid = [](const FVector& P, double Y0, double Y1) { return FHKData::Get().WalkWorld.HitsSolid(FHKPt(P.X, P.Z), Y0, Y1); };
				Env.bHasListener = L.Num() == 3;
				if (Env.bHasListener) Env.Listener = FVector(L[0], L[1], L[2]);
			}
			else { Error(TEXT("unknown kind ") + Kind); return false; }
			if (Vehicle && Vehicle->Kind == TEXT("excavator"))
			{
				Vehicle->OnDump = [this](double X, double Z) { Dumps.Add(FVector(X, Z, StepIndex)); };
			}
			if (Kind == TEXT("car") || Kind == TEXT("motorcycle"))
			{
				Ground()->OnLand = [this](double Impact) { Lands.Add(FVector2D(Impact, StepIndex)); };
			}
			return true;
		}
		int32 StepIndex = 0;
		TArray<FVector2D> Lands;   // impact, step

		void Compare(const TSharedPtr<FJsonObject>& Step, int32 Si, const TArray<FString>& Fields, const TArray<int32>& Sizes)
		{
			const TArray<double> Ref = HKJson::Nums(Step, TEXT("v"));
			int32 Off = 0;
			for (int32 f = 0; f < Fields.Num(); f++)
			{
				const TArray<double> Mine = Field(Fields[f]);
				const int32 Nn = Sizes.IsValidIndex(f) ? Sizes[f] : 0;
				FFieldStat& St = Stats.FindOrAdd(Fields[f]);
				if (Mine.Num() != Nn) { Error(FString::Printf(TEXT("step %d: field %s has %d values, web %d"), Si, *Fields[f], Mine.Num(), Nn)); Off += Nn; continue; }
				const bool bNet = Fields[f] == TEXT("net");
				const double Unit = Vehicle && Vehicle->bAircraft ? 1e-4 : 1e-3;
				for (int32 k = 0; k < Nn; k++)
				{
					const double R = Ref.IsValidIndex(Off + k) ? Ref[Off + k] : NAN, M = Mine[k];
					double D = FMath::Abs(M - R);
					if (R >= 1e8 && M >= 1e8) D = 0;
					St.Count++;
					// netGet() values are rounded to 1/1000 (1/10000 for aircraft): a value right at a rounding edge may flip.
					if (bNet && D > 1e-9 && D < Unit * 1.0001) { St.RoundFlips++; continue; }
					if (D > St.MaxDiff || FMath::IsNaN(D)) { St.MaxDiff = FMath::IsNaN(D) ? 1e9 : D; St.MaxStep = Si; }
					const double Tol = 1e-6 * FMath::Max(1.0, FMath::Abs(R));
					if (!(D <= Tol))
					{
						St.Bad++;
						Error(FString::Printf(TEXT("step %d (%s): %s[%d] = %.9f, web %.9f"), Si, *HKJson::Str(Step, TEXT("op")), *Fields[f], k, M, R));
					}
				}
				Off += Nn;
			}
			FString H;
			if (Step->TryGetStringField(TEXT("h"), H))
			{
				HudCount++;
				const FString Mine = Drone ? DroneText() : (Vehicle->bBroken ? FString(TEXT("CRASHED")) : Vehicle->Hud());
				if (Mine != H) { HudBad++; if (HudBad <= 3) Test.AddWarning(FString::Printf(TEXT("%s step %d hud '%s', web '%s'"), *Name, Si, *Mine, *H)); }
			}
		}

		void CompareNodes(const TSharedPtr<FJsonObject>& Step, int32 Si, const TArray<FString>& Nodes)
		{
			const TArray<double> Ref = HKJson::Nums(Step, TEXT("n"));
			if (Ref.Num() != Nodes.Num() * 11) { Error(TEXT("node values")); return; }
			Vehicle->Rig.Root->UpdateMatrixWorld();
			for (int32 i = 0; i < Nodes.Num(); i++)
			{
				FHKNode* Nd = Nodes[i] == TEXT("root") ? Vehicle->Rig.Root : Vehicle->Rig.Find(Nodes[i]);
				if (!Nd) { Error(TEXT("rig has no node ") + Nodes[i]); continue; }
				FVector P, S;
				FQuat Q;
				Nd->MatrixWorld.Decompose(P, Q, S);
				const double* R = &Ref[i * 11];
				const double Dp = FMath::Max3(FMath::Abs(P.X - R[0]), FMath::Abs(P.Y - R[1]), FMath::Abs(P.Z - R[2]));
				const double Dq1 = FMath::Max(FMath::Max(FMath::Abs(Q.X - R[3]), FMath::Abs(Q.Y - R[4])), FMath::Max(FMath::Abs(Q.Z - R[5]), FMath::Abs(Q.W - R[6])));
				const double Dq2 = FMath::Max(FMath::Max(FMath::Abs(Q.X + R[3]), FMath::Abs(Q.Y + R[4])), FMath::Max(FMath::Abs(Q.Z + R[5]), FMath::Abs(Q.W + R[6])));
				const double Dq = FMath::Min(Dq1, Dq2);
				const double Ds = FMath::Max3(FMath::Abs(S.X - R[7]), FMath::Abs(S.Y - R[8]), FMath::Abs(S.Z - R[9]));
				const bool bVis = Nd->IsVisibleInTree();
				NodeCount++;
				NodeMaxPos = FMath::Max(NodeMaxPos, Dp); NodeMaxQuat = FMath::Max(NodeMaxQuat, Dq); NodeMaxScale = FMath::Max(NodeMaxScale, Ds);
				if (bVis != (R[10] != 0)) { VisBad++; Error(FString::Printf(TEXT("step %d: node %s visible %d, web %d"), Si, *Nodes[i], bVis ? 1 : 0, (int32)R[10])); }
				if (Dp > 2e-6 * FMath::Max(1.0, FMath::Abs(R[0]) + FMath::Abs(R[2])) || Dq > 2e-6 || Ds > 1e-6)
					Error(FString::Printf(TEXT("step %d: node %s pos (%.6f %.6f %.6f) web (%.6f %.6f %.6f), quat diff %.2e, scale diff %.2e"), Si, *Nodes[i], P.X, P.Y, P.Z, R[0], R[1], R[2], Dq, Ds));
			}
		}

		void Run(const FString& InName)
		{
			Name = InName;
			const TSharedPtr<FJsonObject> Root = LoadVehiclesRef(Test);
			if (!Root) return;
			for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(Root, TEXT("scenarios"))) if (HKJson::Str(V->AsObject(), TEXT("name")) == Name) Sc = V->AsObject();
			if (!Sc) { Error(TEXT("no such scenario")); return; }
			Kind = HKJson::Str(Sc, TEXT("kind"));
			const double Dt = HKJson::Num(Sc, TEXT("dt"));
			TArray<FString> Fields, Nodes;
			for (const TSharedPtr<FJsonValue>& F : HKJson::Arr(Sc, TEXT("fields"))) Fields.Add(F->AsString());
			for (const TSharedPtr<FJsonValue>& F : HKJson::Arr(Sc, TEXT("nodes"))) Nodes.Add(F->AsString());
			TArray<int32> Sizes;
			for (double S : HKJson::Nums(Sc, TEXT("sizes"))) Sizes.Add((int32)S);
			FNoDynamic NoDyn;
			// Math.random was a seeded Park-Miller generator during the recording.
			HK::FParkMiller Rng(HKJson::Num(Root, TEXT("randomSeed"), 777));
			HKVehicles::SetRandom([&Rng]() { return Rng.Next(); });
			FRecordingAudio Rec;
			FHKAudio::SetInstance(&Rec);
			if (!Create(HKJson::Nums(Sc, TEXT("ctor")))) { FHKAudio::SetInstance(nullptr); HKVehicles::SetRandom(nullptr); return; }
			const TArray<TSharedPtr<FJsonValue>>& Steps = HKJson::Arr(Sc, TEXT("steps"));
			int32 Compared = 0;
			for (int32 Si = 0; Si < Steps.Num() && Errors < 40; Si++)
			{
				StepIndex = Si;
				const TSharedPtr<FJsonObject> St = Steps[Si]->AsObject();
				const FString Op = HKJson::Str(St, TEXT("op"));
				if (Op == TEXT("u"))
				{
					// Drunk steps: the web's performance.now() was scripted (step.now) for the wobble.
					double Now = 0;
					if (St->TryGetNumberField(TEXT("now"), Now)) HKVehicles::SetClock([Now]() { return Now; });
					else HKVehicles::SetClock(nullptr);
					const FHKVehicleInput In = InputOf(St->TryGetField(TEXT("in")));
					if (Drone) Drone->Update(Dt, In, Env); else Vehicle->Update(Dt, In);
				}
				else if (Op == TEXT("enter")) Vehicle->Enter();
				else if (Op == TEXT("exit"))
				{
					const FVector2D E = Vehicle->Exit();
					const TArray<double> R = HKJson::Nums(St, TEXT("r"));
					if (R.Num() == 2 && (FMath::Abs(E.X - R[0]) > 1e-6 || FMath::Abs(E.Y - R[1]) > 1e-6)) Error(FString::Printf(TEXT("exit point (%.6f, %.6f), web (%.6f, %.6f)"), E.X, E.Y, R[0], R[1]));
				}
				else if (Op == TEXT("net")) Vehicle->NetSet(HKJson::Nums(St, TEXT("a")));
				else if (Op == TEXT("speed")) Vehicle->Speed = HKJson::Num(St, TEXT("s"));
				else if (Op == TEXT("anim")) Vehicle->NetAnimate(Dt);
				else if (Op == TEXT("launch")) { const TArray<double> C = HKJson::Nums(Sc, TEXT("ctor")); Drone->Launch(C[0], C[1], C[2], C[3]); }
				else if (Op == TEXT("mode")) Drone->SetMode(HKJson::Str(St, TEXT("m")));
				else if (Op == TEXT("reset")) Drone->Reset();
				else if (Op == TEXT("battery")) Drone->Battery = HKJson::Num(St, TEXT("b"));
				else if (Op == TEXT("stop")) Drone->Stop();
				else if (Op != TEXT("init")) Error(TEXT("unknown op ") + Op);
				if (St->HasField(TEXT("v"))) { Compare(St, Si, Fields, Sizes); Compared++; }
				if (St->HasField(TEXT("n")) && Vehicle) CompareNodes(St, Si, Nodes);
			}
			FHKAudio::SetInstance(nullptr);
			HKVehicles::SetRandom(nullptr);
			HKVehicles::SetClock(nullptr);
			// Dump events of the excavator.
			const TArray<TSharedPtr<FJsonValue>>* RefDumps;
			if (Sc->TryGetArrayField(TEXT("dumps"), RefDumps))
			{
				if (RefDumps->Num() != Dumps.Num()) Error(FString::Printf(TEXT("%d dumps, web %d"), Dumps.Num(), RefDumps->Num()));
				for (int32 i = 0; i < FMath::Min(RefDumps->Num(), Dumps.Num()); i++)
				{
					const TArray<double> R = HKJson::Nums((*RefDumps)[i]);
					if (FMath::Abs(Dumps[i].X - R[0]) > 1e-6 || FMath::Abs(Dumps[i].Y - R[1]) > 1e-6 || (int32)Dumps[i].Z != (int32)R[2]) Error(FString::Printf(TEXT("dump %d at (%.4f, %.4f) step %d, web (%.4f, %.4f) step %d"), i, Dumps[i].X, Dumps[i].Y, (int32)Dumps[i].Z, R[0], R[1], (int32)R[2]));
				}
			}
			// Landings of cars and motorcycles (jump scenarios): impact (m/s) and step.
			const TArray<TSharedPtr<FJsonValue>>* RefLands;
			if (Sc->TryGetArrayField(TEXT("lands"), RefLands))
			{
				if (RefLands->Num() != Lands.Num()) Error(FString::Printf(TEXT("%d landings, web %d"), Lands.Num(), RefLands->Num()));
				for (int32 i = 0; i < FMath::Min(RefLands->Num(), Lands.Num()); i++)
				{
					const TArray<double> R = HKJson::Nums((*RefLands)[i]);
					if (FMath::Abs(Lands[i].X - R[0]) > 1e-6 || (int32)Lands[i].Y != (int32)R[1]) Error(FString::Printf(TEXT("landing %d: impact %.6f m/s at step %d, web %.6f at step %d"), i, Lands[i].X, (int32)Lands[i].Y, R[0], (int32)R[1]));
				}
				if (Lands.Num()) Test.AddInfo(FString::Printf(TEXT("%s: first landing at step %d, impact %.3f m/s"), *Name, (int32)Lands[0].Y, Lands[0].X));
			}
			// Report.
			FString Worst;
			double WorstD = 0;
			TArray<FString> Lines;
			for (const auto& KV : Stats)
			{
				if (KV.Value.MaxDiff > WorstD) { WorstD = KV.Value.MaxDiff; Worst = KV.Key; }
				Lines.Add(FString::Printf(TEXT("%s %.1e%s"), *KV.Key, KV.Value.MaxDiff, KV.Value.RoundFlips ? *FString::Printf(TEXT(" (%d rounding flips)"), KV.Value.RoundFlips) : TEXT("")));
			}
			Test.AddInfo(FString::Printf(TEXT("%s: %d steps replayed, %d states compared; largest difference %.2e (%s)"), *Name, Steps.Num(), Compared, WorstD, *Worst));
			Test.AddInfo(Name + TEXT(" max differences: ") + FString::Join(Lines, TEXT(", ")));
			if (NodeCount) Test.AddInfo(FString::Printf(TEXT("%s: %d node poses compared, largest differences pos %.2e, quat %.2e, scale %.2e, visibility mismatches %d"), *Name, NodeCount, NodeMaxPos, NodeMaxQuat, NodeMaxScale, VisBad));
			if (HudCount) Test.AddInfo(FString::Printf(TEXT("%s: %d / %d HUD strings match the web version"), *Name, HudCount - HudBad, HudCount));
			if (HudBad > HudCount / 200) Error(FString::Printf(TEXT("%d HUD strings differ"), HudBad));
			int32 Flips = 0, NetCount = 0;
			if (const FFieldStat* S = Stats.Find(TEXT("net"))) { Flips = S->RoundFlips; NetCount = S->Count; }
			if (Flips > NetCount / 500 + 2) Error(FString::Printf(TEXT("%d of %d netGet values differ by one rounding unit"), Flips, NetCount));
			// Sounds: names used by the port (the web's names).
			TMap<FString, int32> Count;
			for (const FString& C : Rec.Calls) Count.FindOrAdd(C)++;
			TArray<FString> Parts;
			for (const auto& KV : Count) Parts.Add(FString::Printf(TEXT("%s x%d"), *KV.Key, KV.Value));
			Test.AddInfo(Name + TEXT(" sounds: ") + FString::Join(Parts, TEXT(", ")));
			ExpectedSounds(Count);
		}

		void ExpectedSounds(const TMap<FString, int32>& C)
		{
			auto Has = [&](const TCHAR* S) { if (!C.Contains(S)) Error(FString::Printf(TEXT("no '%s' sound"), S)); };
			if (Kind == TEXT("motorcycle")) { Has(TEXT("starter")); Has(TEXT("engine:bike")); }
			else if (Kind == TEXT("car")) { Has(TEXT("carDoor")); Has(TEXT("starter")); Has(TEXT("engine:car")); if (Name == TEXT("carCrash")) Has(TEXT("hit")); }
			if (Name.EndsWith(TEXT("Jump"))) Has(TEXT("thud"));
			else if (Kind == TEXT("excavator")) { Has(TEXT("carDoor")); Has(TEXT("engine:excavator")); Has(TEXT("scrape")); Has(TEXT("dump")); }
			else if (Kind == TEXT("helicopter")) { Has(TEXT("starter")); Has(TEXT("engine:heli")); Has(TEXT("crash")); }
			else if (Kind == TEXT("plane")) { Has(TEXT("starter")); Has(TEXT("engine:plane")); Has(TEXT("crash")); }
			else if (Kind == TEXT("drone")) { Has(TEXT("arm")); Has(TEXT("engine:drone")); Has(TEXT("crash")); }
		}
	};
}

#define HK_VEHICLE_SCENARIO_TEST(Class, TestName, Scenario) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, "Haarklous.Vehicles." TestName, EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter) \
	bool Class::RunTest(const FString& Parameters) \
	{ \
		if (!TestTrue(TEXT("data loaded"), FHKData::Get().bLoaded)) return false; \
		FScenarioRun R(*this); \
		R.Run(TEXT(Scenario)); \
		return R.Errors == 0; \
	}

HK_VEHICLE_SCENARIO_TEST(FHKVehMotorcycleTest, "Motorcycle", "motorcycle")
HK_VEHICLE_SCENARIO_TEST(FHKVehCarTest, "Car", "car")
HK_VEHICLE_SCENARIO_TEST(FHKVehCarCrashTest, "CarCrash", "carCrash")
HK_VEHICLE_SCENARIO_TEST(FHKVehExcavatorTest, "Excavator", "excavator")
HK_VEHICLE_SCENARIO_TEST(FHKVehHelicopterTest, "Helicopter", "helicopter")
HK_VEHICLE_SCENARIO_TEST(FHKVehPlaneTest, "Plane", "plane")
HK_VEHICLE_SCENARIO_TEST(FHKVehDroneTest, "Drone", "drone")
HK_VEHICLE_SCENARIO_TEST(FHKVehDroneIndoorTest, "DroneIndoor", "droneIndoor")
HK_VEHICLE_SCENARIO_TEST(FHKVehHeliStrikeTest, "HelicopterRotorStrike", "helicopterStrike")
HK_VEHICLE_SCENARIO_TEST(FHKVehCarDrunkTest, "CarDrunk", "carDrunk")
HK_VEHICLE_SCENARIO_TEST(FHKVehBikeDrunkTest, "MotorcycleDrunk", "motorcycleDrunk")
HK_VEHICLE_SCENARIO_TEST(FHKVehCarJumpTest, "CarJump", "carJump")
HK_VEHICLE_SCENARIO_TEST(FHKVehBikeJumpTest, "MotorcycleJump", "motorcycleJump")

/*
 * Gravity and jumps (issue #4 = web PR #5) with the checks of the web version: the grass hump at local (392, -392),
 * driven along local (0.707, 0.707) in fixed 1/60 s steps from 30 m before the crest, the speed held by the throttle.
 * Car 50 km/h stays on the ground; car 90 km/h flies about 0.57 s / 14 m / up to 0.22 m above the ground; motorcycle
 * 110 km/h about 0.68 s / 21 m / 0.85 m; the vertical acceleration in the air is -9.81 m/s². In the air throttle, brake
 * and steering do nothing; the landing thuds and dips the seat. The excavator does not fly.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKVehJumpTest, "Haarklous.Vehicles.Jump", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKVehJumpTest::RunTest(const FString& Parameters)
{
	FHKData& Data = FHKData::Get();
	if (!TestTrue(TEXT("data loaded"), Data.bLoaded)) return false;
	FNoDynamic NoDyn;
	FRecordingAudio Rec;
	FHKAudio::SetInstance(&Rec);
	const double Dt = 1.0 / 60, Yaw = HKVehicles::LocalHeadingToYaw(std::atan2(.707, .707));
	const FVector2D Crest = Data.ToWorld(392, -392), F(-std::sin(Yaw), -std::cos(Yaw));
	struct FFlight
	{
		double OffT = 0, AirborneT = 0, Dist = 0, MaxH = 0, MaxG = 0, Impact = 0, TakeoffAt = 0, LandAt = 0, MinAcc = 1e9, MaxAcc = -1e9, MaxYawChange = 0, MaxSpeedErr = 0;
		int32 Landings = 0, AccN = 0, Blocked = 0;
		bool bFlew = false, bThud = false, bSeatDip = false;
	};
	// Same run as the recorded "carJump" / "motorcycleJump" scenarios (tools/web-export/reference-vehicles.js): 30 m before the
	// crest with the speed set at once (a short hop at the start), a cruise control on the ground, full throttle, half brake
	// and steering in the air over the crest. Measured: the flight that takes off within 5 m of the crest.
	auto Drive = [&](const FString& Kind, double Kmh)
	{
		FFlight R;
		TUniquePtr<FHKGroundVehicle> V;
		const double X0 = Crest.X - F.X * 30, Z0 = Crest.Y - F.Y * 30;
		if (Kind == TEXT("car")) V = MakeUnique<FHKCar>(X0, Z0, Yaw, 4); else V = MakeUnique<FHKMotorcycle>(X0, Z0, Yaw, 0x9e1b20);
		const bool bCar = Kind == TEXT("car");
		auto Along = [&]() { return (V->X - Crest.X) * F.X + (V->Z - Crest.Y) * F.Y; };
		V->Enter();
		const double Target = Kmh / 3.6;
		V->Speed = Target;
		FVector2D Start(X0, Z0);
		double StartAt = -1e9, CrestAirT = 0;
		TArray<double> Ys;   // heights of consecutive steps off the ground
		V->OnLand = [&](double Impact)
		{
			R.Landings++;
			if (R.bFlew || StartAt <= -5) return;
			R.bFlew = true;
			R.OffT = V->AirT; R.Impact = Impact; R.TakeoffAt = StartAt;
			R.Dist = HKV::Hypot(V->X - Start.X, V->Z - Start.Y);
			R.LandAt = Along();
		};
		for (int32 i = 0; i < 360 && !R.bFlew; i++)
		{
			const bool bAir = V->bAirborne, bOverCrest = bAir && Along() > -5;
			FHKVehicleInput In;
			In.bEmpty = false;
			if (bOverCrest) { In.Throttle = 1; In.Brake = .5; In.Steer = .6; }
			else
			{
				// Cruise control: the throttle whose acceleration lands exactly on the target speed.
				const double S = V->Speed;
				In.Throttle = FMath::Clamp(bCar ? ((Target - S) / Dt + S * .03 + HKV::Sign(S) * .2) / FMath::Max(1.0, 4.2 - FMath::Abs(S) * .08)
					: ((Target - S) / Dt + S * .02 + (S > 0 ? .25 : 0)) / FMath::Max(1.2, 7.5 - S * .12), 0.0, 1.0);
			}
			const double PrevAirT = V->AirT, YawBefore = V->Yaw, SpeedBefore = V->Speed;
			V->Update(Dt, In);
			if (V->Speed == 0) R.Blocked++;
			if (bOverCrest)
			{
				R.MaxYawChange = FMath::Max(R.MaxYawChange, FMath::Abs(V->Yaw - YawBefore));
				R.MaxSpeedErr = FMath::Max(R.MaxSpeedErr, FMath::Abs(V->Speed - SpeedBefore * (1 - (bCar ? .03 : .02) * Dt)));
			}
			if (V->AirT > 0 && PrevAirT == 0) { Start = FVector2D(V->X, V->Z); StartAt = Along(); R.MaxH = R.MaxG = 0; Ys.Reset(); }
			if (V->bAirborne && StartAt > -5) CrestAirT += Dt;
			if (V->AirT > 0 && !R.bFlew)
			{
				// Above the ground under the middle, and above the ground the wheels follow (front and back averaged).
				const FVector2D G = V->Forward();
				const double L = V->Length * .4;
				R.MaxH = FMath::Max(R.MaxH, V->Y - Data.GroundWorld(V->X, V->Z));
				R.MaxG = FMath::Max(R.MaxG, V->Y - (Data.GroundWorld(V->X + G.X * L, V->Z + G.Y * L) + Data.GroundWorld(V->X - G.X * L, V->Z - G.Y * L)) / 2);
				Ys.Add(V->Y);
				if (Ys.Num() >= 3)
				{
					const double A = (Ys.Last() - 2 * Ys[Ys.Num() - 2] + Ys[Ys.Num() - 3]) / (Dt * Dt);
					R.MinAcc = FMath::Min(R.MinAcc, A); R.MaxAcc = FMath::Max(R.MaxAcc, A); R.AccN++;
				}
			}
			else Ys.Reset();
			if (R.bFlew)
			{
				R.bThud = Rec.Calls.Contains(TEXT("thud"));
				const FHKSeatPose S = V->Seat();
				R.bSeatDip = V->Squash > 0 && FMath::Abs(S.Y - (bCar ? V->Y + 1.24 : V->Y + 1.58 * std::cos(static_cast<FHKMotorcycle*>(V.Get())->Lean)) + V->Squash) < 1e-9;
			}
		}
		R.AirborneT = CrestAirT;
		AddInfo(FString::Printf(TEXT("%s at %.0f km/h: %s; off the ground %.3f s (flagged airborne %.3f s), %.2f m from %.2f m to %.2f m past the crest, up to %.3f m above the ground (%.3f m above the wheels' ground), impact %.2f m/s, %d landings in all; vertical acceleration %.4f..%.4f m/s² over %d steps"),
			*Kind, Kmh, R.bFlew ? TEXT("flew over the crest") : TEXT("stayed on the ground at the crest"), R.OffT, R.AirborneT, R.Dist, R.TakeoffAt, R.LandAt, R.MaxH, R.MaxG, R.Impact, R.Landings, R.AccN ? R.MinAcc : 0, R.AccN ? R.MaxAcc : 0, R.AccN));
		TestEqual(Kind + TEXT(": never blocked"), R.Blocked, 0);
		return R;
	};
	// The web code itself with these fixed 1/60 s steps: car 90 km/h 0.600 s / 14.90 m / 0.177 m, motorcycle 110 km/h
	// 0.733 s / 22.28 m / 0.764 m (the recorded scenarios check the port against it exactly). The numbers in the web
	// PR (car 0.57 s / 14 m / 0.22 m, motorcycle 0.68 s / 21 m / 0.85 m) came from a run with the speed held fixed all the
	// way; the flight depends on the small hops just before the crest, so they are checked here as "about".
	Rec.Calls.Reset();
	const FFlight Car50 = Drive(TEXT("car"), 50);
	TestFalse(TEXT("car 50 km/h stays on the ground at the crest"), Car50.bFlew);
	TestTrue(TEXT("car 50 km/h: nothing longer than the 0.06 s hop filter at the crest"), Car50.AirborneT == 0);
	Rec.Calls.Reset();
	const FFlight Car90 = Drive(TEXT("car"), 90);
	TestTrue(TEXT("car 90 km/h flies over the crest"), Car90.bFlew);
	TestTrue(TEXT("car 90 km/h flies about 0.57 s"), FMath::Abs(Car90.OffT - .57) < .06);
	TestTrue(TEXT("car 90 km/h flies about 14 m"), FMath::Abs(Car90.Dist - 14) < 1.5);
	TestTrue(TEXT("car 90 km/h up to about 0.22 m"), FMath::Abs(Car90.MaxH - .22) < .06);
	TestTrue(TEXT("car: no yaw from steering in the air"), Car90.MaxYawChange < 1e-12);
	TestTrue(TEXT("car: in the air only air drag changes the speed"), Car90.MaxSpeedErr < 1e-9);
	TestTrue(TEXT("car: landing thud"), Car90.bThud);
	TestTrue(TEXT("car: landing dips the seat"), Car90.bSeatDip);
	Rec.Calls.Reset();
	const FFlight Bike = Drive(TEXT("motorcycle"), 110);
	TestTrue(TEXT("motorcycle 110 km/h flies over the crest"), Bike.bFlew);
	TestTrue(TEXT("motorcycle 110 km/h flies about 0.68 s"), FMath::Abs(Bike.OffT - .68) < .1);
	TestTrue(TEXT("motorcycle 110 km/h flies about 21 m"), FMath::Abs(Bike.Dist - 21) < 2.5);
	TestTrue(TEXT("motorcycle 110 km/h up to about 0.85 m"), FMath::Abs(Bike.MaxH - .85) < .12);
	TestTrue(TEXT("motorcycle: no yaw in the air"), Bike.MaxYawChange < 1e-12);
	TestTrue(TEXT("motorcycle: in the air only air drag changes the speed"), Bike.MaxSpeedErr < 1e-9);
	TestTrue(TEXT("motorcycle: landing thud"), Bike.bThud);
	TestTrue(TEXT("motorcycle: landing dips the seat"), Bike.bSeatDip);
	for (const FFlight* Fl : {&Car90, &Bike})
		TestTrue(TEXT("vertical acceleration in the air is -9.81 m/s²"), Fl->AccN > 10 && FMath::Abs(Fl->MinAcc + 9.81) < 1e-4 && FMath::Abs(Fl->MaxAcc + 9.81) < 1e-4);
	// The excavator still hugs the ground; a network pose ends a flight.
	{
		FHKExcavator Ex(Crest.X, Crest.Y, Yaw);
		TestFalse(TEXT("the excavator does not fly"), Ex.bFlies);
		FHKCar Car(Crest.X, Crest.Y, Yaw, 4);
		TestTrue(TEXT("cars fly"), Car.bFlies);
		Car.Vy = 3; Car.AirT = .4; Car.bAirborne = true;
		Car.NetSet(Car.NetGet());
		TestTrue(TEXT("netSet resets the vertical motion"), Car.Vy == 0 && Car.AirT == 0 && !Car.bAirborne && !Car.bHasGroundPrev);
	}
	FHKAudio::SetInstance(nullptr);
	return true;
}

/* The vehicle list: same 41 vehicles in the same order as the web (multiplayer numbering). */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKVehListTest, "Haarklous.Vehicles.List", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKVehListTest::RunTest(const FString& Parameters)
{
	FHKData& Data = FHKData::Get();
	if (!TestTrue(TEXT("data loaded"), Data.bLoaded)) return false;
	const TSharedPtr<FJsonObject> Ref = LoadVehiclesRef(*this);
	if (!Ref) return false;
	TArray<TUniquePtr<FHKVehicle>> List;
	const double T0 = FPlatformTime::Seconds();
	HKVehicles::CreateAll(List);
	AddInfo(FString::Printf(TEXT("CreateAll: %d vehicles in %.0f ms"), List.Num(), (FPlatformTime::Seconds() - T0) * 1000));
	int32 Bad = 0;
	double MaxPos = 0, MaxSpawn = 0;
	auto Check = [&](bool bOk, const FString& What) { if (!bOk) { if (Bad < 12) AddError(What); Bad++; } };
	// Against the recording (kind, name, position, yaw, paint colour, spawn) ...
	const TArray<TSharedPtr<FJsonValue>>& RL = HKJson::Arr(Ref, TEXT("list"));
	Check(RL.Num() == List.Num(), FString::Printf(TEXT("%d vehicles, web %d"), List.Num(), RL.Num()));
	int32 Cars = 0, Bikes = 0;
	for (int32 i = 0; i < FMath::Min(RL.Num(), List.Num()); i++)
	{
		const TSharedPtr<FJsonObject> R = RL[i]->AsObject();
		const FHKVehicle& V = *List[i];
		Check(V.Kind == HKJson::Str(R, TEXT("kind")) && V.Name == HKJson::Str(R, TEXT("name")), FString::Printf(TEXT("%d: %s '%s', web %s '%s'"), i, *V.Kind, *V.Name, *HKJson::Str(R, TEXT("kind")), *HKJson::Str(R, TEXT("name"))));
		const double Dp = FMath::Max(FMath::Max(FMath::Abs(V.X - HKJson::Num(R, TEXT("x"))), FMath::Abs(V.Z - HKJson::Num(R, TEXT("z")))), FMath::Max(FMath::Abs(V.Y - HKJson::Num(R, TEXT("y"))), FMath::Abs(V.Yaw - HKJson::Num(R, TEXT("yaw")))));
		MaxPos = FMath::Max(MaxPos, Dp);
		Check(Dp < 1e-9, FString::Printf(TEXT("%d: (%.9f, %.9f, %.9f) yaw %.9f differs from the web by %.2e"), i, V.X, V.Y, V.Z, V.Yaw, Dp));
		double Paint = -1;
		if (R->TryGetNumberField(TEXT("paint"), Paint))
		{
			uint32 Mine = 0;
			if (V.Kind == TEXT("car")) { Mine = HKVehicles::CarColor(static_cast<const FHKCar&>(V).Model); Cars++; }
			else if (V.Kind == TEXT("motorcycle")) { Mine = static_cast<const FHKMotorcycle&>(V).ColorHex; Bikes++; }
			Check(Mine == (uint32)Paint && V.bHasPaint && V.PaintColor.Equals(HKVehicles::ColorFromHex(Mine)), FString::Printf(TEXT("%d: paint %06x, web %06x"), i, Mine, (uint32)Paint));
		}
		const TArray<double> Spawn = HKJson::Nums(R, TEXT("spawn"));
		Check(Spawn.Num() == V.Spawn.Num(), FString::Printf(TEXT("%d: spawn has %d values, web %d"), i, V.Spawn.Num(), Spawn.Num()));
		for (int32 k = 0; k < FMath::Min(Spawn.Num(), V.Spawn.Num()); k++)
		{
			MaxSpawn = FMath::Max(MaxSpawn, FMath::Abs(Spawn[k] - V.Spawn[k]));
			Check(FMath::Abs(Spawn[k] - V.Spawn[k]) < 1e-9, FString::Printf(TEXT("%d: spawn[%d] %.4f, web %.4f"), i, k, V.Spawn[k], Spawn[k]));
		}
	}
	// ... and against game.json "vehicles" (the data the rest of the port uses).
	const TArray<TSharedPtr<FJsonValue>>& GL = HKJson::Arr(Data.Game, TEXT("vehicles"));
	Check(GL.Num() == List.Num(), FString::Printf(TEXT("game.json has %d vehicles, CreateAll %d"), GL.Num(), List.Num()));
	for (int32 i = 0; i < FMath::Min(GL.Num(), List.Num()); i++)
	{
		const TSharedPtr<FJsonObject> R = GL[i]->AsObject();
		const FHKVehicle& V = *List[i];
		Check(V.Kind == HKJson::Str(R, TEXT("kind")) && V.Name == HKJson::Str(R, TEXT("name")), FString::Printf(TEXT("game.json %d: %s, web %s"), i, *V.Name, *HKJson::Str(R, TEXT("name"))));
		Check(FMath::Abs(V.X - HKJson::Num(R, TEXT("x"))) < 1e-9 && FMath::Abs(V.Z - HKJson::Num(R, TEXT("z"))) < 1e-9 && FMath::Abs(V.Yaw - HKJson::Num(R, TEXT("yaw"))) < 1e-9, FString::Printf(TEXT("game.json %d: position or yaw differs"), i));
		Check(FMath::Abs(V.Length - HKJson::Num(R, TEXT("length"))) < 1e-12 && FMath::Abs(V.Width - HKJson::Num(R, TEXT("width"))) < 1e-12 && FMath::Abs(V.Height - HKJson::Num(R, TEXT("height"))) < 1e-12 && FMath::Abs(V.Radius - HKJson::Num(R, TEXT("radius"))) < 1e-12, FString::Printf(TEXT("game.json %d: size differs"), i));
		const TArray<double> Spawn = HKJson::Nums(R, TEXT("spawn"));
		bool bSame = Spawn.Num() == V.Spawn.Num();
		for (int32 k = 0; bSame && k < Spawn.Num(); k++) bSame = FMath::Abs(Spawn[k] - V.Spawn[k]) < 1e-9;
		Check(bSame, FString::Printf(TEXT("game.json %d: spawn differs"), i));
		// footprint of the first rectangle
		const TSharedPtr<FJsonObject> Fo = HKJson::Obj(R, TEXT("footprint"));
		const TArray<FHKObstacle> Mine = V.Footprint();
		const TArray<FHKPt> Pts = HKJson::Pts(HKJson::Arr(Fo, TEXT("points")));
		bool bFoot = Mine.Num() > 0 && Mine[0].Points.Num() == Pts.Num() && FMath::Abs(Mine[0].MinY - HKJson::Num(Fo, TEXT("minY"))) < 1e-9 && FMath::Abs(Mine[0].MaxY - HKJson::Num(Fo, TEXT("maxY"))) < 1e-9;
		for (int32 k = 0; bFoot && k < Pts.Num(); k++) bFoot = FMath::Abs(Mine[0].Points[k].X - Pts[k].X) < 1e-9 && FMath::Abs(Mine[0].Points[k].Z - Pts[k].Z) < 1e-9;
		Check(bFoot, FString::Printf(TEXT("game.json %d: footprint differs"), i));
	}
	int32 Kinds[5] = {0, 0, 0, 0, 0};
	for (const TUniquePtr<FHKVehicle>& V : List)
		Kinds[V->Kind == TEXT("car") ? 0 : V->Kind == TEXT("motorcycle") ? 1 : V->Kind == TEXT("excavator") ? 2 : V->Kind == TEXT("helicopter") ? 3 : 4]++;
	AddInfo(FString::Printf(TEXT("%d cars, %d motorcycles, %d excavator, %d helicopter, %d plane; largest position difference %.2e, spawn %.2e; %d mismatches"), Kinds[0], Kinds[1], Kinds[2], Kinds[3], Kinds[4], MaxPos, MaxSpawn, Bad));
	return Bad == 0;
}

/* HUD number formatting (JavaScript toFixed / Math.round) and the drone's floor finder. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKVehFormatTest, "Haarklous.Vehicles.Format", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKVehFormatTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("toFixed(2.5, 0)"), HKV::ToFixed(2.5, 0), FString(TEXT("3")));
	TestEqual(TEXT("toFixed(0.25, 1)"), HKV::ToFixed(.25, 1), FString(TEXT("0.3")));
	TestEqual(TEXT("toFixed(-0.04, 1)"), HKV::ToFixed(-.04, 1), FString(TEXT("-0.0")));
	TestEqual(TEXT("toFixed(-0, 1)"), HKV::ToFixed(-0.0, 1), FString(TEXT("0.0")));
	TestEqual(TEXT("toFixed(1.005, 2)"), HKV::ToFixed(1.005, 2), FString(TEXT("1.00")));
	TestEqual(TEXT("toFixed(-2.5, 0)"), HKV::ToFixed(-2.5, 0), FString(TEXT("-3")));
	TestEqual(TEXT("Math.round(-2.5)"), HKV::JsRound(-2.5), -2.0);
	TestEqual(TEXT("Math.round(2.5)"), HKV::JsRound(2.5), 3.0);
	TestEqual(TEXT("Math.round(0.49999999999999994)"), HKV::JsRound(0.49999999999999994), 0.0);
	TestEqual(TEXT("Math.hypot(3, 4)"), HKV::Hypot(3, 4), 5.0);
	return true;
}

/*
 * Flying by mouse (web PR #19, not in the recording yet): with hold the drone's angle mode moves the tilt by
 * stick × 1.6 rad/s up to ±0.75 rad and keeps it, and the helicopter levels itself with gain 0.15 instead of 1.2.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKVehMouseHoldTest, "Haarklous.Vehicles.MouseHold", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKVehMouseHoldTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("data loaded"), FHKData::Get().bLoaded)) return false;
	const double Dt = 1.0 / 60;
	FHKVehicleInput In;
	In.bEmpty = false;
	// Drone, high above everything (no floor, nothing solid).
	FHKDrone D;
	D.Launch(0, 300, 0, 0);
	FHKDroneEnv Env;
	In.bHold = true; In.RY = -1;
	for (int32 i = 0; i < 15; i++) D.Update(Dt, In, Env);
	TestTrue(FString::Printf(TEXT("drone: 0.25 s of full stick with hold tilts it -0.4 rad (%.4f)"), D.PitchA), FMath::Abs(D.PitchA + .4) < 1e-9);
	for (int32 i = 0; i < 30; i++) D.Update(Dt, In, Env);
	TestTrue(FString::Printf(TEXT("drone: the tilt stops at -0.75 rad (%.4f)"), D.PitchA), FMath::Abs(D.PitchA + .75) < 1e-9);
	In.RY = 0; In.RX = .5;
	for (int32 i = 0; i < 45; i++) D.Update(Dt, In, Env);
	TestTrue(FString::Printf(TEXT("drone: with hold and the stick centred the pitch stays (%.4f), roll 0.5 × 1.6 rad/s × 0.75 s = 0.6 (%.4f)"), D.PitchA, D.RollA), FMath::Abs(D.PitchA + .75) < 1e-9 && FMath::Abs(D.RollA - .6) < 1e-9);
	In.bHold = false; In.RX = 0;
	for (int32 i = 0; i < 60; i++) D.Update(Dt, In, Env);
	TestTrue(FString::Printf(TEXT("drone: without hold it levels again (%.4f, %.4f)"), D.PitchA, D.RollA), FMath::Abs(D.PitchA) < .01 && FMath::Abs(D.RollA) < .01);
	// Helicopter 80 m up, rotor at speed, nose 0.3 rad down, sticks centred for 1 s.
	auto Heli = [&](bool bHold)
	{
		FHKHelicopter H(0, 0, 0);
		H.bActive = true; H.Rpm = 1; H.bGrounded = false;
		H.Pos.Y = FHKData::Get().GroundWorld(0, 0) + 80;
		H.Q = HKV::Euler(-.3, 0, 0, TEXT("YXZ"));
		FHKVehicleInput Hi;
		Hi.bEmpty = false; Hi.bHold = bHold;
		for (int32 i = 0; i < 60; i++) H.Update(Dt, Hi);
		double P, B;
		H.Attitude(P, B);
		return P;
	};
	const double PHold = Heli(true), PFree = Heli(false);
	AddInfo(FString::Printf(TEXT("helicopter pitch after 1 s from -0.3 rad: %.3f with hold, %.3f without"), PHold, PFree));
	TestTrue(TEXT("helicopter: with hold the tilt stays (still below -0.2 rad after 1 s)"), PHold < -.2);
	TestTrue(TEXT("helicopter: without hold it levels much faster"), FMath::Abs(PFree) < FMath::Abs(PHold) * .6);
	return true;
}

#endif
