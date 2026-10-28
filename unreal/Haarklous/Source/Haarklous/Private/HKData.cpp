#include "HKData.h"
#include "HKMath.h"
#include "Haarklous.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace HKJson
{
	static const TArray<TSharedPtr<FJsonValue>> EmptyArray;

	double Num(const TSharedPtr<FJsonObject>& O, const FString& K, double Default)
	{
		double V;
		return O.IsValid() && O->TryGetNumberField(K, V) ? V : Default;
	}
	FString Str(const TSharedPtr<FJsonObject>& O, const FString& K, const FString& Default)
	{
		FString V;
		return O.IsValid() && O->TryGetStringField(K, V) ? V : Default;
	}
	bool Bool(const TSharedPtr<FJsonObject>& O, const FString& K, bool Default)
	{
		bool V;
		return O.IsValid() && O->TryGetBoolField(K, V) ? V : Default;
	}
	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const FString& K)
	{
		const TSharedPtr<FJsonObject>* V;
		return O.IsValid() && O->TryGetObjectField(K, V) ? *V : TSharedPtr<FJsonObject>();
	}
	const TArray<TSharedPtr<FJsonValue>>& Arr(const TSharedPtr<FJsonObject>& O, const FString& K)
	{
		const TArray<TSharedPtr<FJsonValue>>* V;
		return O.IsValid() && O->TryGetArrayField(K, V) ? *V : EmptyArray;
	}
	TArray<double> Nums(const TSharedPtr<FJsonValue>& V)
	{
		TArray<double> R;
		if (!V.IsValid() || V->Type != EJson::Array) return R;
		for (const TSharedPtr<FJsonValue>& E : V->AsArray()) R.Add(E->Type == EJson::Number ? E->AsNumber() : 0.0);
		return R;
	}
	TArray<double> Nums(const TSharedPtr<FJsonObject>& O, const FString& K)
	{
		TArray<double> R;
		for (const TSharedPtr<FJsonValue>& E : Arr(O, K)) R.Add(E->Type == EJson::Number ? E->AsNumber() : 0.0);
		return R;
	}
	FVector Vec(const TSharedPtr<FJsonObject>& O, const FString& K, const FVector& Default)
	{
		const TArray<double> A = Nums(O, K);
		return A.Num() >= 3 ? FVector(A[0], A[1], A[2]) : Default;
	}
	TArray<FHKPt> Pts(const TArray<TSharedPtr<FJsonValue>>& A)
	{
		TArray<FHKPt> R;
		R.Reserve(A.Num());
		for (const TSharedPtr<FJsonValue>& E : A)
		{
			const TArray<TSharedPtr<FJsonValue>>& P = E->AsArray();
			R.Add(FHKPt(P[0]->AsNumber(), P[1]->AsNumber()));
		}
		return R;
	}
	TSharedPtr<FJsonObject> LoadFile(const FString& Path, FString* Error)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path)) { if (Error) *Error = TEXT("Missing ") + Path; return nullptr; }
		TSharedPtr<FJsonObject> J;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) || !J.IsValid()) { if (Error) *Error = TEXT("Bad JSON in ") + Path; return nullptr; }
		return J;
	}
}

FString FHKData::DataDir()
{
	return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data"));
}

FHKData& FHKData::Get()
{
	static FHKData Instance;
	if (!Instance.bTried)
	{
		Instance.bTried = true;
		const double T0 = FPlatformTime::Seconds();
		Instance.bLoaded = Instance.Load();
		if (Instance.bLoaded)
		{
			UE_LOG(LogHaarklous, Log, TEXT("World data loaded in %.2f s: %d floors, %d obstacles, %d boundary pieces"), FPlatformTime::Seconds() - T0, Instance.WalkWorld.Floors.Num(), Instance.WalkWorld.Obstacles.Num(), Instance.WalkWorld.Boundary.Num());
		}
		else
		{
			UE_LOG(LogHaarklous, Error, TEXT("World data could not load: %s"), *Instance.Error);
		}
	}
	return Instance;
}

