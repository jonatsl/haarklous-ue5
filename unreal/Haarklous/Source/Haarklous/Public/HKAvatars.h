#pragma once

#include "CoreMinimal.h"
#include "HKNode.h"

class UCanvas;

/*
 * Other players: port of the web's game-avatars.js. A jointed humanoid (jacket colour per player slot) with walk,
 * crouch, prone, sit, drive and ride poses, a beer bottle when drinking, a name tag and a chat bubble; plus the
 * small FPV quad for players flying their drone.
 * The model is a rig exported from the web code (Content/Data/rigs/avatar_<slot>.json, one per slot because the jacket
 * material differs; avatar_drone.json for the quad). The nodes are moved exactly like the JS moved its groups.
 * The group's origin is between the feet and it faces -z (like the camera at yaw 0). For seated poses the origin is
 * the hip. Name tag and bubble were canvas sprites (always on top, depthTest off); here DrawHud draws them on the
 * Unreal canvas at the same place and size.
 */
struct FHKPoseArgs
{
	FString Stance = TEXT("stand");   // stand | crouch | prone | sit | drive | ride | pillion | drone
	double Speed = 0;
	double Dt = 0;
	bool bBeer = false;
};

class HAARKLOUS_API FHKAvatar
{
public:
	FHKAvatar(int32 InSlot, const FString& InName);
	FHKAvatar(const FHKAvatar&) = delete;
	FHKAvatar& operator=(const FHKAvatar&) = delete;

	FHKRig Rig;                         // Rig.Root is the web's group
	FHKNode* Group() const { return Rig.Root; }
	int32 Slot = 0;
	FString RigName;                    // 'avatar_<slot % 5>' (mesh folder for AHKRigActor)
	bool bFromRigFile = false;          // false: the rig file was missing, the skeleton was built in code (no meshes)

	void SetName(const FString& N);
	void Say(const FString& Text);
	void Pose(const FHKPoseArgs& A);
	// Bubble timer and sprite sizes; CameraPos = camera.position (web frame).
	void Update(double Dt, const FVector& CameraPos);
	void DrawHud(UCanvas* C) const;

	// The parts (names of the rig nodes in brackets).
	FHKNode* Hips = nullptr;            // 'hips'
	FHKNode* Torso = nullptr;           // 'torso'
	FHKNode* Neck = nullptr;            // 'neck'
	FHKNode* Head = nullptr;            // 'head'
	FHKNode* Bottle = nullptr;          // 'bottle' (in the right hand)
	struct FArm { FHKNode* Sh = nullptr; FHKNode* El = nullptr; };   // 'L.arm.sh', 'L.arm.el' ...
	struct FLeg { FHKNode* Hip = nullptr; FHKNode* Kn = nullptr; };  // 'L.leg.hip', 'L.leg.kn' ...
	struct FSide { FArm Arm; FLeg Leg; };
	FSide L, R;

	// Sprite state (tag.s / bubble.s of the web).
	FString NameNow;
	FString BubbleText;
	double Phase = 0;
	double BubbleT = 0;
	double TagY = 2.08;
	double BubbleY = 2.53;
	double TagK = 1;                    // tag.s.scale = (1.3 k, .26 k)
	double BubbleK = 1;                 // bubble.s.scale = (2.4 k, .7 k)
	bool bTagVisible = true;
	bool bBubbleVisible = false;

private:
	void BuildSkeleton();
	FVector LastCamera = FVector::ZeroVector;
};

namespace HKAvatars
{
	// SLOT_COLORS (jackets) and SLOT_CSS (name tags, chat names, map markers) of game-avatars.js.
	HAARKLOUS_API uint32 SlotColorHex(int32 Slot);
	HAARKLOUS_API uint32 SlotCssHex(int32 Slot);
	HAARKLOUS_API FLinearColor SlotCss(int32 Slot);     // as a linear colour for FHKUi
	HAARKLOUS_API TUniquePtr<FHKAvatar> Avatar(int32 Slot, const FString& Name);
	HAARKLOUS_API TUniquePtr<FHKRig> Drone();           // rig 'avatar_drone'
	HAARKLOUS_API FString RigPath(const FString& RigName);
}
