#pragma once

#include "CoreMinimal.h"

/*
 * A small Web Audio engine in C++ so the web game's sound code (game-audio.js, case-game.js) ports line by line.
 * It implements the nodes those files use, with the Web Audio spec / Chromium formulas:
 *   GainNode, OscillatorNode (band-limited wavetables built like Chromium's PeriodicWave), AudioBufferSourceNode,
 *   BiquadFilterNode (lowpass, highpass, bandpass, peaking, ... ; Q in dB for low/highpass), StereoPannerNode,
 *   WaveShaperNode, ConvolverNode (normalised impulse response, zero-latency partitioned FFT convolution) and
 *   the DynamicsCompressor kernel (used by the music).
 *   AudioParam automation: value, setValueAtTime, linearRampToValueAtTime, exponentialRampToValueAtTime,
 *   setTargetAtTime, cancelScheduledValues, plus audio-rate inputs (an oscillator modulating a gain).
 * Rendering is pull-based in 128-frame render quanta like the browser. Everything here runs on one thread (the audio
 * render thread, or a test); nodes whose sources have stopped are dropped automatically.
 * Math.random is FHKRandom (mulberry32): seeded per session at run time, seeded fixed in tests so the C++ renders can
 * be compared with the browser's own renders.
 */
namespace HKSynth
{
	constexpr int32 Block = 128;
}

class FHKSynthContext;
class FHKSynthNode;

// mulberry32, the same generator the web code uses for its seeded randomness (here also standing in for Math.random).
struct HAARKLOUS_API FHKRandom
{
	uint32 State = 1;
	explicit FHKRandom(uint32 Seed = 1) : State(Seed) {}
	double Next()
	{
		State += 0x6D2B79F5u;
		uint32 T = (State ^ (State >> 15)) * (1u | State);
		T = (T + ((T ^ (T >> 7)) * (61u | T))) ^ T;
		return double(T ^ (T >> 14)) / 4294967296.0;
	}
};

// AudioParam with the Web Audio automation timeline. Values are computed per frame (a-rate).
class HAARKLOUS_API FHKSynthParam
{
public:
	FHKSynthParam(FHKSynthContext& InCtx, double InDefault, double InMin = -3.4e38, double InMax = 3.4e38);
	// param.value = V (with automation events present this is setValueAtTime(V, currentTime), as in the spec).
	void SetValue(double V);
	double GetValue() const { return Value; }
	void SetValueAtTime(double V, double T);
	void LinearRampToValueAtTime(double V, double T);
	void ExponentialRampToValueAtTime(double V, double T);
	void SetTargetAtTime(double Target, double T, double TimeConstant);
	void CancelScheduledValues(double T);
	void SetRange(double InMin, double InMax) { MinValue = InMin; MaxValue = InMax; }

	// Rendering (called by the owning node once per block).
	void Compute(int64 BlockFrame);
	const float* Values() const { return Buf; }
	bool IsConstant() const { return bConstant; }
	bool HasInputs() const { return Inputs.Num() > 0; }
	void AddInput(const TSharedPtr<FHKSynthNode>& Node) { Inputs.Add(Node); }

private:
	enum class EType : uint8 { SetValue, Linear, Exponential, Target };
	struct FEvent { EType Type; double Time; double V; double Tau; };
	void Insert(const FEvent& E);
	FHKSynthContext& Ctx;
	TArray<FEvent> Events;     // pending events, sorted by time
	double Value;              // the current (intrinsic) value
	double PrevTime = 0, PrevValue = 0;   // end of the last completed event (start of a ramp)
	bool bTarget = false;      // a setTarget curve is running
	double Target = 0, TargetTau = 1, TargetCoef = 0;
	bool bExpRunning = false;  // exponential ramp state (incremental)
	double ExpRatio = 1;
	double MinValue, MaxValue;
	bool bConstant = true;
	float Buf[HKSynth::Block];
	TArray<TSharedPtr<FHKSynthNode>> Inputs;
};

