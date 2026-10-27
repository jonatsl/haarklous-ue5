#include "HKMusicPlayer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

using namespace HKSynth;

const TArray<FHKMusicTrackInfo>& FHKMusicPlayer::Tracks()
{
	// GameMusic.tracks: {title, bpm, beats, bars, seconds = bars * beats * 60 / bpm}
	static const TArray<FHKMusicTrackInfo> T = []()
	{
		TArray<FHKMusicTrackInfo> A;
		auto Add = [&A](const TCHAR* Name, const TCHAR* Title, double Bpm, int32 Beats, int32 Bars)
		{
			FHKMusicTrackInfo I;
			I.Name = Name; I.Title = Title; I.Bpm = Bpm; I.Beats = Beats; I.Bars = Bars;
			I.Seconds = Bars * Beats * 60.0 / Bpm;
			A.Add(I);
		};
		Add(TEXT("main"), TEXT("Torshov Evening"), 72, 4, 45);
		Add(TEXT("discgolf"), TEXT("Fairway Breeze"), 96, 4, 32);
		Add(TEXT("petanque"), TEXT("Café du parc"), 160, 3, 64);
		Add(TEXT("mission"), TEXT("Stairwell"), 100, 4, 32);
		return A;
	}();
	return T;
}

const FHKMusicTrackInfo* FHKMusicPlayer::Track(const FString& Name)
{
	for (const FHKMusicTrackInfo& I : Tracks()) if (I.Name == Name) return &I;
	return nullptr;
}

FString FHKMusicPlayer::AudioDir()
{
	return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data"), TEXT("audio"));
}

TSharedPtr<FHKMusicPCM> FHKMusicPCM::LoadWav(const FString& Path, double Gain, FString* Error)
{
	TArray<uint8> Bytes;
	auto Fail = [Error, &Path](const FString& Why) -> TSharedPtr<FHKMusicPCM> { if (Error) *Error = Path + TEXT(": ") + Why; return nullptr; };
	if (!FFileHelper::LoadFileToArray(Bytes, *Path)) return Fail(TEXT("cannot read"));
	auto U32 = [&Bytes](int32 At) { return uint32(Bytes[At]) | uint32(Bytes[At + 1]) << 8 | uint32(Bytes[At + 2]) << 16 | uint32(Bytes[At + 3]) << 24; };
	auto U16 = [&Bytes](int32 At) { return uint16(Bytes[At] | Bytes[At + 1] << 8); };
	if (Bytes.Num() < 44 || FMemory::Memcmp(Bytes.GetData(), "RIFF", 4) || FMemory::Memcmp(Bytes.GetData() + 8, "WAVE", 4)) return Fail(TEXT("not a WAV file"));
	int32 At = 12, Channels = 0, Bits = 0, Rate = 0, DataAt = -1, DataLen = 0;
	while (At + 8 <= Bytes.Num())
	{
		const uint32 Len = U32(At + 4);
		if (!FMemory::Memcmp(Bytes.GetData() + At, "fmt ", 4) && At + 24 <= Bytes.Num())
		{
			if (U16(At + 8) != 1) return Fail(TEXT("not PCM"));
			Channels = U16(At + 10); Rate = int32(U32(At + 12)); Bits = U16(At + 22);
		}
		else if (!FMemory::Memcmp(Bytes.GetData() + At, "data", 4)) { DataAt = At + 8; DataLen = int32(FMath::Min<int64>(Len, Bytes.Num() - DataAt)); break; }
		At += 8 + int32(Len) + (Len & 1);
	}
	if (Channels != 2 || Bits != 16 || DataAt < 0) return Fail(TEXT("expected 16-bit stereo PCM"));
	TSharedPtr<FHKMusicPCM> P = MakeShared<FHKMusicPCM>();
	P->Frames = DataLen / 4;
	P->SampleRate = Rate;
	// The WAV holds the browser's signal before the compressor (at master gain 0.8) divided by Gain.
	P->Scale = Gain / 32767.0 / .8;
	P->Samples.SetNumUninitialized(P->Frames * 2);
	FMemory::Memcpy(P->Samples.GetData(), Bytes.GetData() + DataAt, P->Frames * 4);
	return P;
}

