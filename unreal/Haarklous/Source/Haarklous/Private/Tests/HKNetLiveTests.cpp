// Live online test: the C++ client and a web client (tools/net-test/web-client.mjs, the real web game in headless
// Chrome) in one lobby on the separate test server. Both must see each other's join, states, chat lines, seat claims
// and occupancy, doors and the fire level. Started by tools/net-test/run-live.ps1, which passes
//   -HKLiveServer=ws://127.0.0.1:8787 (npx wrangler dev) or wss://haarklous-ue5-test.server-smoke.workers.dev
//   -HKLiveLobby=<CODE> -HKLiveOut=<folder for the summary>
// Without -HKLiveServer the test does nothing (so normal test runs never go online).
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Haarklous.h"
#include "HKData.h"
#include "HKAvatars.h"
#include "HKMultiplayer.h"
#include "HKNet.h"
#include "HKVehicle.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// A stand-in for the web's vehicle list (41 vehicles, same indices): only the network state is kept.
	class FHKLiveVehicle : public FHKVehicle
	{
	public:
		TArray<double> State;
		virtual void Update(double Dt, const FHKVehicleInput& In) override {}
		virtual FHKSeatPose Seat() override { return FHKSeatPose(); }
		virtual void Enter() override {}
		virtual FVector2D Exit() override { return FVector2D::ZeroVector; }
		virtual TArray<FHKObstacle> Footprint() const override { return {}; }
		virtual TArray<double> NetGet() const override { return State; }
		virtual void NetSet(const TArray<double>& A) override { State = A; }
		virtual FHKHipAnchor HipAnchor(const FString& S) override { return FHKHipAnchor(); }
	};

	// Passes everything through and lets the test see the raw lobby messages (for the shared-fire check).
	class FHKTapSocket : public IHKSocket
	{
	public:
		FHKTapSocket(TSharedPtr<IHKSocket> InInner, TFunction<void(const FString&)> InTap) : Inner(InInner), Tap(MoveTemp(InTap)) {}
		virtual void Connect() override
		{
			Inner->OnOpen = [this]() { if (OnOpen) OnOpen(); };
			Inner->OnMessage = [this](const FString& T) { Tap(T); if (OnMessage) OnMessage(T); };
			Inner->OnClose = [this](int32 Code) { if (OnClose) OnClose(Code); };
			Inner->Connect();
		}
		virtual void Send(const FString& Text) override { Inner->Send(Text); }
		virtual void Close(int32 Code) override { Inner->Close(Code); }
		virtual bool IsOpen() const override { return Inner->IsOpen(); }
	private:
		TSharedPtr<IHKSocket> Inner;
		TFunction<void(const FString&)> Tap;
	};

	struct FHKLiveRun
	{
		FAutomationTestBase* Test = nullptr;
		FString Server, Lobby, OutDir, Mode;
		TUniquePtr<FHKMultiplayer> Mp;
		TArray<TUniquePtr<FHKLiveVehicle>> Vehicles;
		FHKPlayerState Local;
		int32 Driving = -1, WebId = 0, VsFrames = 0;
		double T0 = 0, Last = 0, ChatSentAt = 0, DriveT = 0;
		bool bOnline = false, bActed = false, bClaim5Sent = false, bClaim3Sent = false, bDoneSent = false, bWebDone = false, bFinished = false;
		TArray<FString> LogLines;
		TArray<FString> Names = {TEXT("online"), TEXT("sawWeb"), TEXT("webState"), TEXT("webChat"), TEXT("webDoor"), TEXT("webWood"), TEXT("webClaim5Seen"), TEXT("claim5Refused"),
			TEXT("claim3Ok"), TEXT("fireShared"), TEXT("ownChatEcho"), TEXT("webAvatarVisible")};
		TMap<FString, double> Seen;
		TMap<FString, FString> Notes;
		struct FFireMsg { int32 By; double Level, T; };
		TArray<FFireMsg> FireMsgs;
		TArray<double> PingMs;              // round trips measured while states and vehicle updates flow
		double ClaimSentAt = 0, NextPing = 0;
		int32 PingsSent = 0;

		double Now() const { return FPlatformTime::Seconds() - T0; }
		void L(const FString& S)
		{
			const FString Line = FString::Printf(TEXT("%6.1f s  %s"), Now(), *S);
			LogLines.Add(Line);
			UE_LOG(LogHaarklous, Display, TEXT("[live %s] %s"), *Mode, *Line);
		}
		void Check(const TCHAR* Name, bool bOk, const FString& Note = FString())
		{
			if (!bOk || Seen.Contains(Name)) return;
			Seen.Add(Name, Now());
			if (!Note.IsEmpty()) Notes.Add(Name, Note);
			L(FString::Printf(TEXT("ok: %s %s"), Name, *Note));
		}
		static double H(double X, double Z) { return FHKData::Get().bLoaded ? FHKData::Get().HeightWorld(X, Z) : -10; }

		void Start()
		{
			T0 = Last = FPlatformTime::Seconds();
			for (int32 i = 0; i < 41; i++) Vehicles.Add(MakeUnique<FHKLiveVehicle>());
			// The player stands near the firepit (web frame).
			Local.P = FVector(0, FHKPlayerState::R(H(0, 32)), 32); Local.Y = 1.2; Local.St = TEXT("stand");
			const FString Store = FPaths::ProjectSavedDir() / TEXT("Tests") / FString::Printf(TEXT("LocalStorage-live-%s.json"), *Mode);
			IFileManager::Get().Delete(*Store);
			FHKStore::SetPathForTests(Store);
			FHKStore::Set(FHKNet::NameKey, TEXT("Unreal tester"));
			FHKMultiplayerContext Ctx;
			Ctx.NumVehicles = [this]() { return Vehicles.Num(); };
			Ctx.Vehicle = [this](int32 I) -> FHKVehicle* { return Vehicles.IsValidIndex(I) ? Vehicles[I].Get() : nullptr; };
			Ctx.LocalState = [this]() { return Local; };
			Ctx.DrivingIndex = [this]() { return Driving; };
			Ctx.CameraPosition = [this]() { return Local.P + FVector(0, 1.6, 0); };
			Ctx.Notice = [this](const FString& T, double S) { L(TEXT("notice: ") + T); };
			Ctx.ApplyDoor = [this](const FString& Id, bool bOpen, bool bInstant)
			{
				L(FString::Printf(TEXT("door %s %s%s"), *Id, bOpen ? TEXT("open") : TEXT("closed"), bInstant ? TEXT(" (lobby state)") : TEXT("")));
				Check(TEXT("webDoor"), Id == TEXT("W09-0") && bOpen && !bInstant, Id);
			};
			Ctx.OnChat = [this](const FHKChatLine& Line, bool bMine, bool bOld)
			{
				L(FString::Printf(TEXT("chat %s%s: %s"), *Line.Name, bMine ? TEXT(" (me)") : TEXT(""), *Line.Text));
				Check(TEXT("webChat"), !bMine && Line.Id == WebId && Line.Text == TEXT("hello from web"), Line.Name);
				if (bMine && Line.Text == TEXT("hello from unreal")) Check(TEXT("ownChatEcho"), true, FString::Printf(TEXT("%.0f ms round trip"), (FPlatformTime::Seconds() - ChatSentAt) * 1000));
				if (!bMine && Line.Text == TEXT("done-web")) bWebDone = true;
			};
			Ctx.OnStatus = [this]() { if (Mp) L(TEXT("status: ") + Mp->Net().StatusText()); };
			Ctx.OnRemoteWood = [this](const FHKRemote& R) { L(FString::Printf(TEXT("%s threw a log on the fire"), *R.Name)); Check(TEXT("webWood"), R.Id == WebId, R.Name); };
			FHKNetOptions O = FHKNet::DefaultOptions();
			O.bEnabled = true;
			O.Base = Server;
			O.MakeSocket = [this, Inner = O.MakeSocket](const FString& Url) -> TSharedPtr<IHKSocket>
			{
				TSharedPtr<IHKSocket> S = Inner(Url);
				if (!S) return nullptr;
				return MakeShared<FHKTapSocket>(S, [this](const FString& Text)
				{
					const TSharedPtr<FJsonObject> M = HKJsonWire::Parse(Text);
					if (M && HKJsonWire::Str(M, TEXT("t")) == TEXT("pong") && Mp) PingMs.Add(Mp->Net().DateNow() - HKJsonWire::Num(M, TEXT("c")));
					const TSharedPtr<FJsonObject>* F = nullptr;
					if (M && HKJsonWire::Str(M, TEXT("t")) == TEXT("fire") && M->TryGetObjectField(TEXT("fire"), F))
						FireMsgs.Add({(int32)HKJsonWire::Num(M, TEXT("by")), HKJsonWire::Num(*F, TEXT("level")), HKJsonWire::Num(*F, TEXT("t"))});
				});
			};
			Mp = MakeUnique<FHKMultiplayer>(Ctx, O);
			L(FString::Printf(TEXT("connecting to %s/lobby/%s as '%s'"), *Server, *Lobby, *Mp->Net().GetName()));
			if (!Mp->Net().Connect(Lobby)) L(TEXT("bad lobby code ") + Lobby);
		}

		bool Step()
		{
			if (bFinished) return true;
			const double Clock = FPlatformTime::Seconds(), Dt = FMath::Clamp(Clock - Last, 0.0, .1);
			Last = Clock;
			FHKNet& Net = Mp->Net();
			// Drive car 3 slowly from (0, 34) to (4, 38) once the seat is ours (web frame, car netGet layout).
			if (Driving == 3)
			{
				DriveT = FMath::Min(DriveT + Dt, 4.0);
				const double X = DriveT, Z = 34 + DriveT;
				Vehicles[3]->State = {FHKPlayerState::Q(X), FHKPlayerState::Q(H(X, Z)), FHKPlayerState::Q(Z), FHKPlayerState::Q(-HK::Pi / 4), 0, 0, DriveT < 4 ? 1.414 : 0, 0};
				VsFrames++;
				Local.V = 3; Local.S = TEXT("d");
			}
			Mp->Update(Dt);
			Mp->EndFrame();
			if (Net.Online() && !bOnline) { bOnline = true; Check(TEXT("online"), Net.GetCode() == Lobby, FString::Printf(TEXT("lobby %s, id %d, slot %d, clock offset %.0f ms"), *Net.GetCode(), Net.You(), Net.GetSlot(), Net.ServerNow() - Net.DateNow())); }
			if (!WebId) for (const FHKNetPlayer& P : Net.GetPlayers()) if (P.Name == TEXT("Web tester")) { WebId = P.Id; Check(TEXT("sawWeb"), true, FString::Printf(TEXT("id %d slot %d"), P.Id, P.Slot)); }
			if (WebId && !bActed)
			{
				bActed = true;
				ChatSentAt = FPlatformTime::Seconds();
				Mp->SendChat(TEXT("hello from unreal"));
				Mp->SendDoor(TEXT("W05-0"), true);
				Mp->AddWood();
				L(FString::Printf(TEXT("sent chat, opened door W05-0, added wood (fire now %.2f)"), Mp->FireLevel()));
			}
			if (const FHKRemote* R = WebId ? Mp->Remote(WebId) : nullptr)
			{
				if (R->bHasSt) Check(TEXT("webState"), FMath::Abs(R->St.P.X + 4) < 2 && FMath::Abs(R->St.P.Z - 30) < 2, FString::Printf(TEXT("p = (%.2f, %.2f, %.2f) yaw %.2f stance %s"), R->St.P.X, R->St.P.Y, R->St.P.Z, R->St.Y, *R->St.St));
				Check(TEXT("webAvatarVisible"), R->bSeen && R->Av->Group()->bVisible, FString::Printf(TEXT("avatar at (%.2f, %.2f, %.2f), %s rig"), R->Pos.X, R->Pos.Y, R->Pos.Z, *R->Av->RigName));
			}
			if (WebId && Mp->Occupant(5, TEXT("d")) == WebId)
			{
				Check(TEXT("webClaim5Seen"), true, TEXT("vehicle 5 driver seat"));
				if (!bClaim5Sent)
				{
					bClaim5Sent = true;
					ClaimSentAt = FPlatformTime::Seconds();
					Mp->Claim(5, TEXT("d"), [this](const FHKClaimResult& Res)
					{
						Check(TEXT("claim5Refused"), !Res.bOk && Res.By == WebId, FString::Printf(TEXT("ok=%d by=%d, answer after %.0f ms"), Res.bOk, Res.By, (FPlatformTime::Seconds() - ClaimSentAt) * 1000));
						if (!bClaim3Sent)
						{
							bClaim3Sent = true;
							ClaimSentAt = FPlatformTime::Seconds();
							Mp->Claim(3, TEXT("d"), [this](const FHKClaimResult& R3)
							{
								Check(TEXT("claim3Ok"), R3.bOk, FString::Printf(TEXT("ok=%d by=%d, answer after %.0f ms"), R3.bOk, R3.By, (FPlatformTime::Seconds() - ClaimSentAt) * 1000));
								if (R3.bOk) { Driving = 3; L(TEXT("driving vehicle 3")); }
							});
						}
					});
				}
			}
			// Both logs count and both clients agree: the lobby's fire messages by both players, the second one level + 1.
			if (WebId && FireMsgs.ContainsByPredicate([this](const FFireMsg& F) { return F.By == WebId; }) && FireMsgs.ContainsByPredicate([&Net](const FFireMsg& F) { return F.By == Net.You(); }))
			{
				const FFireMsg& A = FireMsgs[0];
				const FFireMsg& B = FireMsgs.Last();
				const double Expect = FMath::Min(6.0, FMath::Max(0.0, A.Level - (B.T - A.T) / 90000) + 1);
				const double NowLevel = FMath::Max(0.0, B.Level - (Net.ServerNow() - B.T) / 90000);
				FString Note;
				for (const FFireMsg& F : FireMsgs) Note += FString::Printf(TEXT("%.3f by %d, "), F.Level, F.By);
				Check(TEXT("fireShared"), FMath::Abs(B.Level - Expect) < .01 && FMath::Abs(Mp->FireLevel() - NowLevel) < .01, TEXT("levels ") + Note + FString::Printf(TEXT("now %.3f"), Mp->FireLevel()));
			}
			// Round trips while our states (10 Hz) and the car's updates (12 Hz) are flowing.
			if (Driving == 3 && PingsSent < 8 && Now() >= NextPing)
			{
				NextPing = Now() + .5;
				PingsSent++;
				TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
				P->SetStringField(TEXT("t"), TEXT("ping")); P->SetNumberField(TEXT("c"), Net.DateNow());
				Net.Send(P);
			}
			bool bAll = PingMs.Num() >= 8;
			for (const FString& N : Names) bAll &= Seen.Contains(N);
			if (bAll && DriveT >= 4 && !bDoneSent) { bDoneSent = true; Mp->SendChat(TEXT("done-unreal")); L(TEXT("all checks seen; told the web client")); }
			if ((bDoneSent && bWebDone && Now() > 1) || Now() > 240)
			{
				// Linger a moment so the last messages leave, then report.
				Finish(bAll);
				return true;
			}
			return false;
		}

		void Finish(bool bAll)
		{
			bFinished = true;
			TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>(), C = MakeShared<FJsonObject>();
			for (const FString& N : Names)
			{
				const double* T = Seen.Find(N);
				if (!T) Test->AddError(FString::Printf(TEXT("live %s: never saw '%s'"), *Mode, *N));
				TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
				E->SetBoolField(TEXT("ok"), T != nullptr);
				if (T) E->SetNumberField(TEXT("seconds"), *T);
				if (const FString* Note = Notes.Find(N)) E->SetStringField(TEXT("note"), *Note);
				C->SetObjectField(N, E);
			}
			if (!bWebDone) Test->AddError(TEXT("the web client never reported done"));
			J->SetStringField(TEXT("server"), Server); J->SetStringField(TEXT("lobby"), Lobby); J->SetObjectField(TEXT("checks"), C);
			J->SetNumberField(TEXT("vsFrames"), VsFrames); J->SetBoolField(TEXT("webDone"), bWebDone);
			J->SetField(TEXT("pingMs"), HKJsonWire::NumArray(PingMs));
			if (PingMs.Num())
			{
				TArray<double> S = PingMs;
				S.Sort();
				L(FString::Printf(TEXT("round trips under load: median %.0f ms, max %.0f ms (%d pings)"), S[S.Num() / 2], S.Last(), S.Num()));
			}
			TArray<TSharedPtr<FJsonValue>> Lines;
			for (const FString& S : LogLines) Lines.Add(MakeShared<FJsonValueString>(S));
			J->SetArrayField(TEXT("log"), Lines);
			if (!OutDir.IsEmpty())
			{
				IFileManager::Get().MakeDirectory(*OutDir, true);
				FFileHelper::SaveStringToFile(HKJsonWire::Write(J), *(OutDir / FString::Printf(TEXT("unreal-client-%s.json"), *Mode)), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			}
			Test->AddInfo(FString::Printf(TEXT("live %s: %d / %d checks seen (web done: %d)"), *Mode, Seen.Num(), Names.Num(), bWebDone));
			Mp.Reset();
			FHKStore::SetPathForTests(FString());
		}
	};

	class FHKLiveCommand : public IAutomationLatentCommand
	{
	public:
		explicit FHKLiveCommand(TSharedPtr<FHKLiveRun> InRun) : Run(InRun) {}
		virtual bool Update() override { return Run->Step(); }
	private:
		TSharedPtr<FHKLiveRun> Run;
	};
}

