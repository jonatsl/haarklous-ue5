#include "HKAudioImpl.h"
#include "Haarklous.h"
#include "AudioDevice.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformTime.h"

using namespace HKSynth;

/* ============================================================================================================
 * Port of reference/web-game/game-audio.js (procedural game audio, no sound files) and the mission's weapon sounds
 * (case-game.js sound()). The JavaScript is quoted in the comments; every Math.random() call happens in the same
 * order as in the browser, so with the same seed the C++ renders the same noise, the same birds and cars.
 * ============================================================================================================ */

namespace
{
	using FGainPtr = TSharedPtr<FHKSynthGain>;
	using FOscPtr = TSharedPtr<FHKSynthOscillator>;
	using FFiltPtr = TSharedPtr<FHKSynthBiquad>;
	using FSrcPtr = TSharedPtr<FHKSynthBufferSource>;

	// const steps = {surface: [freq, q, gain, decay]}
	struct FStep { double Freq, Q, Gain, Decay; };
	const FStep* StepFor(const FString& Surface)
	{
		static const TMap<FString, FStep> Steps = {
			{TEXT("grass"), {700, 1.5, .05, .1}}, {TEXT("gravel"), {3800, 1.2, .1, .07}}, {TEXT("sand"), {1600, 1, .06, .08}},
			{TEXT("asphalt"), {2200, 2, .07, .04}}, {TEXT("wood"), {900, 2.5, .09, .05}}, {TEXT("tile"), {3200, 4, .07, .03}},
			{TEXT("stone"), {2600, 3.5, .1, .04}}, {TEXT("metal"), {1100, 8, .08, .09}}, {TEXT("terrain"), {900, 1.2, .05, .08}}};
		const FStep* S = Steps.Find(Surface);
		return S ? S : Steps.Find(TEXT("terrain"));
	}
}

FHKGameAudio::FHKGameAudio(double InSampleRate, uint32 Seed, bool bInAutoTick)
	: SampleRate(InSampleRate), Random(Seed), bAutoTick(bInAutoTick)
{
	FMemory::Memzero(Silence, sizeof(Silence));
}

FHKGameAudio::~FHKGameAudio()
{
	FireNode.Reset();
	Amb.Reset();
	Ctx.Reset();
}

bool FHKGameAudio::Init()
{
	if (Ctx) return true;
	Ctx = MakeUnique<FHKSynthContext>(SampleRate);
	// master = ctx.createGain(); master.gain.value = .8; master.connect(ctx.destination); dry = ctx.createGain(); dry.connect(master);
	Master = Ctx->CreateGain(); Master->Gain.SetValue(.8); Master->bPersistent = true; Master->Connect(Ctx->Destination);
	Dry = Ctx->CreateGain(); Dry->bPersistent = true; Dry->Connect(Master);
	// noiseBuf: 2 s of white noise
	NoiseBuf = Ctx->CreateBuffer(1, int32(Ctx->SampleRate * 2));
	{
		float* D = NoiseBuf->Channel(0);
		for (int32 i = 0; i < NoiseBuf->Length; i++) D[i] = float(MathRandom() * 2 - 1);
	}
	// Stairwell/indoor echo: a generated decaying impulse response.
	Convolver = Ctx->CreateConvolver();
	const int32 Len = int32(Ctx->SampleRate * 1.8);
	TSharedPtr<FHKSynthBuffer> Ir = Ctx->CreateBuffer(2, Len);
	for (int32 c = 0; c < 2; c++)
	{
		float* Ch = Ir->Channel(c);
		for (int32 i = 0; i < Len; i++) Ch[i] = float((MathRandom() * 2 - 1) * pow(1 - double(i) / Len, 3.2) * (i < 400 ? i / 400.0 : 1));
	}
	Convolver->SetBuffer(*Ir);
	ReverbSend = Ctx->CreateGain(); ReverbSend->bPersistent = true; ReverbSend->Gain.SetValue(0); ReverbSend->Connect(Convolver); Convolver->Connect(Master);
	StartAmbience();
	return true;
}

void FHKGameAudio::Resume()
{
	Init();
}

// function noise(loop = true) {const s = ctx.createBufferSource(); s.buffer = noiseBuf; s.loop = loop; s.loopStart = Math.random(); return s;}
TSharedPtr<FHKSynthBufferSource> FHKGameAudio::Noise(bool bLoop)
{
	FSrcPtr S = Ctx->CreateBufferSource();
	S->Buffer = NoiseBuf;
	S->bLoop = bLoop;
	S->LoopStart = MathRandom();   // after the first pass the loop restarts here (Chrome loops loopStart .. buffer end)
	return S;
}

// function out(node, reverb = 0) {node.connect(dry); if (reverb) {const g = ctx.createGain(); g.gain.value = reverb; node.connect(g); g.connect(reverbSend);} return node;}
TSharedPtr<FHKSynthNode> FHKGameAudio::Out(const TSharedPtr<FHKSynthNode>& Node, double Reverb)
{
	Node->Connect(Dry);
	if (Reverb)
	{
		FGainPtr G = Ctx->CreateGain();
		G->Gain.SetValue(Reverb);
		Node->Connect(G);
		G->Connect(ReverbSend);
	}
	return Node;
}

// function env(gainNode, t, peak, attack, decay) {setValueAtTime(0, t); linearRampToValueAtTime(peak, t + attack); exponentialRampToValueAtTime(.0001, t + attack + decay);}
void FHKGameAudio::Env(FHKSynthGain& G, double T, double Peak, double Attack, double Decay)
{
	G.Gain.SetValueAtTime(0, T);
	G.Gain.LinearRampToValueAtTime(Peak, T + Attack);
	G.Gain.ExponentialRampToValueAtTime(.0001, T + Attack + Decay);
}

// ---------- Ambience ----------
void FHKGameAudio::StartAmbience()
{
	Amb = MakeUnique<FAmb>();
	FSrcPtr Wind = Noise(); FFiltPtr WindF = Ctx->CreateBiquadFilter(); FGainPtr WindG = Ctx->CreateGain();
	WindF->Type = EHKBiquadType::Lowpass; WindF->Frequency.SetValue(420); WindG->Gain.SetValue(0); Wind->Connect(WindF)->Connect(WindG); Out(WindG); Wind->Start();
	FSrcPtr Traffic = Noise(); FFiltPtr TrafF = Ctx->CreateBiquadFilter(); FGainPtr TrafG = Ctx->CreateGain();
	TrafF->Type = EHKBiquadType::Lowpass; TrafF->Frequency.SetValue(260); TrafG->Gain.SetValue(0); Traffic->Connect(TrafF)->Connect(TrafG); Out(TrafG); Traffic->Start();
	FSrcPtr Room = Noise(); FFiltPtr RoomF = Ctx->CreateBiquadFilter(); FGainPtr RoomG = Ctx->CreateGain();
	RoomF->Type = EHKBiquadType::Lowpass; RoomF->Frequency.SetValue(140); RoomG->Gain.SetValue(0); Room->Connect(RoomF)->Connect(RoomG); Out(RoomG); Room->Start();
	FOscPtr Hum = Ctx->CreateOscillator(); FGainPtr HumG = Ctx->CreateGain();
	Hum->Frequency.SetValue(100); Hum->Type = EHKOscType::Sine; HumG->Gain.SetValue(0); Hum->Connect(HumG); Out(HumG); Hum->Start();
	for (FHKSynthNode* N : {(FHKSynthNode*)WindG.Get(), (FHKSynthNode*)TrafG.Get(), (FHKSynthNode*)RoomG.Get(), (FHKSynthNode*)HumG.Get()}) N->bPersistent = true;
	Amb->WindG = WindG; Amb->WindF = WindF; Amb->TrafG = TrafG; Amb->RoomG = RoomG; Amb->HumG = HumG;
	// state: {indoor: 0, green: .5, traffic: 0, height: 0, stair: false, kitchen: false}, birdTimer: 0, carTimer: 3, last: now()
	Amb->State.Indoor = 0; Amb->State.Green = .5; Amb->State.Traffic = 0; Amb->State.Height = 0; Amb->State.bStair = false; Amb->State.bKitchen = false;
	Amb->BirdTimer = 0; Amb->CarTimer = 3; Amb->Last = Now();
	if (bAutoTick) Ctx->SetInterval([this]() { TickAmbience(); }, .12);
}

