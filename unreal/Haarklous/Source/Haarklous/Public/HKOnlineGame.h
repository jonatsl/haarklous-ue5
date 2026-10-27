#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HKMultiplayer.h"
#include "HKOnlineGame.generated.h"

class AHKGame;
class AHKRigActor;
class UCanvas;
struct FHKInputFrame;

/*
 * Connects the online module to the game: the parts of game-main.js that use mp (localState(), the multiplayer ctx,
 * onLobbyChange, renderMp() and the controller code pad of the pause menu, chat and door hand-offs), using the hooks
 * AHKGame already has (Occupant, NameOf, Claim, Release, RemoteDriven, SitTaken, SharedFireLevel, AddWood,
 * OnDoorChanged, SendChat, IsOnline, RemotePoi, OnBegin, PostModeUpdate, HudExtra, DrawLobby, LobbyInput).
 * One call at the end of AHKGame::SetupModules():  AHKOnline::Setup(this);
 * Online stays off unless [Haarklous.Online] bOnlineEnabled=True or -HKOnline (then the game behaves exactly like the
 * web game without a connection). The other players' figures are AHKRigActors of the avatar rigs.
 */
UCLASS()
class HAARKLOUS_API AHKOnline : public AActor
{
	GENERATED_BODY()
public:
	AHKOnline();
	static AHKOnline* Setup(AHKGame* Game);
	FHKMultiplayer* Mp() const { return MpPtr.Get(); }
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// Pause menu (renderMp): the section's buttons call Clickable(id, x, y, w, h) when the game sets it, and the game
	// passes clicks on them to MenuClick(id): mpJoinCode, mpJoin, mpHome, mpName, mpMic.
	TFunction<void(const FString&, float, float, float, float)> Clickable;
	bool MenuClick(const FString& Id);
	float DrawLobby(UCanvas* C, float X, float Y, float W);
	bool LobbyInput(const FHKInputFrame& Frame);
	// The pause-menu actions of game-main.js.
	void JoinCode(const FString& Code);
	void GoHome();
	void ToggleMic();

private:
	void Wire();
	FHKPlayerState LocalState() const;
	void StopTextEntry();

	TWeakObjectPtr<AHKGame> Game;
	TUniquePtr<FHKMultiplayer> MpPtr;
	UPROPERTY() TArray<TObjectPtr<AHKRigActor>> RigActors;
	TArray<FHKRig*> RigKeys;
	TOptional<FHKCodePad> CodePad;
	enum class EEdit : uint8 { None, Code, Name } Edit = EEdit::None;
	FString Draft;
};
