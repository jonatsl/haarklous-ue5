#include "HKVoice.h"
#include "Haarklous.h"
#include "HKNet.h"
#include "AudioCaptureCore.h"
#include "IWebBrowserSingleton.h"
#include "IWebBrowserWindow.h"
#include "WebBrowserModule.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Misc/App.h"
#include "Misc/Base64.h"
#include "Misc/Paths.h"

void UHKVoiceBridge::ready() { if (OnReady) OnReady(); }
void UHKVoiceBridge::signal(int32 To, const FString& Json) { if (OnSignal) OnSignal(To, Json); }
void UHKVoiceBridge::micresult(bool bOk, bool bOn) { if (OnMicResult) OnMicResult(bOk, bOn); }
void UHKVoiceBridge::stats(const FString& Json) { if (OnStats) OnStats(Json); }
void UHKVoiceBridge::log(const FString& Text) { UE_LOG(LogHaarklous, Log, TEXT("Voice page: %s"), *Text); }

namespace
{
	constexpr double PageRate = 48000;   // the page's AudioContext
}

bool FHKWebVoice::Available()
{
	return !IsRunningCommandlet() && FApp::CanEverRender() && IWebBrowserModule::IsAvailable() && IWebBrowserModule::Get().IsWebModuleAvailable() &&
		IWebBrowserModule::Get().GetSingleton() != nullptr;
}

FString FHKWebVoice::DefaultPage()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Web") / TEXT("voice") / TEXT("index.html"));
}

FHKWebVoice::FHKWebVoice(const FHKWebVoiceOptions& InOptions) : Options(InOptions)
{
	Bridge = TStrongObjectPtr<UHKVoiceBridge>(NewObject<UHKVoiceBridge>());
	Bridge->OnReady = [this]()
	{
		bReady = true;
		UE_LOG(LogHaarklous, Log, TEXT("Voice page ready (%d calls waiting)"), Pending.Num());
		for (const FString& S : Pending) if (Window.IsValid()) Window->ExecuteJavascript(S);
		Pending.Reset();
	};
	Bridge->OnSignal = [this](int32 To, const FString& Json)
	{
		TSharedPtr<FJsonValue> D;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), D) || !D.IsValid()) return;
		if (SendSignal) SendSignal(To, D);
	};
	Bridge->OnMicResult = [this](bool bOk, bool bOn)
	{
		bMic = bOn;
		if (!bMic && Capture) { Capture->StopCapturing(); Capture.Reset(); }
		if (MicWaiters.Num()) { TFunction<void(bool)> Done = MicWaiters[0]; MicWaiters.RemoveAt(0); if (Done) Done(bOk); }
	};
	Bridge->OnStats = [this](const FString& Json)
	{
		LastStats = HKJsonWire::Parse(Json);
		LastStatsAt = FPlatformTime::Seconds();
	};
	if (!Available()) { UE_LOG(LogHaarklous, Warning, TEXT("Voice chat is not available here (no browser engine)")); return; }
	FCreateBrowserWindowSettings S;
	S.InitialURL = TEXT("file:///") + (Options.PagePath.IsEmpty() ? DefaultPage() : Options.PagePath).Replace(TEXT("\\"), TEXT("/"));
	S.BrowserFrameRate = 1;
	S.bShowErrorMessage = false;
	S.bUseTransparency = false;
	Window = IWebBrowserModule::Get().GetSingleton()->CreateBrowserWindow(S);
	if (!Window.IsValid()) { UE_LOG(LogHaarklous, Warning, TEXT("Voice chat: could not create the hidden browser")); return; }
	Window->BindUObject(TEXT("hkvoice"), Bridge.Get(), true);
	Window->SetViewportSize(FIntPoint(64, 64));
	Window->OnConsoleMessage().BindLambda([](const FString& Message, const FString& Source, int32 Line, EWebBrowserConsoleLogSeverity Severity)
	{
		UE_LOG(LogHaarklous, Log, TEXT("Voice page console: %s (%s:%d)"), *Message, *FPaths::GetCleanFilename(Source), Line);
	});
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FHKWebVoice::Tick));
	UE_LOG(LogHaarklous, Log, TEXT("Voice chat: loading %s"), *S.InitialURL);
}

