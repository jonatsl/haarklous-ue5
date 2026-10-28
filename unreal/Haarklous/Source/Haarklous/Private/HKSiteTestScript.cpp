// Scripted play-through "site" (-hktest=site): the construction site (new feature, 28 Oct 2026). Overview shots, then
// every machine driven with the simulated controller the way an operator would: the excavator digs in the pit and loads
// the hauler, the hauler drives off and tips, the dozer pushes into a stockpile, the wheel loader scoops from one, the
// tipper truck drives out through the gate and backs in again, the mobile crane sets its outriggers and raises its boom, the tower crane lifts a pallet of
// bricks and sets it down. Checks that soil is conserved (the site's soil plus what buckets and bodies carry) and
// measures the frame rate at the site. Screenshots go to the -hkout folder; results are HK-TEST lines in the log.
#include "HKTestDriver.h"
#include "Haarklous.h"
#include "HKData.h"
#include "HKGame.h"
#include "HKInput.h"
#include "HKMath.h"
#include "HKNode.h"
#include "HKSite.h"
#include "HKSiteVehicles.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace HKSiteTest
{
	FHKPadState& Pad()
	{
		static FHKPadState None;
		TSharedPtr<FHKInputProcessor> In = FHKInputProcessor::Get();
		return In ? In->SimulatedPad : None;
	}
	void Release() { FHKPadState& P = Pad(); for (float& A : P.Axes) A = 0; for (float& B : P.Buttons) B = 0; }
	double Wrap(double A) { return FMath::UnwindRadians(A); }
	double Bearing(double X0, double Z0, double X1, double Z1) { return FMath::Atan2(-(X1 - X0), -(Z1 - Z0)); }
	double Gnd(const FVector& P) { return FHKData::Get().GroundWorld(P.X, P.Z); }
	// The raw stick position that gives V after the controller's dead zone (HKInput rescales outside 0.14).
	float Raw(double V) { return V == 0 ? 0.f : (float)(FMath::Sign(V) * FMath::Min(1.0, HKPad::DeadZoneValue + (1 - HKPad::DeadZoneValue) * FMath::Abs(V))); }
	// A stick value toward a target: proportional, at least Min outside the tolerance.
	float Toward(double Err, double Gain, double Tol, double Min = .12)
	{
		if (FMath::Abs(Err) < Tol) return 0;
		const double V = FMath::Clamp(Err * Gain, -1.0, 1.0);
		return Raw(FMath::Sign(V) * FMath::Max(FMath::Abs(V), Min));
	}
	// The excavator bucket's pitch in the world (boom + stick + bucket, the machine standing level) and where its
	// opening points: up at -3.57 rad, toward the machine with the teeth down at about -1.6.
	double BucketPitch(const FHKSiteExcavator* X) { return X->Boom + X->StickA + X->BucketA; }
	double MouthUp(const FHKSiteExcavator* X) { const double T = BucketPitch(X); return -.91 * FMath::Cos(T) + .42 * FMath::Sin(T); }

	template <class TM> TM* Find(AHKTestDriver& T, const TCHAR* Kind)
	{
		for (FHKVehicle* V : T.Game()->VehicleList) if (V->Kind == Kind) return static_cast<TM*>(V);
		return nullptr;
	}

	// Soil in the game: the site's soil above rock plus what buckets and bodies carry (a dozer only pushes).
	double SoilTotal(AHKTestDriver& T)
	{
		double S = T.Game()->Site->Volume();
		for (FHKSiteMachine* M : FHKSiteMachine::All()) if (M->Kind != TEXT("dozer")) S += M->Payload;
		return S;
	}

	// The camera for the shots: looking at a moving target from a direction (web yaw), distance and height.
	struct FView { TFunction<FVector()> Target; double Yaw = 0, Dist = 14, Up = 5; };
	void Aim(AHKTestDriver& T, const FView& V)
	{
		if (!V.Target) return;
		AHKGame* G = T.Game();
		const FVector C = V.Target();
		FVector P(C.X + FMath::Sin(V.Yaw) * V.Dist, C.Y + V.Up, C.Z + FMath::Cos(V.Yaw) * V.Dist);
		P.Y = FMath::Max(P.Y, Gnd(P) + 1.6);
		G->bCameraOverride = true;
		G->OverridePos = P;
		G->OverrideQuat = HK::EulerToQuat(-FMath::Atan2(P.Y - C.Y, V.Dist), V.Yaw, 0, TEXT("YXZ"));
	}

	// An operator's action: Control sets the controller each frame and returns true when done (or Max seconds pass).
	void Act(AHKTestDriver& T, TSharedPtr<FView> V, const FString& What, double Max, TFunction<bool(double)> Control)
	{
		TSharedPtr<double> Time = MakeShared<double>(0);
		T.Steps.Add([&T, V, What, Max, Control, Time](double Dt)
		{
			*Time += Dt;
			Aim(T, *V);
			const bool bDone = Control(Dt);
			if (!bDone && *Time < Max) return false;
			Release();
			if (!What.IsEmpty()) UE_LOG(LogHaarklous, Display, TEXT("HK-TEST %s: %s after %.1f s"), *What, bDone ? TEXT("done") : TEXT("time up"), *Time);
			return true;
		});
	}
	// Hold the controller as Set leaves it for Seconds.
	void For(AHKTestDriver& T, TSharedPtr<FView> V, double Seconds, TFunction<void()> Set)
	{
		Act(T, V, FString(), Seconds, [Set](double) { Set(); return false; });
	}
	// A screenshot from the watching camera.
	void Snap(AHKTestDriver& T, TSharedPtr<FView> V, const FString& Name)
	{
		T.Steps.Add([&T, V](double) { Aim(T, *V); return true; });
		T.Shot(Name);
	}

	// Walk up to a machine and climb in with □ (like the vehicles play-through).
	void Board(AHKTestDriver& T, const TCHAR* Kind)
	{
		const FString K = Kind;
		T.Do([&T, K]()
		{
			// Stand beside the cab on whichever side the ground is level with the machine (not in the pit or on a heap).
			AHKGame* G = T.Game();
			for (FHKVehicle* V : G->VehicleList)
				if (V->Kind == K)
				{
					const FVector2D F = V->Forward(), R(-F.Y, F.X);
					const double D = V->Width / 2 + .9;
					double Best = 1e9; FVector2D At = FVector2D::ZeroVector;
					for (const double S : {1.0, -1.0})
					{
						const FVector2D P(V->X + R.X * D * S, V->Z + R.Y * D * S);
						const double Diff = FMath::Abs(FHKData::Get().GroundWorld(P.X, P.Y) - V->Y);
						if (Diff < Best) { Best = Diff; At = P; }
					}
					G->Place(At.X, At.Y, Bearing(At.X, At.Y, V->X, V->Z));
					break;
				}
		});
		T.Wait(.6);
		T.Log([&T]() { return TEXT("prompt: ") + T.Game()->LastPromptText(); });
		T.Press(2);
		T.Wait(.8);
		T.Expect(K + TEXT(": in the driver's seat"), [&T, K]() { const FHKVehicle* V = T.Game()->Player.Vehicle; return T.Game()->Player.Mode == TEXT("vehicle") && V && V->Kind == K; });
		T.Log([&T]() { return TEXT("HUD: ") + T.Game()->SpeedoText; });
	}
	// A screenshot from the operator's own camera (the cab, or the view chosen with R3).
	void OwnShot(AHKTestDriver& T, const FString& Name)
	{
		T.Do([&T]() { T.Game()->bCameraOverride = false; });
		T.Wait(.4);
		T.Shot(Name);
	}
	// R3 changes the view; expects the given view (0 cab, 1 outside, 2 above the hook).
	void ViewTo(AHKTestDriver& T, int32 Want, const FString& Name)
	{
		T.Press(11);
		T.Wait(.3);
		T.Expect(FString::Printf(TEXT("R3: view %d (%s)"), Want, *Name), [&T, Want]() { return T.Game()->Player.SiteView == Want; });
		OwnShot(T, Name);
	}
	void GetOut(AHKTestDriver& T, const FString& Kind)
	{
		T.Do([]() { Release(); });
		T.Wait(.4);
		T.Press(2);
		T.Wait(.6);
		T.Expect(Kind + TEXT(": back on foot"), [&T]() { return T.Game()->Player.Mode == TEXT("foot"); });
	}

	struct FRegister
	{
		FRegister()
		{
			// Quick look from every cab (-hktest=sitecabs): board each machine, put its tool where the operator works with it,
			// a screenshot from the seat; for checking cab views without the whole play-through.
			FHKTestScripts::All().Add(TEXT("sitecabs"), [](AHKTestDriver& T)
			{
				TSharedPtr<FView> V = MakeShared<FView>();
				T.Wait(3);
				T.Press(0);
				T.Wait(1.5);
				T.WaitShaders();
				for (const TCHAR* K : {TEXT("dozer"), TEXT("loader"), TEXT("excavator30"), TEXT("hauler"), TEXT("tipper"), TEXT("crane"), TEXT("tower")})
				{
					const FString Kind = K;
					Board(T, K);
					if (Kind == TEXT("loader"))
						Act(T, V, TEXT("loader: bucket down and flat"), 6, [&T](double)
						{
							FHKWheelLoader* L = Find<FHKWheelLoader>(T, TEXT("loader"));
							Pad().Axes[3] = L->LiftA > -.07 ? -1.f : 0;
							Pad().Axes[2] = Toward(L->TiltA - (-.05), 2, .04);
							return Pad().Axes[3] == 0 && Pad().Axes[2] == 0;
						});
					if (Kind == TEXT("dozer")) Act(T, V, TEXT("dozer: blade down"), 3, [&T](double) { Pad().Axes[3] = -.8f; return Find<FHKDozer>(T, TEXT("dozer"))->Lift <= -.05; });
					OwnShot(T, TEXT("cab-") + Kind);
					GetOut(T, Kind);
				}
			});

			FHKTestScripts::All().Add(TEXT("site"), [](AHKTestDriver& T)
			{
				TSharedPtr<FView> V = MakeShared<FView>();
				TSharedPtr<double> Soil0 = MakeShared<double>(0);
				T.Wait(3);
				T.Press(0);
				T.Wait(1.5);
				T.Expect(TEXT("site: loaded"), [&T]() { return T.Game()->Site.IsValid() && FHKSiteMachine::All().Num() == 7; });
				// Overview from three sides, high and low.
				T.Do([&T, V]()
				{
					const FHKDirtField& S = *T.Game()->Site;
					const FVector2D C = S.NodeWorld(S.NX / 2, S.NS / 2);
					V->Target = [C]() { return FVector(C.X, FHKData::Get().GroundWorld(C.X, C.Y), C.Y); };
					V->Yaw = .9; V->Dist = 52; V->Up = 34;
				});
				Snap(T, V, TEXT("01-overview"));
				T.Do([V]() { V->Yaw = -2.2; V->Dist = 40; V->Up = 16; });
				Snap(T, V, TEXT("02-overview-north"));
				T.Do([V]() { V->Yaw = 2.6; V->Dist = 30; V->Up = 3; });
				Snap(T, V, TEXT("03-ground-level"));
				T.Do([&T, Soil0]() { *Soil0 = SoilTotal(T); });
				T.Log([&T]() { return FString::Printf(TEXT("site soil above rock %.1f m3"), T.Game()->Site->Volume()); });
				auto Conserved = [&T, Soil0](const FString& After)
				{
					T.Log([&T, Soil0, After]() { return FString::Printf(TEXT("soil after %s: %.2f m3 (start %.2f, difference %.3f)"), *After, SoilTotal(T), *Soil0, SoilTotal(T) - *Soil0); });
					T.Expect(TEXT("soil conserved after ") + After, [&T, Soil0]() { return FMath::Abs(SoilTotal(T) - *Soil0) < .1; });
				};

				// ---- Excavator: three bucket cycles from the pit into the hauler ----
				auto Ex = [&T]() { return Find<FHKSiteExcavator>(T, TEXT("excavator30")); };
				auto Hauler = [&T]() { return Find<FHKDumpTruck>(T, TEXT("hauler")); };
				Board(T, TEXT("excavator30"));
				T.Do([V, Ex]()
				{
					FHKSiteExcavator* X = Ex();
					V->Target = [Ex]() { const FHKSiteExcavator* X = Ex(); return FVector(X->X, X->Y + 2, X->Z); };
					V->Yaw = X->Yaw - 2.3; V->Dist = 17; V->Up = 7;
				});
				Snap(T, V, TEXT("04-excavator"));
				auto ExLog = [&T, Ex, Hauler](const FString& What)
				{
					T.Log([Ex, Hauler, What]()
					{
						const FHKSiteExcavator* X = Ex();
						const FVector P = X->Tip->GetWorldPosition();
						return FString::Printf(TEXT("excavator %s: swing %.2f boom %.2f stick %.2f bucket %.2f (opening up %.2f), teeth %.2f m from the ground, bucket %.2f m3, hauler %.2f m3"),
							*What, X->Swing, X->Boom, X->StickA, X->BucketA, MouthUp(X), P.Y - Gnd(P), X->Payload, Hauler()->Payload);
					});
				};
				ExLog(TEXT("start"));
				for (int32 Cycle = 0; Cycle < 3; Cycle++)
				{
					const double DigSwing = (Cycle - 1) * .12;   // dig a little to the left, ahead, to the right
					// Reach out and put the teeth into the ground in front of the machine.
					Act(T, V, TEXT("excavator: bucket into the ground"), 9, [Ex, DigSwing](double)
					{
						FHKSiteExcavator* X = Ex();
						const FVector P = X->Tip->GetWorldPosition();
						FHKPadState& S = Pad();
						S.Axes[0] = Toward(-Wrap(DigSwing - X->Swing), 3, .03);
						S.Axes[1] = X->StickA < -1.0 ? -.8f : 0;
						S.Axes[2] = Toward(-1.4 - BucketPitch(X), 2, .06);
						S.Axes[3] = S.Axes[0] == 0 ? -.8f : 0;
						return S.Axes[0] == 0 && P.Y < Gnd(P) - .3;
					});
					ExLog(TEXT("teeth in"));
					if (Cycle == 0) Snap(T, V, TEXT("05-excavator-digging"));
					// Dig: pull the stick in and curl the bucket, keeping the teeth 0.2 to 0.7 m deep.
					Act(T, V, TEXT("excavator: dig"), 9, [Ex](double)
					{
						FHKSiteExcavator* X = Ex();
						const FVector P = X->Tip->GetWorldPosition();
						const double Depth = Gnd(P) - P.Y;
						FHKPadState& S = Pad();
						S.Axes[1] = .8f;
						S.Axes[2] = X->StickA < -1.7 ? -.8f : -.3f;
						S.Axes[3] = Depth > .7 ? .5f : Depth < .2 ? -.5f : 0;
						return X->Payload >= X->Capacity * .95 || X->StickA <= -2.4;
					});
					ExLog(TEXT("bucket full"));
					// Curl the bucket fully and lift clear of the hauler's sides, swinging over its body.
					Act(T, V, TEXT("excavator: lift and swing to the hauler"), 16, [Ex, Hauler](double)
					{
						FHKSiteExcavator* X = Ex();
						FHKDumpTruck* H = Hauler();
						H->Rig.Root->UpdateMatrixWorld();
						const FVector B = H->Bed->LocalToWorld(FVector(0, 1, -2.5));
						const FVector P = X->Tip->GetWorldPosition();
						const double Want = Wrap(Bearing(X->X, X->Z, B.X, B.Z) - X->Yaw);
						const double Reach = FVector2D(P.X - X->X, P.Z - X->Z).Size(), Need = FVector2D(B.X - X->X, B.Z - X->Z).Size();
						const bool bHigh = P.Y > B.Y + 1.6;
						FHKPadState& S = Pad();
						S.Axes[2] = Toward(-3.57 - BucketPitch(X), 2, .08);   // keep the opening up
						S.Axes[3] = !bHigh ? 1.f : P.Y > B.Y + 3.2 ? -.4f : 0;
						S.Axes[0] = bHigh || P.Y > Gnd(P) + 1.0 ? Toward(-Wrap(Want - X->Swing), 2, .04) : 0;
						S.Axes[1] = bHigh ? Toward(Reach - Need, 1, .4) : 0;
						return bHigh && S.Axes[0] == 0 && S.Axes[1] == 0;
					});
					ExLog(TEXT("over the hauler"));
					if (Cycle == 0) Snap(T, V, TEXT("06-excavator-swing"));
					// Open the bucket over the body.
					Act(T, V, TEXT("excavator: empty the bucket"), 6, [Ex](double)
					{
						Pad().Axes[2] = 1;
						return Ex()->Payload < .01;
					});
					if (Cycle == 0) Snap(T, V, TEXT("07-excavator-dumped"));
					ExLog(TEXT("bucket empty"));
				}
				T.Expect(TEXT("excavator: the hauler got soil"), [Hauler]() { return Hauler()->Payload > 2; });
				Conserved(TEXT("digging"));
				// Swing back over the pit and park the bucket on the ground.
				Act(T, V, TEXT("excavator: back to rest"), 12, [Ex](double)
				{
					FHKSiteExcavator* X = Ex();
					const FVector P = X->Tip->GetWorldPosition();
					FHKPadState& S = Pad();
					S.Axes[0] = Toward(-Wrap(0 - X->Swing), 2, .05);
					S.Axes[2] = Toward(-2.2 - BucketPitch(X), 2, .08);
					S.Axes[3] = S.Axes[0] == 0 && P.Y > Gnd(P) + .3 ? -.7f : 0;
					return S.Axes[0] == 0 && P.Y <= Gnd(P) + .3;
				});
				T.Do([&T]() { T.Game()->bCameraOverride = false; });
				T.Wait(.6);
				T.Shot(TEXT("08-excavator-cab"));
				ViewTo(T, 1, TEXT("08b-excavator-outside"));
				ViewTo(T, 0, TEXT("08c-excavator-cab-again"));
				GetOut(T, TEXT("excavator"));

				// ---- Hauler: drive the load away and tip it ----
				Board(T, TEXT("hauler"));
				OwnShot(T, TEXT("09a-hauler-cab"));
				T.Do([V, Hauler]()
				{
					V->Target = [Hauler]() { const FHKDumpTruck* H = Hauler(); return FVector(H->X, H->Y + 1.5, H->Z); };
					V->Yaw = Hauler()->Yaw + 2.4; V->Dist = 18; V->Up = 6;
				});
				TSharedPtr<FVector> HStart = MakeShared<FVector>();
				T.Do([HStart, Hauler]() { *HStart = FVector(Hauler()->X, 0, Hauler()->Z); });
				For(T, V, 3, []() { Pad().Buttons[7] = 1; });
				T.Log([&T]() { return TEXT("hauler after 3 s full throttle: ") + T.Game()->SpeedoText; });
				Snap(T, V, TEXT("09-hauler-driving"));
				For(T, V, 1.5, []() { Pad().Buttons[7] = .6f; Pad().Axes[0] = .5f; });
				Act(T, V, TEXT("hauler: brake to a stop"), 8, [Hauler](double) { Pad().Buttons[6] = 1; return FMath::Abs(Hauler()->Speed) < .2; });
				T.Log([HStart, Hauler]() { const FHKDumpTruck* H = Hauler(); return FString::Printf(TEXT("hauler drove %.1f m, articulation %.2f rad"), FVector2D(H->X - HStart->X, H->Z - HStart->Z).Size(), H->Art.Phi); });
				T.Do([V, Hauler]() { V->Yaw = Hauler()->Yaw - 2.2; V->Dist = 16; V->Up = 4; });
				Act(T, V, TEXT("hauler: tip the body"), 14, [Hauler](double) { Pad().Axes[3] = 1; return Hauler()->Payload < .02 && Hauler()->Tip > 1.1; });
				Snap(T, V, TEXT("10-hauler-tipped"));
				T.Log([&T]() { return TEXT("hauler: ") + T.Game()->SpeedoText; });
				T.Expect(TEXT("hauler: empty after tipping"), [Hauler]() { return Hauler()->Payload < .05; });
				Act(T, V, TEXT("hauler: lower the body"), 8, [Hauler](double) { Pad().Axes[3] = -1; return Hauler()->Tip <= 0; });
				Conserved(TEXT("tipping"));
				GetOut(T, TEXT("hauler"));

				// ---- Dozer: blade down, push into the stockpile ----
				auto Dz = [&T]() { return Find<FHKDozer>(T, TEXT("dozer")); };
				Board(T, TEXT("dozer"));
				OwnShot(T, TEXT("11a-dozer-cab"));
				T.Do([V, Dz]()
				{
					V->Target = [Dz]() { const FHKDozer* D = Dz(); return FVector(D->X, D->Y + 1, D->Z); };
					V->Yaw = Dz()->Yaw + 1.9; V->Dist = 13; V->Up = 4;
				});
				TSharedPtr<double> Before = MakeShared<double>(0);
				T.Do([&T, Before]() { *Before = T.Game()->Site->Volume(); });
				Act(T, V, TEXT("dozer: blade down"), 3, [Dz](double) { Pad().Axes[3] = -.8f; return Dz()->Lift <= -.05; });
				For(T, V, 5, []() { Pad().Buttons[7] = 1; });
				T.Log([&T, Dz]() { return FString::Printf(TEXT("dozer pushing: %s, blade %.2f"), *T.Game()->SpeedoText, Dz()->Lift); });
				Snap(T, V, TEXT("11-dozer-pushing"));
				For(T, V, 3, []() { Pad().Buttons[7] = 1; });
				T.Log([&T, Dz]() { return FString::Printf(TEXT("dozer after 8 s: %s"), *T.Game()->SpeedoText); });
				T.Expect(TEXT("dozer: soil in front of the blade"), [Dz]() { return Dz()->Ahead > .5; });
				Act(T, V, TEXT("dozer: blade up and reverse"), 4, [Dz](double) { Pad().Axes[3] = 1; Pad().Buttons[6] = 1; return Dz()->Lift >= .3; });
				For(T, V, 2, []() { Pad().Buttons[6] = 1; });
				Snap(T, V, TEXT("12-dozer-cut"));
				Act(T, V, TEXT("dozer: stop"), 5, [Dz](double) { return FMath::Abs(Dz()->Speed) < .1; });
				Conserved(TEXT("dozing"));
				GetOut(T, TEXT("dozer"));

				// ---- Wheel loader: bucket flat on the ground, drive into the stockpile, curl and lift ----
				auto Ld = [&T]() { return Find<FHKWheelLoader>(T, TEXT("loader")); };
				Board(T, TEXT("loader"));
				OwnShot(T, TEXT("13a-loader-cab"));
				T.Do([V, Ld]()
				{
					V->Target = [Ld]() { const FHKWheelLoader* L = Ld(); return FVector(L->X, L->Y + 1.5, L->Z); };
					V->Yaw = Ld()->Yaw + 2.0; V->Dist = 15; V->Up = 4;
				});
				Act(T, V, TEXT("loader: bucket down"), 5, [Ld](double)
				{
					FHKWheelLoader* L = Ld();
					Pad().Axes[3] = L->LiftA > -.07 ? -1.f : 0;
					Pad().Axes[2] = Toward(L->TiltA - (-.05), 2, .04);
					return Pad().Axes[3] == 0 && Pad().Axes[2] == 0;
				});
				Act(T, V, TEXT("loader: into the pile"), 9, [Ld](double)
				{
					FHKWheelLoader* L = Ld();
					Pad().Buttons[7] = .7f;
					Pad().Axes[2] = L->Payload > 1 ? -.6f : 0;   // crowd the bucket back once it fills
					Pad().Axes[3] = L->Payload > 1.5 ? .4f : 0;
					return L->Payload >= L->Capacity * .9;
				});
				T.Log([Ld]() { return FString::Printf(TEXT("loader bucket %.2f m3 (of %.1f)"), Ld()->Payload, Ld()->Capacity); });
				Snap(T, V, TEXT("13-loader-scoop"));
				Act(T, V, TEXT("loader: roll back and lift"), 6, [Ld](double)
				{
					FHKWheelLoader* L = Ld();
					Pad().Axes[2] = L->TiltA < .7 ? -1.f : 0;
					Pad().Axes[3] = L->LiftA < .6 ? 1.f : 0;
					return L->TiltA >= .7 && L->LiftA >= .6;
				});
				For(T, V, 2.5, []() { Pad().Buttons[6] = 1; });
				Act(T, V, TEXT("loader: stop"), 5, [Ld](double) { return FMath::Abs(Ld()->Speed) < .1; });
				T.Expect(TEXT("loader: bucket filled"), [Ld]() { return Ld()->Payload > 1.5; });
				Snap(T, V, TEXT("14-loader-lifted"));
				Act(T, V, TEXT("loader: dump"), 6, [Ld](double) { Pad().Axes[2] = 1; return Ld()->Payload < .01; });
				Conserved(TEXT("loading"));
				GetOut(T, TEXT("loader"));

				// ---- Tipper truck: out through the gate onto the gravel track, then back in (reversing) ----
				// The tipper stands just inside the gate facing out. Steering toward a point on the gate's centre line;
				// L is the truck's position in the site frame (local x, s; the gate is in the fence at s = FenceS0).
				auto Tp = [&T]() { return Find<FHKDumpTruck>(T, TEXT("tipper")); };
				auto Loc = [&T, Tp]() { return FHKData::Get().ToLocal(Tp()->X, Tp()->Z); };
				auto GateAt = [&T](double Ds) { const FHKDirtField& S = *T.Game()->Site; return FHKData::Get().ToWorld(S.GateC, S.FenceS0 + Ds); };
				// Steer for a point: forward, the nose; reversing, the tail (the steering works the other way round).
				auto SteerFor = [Tp](const FVector2D& At, bool bReverse)
				{
					const FHKDumpTruck* D = Tp();
					const double Err = Wrap(Bearing(D->X, D->Z, At.X, At.Y) - D->Yaw - (bReverse ? PI : 0));
					return (float)FMath::Clamp((bReverse ? 2.5 : -2.5) * Err, -1.0, 1.0);
				};
				Board(T, TEXT("tipper"));
				T.Log([&T, Loc]() { const FHKDirtField& S = *T.Game()->Site; return FString::Printf(TEXT("tipper %.1f m inside the gate, %.1f m from its centre line (gate %.1f m wide)"), Loc().Y - S.FenceS0, Loc().X - S.GateC, S.GateW); });
				T.Expect(TEXT("site: there is a gate"), [&T]() { return T.Game()->Site->GateW > 8; });
				// The fence keeps vehicles and people out except at the gate.
				T.Expect(TEXT("site: the fence blocks a vehicle, the gate does not"), [&T]()
				{
					const FHKDirtField& S = *T.Game()->Site;
					FHKData& D = FHKData::Get();
					const FVector2D Fence = D.ToWorld(S.GateC + 15, S.FenceS0), Gate = D.ToWorld(S.GateC, S.FenceS0);
					const FHKWalkWorld& W = D.VehicleWorld(1.4);
					return !W.CanStand(FHKPt(Fence.X, Fence.Y), 1.2, D.GroundWorld(Fence.X, Fence.Y)) && W.CanStand(FHKPt(Gate.X, Gate.Y), 1.2, D.GroundWorld(Gate.X, Gate.Y));
				});
				T.Expect(TEXT("site: the fence blocks a person, the gate does not"), [&T]()
				{
					const FHKDirtField& S = *T.Game()->Site;
					FHKData& D = FHKData::Get();
					const FVector2D Fence = D.ToWorld(S.GateC - 20, S.FenceS0), Gate = D.ToWorld(S.GateC + 3, S.FenceS0);
					return !D.WalkWorld.CanStand(FHKPt(Fence.X, Fence.Y), 1.6, D.GroundWorld(Fence.X, Fence.Y)) && D.WalkWorld.CanStand(FHKPt(Gate.X, Gate.Y), 1.6, D.GroundWorld(Gate.X, Gate.Y));
				});
				T.Do([V, Tp]()
				{
					V->Target = [Tp]() { const FHKDumpTruck* D = Tp(); return FVector(D->X, D->Y + 1.5, D->Z); };
					V->Yaw = Tp()->Yaw + 2.5; V->Dist = 16; V->Up = 5;
				});
				Snap(T, V, TEXT("15-tipper-at-gate"));
				// The driver's view: the open gate through the windscreen.
				T.Do([&T]() { T.Game()->bCameraOverride = false; });
				T.Wait(.5);
				T.Shot(TEXT("15b-tipper-cab-gate"));
				Act(T, V, TEXT("tipper: out through the gate"), 20, [&T, Tp, Loc, GateAt, SteerFor](double)
				{
					Pad().Buttons[7] = .5f; Pad().Axes[0] = SteerFor(GateAt(-14), false);
					return Loc().Y - T.Game()->Site->FenceS0 < -8;
				});
				T.Log([&T]() { return TEXT("tipper outside: ") + T.Game()->SpeedoText; });
				Act(T, V, TEXT("tipper: stop outside"), 8, [Tp](double) { Pad().Buttons[6] = 1; return FMath::Abs(Tp()->Speed) < .2; });
				T.Expect(TEXT("tipper: drove out of the site"), [&T, Tp]() { return !T.Game()->Site->Contains(Tp()->X, Tp()->Z); });
				T.Log([&T, Loc]() { return FString::Printf(TEXT("tipper %.1f m outside the gate, %.1f m off its centre line"), T.Game()->Site->FenceS0 - Loc().Y, Loc().X - T.Game()->Site->GateC); });
				T.Do([V, Tp]() { V->Yaw = Tp()->Yaw + 2.6; V->Dist = 20; V->Up = 7; });
				Snap(T, V, TEXT("15c-tipper-outside"));
				// Back in, reversing through the gate.
				Act(T, V, TEXT("tipper: back in through the gate"), 30, [&T, Loc, GateAt, SteerFor](double)
				{
					Pad().Buttons[6] = .5f; Pad().Axes[0] = SteerFor(GateAt(14), true);
					return Loc().Y - T.Game()->Site->FenceS0 > 9;
				});
				Act(T, V, TEXT("tipper: stop inside"), 8, [Tp](double) { Pad().Buttons[7] = 1; return FMath::Abs(Tp()->Speed) < .2; });
				T.Expect(TEXT("tipper: back inside the site"), [&T, Tp]() { return T.Game()->Site->Contains(Tp()->X, Tp()->Z, 2); });
				Snap(T, V, TEXT("15d-tipper-back-in"));
				GetOut(T, TEXT("tipper"));

				// ---- Mobile crane: outriggers down, boom up and out, slew ----
				auto Cr = [&T]() { return Find<FHKMobileCrane>(T, TEXT("crane")); };
				Board(T, TEXT("crane"));
				T.Do([V, Cr]()
				{
					V->Target = [Cr]() { const FHKMobileCrane* C = Cr(); return FVector(C->X, C->Y + 4, C->Z); };
					V->Yaw = Cr()->Yaw - 2.0; V->Dist = 30; V->Up = 10;
				});
				T.Press(4);
				Act(T, V, TEXT("crane: outriggers"), 8, [Cr](double) { return Cr()->Deploy >= 1; });
				T.Log([Cr]() { return FString::Printf(TEXT("crane outriggers %.2f"), Cr()->Deploy); });
				// Boom up and out until it gets there (the game steps at most 50 ms a frame, so time held is not enough).
				Act(T, V, TEXT("crane: boom up"), 20, [Cr](double) { Pad().Axes[3] = 1; return Cr()->Luff > .7; });
				Act(T, V, TEXT("crane: telescope out"), 20, [Cr](double) { Pad().Axes[1] = -1; return Cr()->Ext > 5; });
				For(T, V, 3, []() { Pad().Axes[0] = .8f; });
				T.Log([&T, Cr]() { const FHKMobileCrane* C = Cr(); return FString::Printf(TEXT("crane luff %.2f rad, telescope %.1f m, slew %.2f rad, wire %.1f m: %s"), C->Luff, C->Ext, C->Slew, C->Hook.Wire, *T.Game()->SpeedoText); });
				T.Expect(TEXT("crane: boom raised and telescoped"), [Cr]() { return Cr()->Luff > .5 && Cr()->Ext > 3; });
				Snap(T, V, TEXT("16-mobile-crane"));
				OwnShot(T, TEXT("16a-crane-cab"));
				ViewTo(T, 2, TEXT("16b-crane-hook"));
				ViewTo(T, 1, TEXT("16c-crane-outside"));
				ViewTo(T, 0, TEXT("16d-crane-cab-again"));
				GetOut(T, TEXT("crane"));

				// ---- Tower crane: pick up a pallet of bricks, lift it, slew and set it down ----
				auto Tw = [&T]() { return Find<FHKTowerCrane>(T, TEXT("tower")); };
				TSharedPtr<FHKSiteLoad*> Load = MakeShared<FHKSiteLoad*>(nullptr);
				TSharedPtr<FVector> LoadStart = MakeShared<FVector>();
				Board(T, TEXT("tower"));
				OwnShot(T, TEXT("17a-tower-cab"));
				T.Do([V, Tw, Load, LoadStart]()
				{
					*Load = FHKSiteMachine::Loads()[0];
					*LoadStart = (*Load)->P;
					V->Target = [Load]() { return (*Load)->P + FVector(0, 4, 0); };
					V->Yaw = Tw()->Yaw + 1.2; V->Dist = 24; V->Up = 6;
				});
				Snap(T, V, TEXT("17-tower-crane"));
				Act(T, V, TEXT("tower crane: hook over the load"), 60, [Tw, Load](double)
				{
					FHKTowerCrane* C = Tw();
					const FVector L = (*Load)->P;
					const double Want = Wrap(Bearing(C->X, C->Z, L.X, L.Z) - C->Yaw);
					const double Err = Wrap(Want - C->Slew), R = FVector2D(L.X - C->X, L.Z - C->Z).Size();
					// Slewing: aim for a speed proportional to the error (the jib has inertia).
					const double Rate = FMath::Clamp(Err * .35, -.075, .075);
					FHKPadState& S = Pad();
					const double Want0 = FMath::Clamp(-(Rate + (Rate - C->SlewV) * 2) / .075, -1.0, 1.0);
					S.Axes[0] = FMath::Abs(Want0) < .04 ? 0.f : Raw(Want0);
					S.Axes[1] = Toward(-(R - C->Trolley), 1, .08);
					const double Sway = FVector2D(C->Hook.Vel.X, C->Hook.Vel.Z).Size();
					const bool bOver = FMath::Abs(Err) * R < .35 && S.Axes[1] == 0 && Sway < .25;
					static double Next = 0;
					if (FPlatformTime::Seconds() > Next)
					{
						Next = FPlatformTime::Seconds() + 3;
						UE_LOG(LogHaarklous, Display, TEXT("HK-TEST tower crane: slew error %.3f rad (speed %.4f), radius %.2f / trolley %.2f, sway %.2f m/s, hook %.1f m above the load"),
							Err, C->SlewV, R, C->Trolley, Sway, C->Hook.P.Y - L.Y - (*Load)->Height);
					}
					S.Axes[3] = bOver && C->Hook.P.Y > L.Y + (*Load)->Height + .8 ? -1.f : 0;
					return bOver && C->Hook.P.Y <= L.Y + (*Load)->Height + .8;
				});
				T.Log([Tw, Load]() { const FHKTowerCrane* C = Tw(); return FString::Printf(TEXT("tower crane: hook %.2f m from the load's top"), FVector::Dist(C->Hook.P, (*Load)->P + FVector(0, (*Load)->Height, 0))); });
				T.Press(0);
				T.Expect(TEXT("tower crane: load hooked"), [Tw]() { return Tw()->Hook.Load != nullptr; });
				For(T, V, 6, []() { Pad().Axes[3] = 1; });
				T.Log([&T, Load]() { return FString::Printf(TEXT("tower crane lifting: load %.1f m above the ground. %s"), (*Load)->P.Y - Gnd((*Load)->P), *T.Game()->SpeedoText); });
				T.Expect(TEXT("tower crane: load in the air"), [Load]() { return (*Load)->P.Y - Gnd((*Load)->P) > 3; });
				Snap(T, V, TEXT("18-tower-crane-lift"));
				ViewTo(T, 2, TEXT("18b-tower-hook"));
				ViewTo(T, 0, TEXT("18c-tower-cab-again"));
				For(T, V, 8, []() { Pad().Axes[0] = -1; Pad().Axes[1] = .6f; });
				Snap(T, V, TEXT("19-tower-crane-slew"));
				Act(T, V, TEXT("tower crane: stop the slew"), 15, [Tw](double) { return FMath::Abs(Tw()->SlewV) < .003; });
				Act(T, V, TEXT("tower crane: lower the load"), 25, [Tw, Load](double)
				{
					Pad().Axes[3] = -1;
					return (*Load)->P.Y - Gnd((*Load)->P) < .05;
				});
				T.Press(0);
				T.Wait(.3);
				T.Expect(TEXT("tower crane: load set down somewhere else"), [Tw, Load, LoadStart]()
				{
					return Tw()->Hook.Load == nullptr && !(*Load)->bHooked && FVector2D((*Load)->P.X - LoadStart->X, (*Load)->P.Z - LoadStart->Z).Size() > 1.5;
				});
				Snap(T, V, TEXT("20-tower-crane-set-down"));
				GetOut(T, TEXT("tower"));

				// The frame rate at the site: looking over it from the gate and from the tower crane's cab.
				T.Do([&T, V]()
				{
					const FHKDirtField& S = *T.Game()->Site;
					const FVector2D C = S.NodeWorld(S.NX / 2, S.NS / 2);
					V->Target = [C]() { return FVector(C.X, FHKData::Get().GroundWorld(C.X, C.Y), C.Y); };
					V->Yaw = 2.6; V->Dist = 34; V->Up = 3;
				});
				Snap(T, V, TEXT("21-after-work"));
				T.MeasureFps(TEXT("site"), 8);
				T.FreeCamera();
			});
		}
	} GSiteRegister;
}
