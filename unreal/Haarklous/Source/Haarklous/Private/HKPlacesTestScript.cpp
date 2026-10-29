// Scripted play-through "places" (-hktest=places): what the web game added on 29 Oct 2026 and the Unreal issues ported
// from it, with the checks each issue lists. Fixed 1/60 s frames.
//   #10 the construction site 96 m south / 72 m west and the paintball course in the old site area (gate, fence, lobby)
//   #3  HODET N.N. (where it stands, walking into it, its steps)       #2  the street lamps (count, a solid post)
//   #11 Muselunden DiscGolfPark (12 holes, the prompt and the HUD)     #9  the bike's first ride: parked cars block it
//   #7  petanque ends once at 13 (and pays 150 kr once)                #6  the stashes and their three boosts
//   #5  the shops: doors, taking items, paying, beer hours, a declined card, the alarm, the bag, the bottle return
//   #14 the distant land from the helicopter (screenshots)
#include "HKTestDriver.h"
#include "Haarklous.h"
#include "HKData.h"
#include "HKGame.h"
#include "HKInput.h"
#include "HKMath.h"
#include "HKMinigames.h"
#include "HKPaintball.h"
#include "HKShops.h"
#include "HKStash.h"
#include "HKVehicles.h"
#include "Dom/JsonObject.h"

namespace HKPlacesTest
{
	const double Fdt = 1.0 / 60;
	FHKData& D() { return FHKData::Get(); }
	FVector2D W(double X, double S) { return FHKData::Get().ToWorld(X, S); }
	FVector2D L(double X, double Z) { return FHKData::Get().ToLocal(X, Z); }
	void GameWait(AHKTestDriver& T, double Seconds)
	{
		TSharedPtr<int32> N = MakeShared<int32>(0);
		const int32 Frames = FMath::Max(1, FMath::RoundToInt(Seconds / Fdt));
		T.Steps.Add([N, Frames](double) { return ++*N >= Frames; });
	}
	// Put the player on foot at the local point (x, s), facing world yaw.
	void At(AHKTestDriver& T, double X, double S, double Yaw)
	{
		T.Do([&T, X, S, Yaw]() { AHKGame* G = T.Game(); if (G->Player.Mode != TEXT("foot")) G->ExitVehicle(); const FVector2D P = W(X, S); G->Place(P.X, P.Y, Yaw); G->Player.Pitch = 0; });
		GameWait(T, .3);
	}
	// Walk forward with the left stick for Seconds of game time.
	void Walk(AHKTestDriver& T, double Seconds) { T.Stick(0, -1, Seconds); }
	// Look at a world point from the eye.
	void LookAt(AHKGame* G, const FVector& P)
	{
		const FVector E(G->Player.X, G->Player.Feet + G->Player.EyeNow, G->Player.Z);
		G->Player.Yaw = std::atan2(-(P.X - E.X), -(P.Z - E.Z));
		G->Player.Pitch = std::atan2(P.Y - E.Y, FVector2D(P.X - E.X, P.Z - E.Z).Size());
	}
	FString PromptOf(AHKGame* G) { return G->FindPrompt().Text; }
	FDateTime Day(int32 DayOfWeekMon0, int32 Hour) { return FDateTime(2026, 10, 26) + FTimespan::FromDays(DayOfWeekMon0) + FTimespan::FromHours(Hour); }   // 26 Oct 2026 is a Monday
	// Walk forward (W held) for Seconds of game time (the game runs fixed 1/60 s frames, whatever the real frame rate).
	void WalkGame(AHKTestDriver& T, double Seconds)
	{
		T.Do([]() { if (auto I = FHKInputProcessor::Get()) I->SimulateKey(TEXT("KeyW"), true); });
		GameWait(T, Seconds);
		T.Do([]() { if (auto I = FHKInputProcessor::Get()) I->SimulateKey(TEXT("KeyW"), false); });
	}