void FHKGameAudio::Ambience(const FHKAmbience& State)
{
	if (Amb) Amb->State = State;
}

void FHKGameAudio::TickAmbience()
{
	if (!Amb || !Ctx) return;
	const FHKAmbience& S = Amb->State;
	const double T = Now(), Dt = FMath::Min(.5, T - Amb->Last);
	Amb->Last = T;
	const double Outdoors = 1 - S.Indoor;
	auto Set = [T](FHKSynthParam& P, double V) { P.SetTargetAtTime(V, T, .4); };
	Set(Amb->WindG->Gain, (.012 + .03 * FMath::Min(1.0, S.Height / 12)) * Outdoors + .004 * S.Indoor);
	Amb->WindF->Frequency.SetTargetAtTime(300 + 260 * (.5 + .5 * sin(T * .23)) + 200 * sin(T * .071), T, .8);
	const double Tr = FMath::Max(0.0, 1 - S.Traffic / 70);
	Set(Amb->TrafG->Gain, (.012 + .05 * Tr * Tr) * (Outdoors + .25 * S.Indoor));
	Set(Amb->RoomG->Gain, .03 * S.Indoor);
	Set(Amb->HumG->Gain, S.bKitchen ? .006 : 0);
	ReverbSend->Gain.SetTargetAtTime(S.bStair ? .6 : S.Indoor * .12, T, .3);
	// Birds: more frequent near trees and in the park, rare indoors.
	Amb->BirdTimer -= Dt;
	// Indoors only an occasional faint, muffled call through the windows.
	if (Amb->BirdTimer <= 0)
	{
		Amb->BirdTimer = Rand(2.5, 9) / (.4 + S.Green * .9);
		if (MathRandom() < Outdoors * .85 + .12 * S.Indoor) Bird(Outdoors * (.3 + S.Green * .5) + .025 * S.Indoor, S.Indoor > .5);
	}
	// Campfire crackles (see fire()).
	if (FireGain > .02 && MathRandom() < .1 + FireLevel * .08) Crackle(FireGain);
	Amb->CarTimer -= Dt;
	if (Amb->CarTimer <= 0)
	{
		Amb->CarTimer = Rand(3, 12);
		if (Tr > .15 && Outdoors > .3) PassingCar(Tr * Outdoors);
	}
}

void FHKGameAudio::Bird(double Level, bool bMuffled)
{
	const double T = Now() + .05;
	TSharedPtr<FHKSynthPanner> Pan = Ctx->CreateStereoPanner();
	Pan->Pan.SetValue(Rand(-.9, .9));
	FGainPtr G = Ctx->CreateGain();
	G->Gain.SetValue(Level * Rand(.05, .12));
	if (bMuffled) { FFiltPtr Lp = Ctx->CreateBiquadFilter(); Lp->Type = EHKBiquadType::Lowpass; Lp->Frequency.SetValue(1400); Pan->Connect(Lp)->Connect(G); Out(G); }
	else { Pan->Connect(G); Out(G, .15); }
	const int32 Species = int32(FMath::FloorToDouble(MathRandom() * 4));
	if (Species == 0)
	{
		// great tit "ti-ta ti-ta"
		for (int32 i = 0; i < 4; i++)
		{
			FOscPtr O = Ctx->CreateOscillator(); FGainPtr E = Ctx->CreateGain();
			const double F = i % 2 ? 4200 : 5600, At = T + i * .16;
			O->Frequency.SetValueAtTime(F, At); O->Frequency.ExponentialRampToValueAtTime(F * .85, At + .08);
			O->Connect(E)->Connect(Pan); Env(*E, At, 1, .01, .09); O->Start(At); O->Stop(At + .12);
		}
	}
	else if (Species == 1)
	{
		// blackbird warble
		double At = T;
		for (int32 i = 0; i < 6; i++)
		{
			FOscPtr O = Ctx->CreateOscillator(); FGainPtr E = Ctx->CreateGain();
			const double F = Rand(1600, 3200), D = Rand(.08, .2);
			O->Frequency.SetValueAtTime(F, At);
			const double To = F * Rand(.7, 1.4);
			O->Frequency.ExponentialRampToValueAtTime(To, At + D);
			O->Connect(E)->Connect(Pan); Env(*E, At, 1, .02, D); O->Start(At); O->Stop(At + D + .05);
			At += D * Rand(.9, 1.3);
		}
	}
	else if (Species == 2)
	{
		// sparrow chirps (the loop condition draws a new random number every time it is checked)
		for (int32 i = 0; i < 3 + int32(FMath::FloorToDouble(MathRandom() * 4)); i++)
		{
			FOscPtr O = Ctx->CreateOscillator(); FGainPtr E = Ctx->CreateGain();
			const double At = T + i * Rand(.12, .25), F = Rand(3500, 4800);
			O->Type = EHKOscType::Triangle;
			O->Frequency.SetValueAtTime(F, At); O->Frequency.LinearRampToValueAtTime(F * 1.2, At + .03); O->Frequency.LinearRampToValueAtTime(F * .8, At + .06);
			O->Connect(E)->Connect(Pan); Env(*E, At, 1, .005, .06); O->Start(At); O->Stop(At + .09);
		}
	}
	else
	{
		// magpie chatter: harsh noise bursts
		for (int32 i = 0; i < 5; i++)
		{
			FSrcPtr N = Noise(false); FFiltPtr F = Ctx->CreateBiquadFilter(); FGainPtr E = Ctx->CreateGain();
			const double At = T + i * .09;
			F->Type = EHKBiquadType::Bandpass; F->Frequency.SetValue(Rand(1800, 2600)); F->Q.SetValue(3);
			N->Connect(F)->Connect(E)->Connect(Pan); Env(*E, At, 1.6, .004, .06); N->Start(At); N->Stop(At + .08);
		}
	}
}

// Campfire: a soft roar plus random crackles, louder for a bigger fire and closer to it. fire(level 0..6, gain 0..1)
void FHKGameAudio::Fire(double Level, double Gain)
{
	if (!Ctx) return;
	if (!FireNode)
	{
		FSrcPtr N = Noise(); FFiltPtr Lp = Ctx->CreateBiquadFilter(); FGainPtr G = Ctx->CreateGain();
		Lp->Type = EHKBiquadType::Lowpass; Lp->Frequency.SetValue(500); G->Gain.SetValue(0); N->Connect(Lp)->Connect(G); Out(G); N->Start();
		G->bPersistent = true;
		FireNode = MakeUnique<FFireNode>();
		FireNode->G = G; FireNode->Lp = Lp;
	}
	FireLevel = Level; FireGain = Gain;
	const double T = Now();
	FireNode->G->Gain.SetTargetAtTime(.05 * Gain * FMath::Min(1.4, Level / 3), T, .3);
	FireNode->Lp->Frequency.SetTargetAtTime(320 + Level * 110, T, .3);
}