TArray<FHKFloor> FHKData::FloorsFromJson(const TArray<TSharedPtr<FJsonValue>>& A)
{
	TArray<FHKFloor> R;
	for (const TSharedPtr<FJsonValue>& V : A)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FHKFloor F;
		F.Name = HKJson::Str(O, TEXT("name"));
		F.SurfaceKind = HKJson::Str(O, TEXT("surfaceKind"));
		F.Points = HKJson::Pts(HKJson::Arr(O, TEXT("points")));
		F.Y = HKJson::Num(O, TEXT("y"));
		F.CeilingHeight = HKJson::Inf(HKJson::Num(O, TEXT("ceilingHeight"), 1e9));
		const TSharedPtr<FJsonObject> S = HKJson::Obj(O, TEXT("slope"));
		if (S.IsValid())
		{
			F.bSlope = true;
			F.SlopeX = HKJson::Num(S, TEXT("x"));
			F.SlopeZ = HKJson::Num(S, TEXT("z"));
			const TArray<double> Or = HKJson::Nums(S, TEXT("origin"));
			if (Or.Num() >= 2) F.SlopeOrigin = FHKPt(Or[0], Or[1]);
		}
		if (HKJson::Bool(O, TEXT("terrain"))) F.Height = [](const FHKPt& P) { return FHKData::Get().GroundWorld(P.X, P.Z); };
		R.Add(MoveTemp(F));
	}
	return R;
}

TArray<FHKObstacle> FHKData::ObstaclesFromJson(const TArray<TSharedPtr<FJsonValue>>& A)
{
	TArray<FHKObstacle> R;
	R.Reserve(A.Num());
	for (const TSharedPtr<FJsonValue>& V : A)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FHKObstacle Ob;
		Ob.Points = HKJson::Pts(HKJson::Arr(O, TEXT("points")));
		Ob.MinY = HKJson::Inf(HKJson::Num(O, TEXT("minY"), -1e9));
		Ob.MaxY = HKJson::Inf(HKJson::Num(O, TEXT("maxY"), 1e9));
		Ob.Kind = HKJson::Str(O, TEXT("kind"));
		Ob.Prepare();
		R.Add(MoveTemp(Ob));
	}
	return R;
}

