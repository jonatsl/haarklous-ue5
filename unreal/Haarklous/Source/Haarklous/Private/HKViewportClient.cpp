#include "HKViewportClient.h"
#include "HKInput.h"

bool UHKGameViewportClient::InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character)
{
	if (TSharedPtr<FHKInputProcessor> In = FHKInputProcessor::Get())
	{
		In->HandleChar(Character);
		if (In->bTextEntry) return true;
	}
	return Super::InputChar(InViewport, ControllerId, Character);
}
