#include "HKSynth.h"

using namespace HKSynth;

int32 FHKSynthContext::NodeCount = 0;

namespace
{
	constexpr double PiD = 3.14159265358979323846;
	inline float DbToLin(float Db) { return powf(10.f, .05f * Db); }
	inline float LinToDb(float L) { return 20.f * log10f(L); }
}

// ---------------------------------------------------------------- AudioParam

FHKSynthParam::FHKSynthParam(FHKSynthContext& InCtx, double InDefault, double InMin, double InMax)
	: Ctx(InCtx), Value(InDefault), PrevValue(InDefault), MinValue(InMin), MaxValue(InMax)
{
	for (int32 i = 0; i < Block; i++) Buf[i] = float(InDefault);
}

void FHKSynthParam::Insert(const FEvent& E)
{
	// Chromium: an event of the same type at the same time replaces the old one; otherwise it goes after the
	// events at the same time.
	int32 i = 0;
	for (; i < Events.Num(); i++)
	{
		if (Events[i].Time == E.Time && Events[i].Type == E.Type) { Events[i] = E; return; }
		if (Events[i].Time > E.Time) break;
	}
	if (Events.Num() == 0 && !bTarget && (E.Type == EType::Linear || E.Type == EType::Exponential))
	{
		// A ramp with no event before it starts now from the current value.
		PrevTime = Ctx.CurrentTime();
		PrevValue = Value;
	}
	Events.Insert(E, i);
}

void FHKSynthParam::SetValue(double V)
{
	if (Events.Num() == 0 && !bTarget) { Value = V; PrevValue = V; PrevTime = Ctx.CurrentTime(); return; }
	SetValueAtTime(V, Ctx.CurrentTime());
}
void FHKSynthParam::SetValueAtTime(double V, double T) { Insert({EType::SetValue, FMath::Max(0.0, T), V, 0}); }
void FHKSynthParam::LinearRampToValueAtTime(double V, double T) { Insert({EType::Linear, FMath::Max(0.0, T), V, 0}); }
void FHKSynthParam::ExponentialRampToValueAtTime(double V, double T) { Insert({EType::Exponential, FMath::Max(0.0, T), V, 0}); }
void FHKSynthParam::SetTargetAtTime(double V, double T, double Tau) { Insert({EType::Target, FMath::Max(0.0, T), V, FMath::Max(1e-6, Tau)}); }
void FHKSynthParam::CancelScheduledValues(double T)
{
	Events.RemoveAll([T](const FEvent& E) { return E.Time >= T; });
}

void FHKSynthParam::Compute(int64 BlockFrame)
{
	const double Sr = Ctx.SampleRate;
	if (Events.Num() == 0 && !bTarget)
	{
		const float V = float(FMath::Clamp(Value, MinValue, MaxValue));
		for (int32 i = 0; i < Block; i++) Buf[i] = V;
		bConstant = true;
	}
	else
	{
		bConstant = false;
		const double Slack = 1e-6 / Sr;   // same rounding as FHKSynthContext::FrameAt (start/stop)
		for (int32 i = 0; i < Block; i++)
		{
			const double T = double(BlockFrame + i) / Sr;
			bool bRamp = false, bFresh = false;
			while (Events.Num() > 0)
			{
				const FEvent& E = Events[0];
				if (E.Type == EType::SetValue || E.Type == EType::Target)
				{
					if (E.Time > T + Slack) break;
					if (E.Type == EType::SetValue) { Value = E.V; bTarget = false; }
					else
					{
						bTarget = true; Target = E.V; TargetTau = E.Tau;
						TargetCoef = 1.0 - exp(-1.0 / (Sr * E.Tau));
						Value = Target + (Value - Target) * exp(-(T - E.Time) / E.Tau);
						bFresh = true;
					}
					PrevTime = E.Time; PrevValue = Value; bExpRunning = false;
					Events.RemoveAt(0, 1, EAllowShrinking::No);
					continue;
				}
				// A ramp that ends at E.Time.
				if (T + Slack >= E.Time)
				{
					Value = E.V; PrevTime = E.Time; PrevValue = E.V; bTarget = false; bExpRunning = false;
					Events.RemoveAt(0, 1, EAllowShrinking::No);
					continue;
				}
				bRamp = true;
				const double T0 = PrevTime, V0 = PrevValue, T1 = E.Time, V1 = E.V;
				if (E.Type == EType::Linear) Value = T <= T0 ? V0 : V0 + (V1 - V0) * (T - T0) / (T1 - T0);
				else if (V0 == 0 || V0 * V1 <= 0) Value = V0;   // spec: no exponential ramp through zero, hold V0
				else if (!bExpRunning)
				{
					Value = V0 * pow(V1 / V0, FMath::Max(0.0, T - T0) / (T1 - T0));
					ExpRatio = pow(V1 / V0, 1.0 / ((T1 - T0) * Sr));
					bExpRunning = true;
				}
				else Value *= ExpRatio;
				break;
			}
			if (!bRamp && bTarget && !bFresh)
			{
				Value += (Target - Value) * TargetCoef;
				const double D = FMath::Abs(Value - Target);
				if (D <= 1.5e-6 * FMath::Abs(Target) || D < 1e-10) { Value = Target; if (Events.Num() == 0) bTarget = false; }
			}
			Buf[i] = float(FMath::Clamp(Value, MinValue, MaxValue));
		}
	}
	if (Inputs.Num())
	{
		// Audio-rate inputs are mixed down to mono and added to the intrinsic value.
		const int64 BI = BlockFrame / Block;
		bool bAny = false;
		for (const TSharedPtr<FHKSynthNode>& N : Inputs)
		{
			N->Render(BI);
			if (N->bSilent) continue;
			bAny = true;
			if (N->NumChannels == 1) for (int32 i = 0; i < Block; i++) Buf[i] += N->Buf[0][i];
			else for (int32 i = 0; i < Block; i++) Buf[i] += .5f * (N->Buf[0][i] + N->Buf[1][i]);
		}
		if (bAny)
		{
			bConstant = false;
			for (int32 i = 0; i < Block; i++) Buf[i] = FMath::Clamp(Buf[i], float(FMath::Max(MinValue, -3.4e38)), float(FMath::Min(MaxValue, 3.4e38)));
		}
		Inputs.RemoveAll([](const TSharedPtr<FHKSynthNode>& N) { return N->IsFinished(); });
	}
}

// ---------------------------------------------------------------- Nodes

TSharedPtr<FHKSynthNode> FHKSynthNode::Connect(const TSharedPtr<FHKSynthNode>& Dest)
{
	if (Dest) Dest->Inputs.AddUnique(AsShared());
	return Dest;
}

void FHKSynthNode::Connect(FHKSynthParam& Param)
{
	Param.AddInput(AsShared());
}

