// Scripted play-throughs (run with -hktest=<name>). Positions are in the web frame; buttons use the standard mapping:
// 0 cross, 1 circle, 2 square, 3 triangle, 4 L1, 5 R1, 6 L2, 7 R2, 8 share, 9 options, 10 L3, 11 R3, 12-15 D-pad, 16 PS, 17 touchpad.
#include "HKTestDriver.h"
#include "HKAudio.h"
#include "HKData.h"
#include "HKGame.h"
#include "HKInput.h"
#include "HKMath.h"
#include "HKNet.h"
#include "HKNode.h"
#include "HKOnlineGame.h"
#include "HKRigActor.h"
#include "HKVehicles.h"
#include "AudioCaptureCore.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
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
				// Close-ups for the photo-real pass: the lawn at your feet, a facade from the side (window depth).
				// The 3D lawn grows one 6 m tile per frame around the camera: give it time after the jump.
				Ground(T, -12, 44, 1.7, Toward(-12, 44, -2.7, 34.7), -.5); T.Wait(2.5); T.Shot(TEXT("04b-lawn-closeup"));
				Ground(T, -10, 42, .45, Toward(-10, 42, -2.7, 34.7), -.2); T.Wait(1); T.Shot(TEXT("04d-lawn-low"));
				Ground(T, 14, 15, 1.7, Toward(14, 15, -6, 9), .12); T.Shot(TEXT("04c-facade-side"));
				// Where Google Street View stands in Sigurd Lies gate (panorama at 59.93614 N 10.77697 E), looking at
				// bearing 322 degrees toward the next block, to compare with the real photo.
				Ground(T, -31.42, -2.52, 2.2, Toward(-31.42, -2.52, -21.78, .15), -.09); T.Wait(1.5); T.Shot(TEXT("04e-street-view-match"));
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
					const FString Line = FString::Printf(TEXT("fps viewport %dx%d, screen percentage %.0f, quality %d"), S.X, S.Y, Sp ? Sp->GetFloat() : 0.f, T.Game()->Quality);
					// Also to <out>/fps.txt: a Shipping build has no log lines.
					const FString File = FPaths::Combine(T.OutDir, TEXT("fps.txt"));
					IFileManager::Get().MakeDirectory(*T.OutDir, true);
					FFileHelper::SaveStringToFile(Line + LINE_TERMINATOR, *File);
					return Line;
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
						const FString Line = FString::Printf(TEXT("fps %s: average %.1f, 1%% low %.1f, slowest frame %.1f fps (%d frames, %.1f s)"), *Label, Avg, Low, Worst, F.Num(), A->T);
						UE_LOG(LogTemp, Display, TEXT("HK-TEST %s"), *Line);
						FFileHelper::SaveStringToFile(Line + LINE_TERMINATOR, *FPaths::Combine(T.OutDir, TEXT("fps.txt")), FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
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

			// Keyboard and mouse, the pause menu and its settings (saved to settings.ini), and with -HKOnline the lobby
			// section: own code, the controller code pad, typing a code, back home, the name field and the microphone.
			FHKTestScripts::All().Add(TEXT("menus"), [](AHKTestDriver& T)
			{
				const FString SettingsFile = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Haarklous"), TEXT("settings.ini"));
				TSharedPtr<TOptional<FString>> Before = MakeShared<TOptional<FString>>();
				TSharedPtr<FString> OldName = MakeShared<FString>();
				T.Do([SettingsFile, Before]() { FString S; if (FFileHelper::LoadFileToString(S, *SettingsFile)) *Before = S; });
				auto In = []() { return FHKInputProcessor::Get(); };
				// The lobby section is drawn offline too (like the web); its steps run only with -HKOnline.
				const bool bOnline = FParse::Param(FCommandLine::Get(), TEXT("HKOnline"));
				// Point the menu mouse at a fraction across a menu item and click (hold for sliders).
				auto Click = [&T, In, bOnline](const FString& Id, double Fx)
				{
					TSharedPtr<bool> Found = MakeShared<bool>(false);
					T.Do([&T, Id, Fx, Found, bOnline]()
					{
						FVector2D P, S;
						*Found = (bOnline || !Id.StartsWith(TEXT("mp"))) && AHKGame::MenuRect(Id, P, S);
						if (*Found) T.Game()->TestMenuMouse = FVector2D(P.X + 6 + Fx * (S.X - 12), P.Y + S.Y / 2);
						else UE_LOG(LogTemp, Display, TEXT("HK-TEST no menu item %s (not clicked)"), *Id);
					});
					T.Wait(.1);
					// Aim again from the latest layout, let the menu draw once with it, then press.
					T.Do([&T, Id, Fx, Found]()
					{
						FVector2D P, S;
						if (*Found && AHKGame::MenuRect(Id, P, S)) T.Game()->TestMenuMouse = FVector2D(P.X + 6 + Fx * (S.X - 12), P.Y + S.Y / 2);
					});
					T.Wait(.1);
					T.Do([In, Found]() { if (auto I = In(); I && *Found) I->SimulateMouse(0, 0, 0, true); });
					T.Wait(.15);
					T.Do([In, Found]() { if (auto I = In(); I && *Found) I->SimulateMouse(0, 0, 0, false); });
					T.Wait(.15);
				};
				// Hold keys for a stretch of game time (the game caps each frame at 50 ms like the web, so slow frames
				// move less per real second); the game time used is logged with the distance.
				TSharedPtr<double> Held = MakeShared<double>(0);
				auto HoldKeys = [&T, In, Held](TArray<FString> Codes, double GameSeconds)
				{
					T.Do([In, Codes, Held]() { *Held = 0; if (auto I = In()) for (const FString& C : Codes) I->SimulateKey(C, true); });
					T.Steps.Add([In, Codes, Held, GameSeconds](double Dt)
					{
						*Held += FMath::Min(Dt, .05);
						if (*Held < GameSeconds) return false;
						if (auto I = In()) for (const FString& C : Codes) I->SimulateKey(C, false);
						return true;
					});
					T.Wait(.2);
				};
				TSharedPtr<FVector> P0 = MakeShared<FVector>();
				auto Mark = [&T, P0]() { T.Do([&T, P0]() { const AHKGame* G = T.Game(); *P0 = FVector(G->Player.X, G->Player.Z, G->Player.Yaw); }); };
				auto Moved = [&T, P0]() { const AHKGame* G = T.Game(); return HK::Hypot(G->Player.X - P0->X, G->Player.Z - P0->Y); };

				T.Wait(3);
				T.Shot(TEXT("01-start-screen"));
				T.Key(TEXT("Enter"));
				T.Wait(1);
				T.Expect(TEXT("keyboard: Enter starts the game"), [&T]() { return T.Game()->bStarted; });
				// Open lawn in Torshovdalen, facing along the slope.
				T.Do([&T]() { const FVector2D W = FHKData::Get().ToWorld(110, 40); T.Game()->Place(W.X, W.Y, 0); });
				T.Wait(.5);
				// W walks at 1.45 m/s, Shift + W runs at 3.6 m/s (game-main.js updateFoot).
				Mark(); HoldKeys({TEXT("KeyW")}, 1.0);
				T.Log([Moved, Held]() { return FString::Printf(TEXT("W for %.2f s of game time: %.2f m (%.2f m/s)"), *Held, Moved(), Moved() / *Held); });
				T.Expect(TEXT("keyboard: W walks at 1.45 m/s"), [Moved, Held]() { return FMath::Abs(Moved() / *Held - 1.45) < .1; });
				Mark(); HoldKeys({TEXT("ShiftLeft"), TEXT("KeyW")}, 1.0);
				T.Log([Moved, Held]() { return FString::Printf(TEXT("Shift + W for %.2f s of game time: %.2f m (%.2f m/s)"), *Held, Moved(), Moved() / *Held); });
				T.Expect(TEXT("keyboard: Shift + W runs at 3.6 m/s"), [Moved, Held]() { return FMath::Abs(Moved() / *Held - 3.6) < .25; });
				T.Key(TEXT("Space"), .05);
				T.Expect(TEXT("keyboard: Space jumps"), [&T]() { return T.Game()->Player.JumpY > .05 || T.Game()->Player.JumpV != 0; });
				T.Wait(1);
				T.Key(TEXT("KeyC"));
				T.Wait(.3);
				T.Expect(TEXT("keyboard: C crouches"), [&T]() { return T.Game()->Player.Stance == TEXT("crouch"); });
				T.Key(TEXT("KeyC"));
				T.Key(TEXT("KeyZ"));
				T.Wait(.3);
				T.Expect(TEXT("keyboard: Z lies prone"), [&T]() { return T.Game()->Player.Stance == TEXT("prone"); });
				T.Key(TEXT("KeyZ"));
				T.Wait(.3);
				T.Expect(TEXT("back on your feet"), [&T]() { return T.Game()->Player.Stance == TEXT("stand"); });
				// Mouse look: yaw -= dx * .0024 * mouse sensitivity.
				Mark();
				T.Do([In]() { if (auto I = In()) I->SimulateMouse(200, 0, -1, false); });
				T.Wait(.2);
				T.Expect(TEXT("mouse: 200 px to the right turns -0.48 rad at sensitivity 1"), [&T, P0]() { return FMath::Abs((T.Game()->Player.Yaw - P0->Z) + 200 * .0024 * T.Game()->Sensitivity.Mouse) < .01; });
				T.Key(TEXT("KeyM"));
				T.Wait(.5);
				T.Expect(TEXT("keyboard: M opens the map"), [&T]() { return T.Game()->Map.bOpen; });
				T.Key(TEXT("KeyM"));
				T.Wait(.3);
				T.Expect(TEXT("keyboard: M closes the map"), [&T]() { return !T.Game()->Map.bOpen; });
				// Pause menu.
				T.Key(TEXT("Escape"));
				T.Wait(.5);
				T.Expect(TEXT("keyboard: Escape pauses"), [&T]() { return T.Game()->bPaused; });
				T.Shot(TEXT("02-pause"));
				TSharedPtr<double> Vol = MakeShared<double>();
				T.Do([Vol]() { *Vol = FHKMusic::Get().GetVolume(); });
				T.Press(15);
				T.Expect(TEXT("controller: D-pad right raises the music volume by 0.1"), [Vol]() { return FMath::Abs(FHKMusic::Get().GetVolume() - FMath::Min(1.0, *Vol + .1)) < .001; });
				T.Press(14);
				T.Expect(TEXT("controller: D-pad left lowers it again"), [Vol]() { return FMath::Abs(FHKMusic::Get().GetVolume() - *Vol) < .001; });
				Click(TEXT("sensPad"), .52);
				T.Log([&T]() { return FString::Printf(TEXT("sliders: pad %.2f, menu zoom %.3f, content %.0f"), T.Game()->Sensitivity.Pad, T.Game()->PauseZoom, T.Game()->PauseContentH); });
				T.Expect(TEXT("mouse: controller sensitivity slider at 52 % = 1.70"), [&T]() { return FMath::Abs(T.Game()->Sensitivity.Pad - 1.7) < .001; });
				Click(TEXT("sensMouse"), .2);
				T.Expect(TEXT("mouse: mouse sensitivity slider at 20 % = 0.80"), [&T]() { return FMath::Abs(T.Game()->Sensitivity.Mouse - .8) < .001; });
				Click(TEXT("musicVol"), .31);
				T.Expect(TEXT("mouse: music volume slider at 31 % = 0.30"), []() { return FMath::Abs(FHKMusic::Get().GetVolume() - .3) < .001; });
				Click(TEXT("quality"), .5);
				T.Expect(TEXT("graphics: Balanced -> Sharp (100 %)"), [&T]() { return T.Game()->Quality == 2 && IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))->GetFloat() == 100; });
				Click(TEXT("quality"), .5);
				T.Expect(TEXT("graphics: Sharp -> Fast (65 %)"), [&T]() { return T.Game()->Quality == 0 && IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))->GetFloat() == 65; });
				Click(TEXT("quality"), .5);
				T.Expect(TEXT("graphics: Fast -> Balanced (75 %)"), [&T]() { return T.Game()->Quality == 1 && IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))->GetFloat() == 75; });
				T.Expect(TEXT("settings saved to settings.ini"), [SettingsFile]()
				{
					FString S;
					return FFileHelper::LoadFileToString(S, *SettingsFile) && S.Contains(TEXT("look-sensitivity-pad=1.7")) && S.Contains(TEXT("look-sensitivity-mouse=0.8")) && S.Contains(TEXT("quality=1")) && S.Contains(TEXT("music-volume=0.3"));
				});
				T.Shot(TEXT("03-settings-changed"));
				// The lobby section (only with -HKOnline; the test server from DefaultGame.ini).
				auto Net = [&T, bOnline]() -> const FHKNet* { AHKOnline* O = T.Game()->GetOnline(); return bOnline && O && O->Mp() ? &O->Mp()->Net() : nullptr; };
				T.Do([&T, Net]() { UE_LOG(LogTemp, Display, TEXT("HK-TEST online section: %s"), Net() ? TEXT("on (test server)") : TEXT("off, skipped")); });
				TSharedPtr<FString> Own = MakeShared<FString>();
				T.Do([&T, OldName]() { AHKOnline* O = T.Game()->GetOnline(); if (O && O->Mp()) *OldName = O->Mp()->Net().GetName(); });
				auto IfOnline = [&T, Net](TFunction<void()> Fn) { T.Do([Net, Fn]() { if (Net()) Fn(); }); };
				auto WaitOnline = [&T, Net](const FString& What, double MaxS, TFunction<bool(const FHKNet&)> Fn)
				{
					TSharedPtr<double> W = MakeShared<double>(0);
					T.Steps.Add([Net, W, MaxS, Fn](double Dt) { *W += Dt; return !Net() || Fn(*Net()) || *W > MaxS; });
					T.Do([&T, Net, What, Fn]() { if (Net()) { const bool bOk = Fn(*Net()); if (!bOk) T.Failures++; UE_LOG(LogTemp, Display, TEXT("HK-TEST %s %s  (%s)"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What, *Net()->StatusText()); } });
				};
				WaitOnline(TEXT("online: in your own lobby with a 4-character code"), 20, [](const FHKNet& N) { return N.Online() && N.GetOwnCode().Len() == 4 && N.GetCode() == N.GetOwnCode(); });
				IfOnline([Net, Own]() { *Own = Net()->GetOwnCode(); UE_LOG(LogTemp, Display, TEXT("HK-TEST own lobby code %s"), **Own); });
				T.Shot(TEXT("04-lobby-section"));
				// Controller code pad: triangle opens it at AAAA, up turns the first letter to B, cross joins BAAA.
				T.Press(3);
				T.Press(12);
				T.Shot(TEXT("05-code-pad"));
				T.Press(0);
				WaitOnline(TEXT("online: the code pad joined lobby BAAA"), 20, [](const FHKNet& N) { return N.Online() && N.GetCode() == TEXT("BAAA"); });
				T.Shot(TEXT("06-joined-by-code"));
				T.Press(2);
				WaitOnline(TEXT("online: square goes back to your own lobby"), 20, [Own](const FHKNet& N) { return N.Online() && N.GetCode() == *Own; });
				// Keyboard: type a code into the field (lower case is accepted) and press Enter.
				Click(TEXT("mpJoinCode"), .5);
				IfOnline([In]() { if (auto I = In()) { I->TypedText += TEXT("cdef"); I->TextKeys.Add(FName("Enter")); } });
				WaitOnline(TEXT("online: typing cdef + Enter joins lobby CDEF"), 20, [](const FHKNet& N) { return N.Online() && N.GetCode() == TEXT("CDEF"); });
				Click(TEXT("mpHome"), .5);
				WaitOnline(TEXT("online: the 'Back to my own lobby' button goes home"), 20, [Own](const FHKNet& N) { return N.Online() && N.GetCode() == *Own; });
				// Name field.
				Click(TEXT("mpName"), .5);
				IfOnline([In]() { if (auto I = In()) { I->TypedText += TEXT(" 2"); I->TextKeys.Add(FName("Enter")); } });
				T.Wait(.5);
				WaitOnline(TEXT("online: the name field adds to the name and Enter keeps it"), 5, [](const FHKNet& N) { return N.GetName().EndsWith(TEXT(" 2")); });
				// Microphone (a test tone with -HKVoiceTone).
				Click(TEXT("mpMic"), .1);
				T.Steps.Add([&T, bOnline, W = MakeShared<double>(0)](double Dt) { *W += Dt; AHKOnline* O = T.Game()->GetOnline(); return !bOnline || !O || !O->Mp() || O->Mp()->Mic() || *W > 15; });
				T.Do([&T, bOnline]() { AHKOnline* O = T.Game()->GetOnline(); if (bOnline && O && O->Mp()) { const bool bOk = O->Mp()->Mic(); if (!bOk) T.Failures++; UE_LOG(LogTemp, Display, TEXT("HK-TEST %s online: the voice checkbox turns the microphone on"), bOk ? TEXT("PASS") : TEXT("FAIL")); } });
				T.Shot(TEXT("07-mic-on"));
				Click(TEXT("mpMic"), .1);
				T.Wait(1);
				// Resume with the mouse.
				T.Log([&T]() { FVector2D P, S; AHKGame::MenuRect(TEXT("resume"), P, S); return FString::Printf(TEXT("resume button at (%.0f, %.0f) %.0fx%.0f, zoom %.3f"), P.X, P.Y, S.X, S.Y, T.Game()->PauseZoom); });
				Click(TEXT("resume"), .5);
				T.Wait(.3);
				T.Expect(TEXT("mouse: Resume closes the menu"), [&T]() { return !T.Game()->bPaused; });
				T.Do([&T, SettingsFile, Before, OldName]()
				{
					T.Game()->TestMenuMouse.Reset();
					// Put the player's own settings and name back (no settings file if there was none).
					const bool bOk = Before->IsSet() ? FFileHelper::SaveStringToFile(Before->GetValue(), *SettingsFile) : IFileManager::Get().Delete(*SettingsFile, false, true, true);
					AHKOnline* O = T.Game()->GetOnline();
					if (!OldName->IsEmpty() && O && O->Mp()) O->Mp()->Net().SetName(*OldName);
					UE_LOG(LogTemp, Display, TEXT("HK-TEST settings %s (%s), name back to '%s'"), Before->IsSet() ? TEXT("restored") : TEXT("file removed"), bOk ? TEXT("ok") : TEXT("failed"), **OldName);
				});
			});

			// The nine stair towers (own 17A first): in each, a flat door that is not ours is locked, as in the web.
			FHKTestScripts::All().Add(TEXT("stairwells"), [](AHKTestDriver& T)
			{
				T.Wait(3);
				T.Press(0);
				T.Wait(1);
				struct FDoor { int32 Tower = -1; FVector Centre = FVector::ZeroVector; };
				TSharedPtr<TArray<FDoor>> Locked = MakeShared<TArray<FDoor>>();
				T.Do([&T, Locked]()
				{
					AHKGame* G = T.Game();
					TSet<int32> Seen;
					for (TActorIterator<AHKRigActor> It(T.GetWorld()); It; ++It)
						for (UActorComponent* AC : It->GetComponents())
						{
							UPrimitiveComponent* C = Cast<UPrimitiveComponent>(AC);
							FHKNode* N = C ? It->NodeOfComponent(C) : nullptr;
							bool bLocked = false;
							for (FHKNode* P = N; P; P = P->Parent) { if (!P->DoorId.IsEmpty()) break; if (P->bLockedDoor) { bLocked = true; break; } }
							if (!bLocked) continue;
							const FVector W = HK::FromUE(C->Bounds.Origin);
							int32 Ti = -1; FVector L;
							if (!G->TowerAt(W.X, W.Z, W.Y - 1.0, Ti, L) || Seen.Contains(Ti)) continue;
							Seen.Add(Ti);
							Locked->Add({Ti, W});
						}
					Locked->Sort([](const FDoor& A, const FDoor& B) { return A.Tower < B.Tower; });
					UE_LOG(LogTemp, Display, TEXT("HK-TEST %d towers, a locked flat door found in %d of them"), G->Towers.Num(), Locked->Num());
				});
				T.Expect(TEXT("a locked flat door in each of the 9 stair towers"), [&T, Locked]() { return T.Game()->Towers.Num() == 9 && Locked->Num() == 9; });
				for (int32 k = 0; k < 9; k++)
				{
					// Stand 1 m from the door toward the middle of the stairwell (the game may push you out of a wall), then
					// keep looking at the door from wherever you stand.
					T.Do([&T, Locked, k]()
					{
						if (!Locked->IsValidIndex(k)) return;
						AHKGame* G = T.Game();
						const FDoor& D = (*Locked)[k];
						const FHKTower& Tw = G->Towers[D.Tower];
						const FVector L = Tw.Inverse.TransformPoint(D.Centre);
						FVector2D Dir(.1 - L.X, 8.0 - L.Z);
						Dir.Normalize();
						const FVector P = Tw.Matrix.TransformPoint(FVector(L.X + Dir.X, L.Y, L.Z + Dir.Y));
						G->Place(P.X, P.Z, 0, D.Centre.Y - G->Player.EyeNow);
					});
					TSharedPtr<double> Held = MakeShared<double>(0);
					T.Steps.Add([&T, Locked, k, Held](double Dt)
					{
						if (!Locked->IsValidIndex(k)) return true;
						AHKGame* G = T.Game();
						const FVector C = (*Locked)[k].Centre;
						const FVector E(G->Player.X, G->Player.Feet + G->Player.EyeNow, G->Player.Z);
						G->Player.Yaw = FMath::Atan2(-(C.X - E.X), -(C.Z - E.Z));
						G->Player.Pitch = FMath::Atan2(C.Y - E.Y, HK::Hypot(C.X - E.X, C.Z - E.Z));
						*Held += Dt;
						return *Held > .6;
					});
					T.Log([&T, k, Locked]()
					{
						AHKGame* G = T.Game();
						const FVector C = Locked->IsValidIndex(k) ? (*Locked)[k].Centre : FVector::ZeroVector;
						TArray<FHitResult> Hits;
						FCollisionQueryParams Q(SCENE_QUERY_STAT(HKTest), true);
						G->GetWorld()->LineTraceMultiByChannel(Hits, HK::ToUE(G->CamPos), HK::ToUE(G->CamPos + (C - G->CamPos) * 1.5), ECC_GameTraceChannel1, Q);
						FString Names;
						for (const FHitResult& H : Hits) Names += (H.GetComponent() ? H.GetComponent()->GetName() : FString(TEXT("?"))) + TEXT(" ");
						const AHKGame::FLookHit L = G->LookTarget();
						return FString::Printf(TEXT("tower %d: prompt '%s', door (%.2f, %.2f, %.2f), eye (%.2f, %.2f, %.2f), look hit %d locked %d, trace: %s"), k, *G->LastPromptText(), C.X, C.Y, C.Z, G->CamPos.X, G->CamPos.Y, G->CamPos.Z, L.bHit, L.bLocked, *Names);
					});
					T.Expect(FString::Printf(TEXT("tower %d%s: a stranger's flat door offers only '□ Try the door'"), k, k == 0 ? TEXT(" (17A)") : TEXT("")), [&T]() { return T.Game()->LastPromptText() == TEXT("□ Try the door"); });
					if (k == 1) T.Shot(TEXT("01-locked-door"));
					T.Press(2);
					T.Expect(FString::Printf(TEXT("tower %d: it is locked"), k), [&T]() { return T.Game()->LastNoticeText().StartsWith(TEXT("Locked")); });
				}
			});

			// The real microphone through the same capture the voice chat uses (levels only, nothing is kept).
			FHKTestScripts::All().Add(TEXT("mic"), [](AHKTestDriver& T)
			{
				struct FMic { TUniquePtr<Audio::FAudioCaptureSynth> C; double T = 0; int64 Samples = 0; double Energy = 0, Peak = 0; int32 Channels = 1; FString Name; bool bOpen = false; };
				TSharedPtr<FMic> M = MakeShared<FMic>();
				T.Wait(2);
				T.Do([M]()
				{
					M->C = MakeUnique<Audio::FAudioCaptureSynth>();
					Audio::FCaptureDeviceInfo Info;
					M->bOpen = M->C->GetDefaultCaptureDeviceInfo(Info) && M->C->OpenDefaultStream() && M->C->StartCapturing();
					M->Name = Info.DeviceName; M->Channels = FMath::Max(1, Info.InputChannels);
					UE_LOG(LogTemp, Display, TEXT("HK-TEST microphone '%s', %d Hz, %d channels, open %d, capturing %d"), *Info.DeviceName, Info.PreferredSampleRate, Info.InputChannels, M->bOpen, M->bOpen && M->C->IsCapturing());
				});
				T.Steps.Add([M](double Dt)
				{
					M->T += Dt;
					if (M->bOpen)
					{
						TArray<float> D;
						M->C->GetAudioData(D);
						M->Samples += D.Num();
						for (float V : D) { M->Energy += V * V; M->Peak = FMath::Max(M->Peak, (double)FMath::Abs(V)); }
					}
					return M->T > 5;
				});
				T.Do([&T, M]()
				{
					const double Rms = M->Samples ? FMath::Sqrt(M->Energy / M->Samples) : 0;
					UE_LOG(LogTemp, Display, TEXT("HK-TEST microphone over 5 s: %lld samples, RMS %.4f, peak %.4f"), M->Samples, Rms, M->Peak);
					if (M->C) { M->C->AbortCapturing(); M->C.Reset(); }
				});
				T.Expect(TEXT("the microphone delivers sound"), [M]() { return M->Samples > 0 && M->Peak > 0; });
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
				// The aquavit on the TV bench: 12 sips, each +30 s drunk.
				TSharedPtr<int32> Aq = MakeShared<int32>(-1);
				TSharedPtr<double> DrunkBefore = MakeShared<double>(0);
				T.Do([&T, Aq]()
				{
					AHKGame* G = T.Game();
					for (int32 i = 0; i < G->Drinks.Num(); i++) if (G->Drinks[i].Kind != TEXT("beer") && G->Drinks[i].Object) *Aq = i;
					UE_LOG(LogTemp, Display, TEXT("HK-TEST aquavit: drink %d, %d sips"), *Aq, *Aq >= 0 ? G->Drinks[*Aq].Sips : 0);
				});
				T.Expect(TEXT("the aquavit has 12 sips"), [&T, Aq]() { return *Aq >= 0 && T.Game()->Drinks[*Aq].Sips == 12; });
				T.Steps.Add([&T, Aq](double)
				{
					AHKGame* G = T.Game();
					if (*Aq < 0) return true;
					const FVector B = G->Drinks[*Aq].Object->GetWorldPosition();
					const FVector2D Dir = FVector2D(B.X - G->Player.X, B.Z - G->Player.Z);
					if (Dir.Size() > .9) { G->Player.Yaw = FMath::Atan2(-Dir.X, -Dir.Y); if (TSharedPtr<FHKInputProcessor> In = FHKInputProcessor::Get()) In->SimulatedPad.Axes[1] = -1; return false; }
					if (TSharedPtr<FHKInputProcessor> In = FHKInputProcessor::Get()) In->SimulatedPad.Axes[1] = 0;
					return true;
				});
				T.Do([&T, Aq, DrunkBefore]()
				{
					AHKGame* G = T.Game();
					if (*Aq < 0) return;
					const FVector B = G->Drinks[*Aq].Object->GetWorldPosition() + FVector(0, .1, 0);
					const FVector E = FVector(G->Player.X, G->Player.Feet + G->Player.EyeNow, G->Player.Z);
					G->Player.Yaw = FMath::Atan2(-(B.X - E.X), -(B.Z - E.Z));
					G->Player.Pitch = FMath::Atan2(B.Y - E.Y, HK::Hypot(B.X - E.X, B.Z - E.Z));
				});
				T.Wait(.5);
				T.Log([&T]() { return TEXT("prompt: ") + T.Game()->LastPromptText(); });
				T.Shot(TEXT("07-aquavit"));
				T.Do([&T, DrunkBefore]() { *DrunkBefore = T.Game()->Drunk; });
				// Drunk, you sway: keep the bottle in view while square goes down and up.
				TSharedPtr<double> Sip = MakeShared<double>(0);
				TSharedPtr<bool> SawPrompt = MakeShared<bool>(false);
				T.Steps.Add([&T, Aq, Sip, SawPrompt](double Dt)
				{
					if (*Sip > .05 && *Sip <= .15 && T.Game()->LastPromptText().StartsWith(TEXT("□ Take a sip of"))) *SawPrompt = true;
					AHKGame* G = T.Game();
					TSharedPtr<FHKInputProcessor> In = FHKInputProcessor::Get();
					*Sip += Dt;
					if (*Aq >= 0)
					{
						const FVector B = G->Drinks[*Aq].Object->GetWorldPosition() + FVector(0, .1, 0);
						const FVector E = FVector(G->Player.X, G->Player.Feet + G->Player.EyeNow, G->Player.Z);
						G->Player.Yaw = FMath::Atan2(-(B.X - E.X), -(B.Z - E.Z));
						G->Player.Pitch = FMath::Atan2(B.Y - E.Y, HK::Hypot(B.X - E.X, B.Z - E.Z));
					}
					if (In) { In->SimulatedPad.Buttons[2] = (*Sip > .15 && *Sip < .3) ? 1 : 0; In->SimulatedPad.Pressed[2] = In->SimulatedPad.Buttons[2] > .5; }
					return *Sip > .5;
				});
				T.Expect(TEXT("prompt to take a sip of the aquavit just before square"), [SawPrompt]() { return *SawPrompt; });
				T.Log([&T]() { return TEXT("notice: ") + T.Game()->LastNoticeText(); });
				T.Expect(TEXT("a sip: 11 sips left and +30 s drunk"), [&T, Aq, DrunkBefore]() { return *Aq >= 0 && T.Game()->Drinks[*Aq].Sips == 11 && T.Game()->Drunk - *DrunkBefore > 29; });
			});
		}
	} GRegister;
}
