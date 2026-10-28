#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HKRigActor.generated.h"

class FHKRig;
class FHKNode;
class UStaticMeshComponent;
class USceneComponent;
class UPoseableMeshComponent;

/*
 * Shows an FHKRig (web-space node tree) in Unreal: one scene component per node, a static mesh component for nodes
 * with a mesh (/Game/Haarklous/Meshes/<rig>/<mesh>). Every tick it copies each node's local pose and visibility,
 * converted with HK::TransformToUE, so code that moves rig nodes moves the model.
 * Parts named "<node>#<k>" can carry interaction flags (doors, drinks, locked doors): they get the Interact trace channel.
 * Player avatars (rigs avatar_0..4; FHKRig::Person picks one of twelve people, one per slot) are shown as realistic people when /Game/Haarklous/People/<person>/SK_<person> exists (a
 * skinned MakeHuman figure, tools/blender/build_people.py): its bones turn like the rig's joints (hips, torso, neck, head,
 * shoulders, elbows, hips, knees) and the rig's own body parts are hidden; the beer bottle stays a rig part.
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
	USceneComponent* NodeComponent(const FString& NodeName) const;   // the component that follows a rig node
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

	// Realistic person for avatar rigs.
	UPROPERTY() TObjectPtr<UPoseableMeshComponent> Person;
	struct FPersonBone { FName Bone; int32 Node = -1; FQuat Rest = FQuat::Identity; };
	TArray<FPersonBone> PersonBones;
	FQuat PersonFix = FQuat::Identity;      // the mesh's component space -> the rig's frame (Unreal axes)
	FVector PelvisRest = FVector::ZeroVector, HipsRest = FVector::ZeroVector;
	int32 RootIndex = -1, HipsIndex = -1;
	TSet<int32> HiddenNodes;
	void InitPerson();
	void SyncPerson();
};