void FHKSynthNode::Render(int64 BlockIndex)
{
	if (RenderedBlock == BlockIndex) return;
	RenderedBlock = BlockIndex;
	PullInputs(BlockIndex);
	Process(BlockIndex * Block);
}

void FHKSynthNode::PullInputs(int64 BlockIndex)
{
	int32 Ch = 1;
	for (const TSharedPtr<FHKSynthNode>& I : Inputs)
	{
		I->Render(BlockIndex);
		if (!I->bSilent) Ch = FMath::Max(Ch, I->NumChannels);
	}
	InChannels = FMath::Min(2, Ch);
	bInputSilent = true;
	for (const TSharedPtr<FHKSynthNode>& I : Inputs)
	{
		if (I->bSilent) continue;
		if (bInputSilent)
		{
			for (int32 c = 0; c < InChannels; c++) FMemory::Memzero(In[c], sizeof(In[c]));
			bInputSilent = false;
		}
		for (int32 c = 0; c < InChannels; c++)
		{
			const float* S = I->Buf[I->NumChannels == 1 ? 0 : c];
			float* D = In[c];
			for (int32 i = 0; i < Block; i++) D[i] += S[i];
		}
	}
	if (bInputSilent) for (int32 c = 0; c < InChannels; c++) FMemory::Memzero(In[c], sizeof(In[c]));
	for (int32 k = Inputs.Num() - 1; k >= 0; k--) if (Inputs[k]->IsFinished()) Inputs.RemoveAtSwap(k, 1, EAllowShrinking::No);
}

bool FHKSynthNode::IsFinished()
{
	if (bPersistent) return false;
	if (IsSource()) return SourceDone();
	return Inputs.Num() == 0 && TailDone();
}

FHKSynthGain::FHKSynthGain(FHKSynthContext& InCtx) : FHKSynthNode(InCtx), Gain(InCtx, 1) {}

void FHKSynthGain::Process(int64 BlockFrame)
{
	Gain.Compute(BlockFrame);
	NumChannels = ChannelsFor(InChannels);
	if (bInputSilent)
	{
		for (int32 c = 0; c < NumChannels; c++) FMemory::Memzero(Buf[c], sizeof(Buf[c]));
		bSilent = true;
		return;
	}
	bSilent = false;
	const float* G = Gain.Values();
	for (int32 c = 0; c < NumChannels; c++)
	{
		const float* S = In[FMath::Min(c, InChannels - 1)];
		float* D = Buf[c];
		if (Gain.IsConstant()) { const float g = G[0]; for (int32 i = 0; i < Block; i++) D[i] = S[i] * g; }
		else for (int32 i = 0; i < Block; i++) D[i] = S[i] * G[i];
	}
}

void FHKSynthSource::Stop(double When)
{
	StopFrame = Ctx.FrameAt(When);
}

FHKSynthOscillator::FHKSynthOscillator(FHKSynthContext& InCtx) : FHKSynthSource(InCtx), Frequency(InCtx, 440)
{
	Frequency.SetRange(-InCtx.SampleRate / 2, InCtx.SampleRate / 2);
}

void FHKSynthOscillator::Start(double When)
{
	if (bStarted) return;
	bStarted = true;
	StartFrame = Ctx.FrameAt(When);
	StartFraction = double(StartFrame) - When * Ctx.SampleRate;
	if (StartFraction < 1e-6) StartFraction = 0;
}

void FHKSynthOscillator::Process(int64 BlockFrame)
{
	Frequency.Compute(BlockFrame);
	NumChannels = 1;
	float* D = Buf[0];
	if (!bStarted || bDone || BlockFrame + Block <= StartFrame || BlockFrame >= StopFrame)
	{
		if (bStarted && BlockFrame >= StopFrame) bDone = true;
		FMemory::Memzero(D, sizeof(Buf[0]));
		bSilent = true;
		return;
	}
	bSilent = false;
	const FHKSynthContext::FTables& T = Ctx.Tables();
	const int32 Size = T.Size, Mask = Size - 1;
	const float* F = Frequency.Values();
	const float* Hi = nullptr;
	const float* Lo = nullptr;
	auto Select = [&](float Freq)
	{
		LastFreq = Freq;
		const float AbsF = FMath::Abs(Freq);
		const float Ratio = AbsF > 0 ? float(AbsF / T.LowestFundamental) : .5f;
		const float Cents = log2f(Ratio) * 1200.f;
		float PitchRange = 1.f + Cents / 400.f;
		PitchRange = FMath::Clamp(PitchRange, 0.f, float(T.NumRanges - 1));
		Range1 = int32(PitchRange);
		Range2 = Range1 < T.NumRanges - 1 ? Range1 + 1 : Range1;
		TableFactor = PitchRange - Range1;
		Incr = float(Freq * Size / Ctx.SampleRate);
	};
	auto Tables = [&]()
	{
		if (Type == EHKOscType::Sine) { Hi = Lo = T.Sine.GetData(); return; }
		const float* W = T.Waves[int32(Type) - 1].GetData();
		Hi = W + Range1 * Size;
		Lo = W + Range2 * Size;
	};
	if (F[0] != LastFreq) Select(F[0]);
	Tables();
	// Chromium quirk (checked in the browser): when the frequency is automated, the frames of the quantum in which the
	// oscillator starts use the frequency values from the beginning of that quantum (shifted by the start offset).
	const int64 Off = StartFrame - BlockFrame;
	const bool bShift = Off > 0 && Off < Block && !Frequency.IsConstant();
	for (int32 i = 0; i < Block; i++)
	{
		const int64 Frame = BlockFrame + i;
		if (Frame < StartFrame || Frame >= StopFrame) { D[i] = 0; if (Frame >= StopFrame) bDone = true; continue; }
		const float Fi = bShift ? F[i - Off] : F[i];
		if (Fi != LastFreq) { Select(Fi); Tables(); }
		if (Frame == StartFrame && StartFraction > 0) Phase += StartFraction * Incr;
		const int32 I0 = int32(Phase);
		const float Frac = float(Phase - I0);
		const int32 I1 = (I0 + 1) & Mask;
		const float H = Hi[I0] + (Hi[I1] - Hi[I0]) * Frac;
		if (Type == EHKOscType::Sine) D[i] = H;
		else
		{
			const float L = Lo[I0] + (Lo[I1] - Lo[I0]) * Frac;
			D[i] = (1.f - TableFactor) * H + TableFactor * L;
		}
		Phase += Incr;
		if (Phase >= Size) Phase -= Size * FMath::FloorToDouble(Phase / Size);
		else if (Phase < 0) Phase += Size * FMath::CeilToDouble(-Phase / Size);
	}
}

