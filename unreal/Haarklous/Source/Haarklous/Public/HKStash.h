#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HKMath.h"
#include "HKNode.h"

/*
 * Port of game-stash.js (web issue #2, Unreal issue #6): three wooden crates hidden around the neighbourhood (the top
 * landing of a neighbour's stairwell, the courtyard behind the building, by a bush in the park north of the block). Each
 * has lines to snort (a mirror with three lines and a rolled-up note) and a syringe with a tourniquet; using either gives
 * a random 30-second boost (AHKGame: 3x running speed, 3x jump height or bullet time). A used item is back after 90 s.
 *
 * Data: game.json "stash" (places, items with the holder's world position and its hit box, the crate obstacles, which are
 * in the walk world already). Rig "stash" (tools/web-export/exporter-places.js): "item<i>.part<j>" are the parts hidden
 * while item i is used up.
 */
struct FHKStashItem
{
	FString Kind;                    // 'line' | 'needle'
	int32 Place = 0;                 // index into Places
	bool bReady = true;
	double T = 0;                    // cooldown left (s)
	FVector Pos = FVector::ZeroVector;   // the holder's world position (x, y, z)
	FHKHitBox Hit;                   // the invisible box the camera aims at
	TArray<FHKNode*> Parts;
};

struct FHKStashPlace
{
	FString Name;
	double X = 0, Y = 0, Z = 0, Yaw = 0;
};

class HAARKLOUS_API FHKStash
{
public:
	static constexpr double COOLDOWN = 90;

	// J = game.json "stash"; RigPath = Content/Data/rigs/stash.json.
	FHKStash(const TSharedPtr<FJsonObject>& J, const FString& RigPath);
	FHKStash(const FHKStash&) = delete;
	FHKStash& operator=(const FHKStash&) = delete;

	FHKRig Rig;
	TArray<FHKStashPlace> Places;
	TArray<FHKStashItem> Items;

	void Use(int32 Item);
	void Update(double Dt);
	// The stash item closest to a standing player (within reach), ready ones first; -1 if none. P = (x, ?, z).
	int32 Nearest(const FVector& P, double Feet) const;
	// The first item whose hit box the ray (camera position, unit direction) meets within Far; -1 if none.
	int32 Aimed(const FVector& From, const FVector& Dir, double Far, double& OutT) const;
};