// A node in the graph. connect() keeps the source alive: the destination holds its inputs.
class HAARKLOUS_API FHKSynthNode : public TSharedFromThis<FHKSynthNode>
{
public:
	explicit FHKSynthNode(FHKSynthContext& InCtx) : Ctx(InCtx) {}
	virtual ~FHKSynthNode() {}
	// node.connect(dest) -> dest (so calls chain like in JavaScript)
	TSharedPtr<FHKSynthNode> Connect(const TSharedPtr<FHKSynthNode>& Dest);
	void Connect(FHKSynthParam& Param);

	// Rendering
	void Render(int64 BlockIndex);
	bool IsFinished();
	int32 NumChannels = 1;                         // output channels of the last block
	float Buf[2][HKSynth::Block];                  // output of the last block
	bool bPersistent = false;                      // buses, destination: never dropped

protected:
	friend class FHKSynthParam;
	friend class FHKSynthContext;
	virtual void Process(int64 BlockFrame) = 0;     // In[]/InChannels -> Buf/NumChannels
	virtual bool IsSource() const { return false; }
	virtual bool SourceDone() const { return false; }
	virtual bool TailDone() const { return true; }
	virtual int32 ChannelsFor(int32 InputChannels) const { return InputChannels; }
	void PullInputs(int64 BlockIndex);
	FHKSynthContext& Ctx;
	TArray<TSharedPtr<FHKSynthNode>> Inputs;
	float In[2][HKSynth::Block];
	int32 InChannels = 1;
	bool bInputSilent = true;
	bool bSilent = true;                           // the last block's output was all zero
	int64 RenderedBlock = -1;
	int64 FinishedChecked = -1;
	bool bFinishedCache = false;
};

class HAARKLOUS_API FHKSynthGain : public FHKSynthNode
{
public:
	explicit FHKSynthGain(FHKSynthContext& InCtx);
	FHKSynthParam Gain;
	int32 ForceChannels = 0;    // 2 for the destination
protected:
	virtual void Process(int64 BlockFrame) override;
	virtual int32 ChannelsFor(int32 InputChannels) const override { return ForceChannels ? ForceChannels : InputChannels; }
};

enum class EHKOscType : uint8 { Sine, Square, Sawtooth, Triangle };

// Scheduled source (start/stop) shared by oscillators and buffer sources.
class HAARKLOUS_API FHKSynthSource : public FHKSynthNode
{
public:
	explicit FHKSynthSource(FHKSynthContext& InCtx) : FHKSynthNode(InCtx) {}
	void Stop(double When);
protected:
	virtual bool IsSource() const override { return true; }
	virtual bool SourceDone() const override { return bDone; }
	int64 StartFrame = TNumericLimits<int64>::Max();
	int64 StopFrame = TNumericLimits<int64>::Max();
	bool bStarted = false, bDone = false;
};

class HAARKLOUS_API FHKSynthOscillator : public FHKSynthSource
{
public:
	explicit FHKSynthOscillator(FHKSynthContext& InCtx);
	EHKOscType Type = EHKOscType::Sine;
	FHKSynthParam Frequency;
	void Start(double When = 0);
protected:
	virtual void Process(int64 BlockFrame) override;
	double Phase = 0;           // read index in the table
	double StartFraction = 0;   // sub-sample start (Chromium starts the phase at fraction * increment)
	float LastFreq = -1.f;
	int32 Range1 = 0, Range2 = 0;
	float TableFactor = 0, Incr = 0;
};

// AudioBuffer: Channels x Length float samples.
struct HAARKLOUS_API FHKSynthBuffer
{
	int32 NumChannels = 1, Length = 0;
	double SampleRate = 48000;
	TArray<float> Data[2];
	float* Channel(int32 C) { return Data[C].GetData(); }
	const float* Channel(int32 C) const { return Data[C].GetData(); }
};