void FHKSynthBufferSource::Start(double When, double Offset)
{
	if (bStarted) return;
	bStarted = true;
	StartFrame = Ctx.FrameAt(When);
	const double BufRate = Buffer ? Buffer->SampleRate : Ctx.SampleRate;
	// Sub-sample accurate start (Chromium): the first frame reads the buffer at the time elapsed since 'when'.
	double Fraction = double(StartFrame) - When * Ctx.SampleRate;
	if (Fraction < 1e-6) Fraction = 0;
	ReadPos = FMath::Max(0.0, Offset) * BufRate + Fraction * BufRate / Ctx.SampleRate;
}

void FHKSynthBufferSource::Process(int64 BlockFrame)
{
	NumChannels = ChannelsFor(1);
	if (!Buffer || !bStarted || bDone || BlockFrame + Block <= StartFrame || BlockFrame >= StopFrame)
	{
		if (bStarted && (BlockFrame >= StopFrame || !Buffer)) bDone = true;
		for (int32 c = 0; c < NumChannels; c++) FMemory::Memzero(Buf[c], sizeof(Buf[c]));
		bSilent = true;
		return;
	}
	bSilent = false;
	// Chromium (checked in the browser): the loop runs from loopStart to loopEnd, or to the end of the buffer when
	// loopEnd is 0; a fractional position (loopStart in seconds) is read with linear interpolation.
	const int32 Len = Buffer->Length;
	const double Sr = Buffer->SampleRate, Dur = Len / Sr;
	double LoopEndFrame = Len, Delta = Len;
	if (bLoop)
	{
		const double E = LoopEnd > 0 ? FMath::Min(LoopEnd, Dur) : Dur;
		if (LoopStart >= 0 && LoopStart < E) { LoopEndFrame = E * Sr; Delta = LoopEndFrame - LoopStart * Sr; }
	}
	double Pos = ReadPos;
	for (int32 i = 0; i < Block; i++)
	{
		const int64 Frame = BlockFrame + i;
		if (Frame < StartFrame || Frame >= StopFrame || bDone)
		{
			for (int32 c = 0; c < NumChannels; c++) Buf[c][i] = 0;
			if (Frame >= StopFrame) bDone = true;
			continue;
		}
		if (!bLoop && Pos >= Len) { bDone = true; for (int32 c = 0; c < NumChannels; c++) Buf[c][i] = 0; continue; }
		const int32 I0 = FMath::Clamp(int32(Pos), 0, Len - 1);
		const double A = Pos - I0;
		if (A == 0) for (int32 c = 0; c < NumChannels; c++) Buf[c][i] = Buffer->Data[c][I0];
		else
		{
			int32 I1 = I0 + 1;
			if (I1 >= Len) I1 = bLoop ? FMath::Clamp(int32(Pos + 1 - Delta), 0, Len - 1) : I0;
			for (int32 c = 0; c < NumChannels; c++) Buf[c][i] = float((1.0 - A) * Buffer->Data[c][I0] + A * Buffer->Data[c][I1]);
		}
		Pos += 1;
		if (bLoop && Pos >= LoopEndFrame) Pos -= Delta;
	}
	ReadPos = Pos;
	if (!bLoop && Pos >= Len) bDone = true;
}

FHKSynthBiquad::FHKSynthBiquad(FHKSynthContext& InCtx)
	: FHKSynthNode(InCtx), Frequency(InCtx, 350, 0, InCtx.SampleRate / 2), Q(InCtx, 1), Gain(InCtx, 0)
{
}

void FHKSynthBiquad::SetCoefficients(double F, double QV, double G)
{
	if (F == LastF && QV == LastQ && G == LastG && Type == LastType) return;
	LastF = F; LastQ = QV; LastG = G; LastType = Type;
	const double Nyquist = Ctx.SampleRate * .5;
	double Fn = F / Nyquist;
	double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
	switch (Type)
	{
	case EHKBiquadType::Lowpass:
		Fn = FMath::Clamp(Fn, 0.0, 1.0);
		if (Fn == 1) { b0 = 1; }
		else if (Fn > 0)
		{
			const double Res = pow(10.0, QV / 20), Theta = PiD * Fn, Alpha = sin(Theta) / (2 * Res), Cosw = cos(Theta), Beta = (1 - Cosw) / 2;
			b0 = Beta; b1 = 2 * Beta; b2 = Beta; a0 = 1 + Alpha; a1 = -2 * Cosw; a2 = 1 - Alpha;
		}
		else b0 = 0;
		break;
	case EHKBiquadType::Highpass:
		Fn = FMath::Clamp(Fn, 0.0, 1.0);
		if (Fn == 1) b0 = 0;
		else if (Fn > 0)
		{
			const double Res = pow(10.0, QV / 20), Theta = PiD * Fn, Alpha = sin(Theta) / (2 * Res), Cosw = cos(Theta), Beta = (1 + Cosw) / 2;
			b0 = Beta; b1 = -2 * Beta; b2 = Beta; a0 = 1 + Alpha; a1 = -2 * Cosw; a2 = 1 - Alpha;
		}
		else b0 = 1;
		break;
	case EHKBiquadType::Bandpass:
		Fn = FMath::Max(0.0, Fn);
		if (Fn > 0 && Fn < 1)
		{
			if (QV > 0)
			{
				const double W0 = PiD * Fn, Alpha = sin(W0) / (2 * QV), K = cos(W0);
				b0 = Alpha; b1 = 0; b2 = -Alpha; a0 = 1 + Alpha; a1 = -2 * K; a2 = 1 - Alpha;
			}
			else b0 = 1;
		}
		else b0 = 0;
		break;
	case EHKBiquadType::Peaking:
	{
		Fn = FMath::Clamp(Fn, 0.0, 1.0);
		const double A = pow(10.0, G / 40);
		if (Fn > 0 && Fn < 1)
		{
			if (QV > 0)
			{
				const double W0 = PiD * Fn, Alpha = sin(W0) / (2 * QV), K = cos(W0);
				b0 = 1 + Alpha * A; b1 = -2 * K; b2 = 1 - Alpha * A; a0 = 1 + Alpha / A; a1 = -2 * K; a2 = 1 - Alpha / A;
			}
			else b0 = A * A;
		}
		break;
	}
	case EHKBiquadType::Notch:
		Fn = FMath::Max(0.0, Fn);
		if (Fn > 0 && Fn < 1)
		{
			if (QV > 0)
			{
				const double W0 = PiD * Fn, Alpha = sin(W0) / (2 * QV), K = cos(W0);
				b0 = 1; b1 = -2 * K; b2 = 1; a0 = 1 + Alpha; a1 = -2 * K; a2 = 1 - Alpha;
			}
			else b0 = 0;
		}
		break;
	case EHKBiquadType::Allpass:
		Fn = FMath::Max(0.0, Fn);
		if (Fn > 0 && Fn < 1)
		{
			if (QV > 0)
			{
				const double W0 = PiD * Fn, Alpha = sin(W0) / (2 * QV), K = cos(W0);
				b0 = 1 - Alpha; b1 = -2 * K; b2 = 1 + Alpha; a0 = 1 + Alpha; a1 = -2 * K; a2 = 1 - Alpha;
			}
			else b0 = -1;
		}
		break;
	case EHKBiquadType::Lowshelf:
	case EHKBiquadType::Highshelf:
	{
		Fn = FMath::Clamp(Fn, 0.0, 1.0);
		const double A = pow(10.0, G / 40);
		const bool bLow = Type == EHKBiquadType::Lowshelf;
		if (Fn == 1) b0 = bLow ? A * A : 1;
		else if (Fn > 0)
		{
			const double W0 = PiD * Fn, S = 1, Alpha = .5 * sin(W0) * sqrt((A + 1 / A) * (1 / S - 1) + 2), K = cos(W0), K2 = 2 * sqrt(A) * Alpha;
			const double Ap = A + 1, Am = A - 1;
			if (bLow)
			{
				b0 = A * (Ap - Am * K + K2); b1 = 2 * A * (Am - Ap * K); b2 = A * (Ap - Am * K - K2);
				a0 = Ap + Am * K + K2; a1 = -2 * (Am + Ap * K); a2 = Ap + Am * K - K2;
			}
			else
			{
				b0 = A * (Ap + Am * K + K2); b1 = -2 * A * (Am + Ap * K); b2 = A * (Ap + Am * K - K2);
				a0 = Ap - Am * K + K2; a1 = 2 * (Am - Ap * K); a2 = Ap - Am * K - K2;
			}
		}
		else b0 = bLow ? 1 : A * A;
		break;
	}
	}
	const double Inv = 1.0 / a0;
	B0 = b0 * Inv; B1 = b1 * Inv; B2 = b2 * Inv; A1 = a1 * Inv; A2 = a2 * Inv;
}

