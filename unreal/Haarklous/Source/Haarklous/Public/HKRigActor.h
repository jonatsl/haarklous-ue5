#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HKRigActor.generated.h"

class FHKRig;
class FHKNode;
class UStaticMeshComponent;
class USceneComponent;

/*
 * Shows an FHKRig (web-space node tree) in Unreal: one scene component per node, a static mesh component for nodes
 * with a mesh (/Game/Haarklous/Meshes/<rig>/<mesh>). Every tick it copies each node's local pose and visibility,
 * converted with HK::TransformToUE, so code that moves rig nodes moves the model.
 * Parts named "<node>#<k>" can carry interaction flags (doors, drinks, locked doors): they get the Interact trace channel.
 */
UCLASS()
class HAARKLOUS_API AHKRigActor : public AActor
{
	GENERATED_BODY()
public:
	AHKRigActor();
	// Builds the components. RigName selects the mesh folder; bSkipRootMesh for rigs whose root mesh is level geometry.
	void Init(FHKRig* InRig, const FString& InRigName, bool bSkipRootMesh = false, bool bCastShadows = true);
	void Sync();
	virtual void Tick(float DeltaSeconds) override;
	// Paint colour for clear-coated body materials (cars, motorcycles).
	void SetPaint(const FLinearColor& Color);
	UStaticMeshComponent* MeshOf(const FHKNode* Node) const;
	// Which rig node a traced component belongs to (or null).
	FHKNode* NodeOfComponent(const UPrimitiveComponent* C) const;
	void SetAllCollision(bool bTrace);
	FHKRig* GetRig() const { return Rig; }
	bool bAutoSync = true;
	// Max draw distance for the meshes (cm, 0 = unlimited).
	void SetCullDistance(float Cm);

private:
	FHKRig* Rig = nullptr;
	FString RigName;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Comps;
	TArray<FHKNode*> Nodes;
	TMap<const UPrimitiveComponent*, FHKNode*> NodeByComp;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> PaintMids;
};