bool FHKData::Load()
{
	const FString Dir = DataDir();
	Game = HKJson::LoadFile(Dir / TEXT("game.json"), &Error);
	if (!Game) return false;
	D = HKJson::LoadFile(Dir / TEXT("surroundings.json"), &Error);
	if (!D) return false;
	Apartment = HKJson::LoadFile(Dir / TEXT("apartment.json"), &Error);
	if (!Apartment) return false;

	const TSharedPtr<FJsonObject> Geo = HKJson::Obj(D, TEXT("geo"));
	Theta = HKJson::Num(Geo, TEXT("theta"));
	CT = FMath::Cos(Theta);
	ST = FMath::Sin(Theta);
	GeoX = HKJson::Num(Geo, TEXT("x"));
	GeoZ = HKJson::Num(Geo, TEXT("z"));
	Ground = HKJson::Num(D, TEXT("ground"));
	const TSharedPtr<FJsonObject> Meta = HKJson::Obj(Game, TEXT("meta"));
	PlayHalf = HKJson::Num(Meta, TEXT("playHalf"), 540);
	SunDir = HKJson::Vec(Meta, TEXT("sunDir"), FVector(0, 1, 0));

	// Terrain: relative heights in decimetres (Int16, little endian), 4 m grid, local frame.
	const TSharedPtr<FJsonObject> Ter = HKJson::Obj(D, TEXT("terrain"));
	TerN = (int32)HKJson::Num(Ter, TEXT("n"));
	TerHalf = HKJson::Num(Ter, TEXT("half"));
	TerStep = HKJson::Num(Ter, TEXT("step"));
	TArray<uint8> Bytes;
	if (!FBase64::Decode(HKJson::Str(Ter, TEXT("dm")), Bytes) || Bytes.Num() < TerN * TerN * 2) { Error = TEXT("Bad terrain grid"); return false; }
	Grid.SetNumUninitialized(TerN * TerN);
	for (int32 i = 0; i < TerN * TerN; i++)
	{
		const int16 Raw = (int16)(Bytes[i * 2] | (Bytes[i * 2 + 1] << 8));
		Grid[i] = (float)(Raw / 10.0);   // Float32Array, like the web code
	}

	if (!FFileHelper::LoadFileToArray(SurfaceClasses, *(Dir / TEXT("surface-classes.bin"))) || SurfaceClasses.Num() != 1024 * 1024) { Error = TEXT("Bad surface-classes.bin"); return false; }

	// Trees (data.trees + courtyard trees, in that order) and bushes, local frame.
	{
		const TArray<double> T = HKJson::Nums(D, TEXT("trees"));
		for (int32 i = 0; i + 2 < T.Num(); i += 3) AllTrees.Add({T[i], T[i + 1], T[i + 2]});
		const TArray<double> Y = HKJson::Nums(HKJson::Obj(D, TEXT("courtyard")), TEXT("trees"));
		for (int32 i = 0; i + 2 < Y.Num(); i += 3) AllTrees.Add({Y[i], Y[i + 1], Y[i + 2]});
		const TArray<double> B = HKJson::Nums(D, TEXT("bushes"));
		for (int32 i = 0; i + 2 < B.Num(); i += 3) Bushes.Add({B[i], B[i + 1], B[i + 2]});
	}
	// Traffic noise sources: residential .35 ... trunk 2 (relative loudness widens the distance).
	{
		const TMap<FString, double> Weights = {{TEXT("residential"), .35}, {TEXT("tertiary"), .7}, {TEXT("secondary"), 1}, {TEXT("primary"), 1.4}, {TEXT("trunk"), 2}, {TEXT("trunk_link"), 1.2}};
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(D, TEXT("roads")))
		{
			const TSharedPtr<FJsonObject> R = V->AsObject();
			const double* W = Weights.Find(HKJson::Str(R, TEXT("k")));
			if (!W) continue;
			const TArray<double> P = HKJson::Nums(R, TEXT("p"));
			for (int32 i = 2; i + 1 < P.Num(); i += 2) Traffic.Add({FVector2D(P[i - 2], P[i - 1]), FVector2D(P[i], P[i + 1]), *W});
		}
	}

	// Collision: the same floors and obstacles as the web game's walkWorld (radius .18, clearance .08).
	TArray<FHKFloor> Floors = FloorsFromJson(HKJson::Arr(Game, TEXT("floors")));
	TArray<FHKObstacle> Obstacles = ObstaclesFromJson(HKJson::Arr(Game, TEXT("obstacles")));
	const TSharedPtr<FJsonObject> Counts = HKJson::Obj(Game, TEXT("counts"));
	const int32 NApt = (int32)HKJson::Num(Counts, TEXT("aptObstacles")), NTow = (int32)HKJson::Num(Counts, TEXT("towerObstacles")), NWorld = (int32)HKJson::Num(Counts, TEXT("worldObstacles"));
	// Vehicles: [surroundings, towers, firepit] (worldFor in game-main.js), no furniture.
	for (int32 i = NApt + NTow; i < NApt + NTow + NWorld && i < Obstacles.Num(); i++) VehicleObstacles.Add(Obstacles[i]);
	for (int32 i = NApt; i < NApt + NTow && i < Obstacles.Num(); i++) VehicleObstacles.Add(Obstacles[i]);
	for (int32 i = NApt + NTow + NWorld; i < Obstacles.Num(); i++) VehicleObstacles.Add(Obstacles[i]);
	WalkWorld.Create(MoveTemp(Floors), MoveTemp(Obstacles), HKJson::Num(Meta, TEXT("radius"), .18), HKJson::Num(Meta, TEXT("clearance"), .08));
	return true;
}

FHKFloor FHKData::TerrainFloor() const
{
	FHKFloor F;
	F.Name = TEXT("Neighbourhood");
	F.SurfaceKind = TEXT("terrain");
	for (const FVector2D& C : {FVector2D(-PlayHalf, -PlayHalf), FVector2D(PlayHalf, -PlayHalf), FVector2D(PlayHalf, PlayHalf), FVector2D(-PlayHalf, PlayHalf)})
	{
		const FVector2D W = ToWorld(C.X, C.Y);
		F.Points.Add(FHKPt(W.X, W.Y));
	}
	F.Height = [](const FHKPt& P) { return FHKData::Get().GroundWorld(P.X, P.Z); };
	return F;
}

FHKWalkWorld& FHKData::VehicleWorld(double Radius)
{
	const FString Key = FString::Printf(TEXT("%.2f"), Radius);
	if (TUniquePtr<FHKWalkWorld>* Found = VehicleWorlds.Find(Key)) return **Found;
	TUniquePtr<FHKWalkWorld> W = MakeUnique<FHKWalkWorld>();
	W->Create({TerrainFloor()}, VehicleObstacles, Radius, .08);
	FHKWalkWorld& Ref = *W;
	VehicleWorlds.Add(Key, MoveTemp(W));
	return Ref;
}