bool FHKSynthBiquad::TailDone() const
{
	for (int32 c = 0; c < 2; c++)
		if (FMath::Abs(Y1[c]) > 1e-9 || FMath::Abs(Y2[c]) > 1e-9 || FMath::Abs(X1[c]) > 1e-9 || FMath::Abs(X2[c]) > 1e-9) return false;
	return true;
}

void FHKSynthBiquad::Process(int64 BlockFrame)
{
	Frequency.Compute(BlockFrame);
	Q.Compute(BlockFrame);
	Gain.Compute(BlockFrame);
	NumChannels = InChannels;
	const bool bAuto = !Frequency.IsConstant() || !Q.IsConstant() || !Gain.IsConstant();
	if (bInputSilent && TailDone())
	{
		for (int32 c = 0; c < NumChannels; c++) { FMemory::Memzero(Buf[c], sizeof(Buf[c])); X1[c] = X2[c] = Y1[c] = Y2[c] = 0; }
		bSilent = true;
		if (!bAuto) SetCoefficients(Frequency.Values()[0], Q.Values()[0], Gain.Values()[0]);
		return;
	}
	bSilent = false;
	const float* F = Frequency.Values();
	const float* QV = Q.Values();
	const float* G = Gain.Values();
	if (!bAuto) SetCoefficients(F[0], QV[0], G[0]);
	for (int32 c = 0; c < NumChannels; c++)
	{
		const float* S = In[c];
		float* D = Buf[c];
		double x1 = X1[c], x2 = X2[c], y1 = Y1[c], y2 = Y2[c];
		for (int32 i = 0; i < Block; i++)
		{
			if (bAuto && c == 0) SetCoefficients(F[i], QV[i], G[i]);
			const double x = S[i];
			const double y = B0 * x + B1 * x1 + B2 * x2 - A1 * y1 - A2 * y2;
			x2 = x1; x1 = x; y2 = y1; y1 = y;
			D[i] = float(y);
		}
		if (FMath::Abs(y1) < 1e-15) y1 = 0;
		if (FMath::Abs(y2) < 1e-15) y2 = 0;
		X1[c] = x1; X2[c] = x2; Y1[c] = y1; Y2[c] = y2;
		if (bAuto && c == 0 && NumChannels == 2)
		{
			// The second channel needs the same per-frame coefficients: redo it frame by frame.
			const float* S2 = In[1];
			float* D2 = Buf[1];
			double u1 = X1[1], u2 = X2[1], v1 = Y1[1], v2 = Y2[1];
			LastF = -1;
			for (int32 i = 0; i < Block; i++)
			{
				SetCoefficients(F[i], QV[i], G[i]);
				const double x = S2[i];
				const double y = B0 * x + B1 * u1 + B2 * u2 - A1 * v1 - A2 * v2;
				u2 = u1; u1 = x; v2 = v1; v1 = y;
				D2[i] = float(y);
			}
			X1[1] = u1; X2[1] = u2; Y1[1] = v1; Y2[1] = v2;
			break;
		}
	}
}

FHKSynthPanner::FHKSynthPanner(FHKSynthContext& InCtx) : FHKSynthNode(InCtx), Pan(InCtx, 0, -1, 1) {}

void FHKSynthPanner::Process(int64 BlockFrame)
{
	Pan.Compute(BlockFrame);
	NumChannels = 2;
	if (bInputSilent) { FMemory::Memzero(Buf[0], sizeof(Buf[0])); FMemory::Memzero(Buf[1], sizeof(Buf[1])); bSilent = true; return; }
	bSilent = false;
	const float* P = Pan.Values();
	const float HalfPi = float(PiD / 2);
	for (int32 i = 0; i < Block; i++)
	{
		const float p = P[Pan.IsConstant() ? 0 : i];
		if (InChannels == 1)
		{
			const float X = (p + 1) * .5f;
			Buf[0][i] = In[0][i] * cosf(X * HalfPi);
			Buf[1][i] = In[0][i] * sinf(X * HalfPi);
		}
		else if (p <= 0)
		{
			const float X = p + 1;
			const float GL = cosf(X * HalfPi), GR = sinf(X * HalfPi);
			Buf[0][i] = In[0][i] + In[1][i] * GL;
			Buf[1][i] = In[1][i] * GR;
		}
		else
		{
			const float GL = cosf(p * HalfPi), GR = sinf(p * HalfPi);
			Buf[0][i] = In[0][i] * GL;
			Buf[1][i] = In[1][i] + In[0][i] * GR;
		}
	}
}

