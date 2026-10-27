// Scripted play-through "online" (-hktest=online): the real game in a lobby on the test server together with the web
// game (tools/net-test/web-client.mjs --game, started by tools/net-test/run-live.ps1 -Game, which also passes
// -HKOnline -HKServer=... -HKLobby=... -HKVoiceTone). Both sides follow the same script and hand over with chat lines:
//   A  chat both ways, a door opened by each side, a log on the fire from each side (shared level), both walk
//   B  the web player drives a car; this game rides along as passenger through the game's own prompt (□ Ride along)
//   C  this game drives the motorcycle (prompt □ + simulated R2); the web player rides pillion
//   D  voice: this game's microphone (a 440 Hz test tone) and the web's fake microphone hear each other
// Screenshots go to the -hkout folder; results are HK-TEST PASS/FAIL lines in the log.
#include "HKTestDriver.h"
#include "Haarklous.h"
#include "HKData.h"
#include "HKGame.h"
#include "HKInput.h"
#include "HKMath.h"
#include "HKMultiplayer.h"
#include "HKOnlineGame.h"
#include "HKVehicle.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"

namespace
{
	AHKOnline* FindOnline(AHKTestDriver& T)
	{
		for (TActorIterator<AHKOnline> It(T.GetWorld()); It; ++It) return *It;
		return nullptr;
	}
	FHKMultiplayer* MpOf(AHKTestDriver& T) { AHKOnline* O = FindOnline(T); return O ? O->Mp() : nullptr; }
	int32 WebId(AHKTestDriver& T)
	{
		if (FHKMultiplayer* M = MpOf(T)) for (const FHKNetPlayer& P : M->Net().GetPlayers()) if (P.Name == TEXT("Web tester")) return P.Id;
		return 0;
	}
	const FHKRemote* Web(AHKTestDriver& T) { FHKMultiplayer* M = MpOf(T); return M ? M->Remote(WebId(T)) : nullptr; }
	// The newest chat line that starts with Prefix ('' = none).
	FString ChatLine(AHKTestDriver& T, const FString& Prefix)
	{
		const TArray<AHKGame::FChatLine>& Log = T.Game()->ChatLog;
		for (int32 i = Log.Num() - 1; i >= 0; i--) if (Log[i].Text.StartsWith(Prefix)) return Log[i].Text;
		return FString();
	}
	bool ChatHas(AHKTestDriver& T, const FString& Prefix) { return !ChatLine(T, Prefix).IsEmpty(); }
	void Say(AHKTestDriver& T, const FString& Text) { T.Do([&T, Text]() { if (T.Game()->SendChat) T.Game()->SendChat(Text); }); }
	double Toward(double X0, double Z0, double X1, double Z1) { return FMath::Atan2(-(X1 - X0), -(Z1 - Z0)); }

