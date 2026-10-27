// Port of the web game's case-game.js (the hostage-rescue mission), line by line. See HKMission.h.
#include "HKMission.h"
#include "HKAudio.h"
#include "HKData.h"
#include "HKInput.h"
#include "HKUi.h"
#include "Haarklous.h"
#include "CanvasItem.h"
#include "GlobalRenderResources.h"
#include "Engine/Canvas.h"
#include "Misc/Paths.h"
#include <cmath>

// The mission must decide exactly like the web code (random rolls against hit chances, spawn checks), also in builds that
// use fast floating-point maths: keep this file's arithmetic strict.
#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(precise, on, push)
#endif

// ======================================================================================================================
// JavaScript number helpers
// ======================================================================================================================
namespace
{
	// three.js Vector3 arithmetic, in the same order of operations.
	inline double MxLen(const FVector& V) { return std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z); }
	inline FVector MxNorm(const FVector& V)
	{
		double L = MxLen(V);
		if (L == 0 || L != L) L = 1;
		const double S = 1 / L;   // divideScalar(s) = multiplyScalar(1 / s)
		return FVector(V.X * S, V.Y * S, V.Z * S);
	}
	inline double MxDist(const FVector& A, const FVector& B)
	{
		const double Dx = A.X - B.X, Dy = A.Y - B.Y, Dz = A.Z - B.Z;
		return std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
	}
	inline FVector MxCross(const FVector& A, const FVector& B) { return FVector(A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X); }
	inline double MxDot(const FVector& A, const FVector& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
	inline FVector MxAddScaled(const FVector& A, const FVector& B, double S) { return FVector(A.X + B.X * S, A.Y + B.Y * S, A.Z + B.Z * S); }
	inline double MxMin(double A, double B) { return A < B ? A : B; }
	inline double MxMax(double A, double B) { return A > B ? A : B; }

	const FString MxPlay(TEXT("play")), MxBriefing(TEXT("briefing")), MxPaused(TEXT("paused")), MxDown(TEXT("down")), MxSuccess(TEXT("success")), MxFail(TEXT("fail")), MxOff(TEXT("off"));
	const FString MxHostile(TEXT("hostile")), MxHostage(TEXT("hostage"));

	// CSS colour (sRGB hex) as a linear colour, like FHKUi (FLinearColor(FColor(...))).
	FLinearColor MxCss(uint32 Rgb, float A = 1) { FLinearColor C(FColor((uint8)((Rgb >> 16) & 255), (uint8)((Rgb >> 8) & 255), (uint8)(Rgb & 255))); C.A = A; return C; }
	FLinearColor MxAlpha(FLinearColor C, float A) { C.A *= A; return C; }
}

double HKCase::JsHypot(double A, double B)
{
	// V8's Math.hypot: largest magnitude first, then a Kahan-compensated sum of the normalised squares.
	const double Abs[2] = {std::fabs(A), std::fabs(B)};
	const double Max = MxMax(Abs[0], Abs[1]);
	if (Max == std::numeric_limits<double>::infinity()) return Max;
	if (A != A || B != B) return std::numeric_limits<double>::quiet_NaN();
	if (Max == 0) return 0;
	double Sum = 0, Compensation = 0;
	for (int32 i = 0; i < 2; i++)
	{
		const double N = Abs[i] / Max, Summand = N * N - Compensation, Preliminary = Sum + Summand;
		Compensation = (Preliminary - Sum) - Summand;
		Sum = Preliminary;
	}
	return std::sqrt(Sum) * Max;
}

double HKCase::JsHypot(double A, double B, double C)
{
	const double Abs[3] = {std::fabs(A), std::fabs(B), std::fabs(C)};
	const double Max = MxMax(MxMax(Abs[0], Abs[1]), Abs[2]);
	if (Max == std::numeric_limits<double>::infinity()) return Max;
	if (A != A || B != B || C != C) return std::numeric_limits<double>::quiet_NaN();
	if (Max == 0) return 0;
	double Sum = 0, Compensation = 0;
	for (int32 i = 0; i < 3; i++)
	{
		const double N = Abs[i] / Max, Summand = N * N - Compensation, Preliminary = Sum + Summand;
		Compensation = (Preliminary - Sum) - Summand;
		Sum = Preliminary;
	}
	return std::sqrt(Sum) * Max;
}

FString HKCase::JsToFixed(double V, int32 Digits)
{
	// Number.prototype.toFixed: the integer n for which n / 10^d - |v| is closest to zero, the larger n on a tie, taken
	// from the exact binary value (so 0.25.toFixed(1) is "0.3", as in the browser).
	Digits = FMath::Clamp(Digits, 0, 3);
	if (V != V) return TEXT("NaN");
	const bool bNeg = V < 0;
	const double X = bNeg ? -V : V;
	if (X >= 1e15) return FString::Printf(TEXT("%s%.0f"), bNeg ? TEXT("-") : TEXT(""), X);
	uint64 Pow10 = 1;
	for (int32 i = 0; i < Digits; i++) Pow10 *= 10;
	int Exp = 0;
	const double Mant = std::frexp(X, &Exp);                  // X = Mant * 2^Exp, Mant in [0.5, 1)
	const uint64 M = (uint64)std::ldexp(Mant, 53);            // X = M * 2^(Exp - 53), exactly
	const int32 E = Exp - 53;
	const uint64 P = M * Pow10;                               // < 2^63
	uint64 N = 0;
	if (X == 0) N = 0;
	else if (E >= 0) N = P << E;                              // X < 1e15 keeps this in range
	else
	{
		const int32 K = -E;
		if (K >= 64) N = 0;                                   // P < 2^63 <= half
		else
		{
			N = P >> K;
			const uint64 R = P & ((uint64(1) << K) - 1), Half = uint64(1) << (K - 1);
			if (R >= Half) N++;
		}
	}
	FString Out = FString::Printf(TEXT("%s%llu"), bNeg ? TEXT("-") : TEXT(""), (unsigned long long)(N / Pow10));
	if (Digits > 0)
	{
		FString Frac;
		uint64 F = N % Pow10;
		for (int32 i = 0; i < Digits; i++) { Frac = FString::Chr(TCHAR('0' + (int32)(F % 10))) + Frac; F /= 10; }
		Out += TEXT(".") + Frac;
	}
	return Out;
}

FString HKCase::Clock(double T)
{
	// `${String(Math.floor(t / 60)).padStart(2, '0')}:${(t % 60).toFixed(1).padStart(4, '0')}`
	FString Min = FString::Printf(TEXT("%lld"), (long long)std::floor(T / 60));
	while (Min.Len() < 2) Min = TEXT("0") + Min;
	FString Sec = JsToFixed(std::fmod(T, 60.0), 1);
	while (Sec.Len() < 4) Sec = TEXT("0") + Sec;
	return Min + TEXT(":") + Sec;
}

// ======================================================================================================================
// Pure helpers (the web module's exports)
// ======================================================================================================================
double HKCase::FRng::Next()
{
	// mulberry32: a = (a + 0x6D2B79F5) | 0; t = imul(a ^ a >>> 15, 1 | a); t = (t + imul(t ^ t >>> 7, 61 | t)) ^ t;
	// return ((t ^ t >>> 14) >>> 0) / 4294967296
	A = A + 0x6D2B79F5u;
	uint32 T = (A ^ (A >> 15)) * (1u | A);
	T = (T + ((T ^ (T >> 7)) * (61u | T))) ^ T;
	return (double)(T ^ (T >> 14)) / 4294967296.0;
}

bool HKCase::PlanCase(FRng& Rng, FSampler Sampler, FPlan& Out, FString* Error, const FPlanOptions& Opt)
{
	Out = FPlan();
	const int32 HostileCount = Opt.HostilesMin + (int32)std::floor(Rng() * (Opt.HostilesMax - Opt.HostilesMin + 1));
	const int32 InStair = FMath::Max(1, FMath::Min(HostileCount - 1, 1 + (int32)std::floor(Rng() * 2)));
	TArray<TPair<FString, FString>> Roles;
	Roles.Add(TPair<FString, FString>(MxHostage, TEXT("apartment")));
	for (int32 i = 0; i < HostileCount; i++) Roles.Add(TPair<FString, FString>(MxHostile, i < InStair ? TEXT("stair") : TEXT("apartment")));
	for (const TPair<FString, FString>& R : Roles)
	{
		bool bFound = false;
		FSpot Spot;
		for (int32 i = 0; i < Opt.MaxTries && !bFound; i++)
		{
			FSpot S;
			if (!Sampler(Rng, R.Value, S)) continue;
			bool bFree = true;
			for (const FPlanActor& A : Out.Actors)
				if (!(JsHypot(A.X - S.X, A.Z - S.Z) >= Opt.MinSeparation || std::fabs(A.Y - S.Y) > 1.5)) { bFree = false; break; }
			if (bFree) { Spot = S; bFound = true; }
		}
		if (!bFound)
		{
			if (Error) *Error = FString::Printf(TEXT("No free position for %s"), *R.Key);
			return false;
		}
		FPlanActor A;
		A.Id = FString::Printf(TEXT("%s-%d"), *R.Key, Out.Actors.Num());
		A.Role = R.Key;
		A.Zone = R.Value;
		A.X = Spot.X;
		A.Z = Spot.Z;
		A.Y = Spot.Y;
		A.Area = Spot.Area;
		A.Facing = Rng() * HK::Pi * 2;
		A.Health = R.Key == MxHostile ? HOSTILE_HEALTH : 1;
		A.bAlive = true;
		Out.Actors.Add(A);
	}
	Out.HostileCount = HostileCount;
	Out.bHasHostage = true;
	return true;
}

void HKCase::ApplyHit(int32& Health, bool& bAlive, bool bHead)
{
	if (!bAlive) return;
	Health = FMath::Max(0, Health - (bHead ? Health : 1));
	bAlive = Health > 0;
}

double HKCase::HostileHitChance(double Distance, double PlayerSpeed, const FString& Stance)
{
	// About 75% close up, falling with distance; moving (especially running) makes the player harder to hit.
	// Crouching and lying prone present a smaller target.
	const double Base = .75 - .05 * MxMax(0, Distance - 1.5), Moving = MxMin(1, MxMax(0, PlayerSpeed) / 3.2) * .3;
	const double Penalty = Stance == TEXT("crouch") ? .12 : Stance == TEXT("prone") ? .22 : 0;
	return MxMax(Stance == TEXT("stand") ? .12 : .08, MxMin(.85, Base - Moving - Penalty));
}

void HKCase::DamagePlayer(int32& Health, bool& bAlive, int32 Amount)
{
	if (!bAlive) return;
	Health = FMath::Max(0, Health - Amount);
	bAlive = Health > 0;
}

FString HKCase::Outcome(const TArray<FRoleAlive>& Actors, const bool* bPlayerAlive)
{
	for (const FRoleAlive& A : Actors) if (A.Role == MxHostage && !A.bAlive) return MxFail;
	if (bPlayerAlive && !*bPlayerAlive) return MxFail;
	for (const FRoleAlive& A : Actors) if (A.Role == MxHostile && A.bAlive) return FString();
	return MxSuccess;
}

bool HKCase::InsidePolygon(double X, double Z, const TArray<FHKPt>& Points)
{
	bool bIn = false;
	const int32 N = Points.Num();
	for (int32 i = 0, j = N - 1; i < N; j = i++)
	{
		const FHKPt& A = Points[i];
		const FHKPt& B = Points[j];
		if ((A.Z > Z) != (B.Z > Z) && X < (B.X - A.X) * (Z - A.Z) / (B.Z - A.Z) + A.X) bIn = !bIn;
	}
	return bIn;
}

double HKCase::PolygonArea(const TArray<FHKPt>& Points)
{
	double S = 0;
	const int32 N = Points.Num();
	for (int32 i = 0; i < N; i++)
	{
		const FHKPt& P = Points[i];
		const FHKPt& Q = Points[(i + 1) % N];
		S = S + P.X * Q.Z - Q.X * P.Z;
	}
	return std::fabs(S) / 2;
}