void FHKSynthShaper::Process(int64 BlockFrame)
{
	NumChannels = InChannels;
	const int32 N = Curve.Num();
	auto Shape = [this, N](float X) -> float
	{
		if (N == 0) return X;
		const double V = (N - 1) * .5 * (double(X) + 1);
		if (V < 0) return Curve[0];
		if (V >= N - 1) return Curve[N - 1];
		const int32 K = int32(V);
		const float F = float(V - K);
		return (1 - F) * Curve[K] + F * Curve[K + 1];
	};
	if (bInputSilent)
	{
		// A silent input still maps through the curve (curve(0) need not be 0).
		const float Z = Shape(0);
		for (int32 c = 0; c < NumChannels; c++) for (int32 i = 0; i < Block; i++) Buf[c][i] = Z;
		bSilent = Z == 0;
		return;
	}
	bSilent = false;
	for (int32 c = 0; c < NumChannels; c++)
		for (int32 i = 0; i < Block; i++) Buf[c][i] = Shape(In[c][i]);
}

// ---------------------------------------------------------------- FFT and convolver

FHKSynthFFT::FHKSynthFFT(int32 InSize) : Size(InSize)
{
	CosT.SetNum(Size / 2);
	SinT.SetNum(Size / 2);
	for (int32 k = 0; k < Size / 2; k++) { CosT[k] = float(cos(2 * PiD * k / Size)); SinT[k] = float(sin(2 * PiD * k / Size)); }
	Rev.SetNum(Size);
	int32 Bits = 0;
	while ((1 << Bits) < Size) Bits++;
	for (int32 i = 0; i < Size; i++)
	{
		int32 R = 0;
		for (int32 b = 0; b < Bits; b++) if (i & (1 << b)) R |= 1 << (Bits - 1 - b);
		Rev[i] = R;
	}
}

void FHKSynthFFT::Transform(float* Re, float* Im, int32 Sign) const
{
	const int32 N = Size;
	for (int32 i = 0; i < N; i++)
	{
		const int32 j = Rev[i];
		if (j > i) { Swap(Re[i], Re[j]); Swap(Im[i], Im[j]); }
	}
	for (int32 Len = 2; Len <= N; Len <<= 1)
	{
		const int32 Half = Len >> 1, Step = N / Len;
		for (int32 i = 0; i < N; i += Len)
		{
			for (int32 j = 0; j < Half; j++)
			{
				const float Wr = CosT[j * Step], Wi = Sign * SinT[j * Step];
				const int32 a = i + j, b = a + Half;
				const float Xr = Re[b] * Wr - Im[b] * Wi, Xi = Re[b] * Wi + Im[b] * Wr;
				Re[b] = Re[a] - Xr; Im[b] = Im[a] - Xi;
				Re[a] += Xr; Im[a] += Xi;
			}
		}
	}
}

void FHKSynthConvolver::FStage::Init(int32 InB, const float* Ir[2], int32 IrStart, int32 IrLen)
{
	B = InB; N = 2 * B; Bins = B + 1;
	NumParts = FMath::Max(1, (IrLen - IrStart + B - 1) / B);
	FFT = MakeUnique<FHKSynthFFT>(N);
	TArray<float> Re, Im;
	Re.SetNum(N); Im.SetNum(N);
	for (int32 c = 0; c < 2; c++)
	{
		HRe[c].SetNumZeroed(NumParts * Bins); HIm[c].SetNumZeroed(NumParts * Bins);
		XRe[c].SetNumZeroed(NumParts * Bins); XIm[c].SetNumZeroed(NumParts * Bins);
		InPrev[c].SetNumZeroed(B);
		YRe[c].SetNumZeroed(Bins); YIm[c].SetNumZeroed(Bins);
		for (int32 p = 0; p < NumParts; p++)
		{
			for (int32 i = 0; i < N; i++) { const int32 k = IrStart + p * B + i; Re[i] = (i < B && k < IrLen) ? Ir[c][k] : 0.f; Im[i] = 0; }
			FFT->Transform(Re.GetData(), Im.GetData(), -1);
			for (int32 k = 0; k < Bins; k++) { HRe[c][p * Bins + k] = Re[k]; HIm[c][p * Bins + k] = Im[k]; }
		}
	}
	WRe.SetNumZeroed(N); WIm.SetNumZeroed(N);
	XPos = 0;
}

void FHKSynthConvolver::FStage::Run(const float* Input[2], float* Output[2])
{
	// Both channels in one complex FFT: left in the real part, right in the imaginary part (overlap-save).
	for (int32 i = 0; i < B; i++)
	{
		WRe[i] = InPrev[0][i]; WIm[i] = InPrev[1][i];
		WRe[B + i] = Input[0][i]; WIm[B + i] = Input[1][i];
	}
	FMemory::Memcpy(InPrev[0].GetData(), Input[0], B * sizeof(float));
	FMemory::Memcpy(InPrev[1].GetData(), Input[1], B * sizeof(float));
	FFT->Transform(WRe.GetData(), WIm.GetData(), -1);
	float* XLr = XRe[0].GetData() + XPos * Bins;
	float* XLi = XIm[0].GetData() + XPos * Bins;
	float* XRr = XRe[1].GetData() + XPos * Bins;
	float* XRi = XIm[1].GetData() + XPos * Bins;
	for (int32 k = 0; k < Bins; k++)
	{
		const int32 m = (N - k) & (N - 1);
		const float a = WRe[k], b = WIm[k], c = WRe[m], d = WIm[m];
		XLr[k] = .5f * (a + c); XLi[k] = .5f * (b - d);
		XRr[k] = .5f * (b + d); XRi[k] = .5f * (c - a);
	}
	for (int32 ch = 0; ch < 2; ch++)
	{
		float* Yr = YRe[ch].GetData();
		float* Yi = YIm[ch].GetData();
		FMemory::Memzero(Yr, Bins * sizeof(float));
		FMemory::Memzero(Yi, Bins * sizeof(float));
		for (int32 p = 0; p < NumParts; p++)
		{
			const int32 Slot = (XPos - p + NumParts) % NumParts;
			const float* Xr = XRe[ch].GetData() + Slot * Bins;
			const float* Xi = XIm[ch].GetData() + Slot * Bins;
			const float* Hr = HRe[ch].GetData() + p * Bins;
			const float* Hi = HIm[ch].GetData() + p * Bins;
			for (int32 k = 0; k < Bins; k++)
			{
				Yr[k] += Xr[k] * Hr[k] - Xi[k] * Hi[k];
				Yi[k] += Xr[k] * Hi[k] + Xi[k] * Hr[k];
			}
		}
	}
	const float* YLr = YRe[0].GetData();
	const float* YLi = YIm[0].GetData();
	const float* YRr = YRe[1].GetData();
	const float* YRi = YIm[1].GetData();
	for (int32 k = 0; k < Bins; k++)
	{
		WRe[k] = YLr[k] - YRi[k]; WIm[k] = YLi[k] + YRr[k];
		if (k > 0 && k < B) { WRe[N - k] = YLr[k] + YRi[k]; WIm[N - k] = -YLi[k] + YRr[k]; }
	}
	FFT->Transform(WRe.GetData(), WIm.GetData(), 1);
	const float Scale = 1.f / N;
	for (int32 i = 0; i < B; i++) { Output[0][i] = WRe[B + i] * Scale; Output[1][i] = WIm[B + i] * Scale; }
	XPos = (XPos + 1) % NumParts;
}

