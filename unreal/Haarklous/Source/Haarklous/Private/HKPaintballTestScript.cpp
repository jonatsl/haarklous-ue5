// Scripted play-through "paintball" (-hktest=paintball): the paintball course (new feature, 28 Oct 2026) with five
// simulated friends. Their states go through the same JSON as online players' ('pb' in 'st'), and they play like
// clients would: join the lobby after us (so we are the admin), ask for teams, take their spawn points, fire balls (as
// 'pb.h' entries) and report being hit. We start a team deathmatch from the lobby (hold ✕), get hit and come back,
// put an opponent out, watch the score; then capture the flag: switch the game with the D-pad, take the red flag and
// bring it home. Screenshots and the frame rate inside the course.
#include "HKTestDriver.h"
#include "Haarklous.h"
#include "HKAvatars.h"
#include "HKData.h"
#include "HKGame.h"
#include "HKInput.h"
#include "HKMath.h"
#include "HKNode.h"
#include "HKPaintball.h"
#include "HKRigActor.h"
#include "Engine/World.h"

namespace HKPaintballTest
{
	struct FFake
	{
		int32 Id = 0, Slot = 0, Req = -1, Ses = 0, Outs = 0, By = 0, Round = 0, Seq = 0;
		FString Name;
		FVector Feet = FVector::ZeroVector;
		double Yaw = 0, Lobby = 0;
		bool bAlive = true;
		TArray<TArray<double>> Shots;
		TUniquePtr<FHKAvatar> Av;
		TWeakObjectPtr<AHKRigActor> Actor;
	};
	struct FScene { TArray<FFake> Fakes; int32 HitsOnFakes = 0; int32 LastVictim = 0; double Dt = .016; };

	FHKPaintball& Pb(AHKTestDriver& T) { return *T.Game()->Paintball; }