void FHKGameAudio::Crackle(double Gain)
{
	const double T = Now() + MathRandom() * .1;
	for (int32 i = 0, N = 1 + int32(FMath::FloorToDouble(MathRandom() * 3)); i < N; i++)
	{
		FSrcPtr S = Noise(false); FFiltPtr F = Ctx->CreateBiquadFilter(); FGainPtr E = Ctx->CreateGain();
		const double At = T + i * Rand(.01, .05);
		F->Type = EHKBiquadType::Bandpass; F->Frequency.SetValue(Rand(1800, 5200)); F->Q.SetValue(2);
		S->Connect(F)->Connect(E); Out(E);
		const double Peak = Rand(.08, .3) * Gain;
		const double Decay = Rand(.02, .07);
		Env(*E, At, Peak, .001, Decay);
		S->Start(At); S->Stop(At + .12);
	}
}

void FHKGameAudio::PassingCar(double Level)
{
	const double T = Now();
	FSrcPtr N = Noise(false); FFiltPtr F = Ctx->CreateBiquadFilter(); FGainPtr G = Ctx->CreateGain();
	TSharedPtr<FHKSynthPanner> Pan = Ctx->CreateStereoPanner();
	F->Type = EHKBiquadType::Lowpass; F->Frequency.SetValueAtTime(300, T); F->Frequency.LinearRampToValueAtTime(900, T + 1.6); F->Frequency.LinearRampToValueAtTime(250, T + 3.4);
	G->Gain.SetValueAtTime(0, T); G->Gain.LinearRampToValueAtTime(.09 * Level, T + 1.6); G->Gain.LinearRampToValueAtTime(0, T + 3.5);
	Pan->Pan.SetValueAtTime(-.8, T); Pan->Pan.LinearRampToValueAtTime(.8, T + 3.4);
	N->Connect(F)->Connect(G)->Connect(Pan); Out(Pan); N->Start(T); N->Stop(T + 3.6);
}

// ---------- Footsteps ----------
void FHKGameAudio::Footstep(const FString& Surface, bool bInStair, double Softness)
{
	if (!Ctx) return;
	const FStep& St = *StepFor(Surface);
	const double T = Now();
	FSrcPtr N = Noise(false); FFiltPtr F = Ctx->CreateBiquadFilter(); FGainPtr E = Ctx->CreateGain();
	F->Type = EHKBiquadType::Bandpass; F->Frequency.SetValue(St.Freq * Rand(.85, 1.15)); F->Q.SetValue(St.Q);
	N->Connect(F)->Connect(E); Out(E, bInStair ? .9 : 0);
	Env(*E, T, St.Gain * Softness * Rand(.8, 1.2), .004, St.Decay);
	N->Start(T); N->Stop(T + St.Decay + .05);
	if (Surface == TEXT("gravel"))
		for (int32 i = 1; i < 4; i++)
		{
			FSrcPtr N2 = Noise(false); FFiltPtr F2 = Ctx->CreateBiquadFilter(); FGainPtr E2 = Ctx->CreateGain();
			const double At = T + i * .018;
			F2->Type = EHKBiquadType::Highpass; F2->Frequency.SetValue(4000); N2->Connect(F2)->Connect(E2); Out(E2);
			Env(*E2, At, St.Gain * .6 * Softness, .002, .02); N2->Start(At); N2->Stop(At + .04);
		}
	if (Surface == TEXT("wood") || Surface == TEXT("stone") || Surface == TEXT("tile"))
	{
		FOscPtr O = Ctx->CreateOscillator(); FGainPtr E3 = Ctx->CreateGain();
		O->Frequency.SetValue(Surface == TEXT("wood") ? 120 : 180); O->Connect(E3); Out(E3, bInStair ? .9 : 0);
		Env(*E3, T, .08 * Softness, .003, .07); O->Start(T); O->Stop(T + .1);
	}
}

// ---------- One-shot effects ----------
FString FHKGameAudio::CanonicalSfx(const FString& InName, double* OutDistance)
{
	static const TMap<FString, FString> Names = []()
	{
		TMap<FString, FString> M;
		for (const TCHAR* N : {TEXT("doorOpen"), TEXT("doorClose"), TEXT("locked"), TEXT("carDoor"), TEXT("bottleOpen"), TEXT("gulp"), TEXT("shutter"),
			TEXT("beep"), TEXT("throw"), TEXT("chains"), TEXT("thud"), TEXT("boule"), TEXT("gravel"), TEXT("click"), TEXT("scrape"), TEXT("dump"),
			TEXT("hit"), TEXT("cheer"), TEXT("starter"), TEXT("crash"), TEXT("arm"), TEXT("pistolShot"), TEXT("pistolClick"), TEXT("hostileShot")})
			M.Add(FString(N).ToLower(), N);
		for (const TCHAR* N : {TEXT("shot"), TEXT("caseShot"), TEXT("gunshot"), TEXT("pistol")}) M.Add(FString(N).ToLower(), TEXT("pistolShot"));
		for (const TCHAR* N : {TEXT("reload"), TEXT("empty"), TEXT("dryFire"), TEXT("caseClick"), TEXT("emptyClick"), TEXT("reloadClick")}) M.Add(FString(N).ToLower(), TEXT("pistolClick"));
		for (const TCHAR* N : {TEXT("enemy"), TEXT("enemyShot"), TEXT("hostile"), TEXT("hostileFire")}) M.Add(FString(N).ToLower(), TEXT("hostileShot"));
		for (const TCHAR* N : {TEXT("door_open"), TEXT("open")}) M.Add(N, TEXT("doorOpen"));
		for (const TCHAR* N : {TEXT("door_close"), TEXT("close")}) M.Add(N, TEXT("doorClose"));
		M.Add(TEXT("car_door"), TEXT("carDoor"));
		M.Add(TEXT("bottle_open"), TEXT("bottleOpen"));
		return M;
	}();
	FString Name = InName, Dist;
	if (InName.Split(TEXT(":"), &Name, &Dist) || InName.Split(TEXT("@"), &Name, &Dist))
	{
		if (OutDistance && Dist.IsNumeric()) *OutDistance = FCString::Atod(*Dist);
	}
	const FString* C = Names.Find(Name.TrimStartAndEnd().ToLower());
	return C ? *C : FString();
}