FHKSynthConvolver::FHKSynthConvolver(FHKSynthContext& InCtx) : FHKSynthNode(InCtx)
{
	bPersistent = true;
}

void FHKSynthConvolver::SetBuffer(const FHKSynthBuffer& Impulse, bool bNormalize)
{
	IrLength = Impulse.Length;
	TArray<float> Ir[2];
	const int32 Nc = FMath::Clamp(Impulse.NumChannels, 1, 2);
	for (int32 c = 0; c < 2; c++) Ir[c] = Impulse.Data[FMath::Min(c, Nc - 1)];
	NormalizationScale = 1;
	if (bNormalize)
	{
		// Chromium reverb.cc: normalise by RMS power, calibrate to -58 dB, scale with 44100 / sample rate.
		float Power = 0;
		for (int32 c = 0; c < Nc; c++)
		{
			float ChannelPower = 0;
			for (int32 i = 0; i < IrLength; i++) ChannelPower += Impulse.Data[c][i] * Impulse.Data[c][i];
			Power += ChannelPower;
		}
		Power = sqrtf(Power / (Nc * IrLength));
		if (!FMath::IsFinite(Power) || Power < .000125f) Power = .000125f;
		NormalizationScale = 1.f / Power * DbToLin(-58) * float(44100.0 / Impulse.SampleRate);
	}
	for (int32 c = 0; c < 2; c++) for (float& V : Ir[c]) V *= NormalizationScale;
	const float* P[2] = {Ir[0].GetData(), Ir[1].GetData()};
	const int32 HeadLen = FMath::Min(IrLength, 4096);
	Head.Init(Block, P, 0, HeadLen);
	bHasTail = IrLength > 4096;
	if (bHasTail) Tail.Init(4096, P, 4096, IrLength);
	for (int32 c = 0; c < 2; c++) { TailIn[c].SetNumZeroed(4096); TailOut[c].SetNumZeroed(4096); }
	TailPos = 0;
	SilentFrames = 1 << 30;
}

void FHKSynthConvolver::Process(int64 BlockFrame)
{
	NumChannels = 2;
	if (IrLength == 0) { FMemory::Memzero(Buf, sizeof(Buf)); bSilent = true; return; }
	float Peak = 0;
	if (!bInputSilent) for (int32 c = 0; c < InChannels; c++) for (int32 i = 0; i < Block; i++) Peak = FMath::Max(Peak, FMath::Abs(In[c][i]));
	if (Peak < 1e-9f) SilentFrames += Block; else SilentFrames = 0;
	const int64 Quiet = int64(IrLength) + 4096 + 2 * Block;
	if (SilentFrames > Quiet)
	{
		if (SilentFrames - Block <= Quiet)
		{
			// The tail has died out: clear the history so the next sound starts from silence.
			for (FStage* S : {&Head, &Tail})
				for (int32 c = 0; c < 2; c++)
				{
					FMemory::Memzero(S->XRe[c].GetData(), S->XRe[c].Num() * sizeof(float));
					FMemory::Memzero(S->XIm[c].GetData(), S->XIm[c].Num() * sizeof(float));
					FMemory::Memzero(S->InPrev[c].GetData(), S->InPrev[c].Num() * sizeof(float));
				}
			for (int32 c = 0; c < 2; c++) { FMemory::Memzero(TailIn[c].GetData(), 4096 * sizeof(float)); FMemory::Memzero(TailOut[c].GetData(), 4096 * sizeof(float)); }
			TailPos = 0;
		}
		FMemory::Memzero(Buf, sizeof(Buf));
		bSilent = true;
		return;
	}
	bSilent = false;
	ActiveBlocks++;
	if (InChannels == 1) FMemory::Memcpy(In[1], In[0], sizeof(In[0]));
	const float* Input[2] = {In[0], In[1]};
	float* Output[2] = {Buf[0], Buf[1]};
	Head.Run(Input, Output);
	if (bHasTail)
	{
		for (int32 c = 0; c < 2; c++)
		{
			FMemory::Memcpy(TailIn[c].GetData() + TailPos, In[c], Block * sizeof(float));
			const float* T = TailOut[c].GetData() + TailPos;
			for (int32 i = 0; i < Block; i++) Buf[c][i] += T[i];
		}
		TailPos += Block;
		if (TailPos >= 4096)
		{
			const float* TI[2] = {TailIn[0].GetData(), TailIn[1].GetData()};
			float* TO[2] = {TailOut[0].GetData(), TailOut[1].GetData()};
			Tail.Run(TI, TO);
			TailPos = 0;
		}
	}
}

// ---------------------------------------------------------------- Compressor (Chromium DynamicsCompressorKernel)

FHKSynthCompressor::FHKSynthCompressor(double InSampleRate) : SampleRate(InSampleRate)
{
	for (int32 c = 0; c < 2; c++) PreDelay[c].SetNumZeroed(1024);
	const uint32 Frames = uint32(.006f * float(SampleRate));
	LastPreDelayFrames = int32(FMath::Min<uint32>(Frames, 1023));
	PreDelayRead = 0;
	PreDelayWrite = LastPreDelayFrames;
}

float FHKSynthCompressor::KneeCurve(float X, float K) const
{
	if (X < LinearThreshold) return X;
	return LinearThreshold + (1 - expf(-K * (X - LinearThreshold))) / K;
}

float FHKSynthCompressor::Saturate(float X, float K) const
{
	if (X < KneeThreshold) return KneeCurve(X, K);
	const float XDb = LinToDb(X);
	const float YDb = YKneeThresholdDb + Slope * (XDb - KneeThresholdDb);
	return DbToLin(YDb);
}

float FHKSynthCompressor::SlopeAt(float X, float K) const
{
	if (X < LinearThreshold) return 1;
	const float X2 = X * 1.001f;
	const float XDb = LinToDb(X), X2Db = LinToDb(X2);
	const float YDb = LinToDb(KneeCurve(X, K)), Y2Db = LinToDb(KneeCurve(X2, K));
	return (Y2Db - YDb) / (X2Db - XDb);
}

float FHKSynthCompressor::KAtSlope(float DesiredSlope) const
{
	const float XDb = DbThreshold + DbKnee;
	const float X = DbToLin(XDb);
	float MinK = .1f, MaxK = 10000, K = 5;
	for (int32 i = 0; i < 15; i++)
	{
		const float S = SlopeAt(X, K);
		if (S < DesiredSlope) MaxK = K; else MinK = K;
		K = sqrtf(MinK * MaxK);
	}
	return K;
}