FHKMusicPlayer::FHKMusicPlayer(double InSampleRate) : Ctx(InSampleRate), Compressor(InSampleRate)
{
	// chain(): comp.threshold -18, ratio 3, attack .01, release .25 (knee stays at the default 30).
	Compressor.Threshold = -18;
	Compressor.Ratio = 3;
	Compressor.Attack = .01f;
	Compressor.Release = .25f;
	Master = MakeUnique<FHKSynthParam>(Ctx, Volume * .8);
}

void FHKMusicPlayer::SetTrackData(const FString& Name, TSharedPtr<const FHKMusicPCM> InData)
{
	Data.Add(Name, InData);
	if (TUniquePtr<FLane>* L = Lanes.Find(Name)) (*L)->Data = InData;
}

bool FHKMusicPlayer::HasTrackData(const FString& Name) const
{
	return Data.Contains(Name);
}

FHKMusicPlayer::FLane& FHKMusicPlayer::Lane(const FString& Name)
{
	if (TUniquePtr<FLane>* Found = Lanes.Find(Name)) return **Found;
	TUniquePtr<FLane> L = MakeUnique<FLane>();
	L->Name = Name;
	L->Def = Track(Name);
	L->Fader = MakeUnique<FHKSynthParam>(Ctx, 0);
	if (const TSharedPtr<const FHKMusicPCM>* D = Data.Find(Name)) L->Data = *D;
	FLane& Ref = *L;
	Lanes.Add(Name, MoveTemp(L));
	return Ref;
}

void FHKMusicPlayer::Fade(FLane& L, double To, double Dur)
{
	const double Now = Ctx.CurrentTime();
	L.Fader->CancelScheduledValues(Now);
	L.Fader->SetValueAtTime(L.Fader->GetValue(), Now);
	L.Fader->LinearRampToValueAtTime(To, Now + Dur);
}

void FHKMusicPlayer::Attach(double InitialVolume)
{
	if (bAttached) return;
	bAttached = true;
	Volume = InitialVolume;
	Master = MakeUnique<FHKSynthParam>(Ctx, Volume * .8);
	NextTickFrame = Ctx.CurrentFrame() + int64(FMath::RoundToDouble(.06 * Ctx.SampleRate));
	// const want = current; current = undefined; play(want);
	const FString Want = Current;
	Current = TEXT("\x01");
	Play(Want);
}

void FHKMusicPlayer::Play(const FString& InName)
{
	const FString Name = Track(InName) ? InName : FString();
	if (Name == Current) return;
	Current = Name;
	if (!bAttached) return;
	const int64 Now = Ctx.CurrentFrame();
	const double Sr = Ctx.SampleRate;
	for (TPair<FString, TUniquePtr<FLane>>& P : Lanes)
	{
		FLane& L = *P.Value;
		if (L.Name != Name && L.bActive && !L.Until) { Fade(L, 0, 2.5); L.Until = Now + int64(FMath::RoundToDouble(2.6 * Sr)); }
	}
	if (!Name.IsEmpty())
	{
		FLane& L = Lane(Name);
		if (!L.bActive) { L.bActive = true; L.NextFrame = Now + int64(FMath::RoundToDouble(.12 * Sr)); }
		L.Until = 0;
		Fade(L, 1, L.Bar == 0 && L.NextFrame > Now ? 1.2 : 2.5);
	}
}

void FHKMusicPlayer::SetVolume(double V)
{
	Volume = V;
	if (bAttached) Master->SetTargetAtTime(Volume * .8, Ctx.CurrentTime(), .1);
	else Master->SetValue(Volume * .8);
}

int32 FHKMusicPlayer::LaneBar(const FString& Name) const
{
	const TUniquePtr<FLane>* L = Lanes.Find(Name);
	return L ? (*L)->Bar : 0;
}