// ======================================================================================================================
// Fixed data from game.json
// ======================================================================================================================
TArray<FHKSpawnArea> HKMissionData::SpawnAreas()
{
	TArray<FHKSpawnArea> R;
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(HKJson::Obj(FHKData::Get().Game, TEXT("apartment")), TEXT("spawnAreas")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FHKSpawnArea A;
		A.Name = HKJson::Str(O, TEXT("name"));
		A.Points = HKJson::Pts(HKJson::Arr(O, TEXT("points")));
		A.CeilingHeight = HKJson::Inf(HKJson::Num(O, TEXT("ceilingHeight"), 1e9));
		R.Add(A);
	}
	return R;
}

TArray<FHKStairSpawn> HKMissionData::StairSpawns()
{
	TArray<FHKStairSpawn> R;
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(HKJson::Obj(FHKData::Get().Game, TEXT("stair")), TEXT("ownFloors")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		const FString Name = HKJson::Str(O, TEXT("name"));
		if (HKJson::Obj(O, TEXT("slope")).IsValid() || Name == TEXT("Street door threshold") || Name.Contains(TEXT("Half landing 0"))) continue;
		FHKStairSpawn S;
		S.Name = Name;
		S.Points = HKJson::Pts(HKJson::Arr(O, TEXT("points")));
		S.Y = HKJson::Num(O, TEXT("y"));
		R.Add(S);
	}
	return R;
}

FVector HKMissionData::Entrance()
{
	return HKJson::Vec(HKJson::Obj(FHKData::Get().Game, TEXT("apartment")), TEXT("entrance"), FVector(1.66, 0, 5.83));
}

TArray<FHKMissionDoor> HKMissionData::Doors()
{
	TArray<FHKMissionDoor> R;
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(FHKData::Get().Game, TEXT("doors")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!HKJson::Bool(O, TEXT("inApartment"))) continue;
		FHKMissionDoor D;
		D.Name = HKJson::Str(O, TEXT("name"));
		D.Motion = HKJson::Str(O, TEXT("motion"), TEXT("swing"));
		D.OpenAngle = HKJson::Num(O, TEXT("openAngle"));
		D.ClosedX = HKJson::Num(O, TEXT("closedX"));
		D.Travel = HKJson::Num(O, TEXT("travel"));
		D.LeafWidth = HKJson::Vec(O, TEXT("leafBox")).X;
		D.ParentWorld = FHKMat::FromArray(HKJson::Nums(O, TEXT("parentWorld")));
		D.HingePos = HKJson::Vec(O, TEXT("hingePos"));
		const TArray<double> Q = HKJson::Nums(O, TEXT("hingeQuat"));
		if (Q.Num() == 4) D.HingeQuat = FQuat(Q[0], Q[1], Q[2], Q[3]);
		D.HingeScale = HKJson::Vec(O, TEXT("hingeScale"), FVector::OneVector);
		D.LeafLocal = FHKMat::FromArray(HKJson::Nums(O, TEXT("leafLocal")));
		R.Add(D);
	}
	return R;
}

FHKMissionStart HKMissionData::MissionStart()
{
	FHKData& Data = FHKData::Get();
	const TArray<double> L = HKJson::Nums(HKJson::Obj(Data.Game, TEXT("mission")), TEXT("start"));
	const TSharedPtr<FJsonObject> Door = HKJson::Obj(HKJson::Obj(Data.Game, TEXT("stair")), TEXT("doorOutside"));
	FHKMissionStart S;
	if (L.Num() < 2 || !Door.IsValid()) return S;
	const FVector2D P = Data.ToWorld(L[0], L[1]);
	S.X = P.X;
	S.Z = P.Y;
	S.Y = Data.HeightWorld(P.X, P.Y);
	S.Yaw = std::atan2(-(HKJson::Num(Door, TEXT("x")) - P.X), -(HKJson::Num(Door, TEXT("z")) - P.Y));
	return S;
}

// ======================================================================================================================
// Spawn sampling inside the apartment, balcony and stairwell (makeSampler())
// ======================================================================================================================
class FHKMission::FSampler
{
public:
	struct FArea { FString Name; TArray<FHKPt> Points; double CeilingHeight = 0, Area = 0; };
	struct FSeg { FHKPt A, B; };
	TArray<FArea> Areas;
	double Total = 0;
	FHKWalkWorld SpawnWorld;
	FHKPt Entrance;
	TArray<FSeg> KeepOut;
	TArray<FHKStairSpawn> StairZones;
	FHKWalkWorld* Walk = nullptr;

	explicit FSampler(const FHKMissionContext& Ctx)
	{
		// Square-root weighting spreads actors over small rooms and the balcony too, not mostly the living room.
		if (Ctx.SpawnAreas)
			for (const FHKSpawnArea& A : Ctx.SpawnAreas()) Areas.Add({A.Name, A.Points, A.CeilingHeight, MxMax(1.2, std::sqrt(HKCase::PolygonArea(A.Points)))});
		for (const FArea& A : Areas) Total = Total + A.Area;
		Walk = Ctx.WalkWorld ? Ctx.WalkWorld() : nullptr;
		TArray<FHKFloor> Floors;
		for (const FArea& A : Areas) { FHKFloor F; F.Points = A.Points; F.CeilingHeight = A.CeilingHeight; Floors.Add(F); }
		SpawnWorld.Create(Floors, Walk ? Walk->Obstacles : TArray<FHKObstacle>(), .24, .08);
		// Keep actors out of each interior door leaf's sweep, so those doors still open around them. The balcony door
		// swings over the whole 0.78 m balcony: there only its closed leaf is kept clear, so someone on the balcony blocks
		// that door and must be engaged through the window.
		const FVector E = Ctx.Entrance ? Ctx.Entrance() : FVector(1.66, 0, 5.83);
		Entrance = FHKPt(E.X, E.Z);
		if (Ctx.Doors)
			for (const FHKMissionDoor& D : Ctx.Doors())
			{
				const double Width = D.LeafWidth;
				const bool bBalcony = D.Name == TEXT("Balcony door");
				const double Ts[6] = {0, .2, .4, .6, .8, 1};
				const FVector Euler = HK::QuatToEuler(D.HingeQuat, TEXT("XYZ"));
				for (int32 k = 0; k < (bBalcony ? 1 : 6); k++)
				{
					const double T = Ts[k];
					FVector Pos = D.HingePos;
					FQuat Q = D.HingeQuat;
					if (D.Motion == TEXT("slide")) Pos.X = D.ClosedX + D.Travel * T;
					else Q = HK::EulerToQuat(Euler.X, D.OpenAngle * T, Euler.Z, TEXT("XYZ"));
					const FHKMat Leaf = (D.ParentWorld * FHKMat::Compose(Pos, Q, D.HingeScale)) * D.LeafLocal;
					const FVector A = Leaf.TransformPoint(FVector(-Width / 2, 0, 0)), B = Leaf.TransformPoint(FVector(Width / 2, 0, 0));
					KeepOut.Add({FHKPt(A.X, A.Z), FHKPt(B.X, B.Z)});
				}
			}
		if (Ctx.StairSpawns) StairZones = Ctx.StairSpawns();
	}

	static double SegmentDistance(const FHKPt& P, const FSeg& S)
	{
		const double Dx = S.B.X - S.A.X, Dz = S.B.Z - S.A.Z;
		double L = Dx * Dx + Dz * Dz;
		if (L == 0 || L != L) L = 1;
		const double T = MxMax(0, MxMin(1, ((P.X - S.A.X) * Dx + (P.Z - S.A.Z) * Dz) / L));
		return HKCase::JsHypot(P.X - S.A.X - T * Dx, P.Z - S.A.Z - T * Dz);
	}
	bool Free(const FHKPt& P) const
	{
		if (!(HKCase::JsHypot(P.X - Entrance.X, P.Z - Entrance.Z) >= 1.7)) return false;
		for (const FSeg& K : KeepOut) if (SegmentDistance(P, K) < .32) return false;
		return SpawnWorld.CanStand(P, 1.75);
	}
	static void Box(const TArray<FHKPt>& Pts, double& X0, double& X1, double& Z0, double& Z1)
	{
		X0 = Z0 = std::numeric_limits<double>::infinity();
		X1 = Z1 = -std::numeric_limits<double>::infinity();
		for (const FHKPt& P : Pts) { X0 = MxMin(X0, P.X); X1 = MxMax(X1, P.X); Z0 = MxMin(Z0, P.Z); Z1 = MxMax(Z1, P.Z); }
	}
	// Pick a room first, then search inside it, so furnished rooms are not starved by rejections.
	bool Sample(HKCase::FRng& Rng, const FString& InOnly, HKCase::FSpot& Out) const
	{
		if (InOnly == TEXT("stair") && StairZones.Num())
		{
			const FHKStairSpawn& Zone = StairZones[(int32)std::floor(Rng() * StairZones.Num())];
			double X0, X1, Z0, Z1;
			Box(Zone.Points, X0, X1, Z0, Z1);
			for (int32 i = 0; i < 60; i++)
			{
				const double X = X0 + .35 + Rng() * (X1 - X0 - .7);
				const double Z = Z0 + .35 + Rng() * (Z1 - Z0 - .7);
				if (HKCase::InsidePolygon(X, Z, Zone.Points) && Walk && Walk->CanStand(FHKPt(X, Z), 1.75, Zone.Y)) { Out = {X, Z, Zone.Y, Zone.Name}; return true; }
			}
			return false;
		}
		FString Only = InOnly;
		if (Only == TEXT("apartment") || Only == TEXT("stair")) Only.Empty();
		if (!Areas.Num()) return false;
		double Pick = Rng() * Total;
		const FArea* Area = &Areas.Last();
		for (const FArea& A : Areas) { if ((Pick -= A.Area) <= 0) { Area = &A; break; } }
		if (!Only.IsEmpty()) Area = Areas.FindByPredicate([&](const FArea& A) { return A.Name == Only; });
		if (!Area) return false;
		double X0, X1, Z0, Z1;
		Box(Area->Points, X0, X1, Z0, Z1);
		for (int32 i = 0; i < 80; i++)
		{
			const double X = X0 + Rng() * (X1 - X0);
			const double Z = Z0 + Rng() * (Z1 - Z0);
			if (HKCase::InsidePolygon(X, Z, Area->Points) && Free(FHKPt(X, Z))) { Out = {X, Z, 0, Area->Name}; return true; }
		}
		return false;
	}
	FExplain Explain(double X, double Z) const
	{
		FExplain E;
		const FHKPt P(X, Z);
		E.Entrance = HKCase::JsHypot(P.X - Entrance.X, P.Z - Entrance.Z);
		E.Door = std::numeric_limits<double>::infinity();
		for (const FSeg& K : KeepOut) E.Door = MxMin(E.Door, SegmentDistance(P, K));
		E.bStand = SpawnWorld.CanStand(P, 1.75);
		return E;
	}
};

TArray<HKCase::FPlan> FHKMission::SamplePlans(int32 N)
{
	FSampler S(Ctx);
	TArray<HKCase::FPlan> Out;
	for (int32 i = 0; i < N; i++)
	{
		HKCase::FRng R((uint32)(i + 1));
		HKCase::FPlan P;
		HKCase::PlanCase(R, [&S](HKCase::FRng& Rng, const FString& Zone, HKCase::FSpot& Spot) { return S.Sample(Rng, Zone, Spot); }, P);
		Out.Add(P);
	}
	return Out;
}

FHKMission::FExplain FHKMission::Explain(double X, double Z)
{
	FSampler S(Ctx);
	return S.Explain(X, Z);
}

TArray<FHKMission::FExplain> FHKMission::ExplainAll(const TArray<FVector2D>& Points)
{
	FSampler S(Ctx);
	TArray<FExplain> Out;
	for (const FVector2D& P : Points) Out.Add(S.Explain(P.X, P.Y));
	return Out;
}