namespace
{
	// A full lobby: plain connections fill a fresh lobby (5 on the live server; the test server holds 12 since
	// 28 Oct 2026, so the number comes from the first one's welcome), one more (a full FHKMultiplayer) must be refused
	// and land back in its own lobby with the web's notice.
	struct FHKFullRun
	{
		FAutomationTestBase* Test = nullptr;
		FString Server, Lobby;
		TArray<TUniquePtr<FHKNet>> Fillers;
		TUniquePtr<FHKMultiplayer> Sixth;
		FHKNetOptions Opt;
		int32 Max = 0;
		TArray<FString> Notices;
		double T0 = 0;
		bool bSixthStarted = false, bDone = false;
		bool Step()
		{
			const double T = FPlatformTime::Seconds() - T0;
			int32 Online = 0;
			for (const TUniquePtr<FHKNet>& N : Fillers) Online += N->Online() ? 1 : 0;
			if (!Max && Online == 1)
			{
				Max = Fillers[0]->Max();
				for (int32 i = 1; i < Max; i++)
				{
					Fillers.Add(MakeUnique<FHKNet>(nullptr, nullptr, Opt));
					Fillers.Last()->SetName(FString::Printf(TEXT("Filler %d"), i + 1));
					Fillers.Last()->Connect(Lobby);
				}
			}
			if (Max && Online == Max && !bSixthStarted)
			{
				bSixthStarted = true;
				FHKMultiplayerContext Ctx;
				Ctx.Notice = [this](const FString& S, double) { Notices.Add(S); UE_LOG(LogHaarklous, Display, TEXT("[live full] notice: %s"), *S); };
				FHKNetOptions O = FHKNet::DefaultOptions();
				O.bEnabled = true; O.Base = Server;
				Sixth = MakeUnique<FHKMultiplayer>(Ctx, O);
				UE_LOG(LogHaarklous, Display, TEXT("[live full] lobby %s has %d players; one more (own lobby %s) tries to join"), *Lobby, Max, *Sixth->Net().GetOwnCode());
				Sixth->Net().Connect(Lobby);
			}
			if (Sixth) Sixth->Update(1.0 / 30);
			const bool bBack = Sixth && Sixth->Net().Online() && Sixth->Net().GetCode() == Sixth->Net().GetOwnCode();
			if (bBack || T > 60)
			{
				const FString Want = FString::Printf(TEXT("Lobby %s is full (%d players). Back in your own lobby."), *Lobby, Max);
				Test->TestTrue(TEXT("the fillers got in"), Max > 0 && Online == Max);
				Test->TestTrue(TEXT("the sixth is back in its own lobby"), bBack);
				Test->TestTrue(TEXT("the web's notice"), Notices.Contains(Want));
				if (Sixth) Test->AddInfo(FString::Printf(TEXT("live full lobby: refused after %.1f s, now %s"), T, *Sixth->Net().StatusText()));
				Sixth.Reset();
				Fillers.Reset();
				FHKStore::SetPathForTests(FString());
				return true;
			}
			return false;
		}
	};

