#pragma once

#include "CoreMinimal.h"
#include <limits>

/*
 * Port of the web game's walk-collision.js (pure first-person collision geometry), line by line, in double precision.
 * Distances are metres in the web frame; y is vertical; points are on the xz plane.
 * A floor is flat at Y, a ramp (stairs: Y + SlopeX*(x-Ox) + SlopeZ*(z-Oz)) or terrain (Height(p)).
 * Every query takes the person's feet height, so stacked storeys do not interfere.
 * Moving solids (door leaves, vehicles, people) go in SetDynamic() without rebuilding the world.
 */
struct FHKPt
{
	double X = 0, Z = 0;
	FHKPt() {}
	FHKPt(double InX, double InZ) : X(InX), Z(InZ) {}
};

struct FHKBounds
{
	double MinX = 0, MaxX = 0, MinZ = 0, MaxZ = 0;
};

struct HAARKLOUS_API FHKFloor
{
	FString Name, SurfaceKind;
	TArray<FHKPt> Points;
	FHKBounds Bounds;
	double CeilingHeight = std::numeric_limits<double>::infinity();
	double Y = 0;
	bool bSlope = false;
	double SlopeX = 0, SlopeZ = 0;
	FHKPt SlopeOrigin;
	TFunction<double(const FHKPt&)> Height;   // terrain floors
	double Surface(const FHKPt& P) const
	{
		if (Height) return Height(P);
		if (!bSlope || (SlopeX == 0 && SlopeZ == 0)) return Y;
		return Y + SlopeX * (P.X - SlopeOrigin.X) + SlopeZ * (P.Z - SlopeOrigin.Z);
	}
};

struct HAARKLOUS_API FHKObstacle
{
	TArray<FHKPt> Points;
	FHKBounds Bounds;
	double MinY = -std::numeric_limits<double>::infinity(), MaxY = std::numeric_limits<double>::infinity();
	FString Kind;
	void Prepare();   // computes Bounds
};

struct FHKBoundaryPiece
{
	FHKPt A, B;
	int32 Floor = -1;
};

struct FHKSupport
{
	bool bFound = false;
	int32 Floor = -1;
	double Y = 0;
};

class HAARKLOUS_API FHKWalkWorld
{
public:
	static const double EPS;
	static const double STEP;
	static const double JOIN;
	static const double CELL;
	static const double INF;

	TArray<FHKFloor> Floors;
	TArray<FHKObstacle> Obstacles;
	TArray<FHKBoundaryPiece> Boundary;
	TArray<FHKObstacle> Dynamic;
	double Radius = .16, Clearance = .08;

	// createWorld({floors, obstacles, radius, clearance})
	void Create(TArray<FHKFloor> InFloors, TArray<FHKObstacle> InObstacles, double InRadius = .16, double InClearance = .08);
	void SetDynamic(TArray<FHKObstacle> List);

	FHKSupport SupportAt(const FHKPt& P, double Y = 0) const;
	bool CanStand(const FHKPt& P, double EyeHeight, double Y = 0) const;
	bool HitsSolid(const FHKPt& P, double Y0, double Y1) const;
	// Returns {x, z, y}: swept/substepped, with sliding; y follows ramps.
	FVector Move(const FHKPt& Position, const FHKPt& Delta, double EyeHeight, double Y = 0) const;
	// Returns false when nothing is found within MaxDistance.
	bool FindSafeSpawn(const FHKPt& Requested, double EyeHeight, FVector& Out, double MaxDistance = 20, double Step = .08, double Y = 0) const;
	double CeilingAt(const FHKPt& P, double Y = 0) const;
	// Returns false for the web's null.
	bool ClampEyeHeight(const FHKPt& P, double Requested, double& Out, double Minimum = .30, double Y = 0) const;

	// Geometry helpers (shared with the rest of the port).
	static bool Inside(const FHKPt& P, const TArray<FHKPt>& Points);
	static double DistanceToSegment(const FHKPt& P, const FHKPt& A, const FHKPt& B);
	static FHKPt ClosestOnSegment(const FHKPt& P, const FHKPt& A, const FHKPt& B);
	static double DistanceToPolygon(const FHKPt& P, const TArray<FHKPt>& Polygon);
	static FHKBounds BoundsOf(const TArray<FHKPt>& Points);
	static bool NearBounds(const FHKPt& P, const FHKBounds& B, double R = 0) { return P.X >= B.MinX - R && P.X <= B.MaxX + R && P.Z >= B.MinZ - R && P.Z <= B.MaxZ + R; }

private:
	struct FCell { TArray<int32> Obstacles; TArray<int32> Boundary; };
	TMap<FIntPoint, FCell> Cells;
	const FCell* CellAt(const FHKPt& P) const;
	void ExposedBoundary();
	FHKPt ClosestInside(const FHKPt& P, const FHKFloor& F) const;
	bool BarrierActive(const FHKBoundaryPiece& Piece, const FHKPt& P, double Y) const;
	static bool Overhead(const FHKObstacle& O, double Y, double Eye) { return O.MaxY > Y + .04 && O.MinY < Y + Eye + .10; }
	// solidsNear(world, p): the cell's static obstacles, then the dynamic ones (same order as the web code).
	template <typename FnType> bool AnySolidNear(const FHKPt& P, FnType&& Fn) const
	{
		if (const FCell* C = CellAt(P)) for (int32 i : C->Obstacles) if (Fn(Obstacles[i])) return true;
		for (const FHKObstacle& O : Dynamic) if (Fn(O)) return true;
		return false;
	}
	template <typename FnType> void ForEachSolidNear(const FHKPt& P, FnType&& Fn) const
	{
		if (const FCell* C = CellAt(P)) for (int32 i : C->Obstacles) Fn(Obstacles[i]);
		for (const FHKObstacle& O : Dynamic) Fn(O);
	}
};