int32 FHKMission::ProbeArea(const FString& Name, int32 N)
{
	FSampler S(Ctx);
	HKCase::FRng R(7);
	int32 Ok = 0;
	for (int32 i = 0; i < N; i++) { HKCase::FSpot Spot; if (S.Sample(R, Name, Spot)) Ok++; }
	return Ok;
}

// ======================================================================================================================
// Figures: mannequins (buildActor) and their raycast geometry
// ======================================================================================================================
namespace
{
	// three.js BoxGeometry(w, h, d) (one segment per side) and SphereGeometry(r, 18, 14): triangle vertices as float32,
	// in three.js order, so ray hits land on exactly the same faces.
	void MxBoxPlane(TArray<FVector>& Tris, int32 U, int32 V, int32 W, double UDir, double VDir, double Width, double Height, double Depth)
	{
		const double SegW = Width / 1, SegH = Height / 1, WHalf = Width / 2, HHalf = Height / 2, DHalf = Depth / 2;
		FVector Grid[4];
		for (int32 iy = 0; iy < 2; iy++)
		{
			const double Y = iy * SegH - HHalf;
			for (int32 ix = 0; ix < 2; ix++)
			{
				const double X = ix * SegW - WHalf;
				double P[3];
				P[U] = X * UDir;
				P[V] = Y * VDir;
				P[W] = DHalf;
				Grid[iy * 2 + ix] = FVector((float)P[0], (float)P[1], (float)P[2]);
			}
		}
		// a = ix + gridX1 * iy, b = ix + gridX1 * (iy + 1), c = (ix + 1) + gridX1 * (iy + 1), d = (ix + 1) + gridX1 * iy
		const FVector& A = Grid[0];
		const FVector& B = Grid[2];
		const FVector& C = Grid[3];
		const FVector& D = Grid[1];
		Tris.Add(A); Tris.Add(B); Tris.Add(D);
		Tris.Add(B); Tris.Add(C); Tris.Add(D);
	}
	const TArray<FVector>* MxBoxTris(const FVector& Size)
	{
		// Values on the heap: pointers to them stay valid while the map grows.
		static TMap<FString, TUniquePtr<TArray<FVector>>> Cache;
		const FString Key = FString::Printf(TEXT("b%.17g,%.17g,%.17g"), Size.X, Size.Y, Size.Z);
		if (const TUniquePtr<TArray<FVector>>* Found = Cache.Find(Key)) return Found->Get();
		TArray<FVector>& T = *Cache.Add(Key, MakeUnique<TArray<FVector>>());
		const double Wd = Size.X, Ht = Size.Y, Dp = Size.Z;
		MxBoxPlane(T, 2, 1, 0, -1, -1, Dp, Ht, Wd);     // px
		MxBoxPlane(T, 2, 1, 0, 1, -1, Dp, Ht, -Wd);     // nx
		MxBoxPlane(T, 0, 2, 1, 1, 1, Wd, Dp, Ht);       // py
		MxBoxPlane(T, 0, 2, 1, 1, -1, Wd, Dp, -Ht);     // ny
		MxBoxPlane(T, 0, 1, 2, 1, -1, Wd, Ht, Dp);      // pz
		MxBoxPlane(T, 0, 1, 2, -1, -1, Wd, Ht, -Dp);    // nz
		return &T;
	}
	const TArray<FVector>* MxSphereTris(double Radius)
	{
		static TMap<FString, TUniquePtr<TArray<FVector>>> Cache;
		const FString Key = FString::Printf(TEXT("s%.17g"), Radius);
		if (const TUniquePtr<TArray<FVector>>* Found = Cache.Find(Key)) return Found->Get();
		TArray<FVector>& T = *Cache.Add(Key, MakeUnique<TArray<FVector>>());
		const int32 WS = 18, HS = 14;
		const double PhiLength = 2 * HK::Pi, ThetaLength = HK::Pi, ThetaEnd = MxMin(0 + ThetaLength, HK::Pi);
		TArray<FVector> Verts;
		for (int32 iy = 0; iy <= HS; iy++)
		{
			const double V = (double)iy / HS;
			for (int32 ix = 0; ix <= WS; ix++)
			{
				const double U = (double)ix / WS;
				const double X = -Radius * std::cos(0 + U * PhiLength) * std::sin(0 + V * ThetaLength);
				const double Y = Radius * std::cos(0 + V * ThetaLength);
				const double Z = Radius * std::sin(0 + U * PhiLength) * std::sin(0 + V * ThetaLength);
				Verts.Add(FVector((float)X, (float)Y, (float)Z));
			}
		}
		auto G = [&](int32 iy, int32 ix) -> const FVector& { return Verts[iy * (WS + 1) + ix]; };
		for (int32 iy = 0; iy < HS; iy++)
			for (int32 ix = 0; ix < WS; ix++)
			{
				const FVector& A = G(iy, ix + 1);
				const FVector& B = G(iy, ix);
				const FVector& C = G(iy + 1, ix);
				const FVector& D = G(iy + 1, ix + 1);
				if (iy != 0) { T.Add(A); T.Add(B); T.Add(D); }
				if (iy != HS - 1 || ThetaEnd < HK::Pi) { T.Add(B); T.Add(C); T.Add(D); }
			}
		return &T;
	}

	// three.js Ray.intersectTriangle(a, b, c, backfaceCulling, target)
	bool MxIntersectTriangle(const FVector& O, const FVector& D, const FVector& A, const FVector& B, const FVector& C, bool bCull, FVector& Out)
	{
		const FVector Edge1 = B - A, Edge2 = C - A, Normal = MxCross(Edge1, Edge2);
		double DdN = MxDot(D, Normal), Sign;
		if (DdN > 0) { if (bCull) return false; Sign = 1; }
		else if (DdN < 0) { Sign = -1; DdN = -DdN; }
		else return false;
		const FVector Diff = O - A;
		const double DdQxE2 = Sign * MxDot(D, MxCross(Diff, Edge2));
		if (DdQxE2 < 0) return false;
		const double DdE1xQ = Sign * MxDot(D, MxCross(Edge1, Diff));
		if (DdE1xQ < 0) return false;
		if (DdQxE2 + DdE1xQ > DdN) return false;
		const double QdN = -Sign * MxDot(Diff, Normal);
		if (QdN < 0) return false;
		Out = MxAddScaled(O, D, QdN / DdN);
		return true;
	}

	// Mesh.raycast for a mesh with a front-side material: the nearest hit within [0, Far].
	bool MxRayMesh(const FHKMat& World, const TArray<FVector>& Tris, const FVector& O, const FVector& D, double Far, double& OutDist)
	{
		const FHKMat Inv = World.Inverse();
		const FVector LO = Inv.TransformPoint(O), LD = MxNorm(Inv.TransformDir(D));
		bool bHit = false;
		for (int32 i = 0; i + 2 < Tris.Num(); i += 3)
		{
			FVector P;
			if (!MxIntersectTriangle(LO, LD, Tris[i], Tris[i + 1], Tris[i + 2], true, P)) continue;
			const double Dist = MxDist(O, World.TransformPoint(P));
			if (Dist < 0 || Dist > Far) continue;
			if (!bHit || Dist < OutDist) { OutDist = Dist; bHit = true; }
		}
		return bHit;
	}

	// buildActor(): limb(parent, w, h, d, x, y, z) boxes and addHead(r, x, y, z) spheres, in the web's order.
	struct FMxLimbDef { double W, H, D, X, Y, Z, R; bool bHead; };
	const FMxLimbDef MxHostileLimbs[] = {
		{.15, .86, .17, -.11, .43, 0, 0, false}, {.15, .86, .17, .11, .43, 0, 0, false},
		{.44, .62, .25, 0, 1.17, 0, 0, false}, {.2, .1, .2, 0, 1.5, 0, 0, false},
		{0, 0, 0, 0, 1.64, 0, .115, true}, {.15, .035, .02, 0, 1.66, -.105, 0, false},
		// Both arms forward holding a pistol; the red armband marks the armed suspect.
		{.11, .11, .42, -.17, 1.33, -.2, 0, false}, {.11, .11, .42, .17, 1.33, -.2, 0, false},
		{.115, .07, .115, .17, 1.33, -.08, 0, false},
		{.08, .08, .08, 0, 1.31, -.43, 0, false}, {.035, .05, .17, 0, 1.36, -.52, 0, false}};
	const FMxLimbDef MxHostageLimbs[] = {
		// Kneeling with hands on the head.
		{.15, .14, .5, -.11, .07, .12, 0, false}, {.15, .14, .5, .11, .07, .12, 0, false},
		{.15, .42, .17, -.11, .35, -.06, 0, false}, {.15, .42, .17, .11, .35, -.06, 0, false},
		{.4, .56, .23, 0, .84, -.04, 0, false}, {0, 0, 0, 0, 1.25, -.04, .11, true}, {.2, .06, .2, 0, 1.34, -.02, 0, false},
		{.09, .3, .09, -.26, 1.12, -.04, 0, false}, {.09, .3, .09, .26, 1.12, -.04, 0, false},
		{.09, .09, .22, -.18, 1.3, -.04, 0, false}, {.09, .09, .22, .18, 1.3, -.04, 0, false}};
}

FHKMission::FHKMission(const FHKMissionContext& InCtx) : Ctx(InCtx)
{
	FlashLight.Color = MxCss(0xffd28a);
	FlashLight.Distance = 7;
	EnemyFlashLight.Color = MxCss(0xffc070);
	EnemyFlashLight.Distance = 6;
	Viewmodel.Rig.Root->bVisible = false;
}

FHKMission::~FHKMission() {}

double FHKMission::Rand()
{
	if (Ctx.Random) return Ctx.Random();
	return FMath::FRand() * (1 - 1e-7);
}

FHKMissionActor* FHKMission::FindActor(const FString& Id) const
{
	for (const TUniquePtr<FHKMissionActor>& A : Actors) if (A->Id == Id) return A.Get();
	return nullptr;
}

TArray<FHKRig*> FHKMission::ActorRigs() const
{
	TArray<FHKRig*> R;
	for (const TUniquePtr<FHKMissionActor>& A : Actors) R.Add(A->Rig.Get());
	return R;
}

void FHKMission::BuildActor(const HKCase::FPlanActor& P)
{
	TUniquePtr<FHKMissionActor> A = MakeUnique<FHKMissionActor>();
	A->Id = P.Id; A->Role = P.Role; A->Zone = P.Zone; A->Area = P.Area;
	A->X = P.X; A->Z = P.Z; A->Y = P.Y; A->Facing = P.Facing;
	A->Health = P.Health; A->bAlive = P.bAlive;
	const bool bHostile = P.Role == MxHostile;
	A->Rig = MakeUnique<FHKRig>();
	FString Error;
	const FString RigName = RigNameOf(*A);
	if (!A->Rig->LoadFromJson(FPaths::Combine(FHKData::DataDir(), TEXT("rigs"), RigName + TEXT(".json")), &Error))
		UE_LOG(LogHaarklous, Warning, TEXT("Mission figure without meshes: %s"), *Error);
	// Root at feet; inner "body" tips over when neutralised.
	A->Root = A->Rig->Root;
	A->Body = A->Rig->Find(TEXT("body"));
	if (!A->Body) A->Body = A->Rig->Create(TEXT("body"), A->Root);
	if (bHostile)
	{
		A->MuzzleFlash = A->Rig->Find(TEXT("muzzleFlash"));
		if (!A->MuzzleFlash) { A->MuzzleFlash = A->Rig->Create(TEXT("muzzleFlash"), A->Body); A->MuzzleFlash->SetPos(0, 1.36, -.67); }
		// Muzzle flash for return fire: muzzleFlash.rotation.x = -PI / 2; hidden until a shot.
		A->MuzzleFlash->SetRotation(-HK::Pi / 2, 0, 0);
		A->MuzzleFlash->bVisible = false;
	}
	A->Root->SetPos(P.X, P.Y, P.Z);
	A->Root->SetRotation(0, P.Facing, 0);
	A->Body->SetPos(0, 0, 0);
	A->Body->SetRotation(0, 0, 0);
	for (const FMxLimbDef& L : bHostile ? TArrayView<const FMxLimbDef>(MxHostileLimbs) : TArrayView<const FMxLimbDef>(MxHostageLimbs))
	{
		FHKMissionActor::FLimb Limb;
		Limb.Pos = FVector(L.X, L.Y, L.Z);
		Limb.Box = FVector(L.W, L.H, L.D);
		Limb.Radius = L.R;
		Limb.bHead = L.bHead;
		Limb.Tris = L.R > 0 ? MxSphereTris(L.R) : MxBoxTris(Limb.Box);
		A->Limbs.Add(Limb);
	}
	A->FireIn = Between(HKCase::HOSTILE_REACTION_MIN, HKCase::HOSTILE_REACTION_MAX);
	Actors.Add(MoveTemp(A));
}

