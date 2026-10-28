#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HKSite.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

/*
 * The construction site's soil (new feature, not in the web game; the user asked for it on 28 Oct 2026).
 * Content/Data/site.json (tools/make-site.py) gives a 64 x 64 m square of the map's local frame, cut out of the terrain
 * mesh by build_assets.py. Inside it the ground is a height field of 0.25 m cells (web frame: metres, y up):
 *   H     the surface
 *   Firm  the top of the compacted ground: soil above it is loose and slumps to its angle of repose (34 degrees);
 *         digging lowers it, dumping does not
 *   Rock  the lowest diggable level (4.5 m under the original ground or the pad)
 *   Base  the game's own terrain height there (FHKData::RelLocal), for the ground colours
 * Machines change it through Cut (remove soil above a tool's bottom inside an oriented rectangle, up to a volume) and
 * Deposit (spread a volume over a rectangle); Step lets loose soil slide. Volumes are cubic metres; soil is conserved
 * (what a tool removes it carries until it puts it down). FHKData::SiteGround makes walking and driving use H.
 */
class HAARKLOUS_API FHKDirtField
{
public:
	bool Load(const FString& Path);
	bool IsLoaded() const { return bLoaded; }

	// World (web frame) queries. Contains: inside the square (shrunk by Margin metres).
	bool Contains(double X, double Z, double Margin = 0) const;
	bool Surface(double X, double Z, double& OutY) const;

	// A tool's footprint: centre and unit forward direction in world x/z, half length along forward, half width,
	// and the height of its cutting edge (web y).
	struct FTool
	{
		FVector2D C = FVector2D::ZeroVector, F = FVector2D(0, -1);
		double HalfL = .5, HalfW = .5, Bottom = 0;
	};
	double Cut(const FTool& T, double MaxVolume);
	double Deposit(const FTool& T, double Volume);
	double VolumeAbove(const FTool& T) const;      // soil above the tool's bottom in its footprint (read only)
	void Step(double Dt);
	double Volume() const;                       // soil above rock (m3)
	double LooseAt(double X, double Z) const;    // loose soil depth (m)

	// Grid (node (i, j) at local x = X0 + i * Cell, s = S0 + j * Cell; index j * NX + i).
	double X0 = 0, S0 = 0, Wd = 0, Dp = 0, Cell = .25;
	int32 NX = 0, NS = 0;
	TArray<float> H, Firm, Rock, Base;
	static constexpr int32 Tile = 32;
	int32 TilesX() const { return (NX - 2) / Tile + 1; }
	int32 TilesS() const { return (NS - 2) / Tile + 1; }
	TSet<int32> DirtyTiles;
	FVector2D NodeWorld(int32 i, int32 j) const;

	// Site layout from site.json (world frame).
	struct FPlaced { FString Kind; double X = 0, Z = 0, Yaw = 0, Len = 0; };
	TArray<FPlaced> Machines, Props, Loads;
	double GateC = 0, GateW = 0;   // the (north) gate: a gap in the fence on its s = FenceS0 side at local x GateC, GateW wide
	double FenceX0 = 0, FenceS0 = 0, FenceW = 0, FenceD = 0;   // the fence (local frame), inside the soil field

private:
	bool bLoaded = false;
	int32 AI0 = 0, AJ0 = 0, AI1 = -1, AJ1 = -1;   // active (unsettled) region
	double Settled = 0;
	void Touch(int32 I0, int32 J0, int32 I1, int32 J1);
	bool Nodes(const FTool& T, TArray<int32>& Out) const;
};

/* Draws the soil: a procedural mesh in 32 x 32-cell tiles (only changed tiles are rebuilt) plus a skirt along the
 * fence that hides the step down to the coarse far terrain. Vertex colour: R loose soil, G subsoil exposed by digging. */
UCLASS()
class HAARKLOUS_API AHKSiteActor : public AActor
{
	GENERATED_BODY()
public:
	AHKSiteActor();
	void Init(FHKDirtField* InField);
	virtual void Tick(float DeltaSeconds) override;
	void RebuildAll();

	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> Material;

private:
	FHKDirtField* Field = nullptr;
	void BuildTile(int32 TI, int32 TJ, bool bCreate);
	void BuildSkirt();
};
