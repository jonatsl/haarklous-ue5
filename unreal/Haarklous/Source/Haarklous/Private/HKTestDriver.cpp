#include "HKTestDriver.h"
#include "HKGame.h"
#include "HKInput.h"
#include "HKMath.h"
#include "Haarklous.h"
#include "Engine/Engine.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

TMap<FString, TFunction<void(AHKTestDriver&)>>& FHKTestScripts::All()
{
	static TMap<FString, TFunction<void(AHKTestDriver&)>> Map;
	return Map;
}

AHKTestDriver::AHKTestDriver()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
}

AHKGame* AHKTestDriver::Game() const { return AHKGame::Get(this); }

FString AHKTestDriver::P() const
{
	const AHKGame* G = Game();
	if (!G) return TEXT("(no game)");
	return FString::Printf(TEXT("(%.2f, %.2f, feet %.2f) %s"), G->Player.X, G->Player.Z, G->Player.Feet, *G->Player.Mode);
}

void AHKTestDriver::BeginPlay()
{
	Super::BeginPlay();
	FParse::Value(FCommandLine::Get(), TEXT("hktest="), TestName);
	if (!FParse::Value(FCommandLine::Get(), TEXT("hkout="), OutDir)) OutDir = FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT(".."), TEXT("tools"), TEXT("out"), TEXT("shots"), TestName);
	OutDir = FPaths::ConvertRelativePathToFull(OutDir);
	TFunction<void(AHKTestDriver&)>* Script = FHKTestScripts::All().Find(TestName);
	UE_LOG(LogHaarklous, Display, TEXT("HK-TEST start %s -> %s"), *TestName, *OutDir);
	if (TSharedPtr<FHKInputProcessor> In = FHKInputProcessor::Get()) { In->bUseSimulatedPad = true; In->SimulatedPad = FHKPadState(); }
	if (!Script) { UE_LOG(LogHaarklous, Error, TEXT("HK-TEST unknown script %s"), *TestName); Quit(); return; }
#if WITH_EDITOR
	// An uncooked run compiles missing shaders in the background and draws the grey default material meanwhile: wait
	// for them (at most 15 minutes) so screenshots and frame rates show the real materials.
	TSharedPtr<double> Waited = MakeShared<double>(0);
	Steps.Add([Waited](double Dt)
	{
		*Waited += Dt;
		const int32 Left = GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
		if ((Left == 0 && *Waited > 1) || *Waited > 900)
		{
			UE_LOG(LogHaarklous, Display, TEXT("HK-TEST shaders ready after %.0f s (%d jobs left)"), *Waited, Left);
			return true;
		}
		return false;
	});
#endif
	(*Script)(*this);
	Quit();
}

void AHKTestDriver::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Steps.IsValidIndex(Index)) return;
	StepT += DeltaSeconds;
	if (Steps[Index](DeltaSeconds)) { Index++; StepT = 0; }
}

void AHKTestDriver::Wait(double Seconds)
{
	TSharedPtr<double> T = MakeShared<double>(0);
	Steps.Add([T, Seconds](double Dt) { *T += Dt; return *T >= Seconds; });
}

void AHKTestDriver::Do(TFunction<void()> Fn)
{
	Steps.Add([Fn](double) { Fn(); return true; });
}

static FHKPadState& Pad() { static FHKPadState Dummy; TSharedPtr<FHKInputProcessor> In = FHKInputProcessor::Get(); return In ? In->SimulatedPad : Dummy; }

void AHKTestDriver::Hold(int32 Button, double Value)
{
	Do([Button, Value]() { Pad().Buttons[Button] = Value; Pad().Pressed[Button] = Value > .5; });
}

void AHKTestDriver::Press(int32 Button, double Seconds)
{
	Hold(Button, 1);
	Wait(Seconds);
	Hold(Button, 0);
	Wait(.16);
}

void AHKTestDriver::Stick(double Lx, double Ly, double Seconds, double Rx, double Ry)
{
	Do([=]() { Pad().Axes[0] = Lx; Pad().Axes[1] = Ly; Pad().Axes[2] = Rx; Pad().Axes[3] = Ry; });
	Wait(Seconds);
	Do([]() { for (float& A : Pad().Axes) A = 0; });
	Wait(.09);
}

