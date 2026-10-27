#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HKCollision.h"
#include "HKMath.h"
#include "HKNode.h"
#include "HKFire.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;

/*
 * Port of game-fire.js: the firepit in the park in front of Haarklous plass 17A (world -2.7, 34.7). A stone ring with
 * burning logs, five benches around it (ten seats), a woodpile to throw more logs on and a crate of beer. The fire
 * level (0-6) sets flame size, light, embers, smoke and sound; it burns down by one level every 90 s (FHKFireLevel,
 * shared in multiplayer).
 *
 * Main loop (game-main.js):
 *   FHKFire Fire(-2.7, 34.7, FHKFireContext::FromGame());   obstacles -> walk and vehicle worlds; Spots/Woodpile/Crate
 *   rig actor for Fire.Rig (rig name "fire"); AHKFireEffects spawned at HK::ToUE(Fire.Center), Init(rig actor mesh of
 *   node "charred")
 *   every frame: Fire.Update(Dt, Level, CameraWebPos); Effects->Sync(Fire, CameraWebPos)
 *   a thrown log: Fire.ThrowLog(FVector(player.x, player.feet + 1.3, player.z)); remote: same with the remote position
 * Math.random comes from the context; the construction uses the module's own seeded generator (seed 7) like the JS.
 */
struct FHKFireSpot
{
	int32 Id = 0;
	double X = 0, Y = 0, Z = 0, Yaw = 0;
};

struct HAARKLOUS_API FHKFireContext
{
	TFunction<double(double, double)> HeightAt;   // world.heightWorld(x, z)
	TFunction<double()> Random;                   // Math.random()
	FString RigDir;                               // folder of fire.json
	static FHKFireContext FromGame();
};

class HAARKLOUS_API FHKFire
{
public:
	static const double DECAY_S;
	static const double MAX;
	static constexpr int32 EN = 70;
	static constexpr int32 SN = 26;
	static constexpr int32 LogPool = 8;

	FHKFire(double InX, double InZ, const FHKFireContext& InCtx);
	FHKFire(const FHKFire&) = delete;
	FHKFire& operator=(const FHKFire&) = delete;

	FHKRig Rig;                      // the JS group: root at (x, y0, z); nodes "charred" (glowing logs) and the thrown logs
	double X = 0, Z = 0, Y0 = 0;
	FVector Center = FVector::ZeroVector;   // {x, y: y0, z}
	TArray<FHKFireSpot> Spots;
	FVector2D Woodpile = FVector2D::ZeroVector, Crate = FVector2D::ZeroVector;
	TArray<FHKObstacle> Obstacles;

	void Update(double Dt, double Level, const FVector& CameraPos);
	// throwLog(from): from.y is optional (?? y0 + 1.2).
	void ThrowLog(const FVector& From, bool bHasY = true);

	// ---- Visual state, the same values the web objects get (read by AHKFireEffects) ----
	struct FFlame
	{
		double K = 1;                  // userData.k
		double Off[2] = {0, 0};        // userData.off
		double Time = 0, Power = 1;    // shader uniforms
		bool bVisible = true;
		FVector Scale = FVector::OneVector, Position = FVector::ZeroVector;   // local to the group
		double RotY = 0;
	};
	FFlame Flames[6];
	struct FEmber { double Life = 0; double V[3] = {0, 0, 0}; };
	TArray<FEmber> Embers;
	TArray<float> EmberPos;          // Float32Array(EN * 3), local
	bool bEmbersVisible = true;
	double EmberSize = .045;
	TArray<double> SmokeLife;
	TArray<float> SmokePos;          // Float32Array(SN * 3), local
	double SmokeOpacity = .32;
	double LightIntensity = 0, LightDistance = 16;
	FVector LightPos = FVector(0, .9, 0);
	double CharredEmissiveIntensity = 0;
	struct FFlying { FHKNode* Node = nullptr; double T = 0; FVector Start = FVector::ZeroVector, Spin = FVector::ZeroVector; };
	TArray<FFlying> Flying;
	double Time = 0, Flick = 1;
	FHKNode* Charred = nullptr;

private:
	FHKFireContext Ctx;
	HK::FParkMiller Rnd = HK::FParkMiller(7);
	TArray<FHKNode*> LogNodes;
	FHKObstacle Rect(double Cx, double Cz, double AlongA, double Len, double Wid, double MinY, double MaxY) const;
};

/* Fire level shared by the lobby (game-multiplayer.js fireLevel()/addWood(), cloud fireNow()): {level, t}. */
struct HAARKLOUS_API FHKFireLevel
{
	double Level = 2, T = 0;       // T in ms (Date.now() or the server clock)
	double DecayS = 90, Max = 6;
	double At(double NowMs) const { return FMath::Max(0.0, Level - (NowMs - T) / (DecayS * 1000)); }
	void AddWood(double NowMs) { const double L = FMath::Min(Max, At(NowMs) + 1); Level = L; T = NowMs; }
};

/*
 * What the web draws with a ShaderMaterial and Points (no mesh in the rigs): six flame cards that face the camera, 70
 * embers, 26 smoke puffs and the point light. Built from C++ only: engine basic shapes, engine materials with dynamic
 * instances and textures made at run time (the flame texture is the web's fragment shader evaluated on the CPU).
 */
UCLASS()
class HAARKLOUS_API AHKFireEffects : public AActor
{
	GENERATED_BODY()
public:
	AHKFireEffects();
	// Optional: the mesh component of rig node "charred", whose EmissiveIntensity follows the fire level.
	void Init(UStaticMeshComponent* InCharred = nullptr);
	// Copies the fire's state; call every frame after FHKFire::Update. CameraWeb = camera position in web space.
	void Sync(const FHKFire& Fire, const FVector& CameraWeb);
	// Beyond this distance (m) embers and smoke are hidden and the flame textures are not animated.
	double DetailDistance = 150;
	// web PointLight intensity (candela, three.js r160) -> Unreal candela
	double LightScale = 1;
	// The web clamps each flame pixel at 1; Unreal blooms emissive above 1, so the cards are drawn a bit dimmer.
	double FlameGain = .5;

	UPROPERTY() TObjectPtr<UPointLightComponent> Light;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> FlameCards;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> EmberMeshes;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SmokeMeshes;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> FlameMids;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> EmberMid;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SmokeMid;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CharredMid;
	UPROPERTY() TArray<TObjectPtr<UTexture2D>> FlameTextures;
	UPROPERTY() TObjectPtr<UTexture2D> SmokeTexture;
	UPROPERTY() TObjectPtr<UTexture2D> WhiteTexture;

	bool IsBuilt() const { return bBuilt; }
	// Parameter names found on the engine materials (for the tests and the log).
	FName AdditiveColorParam, AdditiveTextureParam, TranslucentColorParam, TranslucentTextureParam;
	// The web flame shader (game-fire.js fragmentShader) at uv, time: returns (r, g, b) * shape and shape.
	static FLinearColor FlameShader(double U, double V, double Time);
	// Axis of /Engine/BasicShapes/Plane along which its texture goes up (towards V = 0).
	static FVector PlaneUpAxis();

private:
	bool bBuilt = false;
	double TextureClock = 0;
	double LastTime = -1;
	void Build();
	void PaintFlame(int32 Index, double Time);
	UStaticMeshComponent* MakeMesh(UStaticMesh* Mesh, UMaterialInterface* Mat, const TCHAR* Name);
};
