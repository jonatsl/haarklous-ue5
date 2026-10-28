#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HKTestDriver.generated.h"

class AHKGame;

/*
 * Scripted play-throughs with a simulated DualShock 4, like the web's tests/browser-harness.js:
 *   UnrealEditor.exe Haarklous.uproject -game -hktest=<name> [-hkout=<folder>]
 * Steps: wait, press/hold buttons, move sticks, walk to a point, take screenshots, measure the frame rate, log state.
 * Results go to the log (lines starting with "HK-TEST") and screenshots to tools/out/shots/<name>/.
 * Scripts are registered in HKTestScripts.cpp.
 */
UCLASS()
class HAARKLOUS_API AHKTestDriver : public AActor
{
	GENERATED_BODY()
public:
	AHKTestDriver();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- step builders (each step runs until it returns true) ----
	using FStep = TFunction<bool(double Dt)>;
	TArray<FStep> Steps;
	void Wait(double Seconds);
	void Do(TFunction<void()> Fn);
	void Press(int32 Button, double Seconds = .08);   // standard mapping index
	void Hold(int32 Button, double Value);
	void Stick(double Lx, double Ly, double Seconds, double Rx = 0, double Ry = 0);
	void Key(const FString& Code, double Seconds = .08);
	void WalkTo(double X, double Z, double MaxSeconds = 15);
	void Shot(const FString& Name);
	void Pose(double X, double Y, double Z, double Yaw, double Pitch);   // fixed camera (web frame)
	void FreeCamera();
	void MeasureFps(const FString& Label, double Seconds);
	void WaitShaders(double MaxSeconds = 600);   // uncooked runs: until materials that just came into view are compiled
	void Log(TFunction<FString()> Fn);
	void Expect(const FString& What, TFunction<bool()> Fn);
	void Quit();

	AHKGame* Game() const;
	FString OutDir;
	FString TestName;
	int32 Failures = 0;
	FString P() const;   // the harness's P(): position, feet, mode

private:
	int32 Index = 0;
	double StepT = 0;
	bool bStepStarted = false;
};

// Registry of scripts: name -> function that adds steps to the driver.
struct HAARKLOUS_API FHKTestScripts
{
	static TMap<FString, TFunction<void(AHKTestDriver&)>>& All();
};