void FHKMission::ClearActors()
{
	Actors.Reset();
	RigsVersion++;
}

void FHKMission::RenderPoint()
{
	// What the web's renderer does before the mission's tick: world matrices of every figure (the raycasts of the next
	// shot use these, as in three.js), and vmCamera.aspect = camera.aspect while the mission draws its overlay.
	for (const TUniquePtr<FHKMissionActor>& A : Actors)
	{
		A->Root->UpdateMatrixWorld();
		for (FHKMissionActor::FLimb& L : A->Limbs) L.MatrixWorld = A->Body->MatrixWorld * FHKMat::Compose(L.Pos, FQuat::Identity, FVector::OneVector);
	}
	if (bActive && Ctx.CameraAspect) VmAspect = Ctx.CameraAspect();
}

TArray<FHKObstacle> FHKMission::ActorObstacles() const
{
	TArray<FHKObstacle> List;
	for (const TUniquePtr<FHKMissionActor>& A : Actors)
	{
		if (!A->bAlive) continue;
		const double R = .24, X = A->Root->Position.X, Z = A->Root->Position.Z, Y = A->Root->Position.Y;
		FHKObstacle O;
		O.Points = {FHKPt(X - R, Z - R), FHKPt(X + R, Z - R), FHKPt(X + R, Z + R), FHKPt(X - R, Z + R)};
		O.MinY = Y;
		O.MaxY = Y + 1.8;
		O.Prepare();
		List.Add(O);
	}
	return List;
}

// ======================================================================================================================
// Timers (setTimeout on real time)
// ======================================================================================================================
int32 FHKMission::SetTimeout(TFunction<void()> Fn, double Ms)
{
	FTimer T;
	T.Id = NextTimerId++;
	T.Due = Clock + Ms / 1000;
	T.Fn = MoveTemp(Fn);
	Timers.Add(MoveTemp(T));
	return Timers.Last().Id;
}

void FHKMission::ClearTimeout(int32 Id)
{
	Timers.RemoveAll([Id](const FTimer& T) { return T.Id == Id; });
}

void FHKMission::RunTimers()
{
	for (;;)
	{
		int32 Best = -1;
		for (int32 i = 0; i < Timers.Num(); i++)
			if (Timers[i].Due <= Clock && (Best < 0 || Timers[i].Due < Timers[Best].Due || (Timers[i].Due == Timers[Best].Due && Timers[i].Id < Timers[Best].Id))) Best = i;
		if (Best < 0) return;
		TFunction<void()> Fn = MoveTemp(Timers[Best].Fn);
		Timers.RemoveAt(Best);
		if (Fn) Fn();
	}
}

// ======================================================================================================================
// Case flow
// ======================================================================================================================
void FHKMission::Feed(const FString& Text, const FString& Kind)
{
	FeedText = Text;
	FeedKind = Kind;
	ClearTimeout(FeedTimer);
	FeedTimer = SetTimeout([this]() { FeedText.Empty(); }, 1800);
}

void FHKMission::UpdateHud()
{
	if (!bHasPlan) return;
	Hud.Id = TEXT("CASE ") + CaseId;
	int32 Down = 0, Total = 0;
	const HKCase::FPlanActor* Hostage = nullptr;
	for (const HKCase::FPlanActor& P : Plan.Actors)
	{
		const FHKMissionActor* A = FindActor(P.Id);
		if (P.Role == MxHostile) { Total++; if (A && !A->bAlive) Down++; }
		if (P.Role == MxHostage && !Hostage) Hostage = &P;
	}
	const FHKMissionActor* H = Hostage ? FindActor(Hostage->Id) : nullptr;
	Hud.Objective = FString::Printf(TEXT("Hostiles down: %d / %d · Hostage: %s"), Down, Total, Hostage && H && !H->bAlive ? TEXT("HIT") : TEXT("alive"));
	Hud.Health = FString::Printf(TEXT("%d"), PlayerHealth);
	Hud.HealthBar = PlayerHealth;
	Hud.bHealthLow = PlayerHealth <= 25;
	Hud.Mag = FString::Printf(TEXT("%d"), Mag);
	Hud.Reserve = FString::Printf(TEXT("/ %d"), Reserve);
	Hud.bMagLow = Mag <= 3;
}

void FHKMission::NewCase()
{
	ClearActors();
	Decals.Reset();
	Seed = (uint32)(Rand() * 4294967296.0);
	CaseId = FString::Printf(TEXT("HP-%05u"), Seed % 100000);
	if (Ctx.CloseAllDoors) Ctx.CloseAllDoors();
	if (Ctx.RebuildWalkWorld) Ctx.RebuildWalkWorld();
	{
		FSampler S(Ctx);
		HKCase::FRng R(Seed);
		FString Error;
		HKCase::FPlan P;
		if (!HKCase::PlanCase(R, [&S](HKCase::FRng& Rng, const FString& Zone, HKCase::FSpot& Spot) { return S.Sample(Rng, Zone, Spot); }, P, &Error))
			UE_LOG(LogHaarklous, Error, TEXT("Mission planning failed: %s"), *Error);
		Plan = P;
		bHasPlan = true;
	}
	for (const HKCase::FPlanActor& A : Plan.Actors) BuildActor(A);
	RigsVersion++;
	PlayerHealth = HKCase::PLAYER_HEALTH;
	bPlayerAlive = true;
	DamageT = 0;
	DamageDirT = 0;
	bHasDamageFrom = false;
	EnemyFlashT = 0;
	Elapsed = 0; Shots = 0; Hits = 0; Mag = HKCase::MAGAZINE; Reserve = HKCase::RESERVE; Reload = 0; Recoil = 0; bTriggerHeld = false; bMouseAim = false;
	if (Ctx.RebuildWalkWorld) Ctx.RebuildWalkWorld();
	const FHKMissionStart S0 = Ctx.MissionStart ? Ctx.MissionStart() : FHKMissionStart();
	if (Ctx.SetPose) Ctx.SetPose(S0.X, S0.Z, S0.Yaw, 0, S0.Y);
	UpdateHud();
}

void FHKMission::ShowPanel() { PanelState = State; }
void FHKMission::HidePanel() { PanelState.Empty(); }

void FHKMission::Briefing()
{
	State = MxBriefing;
	ShowPanel();
	UpdateHud();
}

void FHKMission::Begin(bool bViaGesture)
{
	if (State != MxBriefing && State != MxPaused) return;
	State = MxPlay;
	HidePanel();
	bHasLastTime = false;
	UpdateHud();
}

void FHKMission::Pause()
{
	if (State != MxPlay) return;
	State = MxPaused;
	bTriggerHeld = false;
	bMouseAim = false;
	ShowPanel();
}

void FHKMission::Finish(const FString& InResult)
{
	State = InResult;
	bTriggerHeld = false;
	ShowPanel();
	Result = InResult;
}

void FHKMission::Action(const FString& Name, bool bViaGesture)
{
	if (Name == TEXT("begin") || Name == TEXT("resume")) Begin(bViaGesture);
	else if (Name == TEXT("restart")) { Result.Empty(); NewCase(); Briefing(); }
	else if (Name == TEXT("quit")) Stop();
}

void FHKMission::Start(bool bViaGesture)
{
	if (!bActive)
	{
		bActive = true;
		if (Ctx.RebuildWalkWorld) Ctx.RebuildWalkWorld();
		const FHKMissionStart S0 = Ctx.MissionStart ? Ctx.MissionStart() : FHKMissionStart();
		if (Ctx.SetPose) Ctx.SetPose(S0.X, S0.Z, S0.Yaw, 0, S0.Y);
	}
	Result.Empty();
	NewCase();
	Briefing();
	// requestAnimationFrame(tick): the tick runs from the next frame on.
	if (!bFrameScheduled) { bFrameScheduled = true; ScheduledAt = FrameCounter; }
}

void FHKMission::Stop()
{
	if (!bActive) return;
	bActive = false;
	State = MxOff;
	ClearActors();
	Decals.Reset();
	FlashLight.Intensity = 0;
	EnemyFlashLight.Intensity = 0;
	HidePanel();
	Result.Empty();
	bFrameScheduled = false;
	AimT = 0;
	PadAim = 0;
	bMouseAim = false;
	Viewmodel.Rig.Root->bVisible = false;
	if (Ctx.RebuildWalkWorld) Ctx.RebuildWalkWorld();
	if (Ctx.Leave) Ctx.Leave();
}

void FHKMission::Say(const FString& Text)
{
	if (bActive && !Text.IsEmpty()) Feed(Text);
}

// ======================================================================================================================
// Weapon
// ======================================================================================================================
void FHKMission::Sound(const FString& Kind, double Distance)
{
	// case-game.js sound(kind, distance): white-noise bursts through a falling low-pass, straight to the output (no
	// reverb); the audio module implements these names:
	//   'shot'        0.32 s noise, envelope (1 - t)^4, low-pass 3200 -> 300 Hz, gain .9, plus a 140 -> 40 Hz thump (.7 -> 0, .2 s)
	//   'hostileShot' the same with the low-pass from 1500 + 1200 / max(1, d) Hz and noise gain min(.8, 2.2 / max(1, d));
	//                 Gain passed here is 2.2 / max(1, d) (the audio side clamps it to .8 and derives the cut-off)
	//   'reload', 'empty'  the web's 'click': 0.05 s noise, envelope (1 - t)^2, low-pass 5000 -> 2000 Hz, gain .25
	if (Kind == TEXT("hostileShot")) FHKAudio::Get().Sfx(Kind, FHKSfxOpts(2.2 / MxMax(1, Distance)));
	else FHKAudio::Get().Sfx(Kind);
}

void FHKMission::StartReload()
{
	if (State != MxPlay || Reload > 0 || Mag == HKCase::MAGAZINE || Reserve == 0) return;
	Reload = HKCase::RELOAD_SECONDS;
	Feed(TEXT("Reloading…"));
	Sound(TEXT("reload"));
}

FVector FHKMission::CameraDir() const
{
	// camera.getWorldDirection(): -z of the camera's world matrix, normalised.
	const FHKMat M = FHKMat::Compose(Ctx.CameraPosition ? Ctx.CameraPosition() : FVector::ZeroVector, Ctx.CameraQuaternion ? Ctx.CameraQuaternion() : FQuat::Identity, FVector::OneVector);
	const FVector Z = MxNorm(FVector(M.E[8], M.E[9], M.E[10]));
	return FVector(-Z.X, -Z.Y, -Z.Z);
}

