// Live voice test: the Unreal client's voice (HKVoice: the web's game-voice.js in a hidden CEF page) and a web client
// (tools/net-test/web-client.mjs --voice, headless Chrome with a fake microphone) in one lobby on the test server.
// Unreal sends a 440 Hz test tone; both sides must connect a WebRTC audio channel and hear the other.
// Started by tools/net-test/run-voice.ps1 (needs rendering for CEF, so it runs with -game, not -nullrhi):
//   -HKLiveServer=<ws(s)://...> -HKLiveLobby=<CODE> -HKLiveOut=<folder>
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Haarklous.h"
#include "HKMultiplayer.h"
#include "HKNet.h"
#include "HKVoice.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FHKVoiceRun
	{
		FAutomationTestBase* Test = nullptr;
		FString Server, Lobby, OutDir, Mode;
		TUniquePtr<FHKMultiplayer> Mp;
		FHKWebVoice* Voice = nullptr;
		int32 WebId = 0;
		double T0 = 0, MaxLevel = 0, MaxBytesIn = 0, LastLog = 0;
		bool bMicAsked = false, bMicOk = false, bDoneSent = false, bWebDone = false, bWebHeard = false, bFinished = false;
		FString State;
		TArray<FString> LogLines;
		TMap<FString, double> Seen;
		TMap<FString, FString> Notes;
		TArray<FString> Names = {TEXT("online"), TEXT("sawWeb"), TEXT("voicePageReady"), TEXT("micOn"), TEXT("peerConnected"), TEXT("bytesIn"), TEXT("heardWeb"), TEXT("webHeardUnreal")};

		double Now() const { return FPlatformTime::Seconds() - T0; }
		void L(const FString& S)
		{
			const FString Line = FString::Printf(TEXT("%6.1f s  %s"), Now(), *S);
			LogLines.Add(Line);
			UE_LOG(LogHaarklous, Display, TEXT("[voice %s] %s"), *Mode, *Line);
		}
		void Check(const TCHAR* Name, bool bOk, const FString& Note = FString())
		{
			if (!bOk || Seen.Contains(Name)) return;
			Seen.Add(Name, Now());
			if (!Note.IsEmpty()) Notes.Add(Name, Note);
			L(FString::Printf(TEXT("ok: %s %s"), Name, *Note));
		}

		void Start()
		{
			T0 = FPlatformTime::Seconds();
			const FString Store = FPaths::ProjectSavedDir() / TEXT("Tests") / FString::Printf(TEXT("LocalStorage-voice-%s.json"), *Mode);
			IFileManager::Get().Delete(*Store);
			FHKStore::SetPathForTests(Store);
			FHKStore::Set(FHKNet::NameKey, TEXT("Unreal tester"));
			FHKWebVoiceOptions VO;
			VO.bTestTone = true;
			TUniquePtr<FHKWebVoice> V = MakeUnique<FHKWebVoice>(VO);
			Voice = V.Get();
			FHKMultiplayerContext Ctx;
			Ctx.LocalState = []() { FHKPlayerState S; S.P = FVector(0, -9.7, 32); return S; };
			Ctx.Notice = [this](const FString& T, double) { L(TEXT("notice: ") + T); };
			Ctx.OnChat = [this](const FHKChatLine& Line, bool bMine, bool)
			{
				if (bMine) return;
				L(FString::Printf(TEXT("chat %s: %s"), *Line.Name, *Line.Text));
				if (Line.Text.StartsWith(TEXT("voice-heard-unreal"))) { bWebHeard = true; Check(TEXT("webHeardUnreal"), true, Line.Text); }
				if (Line.Text == TEXT("done-web-voice")) bWebDone = true;
			};
			FHKNetOptions O = FHKNet::DefaultOptions();
			O.bEnabled = true;
			O.Base = Server;
			Mp = MakeUnique<FHKMultiplayer>(Ctx, O, MoveTemp(V));
			L(FString::Printf(TEXT("voice available: %d; connecting to %s/lobby/%s"), FHKWebVoice::Available(), *Server, *Lobby));
			Mp->Net().Connect(Lobby);
		}

		bool Step()
		{
			if (bFinished) return true;
			Mp->Update(1.0 / 30);
			Mp->EndFrame();
			FHKNet& Net = Mp->Net();
			Check(TEXT("online"), Net.Online() && Net.GetCode() == Lobby, FString::Printf(TEXT("id %d"), Net.You()));
			if (!WebId) for (const FHKNetPlayer& P : Net.GetPlayers()) if (P.Name == TEXT("Web tester")) { WebId = P.Id; Check(TEXT("sawWeb"), true, FString::Printf(TEXT("id %d"), P.Id)); }
			Check(TEXT("voicePageReady"), Voice->IsReady());
			if (WebId && Voice->IsReady() && !bMicAsked)
			{
				bMicAsked = true;
				Mp->SetMic(true, [this](bool bOk) { bMicOk = bOk; Check(TEXT("micOn"), bOk, TEXT("test tone 440 Hz")); if (!bOk) L(TEXT("mic could not be switched on")); });
			}
			if (WebId)
			{
				double BytesIn = 0;
				const double Level = Voice->PeerLevel(WebId, &State, &BytesIn);
				MaxLevel = FMath::Max(MaxLevel, Level);
				MaxBytesIn = FMath::Max(MaxBytesIn, BytesIn);
				Check(TEXT("peerConnected"), State == TEXT("connected"), State);
				Check(TEXT("bytesIn"), BytesIn > 2000, FString::Printf(TEXT("%.0f bytes"), BytesIn));
				Check(TEXT("heardWeb"), Level > .01, FString::Printf(TEXT("RMS %.3f"), Level));
				if (Now() - LastLog > 5 && Voice->LastStats.IsValid()) { LastLog = Now(); L(TEXT("stats ") + HKJsonWire::Write(Voice->LastStats)); }
			}
			bool bAll = true;
			for (const FString& N : Names) bAll &= Seen.Contains(N);
			if (Seen.Contains(TEXT("heardWeb")) && !bDoneSent) { bDoneSent = true; Mp->SendChat(FString::Printf(TEXT("voice-heard-web %.3f"), MaxLevel)); }
			if (bAll && bWebDone) { Finish(); return true; }
			if (Now() > 240) { Finish(); return true; }
			return false;
		}

		void Finish()
		{
			bFinished = true;
			Mp->SendChat(TEXT("done-unreal-voice"));
			TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>(), C = MakeShared<FJsonObject>();
			for (const FString& N : Names)
			{
				const double* T = Seen.Find(N);
				if (!T) Test->AddError(FString::Printf(TEXT("voice %s: never saw '%s'"), *Mode, *N));
				TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
				E->SetBoolField(TEXT("ok"), T != nullptr);
				if (T) E->SetNumberField(TEXT("seconds"), *T);
				if (const FString* Note = Notes.Find(N)) E->SetStringField(TEXT("note"), *Note);
				C->SetObjectField(N, E);
			}
			J->SetStringField(TEXT("server"), Server); J->SetStringField(TEXT("lobby"), Lobby); J->SetObjectField(TEXT("checks"), C);
			J->SetNumberField(TEXT("maxLevel"), MaxLevel); J->SetNumberField(TEXT("maxBytesIn"), MaxBytesIn);
			if (Voice->LastStats.IsValid()) J->SetObjectField(TEXT("lastStats"), Voice->LastStats);
			TArray<TSharedPtr<FJsonValue>> Lines;
			for (const FString& S : LogLines) Lines.Add(MakeShared<FJsonValueString>(S));
			J->SetArrayField(TEXT("log"), Lines);
			if (!OutDir.IsEmpty())
			{
				IFileManager::Get().MakeDirectory(*OutDir, true);
				FFileHelper::SaveStringToFile(HKJsonWire::Write(J), *(OutDir / FString::Printf(TEXT("unreal-voice-%s.json"), *Mode)), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			}
			Test->AddInfo(FString::Printf(TEXT("voice %s: %d / %d checks seen, max received RMS %.3f"), *Mode, Seen.Num(), Names.Num(), MaxLevel));
		}
		void Close()
		{
			Voice = nullptr;
			Mp.Reset();
			FHKStore::SetPathForTests(FString());
		}
	};

	class FHKVoiceCommand : public IAutomationLatentCommand
	{
	public:
		explicit FHKVoiceCommand(TSharedPtr<FHKVoiceRun> InRun) : Run(InRun) {}
		virtual bool Update() override
		{
			if (!Run->bFinished) { Run->Step(); return false; }
			// Keep the connection a few seconds so the web client sees the end, then close.
			if (!Done) { Done = FPlatformTime::Seconds(); return false; }
			if (FPlatformTime::Seconds() - Done < 3) { Run->Mp->Update(1.0 / 30); return false; }
			Run->Close();
			return true;
		}
	private:
		TSharedPtr<FHKVoiceRun> Run;
		double Done = 0;
	};

	// The voice page runs the web game's own game-voice.js: it must stay identical to the reference copy.
	bool SameAsReference(FAutomationTestBase& Test)
	{
		FString Mine, Ref;
		const FString RefPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("..") / TEXT("..") / TEXT("reference") / TEXT("web-game") / TEXT("game-voice.js"));
		if (!FFileHelper::LoadFileToString(Mine, *(FPaths::ProjectContentDir() / TEXT("Web") / TEXT("voice") / TEXT("game-voice.js")))) { Test.AddError(TEXT("Content/Web/voice/game-voice.js is missing")); return false; }
		if (!FFileHelper::LoadFileToString(Ref, *RefPath)) { Test.AddInfo(TEXT("reference/web-game/game-voice.js not found; copy not compared")); return true; }
		if (Mine.Replace(TEXT("\r\n"), TEXT("\n")) != Ref.Replace(TEXT("\r\n"), TEXT("\n"))) { Test.AddError(TEXT("Content/Web/voice/game-voice.js differs from reference/web-game/game-voice.js")); return false; }
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKNetVoicePageTest, "Haarklous.Net.VoicePage", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKNetVoicePageTest::RunTest(const FString& Parameters)
{
	return SameAsReference(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKNetVoiceTest, "Haarklous.Net.Voice", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKNetVoiceTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FHKVoiceRun> Run = MakeShared<FHKVoiceRun>();
	Run->Test = this;
	const TCHAR* Cmd = FCommandLine::Get();
	if (!FParse::Value(Cmd, TEXT("HKLiveServer="), Run->Server, false) || !FParse::Param(Cmd, TEXT("HKVoiceTest")))
	{
		AddInfo(TEXT("Not run: needs -HKVoiceTest -HKLiveServer= (tools/net-test/run-voice.ps1)."));
		return true;
	}
	if (FHKNet::IsLiveServer(Run->Server)) { AddError(TEXT("Refusing to test against the live game server.")); return false; }
	FParse::Value(Cmd, TEXT("HKLiveLobby="), Run->Lobby, false);
	FParse::Value(Cmd, TEXT("HKLiveOut="), Run->OutDir, false);
	Run->Mode = Run->Server.StartsWith(TEXT("ws://")) ? TEXT("local") : TEXT("deployed");
	if (!FHKWebVoice::Available()) { AddError(TEXT("No browser engine in this process (CEF needs rendering: run with -game, not -nullrhi).")); return false; }
	Run->Start();
	ADD_LATENT_AUTOMATION_COMMAND(FHKVoiceCommand(Run));
	return true;
}

#endif