void FHKMusicPlayer::Tick()
{
	const int64 Now = Ctx.CurrentFrame();
	const double Sr = Ctx.SampleRate;
	for (TPair<FString, TUniquePtr<FLane>>& P : Lanes)
	{
		FLane& L = *P.Value;
		if (!L.bActive) continue;
		if (L.Until && Now > L.Until) { L.bActive = false; L.Until = 0; continue; }
		const int64 BarFrames = int64(FMath::RoundToDouble(L.Def->BarSeconds() * Sr));
		if (L.NextFrame < Now) L.NextFrame = Now + int64(FMath::RoundToDouble(.05 * Sr));
		const int64 Ahead = Now + int64(FMath::RoundToDouble(1.2 * Sr));
		while (L.NextFrame < Ahead)
		{
			// render(bar, nextT): play the loop from this bar's start for one bar.
			FRun* Last = L.Runs.Num() ? &L.Runs.Last() : nullptr;
			const int32 LastBars = Last ? int32((Last->EndFrame - Last->StartFrame) / BarFrames) : 0;
			if (Last && Last->EndFrame == L.NextFrame && (Last->StartBar + LastBars) % L.Def->Bars == L.Bar) Last->EndFrame += BarFrames;
			else
			{
				FRun R;
				R.StartFrame = L.NextFrame; R.EndFrame = L.NextFrame + BarFrames; R.StartBar = L.Bar;
				L.Runs.Add(R);
			}
			L.NextFrame += BarFrames;
			L.Bar = (L.Bar + 1) % L.Def->Bars;
		}
	}
}

void FHKMusicPlayer::RenderBlock(float* OutL, float* OutR)
{
	if (!bAttached) return;
	const int64 F0 = Ctx.CurrentFrame();
	const double Sr = Ctx.SampleRate;
	const int64 TickFrames = int64(FMath::RoundToDouble(.06 * Sr));
	while (NextTickFrame <= F0) { Tick(); NextTickFrame += TickFrames; }
	FMemory::Memzero(MixL, sizeof(MixL));
	FMemory::Memzero(MixR, sizeof(MixR));
	bool bAny = false;
	for (TPair<FString, TUniquePtr<FLane>>& P : Lanes)
	{
		FLane& L = *P.Value;
		L.Fader->Compute(F0);
		L.Runs.RemoveAll([F0](const FRun& R) { return R.EndFrame <= F0; });
		if (!L.Data || L.Data->Frames == 0 || L.Runs.Num() == 0) continue;
		const float* G = L.Fader->Values();
		if (L.Fader->IsConstant() && G[0] == 0) continue;
		const FHKMusicPCM& D = *L.Data;
		const int16* S = D.Samples.GetData();
		const float Scale = float(D.Scale);
		const double BarSec = L.Def->BarSeconds();
		const bool bSameRate = FMath::Abs(D.SampleRate - Sr) < .5;
		for (const FRun& R : L.Runs)
		{
			const int64 A = FMath::Max(F0, R.StartFrame), B = FMath::Min(F0 + Block, R.EndFrame);
			if (A >= B) continue;
			bAny = true;
			if (bSameRate)
			{
				const int64 BarStart = int64(FMath::RoundToDouble(R.StartBar * BarSec * D.SampleRate));
				int64 Pos = (BarStart + (A - R.StartFrame)) % D.Frames;
				for (int64 f = A; f < B; f++)
				{
					const int32 i = int32(f - F0);
					const float g = G[i] * Scale;
					MixL[i] += S[Pos * 2] * g;
					MixR[i] += S[Pos * 2 + 1] * g;
					if (++Pos >= D.Frames) Pos = 0;
				}
			}
			else
			{
				for (int64 f = A; f < B; f++)
				{
					const int32 i = int32(f - F0);
					const double Pos = FMath::Fmod(R.StartBar * BarSec * D.SampleRate + double(f - R.StartFrame) * D.SampleRate / Sr, double(D.Frames));
					const int32 P0 = int32(Pos), P1 = (P0 + 1) % D.Frames;
					const float Fr = float(Pos - P0), g = G[i] * Scale;
					MixL[i] += (S[P0 * 2] + (S[P1 * 2] - S[P0 * 2]) * Fr) * g;
					MixR[i] += (S[P0 * 2 + 1] + (S[P1 * 2 + 1] - S[P0 * 2 + 1]) * Fr) * g;
				}
			}
		}
	}
	Master->Compute(F0);
	const float* M = Master->Values();
	for (int32 i = 0; i < Block; i++) { MixL[i] *= M[i]; MixR[i] *= M[i]; }
	if (bCompressor) Compressor.Process(MixL, MixR, Block);
	if (bAny || bCompressor)
		for (int32 i = 0; i < Block; i++) { OutL[i] += MixL[i]; OutR[i] += MixR[i]; }
	Ctx.BlockIndex++;
}