class HAARKLOUS_API FHKSynthBufferSource : public FHKSynthSource
{
public:
	explicit FHKSynthBufferSource(FHKSynthContext& InCtx) : FHKSynthSource(InCtx) {}
	TSharedPtr<FHKSynthBuffer> Buffer;
	bool bLoop = false;
	double LoopStart = 0, LoopEnd = 0;   // seconds; the loop is the whole buffer unless 0 <= start < end (spec)
	void Start(double When = 0, double Offset = 0);
protected:
	virtual void Process(int64 BlockFrame) override;
	virtual int32 ChannelsFor(int32) const override { return Buffer ? Buffer->NumChannels : 1; }
	double ReadPos = 0;
};

enum class EHKBiquadType : uint8 { Lowpass, Highpass, Bandpass, Lowshelf, Highshelf, Peaking, Notch, Allpass };

class HAARKLOUS_API FHKSynthBiquad : public FHKSynthNode
{
public:
	explicit FHKSynthBiquad(FHKSynthContext& InCtx);
	EHKBiquadType Type = EHKBiquadType::Lowpass;
	FHKSynthParam Frequency, Q, Gain;
protected:
	virtual void Process(int64 BlockFrame) override;
	virtual bool TailDone() const override;
	void SetCoefficients(double F, double QV, double G);
	double B0 = 1, B1 = 0, B2 = 0, A1 = 0, A2 = 0;
	double X1[2] = {0, 0}, X2[2] = {0, 0}, Y1[2] = {0, 0}, Y2[2] = {0, 0};
	double LastF = -1, LastQ = -1e9, LastG = -1e9;
	EHKBiquadType LastType = EHKBiquadType::Allpass;
};

class HAARKLOUS_API FHKSynthPanner : public FHKSynthNode
{
public:
	explicit FHKSynthPanner(FHKSynthContext& InCtx);
	FHKSynthParam Pan;
protected:
	virtual void Process(int64 BlockFrame) override;
	virtual int32 ChannelsFor(int32) const override { return 2; }
};

class HAARKLOUS_API FHKSynthShaper : public FHKSynthNode
{
public:
	explicit FHKSynthShaper(FHKSynthContext& InCtx) : FHKSynthNode(InCtx) {}
	TArray<float> Curve;
protected:
	virtual void Process(int64 BlockFrame) override;
};

// Complex radix-2 FFT (split real/imaginary arrays).
class HAARKLOUS_API FHKSynthFFT
{
public:
	explicit FHKSynthFFT(int32 InSize);
	int32 Size;
	// In place. Sign -1: forward (e^-i), +1: inverse without 1/N scaling.
	void Transform(float* Re, float* Im, int32 Sign) const;
private:
	TArray<float> CosT, SinT;
	TArray<int32> Rev;
};

// ConvolverNode with a stereo impulse response (normalize = true, as in the browser). Zero latency: the first 4096
// taps are convolved in 128-frame partitions, the rest in 4096-frame partitions computed once every 4096 frames.
class HAARKLOUS_API FHKSynthConvolver : public FHKSynthNode
{
public:
	explicit FHKSynthConvolver(FHKSynthContext& InCtx);
	void SetBuffer(const FHKSynthBuffer& Impulse, bool bNormalize = true);
	float NormalizationScale = 1;
	int64 ActiveBlocks = 0;      // blocks actually convolved (statistics)
protected:
	virtual void Process(int64 BlockFrame) override;
	virtual int32 ChannelsFor(int32) const override { return 2; }
	struct FStage
	{
		int32 B = 0, N = 0, NumParts = 0, Bins = 0;
		TArray<float> HRe[2], HIm[2];     // per channel: NumParts x Bins
		TArray<float> XRe[2], XIm[2];     // frequency-domain delay line, NumParts x Bins (ring)
		int32 XPos = 0;
		TArray<float> InPrev[2];          // previous input block (overlap-save)
		TUniquePtr<FHKSynthFFT> FFT;
		TArray<float> WRe, WIm, YRe[2], YIm[2];
		void Init(int32 InB, const float* Ir[2], int32 IrStart, int32 IrLen);
		void Run(const float* Input[2], float* Output[2]);
	};
	FStage Head, Tail;
	bool bHasTail = false;
	TArray<float> TailIn[2], TailOut[2];   // 4096-frame input collector, tail output being played
	int32 TailPos = 0;
	int64 SilentFrames = 1 << 30;
	int32 IrLength = 0;
};