	// A step that waits for a condition (at most Seconds), then logs PASS or FAIL like Expect(). Note: what to log.
	void WaitFor(AHKTestDriver& T, const FString& What, double Seconds, TFunction<bool()> Fn, TFunction<FString()> Note = nullptr, TFunction<void()> EachFrame = nullptr)
	{
		TSharedPtr<double> Start = MakeShared<double>(-1);
		T.Steps.Add([&T, What, Seconds, Fn, Note, EachFrame, Start](double)
		{
			const double Now = FPlatformTime::Seconds();
			if (*Start < 0) *Start = Now;
			if (EachFrame) EachFrame();
			const bool bOk = Fn();
			if (!bOk && Now - *Start < Seconds) return false;
			if (!bOk) T.Failures++;
			UE_LOG(LogHaarklous, Display, TEXT("HK-TEST %s %s (%.1f s) %s  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What, Now - *Start, Note ? *Note() : TEXT(""), *T.P());
			return true;
		});
	}
	// Turns the player (on foot) toward a point.
	void Face(AHKTestDriver& T, const FVector& P)
	{
		AHKGame* G = T.Game();
		G->Player.Yaw = Toward(G->Player.X, G->Player.Z, P.X, P.Z);
		G->Player.Pitch = -.08;
	}
	// A fixed camera at From looking at To (web frame).
	void Look(AHKTestDriver& T, const FVector& From, const FVector& To)
	{
		AHKGame* G = T.Game();
		const FVector D = To - From;
		G->bCameraOverride = true;
		G->OverridePos = From;
		G->OverrideQuat = HK::EulerToQuat(FMath::Atan2(D.Y, HK::Hypot(D.X, D.Z)), Toward(From.X, From.Z, To.X, To.Z), 0, TEXT("YXZ"));
		G->UpdateCamera();
	}
	// A free spot beside a vehicle (its right side, clear of the body).
	FVector2D Beside(const FHKVehicle* V, double Extra)
	{
		const FVector2D F = V->Forward(), R(-F.Y, F.X);
		return FVector2D(V->X, V->Z) + R * (V->Width / 2 + Extra);
	}
	// Presses a button every 1.2 s (held 0.15 s) until the condition holds (e.g. □ to get out once the vehicle stopped).
	void PressUntil(AHKTestDriver& T, const FString& What, int32 Button, double Seconds, TFunction<bool()> Fn)
	{
		TSharedPtr<double> Start = MakeShared<double>(-1);
		T.Steps.Add([&T, What, Button, Seconds, Fn, Start](double)
		{
			FHKPadState Dummy;
			TSharedPtr<FHKInputProcessor> In = FHKInputProcessor::Get();
			FHKPadState& Pad = In ? In->SimulatedPad : Dummy;
			const double Now = FPlatformTime::Seconds();
			if (*Start < 0) *Start = Now;
			const double Phase = FMath::Fmod(Now - *Start, 1.2);
			const bool bOk = Fn();
			const bool bDown = !bOk && Phase > .3 && Phase < .45;
			Pad.Buttons[Button] = bDown ? 1 : 0; Pad.Pressed[Button] = bDown;
			if (!bOk && Now - *Start < Seconds) return false;
			Pad.Buttons[Button] = 0; Pad.Pressed[Button] = false;
			if (!bOk) T.Failures++;
			UE_LOG(LogHaarklous, Display, TEXT("HK-TEST %s %s (%.1f s)  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What, Now - *Start, *T.P());
			return true;
		});
	}
	FVector AnchorWorld(FHKVehicle* V, const FString& Seat)
	{
		const FHKHipAnchor A = V->HipAnchor(Seat);
		if (!A.bValid || !A.Obj) return FVector(HK::Big, HK::Big, HK::Big);
		A.Obj->UpdateWorldMatrix(true, false);
		return A.Obj->LocalToWorld(A.Pos);
	}

	struct FRun
	{
		int32 Car = -1;
		FVector CarStart = FVector::ZeroVector, BikeStart = FVector::ZeroVector;
		double MaxWebSpeed = 0, MaxLevel = 0, WebWalked = 0;
		bool bWalkStart = false;
		FVector WebWalk0 = FVector::ZeroVector;
	};

	struct FRegisterOnline
	{
		FRegisterOnline()
		{
			FHKTestScripts::All().Add(TEXT("online"), [](AHKTestDriver& T)
			{
				TSharedPtr<FRun> S = MakeShared<FRun>();
				T.Do([&T]() { if (!FindOnline(T)) { AHKOnline::Setup(T.Game()); UE_LOG(LogHaarklous, Display, TEXT("HK-TEST online module set up by the test")); } });
				T.Do([&T]() { if (FHKMultiplayer* M = MpOf(T)) M->Net().SetName(TEXT("Unreal tester")); });
				T.Wait(3);
				T.Press(0);   // start: begin() -> mp.net.connect(?lobby) or home()
				T.Wait(1);
				T.Do([&T]() { if (FHKMultiplayer* M = MpOf(T)) if (M->Net().GetStatus() == TEXT("offline")) M->Net().Begin(); });
				WaitFor(T, TEXT("online in the test lobby"), 60, [&T]() { FHKMultiplayer* M = MpOf(T); return M && M->Net().Online(); }, [&T]() { return MpOf(T)->Net().StatusText(); });
				WaitFor(T, TEXT("the web player is in the lobby"), 300, [&T]() { return WebId(T) != 0; });

				// ---- A: chat, doors, fire, walking ----
				T.Do([&T]() { T.Game()->Place(-1, 32.5, Toward(-1, 32.5, -4, 30)); });
				Say(T, TEXT("hello from the game"));
				T.Do([&T]()
				{
					AHKGame* G = T.Game();
					if (FHKDoor* D = G->Doors.FindByPredicate([](const FHKDoor& X) { return X.Id == TEXT("W05-0"); })) if (!D->bOpen && !D->bAnim) G->ToggleDoor(*D);
					G->AddLog();   // the game's own path: throw a log, mp.addWood()
				});
				WaitFor(T, TEXT("chat: the web player's line arrived"), 90, [&T]() { return ChatHas(T, TEXT("hello from web")); });
				WaitFor(T, TEXT("door: W09-0 opened by the web player"), 60, [&T]()
				{
					const FHKDoor* D = T.Game()->Doors.FindByPredicate([](const FHKDoor& X) { return X.Id == TEXT("W09-0"); });
					return D && D->bOpen;
				});
				// (The headless web page runs its game at about a third of real time next to this game, so it walks slowly.)
				WaitFor(T, TEXT("the web player walks (avatar moving)"), 180, [&T, S]()
				{
					const FHKRemote* R = Web(T);
					if (!R || !ChatHas(T, TEXT("web-walk-start"))) return false;
					if (!S->bWalkStart) { S->bWalkStart = true; S->WebWalk0 = R->Pos; }
					S->MaxWebSpeed = FMath::Max(S->MaxWebSpeed, R->Speed);
					S->WebWalked = HK::Hypot(R->Pos.X - S->WebWalk0.X, R->Pos.Z - S->WebWalk0.Z);
					return S->WebWalked > 2 && R->Speed > .1 && R->Av->Group()->bVisible;
				}, [S]() { return FString::Printf(TEXT("walked %.1f m, speed up to %.2f m/s"), S->WebWalked, S->MaxWebSpeed); }, [&T]() { if (const FHKRemote* R = Web(T)) Face(T, R->Pos); });
				T.Shot(TEXT("A1-game-sees-web-walking"));
				Say(T, TEXT("game-walk-start"));
				// Walk to and fro (left stick forward, turning round every 2.5 s) until the web player has seen it.
				{
					TSharedPtr<double> Start = MakeShared<double>(-1);
					T.Steps.Add([&T, Start](double)
					{
						FHKPadState Dummy;
						TSharedPtr<FHKInputProcessor> In = FHKInputProcessor::Get();
						FHKPadState& Pad = In ? In->SimulatedPad : Dummy;
						const double Now = FPlatformTime::Seconds();
						AHKGame* G = T.Game();
						if (*Start < 0) { *Start = Now; G->Player.Yaw = Toward(G->Player.X, G->Player.Z, 3, 37); }
						const int32 Leg = (int32)((Now - *Start) / 2.5);
						G->Player.Yaw = Leg % 2 ? Toward(1.5, 35, -1, 32.5) : Toward(-1, 32.5, 1.5, 35);
						Pad.Axes[1] = -1;
						const bool bSeen = ChatHas(T, TEXT("web-saw-game-walking"));
						if (!bSeen && Now - *Start < 90) return false;
						Pad.Axes[1] = 0;
						if (!bSeen) T.Failures++;
						UE_LOG(LogHaarklous, Display, TEXT("HK-TEST %s the web player saw this game walk (%.1f s)  %s"), bSeen ? TEXT("PASS") : TEXT("FAIL"), Now - *Start, *T.P());
						return true;
					});
				}
				Say(T, TEXT("game-walk-end"));
				WaitFor(T, TEXT("fire: same level on both sides after a log from each"), 90, [&T]()
				{
					// The web reports "fire-web <level> <serverNow>"; the level decays 1 per 90 s.
					TArray<FString> W;
					ChatLine(T, TEXT("fire-web ")).ParseIntoArrayWS(W);
					FHKMultiplayer* M = MpOf(T);
					if (W.Num() < 3 || !M) return false;
					const double Expect = FMath::Max(0.0, FCString::Atod(*W[1]) - (M->Net().ServerNow() - FCString::Atod(*W[2])) / 90000);
					return FMath::Abs(M->FireLevel() - Expect) < .02;
				}, [&T]() { return FString::Printf(TEXT("game %.3f, %s"), T.Game()->FireLevel(), *ChatLine(T, TEXT("fire-web "))); });

				// ---- B: the web player drives a car, this game rides along ----
				WaitFor(T, TEXT("the web player drives a car"), 300, [&T, S]()
				{
					TArray<FString> W;
					ChatLine(T, TEXT("web-drives ")).ParseIntoArrayWS(W);
					if (W.Num() < 2) return false;
					S->Car = FCString::Atoi(*W[1]);
					return T.Game()->VehicleList.IsValidIndex(S->Car) && T.Game()->Occupant && T.Game()->Occupant(S->Car, TEXT("d")) == WebId(T);
				}, [S]() { return FString::Printf(TEXT("vehicle %d"), S->Car); });
				T.Do([&T, S]()
				{
					FHKVehicle* V = T.Game()->VehicleList[S->Car];
					const FVector2D P = Beside(V, 1.0);
					T.Game()->Place(P.X, P.Y, Toward(P.X, P.Y, V->X, V->Z));
				});
				WaitFor(T, TEXT("prompt '□ Ride along with Web tester'"), 20, [&T]() { return T.Game()->LastPromptText().Contains(TEXT("Ride along with Web tester")); },
					[&T]() { return T.Game()->LastPromptText(); });
				T.Press(2);   // □: Interact() -> TryRide() -> mp.claim(i, 'p') -> EnterPassenger()
				WaitFor(T, TEXT("the lobby gave this game the passenger seat"), 15, [&T, S]()
				{
					AHKGame* G = T.Game();
					FHKMultiplayer* M = MpOf(T);
					return G->Player.Mode == TEXT("passenger") && G->Player.Vehicle == G->VehicleList[S->Car] && M && M->Occupant(S->Car, TEXT("p")) == M->Net().You();
				}, [&T]() { return T.Game()->Player.Mode; });
				T.Do([&T, S]() { FHKVehicle* V = T.Game()->VehicleList[S->Car]; S->CarStart = FVector(V->X, V->Y, V->Z); });
				Say(T, TEXT("unreal-rides"));
				// Look at the driver while the web player drives on.
				auto LookAtDriver = [&T]()
				{
					AHKGame* G = T.Game();
					const FHKRemote* R = Web(T);
					if (!R || !G->Player.Vehicle) return;
					const FVector L = G->Player.Vehicle->Rig.Root->WorldToLocal(R->Pos + FVector(0, .6, 0));
					G->Player.LookYaw = .7 * FMath::Atan2(-L.X, -L.Z);   // most of the way: the driver and the road ahead
					G->Player.LookPitch = -.1;
				};
				WaitFor(T, TEXT("the camera rides with the web-driven car"), 240, [&T, S]()
				{
					AHKGame* G = T.Game();
					FHKVehicle* V = G->VehicleList[S->Car];
					return ChatHas(T, TEXT("web-drove")) && HK::Hypot(V->X - S->CarStart.X, V->Z - S->CarStart.Z) > 3 && HK::Hypot(G->Player.X - V->X, G->Player.Z - V->Z) < .5 && G->Player.Mode == TEXT("passenger");
				}, [&T, S]()
				{
					FHKVehicle* V = T.Game()->VehicleList[S->Car];
					return FString::Printf(TEXT("car moved %.1f m, camera at (%.2f, %.2f) car at (%.2f, %.2f)"), HK::Hypot(V->X - S->CarStart.X, V->Z - S->CarStart.Z), T.Game()->CamPos.X, T.Game()->CamPos.Z, V->X, V->Z);
				}, LookAtDriver);
				T.Steps.Add([LookAtDriver](double) { LookAtDriver(); return true; });
				T.Shot(TEXT("B1-game-passenger-seat-web-driving"));
				T.Do([&T]() { T.Game()->Player.LookYaw = 0; });
				T.Wait(.5);
				PressUntil(T, TEXT("got out of the car (□ once it stopped)"), 2, 40, [&T]() { return T.Game()->Player.Mode == TEXT("foot"); });
				Say(T, TEXT("unreal-left-car"));

				// ---- C: this game drives the motorcycle, the web player rides pillion ----
				WaitFor(T, TEXT("the web player left the car"), 120, [&T]() { return ChatHas(T, TEXT("web-left-car")); });
				T.Do([&T]()
				{
					FHKVehicle* V = T.Game()->VehicleList[5];
					const FVector2D P = Beside(V, .8);
					T.Game()->Place(P.X, P.Y, Toward(P.X, P.Y, V->X, V->Z));
				});
				WaitFor(T, TEXT("prompt to get on the motorcycle"), 20, [&T]() { return T.Game()->LastPromptText().StartsWith(TEXT("□")) && T.Game()->LastPromptText().Contains(T.Game()->VehicleList[5]->Name); },
					[&T]() { return T.Game()->LastPromptText(); });
				T.Press(2);   // □: TryDrive() -> mp.claim(5, 'd') -> EnterVehicle()
				WaitFor(T, TEXT("this game drives the motorcycle (driver seat from the lobby)"), 15, [&T]()
				{
					AHKGame* G = T.Game();
					FHKMultiplayer* M = MpOf(T);
					return G->Player.Mode == TEXT("vehicle") && G->Player.Vehicle == G->VehicleList[5] && M && M->Occupant(5, TEXT("d")) == M->Net().You();
				});
				T.Do([&T, S]() { FHKVehicle* V = T.Game()->VehicleList[5]; S->BikeStart = FVector(V->X, V->Y, V->Z); });
				Say(T, TEXT("unreal-drives 5"));
				WaitFor(T, TEXT("the web player rides pillion (seat, state and avatar at the anchor)"), 150, [&T]()
				{
					FHKMultiplayer* M = MpOf(T);
					const FHKRemote* R = Web(T);
					FHKVehicle* V = T.Game()->VehicleList[5];
					return M && R && M->Occupant(5, TEXT("p")) == WebId(T) && R->bHasSt && R->St.V == 5 && R->St.S == TEXT("p") && FVector::Distance(R->Av->Group()->Position, AnchorWorld(V, TEXT("p"))) < .05;
				}, [&T]()
				{
					const FHKRemote* R = Web(T);
					return R ? FString::Printf(TEXT("avatar %.3f m from the pillion anchor"), FVector::Distance(R->Av->Group()->Position, AnchorWorld(T.Game()->VehicleList[5], TEXT("p")))) : FString();
				});
				T.Steps.Add([&T](double)
				{
					FHKVehicle* V = T.Game()->VehicleList[5];
					const FVector2D P = Beside(V, 2.6);
					Look(T, FVector(P.X, V->Y + 1.3, P.Y), FVector(V->X, V->Y + .8, V->Z));
					return true;
				});
				T.Shot(TEXT("C1-game-sees-web-on-pillion"));
				T.Do([&T]() { T.Game()->bCameraOverride = false; });
				T.Hold(7, .55);   // R2 throttle
				T.Wait(2.5);
				T.Hold(7, 0);
				T.Hold(6, 1);     // L2 brake
				T.Wait(1.5);
				WaitFor(T, TEXT("the motorcycle moved and stopped"), 15, [&T, S]()
				{
					FHKVehicle* V = T.Game()->VehicleList[5];
					return FMath::Abs(V->Speed) < .5 && HK::Hypot(V->X - S->BikeStart.X, V->Z - S->BikeStart.Z) > 3;
				}, [&T, S]() { FHKVehicle* V = T.Game()->VehicleList[5]; return FString::Printf(TEXT("moved %.1f m, speed %.2f"), HK::Hypot(V->X - S->BikeStart.X, V->Z - S->BikeStart.Z), V->Speed); });
				T.Hold(6, 0);
				T.Steps.Add([&T](double)
				{
					FHKVehicle* V = T.Game()->VehicleList[5];
					const FVector2D P = Beside(V, 2.6);
					Look(T, FVector(P.X, V->Y + 1.3, P.Y), FVector(V->X, V->Y + .8, V->Z));
					return true;
				});
				T.Shot(TEXT("C2-game-sees-web-on-pillion-after-ride"));
				T.Do([&T]() { T.Game()->bCameraOverride = false; });
				Say(T, TEXT("unreal-drove"));
				WaitFor(T, TEXT("the web player got off"), 180, [&T]() { return ChatHas(T, TEXT("web-left-bike")); });
				PressUntil(T, TEXT("got off the motorcycle (□ once it stopped)"), 2, 30, [&T]() { return T.Game()->Player.Mode == TEXT("foot"); });

				// ---- D: voice (this game's microphone = 440 Hz test tone with -HKVoiceTone) ----
				T.Do([&T]() { if (AHKOnline* O = FindOnline(T)) if (!O->Mp()->Mic()) O->ToggleMic(); });   // the pause menu's microphone switch
				WaitFor(T, TEXT("microphone on"), 30, [&T]() { FHKMultiplayer* M = MpOf(T); return M && M->Mic(); });
				Say(T, TEXT("game-mic-on"));
				WaitFor(T, TEXT("voice: hears the web player's microphone"), 90, [&T, S]()
				{
					FHKMultiplayer* M = MpOf(T);
					FString State;
					const double L = M ? M->Voice().PeerLevel(WebId(T), &State) : 0;
					S->MaxLevel = FMath::Max(S->MaxLevel, L);
					return State == TEXT("connected") && L > .01;
				}, [S]() { return FString::Printf(TEXT("RMS %.3f"), S->MaxLevel); });
				T.Do([&T, S]() { if (T.Game()->SendChat) T.Game()->SendChat(FString::Printf(TEXT("voice-heard-web %.3f"), S->MaxLevel)); });
				WaitFor(T, TEXT("voice: the web player hears this game"), 90, [&T]() { return ChatHas(T, TEXT("voice-heard-game")); }, [&T]() { return ChatLine(T, TEXT("voice-heard-game")); });
				T.MeasureFps(TEXT("online, one remote player"), 8);
				Say(T, TEXT("done-game"));
				WaitFor(T, TEXT("the web client finished"), 90, [&T]() { return ChatHas(T, TEXT("done-web")); });
			});
		}
	} GRegisterOnline;
}
