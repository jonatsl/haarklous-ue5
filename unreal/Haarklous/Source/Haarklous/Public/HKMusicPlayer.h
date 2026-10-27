#pragma once

#include "CoreMinimal.h"
#include "HKSynth.h"

/*
 * Port of game-music.js create(): per-track lanes with faders (2.5 s crossfades), bars scheduled 1.2 s ahead on a
 * 60 ms tick, a returning track continues from its next bar, master volume (volume * 0.8) -> the browser's
 * DynamicsCompressor (threshold -18 dB, ratio 3, attack 10 ms, release 250 ms) -> output.
 * The notes themselves are not synthesised here: tools/web-export/render-music.mjs renders every track with the web
 * game's own GameMusic.renderOffline() into a seamless loop (48 kHz 16-bit stereo WAV, Content/Data/audio), taken
 * before the compressor; this player plays those loops bar by bar exactly where the web version would schedule them.
 */
struct HAARKLOUS_API FHKMusicTrackInfo
{
	FString Name, Title;
	double Bpm = 0;
	int32 Beats = 4, Bars = 0;
	double Seconds = 0;
	double BarSeconds() const { return Beats * 60.0 / Bpm; }
};

// One rendered loop: interleaved stereo int16, plus the scale that turns it back into the web's signal level.
struct HAARKLOUS_API FHKMusicPCM
{
	TArray<int16> Samples;
	int32 Frames = 0;
	double SampleRate = 48000;
	double Scale = 1.0 / 32767;     // sample -> float (includes the render's gain factor)
	// Reads a 16-bit stereo PCM WAV file; Gain from music.json (1 if the render did not need headroom).
	static TSharedPtr<FHKMusicPCM> LoadWav(const FString& Path, double Gain, FString* Error = nullptr);
};

class HAARKLOUS_API FHKMusicPlayer
{
public:
	explicit FHKMusicPlayer(double InSampleRate);
	static const TArray<FHKMusicTrackInfo>& Tracks();
	static const FHKMusicTrackInfo* Track(const FString& Name);
	static FString AudioDir();           // Content/Data/audio

	void SetTrackData(const FString& Name, TSharedPtr<const FHKMusicPCM> Data);
	bool HasTrackData(const FString& Name) const;
	// music.attach(context): starts the clock and plays the wanted track.
	void Attach(double InitialVolume);
	bool IsAttached() const { return bAttached; }
	// music.play(name | null): empty name = silence.
	void Play(const FString& Name);
	// Already clamped and rounded by the caller (FHKMusicImpl).
	void SetVolume(double V);
	// Adds 128 frames of music (after the compressor) to L/R. Silent until attached.
	void RenderBlock(float* L, float* R);
	FString Current;
	double Volume = .5;
	bool bCompressor = true;          // tests can render the loop before the compressor

	// Where a lane is: next bar to schedule (like position().bar in the web).
	int32 LaneBar(const FString& Name) const;
	double Now() const { return Ctx.CurrentTime(); }

private:
	struct FRun { int64 StartFrame = 0, EndFrame = 0; int32 StartBar = 0; };
	struct FLane
	{
		FString Name;
		const FHKMusicTrackInfo* Def = nullptr;
		TUniquePtr<FHKSynthParam> Fader;
		int32 Bar = 0;
		int64 NextFrame = 0;
		bool bActive = false;
		int64 Until = 0;              // 0: not fading out
		TArray<FRun> Runs;
		TSharedPtr<const FHKMusicPCM> Data;
	};
	FLane& Lane(const FString& Name);
	void Fade(FLane& L, double To, double Dur);
	void Tick();
	FHKSynthContext Ctx;              // clock and automation only (no nodes)
	TMap<FString, TUniquePtr<FLane>> Lanes;
	TMap<FString, TSharedPtr<const FHKMusicPCM>> Data;
	TUniquePtr<FHKSynthParam> Master;
	FHKSynthCompressor Compressor;
	bool bAttached = false;
	int64 NextTickFrame = 0;
	float MixL[HKSynth::Block], MixR[HKSynth::Block];
};