// Chromium's DynamicsCompressorKernel (the browser's DynamicsCompressorNode), stereo.
class HAARKLOUS_API FHKSynthCompressor
{
public:
	explicit FHKSynthCompressor(double InSampleRate);
	float Threshold = -24, Knee = 30, Ratio = 12, Attack = .003f, Release = .25f;
	// In place; frames processed in 32-frame divisions (call with multiples of 32).
	void Process(float* L, float* R, int32 Frames);
private:
	float KneeCurve(float X, float K) const;
	float Saturate(float X, float K) const;
	float SlopeAt(float X, float K) const;
	float KAtSlope(float DesiredSlope) const;
	float UpdateStaticCurveParameters(float DbThreshold, float DbKnee, float InRatio);
	double SampleRate;
	float DbThreshold = -1e9f, DbKnee = -1e9f, CurRatio = -1e9f;
	float LinearThreshold = 0, Slope = 1, KneeThresholdDb = 0, KneeThreshold = 0, YKneeThresholdDb = 0, KneeK = 1;
	float DetectorAverage = 0, CompressorGain = 1, MaxAttackCompressionDiffDb = -1;
	TArray<float> PreDelay[2];
	int32 PreDelayRead = 0, PreDelayWrite = 256, LastPreDelayFrames = 256;
};

// AudioContext stand-in: time, node factories, destination, timers (setTimeout/setInterval in audio time).
class HAARKLOUS_API FHKSynthContext
{
public:
	explicit FHKSynthContext(double InSampleRate);
	~FHKSynthContext();
	double SampleRate;
	int64 BlockIndex = 0;                  // next block to render
	double CurrentTime() const { return double(BlockIndex) * HKSynth::Block / SampleRate; }
	int64 CurrentFrame() const { return BlockIndex * HKSynth::Block; }
	TSharedPtr<FHKSynthGain> Destination;

	TSharedPtr<FHKSynthGain> CreateGain();
	TSharedPtr<FHKSynthOscillator> CreateOscillator();
	TSharedPtr<FHKSynthBufferSource> CreateBufferSource();
	TSharedPtr<FHKSynthBiquad> CreateBiquadFilter();
	TSharedPtr<FHKSynthPanner> CreateStereoPanner();
	TSharedPtr<FHKSynthShaper> CreateWaveShaper();
	TSharedPtr<FHKSynthConvolver> CreateConvolver();
	TSharedPtr<FHKSynthBuffer> CreateBuffer(int32 Channels, int32 Length);

	// Timers fire at the start of the first block whose time is >= the due time.
	void SetTimeout(TFunction<void()> Fn, double DelaySeconds);
	void SetInterval(TFunction<void()> Fn, double PeriodSeconds);

	// Renders one block into Buf of the destination; returns pointers to its two channels.
	void RenderBlock(const float*& OutL, const float*& OutR);
	// Frames are converted to the nearest frame at or after a time (Chromium rounds start/stop up).
	int64 FrameAt(double Time) const;

	// Wavetables (shared per sample rate).
	struct FTables
	{
		int32 Size = 4096, NumRanges = 36;
		double LowestFundamental = 0;
		TArray<float> Sine;
		TArray<float> Waves[3];    // square, sawtooth, triangle: NumRanges x Size
	};
	const FTables& Tables() const { return *TablesPtr; }

	// Statistics.
	int32 LiveNodes() const;
	static int32 NodeCount;

private:
	struct FTimer { double Due; double Period; TFunction<void()> Fn; };
	TArray<FTimer> Timers;
	TSharedPtr<const FTables> TablesPtr;
	static TSharedPtr<const FTables> BuildTables(double SampleRate);
};
