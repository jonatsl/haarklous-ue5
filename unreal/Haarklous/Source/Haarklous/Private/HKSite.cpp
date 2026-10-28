#include "HKSite.h"
#include "HKData.h"
#include "HKMath.h"
#include "Haarklous.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const double TanRepose = 0.6745;   // loose soil: 34 degrees

	TArray<int16> Int16s(const FString& B64)
	{
		TArray<uint8> Bytes;
		FBase64::Decode(B64, Bytes);
		TArray<int16> Out;
		Out.SetNumUninitialized(Bytes.Num() / 2);
		FMemory::Memcpy(Out.GetData(), Bytes.GetData(), Out.Num() * 2);
		return Out;
	}
}

// ---------- Field ----------
bool FHKDirtField::Load(const FString& Path)
{
	const TSharedPtr<FJsonObject> J = HKJson::LoadFile(Path);
	if (!J) return false;
	const FHKData& D = FHKData::Get();
	const TSharedPtr<FJsonObject> R = HKJson::Obj(J, TEXT("rect"));
	X0 = HKJson::Num(R, TEXT("x0")); S0 = HKJson::Num(R, TEXT("s0")); Wd = HKJson::Num(R, TEXT("w")); Dp = HKJson::Num(R, TEXT("d"));
	Cell = HKJson::Num(J, TEXT("cell"), .25);
	NX = (int32)HKJson::Num(J, TEXT("nx")); NS = (int32)HKJson::Num(J, TEXT("ns"));
	const TArray<int16> Off = Int16s(HKJson::Str(J, TEXT("offsets"))), FirmOff = Int16s(HKJson::Str(J, TEXT("firm")));
	if (Off.Num() != NX * NS || FirmOff.Num() != NX * NS) { UE_LOG(LogHaarklous, Warning, TEXT("site.json: bad grid")); return false; }
	const double RockCm = HKJson::Num(J, TEXT("rock"), 450);
	H.SetNum(NX * NS); Firm.SetNum(NX * NS); Rock.SetNum(NX * NS); Base.SetNum(NX * NS);
	for (int32 j = 0; j < NS; j++)
		for (int32 i = 0; i < NX; i++)
		{
			const int32 k = j * NX + i;
			const double B = D.YLocal(X0 + i * Cell, S0 + j * Cell);
			Base[k] = (float)B;
			H[k] = (float)(B + Off[k] / 100.0);
			Firm[k] = FMath::Min(H[k], (float)(B + FirmOff[k] / 100.0));
			Rock[k] = (float)(FMath::Min(B, B + FirmOff[k] / 100.0) - RockCm / 100.0);
		}
	auto ReadPlaced = [&J](const TCHAR* Key, TArray<FPlaced>& Out)
	{
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(J, Key))
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FPlaced P;
			P.Kind = HKJson::Str(O, TEXT("kind")); P.X = HKJson::Num(O, TEXT("x")); P.Z = HKJson::Num(O, TEXT("z"));
			P.Yaw = HKJson::Num(O, TEXT("yaw")); P.Len = HKJson::Num(O, TEXT("len"));
			Out.Add(P);
		}
	};
	ReadPlaced(TEXT("machines"), Machines); ReadPlaced(TEXT("props"), Props); ReadPlaced(TEXT("loads"), Loads);
	for (int32 t = 0; t < TilesX() * TilesS(); t++) DirtyTiles.Add(t);
	// Let the stockpiles settle once before the game starts.
	Touch(0, 0, NX - 1, NS - 1);
	for (int32 k = 0; k < 40; k++) Step(1.0 / 30);
	bLoaded = true;
	return true;
}

FVector2D FHKDirtField::NodeWorld(int32 i, int32 j) const
{
	return FHKData::Get().ToWorld(X0 + i * Cell, S0 + j * Cell);
}

bool FHKDirtField::Contains(double X, double Z, double Margin) const
{
	const FVector2D L = FHKData::Get().ToLocal(X, Z);
	return L.X >= X0 + Margin && L.X <= X0 + Wd - Margin && L.Y >= S0 + Margin && L.Y <= S0 + Dp - Margin;
}