double FHKData::RelLocal(double X, double S) const
{
	const int32 N = TerN;
	const double Fi = FMath::Min(N - 1.001, FMath::Max(0.0, (X + TerHalf) / TerStep)), Fj = FMath::Min(N - 1.001, FMath::Max(0.0, (S + TerHalf) / TerStep));
	const int32 i = FMath::FloorToInt32(Fi), j = FMath::FloorToInt32(Fj);
	const double u = Fi - i, v = Fj - j;
	return Grid[j * N + i] * (1 - u) * (1 - v) + Grid[j * N + i + 1] * u * (1 - v) + Grid[(j + 1) * N + i] * (1 - u) * v + Grid[(j + 1) * N + i + 1] * u * v;
}

double FHKData::VisualHeightWorld(double X, double Z) const
{
	const FVector2D L = ToLocal(X, Z);
	const bool bNear = FMath::Max(FMath::Abs(L.X), FMath::Abs(L.Y)) < 300;
	const int32 K = FMath::Max(1, FMath::RoundToInt32((bNear ? 4 : 8) / TerStep)), N = TerN;
	const double Fi = FMath::Min(N - 1.0 - K, FMath::Max(0.0, (L.X + TerHalf) / TerStep)) / K, Fj = FMath::Min(N - 1.0 - K, FMath::Max(0.0, (L.Y + TerHalf) / TerStep)) / K;
	const int32 i = FMath::FloorToInt32(Fi), j = FMath::FloorToInt32(Fj);
	const double u = Fi - i, v = Fj - j;
	const double Ga = Grid[j * K * N + i * K], Gb = Grid[j * K * N + (i + 1) * K], Gc = Grid[(j + 1) * K * N + i * K], Gd = Grid[(j + 1) * K * N + (i + 1) * K];
	// surroundings.js terrainMesh: triangles (a, c, b) and (b, c, d).
	const double H = u + v <= 1 ? Ga + u * (Gb - Ga) + v * (Gc - Ga) : Gd + (1 - u) * (Gc - Gd) + (1 - v) * (Gb - Gd);
	return Ground + H - (bNear ? 0 : .5);
}

FString FHKData::SurfaceAt(double X, double Z) const
{
	static const TCHAR* Names[] = {TEXT("grass"), TEXT("asphalt"), TEXT("gravel"), TEXT("sand"), TEXT("none")};
	const FVector2D L = ToLocal(X, Z);
	const int32 i = FMath::FloorToInt32((L.X + PlayHalf) / (2 * PlayHalf) * 1024), j = FMath::FloorToInt32((L.Y + PlayHalf) / (2 * PlayHalf) * 1024);
	if (i < 0 || j < 0 || i > 1023 || j > 1023) return Names[0];
	const uint8 C = SurfaceClasses[j * 1024 + i];
	return Names[C < 5 ? C : 0];
}

double FHKData::TrafficDistance(double X, double Z) const
{
	const FVector2D L = ToLocal(X, Z);
	double Best = 999;
	for (const FSeg& S : Traffic)
	{
		const double dx = S.B.X - S.A.X, dz = S.B.Y - S.A.Y, L2 = dx * dx + dz * dz;
		const double Div = L2 ? L2 : 1;
		const double T = FMath::Max(0.0, FMath::Min(1.0, ((L.X - S.A.X) * dx + (L.Y - S.A.Y) * dz) / Div));
		const double Dist = HK::Hypot(L.X - S.A.X - T * dx, L.Y - S.A.Y - T * dz) / S.W;
		if (Dist < Best) Best = Dist;
	}
	return Best;
}

double FHKData::Greenery(double X, double Z) const
{
	const FVector2D L = ToLocal(X, Z);
	double N = 0;
	for (const FTree& T : AllTrees) if (FMath::Abs(T.X - L.X) < 40 && FMath::Abs(T.S - L.Y) < 40) N++;
	for (const FBush& B : Bushes) if (FMath::Abs(B.X - L.X) < 25 && FMath::Abs(B.S - L.Y) < 25) N += .3;
	return FMath::Min(1.0, N / 18);
}