FHKMission::FRayHit FHKMission::Raycast(const FVector& Origin, const FVector& Dir, double Far, bool bActorsToo)
{
	// raycaster.intersectObjects([...world, ...actor meshes]): the world part is Unreal's line trace (Ctx.Shoot); the
	// mannequins are tested here against their three.js geometry. Ties go to the world (it comes first in the list).
	FRayHit R;
	FHKShotHit W;
	if (Ctx.Shoot && Ctx.Shoot(Origin, Dir, Far, W) && W.Distance >= 0 && W.Distance <= Far) { R.bHit = true; R.Distance = W.Distance; R.World = W; }
	if (bActorsToo)
		for (const TUniquePtr<FHKMissionActor>& A : Actors)
			for (const FHKMissionActor::FLimb& L : A->Limbs)
			{
				double D = 0;
				if (L.Tris && MxRayMesh(L.MatrixWorld, *L.Tris, Origin, Dir, Far, D) && (!R.bHit || D < R.Distance))
				{
					R.bHit = true; R.Distance = D; R.Actor = A.Get(); R.bHead = L.bHead;
				}
			}
	if (RayLog)
	{
		FRayLog Log;
		Log.Origin = Origin; Log.Dir = Dir; Log.Far = Far; Log.bHit = R.bHit; Log.ActorId = R.Actor ? R.Actor->Id : FString(); Log.bHead = R.bHead; Log.Distance = R.Distance;
		RayLog->Add(Log);
	}
	return R;
}

void FHKMission::AddDecal(const FHKShotHit& Hit)
{
	FDecal D;
	D.Point = Hit.Point;
	D.Normal = Hit.Normal;
	D.Component = Hit.Component;
	D.Serial = ++DecalSerial;
	Decals.Add(D);
	if (Decals.Num() > 80) Decals.RemoveAt(0);
}

void FHKMission::Fire()
{
	if (State != MxPlay || Reload > 0) return;
	if (Mag == 0)
	{
		Sound(TEXT("empty"));
		Feed(Reserve ? TEXT("Empty: press □ or R to reload") : TEXT("Out of ammunition"));
		if (Reserve) StartReload();
		return;
	}
	Mag--; Shots++; Recoil = 1; FlashT = .055;
	Sound(TEXT("shot"));
	const FVector CamPos = Ctx.CameraPosition ? Ctx.CameraPosition() : FVector::ZeroVector;
	FVector Dir = CameraDir();
	const double Spread = .032 * (1 - AimT) + .002;
	const FVector Right = MxNorm(MxCross(Dir, FVector(0, 1, 0))), Up = MxNorm(MxCross(Right, Dir));
	const double Angle = Rand() * HK::Pi * 2, Radius = std::sqrt(Rand()) * Spread;
	Dir = MxAddScaled(Dir, Right, std::cos(Angle) * Radius);
	Dir = MxAddScaled(Dir, Up, std::sin(Angle) * Radius);
	Dir = MxNorm(Dir);
	const FRayHit Hit = Raycast(CamPos, Dir, 60, true);
	FlashLight.Position = MxAddScaled(CamPos, Dir, .6);
	if (Hit.bHit && Hit.Actor)
	{
		FHKMissionActor& A = *Hit.Actor;
		if (A.bAlive)
		{
			Hits++;
			HKCase::ApplyHit(A.Health, A.bAlive, Hit.bHead);
			A.HitFlash = .12;
			HitMarkerT = .25;
			// A hit hostile flinches before it can fire again.
			A.FireIn = MxMax(A.FireIn, .55);
			if (!A.bAlive)
			{
				if (Ctx.RebuildWalkWorld) Ctx.RebuildWalkWorld();
				if (A.Role == MxHostage) Feed(TEXT("HOSTAGE HIT"), MxFail);
				else Feed(Hit.bHead ? TEXT("Hostile down · headshot") : TEXT("Hostile down"), TEXT("ok"));
				TArray<HKCase::FRoleAlive> List;
				for (const TUniquePtr<FHKMissionActor>& X : Actors) List.Add({X->Role, X->bAlive});
				const FString Res = HKCase::Outcome(List, &bPlayerAlive);
				if (!Res.IsEmpty()) SetTimeout([this, Res]() { if (bActive && State == MxPlay) Finish(Res); }, 650);
			}
			else Feed(TEXT("Hit"));
		}
	}
	else if (Hit.bHit) AddDecal(Hit.World);
	if (Mag == 0 && Reserve > 0) SetTimeout([this]() { StartReload(); }, 260);
	UpdateHud();
}

// ======================================================================================================================
// Return fire
// ======================================================================================================================
bool FHKMission::HostileFire(FHKMissionActor& A)
{
	A.FlashT = .06;
	EnemyFlashT = .06;
	const FVector Eye = Ctx.CameraPosition ? Ctx.CameraPosition() : FVector::ZeroVector;
	const FVector Muzzle = A.MuzzleFlash ? A.MuzzleFlash->GetWorldPosition() : A.Root->GetWorldPosition();
	EnemyFlashLight.Position = Muzzle;
	const double Dist = MxDist(Muzzle, Eye);
	Sound(TEXT("hostileShot"), Dist);
	if (Rand() < HKCase::HostileHitChance(Dist, PlayerSpeed, Ctx.Stance ? Ctx.Stance() : FString(TEXT("stand"))))
	{
		bHasDamageFrom = true;
		DamageFrom = FVector2D(A.Root->Position.X, A.Root->Position.Z);
		DamageDirT = 1.4;
		DamageT = .55;
		if (!bInvulnerable) HKCase::DamagePlayer(PlayerHealth, bPlayerAlive);
		UpdateHud();
		if (!bPlayerAlive)
		{
			State = MxDown;
			bTriggerHeld = false;
			bMouseAim = false;
			Feed(TEXT("YOU ARE DOWN"), MxFail);
			SetTimeout([this]() { if (bActive && State == MxDown) Finish(MxFail); }, 1100);
		}
		else Feed(FString::Printf(TEXT("You are hit · health %d"), PlayerHealth), MxFail);
		return true;
	}
	// A miss passes beside the player and marks whatever lies behind.
	FVector Dir = MxNorm(Eye - Muzzle);
	const FVector Side = MxNorm(FVector(-Dir.Z, 0, Dir.X));
	const double SideSign = Rand() < .5 ? -1 : 1;
	const double SideAmount = SideSign * (.03 + Rand() * .05);
	Dir = MxAddScaled(Dir, Side, SideAmount);
	Dir.Y += (Rand() - .5) * .08;
	Dir = MxNorm(Dir);
	const FRayHit Hit = Raycast(Muzzle, Dir, 30, false);
	if (Hit.bHit) AddDecal(Hit.World);
	return false;
}

void FHKMission::HostileLooks(double Dt)
{
	LosTimer -= Dt;
	if (LosTimer > 0) return;
	LosTimer = .2;
	const FVector Eye = Ctx.CameraPosition ? Ctx.CameraPosition() : FVector::ZeroVector;
	for (const TUniquePtr<FHKMissionActor>& A : Actors)
	{
		if (A->Role != MxHostile || !A->bAlive) continue;
		const FVector Head(A->Root->Position.X, A->Root->Position.Y + 1.6, A->Root->Position.Z);
		const double Dist = MxDist(Head, Eye);
		if (Dist > 9) { A->bSees = false; continue; }
		const FRayHit Block = Raycast(Head, MxNorm(Eye - Head), Dist, false);
		A->bSees = !Block.bHit || Block.Distance > Dist - .25;
	}
}

// ======================================================================================================================
// Per-frame update
// ======================================================================================================================
void FHKMission::Tick(double RealDt)
{
	Clock += RealDt;
	bTickedLastFrame = false;
	RenderPoint();
	if (bActive && bFrameScheduled && ScheduledAt < FrameCounter) { TickFrame(); bTickedLastFrame = true; }
	RunTimers();
	FrameCounter++;
	// The viewmodel follows the camera (the web draws it with its own camera); presentation easing of CSS transitions.
	if (Ctx.CameraPosition && Ctx.CameraQuaternion) Viewmodel.PlaceAtCamera(Ctx.CameraPosition(), Ctx.CameraQuaternion(), Ctx.GetFov ? Ctx.GetFov() : 65, bActive);
	const double Ease = FMath::Clamp(RealDt / .08, 0.0, 1.0);
	HitMarkerShown += (Hud.HitMarker - HitMarkerShown) * Ease;
	HealthBarShown += (Hud.HealthBar - HealthBarShown) * FMath::Clamp(RealDt / .2, 0.0, 1.0);
}

void FHKMission::TickFrame()
{
	const double Dt = bHasLastTime ? MxMin(Clock - LastTime, .05) : 0;
	bHasLastTime = true;
	LastTime = Clock;
	if (State == MxPlay)
	{
		Elapsed += Dt;
		if (Reload > 0)
		{
			Reload -= Dt;
			if (Reload <= 0)
			{
				const int32 Need = HKCase::MAGAZINE - Mag, Take = FMath::Min(Need, Reserve);
				Mag += Take; Reserve -= Take; Reload = 0;
				Feed(TEXT("Reloaded"));
				UpdateHud();
			}
		}
		HostileLooks(Dt);
	}
	const bool bAimWanted = State == MxPlay && Reload <= 0 && (PadAim > .3 || bMouseAim);
	AimT += ((bAimWanted ? 1 : 0) - AimT) * MxMin(1, Dt * 11);
	const double Fov = 65 - 22 * AimT;
	if (Ctx.GetFov && Ctx.SetFov && std::fabs(Ctx.GetFov() - Fov) > .01) Ctx.SetFov(Fov);
	const FVector Eye = Ctx.CameraPosition ? Ctx.CameraPosition() : FVector::ZeroVector;
	// Hostiles turn smoothly toward the player while they have line of sight. Once facing the player, a hostile waits
	// its reaction time and then keeps firing.
	for (const TUniquePtr<FHKMissionActor>& APtr : Actors)
	{
		FHKMissionActor& A = *APtr;
		if (A.bAlive && A.bSees)
		{
			const double Want = std::atan2(-(Eye.X - A.Root->Position.X), -(Eye.Z - A.Root->Position.Z));
			double D = Want - A.Root->Rot().Y;
			D = std::atan2(std::sin(D), std::cos(D));
			A.Root->SetRotY(A.Root->Rot().Y + D * MxMin(1, Dt * 4));
			if (!A.bEngaged) { A.bEngaged = true; A.FireIn = Between(HKCase::HOSTILE_REACTION_MIN, HKCase::HOSTILE_REACTION_MAX); }
			if (State == MxPlay && A.Role == MxHostile && std::fabs(D) < .3 && (A.FireIn -= Dt) <= 0) { HostileFire(A); A.FireIn = Between(HKCase::HOSTILE_INTERVAL_MIN, HKCase::HOSTILE_INTERVAL_MAX); }
		}
		else A.bEngaged = false;
		if (A.MuzzleFlash)
		{
			A.FlashT = MxMax(0, A.FlashT - Dt);
			A.MuzzleFlash->bVisible = A.FlashT > 0;
			A.MuzzleFlash->SetRotY(Rand() * 6);
		}
		if (!A.bAlive && A.Fall < 1)
		{
			A.Fall = MxMin(1, A.Fall + Dt * 2.6);
			const double E = 1 - std::pow(1 - A.Fall, 3);
			A.Body->SetRotX(E * HK::Pi / 2 * .96);
			A.Body->Position.Y = E * .08;
		}
		if (A.HitFlash > 0) { A.HitFlash -= Dt; A.Body->Position.Z = A.HitFlash > 0 ? .04 : 0; }
	}
	const double Moved = bHasLastPos ? HKCase::JsHypot(Eye.X - LastPos.X, Eye.Z - LastPos.Y) / MxMax(Dt, 1e-3) : 0;
	LastPos = FVector2D(Eye.X, Eye.Z);
	bHasLastPos = true;
	if (Dt) PlayerSpeed += (MxMin(Moved, 4) - PlayerSpeed) * MxMin(1, Dt * 6);
	EnemyFlashT = MxMax(0, EnemyFlashT - Dt);
	EnemyFlashLight.Intensity = EnemyFlashT > 0 ? 14 : 0;
	DamageT = MxMax(0, DamageT - Dt);
	DamageDirT = MxMax(0, DamageDirT - Dt);
	// MxRed edge flash on each hit, plus a lasting tint at low health; the arrow points toward the shooter.
	Hud.Damage = MxMin(1, DamageT * 1.8 + (bPlayerAlive ? (PlayerHealth <= 25 ? .35 : 0) : .9));
	if (bHasDamageFrom && DamageDirT > 0)
	{
		const FVector F = CameraDir();
		const double Vx = DamageFrom.X - Eye.X, Vz = DamageFrom.Y - Eye.Z;
		Hud.ArrowAngle = std::atan2(Vx * -F.Z + Vz * F.X, Vx * F.X + Vz * F.Z);
		Hud.ArrowOpacity = MxMin(1, DamageDirT);
	}
	else Hud.ArrowOpacity = 0;
	BobPhase += Dt * (5 + Moved * 3.2);
	Recoil = MxMax(0, Recoil - Dt * 9);
	FlashT = MxMax(0, FlashT - Dt);
	HitMarkerT = MxMax(0, HitMarkerT - Dt);
	Viewmodel.Flash->bVisible = FlashT > 0;
	Viewmodel.Flash->SetRotZ(Rand() * 6);
	FlashLight.Intensity = FlashT > 0 ? 22 : 0;
	const double Bob = MxMin(Moved, 3) * (1 - AimT * .85), ReloadPose = Reload > 0 ? std::sin(HK::Pi * (1 - Reload / HKCase::RELOAD_SECONDS)) : 0;
	const FVector& Hp = FHKViewmodel::HipP;
	const FVector& Ap = FHKViewmodel::AdsP;
	FVector G(Hp.X + (Ap.X - Hp.X) * AimT, Hp.Y + (Ap.Y - Hp.Y) * AimT, Hp.Z + (Ap.Z - Hp.Z) * AimT);
	G.X *= MxMin(1, VmAspect / 1.5) * (1 - AimT) + AimT;
	G.X += std::sin(BobPhase) * .004 * Bob;
	G.Y += -std::fabs(std::cos(BobPhase)) * .005 * Bob - ReloadPose * .09;
	G.Z += Recoil * .03;
	Viewmodel.Gun->Position = G;
	Viewmodel.Gun->SetRotation(FHKViewmodel::HipR.X * (1 - AimT) + Recoil * .16 - ReloadPose * .5, FHKViewmodel::HipR.Y * (1 - AimT), ReloadPose * .55);
	Viewmodel.Slide->Position.Z = Recoil > .55 ? .028 : Recoil * .05;
	Hud.Gap = 4 + 14 * (1 - AimT) + Recoil * 8;
	Hud.CrossOpacity = MxMax(0, 1 - AimT * 1.6);
	Hud.HitMarker = HitMarkerT > 0 ? 1 : 0;
	Hud.Time = HKCase::Clock(Elapsed);
	Hud.Reloading = Reload > 0 ? TEXT("RELOADING") : TEXT("");
}

