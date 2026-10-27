#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HKCollision.h"
#include "HKInput.h"
#include "HKNode.h"
#include "HKGame.generated.h"

class AHKRigActor;
class UCameraComponent;
class UCanvas;
class UHierarchicalInstancedStaticMeshComponent;
class FHKVehicle;

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
	void Place(double X, double Z, double Yaw, double Y = NAN, bool bKeepYaw = false);
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

	// Hooks for the module ports (filled in when the modules are merged); null = not available.
	TFunction<bool()> MissionActive;

	// Test driver access.
	FString LastPromptText() const { return PromptAction.Text; }
	FString LastNoticeText() const { return NoticeText; }
	void RenderSettingsLog() const;

private:
	void BuildWorld();
	void SpawnRigs();
	void SpawnTrees();
	void Frame_(double Dt);
	void UpdateFoot(double Dt, double Dl);
	void UpdateSit(double Dt, double Dl);
	void UpdateCamera();
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
};