	class FHKFullCommand : public IAutomationLatentCommand
	{
	public:
		explicit FHKFullCommand(TSharedPtr<FHKFullRun> InRun) : Run(InRun) {}
		virtual bool Update() override { return Run->Step(); }
	private:
		TSharedPtr<FHKFullRun> Run;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKNetLiveFullTest, "Haarklous.Net.LiveFull", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKNetLiveFullTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FHKFullRun> Run = MakeShared<FHKFullRun>();
	Run->Test = this;
	if (!FParse::Value(FCommandLine::Get(), TEXT("HKLiveServer="), Run->Server, false)) { AddInfo(TEXT("Not run: needs -HKLiveServer= (tools/net-test/run-live.ps1).")); return true; }
	if (FHKNet::IsLiveServer(Run->Server)) { AddError(TEXT("Refusing to test against the live game server.")); return false; }
	FHKStore::SetPathForTests(FPaths::ProjectSavedDir() / TEXT("Tests") / TEXT("LocalStorage-live-full.json"));
	Run->Lobby = FHKNet::RandomCode([]() { return (double)FMath::FRand(); });
	FHKNetOptions O = FHKNet::DefaultOptions();
	O.bEnabled = true; O.Base = Run->Server;
	Run->Opt = O;
	Run->Fillers.Add(MakeUnique<FHKNet>(nullptr, nullptr, O));
	Run->Fillers.Last()->SetName(TEXT("Filler 1"));
	Run->Fillers.Last()->Connect(Run->Lobby);
	Run->T0 = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FHKFullCommand(Run));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKNetLiveTest, "Haarklous.Net.Live", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKNetLiveTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FHKLiveRun> Run = MakeShared<FHKLiveRun>();
	Run->Test = this;
	const TCHAR* Cmd = FCommandLine::Get();
	if (!FParse::Value(Cmd, TEXT("HKLiveServer="), Run->Server, false))
	{
		AddInfo(TEXT("Not run: needs -HKLiveServer= (tools/net-test/run-live.ps1)."));
		return true;
	}
	FParse::Value(Cmd, TEXT("HKLiveLobby="), Run->Lobby, false);
	FParse::Value(Cmd, TEXT("HKLiveOut="), Run->OutDir, false);
	Run->Mode = Run->Server.StartsWith(TEXT("ws://127.0.0.1")) || Run->Server.StartsWith(TEXT("ws://localhost")) ? TEXT("local") : TEXT("deployed");
	if (FHKNet::IsLiveServer(Run->Server)) { AddError(TEXT("Refusing to test against the live game server.")); return false; }
	Run->Start();
	ADD_LATENT_AUTOMATION_COMMAND(FHKLiveCommand(Run));
	return true;
}

#endif