void FHKGameAudio::Sfx(const FString& InName, const FHKSfxOpts& Opts)
{
	double Distance = 1;
	const FString Name = CanonicalSfx(InName, &Distance);
	if (Name == TEXT("pistolShot")) { CaseSound(TEXT("shot"), 1, Opts.Gain); return; }
	if (Name == TEXT("pistolClick")) { CaseSound(TEXT("click"), 1, Opts.Gain); return; }
	if (Name == TEXT("hostileShot")) { CaseSound(TEXT("enemy"), Distance, Opts.Gain); return; }
	if (!Ctx) return;
	const double T = Now(), Level = Opts.Gain, Verb = Opts.Reverb >= 0 ? Opts.Reverb : 0;
	// burst(freq, q, peak, attack, decay, type = 'bandpass', at = t)
	auto Burst = [&](double Freq, double Q, double Peak, double Attack, double Decay, EHKBiquadType Type = EHKBiquadType::Bandpass, double At = -1)
	{
		if (At < 0) At = T;
		FSrcPtr N = Noise(false); FFiltPtr F = Ctx->CreateBiquadFilter(); FGainPtr E = Ctx->CreateGain();
		F->Type = Type; F->Frequency.SetValue(Freq); F->Q.SetValue(Q); N->Connect(F)->Connect(E); Out(E, Verb);
		Env(*E, At, Peak * Level, Attack, Decay); N->Start(At); N->Stop(At + Attack + Decay + .05);
	};
	// tone(f0, f1, peak, attack, decay, type = 'sine', at = t)
	auto Tone = [&](double F0, double F1, double Peak, double Attack, double Decay, EHKOscType Type = EHKOscType::Sine, double At = -1)
	{
		if (At < 0) At = T;
		FOscPtr O = Ctx->CreateOscillator(); FGainPtr E = Ctx->CreateGain();
		O->Type = Type; O->Frequency.SetValueAtTime(F0, At); O->Frequency.ExponentialRampToValueAtTime(FMath::Max(20.0, F1), At + Attack + Decay);
		O->Connect(E); Out(E, Verb); Env(*E, At, Peak * Level, Attack, Decay); O->Start(At); O->Stop(At + Attack + Decay + .05);
	};
	using B = EHKBiquadType;
	using O = EHKOscType;
	if (Name == TEXT("doorOpen")) { Tone(320, 520, .03, .05, .35, O::Sawtooth); Burst(600, 1, .05, .01, .2); }
	else if (Name == TEXT("doorClose")) { Burst(160, .8, .5, .004, .25, B::Lowpass); Tone(90, 50, .3, .003, .2); }
	else if (Name == TEXT("locked")) { for (int32 i = 0; i < 3; i++) Burst(2600, 6, .12, .002, .05, B::Bandpass, T + i * .07); }
	else if (Name == TEXT("carDoor")) { Burst(220, .8, .5, .004, .3, B::Lowpass); Tone(70, 40, .4, .003, .25); }
	else if (Name == TEXT("bottleOpen")) { Burst(5000, .7, .25, .01, .35, B::Highpass); Tone(1800, 1200, .05, .002, .04); }
	else if (Name == TEXT("gulp")) { for (int32 i = 0; i < 3; i++) Tone(160, 90, .25, .02, .12, O::Sine, T + .25 + i * .32); }
	else if (Name == TEXT("shutter")) { Burst(4000, 2, .3, .001, .03); Burst(1500, 2, .2, .001, .05, B::Bandpass, T + .07); }
	else if (Name == TEXT("beep")) { Tone(2000, 2000, .05, .005, .08, O::Square); }
	else if (Name == TEXT("throw")) { Burst(900, .7, .12, .05, .25); }
	else if (Name == TEXT("chains"))
	{
		for (int32 i = 0; i < 9; i++)
		{
			const double F0 = Rand(1200, 4800), F1 = Rand(1100, 4600), D = Rand(.2, .7);
			Tone(F0, F1, .05, .002, D, O::Triangle, T + i * .025);
		}
		Burst(3000, 1, .2, .002, .2);
	}
	else if (Name == TEXT("thud")) { Burst(260, 1, .25, .003, .12, B::Lowpass); }
	// Paintball (new feature, 28 Oct 2026): the marker's pneumatic pop, a ball breaking, a ball bouncing off, the
	// referee's whistle.
	else if (Name == TEXT("pbShot")) { Burst(1400, .9, .35, .001, .05); Burst(180, 1, .3, .002, .07, B::Lowpass); Burst(5000, 2, .06, .01, .09); }
	else if (Name == TEXT("pbSplat")) { Burst(700, .8, .3, .001, .08); Burst(2200, 1.5, .1, .004, .05); }
	else if (Name == TEXT("pbBounce")) { Burst(1800, 3, .15, .001, .03); }
	else if (Name == TEXT("whistle")) { Tone(2900, 3000, .12, .02, .5, O::Sine); Tone(3150, 3100, .06, .02, .5, O::Sine); }
	else if (Name == TEXT("boule")) { Tone(2600, 2500, .25, .001, .25, O::Triangle); Tone(5200, 5100, .1, .001, .15); Burst(3500, 3, .2, .001, .03); }
	else if (Name == TEXT("gravel")) { Burst(2500, .8, .15, .01, .18); }
	else if (Name == TEXT("click")) { Burst(3000, 5, .15, .001, .02); }
	else if (Name == TEXT("scrape")) { Burst(800, .6, .25, .05, .4); }
	else if (Name == TEXT("dump")) { Burst(180, .7, .5, .01, .6, B::Lowpass); Burst(1200, .5, .2, .02, .5); }
	else if (Name == TEXT("hit")) { Burst(500, 1, .4, .002, .15, B::Lowpass); }
	else if (Name == TEXT("cheer"))
	{
		for (int32 i = 0; i < 6; i++)
		{
			const double F0 = Rand(300, 700), F1 = Rand(400, 900);
			Tone(F0, F1, .04, .05, .4, O::Sawtooth, T + i * .05);
		}
	}
	else if (Name == TEXT("starter")) { Tone(220, 320, .08, .02, .5, O::Square); }
	else if (Name == TEXT("crash"))
	{
		Burst(220, .7, 1, .003, 1.4, B::Lowpass); Burst(1600, .6, .5, .002, .7); Tone(70, 30, .6, .003, .9);
		for (int32 i = 0; i < 6; i++)
		{
			const double F = Rand(2500, 6000), At = T + .1 + i * Rand(.05, .14);
			Burst(F, 3, .15, .001, .08, B::Bandpass, At);
		}
	}
	else if (Name == TEXT("arm")) { Tone(880, 880, .05, .005, .07, O::Square); Tone(1320, 1320, .05, .005, .1, O::Square, T + .12); }
}

// ---------- Mission weapon (case-game.js sound(kind, distance)) ----------
// The web game plays these on a separate AudioContext straight to the speakers (no master gain).
void FHKGameAudio::CaseSound(const FString& InKind, double Distance, double GainScale)
{
	Init();
	FString Kind = InKind;
	// A hostile shot reuses the pistol sound, darker and quieter with distance.
	const bool bEnemy = Kind == TEXT("enemy");
	if (bEnemy) Kind = TEXT("shot");
	const bool bShot = Kind == TEXT("shot");
	const double T = Now(), Length = bShot ? .32 : .05;
	TSharedPtr<FHKSynthBuffer> Buffer = Ctx->CreateBuffer(1, int32(Ctx->SampleRate * Length));
	float* D = Buffer->Channel(0);
	for (int32 i = 0; i < Buffer->Length; i++) D[i] = float((MathRandom() * 2 - 1) * pow(1 - double(i) / Buffer->Length, bShot ? 4 : 2));
	FSrcPtr Src = Ctx->CreateBufferSource(); FFiltPtr Filter = Ctx->CreateBiquadFilter(); FGainPtr G = Ctx->CreateGain();
	Src->Buffer = Buffer; Filter->Type = EHKBiquadType::Lowpass;
	Filter->Frequency.SetValueAtTime(bEnemy ? 1500 + 1200 / FMath::Max(1.0, Distance) : bShot ? 3200 : 5000, T);
	Filter->Frequency.ExponentialRampToValueAtTime(bShot ? 300 : 2000, T + Length);
	G->Gain.SetValueAtTime((bEnemy ? FMath::Min(.8, 2.2 / FMath::Max(1.0, Distance)) : bShot ? .9 : .25) * GainScale, T);
	Src->Connect(Filter)->Connect(G)->Connect(Ctx->Destination); Src->Start(T);
	if (bShot)
	{
		FOscPtr Osc = Ctx->CreateOscillator(); FGainPtr G2 = Ctx->CreateGain();
		Osc->Frequency.SetValueAtTime(140, T); Osc->Frequency.ExponentialRampToValueAtTime(40, T + .18);
		G2->Gain.SetValueAtTime(.7 * GainScale, T); G2->Gain.ExponentialRampToValueAtTime(.001 * GainScale, T + .2);
		Osc->Connect(G2)->Connect(Ctx->Destination); Osc->Start(T); Osc->Stop(T + .2);
	}
}

// ---------- Engines ----------
namespace
{
	struct FSpec { int32 Cyl; double Idle, Max, Tone, Gain; };

