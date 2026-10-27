// Audio tests: the C++ sound must sound like the web version.
// Reference renders come from the web game's own code in the browser: tools/web-export/reference-audio.js
// (Content/Data/tests/audio.json) and tools/web-export/render-music.mjs (Content/Data/audio/*.wav, music.json).
// Run: tools\run-tests.ps1 -Filter Haarklous.Audio
#include "Misc/AutomationTest.h"
#include "HKAudioImpl.h"
#include "HKData.h"
#include "HAL/PlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr double Rate = 48000;
	constexpr double Pi = 3.14159265358979323846;
	constexpr int32 Win = 960, NFft = 2048;

	struct FFeatures
	{
		TArray<double> Rms, Bands;
		double Peak = 0, Centroid = 0, Balance = 0, Level = -300;
	};

	// The same features as reference-audio.js computes in the browser.
	FFeatures Features(const TArray<float>& L, const TArray<float>& R, const TArray<double>& Edges)
	{
		FFeatures F;
		const int32 N = L.Num(), Nw = N / Win;
		double EL = 0, ER = 0;
		for (int32 w = 0; w < Nw; w++)
		{
			double E = 0;
			for (int32 i = w * Win; i < (w + 1) * Win; i++) E += double(L[i]) * L[i] + double(R[i]) * R[i];
			F.Rms.Add(FMath::Max(-120.0, 10 * log10(E / (2 * Win) + 1e-30)));
		}
		for (int32 i = 0; i < N; i++) { F.Peak = FMath::Max(F.Peak, double(FMath::Max(FMath::Abs(L[i]), FMath::Abs(R[i])))); EL += double(L[i]) * L[i]; ER += double(R[i]) * R[i]; }
		static TArray<double> Hann, CosT, SinT;
		if (Hann.Num() == 0)
		{
			Hann.SetNum(NFft); CosT.SetNum(NFft / 2); SinT.SetNum(NFft / 2);
			for (int32 i = 0; i < NFft; i++) Hann[i] = .5 - .5 * cos(2 * Pi * i / NFft);
			for (int32 k = 0; k < NFft / 2; k++) { CosT[k] = cos(2 * Pi * k / NFft); SinT[k] = sin(2 * Pi * k / NFft); }
		}
		TArray<int32> Starts;
		TArray<double> Energies;
		for (int32 s = 0; s + NFft <= N; s += NFft / 2)
		{
			double E = 0;
			for (int32 i = 0; i < NFft; i++) { const double M = .5 * (double(L[s + i]) + R[s + i]) * Hann[i]; E += M * M; }
			Starts.Add(s); Energies.Add(E);
		}
		double MaxE = 0;
		for (double E : Energies) MaxE = FMath::Max(MaxE, E);
		TArray<double> P, Re, Im;
		P.SetNumZeroed(NFft / 2 + 1); Re.SetNum(NFft); Im.SetNum(NFft);
		for (int32 f = 0; f < Starts.Num(); f++)
		{
			if (Energies[f] < MaxE * 1e-4 || Energies[f] <= 0) continue;
			const int32 s = Starts[f];
			for (int32 i = 0; i < NFft; i++) { Re[i] = .5 * (double(L[s + i]) + R[s + i]) * Hann[i]; Im[i] = 0; }
			for (int32 i = 1, j = 0; i < NFft; i++)
			{
				int32 Bit = NFft >> 1;
				for (; j & Bit; Bit >>= 1) j ^= Bit;
				j ^= Bit;
				if (i < j) { Swap(Re[i], Re[j]); Swap(Im[i], Im[j]); }
			}
			for (int32 Len = 2; Len <= NFft; Len <<= 1)
			{
				const int32 Half = Len >> 1, Step = NFft / Len;
				for (int32 i = 0; i < NFft; i += Len)
					for (int32 k = 0; k < Half; k++)
					{
						const double Wr = CosT[k * Step], Wi = -SinT[k * Step];
						const int32 a = i + k, b = a + Half;
						const double Xr = Re[b] * Wr - Im[b] * Wi, Xi = Re[b] * Wi + Im[b] * Wr;
						Re[b] = Re[a] - Xr; Im[b] = Im[a] - Xi; Re[a] += Xr; Im[a] += Xi;
					}
			}
			for (int32 k = 0; k <= NFft / 2; k++) P[k] += Re[k] * Re[k] + Im[k] * Im[k];
		}
		TArray<double> Bands;
		Bands.SetNumZeroed(Edges.Num() - 1);
		double Total = 0, Fp = 0;
		for (int32 k = 0; k <= NFft / 2; k++)
		{
			const double Fr = k * Rate / NFft;
			int32 b = 0;
			while (b < Bands.Num() - 1 && Fr >= Edges[b + 1]) b++;
			Bands[b] += P[k]; Total += P[k]; Fp += Fr * P[k];
		}
		for (double V : Bands) F.Bands.Add(10 * log10(V / (Total > 0 ? Total : 1) + 1e-12));
		F.Centroid = Total > 0 ? Fp / Total : 0;
		F.Balance = 10 * log10((EL + 1e-30) / (ER + 1e-30));
		F.Level = 10 * log10((EL + ER) / (2.0 * FMath::Max(1, N)) + 1e-30);
		return F;
	}

	FFeatures FromJson(const TSharedPtr<FJsonObject>& O)
	{
		FFeatures F;
		F.Rms = HKJson::Nums(O, TEXT("rms"));
		F.Bands = HKJson::Nums(O, TEXT("bands"));
		F.Peak = HKJson::Num(O, TEXT("peak"));
		F.Centroid = HKJson::Num(O, TEXT("centroid"));
		F.Balance = HKJson::Num(O, TEXT("balance"));
		F.Level = HKJson::Num(O, TEXT("level"));
		return F;
	}

	struct FDiff
	{
		double RmsMean = 0, RmsP95 = 0, Level = 0, Peak = 0, Bands = 0, Centroid = 1, Balance = 0;
		int32 Windows = 0;
	};

	// Differences C++ vs browser. RMS windows count where the browser is within 40 dB of its loudest window.
	FDiff Compare(const FFeatures& Mine, const FFeatures& Web)
	{
		FDiff D;
		double WebMax = -200;
		for (double V : Web.Rms) WebMax = FMath::Max(WebMax, V);
		TArray<double> Abs;
		for (int32 i = 0; i < FMath::Min(Mine.Rms.Num(), Web.Rms.Num()); i++)
			if (Web.Rms[i] >= FMath::Max(WebMax - 40, -85.0)) Abs.Add(FMath::Abs(Mine.Rms[i] - Web.Rms[i]));
		Abs.Sort();
		D.Windows = Abs.Num();
		for (double A : Abs) D.RmsMean += A;
		if (Abs.Num()) { D.RmsMean /= Abs.Num(); D.RmsP95 = Abs[FMath::Min(Abs.Num() - 1, int32(Abs.Num() * .95))]; }
		D.Level = Mine.Level - Web.Level;
		D.Peak = 20 * log10((Mine.Peak + 1e-12) / (Web.Peak + 1e-12));
		for (int32 b = 0; b < FMath::Min(Mine.Bands.Num(), Web.Bands.Num()); b++)
			if (Web.Bands[b] > -20) D.Bands = FMath::Max(D.Bands, FMath::Abs(Mine.Bands[b] - Web.Bands[b]));
		D.Centroid = Web.Centroid > 0 ? Mine.Centroid / Web.Centroid : 1;
		D.Balance = Mine.Balance - Web.Balance;
		return D;
	}

	FString Describe(const FDiff& D)
	{
		return FString::Printf(TEXT("level %+.2f dB, RMS/20ms mean %.2f p95 %.2f dB (%d windows), peak %+.2f dB, bands %.2f dB, centroid x%.3f, balance %+.2f dB"),
			D.Level, D.RmsMean, D.RmsP95, D.Windows, D.Peak, D.Bands, D.Centroid, D.Balance);
	}

	TSharedPtr<FJsonObject> LoadRef(FAutomationTestBase& Test)
	{
		FString Error;
		TSharedPtr<FJsonObject> J = HKJson::LoadFile(FHKData::DataDir() / TEXT("tests") / TEXT("audio.json"), &Error);
		if (!J) Test.AddError(TEXT("audio.json: ") + Error + TEXT(" (run node tools/web-export/export.mjs --only=tests --script=reference-audio.js)"));
		return J;
	}

	FHKAmbience AmbienceFrom(const TSharedPtr<FJsonObject>& O)
	{
		FHKAmbience A;
		A.Indoor = HKJson::Num(O, TEXT("indoor"));
		A.Green = HKJson::Num(O, TEXT("green"));
		A.Traffic = HKJson::Num(O, TEXT("traffic"));
		A.Height = HKJson::Num(O, TEXT("height"));
		A.bStair = HKJson::Bool(O, TEXT("stair"));
		A.bKitchen = HKJson::Bool(O, TEXT("kitchen"));
		return A;
	}

	// Replays a case's events through the C++ port, at the same render quanta as the browser.
	void RenderCase(const TSharedPtr<FJsonObject>& C, TArray<float>& L, TArray<float>& R)
	{
		const int32 Frames = int32(HKJson::Num(C, TEXT("frames")));
		const bool bTicks = HKJson::Bool(C, TEXT("ticks"));
		FHKGameAudio A(Rate, uint32(HKJson::Num(C, TEXT("seed"))), false);
		TMap<int32, TSharedPtr<FHKEngineVoice>> Engines;
		TMap<int64, TArray<TArray<TSharedPtr<FJsonValue>>>> Events;
		for (const TSharedPtr<FJsonValue>& E : HKJson::Arr(C, TEXT("events"))) { const TArray<TSharedPtr<FJsonValue>>& A2 = E->AsArray(); Events.FindOrAdd(int64(A2[0]->AsNumber())).Add(A2); }
		L.Reset(Frames); R.Reset(Frames);
		for (int64 b = 0; b * HKSynth::Block < Frames; b++)
		{
			if (bTicks && b > 0 && b % 45 == 0) A.TickAmbience();
			if (TArray<TArray<TSharedPtr<FJsonValue>>>* List = Events.Find(b))
				for (const TArray<TSharedPtr<FJsonValue>>& E : *List)
				{
					const FString Op = E[1]->AsString();
					auto N = [&E](int32 i) { return E[i]->AsNumber(); };
					if (Op == TEXT("resume")) A.Resume();
					else if (Op == TEXT("ambience")) { if (E[2]->Type == EJson::Object) A.Ambience(AmbienceFrom(E[2]->AsObject())); }
					else if (Op == TEXT("footstep")) A.Footstep(E[2]->AsString(), E[3]->AsBool(), N(4));
					else if (Op == TEXT("sfx"))
					{
						const TSharedPtr<FJsonObject> O = E[3]->AsObject();
						FHKSfxOpts Opts(O->HasField(TEXT("gain")) ? O->GetNumberField(TEXT("gain")) : 1.0, O->HasField(TEXT("reverb")) ? O->GetNumberField(TEXT("reverb")) : -1.0);
						A.Sfx(E[2]->AsString(), Opts);
					}
					else if (Op == TEXT("engine")) Engines.Add(int32(N(2)), A.Engine(E[3]->AsString()));
					else if (Op == TEXT("set")) Engines[int32(N(2))]->Set(N(3), N(4));
					else if (Op == TEXT("hyd")) Engines[int32(N(2))]->Hydraulics(N(3));
					else if (Op == TEXT("stop")) Engines[int32(N(2))]->Stop();
					else if (Op == TEXT("fire")) A.Fire(N(2), N(3));
					else if (Op == TEXT("case")) A.CaseSound(E[2]->AsString(), N(3));
				}
			const float* Lb;
			const float* Rb;
			A.RenderBlock(Lb, Rb);
			L.Append(Lb, HKSynth::Block);
			R.Append(Rb, HKSynth::Block);
		}
		L.SetNum(Frames); R.SetNum(Frames);
	}

	TSharedPtr<FJsonObject> LoadMusicMeta(FAutomationTestBase& Test)
	{
		FString Error;
		TSharedPtr<FJsonObject> J = HKJson::LoadFile(FHKMusicPlayer::AudioDir() / TEXT("music.json"), &Error);
		if (!J) Test.AddError(TEXT("music.json: ") + Error + TEXT(" (run node tools/web-export/render-music.mjs)"));
		return J;
	}

	TSharedPtr<FHKMusicPCM> LoadTrack(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Meta, const FString& Name)
	{
		const TSharedPtr<FJsonObject> T = HKJson::Obj(HKJson::Obj(Meta, TEXT("tracks")), Name);
		if (!T) { Test.AddError(TEXT("music.json has no track ") + Name); return nullptr; }
		FString Error;
		TSharedPtr<FHKMusicPCM> P = FHKMusicPCM::LoadWav(FHKMusicPlayer::AudioDir() / HKJson::Str(T, TEXT("file")), HKJson::Num(T, TEXT("gain"), 1), &Error);
		if (!P) Test.AddError(Error);
		return P;
	}

	float PcmL(const FHKMusicPCM& P, int64 Frame) { return P.Samples[(Frame % P.Frames) * 2] * float(P.Scale); }
	float PcmR(const FHKMusicPCM& P, int64 Frame) { return P.Samples[(Frame % P.Frames) * 2 + 1] * float(P.Scale); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKAudioSynthTest, "Haarklous.Audio.Synth", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKAudioSynthTest::RunTest(const FString& Parameters)
{
	// AudioParam automation (Web Audio spec formulas).
	{
		FHKSynthContext Ctx(Rate);
		FHKSynthParam P(Ctx, 1);
		P.SetValueAtTime(0, 0);
		P.LinearRampToValueAtTime(1, 1);
		P.ExponentialRampToValueAtTime(.01, 2);
		P.SetTargetAtTime(.5, 2.5, .1);
		auto At = [&](double T) { const int64 F = int64(T * Rate); P.Compute((F / 128) * 128); return P.Values()[F % 128]; };
		TestTrue(TEXT("linear ramp halfway"), FMath::Abs(At(.5) - .5f) < 1e-4f);
		TestTrue(TEXT("exponential ramp halfway"), FMath::Abs(At(1.5) - .1f) < 1e-4f);
		TestTrue(TEXT("held after the ramp"), FMath::Abs(At(2.4) - .01f) < 1e-5f);
		TestTrue(TEXT("setTarget after one time constant"), FMath::Abs(At(2.6) - (.5f + (.01f - .5f) * expf(-1))) < 1e-3f);
	}
	// Convolver (head + tail partitions) against direct convolution.
	{
		FHKSynthContext Ctx(Rate);
		FHKRandom Rnd(7);
		FHKSynthBuffer Ir;
		Ir.NumChannels = 2; Ir.Length = 12000; Ir.SampleRate = Rate;
		for (int32 c = 0; c < 2; c++) { Ir.Data[c].SetNum(Ir.Length); for (float& V : Ir.Data[c]) V = float(Rnd.Next() * 2 - 1); }
		TSharedPtr<FHKSynthConvolver> Conv = Ctx.CreateConvolver();
		Conv->SetBuffer(Ir, false);
		TSharedPtr<FHKSynthBuffer> In = Ctx.CreateBuffer(2, 9000);
		for (int32 c = 0; c < 2; c++) for (int32 i = 0; i < 9000; i++) In->Data[c][i] = i < 7000 ? float(Rnd.Next() * 2 - 1) : 0.f;
		TSharedPtr<FHKSynthBufferSource> Src = Ctx.CreateBufferSource();
		Src->Buffer = In; Src->Start(0);
		Src->Connect(Conv)->Connect(Ctx.Destination);
		double MaxErr = 0, MaxRef = 0;
		for (int32 b = 0; b < 160; b++)
		{
			const float* L; const float* R;
			Ctx.RenderBlock(L, R);
			for (int32 i = 0; i < 128; i += 7)
			{
				const int32 n = b * 128 + i;
				for (int32 c = 0; c < 2; c++)
				{
					double Ref = 0;
					for (int32 k = FMath::Max(0, n - 8999); k <= FMath::Min(n, Ir.Length - 1); k++) Ref += double(Ir.Data[c][k]) * In->Data[c][n - k];
					MaxErr = FMath::Max(MaxErr, FMath::Abs(Ref - (c ? R[i] : L[i])));
					MaxRef = FMath::Max(MaxRef, FMath::Abs(Ref));
				}
			}
		}
		AddInfo(FString::Printf(TEXT("convolver vs direct convolution: largest error %.2e of %.1f"), MaxErr, MaxRef));
		TestTrue(TEXT("convolver = direct convolution"), MaxErr < 2e-3 * MaxRef);
	}
	// Finished one-shot sounds leave the graph.
	{
		FHKGameAudio A(Rate, 3, false);
		A.Resume();
		const int32 Base = A.Context()->LiveNodes();
		A.Sfx(TEXT("crash"), FHKSfxOpts());
		A.Footstep(TEXT("gravel"), true, 1);
		const int32 Busy = A.Context()->LiveNodes();
		for (int32 b = 0; b < 375 * 3; b++) { const float* L; const float* R; A.RenderBlock(L, R); }
		const int32 After = A.Context()->LiveNodes();
		AddInfo(FString::Printf(TEXT("graph nodes: %d idle, %d with a crash and a footstep, %d after 3 s"), Base, Busy, After));
		TestEqual(TEXT("nodes are freed"), After, Base);
		TSharedPtr<FHKEngineVoice> E = A.Engine(TEXT("car"));
		E->Set(3000, .5);
		E->Hydraulics(.5);
		for (int32 b = 0; b < 100; b++) { const float* L; const float* R; A.RenderBlock(L, R); }
		E->Stop();
		E.Reset();
		for (int32 b = 0; b < 375 * 2; b++) { const float* L; const float* R; A.RenderBlock(L, R); }
		TestEqual(TEXT("a stopped engine is freed"), A.Context()->LiveNodes(), Base);
	}
	// Names.
	double Dist = 1;
	TestEqual(TEXT("case-insensitive"), FHKGameAudio::CanonicalSfx(TEXT("DOOROPEN")), FString(TEXT("doorOpen")));
	TestEqual(TEXT("alias reload"), FHKGameAudio::CanonicalSfx(TEXT("reload")), FString(TEXT("pistolClick")));
	TestEqual(TEXT("hostile shot with distance"), FHKGameAudio::CanonicalSfx(TEXT("hostileShot:7.5"), &Dist), FString(TEXT("hostileShot")));
	TestEqual(TEXT("distance"), Dist, 7.5);
	TestEqual(TEXT("unknown"), FHKGameAudio::CanonicalSfx(TEXT("nonsense")), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKAudioWebTest, "Haarklous.Audio.MatchesWeb", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKAudioWebTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> J = LoadRef(*this);
	if (!J) return false;
	const TArray<double> Edges = HKJson::Nums(J, TEXT("bandEdges"));
	int32 Good = 0, Total = 0;
	double WorstMean = 0, SumMean = 0;
	const double T0 = FPlatformTime::Seconds();
	double Seconds = 0;
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(J, TEXT("cases")))
	{
		const TSharedPtr<FJsonObject> C = V->AsObject();
		const FString Name = HKJson::Str(C, TEXT("name"));
		TArray<float> L, R;
		RenderCase(C, L, R);
		Seconds += L.Num() / Rate;
		const FFeatures Mine = Features(L, R, Edges), Web = FromJson(C);
		const FDiff D = Compare(Mine, Web);
		// Tolerances: same random numbers, so differences come only from the oscillator tables, filter/ramp details
		// and floating point (measured 27 Oct 2026: level <= 0.05 dB, RMS p95 <= 0.15 dB, bands <= 0.63 dB,
		// centroid within 3 %, peak <= 0.28 dB).
		const bool bOk = FMath::Abs(D.Level) <= .25 && D.RmsMean <= .25 && D.RmsP95 <= .75 && D.Bands <= 1 && FMath::Abs(D.Centroid - 1) <= .05 && FMath::Abs(D.Peak) <= 1 && FMath::Abs(D.Balance) <= .25;
		Total++;
		if (bOk) Good++;
		WorstMean = FMath::Max(WorstMean, D.RmsMean);
		SumMean += D.RmsMean;
		const FString Line = FString::Printf(TEXT("%s: %s"), *Name, *Describe(D));
		if (bOk) AddInfo(Line); else AddError(Line);
	}
	AddInfo(FString::Printf(TEXT("audio cases: %d / %d match the web version (RMS difference mean %.2f dB, worst case %.2f dB); rendered %.1f s of sound in %.2f s"),
		Good, Total, Total ? SumMean / Total : 0, WorstMean, Seconds, FPlatformTime::Seconds() - T0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKAudioCompressorTest, "Haarklous.Audio.Compressor", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKAudioCompressorTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> J = LoadRef(*this);
	if (!J) return false;
	const TSharedPtr<FJsonObject> C = HKJson::Obj(J, TEXT("compressor"));
	const TArray<double> Levels = HKJson::Nums(C, TEXT("levels"));
	const int32 N = int32(3 * Rate);
	TArray<float> L, R;
	L.SetNum(N); R.SetNum(N);
	for (int32 i = 0; i < N; i++)
	{
		const double A = Levels[int32(i / (Rate / 2))];
		L[i] = float(A * sin(2 * Pi * 220 * i / Rate));
		R[i] = float(A * .7 * sin(2 * Pi * 330 * i / Rate + 1));
	}
	FHKSynthCompressor Comp(Rate);
	Comp.Threshold = -18; Comp.Ratio = 3; Comp.Attack = .01f; Comp.Release = .25f;
	for (int32 i = 0; i + 128 <= N; i += 128) Comp.Process(L.GetData() + i, R.GetData() + i, 128);
	const FDiff D = Compare(Features(L, R, HKJson::Nums(J, TEXT("bandEdges"))), FromJson(C));
	const TArray<double> Samples = HKJson::Nums(C, TEXT("samples"));
	const int32 Step = int32(HKJson::Num(C, TEXT("sampleStep")));
	double MaxErr = 0;
	for (int32 k = 0; k < Samples.Num(); k++) MaxErr = FMath::Max(MaxErr, FMath::Abs(Samples[k] - L[k * Step]));
	AddInfo(FString::Printf(TEXT("compressor: %s; largest sample difference %.2e"), *Describe(D), MaxErr));
	TestTrue(TEXT("compressor matches the browser's DynamicsCompressorNode"), D.RmsMean < .1 && D.RmsP95 < .3 && MaxErr < 5e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKAudioMusicLoopsTest, "Haarklous.Audio.MusicLoops", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKAudioMusicLoopsTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Meta = LoadMusicMeta(*this);
	if (!Meta) return false;
	for (const FHKMusicTrackInfo& I : FHKMusicPlayer::Tracks())
	{
		const TSharedPtr<FHKMusicPCM> P = LoadTrack(*this, Meta, I.Name);
		if (!P) continue;
		const TSharedPtr<FJsonObject> T = HKJson::Obj(HKJson::Obj(Meta, TEXT("tracks")), I.Name);
		const int64 Expect = int64(I.Bars) * int64(FMath::RoundToDouble(I.BarSeconds() * P->SampleRate));
		TestEqual(I.Name + TEXT(": loop length"), int64(P->Frames), Expect);
		TestEqual(I.Name + TEXT(": seconds"), P->Frames / P->SampleRate, I.Seconds, 1e-9);
		// Seamless: the jump from the last sample to the first is no bigger than ordinary sample-to-sample steps.
		TArray<float> Steps;
		Steps.Reserve(P->Frames / 7);
		for (int32 i = 0; i + 1 < P->Frames; i += 7) Steps.Add(FMath::Abs(PcmL(*P, i + 1) - PcmL(*P, i)));
		Steps.Sort();
		const float P999 = Steps[int32(Steps.Num() * .999)];
		const float Seam = FMath::Max(FMath::Abs(PcmL(*P, 0) - PcmL(*P, P->Frames - 1)), FMath::Abs(PcmR(*P, 0) - PcmR(*P, P->Frames - 1)));
		const double JoinDiff = HKJson::Num(T, TEXT("seamMax"));
		TestTrue(I.Name + TEXT(": seamless"), Seam <= P999 && JoinDiff < 1e-4);
		// Levels after the compressor at volume 1 (second pass, so the compressor is settled): the web notes say
		// peaks <= 0.52 and 10 s segments about -21 ... -28 dBFS.
		FHKSynthCompressor Comp(P->SampleRate);
		Comp.Threshold = -18; Comp.Ratio = 3; Comp.Attack = .01f; Comp.Release = .25f;
		float L[128], R[128];
		double Peak = 0, E = 0;
		int32 En = 0;
		TArray<double> Seg;
		const int64 SegFrames = int64(10 * P->SampleRate);
		for (int64 f = 0; f < 2 * int64(P->Frames); f += 128)
		{
			for (int32 i = 0; i < 128; i++) { L[i] = PcmL(*P, f + i) * .8f; R[i] = PcmR(*P, f + i) * .8f; }
			Comp.Process(L, R, 128);
			if (f < P->Frames) continue;
			for (int32 i = 0; i < 128; i++)
			{
				Peak = FMath::Max(Peak, double(FMath::Max(FMath::Abs(L[i]), FMath::Abs(R[i]))));
				E += double(L[i]) * L[i] + double(R[i]) * R[i]; En += 2;
				if ((f - P->Frames + i + 1) % SegFrames == 0) { Seg.Add(10 * log10(E / En)); E = 0; En = 0; }
			}
		}
		double Lo = 0, Hi = -200;
		for (double S : Seg) { Lo = FMath::Min(Lo, S); Hi = FMath::Max(Hi, S); }
		AddInfo(FString::Printf(TEXT("%s \"%s\": %d frames = %.2f s, seam step %.4f (largest normal step %.4f), segment joins differ by %.1e, peak %.3f, 10 s levels %.1f ... %.1f dBFS"),
			*I.Name, *I.Title, P->Frames, P->Frames / P->SampleRate, Seam, P999, JoinDiff, Peak, Lo, Hi));
		TestTrue(I.Name + TEXT(": peak <= 0.55"), Peak <= .55);
		TestTrue(I.Name + TEXT(": levels -30 ... -19 dBFS"), Lo >= -30 && Hi <= -19);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKAudioMusicWebTest, "Haarklous.Audio.MusicMatchesWeb", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKAudioMusicWebTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> J = LoadRef(*this);
	const TSharedPtr<FJsonObject> Meta = LoadMusicMeta(*this);
	if (!J || !Meta) return false;
	TMap<FString, TSharedPtr<FHKMusicPCM>> Loaded;
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(J, TEXT("music")))
	{
		const TSharedPtr<FJsonObject> M = V->AsObject();
		const FString Name = HKJson::Str(M, TEXT("name"));
		if (!Loaded.Contains(Name)) Loaded.Add(Name, LoadTrack(*this, Meta, Name));
		const TSharedPtr<FHKMusicPCM> P = Loaded[Name];
		if (!P) continue;
		const FHKMusicTrackInfo* I = FHKMusicPlayer::Track(Name);
		const int64 BarFrames = int64(FMath::RoundToDouble(I->BarSeconds() * Rate));
		const int32 Bar = int32(HKJson::Num(M, TEXT("bar"))), Bars = int32(HKJson::Num(M, TEXT("bars"))), Pre = int32(HKJson::Num(M, TEXT("preBars")));
		const double Volume = HKJson::Num(M, TEXT("volume"));
		// Like the browser render: 0.05 s of silence, then the loop from (bar - preBars), master gain volume * 0.8,
		// the compressor; compare the region.
		const int64 Lead = int64(.05 * Rate), Total = Lead + (Pre + Bars) * BarFrames;
		const int64 From = ((int64(Bar - Pre) % I->Bars + I->Bars) % I->Bars) * BarFrames;
		FHKSynthCompressor Comp(Rate);
		Comp.Threshold = -18; Comp.Ratio = 3; Comp.Attack = .01f; Comp.Release = .25f;
		TArray<float> L, R;
		L.SetNumZeroed(int32(Total + 128)); R.SetNumZeroed(int32(Total + 128));
		for (int64 f = Lead; f < Total; f++) { L[f] = PcmL(*P, From + f - Lead) * float(Volume * .8); R[f] = PcmR(*P, From + f - Lead) * float(Volume * .8); }
		for (int64 f = 0; f + 128 <= L.Num(); f += 128) Comp.Process(L.GetData() + f, R.GetData() + f, 128);
		const int64 Start = Lead + Pre * BarFrames;
		TArray<float> RL(L.GetData() + Start, int32(Bars * BarFrames)), RR(R.GetData() + Start, int32(Bars * BarFrames));
		const FDiff D = Compare(Features(RL, RR, HKJson::Nums(J, TEXT("bandEdges"))), FromJson(M));
		const FString Line = FString::Printf(TEXT("music %s bars %d-%d at volume %.1f: %s"), *Name, Bar, Bar + Bars - 1, Volume, *Describe(D));
		const bool bOk = D.RmsMean <= .2 && D.RmsP95 <= .5 && FMath::Abs(D.Level) <= .2 && FMath::Abs(D.Peak) <= .5;
		if (bOk) AddInfo(Line); else AddError(Line);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKAudioMusicPlayerTest, "Haarklous.Audio.MusicPlayer", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKAudioMusicPlayerTest::RunTest(const FString& Parameters)
{
	// Synthetic loops: 'main' plays (bar + 1) * 100 on the left, 'discgolf' 1000 on the right; no compressor, so the
	// output shows the faders and which bar plays.
	FHKMusicPlayer M(Rate);
	M.bCompressor = false;
	auto Make = [](const FString& Name, bool bLeft)
	{
		const FHKMusicTrackInfo* I = FHKMusicPlayer::Track(Name);
		TSharedPtr<FHKMusicPCM> P = MakeShared<FHKMusicPCM>();
		const int32 BarFrames = int32(FMath::RoundToDouble(I->BarSeconds() * Rate));
		P->Frames = I->Bars * BarFrames;
		P->SampleRate = Rate;
		P->Scale = 1.0 / 32767 / .8;
		P->Samples.SetNum(P->Frames * 2);
		for (int32 f = 0; f < P->Frames; f++) { P->Samples[f * 2] = bLeft ? int16((f / BarFrames + 1) * 100) : 0; P->Samples[f * 2 + 1] = bLeft ? 0 : 1000; }
		return P;
	};
	M.SetTrackData(TEXT("main"), Make(TEXT("main"), true));
	M.SetTrackData(TEXT("discgolf"), Make(TEXT("discgolf"), false));
	const float Unit = float(.5 * .8 / 32767 / .8);   // master volume .5 * .8, sample scale
	float L[128], R[128];
	double T = 0;
	auto RunTo = [&](double Until) { while (T < Until - 1e-9) { FMemory::Memzero(L, sizeof(L)); FMemory::Memzero(R, sizeof(R)); M.RenderBlock(L, R); T += 128 / Rate; } };
	M.Play(TEXT("main"));        // before attach: only remembered (like the web before the audio context exists)
	M.Attach(.5);
	RunTo(.1);
	TestTrue(TEXT("first bar starts after 0.12 s"), L[0] == 0);
	RunTo(.6);
	TestTrue(TEXT("fading in over 1.2 s"), FMath::Abs(L[0] / (100 * Unit) - .6 / 1.2) < .02);
	RunTo(2);
	TestTrue(TEXT("bar 0 at full level"), FMath::Abs(L[0] / Unit - 100) < .5);
	RunTo(20);
	M.Play(TEXT("discgolf"));
	RunTo(21.25);
	TestTrue(TEXT("main fades out over 2.5 s"), FMath::Abs(L[0] / (700 * Unit) - .5) < .02);   // bar 6 plays at 21.25 s
	TestTrue(TEXT("disc golf fades in over 1.2 s"), FMath::Abs(R[0] / (1000 * Unit) - 1) < .01);
	RunTo(23);
	TestEqual(TEXT("main stops after the next bars it had scheduled (bar 8 next)"), M.LaneBar(TEXT("main")), 8);
	TestTrue(TEXT("main silent"), L[0] == 0);
	RunTo(30);
	M.Play(TEXT("main"));
	RunTo(33.1);
	TestTrue(TEXT("main continues at bar 8"), FMath::Abs(L[0] / Unit - 900) < 1);
	TestTrue(TEXT("disc golf faded out"), R[0] == 0);
	// Coming back while still fading out continues without a break.
	M.Play(TEXT("discgolf"));
	RunTo(34);
	M.Play(TEXT("main"));
	RunTo(37);
	TestTrue(TEXT("main back to full level"), L[0] > 0 && FMath::Abs(L[0] / Unit - (int32((37 - 30.12) / (60.0 / 72 * 4)) + 9) * 100) < 1);
	M.Play(FString());
	RunTo(40);
	TestTrue(TEXT("silence"), L[0] == 0 && R[0] == 0);
	// Volume follows setTargetAtTime(level, now, .1).
	M.SetVolume(1);
	TestEqual(TEXT("volume"), M.Volume, 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKAudioEngineTest, "Haarklous.Audio.Engine", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKAudioEngineTest::RunTest(const FString& Parameters)
{
	// The game-thread API through the command queue, rendered like the Unreal synth component does.
	TSharedRef<FHKAudioEngine, ESPMode::ThreadSafe> E = MakeShared<FHKAudioEngine, ESPMode::ThreadSafe>(Rate, 11);
	FHKAudioImpl Audio(E);
	TArray<float> Out;
	Out.SetNum(1024 * 2);
	auto Energy = [&](int32 Callbacks) { double S = 0; for (int32 c = 0; c < Callbacks; c++) { E->Render(Out.GetData(), 1024, 2); for (float V : Out) S += V * V; } return S; };
	Audio.Sfx(TEXT("doorOpen"));
	TestEqual(TEXT("silent before resume()"), Energy(10), 0.0);
	Audio.Resume();
	Audio.Ambience(FHKAmbience());
	Audio.Sfx(TEXT("doorOpen"));
	TestTrue(TEXT("door sound"), Energy(20) > 1e-3);
	{
		TSharedPtr<IHKEngineSound> Car = Audio.Engine(TEXT("car"));
		Car->Set(3000, .5);
		const double A = Energy(20);
		TestTrue(TEXT("engine runs"), A > 1e-2);
		// The excavator's hydraulics through the game-side interface (IHKEngineSound::Hydraulics).
		TSharedPtr<IHKEngineSound> Digger = Audio.Engine(TEXT("excavator"));
		Digger->Set(1000, 0);
		const double Idle = Energy(20);
		Digger->Hydraulics(1);
		TestTrue(TEXT("hydraulics add their whine"), Energy(20) > Idle);
		Digger->Stop();
	}
	Energy(200);   // the handle was dropped: the engine stops and is freed
	int32 Nodes = 0;
	E->Enqueue([&Nodes](FHKAudioEngine& En) { Nodes = En.Audio.Context()->LiveNodes(); });
	Energy(1);
	TestTrue(TEXT("dropped engine handle freed its nodes"), Nodes < 25);
	Audio.Sfx(TEXT("hostileShot:12"));
	TestTrue(TEXT("mission sound"), Energy(10) > 1e-4);
	AddInfo(FString::Printf(TEXT("command queue + render: %d graph nodes idle"), Nodes));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKAudioCpuTest, "Haarklous.Audio.Cpu", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKAudioCpuTest::RunTest(const FString& Parameters)
{
	// CPU cost on this PC: a quiet walk outdoors, and a busy moment (stairwell reverb, engine, footsteps, effects,
	// fire, two music tracks crossfading through the compressor).
	const TSharedPtr<FJsonObject> Meta = LoadMusicMeta(*this);
	TSharedPtr<FHKMusicPCM> Main = Meta ? LoadTrack(*this, Meta, TEXT("main")) : nullptr;
	TSharedPtr<FHKMusicPCM> Mission = Meta ? LoadTrack(*this, Meta, TEXT("mission")) : nullptr;
	// Wall-clock time of the render thread work; other programs on the PC can slow a run down, so the best of three
	// runs counts.
	for (int32 Scene = 0; Scene < 2; Scene++)
	{
		double Best = 1e9;
		int32 MaxNodes = 0;
		for (int32 Run = 0; Run < 3; Run++)
		{
			FHKAudioEngine E(Rate, 5);
			if (Main) E.Music.SetTrackData(TEXT("main"), Main);
			if (Mission) E.Music.SetTrackData(TEXT("mission"), Mission);
			E.Audio.Resume();
			FHKAmbience A;
			A.Green = .6; A.Traffic = 20;
			if (Scene == 1) { A.Indoor = 1; A.bStair = true; }
			E.Audio.Ambience(A);
			E.Music.Play(TEXT("main"));
			TSharedPtr<FHKEngineVoice> Car = Scene == 1 ? E.Audio.Engine(TEXT("excavator")) : nullptr;
			if (Scene == 1) E.Audio.Fire(6, 1);
			TArray<float> Out;
			Out.SetNum(1024 * 2);
			const int32 Callbacks = int32(10 * Rate / 1024);
			const double T0 = FPlatformTime::Seconds();
			for (int32 c = 0; c < Callbacks; c++)
			{
				const double T = c * 1024 / Rate;
				if (c % 21 == 0) E.Audio.Footstep(Scene ? TEXT("stone") : TEXT("gravel"), Scene == 1, 1);
				if (Scene == 1)
				{
					Car->Set(1000 + 1000 * (.5 + .5 * sin(T)), .5 + .5 * sin(T));
					Car->Hydraulics(.5 + .5 * sin(T * 1.3));
					if (c % 19 == 0) E.Audio.Sfx(c % 38 ? TEXT("doorOpen") : TEXT("chains"), FHKSfxOpts(1, .6));
					if (c % 300 == 150) E.Audio.Sfx(TEXT("crash"), FHKSfxOpts());
					if (c == Callbacks / 2) E.Music.Play(TEXT("mission"));
				}
				E.Render(Out.GetData(), 1024, 2);
				if (c % 50 == 0) MaxNodes = FMath::Max(MaxNodes, E.Audio.Context()->LiveNodes());
			}
			Best = FMath::Min(Best, (FPlatformTime::Seconds() - T0) / (Callbacks * 1024 / Rate));
		}
		AddInfo(FString::Printf(TEXT("CPU %s: %.1f ms per second of sound = %.1f %% of one core (%d graph nodes at most)"),
			Scene ? TEXT("busy (stairwell reverb, excavator, footsteps, effects, fire, music crossfade)") : TEXT("quiet walk outdoors with music"),
			Best * 1000, Best * 100, MaxNodes));
		TestTrue(TEXT("audio uses less than 25 % of one core"), Best < .25);
	}
	return true;
}

#endif