bool FHKDirtField::Surface(double X, double Z, double& OutY) const
{
	if (!bLoaded && H.Num() == 0) return false;
	const FVector2D L = FHKData::Get().ToLocal(X, Z);
	const double Fi = (L.X - X0) / Cell, Fj = (L.Y - S0) / Cell;
	if (Fi < 0 || Fj < 0 || Fi > NX - 1 || Fj > NS - 1) return false;
	const int32 i = FMath::Min(NX - 2, FMath::FloorToInt32(Fi)), j = FMath::Min(NS - 2, FMath::FloorToInt32(Fj));
	const double u = Fi - i, v = Fj - j;
	const int32 k = j * NX + i;
	OutY = H[k] * (1 - u) * (1 - v) + H[k + 1] * u * (1 - v) + H[k + NX] * (1 - u) * v + H[k + NX + 1] * u * v;
	return true;
}

double FHKDirtField::LooseAt(double X, double Z) const
{
	const FVector2D L = FHKData::Get().ToLocal(X, Z);
	const int32 i = FMath::RoundToInt32((L.X - X0) / Cell), j = FMath::RoundToInt32((L.Y - S0) / Cell);
	if (i < 0 || j < 0 || i >= NX || j >= NS) return 0;
	return H[j * NX + i] - Firm[j * NX + i];
}

bool FHKDirtField::Nodes(const FTool& T, TArray<int32>& Out) const
{
	Out.Reset();
	const FVector2D R(-T.F.Y, T.F.X);
	const FHKData& D = FHKData::Get();
	double Li0 = 1e9, Lj0 = 1e9, Li1 = -1e9, Lj1 = -1e9;
	for (const FVector2D& Ab : {FVector2D(1, 1), FVector2D(1, -1), FVector2D(-1, 1), FVector2D(-1, -1)})
	{
		const FVector2D W = T.C + T.F * (T.HalfL * Ab.X) + R * (T.HalfW * Ab.Y);
		const FVector2D L = D.ToLocal(W.X, W.Y);
		Li0 = FMath::Min(Li0, (L.X - X0) / Cell); Li1 = FMath::Max(Li1, (L.X - X0) / Cell);
		Lj0 = FMath::Min(Lj0, (L.Y - S0) / Cell); Lj1 = FMath::Max(Lj1, (L.Y - S0) / Cell);
	}
	const int32 I0 = FMath::Max(1, FMath::FloorToInt32(Li0)), I1 = FMath::Min(NX - 2, FMath::CeilToInt32(Li1));
	const int32 J0 = FMath::Max(1, FMath::FloorToInt32(Lj0)), J1 = FMath::Min(NS - 2, FMath::CeilToInt32(Lj1));
	for (int32 j = J0; j <= J1; j++)
		for (int32 i = I0; i <= I1; i++)
		{
			const FVector2D P = NodeWorld(i, j) - T.C;
			if (FMath::Abs(FVector2D::DotProduct(P, T.F)) <= T.HalfL && FMath::Abs(FVector2D::DotProduct(P, R)) <= T.HalfW) Out.Add(j * NX + i);
		}
	return Out.Num() > 0;
}

double FHKDirtField::Cut(const FTool& T, double MaxVolume)
{
	TArray<int32> K;
	if (MaxVolume <= 0 || !Nodes(T, K)) return 0;
	const double A = Cell * Cell;
	double Avail = 0;
	for (int32 k : K) Avail += FMath::Max(0.0, H[k] - FMath::Max(T.Bottom, (double)Rock[k])) * A;
	if (Avail <= 1e-6) return 0;
	const double Scale = FMath::Min(1.0, MaxVolume / Avail);
	int32 I0 = NX, J0 = NS, I1 = 0, J1 = 0;
	for (int32 k : K)
	{
		const double Take = FMath::Max(0.0, H[k] - FMath::Max(T.Bottom, (double)Rock[k])) * Scale;
		if (Take <= 0) continue;
		H[k] -= (float)Take;
		Firm[k] = FMath::Min(Firm[k], H[k]);
		I0 = FMath::Min(I0, k % NX); I1 = FMath::Max(I1, k % NX); J0 = FMath::Min(J0, k / NX); J1 = FMath::Max(J1, k / NX);
	}
	if (I1 >= I0) Touch(I0, J0, I1, J1);
	return Avail * Scale;
}