// ======================================================================================================================
// Input (each returns true when the mission consumed it)
// ======================================================================================================================
bool FHKMission::PadButton(const FString& Name)
{
	if (!bActive) return false;
	if ((State == MxBriefing || State == MxPaused) && (Name == TEXT("left") || Name == TEXT("right")))
	{
		if (Ctx.StepSensitivity) Ctx.StepSensitivity(TEXT("pad"), Name == TEXT("left") ? -1 : 1);
		return true;
	}
	if (State == MxDown) return true;
	if (State == MxBriefing) { if (Name == TEXT("cross")) Begin(false); else if (Name == TEXT("circle")) Stop(); return true; }
	if (State == MxPaused)
	{
		if (Name == TEXT("options") || Name == TEXT("cross")) Begin(false);
		else if (Name == TEXT("touchpad")) Action(TEXT("restart"));
		else if (Name == TEXT("circle")) Stop();
		return true;
	}
	if (State == MxSuccess || State == MxFail)
	{
		if (Name == TEXT("cross") || Name == TEXT("options") || Name == TEXT("touchpad")) Action(TEXT("restart"));
		else if (Name == TEXT("circle")) Stop();
		return true;
	}
	if (Name == TEXT("options")) { Pause(); return true; }
	// □ opens a door in reach, otherwise reloads.
	if (Name == TEXT("square")) { if (!(Ctx.UseDoorAhead && Ctx.UseDoorAhead())) StartReload(); return true; }
	if (Name == TEXT("touchpad")) { Action(TEXT("restart")); return true; }
	// ✕ jump, ○ crouch / hold for prone and L3 run fall through; teleport, eye height and measuring are disabled.
	return !(Name == TEXT("cross") || Name == TEXT("circle") || Name == TEXT("l3"));
}

void FHKMission::Triggers(double L2, double R2)
{
	if (!bActive) return;
	PadAim = L2;
	if (R2 > .55 && !bTriggerHeld) { bTriggerHeld = true; Fire(); }
	else if (R2 < .35) bTriggerHeld = false;
}

bool FHKMission::Key(const FString& KeyName, const FString& Code, bool bRepeat)
{
	if (!bActive) return false;
	if (KeyName == TEXT("Escape"))
	{
		if (State == MxPlay) { Pause(); return true; }
		if (State == MxPaused || State == MxBriefing || State == MxSuccess || State == MxFail) { Stop(); return true; }
	}
	if (KeyName == TEXT("Enter") || KeyName == TEXT(" "))
	{
		if (State == MxBriefing || State == MxPaused) { Begin(true); return true; }
		if (State == MxSuccess || State == MxFail) { Action(TEXT("restart"), true); return true; }
		return KeyName == TEXT("Enter");
	}
	if (Code == TEXT("KeyR") && !bRepeat)
	{
		if (State == MxPlay) StartReload();
		else if (State == MxPaused || State == MxSuccess || State == MxFail) Action(TEXT("restart"), true);
		return true;
	}
	return State != MxPlay && Code != TEXT("KeyE");
}

bool FHKMission::Mouse(int32 Button, bool bDown)
{
	if (Button == 2 && !bDown) bMouseAim = false;
	if (!bActive || State != MxPlay) return false;
	if (Button == 0 && bDown) Fire();
	if (Button == 2) bMouseAim = bDown;
	return true;
}

bool FHKMission::RouteInput(FHKInputFrame& In)
{
	// The canvas mouse handlers run before the frame (mousedown fires, right button aims).
	const bool bRight = In.Mouse.bRight;
	if (!bActive) { bPrevRight = bRight; return false; }
	if (In.Mouse.bLeftEdge) Mouse(0, true);
	if (bRight != bPrevRight) Mouse(2, bRight);
	bPrevRight = bRight;
	if (In.Mouse.bLeftUp) Mouse(0, false);
	// game-main.js: the mission owns ✕ (begin), ○ (leave on panels), Options (pause), □ (door or reload), triggers.
	const TArray<FName> Pressed = In.Pressed.Array();
	for (const FName& N : Pressed)
	{
		const FString Name = N.ToString();
		if (Name.StartsWith(TEXT("key:"))) continue;
		if (Name == TEXT("reload")) Key(TEXT("r"), TEXT("KeyR"), false);
		else if (Name == TEXT("options") || State != MxPlay) PadButton(Name == TEXT("mouseLeft") ? FString() : Name);
		else if (Name == TEXT("square") || Name == TEXT("touchpad")) PadButton(Name);
	}
	if (In.bGamepad) Triggers(In.L2, In.R2);
	// The button that left the mission (○, ✕ ...) must not also crouch or jump.
	if (!bActive)
	{
		for (const TCHAR* B : {TEXT("circle"), TEXT("cross"), TEXT("square"), TEXT("options")}) In.Pressed.Remove(FName(B));
		return true;
	}
	return false;
}

// ======================================================================================================================
// HUD and panels (the web's #caseHud and #casePanel, style.css)
// ======================================================================================================================
namespace
{
	struct FMxButton { FString Action, Label; };
	// A run of panel text; the PlayStation glyphs and arrows are drawn as shapes (the UI font may lack them).
	struct FMxRun
	{
		FString Text;
		FLinearColor Color;
		bool bBold = false;
	};
	bool MxIsGlyph(TCHAR C) { return C == 0x2715 || C == 0x25CB || C == 0x25A1 || C == 0x2190 || C == 0x2192; }

	void MxDrawGlyph(UCanvas* C, TCHAR G, float X, float Y, float Size, const FLinearColor& Col)
	{
		const float S = Size * .62f, Cx = X + Size * .45f, Cy = Y + Size * .62f, T = FMath::Max(1.2f, Size * .1f);
		if (G == 0x2715) { FHKUi::Line(C, FVector2D(Cx - S / 2, Cy - S / 2), FVector2D(Cx + S / 2, Cy + S / 2), T, Col); FHKUi::Line(C, FVector2D(Cx - S / 2, Cy + S / 2), FVector2D(Cx + S / 2, Cy - S / 2), T, Col); }
		else if (G == 0x25CB) FHKUi::Circle(C, FVector2D(Cx, Cy), S / 2, T, Col, 24);
		else if (G == 0x25A1) FHKUi::Frame(C, Cx - S / 2, Cy - S / 2, S, S, T, Col);
		else
		{
			const float Dir = G == 0x2192 ? 1.f : -1.f, Hx = Cx + Dir * S / 2;
			FHKUi::Line(C, FVector2D(Cx - Dir * S / 2, Cy), FVector2D(Hx, Cy), T, Col);
			FHKUi::Line(C, FVector2D(Hx, Cy), FVector2D(Hx - Dir * S * .4f, Cy - S * .35f), T, Col);
			FHKUi::Line(C, FVector2D(Hx, Cy), FVector2D(Hx - Dir * S * .4f, Cy + S * .35f), T, Col);
		}
	}

	// Text with CSS letter-spacing (em), drawn character by character; returns the width.
	float MxSpacedText(UCanvas* C, const FString& S, float X, float Y, float Size, const FLinearColor& Col, float Spacing, bool bBold, bool bDraw, EHKAlign Align = EHKAlign::Left)
	{
		float W = 0;
		for (int32 i = 0; i < S.Len(); i++) W += (MxIsGlyph(S[i]) ? Size * .9f : FHKUi::Measure(C, S.Mid(i, 1), Size, bBold).X) + Spacing * Size;
		float Px = Align == EHKAlign::Center ? X - W / 2 : Align == EHKAlign::Right ? X - W : X;
		if (bDraw)
			for (int32 i = 0; i < S.Len(); i++)
			{
				const FString Ch = S.Mid(i, 1);
				if (MxIsGlyph(S[i])) MxDrawGlyph(C, S[i], Px, Y, Size, Col); else if (Ch != TEXT(" ")) FHKUi::Text(C, Ch, Px, Y, Size, Col, EHKAlign::Left, bBold);
				Px += (MxIsGlyph(S[i]) ? Size * .9f : FHKUi::Measure(C, Ch, Size, bBold).X) + Spacing * Size;
			}
		return W;
	}

