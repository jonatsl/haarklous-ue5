// Scripted play-throughs (run with -hktest=<name>). Positions are in the web frame; buttons use the standard mapping:
// 0 cross, 1 circle, 2 square, 3 triangle, 4 L1, 5 R1, 6 L2, 7 R2, 8 share, 9 options, 10 L3, 11 R3, 12-15 D-pad, 16 PS, 17 touchpad.
#include "HKTestDriver.h"
#include "HKData.h"
#include "HKGame.h"
#include "HKInput.h"
#include "HKMath.h"
#include "HKVehicles.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"

namespace
{
	// A camera standing H metres above the ground at (x, z), looking along yaw (web yaw, 0 = toward -z) and pitch.
	void Ground(AHKTestDriver& T, double X, double Z, double H, double Yaw, double Pitch)
	{
		T.Pose(X, FHKData::Get().HeightWorld(X, Z) + H, Z, Yaw, Pitch);
	}
	// Yaw that looks from (x0, z0) toward (x1, z1).
	double Toward(double X0, double Z0, double X1, double Z1) { return FMath::Atan2(-(X1 - X0), -(Z1 - Z0)); }

	struct FRegister
	{
		FRegister()
		{
			// Views of the whole world for checking the art (no gameplay).
			FHKTestScripts::All().Add(TEXT("shots"), [](AHKTestDriver& T)
			{
				T.Wait(3);
				T.Shot(TEXT("00-start-screen"));
				T.Press(0);
				T.Wait(1.5);
				T.Shot(TEXT("01-player-start"));
				Ground(T, 6, 24, 1.7, Toward(6, 24, 1, 6), .05); T.Shot(TEXT("02-front-of-17A"));
				Ground(T, -12, 44, 1.7, Toward(-12, 44, -2.7, 34.7), -.1); T.Shot(TEXT("03-firepit"));
				Ground(T, 20, 30, 1.7, Toward(20, 30, 26, 14), -.05); T.Shot(TEXT("04-helipad"));
				// Apartment rooms (apartment-data.js): living x 0..2.9 z 0..4.9, kitchen x 1.7..5.7 z 6.3..8.9, bedroom z 8.5..10.
				T.Pose(0.4, 1.62, 0.4, Toward(0.4, 0.4, 2.6, 4.5), -.1); T.Shot(TEXT("05-apartment-living"));
				T.Pose(2.0, 1.62, 6.6, Toward(2.0, 6.6, 5.4, 8.6), -.2); T.Shot(TEXT("06-apartment-kitchen"));
				T.Pose(2.0, 1.62, 9.7, Toward(2.0, 9.7, 5.5, 8.7), -.15); T.Shot(TEXT("06b-apartment-bedroom"));
				T.Pose(0.2, -8.3, 8.0, HK::Pi, .25); T.Shot(TEXT("07-stairwell"));
				T.Pose(40, 70, 90, Toward(40, 90, 0, 0), -.55); T.Shot(TEXT("08-aerial-home"));
				T.Pose(-60, 120, 120, Toward(-60, 120, 60, -60), -.45); T.Shot(TEXT("09-aerial-district"));
				Ground(T, -116.5, 0, 1.7, .5236, 0); T.Shot(TEXT("10-airstrip"));
				// The sky with and without the cloud dome (a sphere mesh around the world).
				Ground(T, 6, 24, 1.7, 2.5, .6); T.Shot(TEXT("11-sky"));
				T.Do([&T]()
				{
					for (TActorIterator<AStaticMeshActor> It(T.GetWorld()); It; ++It)
						if (It->GetStaticMeshComponent()->GetStaticMesh() && It->GetStaticMeshComponent()->GetStaticMesh()->GetName() == TEXT("Sphere")) It->SetActorHiddenInGame(true);
				});
				T.Shot(TEXT("12-sky-without-dome"));
				T.FreeCamera();
			});

			// Frame rate in the busiest places (run at the real resolution, e.g. -Res 1920x1080 -Extra "-fullscreen").
			FHKTestScripts::All().Add(TEXT("fps"), [](AHKTestDriver& T)
			{
				T.Wait(3);
				T.Press(0);
				T.Wait(4);
				T.Log([&T]()
				{
					const FIntPoint S = GEngine->GameViewport && GEngine->GameViewport->Viewport ? GEngine->GameViewport->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
					const IConsoleVariable* Sp = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
					return FString::Printf(TEXT("fps viewport %dx%d, screen percentage %.0f, quality %d"), S.X, S.Y, Sp ? Sp->GetFloat() : 0.f, T.Game()->Quality);
				});
				auto Sweep = [&T](const FString& Label, double X, double Y, double Z, double Pitch)
				{
					// Look around slowly (one full turn over the measurement) so every direction counts.
					TSharedPtr<double> Yaw = MakeShared<double>(0);
					T.Steps.Add([&T, Yaw, X, Y, Z, Pitch](double Dt)
					{
						AHKGame* G = T.Game();
						*Yaw += Dt * 2 * HK::Pi / 12;
						G->bCameraOverride = true;
						G->OverridePos = FVector(X, Y, Z);
						G->OverrideQuat = HK::EulerToQuat(Pitch, *Yaw, 0, TEXT("YXZ"));
						return *Yaw >= 2 * HK::Pi / 12 * 1.2;   // 1.2 s to settle
					});
					T.Shot(TEXT("fps-") + Label);
					// Measure 12 s while turning one full circle.
					struct FAcc { double T = 0; TArray<double> Frames; };
					TSharedPtr<FAcc> A = MakeShared<FAcc>();
					T.Steps.Add([&T, Yaw, A, Label, Pitch](double Dt)
					{
						AHKGame* G = T.Game();
						*Yaw += Dt * 2 * HK::Pi / 12;
						G->OverrideQuat = HK::EulerToQuat(Pitch, *Yaw, 0, TEXT("YXZ"));
						A->T += Dt; A->Frames.Add(Dt);
						if (A->T < 12) return false;
						TArray<double> F = A->Frames;
						F.Sort();
						const double Avg = F.Num() / A->T, Low = 1 / F[FMath::Min(F.Num() - 1, (int32)(F.Num() * .99))], Worst = 1 / F.Last();
						UE_LOG(LogTemp, Display, TEXT("HK-TEST fps %s: average %.1f, 1%% low %.1f, slowest frame %.1f fps (%d frames, %.1f s)"), *Label, Avg, Low, Worst, F.Num(), A->T);
						return true;
					});
				};
				Sweep(TEXT("home"), 3.2, 1.62, 6.5, -.05);
				Sweep(TEXT("firepit"), -6, FHKData::Get().HeightWorld(-6, 38) + 1.65, 38, -.05);
				Sweep(TEXT("park"), FHKData::Get().ToWorld(110, 40).X, FHKData::Get().HeightWorld(FHKData::Get().ToWorld(110, 40).X, FHKData::Get().ToWorld(110, 40).Y) + 1.65, FHKData::Get().ToWorld(110, 40).Y, 0);
				Sweep(TEXT("street"), 2, -8.3, 22, 0);
				Sweep(TEXT("flying"), 30, 110, 60, -.35);
				T.FreeCamera();
			});

			// The drunk post-process at a few levels and screen percentages, next to a sober frame.
			FHKTestScripts::All().Add(TEXT("drunk"), [](AHKTestDriver& T)
			{
				T.Wait(3);
				T.Press(0);
				T.Wait(1);
				T.Do([&T]() { AHKGame* G = T.Game(); G->Place(-6, 38, 0); G->Drunk = 0; });
				T.Wait(1); T.Shot(TEXT("01-sober"));
				T.Do([&T]() { T.Game()->Drunk = 30; });
				T.Wait(.5); T.Shot(TEXT("02-drunk-30s"));
				T.Wait(1); T.Shot(TEXT("02b-drunk-29s"));
				T.Wait(1); T.Shot(TEXT("02c-drunk-28s"));
				T.Do([&T]() { T.Game()->Drunk = 90; });
				T.Wait(.5); T.Shot(TEXT("03-drunk-90s"));
				T.Do([&T]() { GEngine->Exec(T.GetWorld(), TEXT("r.ScreenPercentage 100")); });
				T.Wait(.5); T.Shot(TEXT("04-drunk-90s-sp100"));
				T.Do([&T]() { GEngine->Exec(T.GetWorld(), TEXT("r.ScreenPercentage 75")); T.Game()->Drunk = 0; });
				T.Wait(.5); T.Shot(TEXT("05-sober-again"));
			});

			// Every vehicle: get in with square, drive with the simulated controller, read the HUD, get out.
			FHKTestScripts::All().Add(TEXT("vehicles"), [](AHKTestDriver& T)
			{
				T.Wait(3);
				T.Press(0);
				T.Wait(1);
				auto Approach = [&T](const FString& Kind)
				{
					T.Do([&T, Kind]()
					{
						AHKGame* G = T.Game();
						for (FHKVehicle* V : G->VehicleList)
							if (V->Kind == Kind)
							{
								const FVector2D F = V->Forward(), R(-F.Y, F.X);
								const double D = V->Width / 2 + .9;
								G->Place(V->X + R.X * D, V->Z + R.Y * D, FMath::Atan2(R.X, R.Y));
								UE_LOG(LogTemp, Display, TEXT("HK-TEST at the %s: %s"), *V->Name, *T.P());
								break;
							}
					});
					T.Wait(.6);
					T.Log([&T]() { return TEXT("prompt: ") + T.Game()->LastPromptText(); });
					T.Press(2);
					T.Wait(.8);
					T.Expect(Kind + TEXT(": in the driver's seat"), [&T]() { return T.Game()->Player.Mode == TEXT("vehicle"); });
				};
				auto Speedo = [&T](const FString& What) { T.Log([&T, What]() { return What + TEXT(": ") + T.Game()->SpeedoText + TEXT("  ") + T.P(); }); };
				auto GetOut = [&T](const FString& Kind)
				{
					T.Wait(.5);
					T.Press(2);
					T.Wait(.6);
					T.Expect(Kind + TEXT(": back on foot"), [&T]() { return T.Game()->Player.Mode == TEXT("foot"); });
				};
				// Motorcycle: full throttle for 6 s through the gears, a lean in a turn, brake to a stop.
				Approach(TEXT("motorcycle"));
				T.Hold(7, 1); T.Wait(3); Speedo(TEXT("bike after 3 s")); T.Shot(TEXT("01-motorcycle"));
				T.Wait(3); Speedo(TEXT("bike after 6 s"));
				T.Do([]() { if (auto In = FHKInputProcessor::Get()) In->SimulatedPad.Axes[0] = .8f; });
				T.Wait(1.2); T.Log([&T]() { const FHKMotorcycle* M = static_cast<FHKMotorcycle*>(T.Game()->Player.Vehicle); return FString::Printf(TEXT("bike lean in the turn %.3f rad at %.1f km/h"), M->Lean, FMath::Abs(M->Speed) * 3.6); });
				T.Shot(TEXT("02-motorcycle-lean"));
				T.Do([]() { if (auto In = FHKInputProcessor::Get()) { In->SimulatedPad.Axes[0] = 0; In->SimulatedPad.Buttons[7] = 0; In->SimulatedPad.Buttons[6] = 1; } });
				T.Steps.Add([&T](double) { return FMath::Abs(T.Game()->Player.Vehicle->Speed) < .5; });
				T.Hold(6, 0);
				Speedo(TEXT("bike stopped"));
				GetOut(TEXT("motorcycle"));
				// Car.
				Approach(TEXT("car"));
				T.Hold(7, 1); T.Wait(3); Speedo(TEXT("car after 3 s")); T.Shot(TEXT("03-car"));
				T.Hold(7, 0); T.Hold(6, 1);
				T.Steps.Add([&T](double) { return FMath::Abs(T.Game()->Player.Vehicle->Speed) < .5; });
				T.Hold(6, 0);
				GetOut(TEXT("car"));
				// Excavator: both tracks forward, then swing and boom.
				Approach(TEXT("excavator"));
				T.Hold(4, 1); T.Hold(5, 1); T.Wait(2); T.Hold(4, 0); T.Hold(5, 0);
				Speedo(TEXT("excavator after 2 s on both tracks"));
				T.Stick(.8, 0, 1.5, 0, .8);
				T.Shot(TEXT("04-excavator"));
				GetOut(TEXT("excavator"));
				// Helicopter: collective up for 9 s (spool-up, lift-off), chase camera, then down and out.
				Approach(TEXT("helicopter"));
				T.Do([]() { if (auto In = FHKInputProcessor::Get()) In->SimulatedPad.Axes[1] = -.6f; });
				T.Wait(9); Speedo(TEXT("helicopter after 9 s collective up")); T.Shot(TEXT("05-helicopter"));
				T.Press(11); T.Wait(1); T.Shot(TEXT("06-helicopter-chase")); T.Press(11);
				T.Do([]() { if (auto In = FHKInputProcessor::Get()) In->SimulatedPad.Axes[1] = .4f; });
				T.Steps.Add([&T](double) { return T.Game()->Player.Vehicle->bGrounded; });
				T.Do([]() { if (auto In = FHKInputProcessor::Get()) In->SimulatedPad.Axes[1] = 0; });
				T.Wait(3); Speedo(TEXT("helicopter landed"));
				GetOut(TEXT("helicopter"));
				// Plane: full throttle, pull back at about 85 km/h, lift-off distance.
				Approach(TEXT("plane"));
				TSharedPtr<FVector> Start = MakeShared<FVector>();
				T.Do([&T, Start]() { const FHKVehicle* V = T.Game()->Player.Vehicle; *Start = FVector(V->X, V->Y, V->Z); if (auto In = FHKInputProcessor::Get()) In->SimulatedPad.Axes[1] = -1; });
				T.Wait(4);
				T.Do([]() { if (auto In = FHKInputProcessor::Get()) In->SimulatedPad.Axes[1] = 0; });
				T.Steps.Add([&T](double) { return T.Game()->Player.Vehicle->Speed * 3.6 > 85; });
				T.Do([]() { if (auto In = FHKInputProcessor::Get()) In->SimulatedPad.Axes[3] = .7f; });
				T.Steps.Add([&T](double) { return !T.Game()->Player.Vehicle->bGrounded; });
				T.Log([&T, Start]() { const FHKVehicle* V = T.Game()->Player.Vehicle; return FString::Printf(TEXT("plane lifted off at %.0f km/h after %.1f m (web: about 87 km/h after 76-87 m)"), V->Speed * 3.6, HK::Hypot(V->X - Start->X, V->Z - Start->Z)); });
				T.Wait(4);
				T.Do([]() { if (auto In = FHKInputProcessor::Get()) In->SimulatedPad.Axes[3] = 0; });
				Speedo(TEXT("plane climbing")); T.Shot(TEXT("07-plane"));
				T.Press(11); T.Wait(1.5); T.Shot(TEXT("08-plane-chase"));
			});

			// Activities on foot: drone, photo camera, watch, map, quick chat, firepit, disc golf, petanque.
			FHKTestScripts::All().Add(TEXT("activities"), [](AHKTestDriver& T)
			{
				T.Wait(3);
				T.Press(0);
				T.Wait(1);
				// Wristwatch (D-pad up).
				T.Press(12); T.Wait(1); T.Shot(TEXT("01-watch"));
				T.Expect(TEXT("watch raised"), [&T]() { return T.Game()->Watch.Target > 0; });
				T.Press(12); T.Wait(.6);
				// Photo camera: tap triangle, raise with L2, take a photo with R2, open the gallery with square.
				T.Press(3); T.Wait(.6); T.Shot(TEXT("02-camera-out"));
				T.Expect(TEXT("photo camera out"), [&T]() { return T.Game()->Photo.bActive; });
				T.Hold(6, 1); T.Wait(.5); T.Shot(TEXT("03-viewfinder"));
				TSharedPtr<int32> Before = MakeShared<int32>(0);
				T.Do([&T, Before]() { *Before = T.Game()->Photo.Photos.Num(); });
				T.Hold(7, 1); T.Wait(.4); T.Hold(7, 0); T.Wait(1.5);
				T.Expect(TEXT("a photo was saved"), [&T, Before]() { return T.Game()->Photo.Photos.Num() == *Before + 1; });
				T.Hold(6, 0);
				T.Press(2); T.Wait(1); T.Shot(TEXT("04-gallery"));
				T.Press(1); T.Wait(.3); T.Press(1); T.Wait(.3);
				T.Expect(TEXT("camera put away"), [&T]() { return !T.Game()->Photo.bActive; });
				// Gear menu: hold triangle, choose the drone, release.
				T.Hold(3, 1); T.Wait(.5); T.Press(15); T.Wait(.3); T.Shot(TEXT("05-gear-menu"));
				T.Hold(3, 0); T.Wait(.5);
				T.Expect(TEXT("flying the FPV drone"), [&T]() { return T.Game()->Player.Mode == TEXT("drone"); });
				T.Do([]() { if (auto In = FHKInputProcessor::Get()) In->SimulatedPad.Axes[1] = -.5f; });
				T.Wait(2.5);
				T.Do([]() { if (auto In = FHKInputProcessor::Get()) { In->SimulatedPad.Axes[1] = 0; In->SimulatedPad.Axes[3] = -.3f; } });
				T.Wait(1.5); T.Shot(TEXT("06-drone"));
				T.Do([]() { if (auto In = FHKInputProcessor::Get()) In->SimulatedPad.Axes[3] = 0; });
				T.Press(1); T.Wait(.5);
				T.Expect(TEXT("drone packed away"), [&T]() { return T.Game()->Player.Mode == TEXT("foot"); });
				// Map (touchpad).
				T.Press(17); T.Wait(.8); T.Shot(TEXT("07-map"));
				T.Press(1); T.Wait(.3);
				// Quick chat (share): send "Hei!" offline.
				T.Press(8); T.Wait(.4); T.Shot(TEXT("08-quick-chat")); T.Press(0); T.Wait(.6);
				T.Expect(TEXT("chat line shown"), [&T]() { return T.Game()->ChatLog.Num() > 0; });
				// Firepit: throw a log, sit on a bench, have a beer, stand up.
				T.Do([&T]() { AHKGame* G = T.Game(); G->Place(G->FireWoodpile.X + 1, G->FireWoodpile.Y, 0); G->Player.Yaw = FMath::Atan2(-(G->FireWoodpile.X - G->Player.X), -(G->FireWoodpile.Y - G->Player.Z)); });
				T.Wait(.5);
				TSharedPtr<double> Lv = MakeShared<double>(0);
				T.Do([&T, Lv]() { *Lv = T.Game()->FireLevel(); });
				T.Log([&T]() { return TEXT("prompt: ") + T.Game()->LastPromptText(); });
				T.Press(2); T.Wait(1.5);
				T.Expect(TEXT("fire one level higher after a log"), [&T, Lv]() { return T.Game()->FireLevel() > *Lv + .9; });
				// A seat away from the woodpile and the crate (their prompts come first, as in the web).
				T.Do([&T]()
				{
					AHKGame* G = T.Game();
					const FHKSitSpot* Best = &G->FireSpots[0];
					double Bd = -1;
					for (const FHKSitSpot& S : G->FireSpots)
					{
						const double D = FMath::Min(HK::Hypot(S.X - G->FireWoodpile.X, S.Z - G->FireWoodpile.Y), HK::Hypot(S.X - G->FireCrate.X, S.Z - G->FireCrate.Y));
						if (D > Bd) { Bd = D; Best = &S; }
					}
					// Stand just behind the seat (away from the fire), facing the fire.
					const double Dx = Best->X - G->FireCenter.X, Dz = Best->Z - G->FireCenter.Z, L = HK::Hypot(Dx, Dz);
					G->Place(Best->X + Dx / L * .45, Best->Z + Dz / L * .45, FMath::Atan2(Dx, Dz));
				});
				T.Log([&T]() { return TEXT("prompt: ") + T.Game()->LastPromptText(); });
				T.Wait(.5); T.Press(2); T.Wait(.5);
				T.Expect(TEXT("sitting by the fire"), [&T]() { return T.Game()->Player.Mode == TEXT("sit"); });
				T.Hold(7, 1); T.Wait(.3); T.Hold(7, 0); T.Wait(1);
				T.Shot(TEXT("09-firepit-sitting"));
				T.Expect(TEXT("beer from the crate: drunk"), [&T]() { return T.Game()->Drunk > 25; });
				T.Press(2); T.Wait(.5);
				// Disc golf: walk onto the first tee, start, throw.
				T.Do([&T]() { AHKGame* G = T.Game(); const TArray<double> Tee = HKJson::Nums(HKJson::Arr(FHKData::Get().D, TEXT("discgolf"))[0]->AsObject(), TEXT("tee")); const FVector2D W = FHKData::Get().ToWorld(Tee[0], Tee[1]); G->Place(W.X, W.Y, 0); });
				T.Wait(.6);
				T.Log([&T]() { return TEXT("prompt: ") + T.Game()->LastPromptText(); });
				T.Press(2); T.Wait(.8);
				T.Expect(TEXT("disc golf started"), [&T]() { return T.Game()->Player.Mode == TEXT("minigame"); });
				T.Hold(7, 1); T.Wait(1.0); T.Shot(TEXT("10-discgolf-charge")); T.Hold(7, 0);
				T.Wait(3); T.Shot(TEXT("11-discgolf-flight"));
				T.Wait(4);
				T.Press(1); T.Wait(1);
				// Petanque.
				T.Do([&T]() { AHKGame* G = T.Game(); const TSharedPtr<FJsonObject> Pq = HKJson::Obj(FHKData::Get().D, TEXT("petanque")); const FVector2D W = FHKData::Get().ToWorld(HKJson::Num(Pq, TEXT("x")), HKJson::Num(Pq, TEXT("s"))); G->Place(W.X, W.Y, 0); });
				T.Wait(.6);
				T.Log([&T]() { return TEXT("prompt: ") + T.Game()->LastPromptText(); });
				T.Press(2); T.Wait(1);
				T.Hold(7, 1); T.Wait(.7); T.Hold(7, 0); T.Wait(4); T.Shot(TEXT("12-petanque"));
				T.Press(1); T.Wait(1);
			});

			// Hostage rescue: the red ring beside the entrance, the briefing, the pistol HUD, aim and fire, leave.
			FHKTestScripts::All().Add(TEXT("mission"), [](AHKTestDriver& T)
			{
				T.Wait(3);
				T.Press(0);
				T.Wait(1);
				T.Do([&T]() { AHKGame* G = T.Game(); G->Place(G->MissionMark.X, G->MissionMark.Y + .3, 0); });
				T.Wait(.6);
				T.Log([&T]() { return TEXT("prompt: ") + T.Game()->LastPromptText(); });
				T.Expect(TEXT("mission prompt in the ring"), [&T]() { return T.Game()->LastPromptText().Contains(TEXT("Start mission")); });
				T.Press(2);
				T.Wait(1.5);
				T.Expect(TEXT("mission active"), [&T]() { return T.Game()->Mission && T.Game()->Mission->IsActive(); });
				T.Shot(TEXT("01-briefing"));
				T.Log([&T]() { return TEXT("state ") + T.Game()->Mission->GetState(); });
				T.Press(0);   // begin
				T.Wait(2);
				T.Log([&T]() { return TEXT("state ") + T.Game()->Mission->GetState() + TEXT(" ") + T.P(); });
				T.Shot(TEXT("02-play"));
				T.Hold(6, 1); T.Wait(.8); T.Shot(TEXT("03-aim"));
				T.Hold(7, 1); T.Wait(.15); T.Hold(7, 0); T.Wait(.4);
				T.Hold(7, 1); T.Wait(.15); T.Hold(7, 0); T.Wait(.4);
				T.Shot(TEXT("04-fired"));
				T.Hold(6, 0);
				T.Wait(1);
				T.Press(9);   // pause panel
				T.Wait(.8);
				T.Shot(TEXT("05-paused"));
				T.Press(1);   // leave
				T.Wait(1.5);
				T.Expect(TEXT("mission left, on foot"), [&T]() { return !T.Game()->Mission->IsActive() && T.Game()->Player.Mode == TEXT("foot"); });
				T.Shot(TEXT("06-after"));
			});

			// On foot: street door -> stairs -> 4th floor -> entrance door -> the flat -> a beer (the web's verified route).
			FHKTestScripts::All().Add(TEXT("walkhome"), [](AHKTestDriver& T)
			{
				T.Wait(2);
				T.Press(0);
				T.Wait(1);
				T.Log([&T]() { return TEXT("start ") + T.P(); });
				T.WalkTo(0.75, 12.4);
				T.Do([&T]() { T.Game()->Player.Yaw = 0; T.Game()->Player.Pitch = -.1; });
				T.Wait(.4);
				T.Expect(TEXT("prompt at the street door"), [&T]() { return T.Game()->LastPromptText().Contains(TEXT("street door")); });
				T.Log([&T]() { return TEXT("prompt: ") + T.Game()->LastPromptText(); });
				T.Shot(TEXT("01-street-door"));
				T.Press(2);
				T.Wait(1.2);
				T.Expect(TEXT("street door open"), [&T]() { const FHKDoor* D = T.Game()->Doors.FindByPredicate([](const FHKDoor& X) { return X.Id == TEXT("own-street-door"); }); return D && D->bOpen; });
				T.Shot(TEXT("02-door-open"));
				T.WalkTo(0.75, 10.2);
				// Four storeys: west flight up to each landing, east flight up to each half landing.
				for (int32 n = 1; n <= 4; n++)
				{
					T.WalkTo(-0.62, 9.4); T.WalkTo(-0.62, 6.4);
					if (n < 4) { T.WalkTo(0.82, 6.6); T.WalkTo(0.82, 9.6); }
					T.Log([&T, n]() { return FString::Printf(TEXT("landing %d %s"), n, *T.P()); });
				}
				T.Expect(TEXT("on the 4th floor landing (feet ~0)"), [&T]() { return FMath::Abs(T.Game()->Player.Feet) < .05; });
				T.WalkTo(0.25, 5.83);
				T.Do([&T]() { T.Game()->Player.Yaw = -HK::Pi / 2; T.Game()->Player.Pitch = -.05; });
				T.Wait(.4);
				T.Shot(TEXT("03-entrance-door"));
				T.Log([&T]() { return TEXT("prompt: ") + T.Game()->LastPromptText(); });
				T.Press(2);
				T.Wait(1.2);
				T.WalkTo(2.6, 5.8);
				T.Expect(TEXT("in the apartment"), [&T]() { return T.Game()->InApartment(); });
				T.Shot(TEXT("04-hall"));
				// A beer from the kitchen worktop: walk near it, look at it, press square.
				T.Do([&T]()
				{
					AHKGame* G = T.Game();
					const FVector B = G->Drinks[0].Object ? G->Drinks[0].Object->GetWorldPosition() : FVector::ZeroVector;
					UE_LOG(LogTemp, Display, TEXT("HK-TEST beer at (%.2f, %.2f, %.2f)"), B.X, B.Y, B.Z);
				});
				T.Steps.Add([&T](double)
				{
					AHKGame* G = T.Game();
					const FVector B = G->Drinks[0].Object->GetWorldPosition();
					const FVector2D Dir = FVector2D(B.X - G->Player.X, B.Z - G->Player.Z);
					const double D = Dir.Size();
					if (D > .9) { G->Player.Yaw = FMath::Atan2(-Dir.X, -Dir.Y); if (TSharedPtr<FHKInputProcessor> In = FHKInputProcessor::Get()) In->SimulatedPad.Axes[1] = -1; return false; }
					if (TSharedPtr<FHKInputProcessor> In = FHKInputProcessor::Get()) In->SimulatedPad.Axes[1] = 0;
					return true;
				});
				T.Do([&T]()
				{
					AHKGame* G = T.Game();
					const FVector B = G->Drinks[0].Object->GetWorldPosition() + FVector(0, .1, 0);
					const FVector E = FVector(G->Player.X, G->Player.Feet + G->Player.EyeNow, G->Player.Z);
					G->Player.Yaw = FMath::Atan2(-(B.X - E.X), -(B.Z - E.Z));
					G->Player.Pitch = FMath::Atan2(B.Y - E.Y, HK::Hypot(B.X - E.X, B.Z - E.Z));
				});
				T.Wait(.5);
				T.Log([&T]() { return TEXT("prompt: ") + T.Game()->LastPromptText(); });
				T.Shot(TEXT("05-beer"));
				T.Press(2);
				T.Wait(.5);
				T.Expect(TEXT("drunk 30 s after a beer"), [&T]() { return T.Game()->Drunk > 29 && T.Game()->Drunk <= 30; });
				T.Wait(3);
				T.Shot(TEXT("06-drunk"));
			});
		}
	} GRegister;
}