FHKWebVoice::~FHKWebVoice()
{
	if (TickHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	if (Capture) { Capture->AbortCapturing(); Capture.Reset(); }
	if (Bridge.IsValid()) { Bridge->OnReady = nullptr; Bridge->OnSignal = nullptr; Bridge->OnMicResult = nullptr; Bridge->OnStats = nullptr; }
	if (Window.IsValid()) { Window->CloseBrowser(true); Window.Reset(); }
}

void FHKWebVoice::Js(const FString& Code)
{
	if (!Window.IsValid()) return;
	if (bReady) Window->ExecuteJavascript(Code); else Pending.Add(Code);
}

FString FHKWebVoice::YouCall() const { return FString::Printf(TEXT("hkVoice.setYou(%d);"), You ? You() : 0); }

void FHKWebVoice::SetMic(bool bOn, TFunction<void(bool)> Done)
{
	if (!Window.IsValid()) { if (Done) Done(false); return; }
	if (bOn && !Options.bTestTone && !Capture)
	{
		// The page's microphone is what Unreal captures from the default input device.
		Capture = MakeUnique<Audio::FAudioCaptureSynth>();
		Audio::FCaptureDeviceInfo Info;
		if (!Capture->GetDefaultCaptureDeviceInfo(Info) || !Capture->OpenDefaultStream() || !Capture->StartCapturing())
		{
			UE_LOG(LogHaarklous, Warning, TEXT("Voice chat: no microphone could be opened"));
			Capture.Reset();
			if (Done) Done(false);
			return;
		}
		CaptureRate = Info.PreferredSampleRate > 0 ? Info.PreferredSampleRate : 48000;
		CaptureChannels = FMath::Max(1, Info.InputChannels);
		CaptureFrac = 0;
		UE_LOG(LogHaarklous, Log, TEXT("Voice chat: microphone '%s' (%d Hz, %d channels)"), *Info.DeviceName, (int32)CaptureRate, CaptureChannels);
	}
	MicWaiters.Add(MoveTemp(Done));
	Js(FString::Printf(TEXT("hkVoice.setMic(%s)"), bOn ? TEXT("true") : TEXT("false")));
}

void FHKWebVoice::OnSignal(int32 From, const TSharedPtr<FJsonValue>& D)
{
	Js(YouCall() + FString::Printf(TEXT("hkVoice.onSignal(%d,%s)"), From, *HKJsonWire::Quote(HKJsonWire::WriteValue(D))));
}

void FHKWebVoice::PeerJoined(int32 Id) { Js(YouCall() + FString::Printf(TEXT("hkVoice.peerJoined(%d)"), Id)); }
void FHKWebVoice::PeerLeft(int32 Id) { Js(FString::Printf(TEXT("hkVoice.peerLeft(%d)"), Id)); }
void FHKWebVoice::SetVolume(int32 Id, double V) { Js(FString::Printf(TEXT("hkVoice.setVolume(%d,%s)"), Id, *HKJsonWire::Number(V))); }
void FHKWebVoice::Reset() { Js(TEXT("hkVoice.reset()")); }

double FHKWebVoice::PeerLevel(int32 Id, FString* OutState, double* OutBytesIn) const
{
	const TArray<TSharedPtr<FJsonValue>>* Peers = nullptr;
	if (!LastStats.IsValid() || !LastStats->TryGetArrayField(TEXT("peers"), Peers)) return 0;
	for (const TSharedPtr<FJsonValue>& P : *Peers)
	{
		const TSharedPtr<FJsonObject> O = P.IsValid() ? P->AsObject() : nullptr;
		if (!O || (int32)HKJsonWire::Num(O, TEXT("id")) != Id) continue;
		if (OutState) *OutState = HKJsonWire::Str(O, TEXT("state"));
		if (OutBytesIn) *OutBytesIn = HKJsonWire::Num(O, TEXT("bytesIn"));
		return HKJsonWire::Num(O, TEXT("level"));
	}
	return 0;
}

bool FHKWebVoice::Tick(float Dt)
{
	if (!bReady) return true;
	// Browsers only play sound after the user has touched the page: give the hidden page one key press, then let it
	// resume its audio (game-voice.js also retries playback every 1.5 s).
	SinceUnlock += Dt;
	if (!bUnlocked || SinceUnlock > 5)
	{
		if (!bUnlocked && Window.IsValid())
		{
			Window->OnFocus(true, false);
			const FKeyEvent Key(EKeys::A, FModifierKeysState(), 0, false, 0, 0x41);
			Window->OnKeyDown(Key);
			Window->OnKeyUp(Key);
			Window->OnFocus(false, false);
		}
		bUnlocked = true;
		SinceUnlock = 0;
		Js(TEXT("hkVoice.unlock()"));
	}
	PumpAudio(FMath::Min((double)Dt, .25));
	return true;
}

void FHKWebVoice::PumpAudio(double Dt)
{
	if (!bMic) { if (Capture) { TArray<float> Drop; Capture->GetAudioData(Drop); } return; }
	TArray<float> Mono;
	if (Options.bTestTone)
	{
		const double Want = PageRate * Dt + CaptureFrac;
		const int32 N = (int32)Want;
		CaptureFrac = Want - N;
		Mono.SetNumUninitialized(N);
		for (int32 i = 0; i < N; i++)
		{
			Mono[i] = (float)(FMath::Sin(TonePhase) * Options.ToneGain);
			TonePhase += 2 * HK::Pi * Options.ToneHz / PageRate;
			if (TonePhase > 2 * HK::Pi) TonePhase -= 2 * HK::Pi;
		}
	}
	else if (Capture)
	{
		TArray<float> Data;
		Capture->GetAudioData(Data);
		const int32 Frames = Data.Num() / CaptureChannels;
		if (!Frames) return;
		// Mix down to mono and resample (linear) to the page's 48 kHz.
		Resample.Reset();
		for (int32 f = 0; f < Frames; f++)
		{
			float S = 0;
			for (int32 c = 0; c < CaptureChannels; c++) S += Data[f * CaptureChannels + c];
			Resample.Add(S / CaptureChannels);
		}
		const double Step = CaptureRate / PageRate;
		double Pos = CaptureFrac;
		for (; Pos < Frames - 1; Pos += Step)
		{
			const int32 I = (int32)Pos;
			const double T = Pos - I;
			Mono.Add((float)(Resample[I] * (1 - T) + Resample[I + 1] * T));
		}
		CaptureFrac = FMath::Max(0.0, Pos - Frames);
	}
	if (!Mono.Num()) return;
	TArray<uint8> Bytes;
	Bytes.SetNumUninitialized(Mono.Num() * 2);
	for (int32 i = 0; i < Mono.Num(); i++)
	{
		const int16 V = (int16)FMath::Clamp(FMath::RoundToInt(Mono[i] * 32767.f), -32768, 32767);
		Bytes[i * 2] = (uint8)(V & 255);
		Bytes[i * 2 + 1] = (uint8)((V >> 8) & 255);
	}
	Js(FString::Printf(TEXT("hkVoice.pcm('%s')"), *FBase64::Encode(Bytes)));
}
