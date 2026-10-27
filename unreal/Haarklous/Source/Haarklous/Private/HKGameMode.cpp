#include "HKGameMode.h"
#include "HKGame.h"
#include "HKTestDriver.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"

AHKGameMode::AHKGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AHKPlayerController::StaticClass();
	HUDClass = AHKHud::StaticClass();
}

void AHKGameMode::StartPlay()
{
	Super::StartPlay();
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<AHKGame>(AHKGame::StaticClass(), FTransform::Identity, P);
	// Scripted play-throughs and screenshots: -hktest=<name> (see HKTestDriver.cpp).
	FString Test;
	if (FParse::Value(FCommandLine::Get(), TEXT("hktest="), Test)) GetWorld()->SpawnActor<AHKTestDriver>(AHKTestDriver::StaticClass(), FTransform::Identity, P);
}

void AHKPlayerController::BeginPlay()
{
	Super::BeginPlay();
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
}

void AHKHud::DrawHUD()
{
	Super::DrawHUD();
	if (AHKGame* G = AHKGame::Get(this)) G->DrawHud(Canvas);
}
