#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HKMissionFx.generated.h"

class FHKMission;
class AHKRigActor;
class UPointLightComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInstanceDynamic;

/*
 * Shows the hostage mission in Unreal: one AHKRigActor per figure (hostiles, hostage) and one for the pistol and hands,
 * the two flash lights (the player's muzzle flash and the hostiles' return fire, three.js PointLights in candela) and
 * the bullet holes (dark 22 mm discs that stick to what was hit, at most 80).
 * AHKGame spawns one, calls Bind(&Mission) and leaves it; it ticks after the game actor (TG_PostPhysics), so the rigs
 * show the poses the mission set this frame. The rig actors never touch a figure the mission has already removed:
 * they are synced only from here, after checking FHKMission::RigsVersion.
 */
UCLASS()
class HAARKLOUS_API AHKMissionFx : public AActor
{
	GENERATED_BODY()
public:
	AHKMissionFx();
	void Bind(FHKMission* InMission, AActor* TickAfter = nullptr);
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void Sync();

private:
	FHKMission* Mission = nullptr;
	int32 RigsVersion = -1;
	UPROPERTY() TArray<TObjectPtr<AHKRigActor>> Figures;
	UPROPERTY() TObjectPtr<AHKRigActor> Viewmodel;
	UPROPERTY() TObjectPtr<UPointLightComponent> FlashLight;
	UPROPERTY() TObjectPtr<UPointLightComponent> EnemyFlashLight;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Holes;
	TArray<int32> HoleSerials;
	UPROPERTY() TObjectPtr<UStaticMesh> HoleMesh;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HoleMaterial;
	void ClearFigures();
};
