#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HKCollision.h"

/* Small helpers for the exported JSON (Content/Data/*.json). */
namespace HKJson
{
	HAARKLOUS_API double Num(const TSharedPtr<FJsonObject>& O, const FString& K, double Default = 0);
	HAARKLOUS_API FString Str(const TSharedPtr<FJsonObject>& O, const FString& K, const FString& Default = FString());
	HAARKLOUS_API bool Bool(const TSharedPtr<FJsonObject>& O, const FString& K, bool Default = false);
	HAARKLOUS_API TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const FString& K);
	HAARKLOUS_API const TArray<TSharedPtr<FJsonValue>>& Arr(const TSharedPtr<FJsonObject>& O, const FString& K);
	HAARKLOUS_API TArray<double> Nums(const TSharedPtr<FJsonObject>& O, const FString& K);
	HAARKLOUS_API TArray<double> Nums(const TSharedPtr<FJsonValue>& V);
	HAARKLOUS_API FVector Vec(const TSharedPtr<FJsonObject>& O, const FString& K, const FVector& Default = FVector::ZeroVector);
	// [[x, z], ...] -> points
	HAARKLOUS_API TArray<FHKPt> Pts(const TArray<TSharedPtr<FJsonValue>>& A);
	// Exported +-1e9 stand for +-Infinity.
	inline double Inf(double V) { return V >= 1e8 ? std::numeric_limits<double>::infinity() : (V <= -1e8 ? -std::numeric_limits<double>::infinity() : V); }
	HAARKLOUS_API TSharedPtr<FJsonObject> LoadFile(const FString& Path, FString* Error = nullptr);
}

/*
 * The world data of the web version, loaded once from Content/Data (written by tools/web-export):
 * map frame (toWorld/toLocal), Kartverket terrain heights, surface classes, traffic and greenery for the ambience,
 * and the walk-collision worlds built from exactly the same floors and obstacles as the web game.
 */
class HAARKLOUS_API FHKData
{
public:
	static FHKData& Get();
	FHKData() = default;
	FHKData(const FHKData&) = delete;
	FHKData& operator=(const FHKData&) = delete;
	static FString DataDir();

	bool bLoaded = false;
	FString Error;
	TSharedPtr<FJsonObject> Game, D, Apartment;   // game.json, surroundings.json, apartment.json

	double Theta = 0, CT = 1, ST = 0, GeoX = 0, GeoZ = 0, Ground = 0, PlayHalf = 540;
	FVector SunDir = FVector(0, 1, 0);

	// surroundings.js: toWorld(x, s) -> (x, z); toLocal(x, z) -> (x, s)
	FVector2D ToWorld(double X, double S) const { return FVector2D(X * CT + S * ST + GeoX, -X * ST + S * CT + GeoZ); }
	FVector2D ToLocal(double X, double Z) const { const double dx = X - GeoX, dz = Z - GeoZ; return FVector2D(dx * CT - dz * ST, dx * ST + dz * CT); }
	double RelLocal(double X, double S) const;
	double YLocal(double X, double S) const { return Ground + RelLocal(X, S); }
	double HeightWorld(double X, double Z) const { const FVector2D L = ToLocal(X, Z); return YLocal(L.X, L.Y); }
	/** The construction site (new feature): when set, the ground inside the site is its soil (FHKDirtField). Walking
	 *  and driving use GroundWorld; HeightWorld stays the web's terrain (the automation tests compare it). */
	TFunction<bool(double, double, double&)> SiteGround;
	double GroundWorld(double X, double Z) const { double H; if (SiteGround && SiteGround(X, Z, H)) return H; return HeightWorld(X, Z); }
	/** Looks only: the height of the drawn ground (the web's terrain mesh triangles, 4 m cells within 300 m, else
	 *  8 m cells 0.5 m lower), which differs from the bilinear HeightWorld by a few cm. For grass and kerbs. */
	double VisualHeightWorld(double X, double Z) const;
	// "grass", "asphalt", "gravel", "sand" or "none" (footstep sounds)
	FString SurfaceAt(double X, double Z) const;
	double TrafficDistance(double X, double Z) const;
	double Greenery(double X, double Z) const;

	struct FTree { double X, S, H; };
	TArray<FTree> AllTrees;   // world.trees: data.trees + courtyard trees (local frame)

	// The player's collision world (apartment, stair towers, surroundings, firepit) and the vehicles' worlds.
	FHKWalkWorld WalkWorld;
	FHKWalkWorld& VehicleWorld(double Radius);
	// The vehicle worlds made so far (game-main.js updateDynamic sets the dynamic obstacles of the existing ones only).
	void ForEachVehicleWorld(TFunctionRef<void(FHKWalkWorld&)> Fn) { for (TPair<FString, TUniquePtr<FHKWalkWorld>>& P : VehicleWorlds) Fn(*P.Value); }
	FHKFloor TerrainFloor() const;
	static TArray<FHKFloor> FloorsFromJson(const TArray<TSharedPtr<FJsonValue>>& A);
	static TArray<FHKObstacle> ObstaclesFromJson(const TArray<TSharedPtr<FJsonValue>>& A);

private:
	bool Load();
	bool bTried = false;
	int32 TerN = 0;
	double TerHalf = 600, TerStep = 4;
	TArray<float> Grid;
	TArray<uint8> SurfaceClasses;
	struct FBush { double X, S, R; };
	TArray<FBush> Bushes;
	struct FSeg { FVector2D A, B; double W; };
	TArray<FSeg> Traffic;
	TArray<FHKObstacle> VehicleObstacles;
	TMap<FString, TUniquePtr<FHKWalkWorld>> VehicleWorlds;
};
