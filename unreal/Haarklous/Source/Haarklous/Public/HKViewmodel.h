#pragma once

#include "CoreMinimal.h"
#include "HKNode.h"

/*
 * The first-person pistol and gloved hands of the hostage mission (geometry: viewmodel.js; pose: case-game.js).
 * The meshes come from the web export, rig "mission_viewmodel" (Content/Data/rigs/mission_viewmodel.json):
 *   root   the web's viewmodel camera (vmCamera: vertical FOV 62 degrees, near 0.01 m), here placed at the player camera
 *   gun    the gun group: hip / aim-down-sights pose, bob, recoil kick and reload tilt (FHKMission::Tick moves it)
 *   slide  recoils along z;  flash  the muzzle flash cone (visible for 55 ms after a shot)
 * In the web the viewmodel has its own camera and is drawn over the world without depth, so it never clips walls.
 * Here the rig hangs in the world at the player camera: FHKMission puts Root at the camera pose every frame, with the
 * scale from RootScale(), which (a) makes the camera-space rig project exactly as through a 62-degree camera at whatever
 * FOV the world camera has (65 -> 43 degrees while aiming), and (b) shrinks it toward the eye (perspective does not
 * change), so it sits inside the player's collision radius and cannot poke into walls. It needs a near clip plane of
 * about 1 cm (r.SetNearClipPlane 1) and should not cast shadows.
 */
class HAARKLOUS_API FHKViewmodel
{
public:
	FHKViewmodel();
	FHKViewmodel(const FHKViewmodel&) = delete;
	FHKViewmodel& operator=(const FHKViewmodel&) = delete;

	FHKRig Rig;
	FHKNode* Gun = nullptr;
	FHKNode* Slide = nullptr;
	FHKNode* Flash = nullptr;
	bool bLoaded = false;   // nodes and mesh names came from the rig file (otherwise bare nodes with the web's rest poses)

	// case-game.js: const hip = {p: (.12, -.075, -.44), r: (.02, .08, 0)}, ads = {p: (0, -.029, -.36), r: (0, 0, 0)}
	static const FVector HipP;
	static const FVector HipR;
	static const FVector AdsP;
	static const FVector AdsR;
	static constexpr double FovY = 62;
	static constexpr double DefaultShrink = .25;

	// Scale of the root in web axes (x, y, z) for a world camera with vertical FOV WorldFovYDeg.
	static FVector RootScale(double WorldFovYDeg, double Shrink = DefaultShrink);
	// Root = camera pose (web space), scaled by RootScale; Visible = show it at all.
	void PlaceAtCamera(const FVector& CamPos, const FQuat& CamQuat, double WorldFovYDeg, bool bVisible, double Shrink = DefaultShrink);
};