	// Word-wrapped rich text in [X, X + W) from Y; returns the height used (LineH per line).
	float MxRichText(UCanvas* C, const TArray<FMxRun>& Runs, float X, float Y, float W, float Size, float LineH, bool bDraw)
	{
		struct FTok { FString S; TCHAR Glyph = 0; bool bSpaceBefore = false; FLinearColor Color; bool bBold = false; float Width = 0; };
		TArray<FTok> Toks;
		bool bPendingSpace = false;
		for (const FMxRun& R : Runs)
		{
			FString Word;
			auto Flush = [&]() { if (!Word.IsEmpty()) { FTok T; T.S = Word; T.bSpaceBefore = bPendingSpace; T.Color = R.Color; T.bBold = R.bBold; Toks.Add(T); bPendingSpace = false; Word.Empty(); } };
			for (const TCHAR Ch : R.Text)
			{
				if (Ch == TEXT(' ')) { Flush(); bPendingSpace = true; }
				else if (MxIsGlyph(Ch)) { Flush(); FTok T; T.Glyph = Ch; T.bSpaceBefore = bPendingSpace; T.Color = R.Color; T.bBold = R.bBold; Toks.Add(T); bPendingSpace = false; }
				else Word.AppendChar(Ch);
			}
			Flush();
		}
		const float Space = FHKUi::Measure(C, TEXT("a b"), Size).X - FHKUi::Measure(C, TEXT("ab"), Size).X;
		for (FTok& T : Toks) T.Width = T.Glyph ? Size * .9f : FHKUi::Measure(C, T.S, Size, T.bBold).X;
		float Px = 0, Line = 0;
		const float TextTop = (LineH - Size * 1.17f) / 2;
		for (int32 i = 0; i < Toks.Num(); i++)
		{
			const FTok& T = Toks[i];
			const float Gap = (Px > 0 && T.bSpaceBefore) ? Space : 0;
			if (Px > 0 && Px + Gap + T.Width > W) { Line++; Px = 0; }
			else Px += Gap;
			if (bDraw)
			{
				if (T.Glyph) MxDrawGlyph(C, T.Glyph, X + Px, Y + Line * LineH + TextTop, Size, T.Color);
				else FHKUi::Text(C, T.S, X + Px, Y + Line * LineH + TextTop, Size, T.Color, EHKAlign::Left, T.bBold, false);
			}
			Px += T.Width;
		}
		return (Line + 1) * LineH;
	}
	float MxRichWidth(UCanvas* C, const FString& S, float Size, bool bBold)
	{
		float W = 0;
		FString Word;
		for (const TCHAR Ch : S)
		{
			if (MxIsGlyph(Ch)) { W += FHKUi::Measure(C, Word, Size, bBold).X + Size * .9f; Word.Empty(); }
			else Word.AppendChar(Ch);
		}
		return W + FHKUi::Measure(C, Word, Size, bBold).X;
	}
	// Draws text that may contain glyphs on one line.
	void MxGlyphText(UCanvas* C, const FString& S, float X, float Y, float Size, const FLinearColor& Col, bool bBold)
	{
		FString Word;
		float Px = X;
		for (const TCHAR Ch : S)
		{
			if (MxIsGlyph(Ch))
			{
				if (!Word.IsEmpty()) { FHKUi::Text(C, Word, Px, Y, Size, Col, EHKAlign::Left, bBold, false); Px += FHKUi::Measure(C, Word, Size, bBold).X; Word.Empty(); }
				MxDrawGlyph(C, Ch, Px, Y, Size, Col);
				Px += Size * .9f;
			}
			else Word.AppendChar(Ch);
		}
		if (!Word.IsEmpty()) FHKUi::Text(C, Word, Px, Y, Size, Col, EHKAlign::Left, bBold, false);
	}

	const FLinearColor MxPanelInk = MxCss(0xece8dc), MxPanelPara = MxCss(0xd9d4c7), MxGold = MxCss(0xd8b25a), MxGreen = MxCss(0x8fe39a), MxRed = MxCss(0xff7a6b);
	const TCHAR* MxControls[][2] = {
		{TEXT("Left stick"), TEXT("Move · click L3 to run")}, {TEXT("Right stick"), TEXT("Look")}, {TEXT("L2"), TEXT("Aim down sights")}, {TEXT("R2"), TEXT("Fire")},
		{TEXT("□"), TEXT("Open / close the door in front, otherwise reload")}, {TEXT("✕"), TEXT("Jump")}, {TEXT("○"), TEXT("Tap: crouch · hold: lie prone")},
		{TEXT("Options"), TEXT("Pause")}, {TEXT("Keyboard"), TEXT("Space jump · C crouch · Z prone · R reload · E door")}, {TEXT("Mouse"), TEXT("Right button aim · left fire")}};
}

