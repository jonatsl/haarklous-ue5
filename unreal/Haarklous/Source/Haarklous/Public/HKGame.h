#pragma once

#include <limits>
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HKCollision.h"
#include "HKInput.h"
#include "HKNode.h"
#include "HKVehicle.h"
#include "HKAircraft.h"
#include "HKMinigames.h"
#include "HKFire.h"
#include "HKMission.h"
#include "HKGame.generated.h"

class AHKRigActor;
class UCameraComponent;
class UCanvas;
class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UStaticMesh;

/* A door of the web game (apartment doors and the street doors of the stair towers). */
struct FHKDoor
{
	FString Id, Name, Motion, Sound;
	double OpenAngle = 0, ClosedX = 0, Travel = 0;
	FVector LeafBox = FVector::ZeroVector;   // box geometry parameters (width, height, depth) of the leaf
	FHKMat LeafLocal;                         // leaf pose in the hinge frame
	FHKNode* Hinge = nullptr;                 // rig node (door:<id>)
	bool bOpen = false;
	bool bAnim = false;
	double AnimFrom = 0, AnimTo = 0, AnimT = 0;
	int32 Tower = -1;
	bool bInApartment = false;
};

struct FHKDrink
{
	FString Kind, Name;
	int32 Sips = 0;
	bool bUsed = false;
	FHKNode* Object = nullptr;
	FHKNode* Liquid = nullptr;
};

struct FHKTower
{
	bool bOwn = false;
	int32 Building = -1;
	FHKMat Matrix, Inverse;
};

struct FHKSitSpot
{
	int32 Id = 0;
	double X = 0, Y = 0, Z = 0, Yaw = 0;
};

/* Something the □ button can do right now (findPrompt()). */
struct FHKPrompt
{
	FString Text;
	TFunction<void()> Act;
	bool IsSet() const { return !Text.IsEmpty(); }
};

// The player object of game-main.js.
struct FHKPlayer
{
	double X = 0, Z = 0, Feet = 0, Yaw = 0, Pitch = 0;
	FString Stance = TEXT("stand");
	double Eye = 1.65, EyeNow = 1.65, JumpY = 0, JumpV = 0, Stride = 0;
	bool bRun = false;
	FString Mode = TEXT("foot");   // foot, vehicle, passenger, sit, drone, minigame
	FHKVehicle* Vehicle = nullptr;
	double LookYaw = 0, LookPitch = 0;
	bool bChase = false, bHasChaseCam = false;
	FVector ChaseCam = FVector::ZeroVector;
	double CircleAt = -1;
	bool bCircleHeld = false;
	const FHKSitSpot* SitSpot = nullptr;
	bool bR2Held = false;
};

/*
 * The game: port of game-main.js (frame loop, player, modes, doors, interaction, drinks, drunk effect, HUD, menus).
 * Everything here works in the web frame; the camera pose is converted to Unreal once per frame.
 */
UCLASS()
class HAARKLOUS_API AHKGame : public AActor
{
	GENERATED_BODY()
public:
	AHKGame();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	void DrawHud(UCanvas* Canvas);
	static AHKGame* Get(const UObject* WorldContext);

	// ---- State (names follow game-main.js) ----
	FHKPlayer Player;
	TArray<FHKDoor> Doors;
	TArray<FHKDrink> Drinks;
	TArray<FHKTower> Towers;
	TArray<FHKSitSpot> FireSpots;
	FVector FireCenter = FVector::ZeroVector;
	FVector2D FireWoodpile = FVector2D::ZeroVector, FireCrate = FVector2D::ZeroVector, FirePos = FVector2D(-2.7, 34.7);
	FVector2D MissionMark = FVector2D(4.8, 14.2);
	double Drunk = 0;              // seconds of drunkenness remaining
	double BeerT = 0;
	bool bStarted = false, bPaused = false;
	double FpsAvg = 30;
	struct FSensitivity { double Pad = 1, Mouse = 1; } Sensitivity;
	int32 Quality = 1;
	double Fov = 70;               // vertical field of view (three.js), converted to Unreal's horizontal one
	FVector CamPos = FVector::ZeroVector;
	FQuat CamQuat = FQuat::Identity;
	double Time = 0;               // performance.now() / 1000 equivalent (seconds since start)