	struct FHyd { FOscPtr O; FGainPtr Hg; FSrcPtr Hn; };
	TUniquePtr<FHyd> MakeHydraulics(FHKGameAudio& A)
	{
		// const o = ctx.createOscillator(), hg = ctx.createGain(), hn = noise(), hf = ctx.createBiquadFilter(); ...
		FHKSynthContext& Ctx = *A.Context();
		TUniquePtr<FHyd> H = MakeUnique<FHyd>();
		FOscPtr O = Ctx.CreateOscillator(); FGainPtr Hg = Ctx.CreateGain(); FSrcPtr Hn = A.Noise(); FFiltPtr Hf = Ctx.CreateBiquadFilter();
		O->Type = EHKOscType::Sawtooth; Hf->Type = EHKBiquadType::Bandpass; Hf->Frequency.SetValue(1400); Hf->Q.SetValue(2); Hg->Gain.SetValue(0);
		O->Connect(Hg); Hn->Connect(Hf)->Connect(Hg); A.Out(Hg); O->Start(); Hn->Start();
		H->O = O; H->Hg = Hg; H->Hn = Hn;
		return H;
	}
	void SetHydraulics(FHKGameAudio& A, FHyd& H, double Flow)
	{
		H.O->Frequency.SetTargetAtTime(380 + 260 * Flow, A.Now(), .05);
		H.Hg->Gain.SetTargetAtTime(.05 * Flow, A.Now(), .08);
	}
	void StopHydraulicsLater(FHKGameAudio& A, const FHyd& H, double Delay)
	{
		// setTimeout(..., 900): hyd.hg.gain.value = 0; hyd.o.stop(). (The web never stops the noise; at gain 0 it is
		// silent, here it is stopped too so it does not use CPU.)
		FGainPtr Hg = H.Hg; FOscPtr O = H.O; FSrcPtr Hn = H.Hn; FHKSynthContext* Ctx = A.Context();
		Ctx->SetTimeout([Hg, O, Hn, Ctx]() { Hg->Gain.SetValue(0); O->Stop(Ctx->CurrentTime()); Hn->Stop(Ctx->CurrentTime()); }, Delay);
	}

	class FPistonEngine : public FHKEngineVoice
	{
	public:
		FHKGameAudio& A;
		FSpec Spec;
		FOscPtr Saw, Sq, Sub, AmOsc;
		FSrcPtr N;
		FFiltPtr Lp;
		FGainPtr G;
		TUniquePtr<FHyd> Hyd;
		FPistonEngine(FHKGameAudio& InA, const FSpec& InSpec) : A(InA), Spec(InSpec)
		{
			FHKSynthContext& Ctx = *A.Context();
			const double T = A.Now();
			Saw = Ctx.CreateOscillator(); Sq = Ctx.CreateOscillator(); Sub = Ctx.CreateOscillator();
			Saw->Type = EHKOscType::Sawtooth; Sq->Type = EHKOscType::Square; Sub->Type = EHKOscType::Sine;
			TSharedPtr<FHKSynthShaper> Shaper = Ctx.CreateWaveShaper();
			Shaper->Curve.SetNum(1024);
			for (int32 i = 0; i < 1024; i++) { const double X = i / 512.0 - 1; Shaper->Curve[i] = float(tanh(X * 3)); }
			Lp = Ctx.CreateBiquadFilter(); Lp->Type = EHKBiquadType::Lowpass; G = Ctx.CreateGain(); G->Gain.SetValue(0);
			FGainPtr Mix = Ctx.CreateGain(); Mix->Gain.SetValue(.5); Saw->Connect(Mix);
			FGainPtr SqG = Ctx.CreateGain(); SqG->Gain.SetValue(.25); Sq->Connect(SqG)->Connect(Mix);
			FGainPtr SubG = Ctx.CreateGain(); SubG->Gain.SetValue(.6); Sub->Connect(SubG)->Connect(Mix);
			// Exhaust roughness: noise amplitude-modulated at the firing frequency.
			N = A.Noise(); FFiltPtr Nf = Ctx.CreateBiquadFilter(); FGainPtr NG = Ctx.CreateGain(), Am = Ctx.CreateGain();
			Nf->Type = EHKBiquadType::Bandpass; Nf->Frequency.SetValue(900); Nf->Q.SetValue(.7); NG->Gain.SetValue(.25);
			N->Connect(Nf)->Connect(Am)->Connect(NG)->Connect(Mix);
			AmOsc = Ctx.CreateOscillator(); AmOsc->Type = EHKOscType::Square; FGainPtr AmDepth = Ctx.CreateGain(); AmDepth->Gain.SetValue(.5);
			AmOsc->Connect(AmDepth)->Connect(Am->Gain); Am->Gain.SetValue(.5);
			Mix->Connect(Shaper)->Connect(Lp)->Connect(G); A.Out(G);
			for (const FOscPtr& O : {Saw, Sq, Sub, AmOsc}) O->Start(T);
			N->Start(T);
		}
		virtual void Set(double Rpm, double Throttle) override
		{
			const double Tt = A.Now(), Fire = Rpm / 60 * Spec.Cyl / 2;
			Saw->Frequency.SetTargetAtTime(Fire, Tt, .03); Sq->Frequency.SetTargetAtTime(Fire * 2.01, Tt, .03); Sub->Frequency.SetTargetAtTime(Fire * .5, Tt, .03); AmOsc->Frequency.SetTargetAtTime(Fire, Tt, .03);
			Lp->Frequency.SetTargetAtTime(300 + 2600 * Spec.Tone * (.25 + .75 * Throttle) * FMath::Min(1.0, Rpm / Spec.Max + .2), Tt, .05);
			G->Gain.SetTargetAtTime(Spec.Gain * (.35 + .65 * Throttle) * (.7 + .3 * Rpm / Spec.Max), Tt, .05);
		}
		virtual void Hydraulics(double Flow) override
		{
			if (!Hyd) Hyd = MakeHydraulics(A);
			SetHydraulics(A, *Hyd, Flow);
		}
		virtual void Stop() override
		{
			const double Tt = A.Now();
			G->Gain.SetTargetAtTime(0, Tt, .15);
			FOscPtr S1 = Saw, S2 = Sq, S3 = Sub, S4 = AmOsc; FSrcPtr S5 = N; FHKSynthContext* Ctx = A.Context();
			Ctx->SetTimeout([S1, S2, S3, S4, S5, Ctx]() { const double Now = Ctx->CurrentTime(); S1->Stop(Now); S2->Stop(Now); S3->Stop(Now); S4->Stop(Now); S5->Stop(Now); }, .9);
			if (Hyd) StopHydraulicsLater(A, *Hyd, .9);
		}
	};

