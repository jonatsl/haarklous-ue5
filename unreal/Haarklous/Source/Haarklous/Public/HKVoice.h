#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/StrongObjectPtr.h"
#include "Containers/Ticker.h"
#include "HKMultiplayer.h"
#include "HKVoice.generated.h"

class IWebBrowserWindow;
namespace Audio { class FAudioCaptureSynth; }

/*
 * Lobby voice chat for the Unreal client, compatible with the web clients' WebRTC mesh (game-voice.js: the lower id
 * offers, one sendrecv audio channel per pair, mic on/off = replaceTrack, STUN stun.cloudflare.com, full volume).
 * Unreal has no WebRTC of its own, so the web's own game-voice.js runs unchanged in a hidden browser page (the engine's
 * CEF / Chromium, WebBrowser module): Content/Web/voice/index.html. Signalling goes through our lobby WebSocket
 * ('rtc' messages) via a bound UObject (window.ue.hkvoice). CEF in Unreal may not open the microphone itself
 * (no media permission handler, command-line switches are locked), so the page's "microphone" is the sound Unreal
 * sends it: the default capture device read with AudioCaptureCore (or a test tone), 48 kHz PCM pushed into a Web Audio
 * stream. Remote voices are played by the page (Chromium's audio output, full volume like the web).
 * Needs rendering (CEF is off with -nullrhi and in commandlets); FHKWebVoice::Available() tells.
 */
UCLASS()
class HAARKLOUS_API UHKVoiceBridge : public UObject
{
	GENERATED_BODY()
public:
	// Called from the page (window.ue.hkvoice.<name>(...)).
	UFUNCTION() void ready();
	UFUNCTION() void signal(int32 To, const FString& Json);
	UFUNCTION() void micresult(bool bOk, bool bOn);
	UFUNCTION() void stats(const FString& Json);
	UFUNCTION() void log(const FString& Text);

	TFunction<void()> OnReady;
	TFunction<void(int32, const FString&)> OnSignal;
	TFunction<void(bool, bool)> OnMicResult;
	TFunction<void(const FString&)> OnStats;
};

struct FHKWebVoiceOptions
{
	bool bTestTone = false;          // send a 440 Hz tone instead of the microphone (tests)
	double ToneHz = 440;
	double ToneGain = .3;
	FString PagePath;                // empty: Content/Web/voice/index.html
};

class HAARKLOUS_API FHKWebVoice : public IHKVoice
{
public:
	explicit FHKWebVoice(const FHKWebVoiceOptions& InOptions = FHKWebVoiceOptions());
	virtual ~FHKWebVoice() override;
	static bool Available();          // CEF can run in this process
	static FString DefaultPage();

	// IHKVoice (game-voice.js API)
	virtual void SetMic(bool bOn, TFunction<void(bool)> Done) override;
	virtual bool Mic() const override { return bMic; }
	virtual void OnSignal(int32 From, const TSharedPtr<FJsonValue>& D) override;
	virtual void PeerJoined(int32 Id) override;
	virtual void PeerLeft(int32 Id) override;
	virtual void SetVolume(int32 Id, double V) override;
	virtual void Reset() override;

	bool IsReady() const { return bReady; }
	// Last stats from the page: {ctx, mic, buffered, peers: [{id, state, ice, paused, bytesIn, bytesOut, level, energy}]}.
	TSharedPtr<FJsonObject> LastStats;
	double LastStatsAt = 0;
	// Received level (RMS 0..1) and connection state of one peer from the last stats.
	virtual double PeerLevel(int32 Id, FString* OutState = nullptr, double* OutBytesIn = nullptr) const override;

private:
	void Js(const FString& Code);
	bool Tick(float Dt);
	void PumpAudio(double Dt);
	FString YouCall() const;

	FHKWebVoiceOptions Options;
	TSharedPtr<IWebBrowserWindow> Window;
	TStrongObjectPtr<UHKVoiceBridge> Bridge;
	TUniquePtr<Audio::FAudioCaptureSynth> Capture;
	FTSTicker::FDelegateHandle TickHandle;
	TArray<FString> Pending;
	TArray<TFunction<void(bool)>> MicWaiters;
	TArray<float> Resample;
	bool bReady = false, bMic = false, bUnlocked = false;
	double TonePhase = 0, CaptureRate = 48000, CaptureFrac = 0, SinceUnlock = 0;
	int32 CaptureChannels = 1;
};