double FHKDirtField::VolumeAbove(const FTool& T) const
{
	TArray<int32> K;
	if (!Nodes(T, K)) return 0;
	double V = 0;
	for (int32 k : K) V += FMath::Max(0.0, H[k] - FMath::Max(T.Bottom, (double)Rock[k]));
	return V * Cell * Cell;
}

double FHKDirtField::Deposit(const FTool& T, double Volume)
{
	TArray<int32> K;
	if (Volume <= 0 || !Nodes(T, K)) return 0;
	const double Dh = Volume / (K.Num() * Cell * Cell);
	int32 I0 = NX, J0 = NS, I1 = 0, J1 = 0;
	for (int32 k : K)
	{
		H[k] += (float)Dh;
		I0 = FMath::Min(I0, k % NX); I1 = FMath::Max(I1, k % NX); J0 = FMath::Min(J0, k / NX); J1 = FMath::Max(J1, k / NX);
	}
	Touch(I0, J0, I1, J1);
	return Volume;
}

void FHKDirtField::Touch(int32 I0, int32 J0, int32 I1, int32 J1)
{
	const int32 P = 8;   // loose soil runs a little beyond what the tool touched
	I0 = FMath::Max(0, I0 - P); J0 = FMath::Max(0, J0 - P); I1 = FMath::Min(NX - 1, I1 + P); J1 = FMath::Min(NS - 1, J1 + P);
	if (AI1 < AI0) { AI0 = I0; AJ0 = J0; AI1 = I1; AJ1 = J1; }
	else { AI0 = FMath::Min(AI0, I0); AJ0 = FMath::Min(AJ0, J0); AI1 = FMath::Max(AI1, I1); AJ1 = FMath::Max(AJ1, J1); }
	Settled = 0;
	for (int32 tj = FMath::Max(0, (J0 - 1) / Tile); tj <= FMath::Min(TilesS() - 1, J1 / Tile); tj++)
		for (int32 ti = FMath::Max(0, (I0 - 1) / Tile); ti <= FMath::Min(TilesX() - 1, I1 / Tile); ti++)
			DirtyTiles.Add(tj * TilesX() + ti);
}

void FHKDirtField::Step(double Dt)
{
	if (AI1 < AI0) return;
	// Loose soil slides to its angle of repose: between neighbours the surface may differ by Cell * tan(34 deg);
	// a steeper step moves soil from the higher node (only its loose part) to the lower one. Two sweeps a frame.
	const float Lim = (float)(Cell * TanRepose);
	double Moved = 0;
	int32 I0 = NX, J0 = NS, I1 = -1, J1 = -1;
	for (int32 Sweep = 0; Sweep < 2; Sweep++)
		for (int32 j = FMath::Max(0, AJ0); j <= FMath::Min(NS - 1, AJ1); j++)
			for (int32 i = FMath::Max(0, AI0); i <= FMath::Min(NX - 1, AI1); i++)
			{
				const int32 k = j * NX + i;
				for (const int32 n : {i + 1 < NX ? k + 1 : -1, j + 1 < NS ? k + NX : -1})
				{
					if (n < 0) continue;
					const float Dh = H[k] - H[n];
					if (FMath::Abs(Dh) <= Lim) continue;
					const int32 Hi = Dh > 0 ? k : n, Lo = Dh > 0 ? n : k;
					const float M = FMath::Min((FMath::Abs(Dh) - Lim) * .25f, H[Hi] - Firm[Hi]);
					if (M <= 1e-5f) continue;
					H[Hi] -= M; H[Lo] += M;
					Moved += M;
					const int32 a = Hi % NX, b = Hi / NX;
					I0 = FMath::Min(I0, FMath::Min(a, n % NX)); I1 = FMath::Max(I1, FMath::Max(a, n % NX));
					J0 = FMath::Min(J0, FMath::Min(b, n / NX)); J1 = FMath::Max(J1, FMath::Max(b, n / NX));
				}
			}
	if (I1 >= I0)
	{
		for (int32 tj = FMath::Max(0, (J0 - 1) / Tile); tj <= FMath::Min(TilesS() - 1, J1 / Tile); tj++)
			for (int32 ti = FMath::Max(0, (I0 - 1) / Tile); ti <= FMath::Min(TilesX() - 1, I1 / Tile); ti++)
				DirtyTiles.Add(tj * TilesX() + ti);
	}
	if (Moved < 1e-3) { Settled += Dt; if (Settled > .5) { AI0 = 0; AJ0 = 0; AI1 = -1; AJ1 = -1; } }
	else Settled = 0;
}