	// Helicopter: main-rotor blade slap (noise, amplitude-modulated at the blade-passing rate), turbine whine, tail-rotor buzz.
	class FHeliEngine : public FHKEngineVoice
	{
	public:
		FHKGameAudio& A;
		FOscPtr Lfo, Tur, Tur2, Tail;
		FSrcPtr N, Rumble;
		FGainPtr SlapG, RG, TG, TailG;
		explicit FHeliEngine(FHKGameAudio& InA) : A(InA)
		{
			FHKSynthContext& Ctx = *A.Context();
			const double T = A.Now();
			N = A.Noise(); FFiltPtr Bp = Ctx.CreateBiquadFilter(); FGainPtr Am = Ctx.CreateGain(); SlapG = Ctx.CreateGain(); Lfo = Ctx.CreateOscillator(); FGainPtr Depth = Ctx.CreateGain();
			Bp->Type = EHKBiquadType::Bandpass; Bp->Frequency.SetValue(260); Bp->Q.SetValue(.9); Am->Gain.SetValue(.35); Lfo->Type = EHKOscType::Sawtooth; Depth->Gain.SetValue(.65); Lfo->Connect(Depth)->Connect(Am->Gain);
			SlapG->Gain.SetValue(0); N->Connect(Bp)->Connect(Am)->Connect(SlapG); A.Out(SlapG, .08);
			Rumble = A.Noise(); FFiltPtr Rlp = Ctx.CreateBiquadFilter(); RG = Ctx.CreateGain();
			Rlp->Type = EHKBiquadType::Lowpass; Rlp->Frequency.SetValue(110); RG->Gain.SetValue(0); Rumble->Connect(Rlp)->Connect(RG); A.Out(RG);
			Tur = Ctx.CreateOscillator(); Tur2 = Ctx.CreateOscillator(); TG = Ctx.CreateGain();
			Tur->Type = EHKOscType::Sine; Tur2->Type = EHKOscType::Triangle; TG->Gain.SetValue(0); Tur->Connect(TG); Tur2->Connect(TG); A.Out(TG);
			Tail = Ctx.CreateOscillator(); FFiltPtr Tlp = Ctx.CreateBiquadFilter(); TailG = Ctx.CreateGain();
			Tail->Type = EHKOscType::Sawtooth; Tlp->Type = EHKBiquadType::Lowpass; Tlp->Frequency.SetValue(420); TailG->Gain.SetValue(0); Tail->Connect(Tlp)->Connect(TailG); A.Out(TailG);
			for (const FOscPtr& O : {Lfo, Tur, Tur2, Tail}) O->Start(T);
			N->Start(T); Rumble->Start(T);
		}
		// set(rotor 0..1, load 0..1)
		virtual void Set(double Rpm, double Load) override
		{
			const double Tt = A.Now(), Blade = 3 * 6.4 * Rpm;
			Lfo->Frequency.SetTargetAtTime(FMath::Max(.5, Blade), Tt, .1); Tail->Frequency.SetTargetAtTime(30 + 85 * Rpm, Tt, .1);
			Tur->Frequency.SetTargetAtTime(500 + 1150 * Rpm, Tt, .2); Tur2->Frequency.SetTargetAtTime(1000 + 2300 * Rpm, Tt, .2);
			SlapG->Gain.SetTargetAtTime(.32 * Rpm * Rpm * (.6 + .5 * Load), Tt, .1); RG->Gain.SetTargetAtTime(.22 * Rpm * (.5 + .5 * Load), Tt, .1);
			TG->Gain.SetTargetAtTime(.012 * Rpm, Tt, .2); TailG->Gain.SetTargetAtTime(.025 * Rpm, Tt, .1);
		}
		virtual void Stop() override
		{
			const double Tt = A.Now();
			for (const FGainPtr& G : {SlapG, RG, TG, TailG}) G->Gain.SetTargetAtTime(0, Tt, .4);
			FOscPtr S1 = Lfo, S2 = Tur, S3 = Tur2, S4 = Tail; FSrcPtr S5 = N, S6 = Rumble; FHKSynthContext* Ctx = A.Context();
			Ctx->SetTimeout([S1, S2, S3, S4, S5, S6, Ctx]() { const double Now = Ctx->CurrentTime(); S1->Stop(Now); S2->Stop(Now); S3->Stop(Now); S4->Stop(Now); S5->Stop(Now); S6->Stop(Now); }, 2.5);
		}
	};

	// FPV quad: four slightly detuned motors (whine) plus propeller wash. set(thrust 0..1, level 0..1 = distance from the pilot)
	class FDroneMotors : public FHKEngineVoice
	{
	public:
		FHKGameAudio& A;
		FGainPtr G, WG;
		TArray<TPair<FOscPtr, double>> Motors;
		FSrcPtr Wash;
		explicit FDroneMotors(FHKGameAudio& InA) : A(InA)
		{
			FHKSynthContext& Ctx = *A.Context();
			const double T = A.Now();
			FFiltPtr Bp = Ctx.CreateBiquadFilter(); G = Ctx.CreateGain();
			Bp->Type = EHKBiquadType::Bandpass; Bp->Frequency.SetValue(1400); Bp->Q.SetValue(.5); G->Gain.SetValue(0); Bp->Connect(G); A.Out(G);
			for (double K : {1.0, 1.013, .987, 1.021})
			{
				FOscPtr O = Ctx.CreateOscillator(); O->Type = EHKOscType::Sawtooth; O->Connect(Bp); O->Start(T);
				Motors.Add({O, K});
			}
			Wash = A.Noise(); FFiltPtr Wf = Ctx.CreateBiquadFilter(); WG = Ctx.CreateGain();
			Wf->Type = EHKBiquadType::Bandpass; Wf->Frequency.SetValue(2600); Wf->Q.SetValue(.6); WG->Gain.SetValue(0); Wash->Connect(Wf)->Connect(WG); A.Out(WG); Wash->Start(T);
		}
		virtual void Set(double Thrust, double Level) override
		{
			const double Tt = A.Now();
			for (const TPair<FOscPtr, double>& M : Motors) M.Key->Frequency.SetTargetAtTime((150 + 560 * Thrust) * M.Value, Tt, .04);
			G->Gain.SetTargetAtTime((.02 + .07 * Thrust) * Level, Tt, .05);
			WG->Gain.SetTargetAtTime(.05 * Thrust * Level, Tt, .05);
		}
		virtual void Stop() override
		{
			const double Tt = A.Now();
			G->Gain.SetTargetAtTime(0, Tt, .1); WG->Gain.SetTargetAtTime(0, Tt, .1);
			TArray<FOscPtr> Os; for (const TPair<FOscPtr, double>& M : Motors) Os.Add(M.Key);
			FSrcPtr W = Wash; FHKSynthContext* Ctx = A.Context();
			Ctx->SetTimeout([Os, W, Ctx]() { const double Now = Ctx->CurrentTime(); for (const FOscPtr& O : Os) O->Stop(Now); W->Stop(Now); }, .8);
		}
	};

	// The excavator's hydraulics on its own handle: Engine("hydraulics") -> Set(flow), Stop().
	class FHydraulicsVoice : public FHKEngineVoice
	{
	public:
		FHKGameAudio& A;
		TUniquePtr<FHyd> Hyd;
		explicit FHydraulicsVoice(FHKGameAudio& InA) : A(InA) {}
		virtual void Set(double Flow, double) override { Hydraulics(Flow); }
		virtual void Hydraulics(double Flow) override { if (!Hyd) Hyd = MakeHydraulics(A); SetHydraulics(A, *Hyd, Flow); }
		virtual void Stop() override { if (Hyd) StopHydraulicsLater(A, *Hyd, .9); }
	};

	class FSilentVoice : public FHKEngineVoice
	{
	public:
		virtual void Set(double, double) override {}
		virtual void Stop() override {}
	};
}

TSharedPtr<FHKEngineVoice> FHKGameAudio::Engine(const FString& InKind)
{
	if (!Init()) return MakeShared<FSilentVoice>();
	FString Kind = InKind.ToLower();
	if (Kind == TEXT("motorcycle")) Kind = TEXT("bike");
	if (Kind == TEXT("helicopter")) Kind = TEXT("heli");
	if (Kind == TEXT("heli")) return MakeShared<FHeliEngine>(*this);
	if (Kind == TEXT("drone")) return MakeShared<FDroneMotors>(*this);
	if (Kind == TEXT("hydraulics")) return MakeShared<FHydraulicsVoice>(*this);
	FSpec Spec = {4, 800, 6200, .6, .16};   // car (also for unknown kinds, like game-main's `|| 'car'`)
	if (Kind == TEXT("bike")) Spec = {4, 1300, 11500, 1, .22};
	else if (Kind == TEXT("excavator")) Spec = {4, 900, 2100, .45, .2};
	else if (Kind == TEXT("diesel")) Spec = {6, 650, 2200, .4, .22};   // construction-site machines and trucks (new)
	else if (Kind == TEXT("plane")) Spec = {4, 700, 2750, .85, .22};
	return MakeShared<FPistonEngine>(*this, Spec);
}