	struct FRegister
	{
		FRegister()
		{
			FHKTestScripts::All().Add(TEXT("places"), [](AHKTestDriver& T)
			{
				T.Wait(4);
				T.Press(0);   // start
				T.Wait(2);
				T.Do([&T]() { AHKGame* G = T.Game(); G->FixedDt = Fdt; if (G->Shops) { G->Shops->Wallet.SavePath.Empty(); G->Shops->Wallet.Reset(500); } });

				// ---------------- #10: the site and the paintball course moved ----------------
				T.Expect(TEXT("#10 site machines inside the new fence (x -158..-18, s 371..461)"), [&T]()
				{
					AHKGame* G = T.Game();
					int32 N = 0;
					for (int32 i = 41; i < G->VehicleList.Num(); i++)   // the site machines come after the 41 street vehicles
					{
						const FHKVehicle* V = G->VehicleList[i];
						const FVector2D P = L(V->X, V->Z);
						if (P.X < -158 || P.X > -18 || P.Y < 371 || P.Y > 461) return false;
						N++;
					}
					return N == 7;
				});
				T.Log([&T]() { FString S; const TArray<FHKVehicle*>& Vl = T.Game()->VehicleList; for (int32 i = 41; i < Vl.Num(); i++) { const FVector2D P = L(Vl[i]->X, Vl[i]->Z); S += FString::Printf(TEXT("%s (%.0f, %.0f) "), *Vl[i]->Kind, P.X, P.Y); } return TEXT("#10 machines: ") + S; });
				T.Expect(TEXT("#10 41 street vehicles and 7 machines"), [&T]() { return T.Game()->VehicleList.Num() == 48; });
				// In through the north gate at x -120 (walking south, +s).
				At(T, -120, 364, 0);
				T.Do([&T]() { AHKGame* G = T.Game(); const FVector2D A = W(-120, 364), B = W(-120, 380); G->Player.Yaw = std::atan2(-(B.X - A.X), -(B.Y - A.Y)); });
				Walk(T, 5);
				T.Expect(TEXT("#10 walking in through the north gate at x -120"), [&T]() { return L(T.Game()->Player.X, T.Game()->Player.Z).Y > 374; });
				// 20 m east of it the fence stops you (web: s 370.74).
				At(T, -100, 364, 0);
				T.Do([&T]() { AHKGame* G = T.Game(); const FVector2D A = W(-100, 364), B = W(-100, 380); G->Player.Yaw = std::atan2(-(B.X - A.X), -(B.Y - A.Y)); });
				Walk(T, 5);
				T.Log([&T]() { return FString::Printf(TEXT("#10 20 m east of the gate the fence stops you at s %.2f (web 370.74)"), L(T.Game()->Player.X, T.Game()->Player.Z).Y); });
				T.Expect(TEXT("#10 the fence stops you 20 m east of the gate"), [&T]() { const double S = L(T.Game()->Player.X, T.Game()->Player.Z).Y; return S > 369.5 && S < 371.6; });
				T.Do([&T]() { AHKGame* G = T.Game(); const FVector2D A = W(-110, 350), B = W(-120, 400); G->Place(A.X, A.Y, std::atan2(-(B.X - A.X), -(B.Y - A.Y))); G->Player.Pitch = -.15; });
				GameWait(T, .5);
				T.Shot(TEXT("01-site-new-place"));
				// The old site area is park again: its trees are back.
				T.Expect(TEXT("#10 the old site area has its trees again"), []()
				{
					int32 N = 0;
					for (const FHKData::FTree& Tr : D().AllTrees) if (Tr.X > -88 && Tr.X < 56 && Tr.S > 272 && Tr.S < 368 && !(Tr.X > -73 && Tr.X < -7 && Tr.S > 295 && Tr.S < 345)) N++;
					return N > 5;
				});
				// The paintball lobby (x -61..-33, s 339..356) and the yard floor (-27.05 m).
				At(T, -47, 349, 0);
				GameWait(T, 1);
				T.Expect(TEXT("#10 in the paintball lobby"), [&T]() { return T.Game()->Paintball && T.Game()->Paintball->InLobbyNow(); });
				T.Expect(TEXT("#10 the yard floor is at -27.05 m"), []() { const FVector2D P = W(-40, 318); return FMath::Abs(D().GroundWorld(P.X, P.Y) + 27.05) < .02; });
				T.Shot(TEXT("02-paintball-lobby"));

				// ---------------- #3: HODET N.N. ----------------
				T.Expect(TEXT("#3 HODET N.N. at local (9.1, 275.5)"), []() { const TSharedPtr<FJsonObject> A = HKJson::Obj(HKJson::Obj(D().Game, TEXT("sculpture")), TEXT("at")); return FMath::Abs(HKJson::Num(A, TEXT("x")) - 9.1) < .1 && FMath::Abs(HKJson::Num(A, TEXT("s")) - 275.5) < .1; });
				// Walk at it from 8 m south (+s): you stop 2.43 m from its centre.
				At(T, 9.1, 283.5, 0);
				T.Do([&T]() { AHKGame* G = T.Game(); const FVector2D A = W(9.1, 283.5), B = W(9.1, 275.5); G->Player.Yaw = std::atan2(-(B.X - A.X), -(B.Y - A.Y)); });
				Walk(T, 4);
				T.Log([&T]() { const FVector2D P = L(T.Game()->Player.X, T.Game()->Player.Z); return FString::Printf(TEXT("#3 walking at the sculpture from the south: stopped %.2f m from its centre (web 2.43)"), FVector2D(P.X - 9.1, P.Y - 275.5).Size()); });
				T.Expect(TEXT("#3 the bronze is solid (stop about 2.43 m from the centre)"), [&T]() { const FVector2D P = L(T.Game()->Player.X, T.Game()->Player.Z); return FMath::Abs(FVector2D(P.X - 9.1, P.Y - 275.5).Size() - 2.43) < .15; });
				// From 16 m south-south-west: the face toward you, the steps on the right.
				T.Do([&T]() { AHKGame* G = T.Game(); const FVector2D A = W(9.1 - 6, 275.5 + 14.8), B = W(9.1, 275.5); G->Place(A.X, A.Y, std::atan2(-(B.X - A.X), -(B.Y - A.Y))); G->Player.Pitch = .12; });
				GameWait(T, .5);
				T.Shot(TEXT("03-sculpture-from-ssw"));
				// Stand on a step: feet = the step's top.
				T.Do([&T]() { AHKGame* G = T.Game(); const double Yaw = HKJson::Num(HKJson::Obj(D().Game, TEXT("sculpture")), TEXT("yaw")); const FVector2D C = W(9.1, 275.5);
					// model +x is east: toW(cos(a) r, sin(a) r) at a = 0, r = 6.2 (the inner row's middle block)
					const double X = C.X + 6.2 * FMath::Cos(Yaw), Z = C.Y - 6.2 * FMath::Sin(Yaw); G->Place(X, Z, G->Player.Yaw); });
				GameWait(T, .5);
				T.Log([&T]() { AHKGame* G = T.Game(); return FString::Printf(TEXT("#3 on a step: feet %.2f, ground %.2f (web -18.67 / -19.01)"), G->Player.Feet, D().HeightWorld(G->Player.X, G->Player.Z)); });
				T.Expect(TEXT("#3 standing on a step (feet 0.34 m above the ground)"), [&T]() { AHKGame* G = T.Game(); return FMath::Abs(G->Player.Feet - D().HeightWorld(G->Player.X, G->Player.Z) - .34) < .08; });

				// ---------------- #2: street lamps ----------------
				T.Expect(TEXT("#2 359 solid lamp posts in the walk world"), []() { int32 N = 0; for (const FHKObstacle& O : D().WalkWorld.Obstacles) if (O.Kind == TEXT("lamp")) N++; return N == 359; });
				// Walk into the lamp nearest to home: you stop about 0.32 m from the post.
				TSharedPtr<FVector2D> Lamp = MakeShared<FVector2D>();
				T.Do([Lamp]() { const TArray<double> Lm = HKJson::Nums(D().D, TEXT("lamps")); double Best = 1e9; for (int32 i = 0; i + 1 < Lm.Num(); i += 2) { const double R = FVector2D(Lm[i], Lm[i + 1]).Size(); if (R > 25 && R < Best) { Best = R; *Lamp = FVector2D(Lm[i], Lm[i + 1]); } } });
				T.Do([&T, Lamp]() { AHKGame* G = T.Game(); const FVector2D P = W(Lamp->X - 3, Lamp->Y), Q = W(Lamp->X, Lamp->Y); G->Place(P.X, P.Y, std::atan2(-(Q.X - P.X), -(Q.Y - P.Y))); });
				GameWait(T, .3);
				Walk(T, 3);
				T.Log([&T, Lamp]() { const FVector2D Q = W(Lamp->X, Lamp->Y); return FString::Printf(TEXT("#2 walking into a lamp post: stopped %.2f m from it (web 0.32)"), FVector2D(T.Game()->Player.X - Q.X, T.Game()->Player.Z - Q.Y).Size()); });
				T.Expect(TEXT("#2 a lamp post stops you (about 0.32 m)"), [&T, Lamp]() { const FVector2D Q = W(Lamp->X, Lamp->Y); const double R = FVector2D(T.Game()->Player.X - Q.X, T.Game()->Player.Z - Q.Y).Size(); return R > .25 && R < .45; });
				T.Do([&T, Lamp]() { AHKGame* G = T.Game(); const FVector2D P = W(Lamp->X - 6, Lamp->Y + 4), Q = W(Lamp->X, Lamp->Y); G->Place(P.X, P.Y, std::atan2(-(Q.X - P.X), -(Q.Y - P.Y))); G->Player.Pitch = .25; });
				GameWait(T, .5);
				T.Shot(TEXT("04-street-lamp"));

				// ---------------- #11: Muselunden ----------------
				T.Expect(TEXT("#11 Muselunden: 12 holes, par 36"), [&T]() { FHKDiscGolf* M = T.Game()->Mini->Muselunden.Get(); if (!M || M->Holes.Num() != 12) return false; double P = 0; for (const FHKDiscGolf::FHole& H : M->Holes) P += H.Par; return P == 36; });
				T.Expect(TEXT("#11 the walkable square is +-760 m"), []() { return D().PlayHalf == 760; });
				At(T, 655.1, -359.0, 0);
				T.Do([&T]() { AHKGame* G = T.Game(); const FHKDiscGolf::FHole& H = G->Mini->Muselunden->Holes[0]; G->Place(H.Tee.X, H.Tee.Z, std::atan2(-(H.Basket.X - H.Tee.X), -(H.Basket.Z - H.Tee.Z))); });
				GameWait(T, .5);
				T.Log([&T]() { return TEXT("#11 prompt on hole 1's tee: ") + PromptOf(T.Game()); });
				T.Expect(TEXT("#11 the Muselunden prompt on hole 1"), [&T]() { return PromptOf(T.Game()).Contains(TEXT("Muselunden DiscGolfPark · Hull 1 (par 3, 106 m)")); });
				T.Shot(TEXT("05-muselunden-tee-1"));
				T.Press(2);
				GameWait(T, .5);
				T.Expect(TEXT("#11 playing Muselunden: HUD Hull 1/12"), [&T]() { FHKDiscGolf* M = T.Game()->Mini->Muselunden.Get(); return M->bActive && M->Hud.Course == TEXT("Muselunden DiscGolfPark") && M->Hud.Hole == 1 && M->Hud.Holes == 12; });
				T.Shot(TEXT("06-muselunden-hud"));
				T.Press(1);   // ○ quits
				GameWait(T, .5);
				T.Expect(TEXT("#11 buildings beyond the old 540 m edge block you"), []()
				{
					// Building outlines in the walk world with their middle more than 560 m out (local frame): standing inside is refused.
					int32 Tested = 0, Blocked = 0;
					for (const FHKObstacle& O : D().WalkWorld.Obstacles)
					{
						if (O.Kind != TEXT("building") || O.Points.Num() < 3 || Tested >= 10) continue;
						FVector2D C(0, 0);
						for (const FHKPt& P : O.Points) C += FVector2D(P.X, P.Z);
						C /= O.Points.Num();
						const FVector2D Lc = L(C.X, C.Y);
						if (FMath::Max(FMath::Abs(Lc.X), FMath::Abs(Lc.Y)) < 560) continue;
						bool bIn = false;   // the middle must be inside the outline
						for (int32 a = 0, b = O.Points.Num() - 1; a < O.Points.Num(); b = a++)
							if ((O.Points[a].Z > C.Y) != (O.Points[b].Z > C.Y) && C.X < (O.Points[b].X - O.Points[a].X) * (C.Y - O.Points[a].Z) / (O.Points[b].Z - O.Points[a].Z) + O.Points[a].X) bIn = !bIn;
						if (!bIn) continue;
						Tested++;
						if (!D().WalkWorld.CanStand(FHKPt(C.X, C.Y), 1.65, D().HeightWorld(C.X, C.Y))) Blocked++;
					}
					UE_LOG(LogHaarklous, Display, TEXT("HK-TEST #11 %d of %d buildings beyond 560 m block standing inside them"), Blocked, Tested);
					return Tested >= 5 && Blocked == Tested;
				});

				// ---------------- #9: the bike's first ride ----------------
				TSharedPtr<int32> Blocked = MakeShared<int32>(0);
				TSharedPtr<bool> Free = MakeShared<bool>(false);
				T.Do([&T]() { AHKGame* G = T.Game(); for (FHKVehicle* V : G->VehicleList) if (V->Kind == TEXT("motorcycle")) { G->Place(V->X + 1.2, V->Z, 0); break; } });
				GameWait(T, .3);
				T.Do([&T, Blocked, Free]()
				{
					AHKGame* G = T.Game();
					FHKMotorcycle* B = nullptr;
					for (FHKVehicle* V : G->VehicleList) if (V->Kind == TEXT("motorcycle")) { B = static_cast<FHKMotorcycle*>(V); break; }
					G->EnterVehicle(B);
					int32 N = 0;
					for (FHKVehicle* V : G->VehicleList) if (V->Kind == TEXT("car") && N < 6) { N++; if (!B->Clear(V->X, V->Z)) (*Blocked)++; }
					*Free = B->Clear(B->X - FMath::Sin(B->Yaw) * .3, B->Z - FMath::Cos(B->Yaw) * .3);
				});
				T.Log([Blocked, Free]() { return FString::Printf(TEXT("#9 the bike's first ride: %d of 6 parked cars block it, 0.3 m ahead free: %s"), *Blocked, *Free ? TEXT("yes") : TEXT("no")); });
				T.Expect(TEXT("#9 on the bike's first ride all 6 parked cars block it"), [Blocked]() { return *Blocked == 6; });
				T.Expect(TEXT("#9 the spot 0.3 m in front of the bike is free"), [Free]() { return *Free; });
				T.Do([&T]() { T.Game()->ExitVehicle(); });
				GameWait(T, .3);

				// ---------------- #7: petanque ends once at 13 ----------------
				TSharedPtr<double> Money0 = MakeShared<double>(0);
				T.Do([&T]() { AHKGame* G = T.Game(); const TSharedPtr<FJsonObject> Pq = HKJson::Obj(D().D, TEXT("petanque")); const FVector2D P = W(HKJson::Num(Pq, TEXT("x")), HKJson::Num(Pq, TEXT("s"))); G->Place(P.X, P.Y, 0); });
				GameWait(T, .4);
				T.Do([&T]() { AHKGame* G = T.Game(); for (FHKMinigame* M : G->Mini->All()) if (M == &G->Mini->Petanque) { G->StartMinigame(M); break; } });
				GameWait(T, .5);
				T.Do([&T, Money0]()
				{
					AHKGame* G = T.Game();
					FHKPetanque& P = G->Mini->Petanque;
					*Money0 = G->Shops->Wallet.Money;
					P.ScoreYou = 12; P.ScoreNpc = 12;
					// The last boules of a mène: the jack 8 m out, your boule 10 cm from it, everything else out of the court.
					P.Phase = FHKPetanque::EPhase::Play; P.Current = EHKSide::None; P.Waiting = 0; P.bHasFinish = false;
					P.Jack().bAlive = true; P.Jack().bDead = false; P.Jack().bFlight = false; P.Jack().U = P.Circle.X + 8; P.Jack().V = P.Circle.Y;
					for (int32 i = 1; i < 7; i++) { FHKPetanque::FBall& B = P.Balls[i]; B.bAlive = false; B.bDead = true; B.bFlight = false; B.VU = B.VV = 0; }
					FHKPetanque::FBall& Mine = P.Balls[1]; Mine.bAlive = true; Mine.bDead = false; Mine.U = P.Jack().U - .1; Mine.V = P.Jack().V;
				});
				GameWait(T, 1.5);
				T.Log([&T]() { const FHKPetanque& P = T.Game()->Mini->Petanque; return FString::Printf(TEXT("#7 petanque from 12-12: %s (score %d-%d)"), *P.Message, P.ScoreYou, P.ScoreNpc); });
				T.Expect(TEXT("#7 'You win 13–12!' and the score stays 13-12"), [&T]() { const FHKPetanque& P = T.Game()->Mini->Petanque; return P.Message == TEXT("You win 13–12!") && P.ScoreYou == 13 && P.ScoreNpc == 12; });
				GameWait(T, 3.5);
				T.Expect(TEXT("#7 the game ends by itself after 4 s"), [&T]() { return T.Game()->Player.Mode == TEXT("foot") && !T.Game()->Mini->Petanque.bActive; });
				T.Expect(TEXT("#7 +150 kr exactly once"), [&T, Money0]() { return FMath::Abs(T.Game()->Shops->Wallet.Money - *Money0 - 150) < .001; });

				// ---------------- #6: the stashes ----------------
				T.Expect(TEXT("#6 three crates, six items"), [&T]() { return T.Game()->Stash && T.Game()->Stash->Places.Num() == 3 && T.Game()->Stash->Items.Num() == 6; });
				for (int32 Pl = 0; Pl < 3; Pl++)
				{
					T.Do([&T, Pl]() { AHKGame* G = T.Game(); const FHKStashPlace& P = G->Stash->Places[Pl]; const double A = P.Yaw + HK::Pi / 2; G->Place(P.X + FMath::Sin(A) * 1.0, P.Z + FMath::Cos(A) * 1.0, A, P.Y); });
					GameWait(T, .4);
					T.Do([&T, Pl]() { AHKGame* G = T.Game(); const FHKStashPlace& P = G->Stash->Places[Pl]; LookAt(G, FVector(P.X, P.Y + .35, P.Z)); });
					GameWait(T, .3);
					T.Log([&T, Pl]() { return FString::Printf(TEXT("#6 at the %s crate: %s"), *T.Game()->Stash->Places[Pl].Name, *PromptOf(T.Game())); });
					T.Expect(FString::Printf(TEXT("#6 the stash prompt at crate %d"), Pl), [&T]() { const FString S = PromptOf(T.Game()); return S == TEXT("□ Snort a line") || S == TEXT("□ Use the needle"); });
					T.Shot(FString::Printf(TEXT("07-stash-%d"), Pl));
				}
				// Use one: a boost, the item hides, "Nothing left here", back after 90 s.
				T.Press(2);
				GameWait(T, .3);
				T.Expect(TEXT("#6 a boost for 30 s"), [&T]() { return !T.Game()->BoostKind.IsEmpty() && T.Game()->BoostT > 29; });
				T.Log([&T]() { return TEXT("#6 boost: ") + T.Game()->BoostKind + TEXT(" · notice: ") + T.Game()->CurrentNotice(); });
				// Both items of that crate used up: only then the crate has nothing left (a ready item wins the prompt).
				T.Do([&T]()
				{
					AHKGame* G = T.Game();
					int32 Pl = -1;
					for (const FHKStashItem& It : G->Stash->Items) if (!It.bReady) Pl = It.Place;
					for (int32 i = 0; i < G->Stash->Items.Num(); i++) if (G->Stash->Items[i].Place == Pl && G->Stash->Items[i].bReady) G->Stash->Use(i);
					G->Player.Pitch = 1.2;   // look up: nothing aimed at, the nearest item counts
				});
				GameWait(T, .3);
				T.Expect(TEXT("#6 'Nothing left here – come back later'"), [&T]() { return PromptOf(T.Game()) == TEXT("Nothing left here – come back later"); });
				T.Do([&T]() { for (FHKStashItem& It : T.Game()->Stash->Items) if (!It.bReady) It.T = .2; });
				GameWait(T, .5);
				T.Expect(TEXT("#6 the item is back after the cooldown"), [&T]() { for (const FHKStashItem& It : T.Game()->Stash->Items) if (!It.bReady) return false; return true; });
				// Speed: 3.5 -> 10.5 m/s, FOV 82.
				At(T, 100, 60, 0);
				T.Do([&T]() { AHKGame* G = T.Game(); G->BoostKind = TEXT("speed"); G->BoostT = 30; G->Player.bRun = false; });
				// On the long grass strip (the airstrip), along it.
				T.Do([&T]() { AHKGame* G = T.Game(); const double Yw = .5236; G->Place(-116.5 - FMath::Sin(Yw) * 30, -FMath::Cos(Yw) * 30, Yw); G->Player.Pitch = 0; });
				GameWait(T, .3);
				TSharedPtr<FVector2D> P0 = MakeShared<FVector2D>();
				WalkGame(T, .5);
				T.Do([&T, P0]() { *P0 = FVector2D(T.Game()->Player.X, T.Game()->Player.Z); });
				WalkGame(T, 2);
				T.Log([&T, P0]() { return FString::Printf(TEXT("#6 speed boost: %.2f m/s walking (web 10.5), fov %.0f"), (FVector2D(T.Game()->Player.X, T.Game()->Player.Z) - *P0).Size() / 2, T.Game()->Fov); });
				T.Expect(TEXT("#6 speed boost: about 10.5 m/s and fov 82"), [&T, P0]() { const double V = (FVector2D(T.Game()->Player.X, T.Game()->Player.Z) - *P0).Size() / 2; return V > 9.5 && V < 11 && FMath::Abs(T.Game()->Fov - 82) < .01; });
				T.Shot(TEXT("08-speed-boost"));
				// Jump: about 0.49 -> 1.47 m.
				T.Do([&T]() { AHKGame* G = T.Game(); G->BoostKind = TEXT("jump"); G->BoostT = 30; });
				GameWait(T, .3);
				TSharedPtr<double> Top = MakeShared<double>(0);
				T.Press(0);
				{
					TSharedPtr<int32> N = MakeShared<int32>(0);
					T.Steps.Add([&T, Top, N](double) { *Top = FMath::Max(*Top, T.Game()->Player.JumpY); return ++*N > 100; });
				}
				T.Log([Top]() { return FString::Printf(TEXT("#6 jump boost: %.2f m high (web 1.47)"), *Top); });
				T.Expect(TEXT("#6 jump boost: about 1.47 m"), [Top]() { return FMath::Abs(*Top - 1.47) < .06; });
				// Bullet time: the world at 0.35x, your own walking unchanged, the sound muffled; then the boost ends.
				T.Do([&T]() { AHKGame* G = T.Game(); G->BoostKind = TEXT("slowmo"); G->BoostT = 30; });
				T.Expect(TEXT("#6 bullet time: the world at 0.35x"), [&T]() { return FMath::Abs(T.Game()->WorldScale() - .35) < 1e-9; });
				WalkGame(T, .3);
				T.Do([&T, P0]() { *P0 = FVector2D(T.Game()->Player.X, T.Game()->Player.Z); });
				WalkGame(T, 1);
				T.Expect(TEXT("#6 bullet time: your own walking speed is unchanged (3.5 m/s)"), [&T, P0]() { const double V = (FVector2D(T.Game()->Player.X, T.Game()->Player.Z) - *P0).Size(); return V > 3.2 && V < 3.8; });
				T.Shot(TEXT("09-bullet-time"));
				T.Do([&T]() { T.Game()->BoostT = .05; });
				GameWait(T, .3);
				T.Expect(TEXT("#6 the boost wears off"), [&T]() { return T.Game()->BoostKind.IsEmpty() && T.Game()->WorldScale() == 1 && T.Game()->CurrentNotice() == TEXT("The boost wears off."); });

				// ---------------- #5: the shops ----------------
				for (int32 Si = 0; Si < 2; Si++)
				{
					// From the street through the door centre into the shop.
					T.Do([&T, Si]() { AHKGame* G = T.Game(); const FHKShop& S = G->Shops->Shops[Si]; const FVector2D A = S.ToWorld(S.UDoor, -3.5), B = S.ToWorld(S.UDoor, 3); G->Place(A.X, A.Y, std::atan2(-(B.X - A.X), -(B.Y - A.Y))); G->Player.Pitch = 0; });
					GameWait(T, .4);
					T.Shot(FString::Printf(TEXT("10-shop-%d-front"), Si));
					Walk(T, 2.2);
					T.Expect(FString::Printf(TEXT("#5 walked through the door into shop %d"), Si), [&T, Si]() { return T.Game()->Shops->ShopAt(T.Game()->Player.X, T.Game()->Player.Z, T.Game()->Player.Feet) == Si; });
					T.Shot(FString::Printf(TEXT("11-shop-%d-inside"), Si));
				}
				// Every kind of item by aiming at it (Joker), then pay: Grandiosa + Banan + Sekk med ved + Red Bull = 196,60 kr.
				TSharedPtr<int32> Aimed = MakeShared<int32>(0);
				T.Do([&T, Aimed]()
				{
					AHKGame* G = T.Game();
					FHKShops& Sh = *G->Shops;
					G->Basket.Reset();
					for (const TCHAR* Id : {TEXT("grandiosa"), TEXT("banan"), TEXT("ved"), TEXT("redbull")})
					{
						const int32 Pi = Sh.ProductIndex(Id);
						for (int32 b = 0; b < Sh.Boxes.Num(); b++)
						{
							const FHKShopBox& B = Sh.Boxes[b];
							if (B.Shop != 0 || B.Product != Pi) continue;
							// Stand 0.9 m beside the box on the side the aim is free (tried in turn) and aim at it.
							const FVector C = B.Box.World.GetPosition();
							const FVector2D Lc = Sh.Shops[0].ToLocal(C.X, C.Z);
							bool bTaken = false;
							for (const FVector2D& Dir : {FVector2D(-1, 0), FVector2D(1, 0), FVector2D(0, -1), FVector2D(0, 1)})
							{
								const FVector2D Stand = Sh.Shops[0].ToWorld(Lc.X + Dir.X * .9, Lc.Y + Dir.Y * .9);
								G->Place(Stand.X, Stand.Y, G->Player.Yaw, Sh.Shops[0].FloorY);
								LookAt(G, C);
								G->CamPos = FVector(G->Player.X, G->Player.Feet + G->Player.EyeNow, G->Player.Z);
								G->CamQuat = HK::EulerToQuat(G->Player.Pitch, G->Player.Yaw, 0, TEXT("YXZ"));
								const FHKPrompt Pr = G->ShopPrompt();
								if (Pr.Text.StartsWith(TEXT("□ Take ")) && Pr.Text.Contains(Sh.Products[Pi].Name)) { Pr.Act(); (*Aimed)++; bTaken = true; break; }
							}
							if (bTaken) break;
						}
					}
				});
				T.Expect(TEXT("#5 four items taken by aiming at them"), [Aimed]() { return *Aimed == 4; });
				T.Do([&T]() { AHKGame* G = T.Game(); G->Shops->Wallet.Reset(500); G->ShopClock = []() { return Day(3, 12); }; G->PayAt(0); });
				T.Log([&T]() { return FString::Printf(TEXT("#5 paid at Joker: %s left (500 - 196,60 = 303,40), notice: %s"), *FHKShops::Kr(T.Game()->Shops->Wallet.Money), *T.Game()->CurrentNotice()); });
				T.Expect(TEXT("#5 Joker: 196,60 kr for Grandiosa + Banan + Sekk med ved + Red Bull"), [&T]() { return FMath::Abs(T.Game()->Shops->Wallet.Money - 303.4) < .001 && T.Game()->Basket.Num() == 0; });
				// Kiwi: Tine Lettmelk + Kvikk Lunsj = 47,80 kr.
				T.Expect(TEXT("#5 Kiwi: 47,80 kr for Tine Lettmelk + Kvikk Lunsj"), [&T]()
				{
					AHKGame* G = T.Game(); FHKShops& Sh = *G->Shops;
					const double A = FHKShops::PriceOf(Sh.Shops[1], Sh.Products[Sh.ProductIndex(TEXT("melk"))]), B = FHKShops::PriceOf(Sh.Shops[1], Sh.Products[Sh.ProductIndex(TEXT("kvikklunsj"))]);
					return FMath::Abs(A + B - 47.8) < 1e-9;
				});
				T.Expect(TEXT("#5 every Kiwi price ends in ,90"), [&T]() { for (const FHKProduct& P : T.Game()->Shops->Products) { const double V = FHKShops::PriceOf(T.Game()->Shops->Shops[1], P); if (FMath::Abs(V - FMath::FloorToDouble(V) - .9) > 1e-6) return false; } return true; });
				// Beer hours with a fake clock.
				auto Buy = [](AHKGame* G, std::initializer_list<const TCHAR*> Ids) { G->Basket.Reset(); for (const TCHAR* Id : Ids) G->TakeProduct(0, G->Shops->ProductIndex(Id)); };
				T.Do([&T, Buy]() { AHKGame* G = T.Game(); G->Shops->Wallet.Reset(500); G->ShopClock = []() { return Day(6, 14); }; Buy(G, {TEXT("pils")}); G->PayAt(0); });
				T.Expect(TEXT("#5 Sunday 14:00: beer refused, nothing charged"), [&T]() { return T.Game()->Shops->Wallet.Money == 500 && T.Game()->Shops->Wallet.Count(TEXT("pils")) == 0; });
				T.Do([&T, Buy]() { AHKGame* G = T.Game(); G->Shops->Wallet.Reset(500); G->ShopClock = []() { return Day(5, 19); }; Buy(G, {TEXT("pils"), TEXT("cola")}); G->PayAt(0); });
				T.Expect(TEXT("#5 Saturday 19:00: beer + cola charges only the cola"), [&T]() { return FMath::Abs(T.Game()->Shops->Wallet.Money - (500 - 34.9)) < .001 && T.Game()->Shops->Wallet.Count(TEXT("cola")) == 1 && !T.Game()->Shops->Wallet.Count(TEXT("pils")); });
				T.Do([&T, Buy]() { AHKGame* G = T.Game(); G->Shops->Wallet.Reset(500); G->ShopClock = []() { return Day(3, 12); }; Buy(G, {TEXT("pils"), TEXT("pils")}); G->PayAt(0); });
				T.Expect(TEXT("#5 Thursday 12:00: two beers sold"), [&T]() { return T.Game()->Shops->Wallet.Count(TEXT("pils")) == 2; });
				// Card declined with 20 kr: coffee (74,90) refused and still in the basket.
				T.Do([&T, Buy]() { AHKGame* G = T.Game(); G->Shops->Wallet.Reset(20); Buy(G, {TEXT("kaffe")}); G->PayAt(0); });
				T.Expect(TEXT("#5 card declined: nothing charged, the coffee stays in the basket"), [&T]() { return T.Game()->Shops->Wallet.Money == 20 && T.Game()->Basket.Num() == 1 && T.Game()->CurrentNotice().StartsWith(TEXT("Card declined")); });
				T.Shot(TEXT("12-card-declined"));
				// Walk out with it: the alarm, the basket empties.
				T.Do([&T]() { AHKGame* G = T.Game(); const FHKShop& S = G->Shops->Shops[0]; const FVector2D A = S.ToWorld(S.UDoor, 2.5), B = S.ToWorld(S.UDoor, -4); G->Place(A.X, A.Y, std::atan2(-(B.X - A.X), -(B.Y - A.Y)), S.FloorY); });
				GameWait(T, .3);
				Walk(T, 2.5);
				T.Expect(TEXT("#5 leaving with an unpaid item: alarm, basket empty"), [&T]() { return T.Game()->Basket.Num() == 0 && T.Game()->CurrentNotice().StartsWith(TEXT("BEEP BEEP")); });
				// The bag: drink a beer (+1 bottle), return it (+2 kr), firewood at the firepit (+3 fire).
				T.Do([&T]() { AHKGame* G = T.Game(); G->Shops->Wallet.Reset(100); G->Shops->Wallet.Add(TEXT("pils")); G->Shops->Wallet.Add(TEXT("ved")); G->Drunk = 0; G->OpenBag(); });
				GameWait(T, .2);
				T.Shot(TEXT("13-bag"));
				T.Press(0);   // ✕ uses the first item (the beer)
				GameWait(T, .3);
				T.Expect(TEXT("#5 drinking a beer from the bag: +30 s drunk, +1 bottle"), [&T]() { return T.Game()->Shops->Wallet.Pant == 1 && T.Game()->Drunk > 29; });
				T.Press(1);   // ○ closes the bag
				T.Do([&T]() { AHKGame* G = T.Game(); G->Drunk = 0; G->ReturnPant(); });
				T.Expect(TEXT("#5 the bottle return pays 2 kr a bottle"), [&T]() { return FMath::Abs(T.Game()->Shops->Wallet.Money - 102) < .001 && T.Game()->Shops->Wallet.Pant == 0; });
				TSharedPtr<double> Fire0 = MakeShared<double>(0);
				T.Do([&T, Fire0]() { AHKGame* G = T.Game(); G->Place(G->FirePos.X + 3, G->FirePos.Y, 0); G->LocalFire.Level = 1; *Fire0 = G->FireLevel(); G->UseItem(G->Shops->ProductIndex(TEXT("ved"))); });
				GameWait(T, 1.5);
				T.Log([&T, Fire0]() { return FString::Printf(TEXT("#5 firewood at the firepit: fire %.2f -> %.2f"), *Fire0, T.Game()->FireLevel()); });
				T.Expect(TEXT("#5 the firewood throws 3 logs on the fire"), [&T, Fire0]() { return FMath::Abs(T.Game()->FireLevel() - *Fire0 - 3) < .1; });
				T.Do([&T]() { T.Game()->ShopClock = nullptr; T.Game()->Shops->Wallet.Reset(500); });

				// ---------------- #14: the distant land from the air ----------------
				T.Do([&T]() { const FVector2D P = W(-300, 640), Q = W(-300, 2000); T.Pose(P.X, D().HeightWorld(P.X, P.Y) + 60, P.Y, std::atan2(-(Q.X - P.X), -(Q.Y - P.Y)), -.18); });
				T.Wait(2);
				T.Shot(TEXT("14-distant-land-south"));
				T.Do([&T]() { const FVector2D P = W(-88, 400), Q = W(-88, 1500); T.Pose(P.X, D().HeightWorld(P.X, P.Y) + 150, P.Y, std::atan2(-(Q.X - P.X), -(Q.Y - P.Y)), -.3); });
				T.Wait(2);
				T.Shot(TEXT("15-above-the-site-south"));
				T.FreeCamera();
				T.Do([&T]() { T.Game()->FixedDt = 0; });
				T.Quit();
			});
		}
	} GPlacesRegister;
}
