#include "HKAudio.h"

namespace
{
	class FSilentEngine : public IHKEngineSound
	{
	public:
		virtual void Set(double A, double B) override {}
		virtual void Stop() override {}
	};
	FHKAudio GSilentAudio;
	FHKMusic GSilentMusic;
	FHKAudio* GAudio = nullptr;
	FHKMusic* GMusic = nullptr;
}

FHKAudio& FHKAudio::Get() { return GAudio ? *GAudio : GSilentAudio; }
void FHKAudio::SetInstance(FHKAudio* Instance) { GAudio = Instance; }
TSharedPtr<IHKEngineSound> FHKAudio::Engine(const FString& Kind) { return MakeShared<FSilentEngine>(); }

FHKMusic& FHKMusic::Get() { return GMusic ? *GMusic : GSilentMusic; }
void FHKMusic::SetInstance(FHKMusic* Instance) { GMusic = Instance; }