void FHKGameAudio::RenderBlock(const float*& L, const float*& R)
{
	if (!Ctx) { L = R = Silence; return; }
	Ctx->RenderBlock(L, R);
}

// ============================================================================================================
// Engine (audio thread) and command queue
// ============================================================================================================

FHKAudioEngine::FHKAudioEngine(double InSampleRate, uint32 Seed)
	: Audio(InSampleRate, Seed), Music(InSampleRate), SampleRate(InSampleRate)
{
	FMemory::Memzero(BlockL, sizeof(BlockL));
	FMemory::Memzero(BlockR, sizeof(BlockR));
}

void FHKAudioEngine::Enqueue(TFunction<void(FHKAudioEngine&)>&& Command)
{
	FScopeLock Guard(&Lock);
	Pending.Add(MoveTemp(Command));
}

void FHKAudioEngine::RenderBlock()
{
	const float* L;
	const float* R;
	const bool bWasRunning = Audio.IsRunning();
	Audio.RenderBlock(L, R);
	FMemory::Memcpy(BlockL, L, sizeof(BlockL));
	FMemory::Memcpy(BlockR, R, sizeof(BlockR));
	// The music attaches to the game's audio context once it exists (game-main: if (audio.context) music.attach(...)).
	if (bWasRunning && !Music.IsAttached()) Music.Attach(MusicVolume);
	Music.RenderBlock(BlockL, BlockR);
	BlockPos = 0;
}

void FHKAudioEngine::Render(float* Out, int32 Frames, int32 Channels)
{
	const double T0 = FPlatformTime::Seconds();
	TArray<TFunction<void(FHKAudioEngine&)>> Run;
	{
		FScopeLock Guard(&Lock);
		Swap(Run, Pending);
	}
	for (TFunction<void(FHKAudioEngine&)>& C : Run) C(*this);
	Run.Empty();
	for (int32 f = 0; f < Frames; f++)
	{
		if (BlockPos >= Block) RenderBlock();
		const float Lv = BlockL[BlockPos] * OutputGain, Rv = BlockR[BlockPos] * OutputGain;
		BlockPos++;
		PeakOutput = FMath::Max(PeakOutput, FMath::Max(FMath::Abs(Lv), FMath::Abs(Rv)));
		SumSquares += double(Lv) * Lv + double(Rv) * Rv;
		if (Channels == 1) Out[f] = .5f * (Lv + Rv);
		else
		{
			Out[f * Channels] = Lv;
			Out[f * Channels + 1] = Rv;
			for (int32 c = 2; c < Channels; c++) Out[f * Channels + c] = 0;
		}
	}
	FramesRendered += Frames;
	CpuSeconds += FPlatformTime::Seconds() - T0;
}

// ============================================================================================================
// Game-thread front ends
// ============================================================================================================

FHKEngineSoundImpl::FHKEngineSoundImpl(const TSharedRef<FHKAudioEngine, ESPMode::ThreadSafe>& InEngine, const FString& Kind)
	: Engine(InEngine), Slot(MakeShared<FSlot, ESPMode::ThreadSafe>())
{
	TSharedPtr<FSlot, ESPMode::ThreadSafe> S = Slot;
	InEngine->Enqueue([S, Kind](FHKAudioEngine& E) { S->Voice = E.Audio.Engine(Kind); });
}

FHKEngineSoundImpl::~FHKEngineSoundImpl()
{
	// The voice lives on the audio thread: stop it (if the game did not) and release it there.
	if (TSharedPtr<FHKAudioEngine, ESPMode::ThreadSafe> E = Engine.Pin())
	{
		TSharedPtr<FSlot, ESPMode::ThreadSafe> S = MoveTemp(Slot);
		const bool bStop = !bStopped;
		E->Enqueue([S, bStop](FHKAudioEngine&) { if (S->Voice && bStop) S->Voice->Stop(); S->Voice.Reset(); });
	}
}

void FHKEngineSoundImpl::Set(double A, double B)
{
	if (TSharedPtr<FHKAudioEngine, ESPMode::ThreadSafe> E = Engine.Pin())
	{
		TSharedPtr<FSlot, ESPMode::ThreadSafe> S = Slot;
		E->Enqueue([S, A, B](FHKAudioEngine&) { if (S->Voice) S->Voice->Set(A, B); });
	}
}

void FHKEngineSoundImpl::Hydraulics(double Flow)
{
	if (TSharedPtr<FHKAudioEngine, ESPMode::ThreadSafe> E = Engine.Pin())
	{
		TSharedPtr<FSlot, ESPMode::ThreadSafe> S = Slot;
		E->Enqueue([S, Flow](FHKAudioEngine&) { if (S->Voice) S->Voice->Hydraulics(Flow); });
	}
}

void FHKEngineSoundImpl::Stop()
{
	if (bStopped) return;
	bStopped = true;
	if (TSharedPtr<FHKAudioEngine, ESPMode::ThreadSafe> E = Engine.Pin())
	{
		TSharedPtr<FSlot, ESPMode::ThreadSafe> S = Slot;
		E->Enqueue([S](FHKAudioEngine&) { if (S->Voice) S->Voice->Stop(); });
	}
}

void FHKAudioImpl::Resume() { EngineRef->Enqueue([](FHKAudioEngine& E) { E.Audio.Resume(); }); }
void FHKAudioImpl::Ambience(const FHKAmbience& State) { EngineRef->Enqueue([State](FHKAudioEngine& E) { E.Audio.Ambience(State); }); }
void FHKAudioImpl::Footstep(const FString& Surface, bool bStair, double Soft) { EngineRef->Enqueue([Surface, bStair, Soft](FHKAudioEngine& E) { E.Audio.Footstep(Surface, bStair, Soft); }); }
void FHKAudioImpl::Sfx(const FString& Name, const FHKSfxOpts& Opts) { EngineRef->Enqueue([Name, Opts](FHKAudioEngine& E) { E.Audio.Sfx(Name, Opts); }); }
void FHKAudioImpl::Fire(double Level, double Near) { EngineRef->Enqueue([Level, Near](FHKAudioEngine& E) { E.Audio.Fire(Level, Near); }); }
TSharedPtr<IHKEngineSound> FHKAudioImpl::Engine(const FString& Kind) { return MakeShared<FHKEngineSoundImpl>(EngineRef, Kind); }

namespace
{
	const TCHAR* MusicSection = TEXT("Haarklous.Music");
	const TCHAR* MusicKey = TEXT("haarklous-music-volume-v1");
}

double FHKMusicImpl::LoadVolume()
{
	// try {const v = localStorage.getItem('haarklous-music-volume-v1'); if (v !== null && v !== '') volume = clamp(+v);}
	FString V;
	if (GConfig && GConfig->GetString(MusicSection, MusicKey, V, GGameUserSettingsIni) && !V.IsEmpty() && V.IsNumeric())
		return FMath::Clamp(FCString::Atod(*V), 0.0, 1.0);
	return .5;
}

FHKMusicImpl::FHKMusicImpl(const TSharedRef<FHKAudioEngine, ESPMode::ThreadSafe>& InEngine) : Engine(InEngine)
{
	Volume = LoadVolume();
	const double V = Volume;
	Engine->Enqueue([V](FHKAudioEngine& E) { E.MusicVolume = V; E.Music.SetVolume(V); });
}

