#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SynthComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "HKAudio.h"
#include "HKSynth.h"
#include "HKMusicPlayer.h"
#include "HKAudioImpl.generated.h"

/*
 * The game's sound, ported from the web version:
 *   FHKGameAudio  = game-audio.js create() line by line (ambience, footsteps, sfx, engines, fire) plus the
 *                   mission's pistol sounds from case-game.js sound(), on the Web Audio engine in HKSynth.h.
 *   FHKMusicPlayer = game-music.js create() (HKMusicPlayer.h).
 *   FHKAudioEngine = both, rendered on the audio thread; the game thread talks to it through a command queue.
 *   FHKAudioImpl / FHKMusicImpl = the FHKAudio / FHKMusic implementations the game calls (game thread).
 *   AHKAudioActor = owns the engine and the UHKAudioSynthComponent that plays it, registers the instances.
 *   UHKAudioSubsystem = spawns AHKAudioActor in every game world (nothing for the main loop to do).
 *
 * Sfx names (FHKAudio::Get().Sfx(Name, Opts); case-insensitive):
 *   game-audio.js: doorOpen doorClose locked carDoor bottleOpen gulp shutter beep throw chains thud boule gravel
 *                  click scrape dump hit cheer starter crash arm
 *   case-game.js:  pistolShot (also "shot", "caseShot", "gunshot")
 *                  pistolClick (also "reload", "empty", "dryFire", "caseClick")  - the web's sound('click')
 *                  hostileShot (also "enemy", "enemyShot") - add the distance in metres after a colon,
 *                  e.g. "hostileShot:7.5" (the web's sound('enemy', 7.5)); without it the distance is 1.
 *   Opts.Gain multiplies every sound (web opts.gain); Opts.Reverb >= 0 sets the reverb send (web opts.reverb).
 * Engine kinds (FHKAudio::Get().Engine(Kind)): bike, car, excavator, plane, heli, drone (also motorcycle, helicopter),
 *   and "hydraulics" (the excavator's engine.hydraulics(flow) as its own handle: Set(Flow); Stop()).
 *   IHKEngineSound::Hydraulics(Flow) on the 'excavator' handle is the web's engine.hydraulics(flow).
 */

// A running engine sound on the audio thread (engine(kind) -> {set, stop, hydraulics}).
class HAARKLOUS_API FHKEngineVoice
{
public:
	virtual ~FHKEngineVoice() {}
	virtual void Set(double A, double B) = 0;
	virtual void Stop() = 0;
	virtual void Hydraulics(double Flow) {}
};

class HAARKLOUS_API FHKGameAudio
{
public:
	FHKGameAudio(double InSampleRate, uint32 Seed, bool bInAutoTick = true);
	~FHKGameAudio();

	// The web API (audio thread only).
	bool Init();
	void Resume();
	void Ambience(const FHKAmbience& State);
	void Footstep(const FString& Surface, bool bInStair, double Softness);
	void Sfx(const FString& Name, const FHKSfxOpts& Opts);
	TSharedPtr<FHKEngineVoice> Engine(const FString& Kind);
	void Fire(double Level, double Gain);
	// case-game.js sound(kind, distance): kind 'shot', 'click' or 'enemy'.
	void CaseSound(const FString& Kind, double Distance, double Gain = 1);
	// The 120 ms ambience interval (called by the context timer unless bAutoTick is false, as in the tests).
	void TickAmbience();

	bool IsRunning() const { return Ctx.IsValid(); }
	FHKSynthContext* Context() { return Ctx.Get(); }
	// Renders one 128-frame block (silence before Init). Returns the two output channels.
	void RenderBlock(const float*& L, const float*& R);
	double SampleRate;
	FHKRandom Random;
	// Maps a name (or close variant) to the sfx it plays; empty if unknown.
	static FString CanonicalSfx(const FString& Name, double* OutDistance = nullptr);

	// For the engine voices.
	double Rand(double A, double B) { return A + Random.Next() * (B - A); }
	double MathRandom() { return Random.Next(); }
	double Now() const { return Ctx->CurrentTime(); }
	TSharedPtr<FHKSynthBufferSource> Noise(bool bLoop = true);
	TSharedPtr<FHKSynthNode> Out(const TSharedPtr<FHKSynthNode>& Node, double Reverb = 0);
	static void Env(FHKSynthGain& G, double T, double Peak, double Attack, double Decay);

private:
	void StartAmbience();
	void Bird(double Level, bool bMuffled);
	void Crackle(double Gain);
	void PassingCar(double Level);
	bool bAutoTick;
	TUniquePtr<FHKSynthContext> Ctx;
	TSharedPtr<FHKSynthGain> Master, Dry, ReverbSend;
	TSharedPtr<FHKSynthConvolver> Convolver;
	TSharedPtr<FHKSynthBuffer> NoiseBuf;
	struct FAmb
	{
		TSharedPtr<FHKSynthGain> WindG, TrafG, RoomG, HumG;
		TSharedPtr<FHKSynthBiquad> WindF;
		FHKAmbience State;
		double BirdTimer = 0, CarTimer = 3, Last = 0;
	};
	TUniquePtr<FAmb> Amb;
	struct FFireNode { TSharedPtr<FHKSynthGain> G; TSharedPtr<FHKSynthBiquad> Lp; };
	TUniquePtr<FFireNode> FireNode;
	double FireLevel = 0, FireGain = 0;
	float Silence[HKSynth::Block];
};

