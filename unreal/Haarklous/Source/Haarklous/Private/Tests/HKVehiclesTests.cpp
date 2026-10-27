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
			return true;
		}
		int32 StepIndex = 0;

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

#endif