void FHKMusicImpl::Play(const FString& Track)
{
	const FString Name = FHKMusicPlayer::Track(Track) ? Track : FString();
	if (Name == Current) return;
	Current = Name;
	Engine->Enqueue([Name](FHKAudioEngine& E) { E.Music.Play(Name); });
}

void FHKMusicImpl::SetVolume(double V)
{
	// volume = Math.round(clamp(v) * 100) / 100; localStorage.setItem(...); master.gain.setTargetAtTime(level(), now, .1)
	Volume = FMath::RoundToDouble(FMath::Clamp(V, 0.0, 1.0) * 100) / 100;
	if (GConfig)
	{
		GConfig->SetString(MusicSection, MusicKey, *FString::SanitizeFloat(Volume), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	const double Vol = Volume;
	Engine->Enqueue([Vol](FHKAudioEngine& E) { E.MusicVolume = Vol; E.Music.SetVolume(Vol); });
}

void FHKMusicImpl::Position(FString& Title, double& LengthSeconds) const
{
	const FHKMusicTrackInfo* I = FHKMusicPlayer::Track(Current);
	Title = I ? I->Title : FString();
	LengthSeconds = I ? I->Seconds : 0;
}

void FHKMusicImpl::LoadTracks()
{
	// Content/Data/audio/music.json: {"tracks": {"main": {"file": "music-main.wav", "gain": 1, ...}, ...}}
	const FString Dir = FHKMusicPlayer::AudioDir();
	FString Text;
	TMap<FString, TPair<FString, double>> Files;
	if (FFileHelper::LoadFileToString(Text, *(Dir / TEXT("music.json"))))
	{
		TSharedPtr<FJsonObject> J;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) && J)
		{
			const TSharedPtr<FJsonObject>* T = nullptr;
			if (J->TryGetObjectField(TEXT("tracks"), T))
				for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*T)->Values)
				{
					const TSharedPtr<FJsonObject> O = P.Value->AsObject();
					if (O) Files.Add(P.Key, {O->GetStringField(TEXT("file")), O->HasField(TEXT("gain")) ? O->GetNumberField(TEXT("gain")) : 1.0});
				}
		}
	}
	TWeakPtr<FHKAudioEngine, ESPMode::ThreadSafe> Weak = Engine;
	for (const FHKMusicTrackInfo& I : FHKMusicPlayer::Tracks())
	{
		const TPair<FString, double>* F = Files.Find(I.Name);
		const FString Path = Dir / (F ? F->Key : FString::Printf(TEXT("music-%s.wav"), *I.Name));
		const double Gain = F ? F->Value : 1.0;
		const FString Name = I.Name;
		Async(EAsyncExecution::ThreadPool, [Weak, Path, Gain, Name]()
		{
			FString Error;
			TSharedPtr<const FHKMusicPCM> P = FHKMusicPCM::LoadWav(Path, Gain, &Error);
			if (!P) { UE_LOG(LogHaarklous, Warning, TEXT("Music: %s"), *Error); return; }
			if (TSharedPtr<FHKAudioEngine, ESPMode::ThreadSafe> E = Weak.Pin()) E->Enqueue([Name, P](FHKAudioEngine& En) { En.Music.SetTrackData(Name, P); });
		});
	}
}

// ============================================================================================================
// Unreal glue
// ============================================================================================================

UHKAudioSynthComponent::UHKAudioSynthComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bAllowSpatialization = false;
	bIsUISound = true;          // keeps playing in the pause menu, like the browser
	bAutoActivate = false;
	NumChannels = 2;
}

bool UHKAudioSynthComponent::Init(int32& SampleRate)
{
	NumChannels = 2;
	if (Engine) SampleRate = int32(Engine->SampleRate);
	return true;
}

int32 UHKAudioSynthComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	if (!Engine) { FMemory::Memzero(OutAudio, NumSamples * sizeof(float)); return NumSamples; }
	Engine->Render(OutAudio, NumSamples / NumChannels, NumChannels);
	return NumSamples;
}

AHKAudioActor::AHKAudioActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Synth = CreateDefaultSubobject<UHKAudioSynthComponent>(TEXT("Synth"));
	RootComponent = Synth;
}

AHKAudioActor* AHKAudioActor::Ensure(UWorld* World)
{
	if (!World) return nullptr;
	for (TActorIterator<AHKAudioActor> It(World); It; ++It) return *It;
	FActorSpawnParameters P;
	P.Name = TEXT("HKAudio");
	P.ObjectFlags |= RF_Transient;
	return World->SpawnActor<AHKAudioActor>(P);
}

void AHKAudioActor::BeginPlay()
{
	Super::BeginPlay();
	FAudioDeviceHandle Device = GetWorld() ? GetWorld()->GetAudioDevice() : FAudioDeviceHandle();
	if (!Device.IsValid())
	{
		UE_LOG(LogHaarklous, Log, TEXT("Audio: no audio device (-nosound?), the game stays silent"));
		return;
	}
	const double Rate = Device->GetSampleRate() > 0 ? Device->GetSampleRate() : 48000;
	Engine = MakeShared<FHKAudioEngine, ESPMode::ThreadSafe>(Rate, uint32(FPlatformTime::Cycles64() ^ 0x9E3779B9u) | 1u);
	// Unreal lowers every source by the platform headroom (-3 dB on Windows); undo it so levels match the browser.
	float HeadroomDb = 0;
	if (GConfig) GConfig->GetFloat(TEXT("Audio"), TEXT("PlatformHeadroomDB"), HeadroomDb, GEngineIni);
	Engine->OutputGain = FMath::Pow(10.f, -FMath::Min(0.f, HeadroomDb) / 20.f);
	Synth->Engine = Engine;
	AudioImpl = MakeUnique<FHKAudioImpl>(Engine.ToSharedRef());
	MusicImpl = MakeUnique<FHKMusicImpl>(Engine.ToSharedRef());
	MusicImpl->LoadTracks();
	FHKAudio::SetInstance(AudioImpl.Get());
	FHKMusic::SetInstance(MusicImpl.Get());
	Synth->Start();
	UE_LOG(LogHaarklous, Log, TEXT("Audio: running at %.0f Hz (output gain %.3f)"), Rate, Engine->OutputGain);
}

void AHKAudioActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Engine)
		UE_LOG(LogHaarklous, Log, TEXT("Audio: rendered %.1f s of sound, %.2f ms audio-thread time per second of sound, output peak %.3f, average %.1f dBFS, music %s"),
			Engine->FramesRendered / Engine->SampleRate, CpuMillisecondsPerSecond(), Engine->PeakOutput,
			10 * log10(Engine->SumSquares / FMath::Max<double>(1, 2.0 * Engine->FramesRendered) + 1e-30), *Engine->Music.Current);
	if (AudioImpl && &FHKAudio::Get() == AudioImpl.Get()) FHKAudio::SetInstance(nullptr);
	if (MusicImpl && &FHKMusic::Get() == MusicImpl.Get()) FHKMusic::SetInstance(nullptr);
	if (Synth) { Synth->Stop(); }
	AudioImpl.Reset();
	MusicImpl.Reset();
	Super::EndPlay(Reason);
}

double AHKAudioActor::CpuMillisecondsPerSecond() const
{
	if (!Engine || Engine->FramesRendered == 0) return 0;
	return Engine->CpuSeconds * 1000 / (Engine->FramesRendered / Engine->SampleRate);
}

bool UHKAudioSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UHKAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	AHKAudioActor::Ensure(&InWorld);
}