// Game audio + music on the audio thread, fed by commands from the game thread.
class HAARKLOUS_API FHKAudioEngine : public TSharedFromThis<FHKAudioEngine, ESPMode::ThreadSafe>
{
public:
	FHKAudioEngine(double InSampleRate, uint32 Seed);
	void Enqueue(TFunction<void(FHKAudioEngine&)>&& Command);
	// Interleaved output (any frame count). Runs the queued commands first.
	void Render(float* Out, int32 Frames, int32 Channels);
	FHKGameAudio Audio;
	FHKMusicPlayer Music;
	double SampleRate;
	double MusicVolume = .5;
	// Statistics (audio thread): seconds spent rendering, frames rendered.
	double CpuSeconds = 0;
	int64 FramesRendered = 0;
	float PeakOutput = 0;           // loudest output sample so far
	double SumSquares = 0;          // for the average output level
	// Output gain (compensates Unreal's platform headroom so levels match the browser).
	float OutputGain = 1;
private:
	void RenderBlock();
	FCriticalSection Lock;
	TArray<TFunction<void(FHKAudioEngine&)>> Pending;
	float BlockL[HKSynth::Block], BlockR[HKSynth::Block];
	int32 BlockPos = HKSynth::Block;
};

// The engine handle the game holds (game thread). Dropping it stops the sound.
class HAARKLOUS_API FHKEngineSoundImpl : public IHKEngineSound
{
public:
	struct FSlot { TSharedPtr<FHKEngineVoice> Voice; };
	FHKEngineSoundImpl(const TSharedRef<FHKAudioEngine, ESPMode::ThreadSafe>& InEngine, const FString& Kind);
	virtual ~FHKEngineSoundImpl();
	virtual void Set(double A, double B = 0) override;
	virtual void Stop() override;
	virtual void Hydraulics(double Flow) override;   // excavator: engine.hydraulics(flow)
private:
	TWeakPtr<FHKAudioEngine, ESPMode::ThreadSafe> Engine;
	TSharedPtr<FSlot, ESPMode::ThreadSafe> Slot;
	bool bStopped = false;
};

class HAARKLOUS_API FHKAudioImpl : public FHKAudio
{
public:
	explicit FHKAudioImpl(const TSharedRef<FHKAudioEngine, ESPMode::ThreadSafe>& InEngine) : EngineRef(InEngine) {}
	virtual void Resume() override;
	virtual void Ambience(const FHKAmbience& State) override;
	virtual void Footstep(const FString& Surface, bool bStair, double Soft = 1) override;
	virtual void Sfx(const FString& Name, const FHKSfxOpts& Opts = FHKSfxOpts()) override;
	virtual TSharedPtr<IHKEngineSound> Engine(const FString& Kind) override;
	virtual void Fire(double Level, double Near) override;
	virtual void SetListener(const FVector& WebPos, double WebYaw) override { ListenerPos = WebPos; ListenerYaw = WebYaw; }
	FVector ListenerPos = FVector::ZeroVector;   // the web version has no positional sound; kept for later
	double ListenerYaw = 0;
private:
	TSharedRef<FHKAudioEngine, ESPMode::ThreadSafe> EngineRef;
};

class HAARKLOUS_API FHKMusicImpl : public FHKMusic
{
public:
	explicit FHKMusicImpl(const TSharedRef<FHKAudioEngine, ESPMode::ThreadSafe>& InEngine);
	virtual void Play(const FString& Track) override;
	virtual void SetVolume(double V) override;
	virtual void Position(FString& Title, double& LengthSeconds) const override;
	// Saved volume (localStorage 'haarklous-music-volume-v1' in the web version), default 0.5.
	static double LoadVolume();
	// Loads the four loops in the background and hands them to the engine.
	void LoadTracks();
private:
	TSharedRef<FHKAudioEngine, ESPMode::ThreadSafe> Engine;
	FString Current;
};

UCLASS()
class HAARKLOUS_API UHKAudioSynthComponent : public USynthComponent
{
	GENERATED_BODY()
public:
	UHKAudioSynthComponent(const FObjectInitializer& ObjectInitializer);
	TSharedPtr<FHKAudioEngine, ESPMode::ThreadSafe> Engine;
protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
};

UCLASS()
class HAARKLOUS_API AHKAudioActor : public AActor
{
	GENERATED_BODY()
public:
	AHKAudioActor();
	// Spawns the audio actor once per world (the subsystem does this; calling it again is harmless).
	static AHKAudioActor* Ensure(UWorld* World);
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY()
	TObjectPtr<UHKAudioSynthComponent> Synth;
	TSharedPtr<FHKAudioEngine, ESPMode::ThreadSafe> Engine;
	// Average audio-thread cost so far (milliseconds of CPU per second of sound).
	double CpuMillisecondsPerSecond() const;
private:
	TUniquePtr<FHKAudioImpl> AudioImpl;
	TUniquePtr<FHKMusicImpl> MusicImpl;
};

UCLASS()
class HAARKLOUS_API UHKAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
