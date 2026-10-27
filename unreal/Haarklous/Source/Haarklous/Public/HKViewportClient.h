#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "HKViewportClient.generated.h"

/* Passes typed characters (chat, lobby code, name) to the game's input processor. */
UCLASS()
class HAARKLOUS_API UHKGameViewportClient : public UGameViewportClient
{
	GENERATED_BODY()
public:
	virtual bool InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character) override;
};
