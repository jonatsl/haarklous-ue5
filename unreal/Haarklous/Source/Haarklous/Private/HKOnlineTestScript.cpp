// Scripted play-through "online" (-hktest=online): the real game joins a lobby on the test server together with a web
// client (tools/net-test/run-live.ps1 -Game, which also passes -HKOnline -HKServer=... -HKLobby=...). Checks that the
// game's own player state goes out, the web player shows up (avatar, name tag), chat lines reach the game's chat log,
// the web player's door and log on the fire arrive, and measures the frame rate with a remote player around.
#include "HKTestDriver.h"
#include "Haarklous.h"
#include "HKData.h"
#include "HKGame.h"
#include "HKMath.h"
#include "HKMultiplayer.h"
#include "HKOnlineGame.h"
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
	// A step that waits for a condition (at most Seconds), then logs PASS or FAIL like Expect().
	void WaitFor(AHKTestDriver& T, const FString& What, double Seconds, TFunction<bool()> Fn)
	{
		TSharedPtr<double> Start = MakeShared<double>(-1);
		T.Steps.Add([&T, What, Seconds, Fn, Start](double)
		{
			const double Now = FPlatformTime::Seconds();
			if (*Start < 0) *Start = Now;
			const bool bOk = Fn();
			if (!bOk && Now - *Start < Seconds) return false;
			if (!bOk) T.Failures++;
			UE_LOG(LogHaarklous, Display, TEXT("HK-TEST %s %s (%.1f s)  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What, Now - *Start, *T.P());
			return true;
		});
	}
	bool ChatHas(AHKTestDriver& T, const FString& Text)
	{
		for (const AHKGame::FChatLine& L : T.Game()->ChatLog) if (L.Text.Contains(Text)) return true;
		return false;
	}

	struct FRegisterOnline
	{
		FRegisterOnline()
		{
			FHKTestScripts::All().Add(TEXT("online"), [](AHKTestDriver& T)
			{
				// Until AHKGame::SetupModules() calls AHKOnline::Setup(this) itself.
				T.Do([&T]() { if (!FindOnline(T)) { AHKOnline::Setup(T.Game()); UE_LOG(LogHaarklous, Display, TEXT("HK-TEST online module set up by the test")); } });
				T.Do([&T]() { if (FHKMultiplayer* M = MpOf(T)) M->Net().SetName(TEXT("Unreal tester")); });
				T.Wait(3);
				T.Press(0);   // start: begin() -> mp.net.connect(?lobby) or home()
				T.Wait(1);
				T.Do([&T]() { if (FHKMultiplayer* M = MpOf(T)) if (M->Net().GetStatus() == TEXT("offline")) M->Net().Begin(); });
				WaitFor(T, TEXT("online in the test lobby"), 60, [&T]() { FHKMultiplayer* M = MpOf(T); return M && M->Net().Online(); });
				WaitFor(T, TEXT("the web player is in the lobby"), 240, [&T]() { return WebId(T) != 0; });
				// Stand near the firepit, facing where the web player stands (-4, 30).
				T.Do([&T]() { T.Game()->Place(-1, 32.5, FMath::Atan2(-(-4 - -1), -(30 - 32.5))); });
				T.Do([&T]() { if (FHKMultiplayer* M = MpOf(T)) M->SendChat(TEXT("hello from the game")); });
				WaitFor(T, TEXT("the web player's chat line in the game's chat log"), 60, [&T]() { return ChatHas(T, TEXT("hello from web")); });
				WaitFor(T, TEXT("the web player is visible (avatar placed from their state)"), 60, [&T]()
				{
					FHKMultiplayer* M = MpOf(T);
					const FHKRemote* R = M ? M->Remote(WebId(T)) : nullptr;
					return R && R->bSeen && R->Av->Group()->bVisible && HK::Hypot(R->Pos.X + 4, R->Pos.Z - 30) < 2;
				});
				WaitFor(T, TEXT("the web player's door W09-0 opened here"), 30, [&T]()
				{
					const FHKDoor* D = T.Game()->Doors.FindByPredicate([](const FHKDoor& X) { return X.Id == TEXT("W09-0"); });
					return D && D->bOpen;
				});
				WaitFor(T, TEXT("vehicle 5 is taken by the web player"), 30, [&T]() { return T.Game()->Occupant && T.Game()->Occupant(5, TEXT("d")) == WebId(T) && WebId(T) != 0; });
				T.Log([&T]() { return FString::Printf(TEXT("fire level %.2f, players %s"), T.Game()->FireLevel(), MpOf(T) ? *MpOf(T)->Net().StatusText() : TEXT("-")); });
				T.Wait(1);
				T.Shot(TEXT("online-web-player"));
				T.MeasureFps(TEXT("online, one remote player in view"), 8);
				T.Do([&T]() { if (FHKMultiplayer* M = MpOf(T)) M->SendChat(TEXT("done-game")); });
				WaitFor(T, TEXT("the web client finished"), 60, [&T]() { return ChatHas(T, TEXT("done-web")); });
			});
		}
	} GRegisterOnline;
}