void FHKMission::DrawHud(UCanvas* C)
{
	ClickTargets.Reset();
	if (!bActive || !C) return;
	const FVector2D Sz = FHKUi::Size(C);
	const float W = Sz.X, H = Sz.Y, Cx = W / 2, Cy = H / 2;
	const FLinearColor Ink = MxCss(0xf4f1e8);
	auto Baseline = [](float B, float Size) { return B - Size * .93f; };

	// ---- .case-top: case id and objective (left), clock (right)
	FHKUi::Text(C, Hud.Id, 16, 14, 13, Ink, EHKAlign::Left, true);
	FHKUi::Text(C, Hud.Objective, 16, 14 + 16.9f + 3, 13, MxAlpha(Ink, .9f));
	FHKUi::Text(C, Hud.Time, W - 16, 14, 13, Ink, EHKAlign::Right, true);

	// ---- crosshair (gap and fade from the tick), hit marker
	{
		const float G = (float)Hud.Gap, O = (float)Hud.CrossOpacity;
		if (O > 0)
		{
			const FLinearColor Shade(0, 0, 0, .55f * O), White(1, 1, 1, O);
			auto Bar = [&](float X0, float Y0, float Bw, float Bh) { FHKUi::Rect(C, X0 - 1, Y0 - 1, Bw + 2, Bh + 2, Shade); FHKUi::Rect(C, X0, Y0, Bw, Bh, White); };
			Bar(Cx - G - 9, Cy - 1, 9, 2); Bar(Cx + G, Cy - 1, 9, 2);
			Bar(Cx - 1, Cy - G - 9, 2, 9); Bar(Cx - 1, Cy + G, 2, 9);
			FHKUi::Disc(C, FVector2D(Cx, Cy), 2.5f, Shade, 12);
			FHKUi::Disc(C, FVector2D(Cx, Cy), 1.5f, White, 12);
		}
		const float Hm = (float)HitMarkerShown;
		if (Hm > .01f)
		{
			const float S = 8, Y = Cy - 26 * .02f;
			for (int32 Pass = 0; Pass < 2; Pass++)
			{
				const FLinearColor Col = Pass ? FLinearColor(1, 1, 1, Hm) : FLinearColor(0, 0, 0, .6f * Hm);
				const float T = Pass ? 2.4f : 4.4f;
				FHKUi::Line(C, FVector2D(Cx - S, Y - S), FVector2D(Cx + S, Y + S), T, Col);
				FHKUi::Line(C, FVector2D(Cx - S, Y + S), FVector2D(Cx + S, Y - S), T, Col);
			}
		}
	}

	// ---- #caseFeed (centre, 62 % down)
	if (!FeedText.IsEmpty())
	{
		const bool bFail = FeedKind == MxFail, bOk = FeedKind == TEXT("ok");
		const float Size = bFail ? 20.f : 16.f;
		MxSpacedText(C, FeedText, Cx, H * .62f, Size, bFail ? MxCss(0xff6b5e) : bOk ? MxCss(0x9ff0a8) : Ink, .06f, true, true, EHKAlign::Center);
	}

	// ---- .case-ammo (bottom right): magazine, reserve, RELOADING
	{
		const float Base = H - 18 - (44.2f - 34) / 2 - .26f * 34;
		const float RW = FHKUi::Measure(C, Hud.Reserve, 16, true).X;
		FHKUi::Text(C, Hud.Reserve, W - 22, Baseline(Base, 16), 16, MxAlpha(Ink, .85f), EHKAlign::Right, true);
		FHKUi::Text(C, Hud.Mag, W - 22 - RW - 6, Baseline(Base, 34), 34, Hud.bMagLow ? MxRed : Ink, EHKAlign::Right, true);
		if (!Hud.Reloading.IsEmpty()) FHKUi::Text(C, Hud.Reloading, W - 22, Baseline(H - 18 - 44 - 4, 10.8f), 10.8f, MxCss(0xffd27a), EHKAlign::Right, true);
	}

	// ---- .case-health (bottom left): label, number, bar
	{
		const float Base = H - 25 - (39 - 30) / 2.f - .26f * 30;
		const float LW = MxSpacedText(C, TEXT("HEALTH"), 22, Baseline(Base, 11), 11, MxAlpha(Ink, .85f), .14f, false, true);
		FHKUi::Text(C, Hud.Health, 22 + LW + 8, Baseline(Base, 30), 30, Hud.bHealthLow ? MxRed : Ink, EHKAlign::Left, true);
		FHKUi::Panel(C, 22, H - 23, 150, 5, FLinearColor(0, 0, 0, .44f), 2.5f);
		const float Fill = 150 * FMath::Clamp((float)HealthBarShown / 100.f, 0.f, 1.f);
		if (Fill > .5f) FHKUi::Panel(C, 22, H - 23, Fill, 5, MxCss(0x9ff0a8), FMath::Min(2.5f, Fill / 2));
	}

	// ---- #caseDamage: red vignette (radial gradient, transparent to 45 %, #c4100a88 at 80 %, #a00c07dd at the corners)
	if (Hud.Damage > .001)
	{
		const float S = FHKUi::Scale(C), O = (float)Hud.Damage;
		const int32 Seg = 48;
		const float Rx = W / 2 * 1.41421356f, Ry = H / 2 * 1.41421356f;
		const float Stops[3] = {.45f, .8f, 1.f};
		const FLinearColor Cols[3] = {MxCss(0xc4100a, 0), MxCss(0xc4100a, .533f * O), MxCss(0xa00c07, .867f * O)};
		TArray<FCanvasUVTri> Tris;
		for (int32 r = 0; r < 2; r++)
			for (int32 i = 0; i < Seg; i++)
			{
				const float A0 = 2 * PI * i / Seg, A1 = 2 * PI * (i + 1) / Seg;
				auto P = [&](float T, float A) { return FVector2D((Cx + FMath::Cos(A) * Rx * T) * S, (Cy + FMath::Sin(A) * Ry * T) * S); };
				FCanvasUVTri T1, T2;
				T1.V0_Pos = P(Stops[r], A0); T1.V1_Pos = P(Stops[r + 1], A0); T1.V2_Pos = P(Stops[r + 1], A1);
				T1.V0_Color = Cols[r]; T1.V1_Color = Cols[r + 1]; T1.V2_Color = Cols[r + 1];
				T2.V0_Pos = P(Stops[r], A0); T2.V1_Pos = P(Stops[r + 1], A1); T2.V2_Pos = P(Stops[r], A1);
				T2.V0_Color = Cols[r]; T2.V1_Color = Cols[r + 1]; T2.V2_Color = Cols[r];
				Tris.Add(T1); Tris.Add(T2);
			}
		FCanvasTriangleItem Item(Tris, GWhiteTexture);
		Item.BlendMode = SE_BLEND_Translucent;
		C->DrawItem(Item);
	}
	// ---- #caseDamageDir: arrow 118 px from the centre toward the shooter
	if (Hud.ArrowOpacity > .001)
	{
		const float S = FHKUi::Scale(C), O = (float)Hud.ArrowOpacity, Ca = FMath::Cos((float)Hud.ArrowAngle), Sa = FMath::Sin((float)Hud.ArrowAngle);
		auto Rot = [&](float X, float Y) { return FVector2D((Cx + X * Ca - Y * Sa) * S, (Cy + X * Sa + Y * Ca) * S); };
		for (int32 Pass = 0; Pass < 2; Pass++)
		{
			const float Grow = Pass ? 0.f : 3.f;
			FCanvasUVTri T;
			T.V0_Pos = Rot(0, -11 - 118 - Grow); T.V1_Pos = Rot(-16 - Grow, 11 - 118 + Grow * .5f); T.V2_Pos = Rot(16 + Grow, 11 - 118 + Grow * .5f);
			T.V0_Color = T.V1_Color = T.V2_Color = Pass ? MxCss(0xff3b2f, .85f * O) : FLinearColor(0, 0, 0, .4f * O);
			FCanvasTriangleItem Item(TArray<FCanvasUVTri>({T}), GWhiteTexture);
			Item.BlendMode = SE_BLEND_Translucent;
			C->DrawItem(Item);
		}
	}

	// ---- #casePanel: briefing, pause, result
	if (PanelState.IsEmpty()) return;
	const float PW = FMath::Min(560.f, W - 32), PadX = 26, PadY = 24, IW = PW - 2 * PadX;
	auto Layout = [&](float X0, float Y0, bool bDraw) -> float
	{
		float Y = Y0, PendingMargin = 0;
		auto Block = [&](float MarginTop) { Y += FMath::Max(PendingMargin, MarginTop); PendingMargin = 0; };
		auto Eyebrow = [&](const FString& S) { Block(0); MxSpacedText(C, S, X0, Y, 11, MxGold, .14f, true, bDraw); Y += 11; };
		auto H2 = [&](const FString& S, const FLinearColor& Col) { Block(4); Y += MxRichText(C, {{S, Col, true}}, X0, Y, IW, 24, 36, bDraw); PendingMargin = 12; };
		auto Para = [&](const TArray<FMxRun>& Runs) { Block(0); Y += MxRichText(C, Runs, X0, Y, IW, 14, 21, bDraw); PendingMargin = 10; };
		auto Dl = [&](const TArray<TPair<FString, TArray<FMxRun>>>& Rows, float Size)
		{
			Block(14);
			float DtW = 0;
			for (const auto& R : Rows) DtW = FMath::Max(DtW, MxRichWidth(C, R.Key, Size, false));
			const float LineH = Size * 1.5f;
			for (int32 i = 0; i < Rows.Num(); i++)
			{
				if (i) Y += 4;
				if (bDraw) MxGlyphText(C, Rows[i].Key, X0, Y + (LineH - Size * 1.17f) / 2, Size, MxGold, false);
				Y += MxRichText(C, Rows[i].Value, X0 + DtW + 14, Y, IW - DtW - 14, Size, LineH, bDraw);
			}
			PendingMargin = 14;
		};
		auto Sens = [&]()
		{
			Block(14);
			// .case-sens: padding 10 12, eyebrow, two rows (label 90 px | slider | output 52 px), a hint line.
			const float BoxY = Y, RowsY = BoxY + 10 + 11 + 6, SmallY = RowsY + 52, BoxH = (SmallY + 18 + 10) - BoxY;
			if (bDraw)
			{
				FHKUi::Panel(C, X0, BoxY, IW, BoxH, FLinearColor(1, 1, 1, .05f), 6);
				MxSpacedText(C, TEXT("LOOK SENSITIVITY"), X0 + 12, BoxY + 10, 11, MxGold, .14f, true, true);
				MxRichText(C, {{TEXT("D-pad ←/→ changes controller sensitivity here."), MxAlpha(MxPanelInk, .75f), false}}, X0 + 12, SmallY, IW - 24, 12, 18, true);
			}
			auto Row = [&](const FString& Kind, const FString& Label, float RY)
			{
				const double Min = Ctx.SensitivityMin, Max = Ctx.SensitivityMax, V = Ctx.GetSensitivity ? Ctx.GetSensitivity(Kind) : 1;
				const float SX = X0 + 12 + 90 + 10, SW = IW - 24 - 90 - 52 - 20, Mid = RY + 10;
				if (!bDraw) return;
				FHKUi::Text(C, Label, X0 + 12, RY + (20 - 13 * 1.17f) / 2, 13, MxPanelInk, EHKAlign::Left, false, false);
				const float F = (float)FMath::Clamp((V - Min) / (Max - Min), 0.0, 1.0);
				FHKUi::Panel(C, SX, Mid - 2, SW, 4, FLinearColor(1, 1, 1, .25f), 2);
				FHKUi::Panel(C, SX, Mid - 2, FMath::Max(4.f, SW * F), 4, MxGold, 2);
				FHKUi::Disc(C, FVector2D(SX + SW * F, Mid), 7, MxGold, 20);
				FHKUi::Text(C, HKCase::JsToFixed(V, 2) + TEXT("×"), X0 + IW - 12, RY + (20 - 13 * 1.17f) / 2, 13, MxPanelInk, EHKAlign::Right, false, false);
				FClickTarget T;
				T.Box = FBox2D(FVector2D(SX - 8, RY), FVector2D(SX + SW + 8, RY + 20));
				T.Slider = Kind;
				T.Min = Min;
				T.Max = Max;
				ClickTargets.Add(T);
			};
			Row(TEXT("pad"), TEXT("Controller"), RowsY);
			Row(TEXT("mouse"), TEXT("Mouse"), RowsY + 26);
			Y = BoxY + BoxH;
			PendingMargin = 14;
		};
		auto Buttons = [&](const TArray<FMxButton>& List)
		{
			Block(16);
			float Bx = X0;
			for (int32 i = 0; i < List.Num(); i++)
			{
				const float BW = MxRichWidth(C, List[i].Label, 14, true) + 28;
				if (Bx > X0 && Bx + BW > X0 + IW) { Bx = X0; Y += 34 + 10; }
				if (bDraw)
				{
					FHKUi::Panel(C, Bx, Y, BW, 34, i == 0 ? MxGold : MxCss(0x2c343d), 5);
					MxGlyphText(C, List[i].Label, Bx + 14, Y + (34 - 14 * 1.17f) / 2, 14, i == 0 ? MxCss(0x11161c) : MxPanelInk, true);
					FClickTarget T;
					T.Box = FBox2D(FVector2D(Bx, Y), FVector2D(Bx + BW, Y + 34));
					T.Action = List[i].Action;
					ClickTargets.Add(T);
				}
				Bx += BW + 10;
			}
			Y += 34;
		};
		auto ControlsDl = [&]()
		{
			TArray<TPair<FString, TArray<FMxRun>>> Rows;
			for (const auto& R : MxControls) Rows.Add(TPair<FString, TArray<FMxRun>>(R[0], TArray<FMxRun>({{R[1], MxPanelInk, false}})));
			Dl(Rows, 13);
		};
		const FString P = PanelState;
		if (P == MxBriefing)
		{
			Eyebrow(TEXT("CASE FILE · ") + CaseId);
			H2(TEXT("Hostage rescue · Haarklous plass 17A"), FLinearColor::White);
			const FLinearColor B = FLinearColor::White;
			Para({{TEXT("You are outside the street door. A "), MxPanelPara}, {TEXT("hostage"), B, true}, {TEXT(" is held in the "), MxPanelPara}, {TEXT("4th-floor apartment"), B, true},
				{TEXT(". Intel reports "), MxPanelPara}, {FString::Printf(TEXT("%d armed hostiles"), Plan.HostileCount), B, true}, {TEXT(": some in the stairwell, the rest in the apartment."), MxPanelPara}});
			Para({{TEXT("The hostiles "), MxPanelPara}, {TEXT("shoot back"), B, true}, {TEXT(" once they see you: four hits and you are down. Moving makes you harder to hit."), MxPanelPara}});
			Para({{TEXT("Success"), MxGreen, true}, {TEXT(" every hostile neutralised. "), MxPanelPara}, {TEXT("Failure"), MxRed, true}, {TEXT(" the hostage is shot, or you are down."), MxPanelPara}});
			ControlsDl();
			Sens();
			Buttons({{TEXT("begin"), TEXT("✕ / Enter · Begin mission")}, {TEXT("quit"), TEXT("○ / Esc · Leave")}});
		}
		else if (P == MxPaused)
		{
			Eyebrow(FString::Printf(TEXT("CASE %s · PAUSED"), *CaseId));
			H2(FString::Printf(TEXT("%s · health %d"), *HKCase::Clock(Elapsed), PlayerHealth), FLinearColor::White);
			Sens();
			ControlsDl();
			Buttons({{TEXT("resume"), TEXT("Options / ✕ · Resume")}, {TEXT("restart"), TEXT("Touchpad / R · New case")}, {TEXT("quit"), TEXT("○ / Esc · Leave")}});
		}
		else
		{
			const bool bSuccess = P == MxSuccess;
			int32 Down = 0, Total = 0;
			bool bHostageDown = false, bHostageFound = false;
			for (const HKCase::FPlanActor& A : Plan.Actors)
			{
				const FHKMissionActor* X = FindActor(A.Id);
				if (A.Role == MxHostile) { Total++; if (X && !X->bAlive) Down++; }
				else if (A.Role == MxHostage && !bHostageFound) { bHostageFound = true; bHostageDown = X && !X->bAlive; }
			}
			const FString Title = bSuccess ? TEXT("Hostage rescued") : bHostageDown ? TEXT("Mission failed: hostage shot") : TEXT("Mission failed: you were shot");
			Eyebrow(FString::Printf(TEXT("CASE %s · %s"), *CaseId, bSuccess ? TEXT("CLOSED") : TEXT("FAILED")));
			H2(Title, bSuccess ? MxGreen : MxRed);
			TArray<TPair<FString, TArray<FMxRun>>> Rows;
			auto Plain = [&](const FString& S) { return TArray<FMxRun>({{S, MxPanelInk, false}}); };
			Rows.Add(TPair<FString, TArray<FMxRun>>(TEXT("Time"), Plain(HKCase::Clock(Elapsed))));
			Rows.Add(TPair<FString, TArray<FMxRun>>(TEXT("Hostiles"), Plain(FString::Printf(TEXT("%d / %d neutralised"), Down, Total))));
			Rows.Add(TPair<FString, TArray<FMxRun>>(TEXT("Hostage"), Plan.bHasHostage ? (bHostageDown ? TArray<FMxRun>({{TEXT("Shot"), MxRed, true}}) : Plain(TEXT("Safe"))) : Plain(TEXT("None present"))));
			Rows.Add(TPair<FString, TArray<FMxRun>>(TEXT("Health"), Plain(FString::Printf(TEXT("%d"), PlayerHealth))));
			Rows.Add(TPair<FString, TArray<FMxRun>>(TEXT("Shots"), Plain(FString::Printf(TEXT("%d"), Shots))));
			Rows.Add(TPair<FString, TArray<FMxRun>>(TEXT("Accuracy"), Plain(FString::Printf(TEXT("%d%%"), Shots ? (int32)HK::JsRound((double)Hits / Shots * 100) : 0))));
			Dl(Rows, 15);
			Buttons({{TEXT("restart"), TEXT("✕ / Enter · New case")}, {TEXT("quit"), TEXT("○ / Esc · Leave")}});
		}
		return Y - Y0;
	};
	const float ContentH = Layout(0, 0, false);
	const float PH = FMath::Min(ContentH + 2 * PadY, H - 40), PX = Cx - PW / 2, PY = Cy - PH / 2;
	for (int32 i = 3; i >= 1; i--) FHKUi::Panel(C, PX - i * 5, PY + 12 - i * 5 + 6, PW + i * 10, PH + i * 10, FLinearColor(0, 0, 0, .12f), 8 + i * 5);
	FHKUi::Panel(C, PX, PY, PW, PH, MxCss(0x11161c, .95f), 8);
	ClickTargets.Reset();
	Layout(PX + PadX, PY + PadY, true);
}

bool FHKMission::HandleClick(const FVector2D& UiPos)
{
	for (const FClickTarget& T : ClickTargets)
	{
		if (!T.Box.IsInside(UiPos)) continue;
		if (!T.Action.IsEmpty()) { Action(T.Action, true); return true; }
		if (!T.Slider.IsEmpty() && Ctx.SetSensitivity)
		{
			// <input type="range" step="0.05">: the value snaps to the step from the minimum.
			const double F = FMath::Clamp((UiPos.X - T.Box.Min.X - 8) / FMath::Max(1.0, (double)(T.Box.Max.X - T.Box.Min.X - 16)), 0.0, 1.0);
			const double V = T.Min + HK::JsRound((F * (T.Max - T.Min)) / .05) * .05;
			Ctx.SetSensitivity(T.Slider, V);
			return true;
		}
	}
	return false;
}

#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(pop)
#endif