float FHKSynthCompressor::UpdateStaticCurveParameters(float InDbThreshold, float InDbKnee, float InRatio)
{
	if (InDbThreshold != DbThreshold || InDbKnee != DbKnee || InRatio != CurRatio)
	{
		DbThreshold = InDbThreshold;
		LinearThreshold = DbToLin(InDbThreshold);
		DbKnee = InDbKnee;
		CurRatio = InRatio;
		Slope = 1 / CurRatio;
		const float K = KAtSlope(1 / CurRatio);
		KneeThresholdDb = InDbThreshold + InDbKnee;
		KneeThreshold = DbToLin(KneeThresholdDb);
		YKneeThresholdDb = LinToDb(KneeCurve(KneeThreshold, K));
		KneeK = K;
	}
	return KneeK;
}

void FHKSynthCompressor::Process(float* L, float* R, int32 Frames)
{
	const float Sr = float(SampleRate);
	const float K = UpdateStaticCurveParameters(Threshold, Knee, Ratio);
	const float FullRangeGain = Saturate(1, K);
	float FullRangeMakeupGain = 1 / FullRangeGain;
	FullRangeMakeupGain = powf(FullRangeMakeupGain, .6f);
	const float MasterLinearGain = DbToLin(0) * FullRangeMakeupGain;
	const float AttackFrames = FMath::Max(.001f, Attack) * Sr;
	const float ReleaseFrames = Sr * Release;
	const float SatReleaseFrames = .0025f * Sr;
	const float Y1 = ReleaseFrames * .09f, Y2 = ReleaseFrames * .16f, Y3 = ReleaseFrames * .42f, Y4 = ReleaseFrames * .98f;
	const float KA = .9999999999999998f * Y1 + 1.8432219684323923e-16f * Y2 - 1.9373394351676423e-16f * Y3 + 8.824516011816245e-18f * Y4;
	const float KB = -1.5788320352845888f * Y1 + 2.3305837032074286f * Y2 - .9141194204840429f * Y3 + .1623677525612032f * Y4;
	const float KC = .5334142869106424f * Y1 - 1.272736789213631f * Y2 + .9258856042207512f * Y3 - .18656310191776226f * Y4;
	const float KD = .08783463138207234f * Y1 - .1694162967925622f * Y2 + .08588057951595272f * Y3 - .00429891410546283f * Y4;
	const float KE = -.042416883008123074f * Y1 + .1115693827987602f * Y2 - .09764676325265872f * Y3 + .028494263462021576f * Y4;
	const float PiOverTwo = float(PiD / 2);
	const int32 Divisions = Frames / 32;
	int32 FrameIndex = 0;
	float* Chan[2] = {L, R};
	for (int32 d = 0; d < Divisions; d++)
	{
		if (!FMath::IsFinite(DetectorAverage)) DetectorAverage = 1;
		const float DesiredGain = DetectorAverage;
		const float ScaledDesiredGain = asinf(DesiredGain) / PiOverTwo;
		float EnvelopeRate;
		const bool bReleasing = ScaledDesiredGain > CompressorGain;
		float CompressionDiffDb = LinToDb(CompressorGain / ScaledDesiredGain);
		if (bReleasing)
		{
			MaxAttackCompressionDiffDb = -1;
			if (!FMath::IsFinite(CompressionDiffDb)) CompressionDiffDb = -1;
			float X = FMath::Clamp(CompressionDiffDb, -12.f, 0.f);
			X = .25f * (X + 12);
			const float X2 = X * X, X3 = X2 * X, X4 = X2 * X2;
			const float RF = KA + KB * X + KC * X2 + KD * X3 + KE * X4;
			const float DbPerFrame = 5.f / RF;
			EnvelopeRate = DbToLin(DbPerFrame);
		}
		else
		{
			if (!FMath::IsFinite(CompressionDiffDb)) CompressionDiffDb = 1;
			if (MaxAttackCompressionDiffDb == -1 || MaxAttackCompressionDiffDb < CompressionDiffDb) MaxAttackCompressionDiffDb = CompressionDiffDb;
			const float EffAttenDiffDb = FMath::Max(.5f, MaxAttackCompressionDiffDb);
			const float X = .25f / EffAttenDiffDb;
			EnvelopeRate = 1 - powf(X, 1 / AttackFrames);
		}
		for (int32 f = 0; f < 32; f++)
		{
			float CompressorInput = 0;
			for (int32 c = 0; c < 2; c++)
			{
				const float Undelayed = Chan[c][FrameIndex];
				PreDelay[c][PreDelayWrite] = Undelayed;
				const float A = Undelayed > 0 ? Undelayed : -Undelayed;
				if (CompressorInput < A) CompressorInput = A;
			}
			const float AbsInput = CompressorInput;
			const float Shaped = Saturate(AbsInput, K);
			const float Attenuation = AbsInput <= .0001f ? 1 : Shaped / AbsInput;
			float AttenuationDb = -LinToDb(Attenuation);
			AttenuationDb = FMath::Max(2.f, AttenuationDb);
			const float DbPerFrame = AttenuationDb / SatReleaseFrames;
			const float SatReleaseRate = DbToLin(DbPerFrame) - 1;
			const bool bIsRelease = Attenuation > DetectorAverage;
			const float Rate = bIsRelease ? SatReleaseRate : 1;
			DetectorAverage += (Attenuation - DetectorAverage) * Rate;
			DetectorAverage = FMath::Min(1.f, DetectorAverage);
			if (!FMath::IsFinite(DetectorAverage)) DetectorAverage = 1;
			if (EnvelopeRate < 1) CompressorGain += (ScaledDesiredGain - CompressorGain) * EnvelopeRate;
			else { CompressorGain *= EnvelopeRate; CompressorGain = FMath::Min(1.f, CompressorGain); }
			const float PostWarp = sinf(PiOverTwo * CompressorGain);
			const float TotalGain = MasterLinearGain * PostWarp;
			for (int32 c = 0; c < 2; c++) Chan[c][FrameIndex] = PreDelay[c][PreDelayRead] * TotalGain;
			FrameIndex++;
			PreDelayRead = (PreDelayRead + 1) & 1023;
			PreDelayWrite = (PreDelayWrite + 1) & 1023;
		}
		if (FMath::Abs(DetectorAverage) < 1e-30f) DetectorAverage = 0;
		if (FMath::Abs(CompressorGain) < 1e-30f) CompressorGain = 0;
	}
}

// ---------------------------------------------------------------- Context

FHKSynthContext::FHKSynthContext(double InSampleRate) : SampleRate(InSampleRate)
{
	TablesPtr = BuildTables(SampleRate);
	Destination = CreateGain();
	Destination->ForceChannels = 2;
	Destination->bPersistent = true;
}

FHKSynthContext::~FHKSynthContext()
{
	Timers.Empty();
	Destination.Reset();
}