	// A fake player's 'pb' state, as its client would send it (then parsed like a remote's).
	TSharedPtr<FJsonObject> StateOf(FFake& F)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("l"), F.Lobby); O->SetNumberField(TEXT("q"), F.Req); O->SetNumberField(TEXT("s"), F.Ses);
		O->SetNumberField(TEXT("a"), F.bAlive ? 1 : 0); O->SetNumberField(TEXT("o"), F.Outs); O->SetNumberField(TEXT("b"), F.By);
		O->SetNumberField(TEXT("r"), F.Round);
		TArray<TSharedPtr<FJsonValue>> H;
		for (const TArray<double>& S : F.Shots)
		{
			TArray<TSharedPtr<FJsonValue>> A;
			for (double D : S) A.Add(MakeShared<FJsonValueNumber>(D));
			H.Add(MakeShared<FJsonValueArray>(A));
		}
		if (H.Num()) O->SetArrayField(TEXT("h"), H);
		F.Shots.Reset();
		return O;
	}

	// The fakes' client logic each frame: play in the admin's game, take their spawn points, pose their avatars.
	void Install(AHKTestDriver& T, TSharedPtr<FScene> S)
	{
		AHKGame* G = T.Game();
		G->PbOthers = [&T, S](TArray<FHKPbMember>& Out)
		{
			FHKPaintball& P = Pb(T);
			const FHKPbSession& Ses = P.Session();
			for (FFake& F : S->Fakes)
			{
				const int32 Team = Ses.TeamOf(F.Id);
				if (Ses.Id && Ses.State != TEXT("lobby") && Team >= 0 && F.Ses != Ses.Id)
				{
					TArray<int32> Ids;
					for (const auto& KV : Ses.Team) if (KV.Value == Team) Ids.Add(KV.Key);
					Ids.Sort();
					F.Feet = P.SpawnPoint(Team, Ids.IndexOfByKey(F.Id), F.Yaw);
					F.Ses = Ses.Id; F.Round = Ses.Round; F.bAlive = true; F.Outs = 0;
				}
				if (Ses.State == TEXT("lobby") && F.Ses) F.Ses = 0;
				FHKPbMember M;
				FHKPbMember::FromJson(StateOf(F), M);
				M.Id = F.Id; M.Name = F.Name; M.Slot = F.Slot; M.Feet = F.Feet; M.Yaw = F.Yaw; M.Stance = TEXT("stand");
				Out.Add(M);
				if (F.Av)
				{
					FHKPoseArgs A; A.Stance = TEXT("stand"); A.Dt = S->Dt;
					A.bMarker = Ses.State != TEXT("lobby") && Ses.TeamOf(F.Id) >= 0 && F.Ses == Ses.Id;
					F.Av->Pose(A);
					F.Av->Group()->SetPos(F.Feet.X, F.Feet.Y, F.Feet.Z);
					F.Av->Group()->SetRotation(0, F.Yaw, 0);
					F.Av->Group()->bVisible = !(A.bMarker && !F.bAlive);
					F.Av->Group()->UpdateMatrixWorld();
				}
			}
		};
		G->PbAvatarOf = [S](int32 Id) -> AHKRigActor* { for (FFake& F : S->Fakes) if (F.Id == Id) return F.Actor.Get(); return nullptr; };
		G->Paintball->OnBallHitsFigure = [S](int32 Owner, int32 Victim) { if (Owner == 0) { S->HitsOnFakes++; S->LastVictim = Victim; } };
	}

	FFake* Fake(TSharedPtr<FScene> S, int32 Id) { for (FFake& F : S->Fakes) if (F.Id == Id) return &F; return nullptr; }

	// A ball from a fake's eye toward a point (lifted a little for the drop over the distance).
	void Shoot(AHKTestDriver& T, FFake& F, const FVector& At)
	{
		const FVector Eye = F.Feet + FVector(0, 1.55, 0);
		const double D = FVector::Dist(Eye, At);
		const FVector Dir = (At + FVector(0, D * D * .0012, 0) - Eye).GetSafeNormal();
		const FVector O = Eye + Dir * .45, V = Dir * HKPb::V0;
		F.Shots.Add({(double)++F.Seq, T.Game()->PbServerNow ? T.Game()->PbServerNow() : T.Game()->Time * 1000, O.X, O.Y, O.Z, V.X, V.Y, V.Z});
	}

	void Look(AHKTestDriver& T, const FVector& At)
	{
		AHKGame* G = T.Game();
		const FVector Eye(G->Player.X, G->Player.Feet + G->Player.EyeNow, G->Player.Z);
		G->Player.Yaw = FMath::Atan2(-(At.X - Eye.X), -(At.Z - Eye.Z));
		G->Player.Pitch = FMath::Atan2(At.Y - Eye.Y, FVector2D(At.X - Eye.X, At.Z - Eye.Z).Size());
	}

	// Wait for a condition (logging PASS / FAIL like Expect), at most Seconds.
	void Until(AHKTestDriver& T, const FString& What, double Seconds, TFunction<bool()> Fn)
	{
		TSharedPtr<double> Time = MakeShared<double>(0);
		T.Steps.Add([&T, What, Seconds, Fn, Time](double Dt)
		{
			*Time += Dt;
			const bool bOk = Fn();
			if (!bOk && *Time < Seconds) return false;
			if (!bOk) T.Failures++;
			UE_LOG(LogHaarklous, Display, TEXT("HK-TEST %s %s (%.1f s)  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What, *Time, *T.P());
			return true;
		});
	}

	struct FRegister
	{
		FRegister()
		{
			FHKTestScripts::All().Add(TEXT("paintball"), [](AHKTestDriver& T)
			{
				TSharedPtr<FScene> S = MakeShared<FScene>();
				T.Wait(3);
				T.Press(0);
				T.Wait(1.5);
				T.Expect(TEXT("paintball: course loaded"), [&T]() { return T.Game()->Paintball.IsValid() && T.Game()->PbCourse; });
				// The course from above, and from the lobby.
				T.Do([&T]()
				{
					const FHKPaintball& P = Pb(T);
					const FVector C = P.World(-12, P.Floor + 38, 46);
					T.Game()->bCameraOverride = true;
					T.Game()->OverridePos = C;
					const FVector To = P.World(0, P.Floor, 0);
					T.Game()->OverrideQuat = HK::EulerToQuat(-FMath::Atan2(C.Y - To.Y, FVector2D(C.X - To.X, C.Z - To.Z).Size()), FMath::Atan2(-(To.X - C.X), -(To.Z - C.Z)), 0, TEXT("YXZ"));
				});
				T.WaitShaders();
				T.Wait(1);
				T.Shot(TEXT("01-course"));
				// Printed signs must read the right way: a construction-site fence banner from outside, the course's gate sign.
				auto LookAt = [&T](const FVector& From, const FVector& To)
				{
					T.Do([&T, From, To]()
					{
						T.Game()->bCameraOverride = true; T.Game()->OverridePos = From;
						T.Game()->OverrideQuat = HK::EulerToQuat(FMath::Atan2(To.Y - From.Y, FVector2D(To.X - From.X, To.Z - From.Z).Size()), FMath::Atan2(-(To.X - From.X), -(To.Z - From.Z)), 0, TEXT("YXZ"));
					});
				};
				{
					const FHKData& D = FHKData::Get();
					const FVector2D A = D.ToWorld(-41.75, 298), B = D.ToWorld(-41.75, 304);
					LookAt(FVector(A.X, D.GroundWorld(A.X, A.Y) + 1.4, A.Y), FVector(B.X, D.GroundWorld(B.X, B.Y) + 1.2, B.Y));
				}
				T.Wait(.6);
				T.Shot(TEXT("00-site-banner"));
				T.Do([&T]()
				{
					const FHKPaintball& P = Pb(T);
					const FVector From = P.World(-2.8, P.Floor + 1.7, 31), To = P.World(-2.8, P.Floor + 1.6, 27.2);
					T.Game()->bCameraOverride = true; T.Game()->OverridePos = From;
					T.Game()->OverrideQuat = HK::EulerToQuat(FMath::Atan2(To.Y - From.Y, FVector2D(To.X - From.X, To.Z - From.Z).Size()), FMath::Atan2(-(To.X - From.X), -(To.Z - From.Z)), 0, TEXT("YXZ"));
				});
				T.Wait(.6);
				T.Shot(TEXT("00b-gate-sign"));
				T.FreeCamera();
				// Walk in: we are first, so we are the admin.
				T.Do([&T]() { const FHKPaintball& P = Pb(T); const FVector L = P.World(-6, 0, 32); T.Game()->Place(L.X, L.Z, P.WorldYaw(0)); });
				T.Wait(1);
				T.Expect(TEXT("paintball: in the lobby"), [&T]() { return Pb(T).Member() && Pb(T).InLobbyNow(); });
				T.Expect(TEXT("paintball: first in is the admin"), [&T]() { return Pb(T).IsAdmin(); });
				// Five friends come after us: two ask for blue, one for red, two take any team.
				T.Do([&T, S]()
				{
					FHKPaintball& P = Pb(T);
					const TCHAR* Names[5] = {TEXT("Kari"), TEXT("Ola"), TEXT("Nora"), TEXT("Emil"), TEXT("Sara")};
					const int32 Reqs[5] = {0, 0, 1, -1, -1};
					for (int32 i = 0; i < 5; i++)
					{
						FFake F;
						F.Id = 2 + i; F.Slot = (i + 1) % 5; F.Name = Names[i]; F.Req = Reqs[i];
						F.Lobby = (T.Game()->PbServerNow ? T.Game()->PbServerNow() : T.Game()->Time * 1000) + 500 + i * 300;
						const FVector L = P.World(-12 + i * 2.6, 0, 33 + (i % 2));
						F.Feet = FVector(L.X, FHKData::Get().GroundWorld(L.X, L.Z), L.Z);
						F.Yaw = P.WorldYaw(0);
						F.Av = HKAvatars::Avatar(F.Slot, F.Name);
						FActorSpawnParameters SP;
						SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
						AHKRigActor* A = T.GetWorld()->SpawnActor<AHKRigActor>(AHKRigActor::StaticClass(), FTransform::Identity, SP);
						A->Init(&F.Av->Rig, F.Av->RigName);
						F.Actor = A;
						S->Fakes.Add(MoveTemp(F));
					}
					Install(T, S);
				});
				T.Wait(1);
				T.Expect(TEXT("paintball: six in the lobby, we stay admin"), [&T]() { int32 N = 0; for (const FHKPbMember& M : Pb(T).Members()) if (M.Lobby > 0) N++; return N == 6 && Pb(T).IsAdmin(); });
				T.Do([&T]() { const FHKPaintball& P = Pb(T); const FVector L = P.World(-6, 0, 29); T.Game()->Place(L.X, L.Z, P.WorldYaw(PI)); });
				T.Wait(.8);
				T.Shot(TEXT("02-lobby"));
				// ↓ asks for red; then hold ✕ to start the team deathmatch (respawns).
				T.Press(13);
				T.Wait(.3);
				T.Hold(0, 1); T.Wait(1.4); T.Hold(0, 0);
				Until(T, TEXT("paintball: game started (countdown)"), 3, [&T]() { return Pb(T).InGame() && Pb(T).Session().State == TEXT("count"); });
				T.Log([&T]()
				{
					const FHKPbSession& Ses = Pb(T).Session();
					FString L;
					for (const auto& KV : Ses.Team) L += FString::Printf(TEXT("%d:%s "), KV.Key, KV.Value ? TEXT("red") : TEXT("blue"));
					return FString::Printf(TEXT("teams %s (we asked for red)"), *L);
				});
				T.Expect(TEXT("paintball: wishes honoured and teams balanced"), [&T]()
				{
					const FHKPbSession& Ses = Pb(T).Session();
					int32 N[2] = {0, 0};
					for (const auto& KV : Ses.Team) N[KV.Value]++;
					return Ses.TeamOf(0) == 1 && Ses.TeamOf(2) == 0 && Ses.TeamOf(3) == 0 && Ses.TeamOf(4) == 1 && N[0] == 3 && N[1] == 3;
				});
				T.Expect(TEXT("paintball: we are at a red spawn point"), [&T]() { const FVector2D C = Pb(T).Course(T.Game()->Player.X, T.Game()->Player.Z); return C.X > 25; });
				T.Wait(.5);
				T.Shot(TEXT("03-countdown"));
				Until(T, TEXT("paintball: play"), 7, [&T]() { return Pb(T).Session().State == TEXT("play"); });
				// A blue friend fires at us until a ball breaks on us.
				TSharedPtr<double> Fired = MakeShared<double>(0);
				T.Steps.Add([&T, S, Fired](double Dt)
				{
					AHKGame* G = T.Game();
					FFake* F = Fake(S, 2);
					// Kari stands about 10 m from us with a clear line (no cover between).
					if (*Fired == 0) { F->Feet = Pb(T).World(20, Pb(T).Floor, -16); F->Yaw = FMath::Atan2(-(G->Player.X - F->Feet.X), -(G->Player.Z - F->Feet.Z)); }
					*Fired += Dt;
					if (FMath::Fmod(*Fired, .35) < Dt) Shoot(T, *F, FVector(G->Player.X, G->Player.Feet + 1.2, G->Player.Z));
					return !Pb(T).Alive() || *Fired > 6;
				});
				T.Do([&T]() { Look(T, Pb(T).World(20, Pb(T).Floor + 1.4, -16)); });
				T.Expect(TEXT("paintball: a ball broke on us: out"), [&T]() { return !Pb(T).Alive(); });
				T.Wait(.2);
				T.Shot(TEXT("04-hit"));
				T.Log([&T]() { return FString::Printf(TEXT("score blue %d : red %d"), Pb(T).Session().Score(0), Pb(T).Session().Score(1)); });
				Until(T, TEXT("paintball: blue scored for it"), 2, [&T]() { return Pb(T).Session().Score(0) == 1; });
				Until(T, TEXT("paintball: back in at our base after 5 s"), 7, [&T]() { return Pb(T).Alive() && Pb(T).Course(T.Game()->Player.X, T.Game()->Player.Z).X > 25; });
				// We put Kari out: aim at her chest and fire (as her client, she reports it).
				TSharedPtr<double> Shooting = MakeShared<double>(0);
				T.Steps.Add([&T, S, Shooting](double Dt)
				{
					FFake* F = Fake(S, 2);
					F->Feet = Pb(T).World(14, Pb(T).Floor, 2);
					const FVector Chest = F->Feet + FVector(0, 1.25, 0);
					Look(T, Chest);
					*Shooting += Dt;
					if (FMath::Fmod(*Shooting, .25) < Dt)
					{
						const FVector Eye = T.Game()->CamPos, D = (Chest + FVector(0, FMath::Square(FVector::Dist(Eye, Chest)) * .0012, 0) - Eye).GetSafeNormal();
						Pb(T).TestFire(D);
					}
					if (S->LastVictim == 2 && F->bAlive) { F->bAlive = false; F->Outs++; F->By = 0; }
					return !F->bAlive || *Shooting > 6;
				});
				T.Shot(TEXT("05-firing"));
				Until(T, TEXT("paintball: our ball reached Kari, red scored"), 3, [&T, S]() { return S->HitsOnFakes > 0 && Pb(T).Session().Score(1) == 1; });
				// Paint everywhere: a sweep over the cover, and the others in their gear.
				TSharedPtr<double> Sweep = MakeShared<double>(0);
				T.Steps.Add([&T, Sweep](double Dt)
				{
					*Sweep += Dt;
					AHKGame* G = T.Game();
					G->Player.Yaw += Dt * .25; G->Player.Pitch = -.05;
					if (FMath::Fmod(*Sweep, .1) < Dt) Pb(T).TestFire(Pb(T).Ctx.CameraFwd());
					return *Sweep > 4;
				});
				T.Shot(TEXT("06-paint"));
				// Close up: a dozen balls on the side of a container from 4 m.
				T.Do([&T]() { const FHKPaintball& P = Pb(T); const FVector At = P.World(17, 0, 7); T.Game()->Place(At.X, At.Z, P.WorldYaw(PI)); });
				TSharedPtr<double> Burst = MakeShared<double>(0);
				T.Steps.Add([&T, Burst](double Dt)
				{
					*Burst += Dt;
					Look(T, Pb(T).World(17 + FMath::Sin(*Burst * 3) * 1.5, Pb(T).Floor + 1.3 + FMath::Cos(*Burst * 2) * .5, 10.8));
					if (FMath::Fmod(*Burst, .12) < Dt) Pb(T).TestFire(Pb(T).Ctx.CameraFwd());
					return *Burst > 1.6;
				});
				T.Wait(.3);
				T.Shot(TEXT("06b-splats"));
				T.Do([&T, S]()
				{
					// The camera on the blue team at their base: markers, goggles, armbands.
					FHKPaintball& P = Pb(T);
					FFake* F = Fake(S, 3);
					double Y;
					F->Feet = P.SpawnPoint(0, 1, Y); F->Yaw = Y;
					const FVector C = F->Feet + FVector(0, 1.6, 0) + FVector(-FMath::Sin(Y), 0, -FMath::Cos(Y)) * 2.2;
					T.Game()->bCameraOverride = true; T.Game()->OverridePos = C;
					T.Game()->OverrideQuat = HK::EulerToQuat(-.12, Y + PI, 0, TEXT("YXZ"));
				});
				T.Wait(.6);
				T.Shot(TEXT("07-players-gear"));
				T.FreeCamera();
				T.MeasureFps(TEXT("paintball"), 6);
				// End this game (as the admin), back to the lobby; capture the flag next.
				T.Do([&T]() { Pb(T).TestSession([&T](FHKPbSession& Ses) { Ses.T0 = (T.Game()->PbServerNow ? T.Game()->PbServerNow() : T.Game()->Time * 1000) - Ses.Dur * 1000; }); });
				Until(T, TEXT("paintball: the game ends on time"), 3, [&T]() { return Pb(T).Session().State == TEXT("end"); });
				T.Shot(TEXT("08-result"));
				Until(T, TEXT("paintball: back in the lobby"), 14, [&T]() { return !Pb(T).InGame() && Pb(T).InLobbyNow(); });
				T.Press(15); T.Wait(.2); T.Press(15); T.Wait(.2);
				T.Expect(TEXT("paintball: → → picks capture the flag"), [&T]() { return Pb(T).Session().Mode == TEXT("ctf"); });
				T.Hold(0, 1); T.Wait(1.4); T.Hold(0, 0);
				Until(T, TEXT("paintball: capture the flag started"), 9, [&T]() { return Pb(T).Session().State == TEXT("play") && Pb(T).Session().Mode == TEXT("ctf"); });
				// Take the blue flag (we are red) and bring it to our base.
				T.Do([&T]() { const FVector F = Pb(T).BaseFlag(0); T.Game()->Place(F.X + .5, F.Z, T.Game()->Player.Yaw); });
				Until(T, TEXT("paintball: we carry the blue flag"), 3, [&T]() { int32 By = -1; Pb(T).FlagPos(0, By); return By == 0 && Pb(T).Session().Flag[0].S == TEXT("carried"); });
				T.Do([&T]() { Look(T, Pb(T).World(0, Pb(T).Floor + 1.5, 0)); });
				T.Wait(.4);
				T.Shot(TEXT("09-flag"));
				T.Do([&T]() { double Y; const FVector P = Pb(T).SpawnPoint(1, 2, Y); T.Game()->Place(P.X, P.Z, Y); });
				Until(T, TEXT("paintball: capture for red"), 3, [&T]() { return Pb(T).Session().Caps[1] == 1 && Pb(T).Session().Flag[0].S == TEXT("home"); });
				T.Wait(.3);
				T.Shot(TEXT("10-capture"));
				// Leave the game: hold △.
				T.Hold(3, 1); T.Wait(2.4); T.Hold(3, 0);
				Until(T, TEXT("paintball: left the game, in the lobby"), 3, [&T]() { return !Pb(T).InGame() && Pb(T).InLobbyNow(); });
				T.Do([&T, S]()
				{
					T.Game()->PbOthers = nullptr; T.Game()->PbAvatarOf = nullptr;
					for (FFake& F : S->Fakes) if (F.Actor.IsValid()) F.Actor->Destroy();
					S->Fakes.Reset();
				});
			});
		}
	} GPaintballRegister;

	// Online (tools/net-test/run-paintball.ps1, test server only): the real game with seven bot clients
	// (tools/net-test/pb-bots.mjs) in one lobby: 8 players (the test server allows 12), a 4 v 4 team deathmatch.
	struct FRegisterOnline
	{
		FRegisterOnline()
		{
			FHKTestScripts::All().Add(TEXT("pbonline"), [](AHKTestDriver& T)
			{
				T.Wait(3);
				T.Press(0);
				Until(T, TEXT("pbonline: online in the lobby on the test server"), 60, [&T]() { return T.Game()->PbYou && T.Game()->PbYou() != 0; });
				T.Do([&T]() { const FHKPaintball& P = Pb(T); const FVector L = P.World(-6, 0, 29); T.Game()->Place(L.X, L.Z, P.WorldYaw(PI)); });
				Until(T, TEXT("pbonline: we are the paintball admin"), 10, [&T]() { return Pb(T).Member() && Pb(T).IsAdmin(); });
				Until(T, TEXT("pbonline: eight players in the paintball lobby"), 90, [&T]() { int32 N = 0; for (const FHKPbMember& M : Pb(T).Members()) if (M.Lobby > 0) N++; return N >= 8; });
				T.Wait(1.5);
				T.Shot(TEXT("01-online-lobby"));
				T.Hold(0, 1); T.Wait(1.4); T.Hold(0, 0);
				Until(T, TEXT("pbonline: 4 v 4 game started"), 5, [&T]() { const FHKPbSession& S = Pb(T).Session(); int32 N[2] = {0, 0}; for (const auto& KV : S.Team) N[KV.Value]++; return Pb(T).InGame() && N[0] == 4 && N[1] == 4; });
				Until(T, TEXT("pbonline: all bots in the game"), 10, [&T]() { int32 N = 0; for (const FHKPbMember& M : Pb(T).Members()) if (M.Ses == Pb(T).Session().Id) N++; return N >= 8; });
				Until(T, TEXT("pbonline: play"), 8, [&T]() { return Pb(T).Session().State == TEXT("play"); });
				T.Wait(2);
				T.Shot(TEXT("02-online-teams"));
				Until(T, TEXT("pbonline: a bot's ball put us out"), 30, [&T]() { return Pb(T).Session().Outs.FindRef(T.Game()->PbYou()) > 0; });
				T.Log([&T]() { return FString::Printf(TEXT("score blue %d : red %d"), Pb(T).Session().Score(0), Pb(T).Session().Score(1)); });
				Until(T, TEXT("pbonline: back in"), 8, [&T]() { return Pb(T).Alive(); });
				// Fire at the nearest bot of the other team until it reports being out.
				TSharedPtr<double> Fire = MakeShared<double>(0);
				TSharedPtr<int32> Target = MakeShared<int32>(0);
				T.Steps.Add([&T, Fire, Target](double Dt)
				{
					FHKPaintball& P = Pb(T);
					const int32 My = P.MyTeam();
					const FHKPbMember* Best = nullptr;
					for (const FHKPbMember& M : P.Members())
						if (!M.bLocal && M.bAlive && P.MemberPlaying(M.Id) && P.TeamOfMember(M.Id) == 1 - My &&
							(!Best || FVector::Dist(M.Feet, P.Ctx.Feet()) < FVector::Dist(Best->Feet, P.Ctx.Feet()))) Best = &M;
					if (!Best) return *Fire > 0;
					*Target = Best->Id;
					const FVector Chest = Best->Feet + FVector(0, 1.25, 0);
					Look(T, Chest);
					*Fire += Dt;
					if (FMath::Fmod(*Fire, .25) < Dt)
					{
						const FVector Eye = T.Game()->CamPos;
						P.TestFire((Chest + FVector(0, FMath::Square(FVector::Dist(Eye, Chest)) * .0012, 0) - Eye).GetSafeNormal());
					}
					return P.Session().Outs.FindRef(*Target) > 0 || *Fire > 12;
				});
				T.Shot(TEXT("03-online-firing"));
				Until(T, TEXT("pbonline: a bot reported our hit, we scored"), 5, [&T]() { const int32 My = Pb(T).MyTeam(); return My >= 0 && Pb(T).Session().Score(My) > 0; });
				T.Log([&T]() { return FString::Printf(TEXT("score blue %d : red %d"), Pb(T).Session().Score(0), Pb(T).Session().Score(1)); });
				T.Do([&T]() { Pb(T).TestSession([&T](FHKPbSession& Ses) { Ses.T0 = T.Game()->PbServerNow() - Ses.Dur * 1000; }); });
				Until(T, TEXT("pbonline: game over"), 4, [&T]() { return Pb(T).Session().State == TEXT("end"); });
				Until(T, TEXT("pbonline: everyone back in the lobby"), 16, [&T]() { int32 N = 0; for (const FHKPbMember& M : Pb(T).Members()) if (M.Lobby > 0 && M.Ses == 0) N++; return !Pb(T).InGame() && N >= 8; });
			});
		}
	} GPaintballOnlineRegister;
}