double FHKDirtField::Volume() const
{
	double V = 0;
	for (int32 k = 0; k < H.Num(); k++) V += H[k] - Rock[k];
	return V * Cell * Cell;
}

// ---------- Mesh ----------
AHKSiteActor::AHKSiteActor()
{
	PrimaryActorTick.bCanEverTick = true;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Soil"));
	RootComponent = Mesh;
	Mesh->bUseAsyncCooking = false;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(true);
}

void AHKSiteActor::Init(FHKDirtField* InField)
{
	Field = InField;
	Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Haarklous/Materials/M_HK_Dirt.M_HK_Dirt"));
	RebuildAll();
}

void AHKSiteActor::RebuildAll()
{
	if (!Field) return;
	Mesh->ClearAllMeshSections();
	for (int32 tj = 0; tj < Field->TilesS(); tj++)
		for (int32 ti = 0; ti < Field->TilesX(); ti++) BuildTile(ti, tj, true);
	BuildSkirt();
	Field->DirtyTiles.Reset();
}

void AHKSiteActor::BuildTile(int32 TI, int32 TJ, bool bCreate)
{
	const FHKDirtField& F = *Field;
	const int32 I0 = TI * FHKDirtField::Tile, J0 = TJ * FHKDirtField::Tile;
	const int32 I1 = FMath::Min(F.NX - 1, I0 + FHKDirtField::Tile), J1 = FMath::Min(F.NS - 1, J0 + FHKDirtField::Tile);
	const int32 Cw = I1 - I0 + 1, Ch = J1 - J0 + 1;
	TArray<FVector> V; TArray<FVector> N; TArray<FVector2D> UV; TArray<FColor> C; TArray<FProcMeshTangent> T; TArray<int32> Tri;
	V.SetNumUninitialized(Cw * Ch); N.SetNumUninitialized(Cw * Ch); UV.SetNumUninitialized(Cw * Ch); C.SetNumUninitialized(Cw * Ch); T.SetNumUninitialized(Cw * Ch);
	const FHKData& D = FHKData::Get();
	// World directions of the local axes (web frame x/z): +x -> (CT, -ST), +s -> (ST, CT).
	const FVector Ex = HK::ToUE(FVector(D.CT, 0, -D.ST)).GetSafeNormal(), Es = HK::ToUE(FVector(D.ST, 0, D.CT)).GetSafeNormal();
	for (int32 j = J0; j <= J1; j++)
		for (int32 i = I0; i <= I1; i++)
		{
			const int32 k = j * F.NX + i, o = (j - J0) * Cw + (i - I0);
			const FVector2D W = F.NodeWorld(i, j);
			V[o] = HK::ToUE(FVector(W.X, F.H[k], W.Y));
			const float Hx0 = F.H[j * F.NX + FMath::Max(0, i - 1)], Hx1 = F.H[j * F.NX + FMath::Min(F.NX - 1, i + 1)];
			const float Hs0 = F.H[FMath::Max(0, j - 1) * F.NX + i], Hs1 = F.H[FMath::Min(F.NS - 1, j + 1) * F.NX + i];
			const double Gx = (Hx1 - Hx0) / (2 * F.Cell), Gs = (Hs1 - Hs0) / (2 * F.Cell);
			N[o] = (FVector(0, 0, 1) - Ex * Gx - Es * Gs).GetSafeNormal();
			UV[o] = FVector2D((F.X0 + i * F.Cell) / 2.5, (F.S0 + j * F.Cell) / 2.5);
			T[o] = FProcMeshTangent((Ex + FVector(0, 0, Gx)).GetSafeNormal(), false);
			const double Loose = FMath::Clamp((F.H[k] - F.Firm[k]) / .3, 0.0, 1.0), Deep = FMath::Clamp((F.Base[k] - F.H[k] - .3) / 2.0, 0.0, 1.0);
			C[o] = FColor((uint8)(Loose * 255), (uint8)(Deep * 255), 0, 255);
		}
	for (int32 j = 0; j < Ch - 1; j++)
		for (int32 i = 0; i < Cw - 1; i++)
		{
			const int32 a = j * Cw + i, b = a + 1, c = a + Cw, d = c + 1;
			// Local axes (x, s) map to a left-handed pair in Unreal: wind so the faces point up.
			Tri.Append({a, b, c, b, d, c});
		}
	// Make sure the winding faces up (the local frame may be mirrored in Unreal's axes). Unreal's front faces have
	// (b - a) x (c - a) pointing away from the viewer (left-handed axes).
	if (Tri.Num() >= 3 && FVector::CrossProduct(V[Tri[1]] - V[Tri[0]], V[Tri[2]] - V[Tri[0]]).Z > 0)
		for (int32 t = 0; t < Tri.Num(); t += 3) Swap(Tri[t + 1], Tri[t + 2]);
	const int32 Section = TJ * F.TilesX() + TI;
	if (bCreate)
	{
		Mesh->CreateMeshSection(Section, V, Tri, N, UV, C, T, false);
		if (Material) Mesh->SetMaterial(Section, Material);
	}
	else Mesh->UpdateMeshSection(Section, V, N, UV, C, T);
}