int64 FHKSynthContext::FrameAt(double Time) const
{
	// Round up to the next frame (with a little slack for times computed in floating point).
	return int64(FMath::CeilToDouble(Time * SampleRate - 1e-6));
}

TSharedPtr<FHKSynthGain> FHKSynthContext::CreateGain() { return MakeShared<FHKSynthGain>(*this); }
TSharedPtr<FHKSynthOscillator> FHKSynthContext::CreateOscillator() { return MakeShared<FHKSynthOscillator>(*this); }
TSharedPtr<FHKSynthBufferSource> FHKSynthContext::CreateBufferSource() { return MakeShared<FHKSynthBufferSource>(*this); }
TSharedPtr<FHKSynthBiquad> FHKSynthContext::CreateBiquadFilter() { return MakeShared<FHKSynthBiquad>(*this); }
TSharedPtr<FHKSynthPanner> FHKSynthContext::CreateStereoPanner() { return MakeShared<FHKSynthPanner>(*this); }
TSharedPtr<FHKSynthShaper> FHKSynthContext::CreateWaveShaper() { return MakeShared<FHKSynthShaper>(*this); }
TSharedPtr<FHKSynthConvolver> FHKSynthContext::CreateConvolver() { return MakeShared<FHKSynthConvolver>(*this); }

TSharedPtr<FHKSynthBuffer> FHKSynthContext::CreateBuffer(int32 Channels, int32 Length)
{
	TSharedPtr<FHKSynthBuffer> B = MakeShared<FHKSynthBuffer>();
	B->NumChannels = FMath::Clamp(Channels, 1, 2);
	B->Length = FMath::Max(0, Length);
	B->SampleRate = SampleRate;
	for (int32 c = 0; c < B->NumChannels; c++) B->Data[c].SetNumZeroed(B->Length);
	return B;
}

void FHKSynthContext::SetTimeout(TFunction<void()> Fn, double DelaySeconds)
{
	Timers.Add({CurrentTime() + FMath::Max(0.0, DelaySeconds), 0, MoveTemp(Fn)});
}

void FHKSynthContext::SetInterval(TFunction<void()> Fn, double PeriodSeconds)
{
	Timers.Add({CurrentTime() + PeriodSeconds, PeriodSeconds, MoveTemp(Fn)});
}

void FHKSynthContext::RenderBlock(const float*& OutL, const float*& OutR)
{
	const double Now = CurrentTime();
	// Timers due now (in order of their due time; callbacks may add new timers).
	for (;;)
	{
		int32 Best = INDEX_NONE;
		for (int32 i = 0; i < Timers.Num(); i++)
			if (Timers[i].Due <= Now + 1e-9 && (Best == INDEX_NONE || Timers[i].Due < Timers[Best].Due)) Best = i;
		if (Best == INDEX_NONE) break;
		if (Timers[Best].Period > 0)
		{
			TFunction<void()> Fn = Timers[Best].Fn;
			Timers[Best].Due += Timers[Best].Period;
			Fn();
		}
		else
		{
			TFunction<void()> Fn = MoveTemp(Timers[Best].Fn);
			Timers.RemoveAtSwap(Best);
			Fn();
		}
	}
	Destination->Render(BlockIndex);
	OutL = Destination->Buf[0];
	OutR = Destination->Buf[1];
	BlockIndex++;
}

int32 FHKSynthContext::LiveNodes() const
{
	TSet<const FHKSynthNode*> Seen;
	TArray<const FHKSynthNode*> Stack = {Destination.Get()};
	while (Stack.Num())
	{
		const FHKSynthNode* N = Stack.Pop();
		if (!N || Seen.Contains(N)) continue;
		Seen.Add(N);
		for (const TSharedPtr<FHKSynthNode>& I : N->Inputs) Stack.Add(I.Get());
	}
	return Seen.Num();
}

TSharedPtr<const FHKSynthContext::FTables> FHKSynthContext::BuildTables(double SampleRate)
{
	static FCriticalSection Lock;
	static TMap<int32, TSharedPtr<const FTables>> Cache;
	FScopeLock Guard(&Lock);
	const int32 Key = int32(SampleRate);
	if (const TSharedPtr<const FTables>* Found = Cache.Find(Key)) return *Found;
	TSharedPtr<FTables> T = MakeShared<FTables>();
	// Chromium PeriodicWave: table size by sample rate, 3 ranges per octave, partials culled per range.
	T->Size = SampleRate <= 24000 ? 2048 : SampleRate <= 88200 ? 4096 : 16384;
	T->NumRanges = int32(lrintf(3.f * log2f(float(T->Size))));
	const int32 Size = T->Size, Half = Size / 2;
	T->LowestFundamental = (SampleRate / 2) / Half;
	T->Sine.SetNum(Size);
	for (int32 k = 0; k < Size; k++) T->Sine[k] = float(sin(2 * PiD * k / Size));
	FHKSynthFFT FFT(Size);
	TArray<float> Re, Im, Coef;
	Re.SetNum(Size); Im.SetNum(Size); Coef.SetNumZeroed(Half);
	for (int32 w = 0; w < 3; w++)
	{
		for (int32 n = 1; n < Half; n++)
		{
			const float PiFactor = 2.f / (n * float(PiD));
			float B = 0;
			if (w == 0) B = PiFactor * ((n & 1) ? 2.f : 0.f);                                   // square
			else if (w == 1) B = PiFactor * ((n & 1) ? 1.f : -1.f);                             // sawtooth
			else B = (n & 1) ? 2 * (PiFactor * PiFactor) * ((((n - 1) >> 1) & 1) ? -1.f : 1.f) : 0.f;   // triangle
			Coef[n] = B;
		}
		TArray<float>& Out = T->Waves[w];
		Out.SetNum(T->NumRanges * Size);
		float Norm = 1;
		for (int32 r = 0; r < T->NumRanges; r++)
		{
			const float CentsToCull = r * 400.f;
			const float CullingScale = powf(2.f, -CentsToCull / 1200.f);
			const uint32 Partials = uint32(CullingScale * Half);
			for (int32 i = 0; i < Size; i++) { Re[i] = 0; Im[i] = 0; }
			const int32 Keep = FMath::Min<int32>(Half, int32(Partials) + 1);
			for (int32 n = 1; n < Keep; n++) { Im[n] = -.5f * Coef[n]; Im[Size - n] = .5f * Coef[n]; }
			FFT.Transform(Re.GetData(), Im.GetData(), 1);
			if (r == 0)
			{
				float Max = 0;
				for (int32 i = 0; i < Size; i++) Max = FMath::Max(Max, FMath::Abs(Re[i]));
				if (Max > 0) Norm = 1.f / Max;
			}
			for (int32 i = 0; i < Size; i++) Out[r * Size + i] = Re[i] * Norm;
		}
	}
	Cache.Add(Key, T);
	return T;
}