	// ---- game-main.js functions ----
	double DrunkLevel() const { return FMath::Min(1.0, Drunk / 90); }
	double StanceEye(const FString& S) const { return S == TEXT("prone") ? .32 : S == TEXT("crouch") ? 1.05 : Player.Eye; }
	double StanceSpeed() const { return Player.Stance == TEXT("prone") ? .28 : Player.Stance == TEXT("crouch") ? .55 : 1; }
	void Place(double X, double Z, double Yaw, double Y = std::numeric_limits<double>::quiet_NaN(), bool bKeepYaw = false);
	void SetStance(const FString& Next);
	void MovePlayer(double Dx, double Dz);
	bool InApartment() const;
	bool InFlat() const;
	bool TowerAt(double X, double Z, double Feet, int32& OutTower, FVector& OutLocal) const;
	FString RoomAt() const;
	void SetDoor(FHKDoor& D, double T);
	TArray<FHKPt> DoorPolygon(const FHKDoor& D, double& MinY, double& MaxY);
	void ToggleDoor(FHKDoor& D);
	void ApplyDoor(const FString& Id, bool bOpenIt, bool bInstant);
	void CloseAllDoors();
	void UpdateDynamic();
	void Notice(const FString& Text, double Seconds = 3);
	FHKPrompt FindPrompt();
	bool Interact(bool bSilent = false);
	void Drink(FHKDrink& D);
	void DrinkBeer();
	void SitDown(const FHKSitSpot& Spot);
	void StandUp();
	FHKPrompt FirePrompt();
	FString PlaceName() const;
	void Footstep(double Soft);
	void Begin();
	void SetPaused(bool bP);
	void SetSens(const FString& K, double V);

	// Look target (raycast from the screen centre against interactable parts, like lookTarget(range)).
	struct FLookHit { FString DoorId; bool bLocked = false; int32 Drink = -1; bool bHit = false; };
	FLookHit LookTarget(double Range = 2.6) const;

	// Input routing.
	TSharedPtr<FHKInputProcessor> InputProc;
	FHKInputFrame Frame;

	// ---- Hooks for the module ports (set up in HKGameModules.cpp); empty = not available ----
	TArray<FHKVehicle*> VehicleList;                 // the web's vehicles[] (multiplayer uses the indices)
	TFunction<TArray<FHKObstacle>()> ExtraDynamic;   // mission actors, remote players ... (updateDynamic)
	bool bDynamicEveryFrame = false;                 // "if (doorsChanged || caseGame?.active) updateDynamic()"
	TFunction<bool()> MissionActive, MissionPlaying, MissionCanMove, PhotoActive, PromptBlocked;
	TFunction<double()> MissionMoveScale, LookScaleExtra, FootFov;
	TFunction<void()> StartMission, OnLeaveFoot, OnBegin, AddWood;
	TFunction<bool(FHKPrompt&)> ModePrompt, VehiclePrompt, MinigamePrompt;
	TFunction<void(const FString&, bool)> OnDoorChanged;   // mp.sendDoor
	TFunction<bool(int32)> SitTaken;
	TFunction<void(const FVector&)> ThrowLog;
	TFunction<double()> SharedFireLevel;
	TFunction<FString()> MusicTrack;
	TFunction<bool()> OverlayBack;                   // Options closes the map / gallery first
	TFunction<bool(double)> PreModeUpdate;           // chat, map, gear menu, photo camera ... return true to skip the rest
	TFunction<void(double, double)> ModeUpdate;      // vehicle, passenger, drone, minigame modes (dt, drunk level)
	TFunction<void(double)> PostModeUpdate;          // other vehicles, multiplayer, fire
	double FireMax = 6, FireDecayS = 90;
	struct FFireState { double Level = 2; double T = 0; } LocalFire;   // offline fire {level: 2, t: now}
	double FireLevel() const;
	void AddLog();
	double FixedDt = 0;                              // tests: run frames with a fixed time step
	TFunction<void(UCanvas*)> HudExtra;              // module HUDs (mission, minigames, map, camera, chat ...)
	TFunction<bool()> HideCrosshair;
	TFunction<float(UCanvas*, float, float, float)> DrawLobby;   // pause menu multiplayer section (x, y, w) -> new y
	TFunction<bool(const FHKInputFrame&)> LobbyInput;            // controller code pad etc. in the pause menu
	TFunction<void(const FString&)> OnMenuClick;
	static void RegisterClickable(const FString& Id, float X, float Y, float W, float H);   // pause-menu hit areas
	FString SpeedoText;                              // #speedo
	bool bSpeedoAir = false;
	FVector2D MenuMouse = FVector2D::ZeroVector;
	// The pause menu shrinks to fit the screen (the web's panel scrolled instead): its height and zoom of last frame.
	float PauseContentH = 0, PauseZoom = 1;
	// Scripted tests: point the menu mouse somewhere instead of the OS cursor, find a menu button's rectangle (last
	// frame, menu units), reach the online module.
	TOptional<FVector2D> TestMenuMouse;
	static bool MenuRect(const FString& Id, FVector2D& Pos, FVector2D& Size);
	class AHKOnline* GetOnline() const { return Online; }
	// Tests and photos: take over the camera pose.
	bool bCameraOverride = false;
	FVector OverridePos = FVector::ZeroVector;
	FQuat OverrideQuat = FQuat::Identity;
	void UpdateCamera();