void AHKSiteActor::BuildSkirt()
{
	const FHKDirtField& F = *Field;
	TArray<FVector> V; TArray<FVector> N; TArray<FVector2D> UV; TArray<FColor> C; TArray<FProcMeshTangent> T; TArray<int32> Tri;
	auto Edge = [&](int32 Ia, int32 Ja, int32 Di, int32 Dj, int32 Count, const FVector2D& Out)
	{
		const FVector Nn = HK::ToUE(FVector(Out.X, 0, Out.Y)).GetSafeNormal();
		const int32 Start = V.Num();
		for (int32 k = 0; k <= Count; k++)
		{
			const int32 i = Ia + Di * k, j = Ja + Dj * k;
			const FVector2D W = F.NodeWorld(i, j);
			const float Y = F.H[j * F.NX + i];
			V.Add(HK::ToUE(FVector(W.X, Y, W.Y))); V.Add(HK::ToUE(FVector(W.X, Y - 1.6, W.Y)));
			for (int32 q = 0; q < 2; q++) { N.Add(Nn); UV.Add(FVector2D(k * F.Cell / 2.5, q * .6)); C.Add(FColor(0, 255, 0, 255)); T.Add(FProcMeshTangent()); }
		}
		for (int32 k = 0; k < Count; k++)
		{
			const int32 a = Start + k * 2, b = a + 2;
			const bool bFlip = FVector::DotProduct(FVector::CrossProduct(V[b] - V[a], V[a + 1] - V[a]), Nn) > 0;
			if (bFlip) Tri.Append({a, a + 1, b, b, a + 1, b + 1}); else Tri.Append({a, b, a + 1, b, b + 1, a + 1});
		}
	};
	const FHKData& D = FHKData::Get();
	const FVector2D Ex(D.CT, -D.ST), Es(D.ST, D.CT);
	Edge(0, 0, 1, 0, F.NX - 1, -Es);
	Edge(0, F.NS - 1, 1, 0, F.NX - 1, Es);
	Edge(0, 0, 0, 1, F.NS - 1, -Ex);
	Edge(F.NX - 1, 0, 0, 1, F.NS - 1, Ex);
	const int32 Section = F.TilesX() * F.TilesS();
	Mesh->CreateMeshSection(Section, V, Tri, N, UV, C, T, false);
	if (Material) Mesh->SetMaterial(Section, Material);
}

void AHKSiteActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Field || Field->DirtyTiles.Num() == 0) return;
	// A few tiles a frame: digging touches one or two; the slumping after a dump may touch more over a few frames.
	int32 Budget = 6;
	TArray<int32> Done;
	for (const int32 t : Field->DirtyTiles)
	{
		if (Budget-- <= 0) break;
		BuildTile(t % Field->TilesX(), t / Field->TilesX(), false);
		Done.Add(t);
	}
	for (const int32 t : Done) Field->DirtyTiles.Remove(t);
}