void AHKTestDriver::Key(const FString& Code, double Seconds)
{
	Do([Code]() { if (TSharedPtr<FHKInputProcessor> In = FHKInputProcessor::Get()) In->SimulateKey(Code, true); });
	Wait(Seconds);
	Do([Code]() { if (TSharedPtr<FHKInputProcessor> In = FHKInputProcessor::Get()) In->SimulateKey(Code, false); });
	Wait(.12);
}

void AHKTestDriver::WalkTo(double X, double Z, double MaxSeconds)
{
	// harness walkTo(): face the target and push the left stick forward until within .35 m.
	TSharedPtr<double> T = MakeShared<double>(0);
	Steps.Add([this, T, X, Z, MaxSeconds](double Dt)
	{
		AHKGame* G = Game();
		*T += Dt;
		const double Dx = X - G->Player.X, Dz = Z - G->Player.Z, D = HK::Hypot(Dx, Dz);
		if (D < .35 || *T > MaxSeconds) { for (float& A : Pad().Axes) A = 0; UE_LOG(LogHaarklous, Display, TEXT("HK-TEST walkTo(%.2f, %.2f) -> %s after %.1f s"), X, Z, *P(), *T); return true; }
		G->Player.Yaw = FMath::Atan2(-Dx, -Dz);
		Pad().Axes[0] = 0; Pad().Axes[1] = -1;
		return false;
	});
	Wait(.1);
}

void AHKTestDriver::Shot(const FString& Name)
{
	// Two frames of settling, then the screenshot (with the HUD), then one frame for it to be written.
	Wait(.25);
	Do([this, Name]()
	{
		const FString File = FPaths::Combine(OutDir, Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(File, true, false);
		UE_LOG(LogHaarklous, Display, TEXT("HK-TEST shot %s %s"), *File, *P());
	});
	Wait(.3);
}

void AHKTestDriver::Pose(double X, double Y, double Z, double Yaw, double Pitch)
{
	Steps.Add([this, X, Y, Z, Yaw, Pitch](double)
	{
		AHKGame* G = Game();
		G->bCameraOverride = true;
		G->OverridePos = FVector(X, Y, Z);
		G->OverrideQuat = HK::EulerToQuat(Pitch, Yaw, 0, TEXT("YXZ"));
		G->UpdateCamera();
		return true;
	});
}

void AHKTestDriver::FreeCamera()
{
	Do([this]() { Game()->bCameraOverride = false; });
}

void AHKTestDriver::MeasureFps(const FString& Label, double Seconds)
{
	struct FAcc { double T = 0; int32 N = 0; TArray<double> Frames; };
	TSharedPtr<FAcc> A = MakeShared<FAcc>();
	Wait(1.0);
	Steps.Add([A, Label, Seconds](double Dt)
	{
		A->T += Dt; A->N++; A->Frames.Add(Dt);
		if (A->T < Seconds) return false;
		A->Frames.Sort();
		const double Avg = A->N / A->T, Low = 1 / A->Frames[FMath::Min(A->Frames.Num() - 1, (int32)(A->Frames.Num() * .99))];
		UE_LOG(LogHaarklous, Display, TEXT("HK-TEST fps %s: average %.1f, 1%% low %.1f over %.1f s (%d frames)"), *Label, Avg, Low, A->T, A->N);
		return true;
	});
}

void AHKTestDriver::Log(TFunction<FString()> Fn)
{
	Do([Fn]() { UE_LOG(LogHaarklous, Display, TEXT("HK-TEST %s"), *Fn()); });
}

void AHKTestDriver::Expect(const FString& What, TFunction<bool()> Fn)
{
	Do([this, What, Fn]()
	{
		const bool bOk = Fn();
		if (!bOk) Failures++;
		UE_LOG(LogHaarklous, Display, TEXT("HK-TEST %s %s  %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What, *P());
	});
}

void AHKTestDriver::Quit()
{
	Steps.Add([this](double)
	{
		UE_LOG(LogHaarklous, Display, TEXT("HK-TEST done %s: %d failures"), *TestName, Failures);
		FPlatformMisc::RequestExit(false);
		return false;
	});
}