	// ---- Chat (game-main.js chatLine/openChat/quick chat) ----
	struct FChatLine { FString Name, Text; int32 Slot = 0; bool bSys = false; double T = 0; };
	TArray<FChatLine> ChatLog;
	bool bChatOpen = false, bQuick = false, bQuickStick = false;
	int32 QuickSel = 0;
	double ChatRecentT = 0;
	FString ChatDraft;
	TFunction<bool(const FString&)> SendChat;        // mp.sendChat(text) -> sent online
	TFunction<bool()> IsOnline;
	void ChatLine(const FString& Name, const FString& Text, int32 Slot, bool bSys, bool bMine, bool bOld);
	void OpenChat();
	void CloseChat();
	// ---- Gear menu (hold △), photo camera (game-camera.js), wristwatch (game-watch.js), map (game-map.js) ----
	bool bGear = false, bGearOpen = false, bGearStick = false;
	double GearT0 = 0;
	int32 GearSel = 0, GearLast = 0;
	struct FPhoto { int64 Id = 0; FString File, Place; };
	struct FPhotoCam { bool bActive = false, bGallery = false, bShutterHeld = false, bStick = false; double Raised = 0, Zoom = 1, FlashT = 0, PreviewT = 0; int32 Index = 0; TArray<FPhoto> Photos; } Photo;
	struct FWatch { double Target = 0, T = 0, HoldT = 0, LastDraw = -1; } Watch;
	struct FMapView { bool bOpen = false; double Cx = 0, Cs = 0, Scale = 2.2; } Map;
	TFunction<void()> StartDrone;                    // set by the aircraft module
	TFunction<TArray<TTuple<FVector2D, FLinearColor, FString>>()> RemotePoi;   // other players on the map
	void PhotoToggle();
	void PhotoOpenGallery(bool bOpen);
	void PhotoCapture();
	void WatchToggle() { Watch.Target = Watch.Target ? 0 : 1; Watch.HoldT = 0; }
	void WatchHide() { Watch.Target = 0; }
	void MapToggle();
	bool ExtrasUpdate(double Dt);   // the parts of frame() before the mode update; true = skip the rest of the frame
	void DrawExtras(UCanvas* C);
	void UpdateOverlayModels(double Dt);
	TFunction<void()> MissionRoute;                  // the mission's input routing (between quick chat and the map)

	// ---- Vehicles, aircraft, drone, minigames, firepit (HKGameModules.cpp) ----
	TArray<TUniquePtr<FHKVehicle>> Vehicles;
	TUniquePtr<class FHKDrone> Drone;
	TUniquePtr<struct FHKMinigames> Mini;
	TUniquePtr<class FHKFire> FirePit;
	class FHKMinigame* ActiveMinigame = nullptr;
	TSharedPtr<class IHKEngineSound> PassengerEngine;
	// Multiplayer (offline defaults: every seat is yours, nobody else is around).
	TFunction<int32(int32, const FString&)> Occupant;                                    // mp.occupant(i, seat) -> player id or 0
	TFunction<FString(int32)> NameOf;                                                    // mp.nameOf(id)
	TFunction<void(int32, const FString&, TFunction<void(bool, int32, bool)>)> Claim;    // mp.claim(i, seat) -> (ok, by, timeout)
	TFunction<void(int32, const FString&, const TArray<double>*)> Release;               // mp.release(i, seat, final state)
	TFunction<bool(int32)> RemoteDriven;                                                 // mp.remoteDriven(i)
	int32 IndexOf(const FHKVehicle* V) const;
	FHKVehicle* NearestVehicle();
	void TryDrive(FHKVehicle* V);
	void TryRide(FHKVehicle* V);
	void EnterVehicle(FHKVehicle* V);
	void ExitVehicle();
	void EnterPassenger(FHKVehicle* V);
	void ExitPassenger();
	void StartDroneNow();
	void StopDrone();
	void StartMinigame(class FHKMinigame* G);
	FHKVehicleInput FlightSticks(double Dl) const;
	void SetClip(double NearM);
	TUniquePtr<FHKMission> Mission;
	bool ShootQuery(const FVector& Origin, const FVector& Dir, double MaxDist, FHKShotHit& Hit) const;
	UFUNCTION() void DrawDialRT(UCanvas* C, int32 W, int32 H);

