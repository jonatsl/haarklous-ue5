#pragma once

#include "CoreMinimal.h"

/*
 * The game's sound API, same calls as the web's game-audio.js and game-music.js.
 * Everyone calls FHKAudio::Get() / FHKMusic::Get(); until the real implementation registers itself
 * (FHKAudio::SetInstance), a silent stand-in answers, so modules can be built and tested in any order.
 */
struct FHKSfxOpts
{
	double Gain = 1;
	double Reverb = -1;     // < 0: the sound's default
	FHKSfxOpts() {}
	FHKSfxOpts(double InGain) : Gain(InGain) {}
	FHKSfxOpts(double InGain, double InReverb) : Gain(InGain), Reverb(InReverb) {}
};

// audio.ambience({indoor, green, traffic, height, stair, kitchen})
struct FHKAmbience
{
	double Indoor = 0, Green = 0, Traffic = 999, Height = 0;
	bool bStair = false, bKitchen = false;
};

// A running engine or motor sound: set(rpm, load) for engines, set(thrust, level) for the drone; stop() fades it out.
class HAARKLOUS_API IHKEngineSound
{
public:
	virtual ~IHKEngineSound() {}
	virtual void Set(double A, double B = 0) = 0;
	virtual void Stop() = 0;
};

class HAARKLOUS_API FHKAudio
{
public:
	virtual ~FHKAudio() {}
	static FHKAudio& Get();
	static void SetInstance(FHKAudio* Instance);

	virtual void Resume() {}
	virtual void Ambience(const FHKAmbience& State) {}
	virtual void Footstep(const FString& Surface, bool bStair, double Soft = 1) {}
	virtual void Sfx(const FString& Name, const FHKSfxOpts& Opts = FHKSfxOpts()) {}
	// kind: 'bike', 'car', 'excavator', 'heli', 'plane', 'drone'
	virtual TSharedPtr<IHKEngineSound> Engine(const FString& Kind);
	virtual void Fire(double Level, double Near) {}
	// The listener (camera) in web space, for sounds that have a position.
	virtual void SetListener(const FVector& WebPos, double WebYaw) {}
};

class HAARKLOUS_API FHKMusic
{
public:
	virtual ~FHKMusic() {}
	static FHKMusic& Get();
	static void SetInstance(FHKMusic* Instance);

	// 'main', 'discgolf', 'petanque', 'mission'; crossfades 2.5 s, a returning track continues where it was.
	virtual void Play(const FString& Track) {}
	virtual void SetVolume(double V) { Volume = FMath::Clamp(V, 0.0, 1.0); }
	virtual double GetVolume() const { return Volume; }
	// Title and loop length of what is playing (empty title: nothing).
	virtual void Position(FString& Title, double& LengthSeconds) const { Title.Empty(); LengthSeconds = 0; }
protected:
	double Volume = .5;
};
