#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HKGameMode.generated.h"

/* No pawn: AHKGame runs the player like the web code does and owns the camera. */
UCLASS()
class HAARKLOUS_API AHKGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AHKGameMode();
	virtual void StartPlay() override;
};

UCLASS()
class HAARKLOUS_API AHKPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
};

/* Draws AHKGame's HUD and menus on the canvas. */
UCLASS()
class HAARKLOUS_API AHKHud : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
};
