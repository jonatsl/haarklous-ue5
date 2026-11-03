// Street parking (3 Nov 2026): see HKParking.h. Placement follows the web's parked cars in HKVehicles::CreateAll.
#include "HKParking.h"
#include "HKData.h"
#include "HKVehicleMath.h"
#include "HKVehicles.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	constexpr double Reach = 400;     // metres from home (the realism radius the user asked for)
	constexpr int32 MaxCars = 220;
	double Cross(const FVector2D& A, const FVector2D& B) { return A.X * B.Y - A.Y * B.X; }
}

bool HKParking::Enabled()
{
	static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("HKParkedCars"));
	return bOn;
}

void HKParking::Create(const TArray<TUniquePtr<FHKVehicle>>& Existing, TArray<TUniquePtr<FHKVehicle>>& Out)
{
	FHKData& Data = FHKData::Get();
	HK::FParkMiller Rng(4711);
	auto Rnd = [&Rng]() { return Rng.Next(); };
	// The streets the web parks on (with the service lanes along the blocks, lined with cars in Street View) and the
	// paths that cross them, in the map frame (x, s).
	static const TCHAR* Kinds[] = {TEXT("residential"), TEXT("tertiary"), TEXT("secondary"), TEXT("unclassified"), TEXT("service"), TEXT("living_street")};
	static const TCHAR* PathKinds[] = {TEXT("footway"), TEXT("path"), TEXT("cycleway"), TEXT("steps"), TEXT("pedestrian")};
	struct FSeg { int32 Ri; double W; FVector2D A, B; };
	TArray<FSeg> Roads, Paths;
	int32 Ri = 0;
	for (const TSharedPtr<FJsonValue>& RV : HKJson::Arr(Data.D, TEXT("roads")))
	{
		const TSharedPtr<FJsonObject> R = RV->AsObject();
		const FString K = HKJson::Str(R, TEXT("k"));
		bool bRoad = false, bPath = false;
		for (const TCHAR* Kd : Kinds) bRoad |= K == Kd;
		for (const TCHAR* Kd : PathKinds) bPath |= K == Kd;
		if (!bRoad && !bPath) continue;
		const TArray<double> Pts = HKJson::Nums(R, TEXT("p"));
		const double Wd = HKJson::Num(R, TEXT("w"), 6);
		for (int32 k = 0; k + 3 < Pts.Num(); k += 2)
		{
			const FVector2D A(Pts[k], Pts[k + 1]), B(Pts[k + 2], Pts[k + 3]);
			if (A.Size() > Reach + 60 && B.Size() > Reach + 60) continue;
			(bRoad ? Roads : Paths).Add({Ri, Wd, A, B});
		}
		Ri++;
	}
	// Nearest streets first, so the cars (MaxCars of them) fill the streets around home.
	TArray<FSeg> Order = Roads;
	Order.Sort([](const FSeg& A, const FSeg& B) { return ((A.A + A.B) / 2).Size() < ((B.A + B.B) / 2).Size(); });
	auto SegDist = [](const FVector2D& P, const FSeg& S)
	{
		const FVector2D D = S.B - S.A;
		const double L = FMath::Max(D.SizeSquared(), 1e-9), t = FMath::Clamp(FVector2D::DotProduct(P - S.A, D) / L, 0.0, 1.0);
		return (P - S.A - D * t).Size();
	};
	// Keep clear of the web's vehicles, the helipad, the firepit and the runway (as the web's parking does).
	struct FKeep { double X, Z, R; };
	const HKVehicles::FAir Heli = HKVehicles::HeliHome(), Plane = HKVehicles::PlaneHome();
	TArray<FKeep> Keep = {{Heli.X, Heli.Z, 14}, {-2.7, 34.7, 9}};
	for (const TUniquePtr<FHKVehicle>& V : Existing) Keep.Add({V->X, V->Z, 6.5});
	const FSeg Runway = {-1, 0, FVector2D(Plane.X, Plane.Z), FVector2D(Plane.X - std::sin(Plane.Yaw) * 280, Plane.Z - std::cos(Plane.Yaw) * 280)};
	FHKWalkWorld& W = Data.VehicleWorld(1.25);
	TArray<FHKObstacle> SavedDynamic = W.Dynamic;
	W.SetDynamic({});
	TArray<FVector2D> Placed;
	for (const FSeg& S : Order)
	{
		const FVector2D D = S.B - S.A;
		const double L = D.Size();
		if (L < 12) continue;
		const FVector2D U = D / L, Nrm(-U.Y, U.X);
		// Where footpaths cross (zebra crossings, path ends): no car within 4 m.
		TArray<double> Crossings;
		for (const FSeg& P : Paths)
		{
			const FVector2D E = P.B - P.A;
			const double Den = Cross(D, E);
			if (FMath::Abs(Den) < 1e-9) continue;
			const double t = Cross(P.A - S.A, E) / Den, v = Cross(P.A - S.A, D) / Den;
			if (t >= 0 && t <= 1 && v >= 0 && v <= 1) Crossings.Add(t * L);
		}
		for (const double Side : {1.0, -1.0})
		{
			// On the asphalt by the kerb where the street is wide enough to pass (7 m), else beside it like the web's.
			const double Off = S.W >= 7 ? S.W / 2 - 1.0 : S.W / 2 + 1.25;
			for (double t = 5 + Rnd() * 3; t < L - 5 && Placed.Num() < MaxCars; t += 5.3 + Rnd() * 1.7)
			{
				if (Rnd() < .12) continue;   // a gap now and then (a drive, a free space)
				const FVector2D Lp = S.A + U * t + Nrm * (Off * Side);
				if (Lp.Size() > Reach) continue;
				bool bSkip = false;
				for (const double C : Crossings) bSkip |= FMath::Abs(C - t) < 4;
				for (int32 k = 0; k < Roads.Num() && !bSkip; k++) bSkip |= Roads[k].Ri != S.Ri && SegDist(Lp, Roads[k]) < 10;
				if (bSkip) continue;
				const FVector2D P2 = Data.ToWorld(Lp.X, Lp.Y);
				const FHKPt P(P2.X, P2.Y);
				if (Data.InCleared(P.X, P.Z) || SegDist(FVector2D(P.X, P.Z), Runway) < 12) continue;
				for (const FKeep& Kp : Keep) bSkip |= HKV::Hypot(Kp.X - P.X, Kp.Z - P.Z) < Kp.R;
				for (const FVector2D& Q : Placed) bSkip |= HKV::Hypot(Q.X - P.X, Q.Y - P.Z) < 4.6;
				if (bSkip) continue;
				const double Yaw = HKVehicles::LocalHeadingToYaw(std::atan2(U.X * Side, U.Y * Side)), Fx = -std::sin(Yaw), Fz = -std::cos(Yaw);
				const double H = Data.HeightWorld(P.X, P.Z);
				if (FMath::Abs(Data.HeightWorld(P.X + Fx * 2, P.Z + Fz * 2) - Data.HeightWorld(P.X - Fx * 2, P.Z - Fz * 2)) > 1.1) continue;
				if (!W.CanStand(P, 1.4, H) || !W.CanStand(FHKPt(P.X + Fx * 1.6, P.Z + Fz * 1.6), 1.4, H) || !W.CanStand(FHKPt(P.X - Fx * 1.6, P.Z - Fz * 1.6), 1.4, H)) continue;
				Out.Add(MakeUnique<FHKCar>(P.X, P.Z, Yaw, (int32)std::floor(Rnd() * 7)));
				Placed.Add(FVector2D(P.X, P.Z));
			}
		}
	}
	W.SetDynamic(MoveTemp(SavedDynamic));
}