	// Test driver access.
	FString LastPromptText() const { return PromptAction.Text; }
	FString LastNoticeText() const { return NoticeText; }
	void RenderSettingsLog() const;

private:
	void BuildWorld();
	void SpawnRigs();
	void SpawnTrees();
	// Realism pass (visual only): 3D grass clumps near the camera, in 6 m tiles within three tiles of it, only on
	// 'grass' ground and outside the stair towers; off on graphics quality Fast and high above the ground.
	void UpdateGrass();
	void Frame_(double Dt);
	void UpdateFoot(double Dt, double Dl);
	void UpdateSit(double Dt, double Dl);
	void UpdateAmbience();
	void ApplyQuality();
	void SaveSettings() const;
	void LoadSettings();
	void DrawStart(UCanvas* C);
	void DrawPause(UCanvas* C);
	void DrawWorldHud(UCanvas* C);
	void SetCameraFromEuler(double X, double Y, double Z);   // camera.rotation (order YXZ)

	UPROPERTY() TObjectPtr<UCameraComponent> Camera;
	UPROPERTY() TArray<TObjectPtr<AHKRigActor>> RigActors;
	UPROPERTY() TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> TreeComps;
	UPROPERTY() TMap<FIntPoint, TObjectPtr<UInstancedStaticMeshComponent>> GrassTiles;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> GrassPool;   // tiles out of range, reused
	UPROPERTY() TObjectPtr<UStaticMesh> GrassMesh;
	TArray<TUniquePtr<FHKRig>> Rigs;
	FHKRig* AptRig = nullptr;
	TArray<FHKRig*> StairRigs;
	TMap<FString, AHKRigActor*> ActorOfRig;
	FHKPrompt PromptAction;
	double PromptTimer = 0, AmbienceTimer = 0, CullT = 0, NoticeT = 0;
	FString NoticeText;
	double DrunkSwayT = 0;
	bool bDoorsChanged = false;
	TArray<FHKPt> AptFloorBox;     // aptBox corners
	double AptX0 = 0, AptX1 = 0, AptZ0 = 0, AptZ1 = 0;
	struct FRoom { FString Id, Name; TArray<FHKPt> Points; };
	TArray<FRoom> Rooms;
	int32 AptFloorCount = 0;
	TArray<TArray<FHKPt>> AptFloorPolys;
	FVector2D MissionStartLocal = FVector2D::ZeroVector;
	FVector2D StairDoorOutside = FVector2D::ZeroVector;
	double LowFrames = 0;
	float ScreenPercentage = 75;
	int32 PauseSel = 0;
	double PendingThud = 0;
	void PauseInput(double Dt);
	// Overlay models (wristwatch, photo camera) and the photo gallery.
	FHKRig* WatchRig = nullptr;
	FHKRig* PhotoRig = nullptr;
	UPROPERTY() TObjectPtr<AHKRigActor> WatchActor;
	UPROPERTY() TObjectPtr<AHKRigActor> PhotoActor;
	UPROPERTY() TObjectPtr<class UCanvasRenderTarget2D> DialRT;
	UPROPERTY() TObjectPtr<class UTexture2D> GalleryTex;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> DrunkMid;
	void UpdateDrunkEffect();
	FString GalleryTexFile;
	bool bPhotoPending = false;
	FString PhotoPendingPlace;
	FDelegateHandle ShotHandle;
	void DrawDial(class UCanvas* C, int32 W, int32 H);
	void LoadPhotos();
	void SpawnOverlay();
	// Modules (HKGameModules.cpp).
	void SetupModules();
	void UpdateVehicle(double Dt, double Dl);
	void UpdateAircraft(double Dt, double Dl);
	void UpdatePassenger(double Dt, double Dl);
	void UpdateDrone(double Dt, double Dl);
	void UpdateMinigame(double Dt, double Dl);
	void UpdateOthers(double Dt);
	void SyncModuleVisuals();
	void DrawModuleHud(UCanvas* C);
	UPROPERTY() TArray<TObjectPtr<AHKRigActor>> VehicleActors;
	UPROPERTY() TObjectPtr<AHKRigActor> DiscActor;
	UPROPERTY() TObjectPtr<AHKRigActor> PetanqueActor;
	UPROPERTY() TObjectPtr<AHKRigActor> FireActor;
	UPROPERTY() TObjectPtr<class AHKFireEffects> FireFx;
	UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> DirtPiles;
	double VehicleCullT = 0, OsdT = 0;
	struct FOsd { bool bShow = false; FString Tl, Tr, Bl, Br, Warn; double Battery = 0; } Osd;
	void AddDirtPile(double X, double Z, double R);
	void SetupMission();
	UPROPERTY() TObjectPtr<class AHKOnline> Online;
	UPROPERTY() TObjectPtr<class AHKMissionFx> MissionFx;
};
