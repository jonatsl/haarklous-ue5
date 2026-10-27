// Automation tests for the hostage-rescue port (HKMission): the C++ code must answer like the web's case-game.js.
//   Haarklous.Mission.WebUnitTests   reference/web-game/tests/case-game.test.cjs, ported one to one
//   Haarklous.Mission.Rules          createRng, planCase, hit chance, damage, outcome against the web's own answers
//   Haarklous.Mission.Sampler        the real spawn sampler (apartment, balcony, stairwell) and its plans for 100 seeds
//   Haarklous.Mission.Replay         three scripted missions replayed frame by frame (AI, shots, health, HUD, viewmodel)
//   Haarklous.Mission.Rigs           the exported figures load with the nodes the code moves
//   Haarklous.Mission.Numbers        JavaScript number formatting (toFixed, the mission clock, Math.hypot)
// Reference answers: tools/web-export/reference-mission.js -> Content/Data/tests/mission.json.
#include "Misc/AutomationTest.h"
#include "HKData.h"
#include "HKInput.h"
#include "HKMission.h"
#include "Misc/Paths.h"
#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TSharedPtr<FJsonObject> MxLoadRef(FAutomationTestBase& Test)
	{
		static TSharedPtr<FJsonObject> Cached;
		if (Cached.IsValid()) return Cached;
		FString Error;
		Cached = HKJson::LoadFile(FHKData::DataDir() / TEXT("tests") / TEXT("mission.json"), &Error);
		if (!Cached) Test.AddError(Error);
		return Cached;
	}
	bool MxNear(double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol * FMath::Max(1.0, FMath::Abs(B)) || (A >= 1e8 && B >= 1e8) || (!FMath::IsFinite(A) && FMath::Abs(B) >= 1e8); }
	struct FMxTally
	{
		FAutomationTestBase& Test; FString Name; int32 Total = 0, Bad = 0, MaxErrors = 12;
		FMxTally(FAutomationTestBase& T, const FString& N) : Test(T), Name(N) {}
		bool Check(bool bOk, const FString& What) { Total++; if (!bOk) { if (Bad < MaxErrors) Test.AddError(Name + TEXT(": ") + What); Bad++; } return bOk; }
		~FMxTally() { Test.AddInfo(FString::Printf(TEXT("%s: %d / %d match the web version"), *Name, Total - Bad, Total)); }
	};
	FString MxStr(const TSharedPtr<FJsonValue>& V) { return V.IsValid() && V->Type == EJson::String ? V->AsString() : FString(); }
	double MxNum(const TSharedPtr<FJsonValue>& V) { return V.IsValid() && V->Type == EJson::Number ? V->AsNumber() : (V.IsValid() && V->Type == EJson::Boolean ? (V->AsBool() ? 1 : 0) : 0); }
	bool MxBool(const TSharedPtr<FJsonValue>& V) { return V.IsValid() && (V->Type == EJson::Boolean ? V->AsBool() : V->Type == EJson::Number ? V->AsNumber() != 0 : false); }

	// planOut(): {hostileCount, hasHostage, actors: [[id, role, zone, x, z, y, area, facing, health]]}
	bool MxSamePlan(const HKCase::FPlan& P, const TSharedPtr<FJsonObject>& J, FString& Why)
	{
		const TArray<TSharedPtr<FJsonValue>>& A = HKJson::Arr(J, TEXT("actors"));
		if (P.HostileCount != (int32)HKJson::Num(J, TEXT("hostileCount"))) { Why = FString::Printf(TEXT("hostileCount %d vs web %d"), P.HostileCount, (int32)HKJson::Num(J, TEXT("hostileCount"))); return false; }
		if (P.Actors.Num() != A.Num()) { Why = FString::Printf(TEXT("%d actors vs web %d"), P.Actors.Num(), A.Num()); return false; }
		for (int32 i = 0; i < A.Num(); i++)
		{
			const TArray<TSharedPtr<FJsonValue>>& W = A[i]->AsArray();
			const HKCase::FPlanActor& X = P.Actors[i];
			if (X.Id != MxStr(W[0]) || X.Role != MxStr(W[1]) || X.Zone != MxStr(W[2]) || !MxNear(X.X, MxNum(W[3]), 1e-12) || !MxNear(X.Z, MxNum(W[4]), 1e-12) || !MxNear(X.Y, MxNum(W[5]), 1e-12)
				|| X.Area != MxStr(W[6]) || !MxNear(X.Facing, MxNum(W[7]), 1e-12) || X.Health != (int32)MxNum(W[8]))
			{
				Why = FString::Printf(TEXT("actor %d: %s %s %s (%.6f, %.6f, %.3f) %s %.6f vs web %s %s %s (%.6f, %.6f, %.3f) %s %.6f"), i, *X.Id, *X.Role, *X.Zone, X.X, X.Z, X.Y, *X.Area, X.Facing,
					*MxStr(W[0]), *MxStr(W[1]), *MxStr(W[2]), MxNum(W[3]), MxNum(W[4]), MxNum(W[5]), *MxStr(W[6]), MxNum(W[7]));
				return false;
			}
		}
		return true;
	}
	// The test samplers of case-game.test.cjs.
	const auto MxZoned = [](HKCase::FRng& Rng, const FString& Zone, HKCase::FSpot& S)
	{
		if (Zone == TEXT("stair")) { S.X = Rng() * 2.6; S.Z = 5 + Rng() * 2; S.Y = -2.85 * std::floor(Rng() * 4); S.Area = TEXT("Stair"); }
		else { S.X = Rng() * 6; S.Z = Rng() * 10; S.Y = 0; S.Area = TEXT("Room"); }
		return true;
	};
	const auto MxFlat = [](HKCase::FRng& Rng, const FString& Zone, HKCase::FSpot& S) { S.X = Rng() * 6; S.Z = Rng() * 10; S.Y = 0; S.Area = TEXT("Room"); return true; };

	TArray<FHKObstacle> MxObstaclesOf(const TSharedPtr<FJsonValue>& V) { return V.IsValid() && V->Type == EJson::Array ? FHKData::ObstaclesFromJson(V->AsArray()) : TArray<FHKObstacle>(); }

	// A mission context like AHKGame's, with the game.json data and a walk world whose dynamic list is set by the test.
	FHKMissionContext MxDataContext(TArray<FHKObstacle>* Base, FHKMission** Mission)
	{
		FHKMissionContext Ctx;
		FHKWalkWorld* W = &FHKData::Get().WalkWorld;
		Ctx.WalkWorld = [W]() { return W; };
		Ctx.RebuildWalkWorld = [W, Base, Mission]()
		{
			TArray<FHKObstacle> List = Base ? *Base : TArray<FHKObstacle>();
			if (Mission && *Mission && (*Mission)->IsActive()) List.Append((*Mission)->ActorObstacles());
			W->SetDynamic(List);
		};
		Ctx.CloseAllDoors = Ctx.RebuildWalkWorld;
		Ctx.Doors = []() { return HKMissionData::Doors(); };
		Ctx.SpawnAreas = []() { return HKMissionData::SpawnAreas(); };
		Ctx.StairSpawns = []() { return HKMissionData::StairSpawns(); };
		Ctx.Entrance = []() { return HKMissionData::Entrance(); };
		Ctx.MissionStart = []() { return HKMissionData::MissionStart(); };
		return Ctx;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKMissionWebUnitTest, "Haarklous.Mission.WebUnitTests", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKMissionWebUnitTest::RunTest(const FString& Parameters)
{
	// seeded generator repeats exactly and differs between seeds
	{
		HKCase::FRng A(42), B(42), C(43);
		bool bSame = true, bDiffer = false;
		for (int32 i = 0; i < 5; i++) { const double X = A(), Y = B(), Z = C(); bSame &= X == Y; bDiffer |= X != Z; }
		TestTrue(TEXT("same seed, same sequence"), bSame);
		TestTrue(TEXT("other seed, other sequence"), bDiffer);
	}
	// every mission has 3-5 hostiles split between apartment and stairwell, one hostage in the apartment
	{
		int32 Counts[6] = {0};
		TSet<FString> Spots;
		bool bOk = true;
		for (int32 Seed = 1; Seed <= 300; Seed++)
		{
			HKCase::FRng R(Seed);
			HKCase::FPlan P;
			bOk &= HKCase::PlanCase(R, MxZoned, P);
			int32 Hostiles = 0, Hostages = 0;
			bool bStair = false, bApt = false, bHostageInApt = false;
			for (const HKCase::FPlanActor& A : P.Actors)
			{
				if (A.Role == TEXT("hostile")) { Hostiles++; bStair |= A.Zone == TEXT("stair"); bApt |= A.Zone == TEXT("apartment"); }
				else { Hostages++; bHostageInApt = A.Zone == TEXT("apartment"); }
				Spots.Add(FString::Printf(TEXT("%.2f,%.2f"), A.X, A.Z));
			}
			bOk &= Hostiles >= 3 && Hostiles <= 5 && Hostiles == P.HostileCount && Hostages == 1 && bHostageInApt && bStair && bApt;
			if (Hostiles >= 3 && Hostiles <= 5) Counts[Hostiles]++;
		}
		TestTrue(TEXT("3-5 hostiles split between stairwell and apartment, one hostage in the apartment"), bOk);
		TestTrue(FString::Printf(TEXT("all counts occur (%d, %d, %d)"), Counts[3], Counts[4], Counts[5]), Counts[3] > 50 && Counts[4] > 50 && Counts[5] > 50);
		TestTrue(TEXT("positions change from mission to mission"), Spots.Num() > 1000);
	}
	// planning fails loudly instead of placing actors in invalid spots
	{
		HKCase::FRng R(1);
		HKCase::FPlan P;
		FString Error;
		HKCase::FPlanOptions O;
		O.MaxTries = 20;
		const bool bPlanned = HKCase::PlanCase(R, [](HKCase::FRng&, const FString&, HKCase::FSpot&) { return false; }, P, &Error, O);
		TestTrue(TEXT("no free position"), !bPlanned && Error.Contains(TEXT("No free position")));
	}
	// hostiles need two body hits or one head hit; a hostage falls to one hit
	{
		int32 H = 2; bool bA = true;
		HKCase::ApplyHit(H, bA, false); TestTrue(TEXT("one body hit"), bA);
		HKCase::ApplyHit(H, bA, false); TestFalse(TEXT("two body hits"), bA);
		H = 2; bA = true; HKCase::ApplyHit(H, bA, true); TestFalse(TEXT("head hit"), bA);
		H = 1; bA = true; HKCase::ApplyHit(H, bA, false); TestFalse(TEXT("hostage"), bA);
	}
	// case succeeds only when all hostiles are down and fails when a hostage is shot
	{
		TArray<HKCase::FRoleAlive> L = {{TEXT("hostile"), true}, {TEXT("hostile"), true}, {TEXT("hostage"), true}};
		TestEqual(TEXT("all alive"), HKCase::Outcome(L), FString());
		L[0].bAlive = false; TestEqual(TEXT("one down"), HKCase::Outcome(L), FString());
		L[1].bAlive = false; TestEqual(TEXT("all down"), HKCase::Outcome(L), FString(TEXT("success")));
		L[2].bAlive = false; TestEqual(TEXT("hostage down"), HKCase::Outcome(L), FString(TEXT("fail")));
		TestEqual(TEXT("hostage down, hostile alive"), HKCase::Outcome({{TEXT("hostile"), true}, {TEXT("hostage"), false}}), FString(TEXT("fail")));
	}
	// polygon test used for spawn areas handles concave rooms
	{
		const TArray<FHKPt> L = {FHKPt(0, 0), FHKPt(4, 0), FHKPt(4, 1), FHKPt(1, 1), FHKPt(1, 4), FHKPt(0, 4)};
		TestTrue(TEXT("inside the L"), HKCase::InsidePolygon(.5, 3, L));
		TestFalse(TEXT("outside the L"), HKCase::InsidePolygon(3, 3, L));
	}
	// hostile hit chance falls with distance and player movement, within fixed bounds
	{
		const double Close = HKCase::HostileHitChance(1.5, 0), Far = HKCase::HostileHitChance(8, 0), Running = HKCase::HostileHitChance(1.5, 3.2);
		TestTrue(TEXT("close"), Close > .7 && Close <= .85);
		TestTrue(TEXT("far"), Far < Close);
		TestTrue(TEXT("running"), Running < Close - .25);
		TestEqual(TEXT("floor"), HKCase::HostileHitChance(40, 4), .12);
		TestTrue(TEXT("ceiling"), HKCase::HostileHitChance(0, 0) <= .85);
	}
	// four hostile hits put the player down and fail the case
	{
		int32 H = HKCase::PLAYER_HEALTH; bool bA = true;
		for (int32 i = 0; i < 3; i++) HKCase::DamagePlayer(H, bA);
		TestEqual(TEXT("health after three"), H, HKCase::PLAYER_HEALTH - 3 * HKCase::HOSTILE_DAMAGE);
		TestTrue(TEXT("alive after three"), bA);
		TestEqual(TEXT("no outcome yet"), HKCase::Outcome({{TEXT("hostile"), true}}, &bA), FString());
		HKCase::DamagePlayer(H, bA);
		TestEqual(TEXT("health after four"), H, 0);
		TestFalse(TEXT("down after four"), bA);
		TestEqual(TEXT("fail"), HKCase::Outcome({{TEXT("hostile"), true}}, &bA), FString(TEXT("fail")));
		HKCase::DamagePlayer(H, bA);
		TestEqual(TEXT("stays at 0"), H, 0);
	}
	// crouching and lying prone make the player harder to hit
	{
		const double S = HKCase::HostileHitChance(3, 0, TEXT("stand")), C = HKCase::HostileHitChance(3, 0, TEXT("crouch")), P = HKCase::HostileHitChance(3, 0, TEXT("prone"));
		TestTrue(TEXT("stand > crouch > prone"), S > C && C > P);
		TestTrue(TEXT("prone floor"), P >= .08);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKMissionRulesTest, "Haarklous.Mission.Rules", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKMissionRulesTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> R = MxLoadRef(*this);
	if (!R) return false;
	{
		FMxTally T(*this, TEXT("createRng"));
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("rng")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
			HKCase::FRng G((uint32)(int64)MxNum(A[0]));
			for (const TSharedPtr<FJsonValue>& X : A[1]->AsArray()) { const double Got = G(); T.Check(Got == MxNum(X), FString::Printf(TEXT("seed %.0f: %.17g vs web %.17g"), MxNum(A[0]), Got, MxNum(X))); }
		}
	}
	auto Plans = [&](const TCHAR* Key, auto Sampler)
	{
		FMxTally T(*this, Key);
		int32 Seed = 1;
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, Key))
		{
			HKCase::FRng G(Seed);
			HKCase::FPlan P;
			HKCase::PlanCase(G, Sampler, P);
			FString Why;
			T.Check(MxSamePlan(P, V->AsObject(), Why), FString::Printf(TEXT("seed %d: %s"), Seed, *Why));
			Seed++;
		}
	};
	Plans(TEXT("zonedPlans"), MxZoned);
	Plans(TEXT("flatPlans"), MxFlat);
	{
		FMxTally T(*this, TEXT("planCase options"));
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("optionPlans")))
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			HKCase::FPlanOptions Opt;
			Opt.MinSeparation = HKJson::Num(O, TEXT("minSeparation"));
			const TArray<double> H = HKJson::Nums(O, TEXT("hostiles"));
			Opt.HostilesMin = (int32)H[0]; Opt.HostilesMax = (int32)H[1];
			Opt.MaxTries = (int32)HKJson::Num(O, TEXT("maxTries"));
			HKCase::FRng G((uint32)HKJson::Num(O, TEXT("seed")));
			HKCase::FPlan P;
			HKCase::PlanCase(G, MxZoned, P, nullptr, Opt);
			FString Why;
			T.Check(MxSamePlan(P, HKJson::Obj(O, TEXT("plan")), Why), Why);
		}
		HKCase::FRng G(1);
		HKCase::FPlan P;
		FString Error;
		HKCase::FPlanOptions Opt;
		Opt.MaxTries = 20;
		HKCase::PlanCase(G, [](HKCase::FRng&, const FString&, HKCase::FSpot&) { return false; }, P, &Error, Opt);
		T.Check(Error == HKJson::Str(R, TEXT("planError")), FString::Printf(TEXT("error '%s' vs web '%s'"), *Error, *HKJson::Str(R, TEXT("planError"))));
	}
	{
		FMxTally T(*this, TEXT("hostileHitChance"));
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("hitChance")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
			const double Got = HKCase::HostileHitChance(MxNum(A[0]), MxNum(A[1]), MxStr(A[2]));
			T.Check(Got == MxNum(A[3]), FString::Printf(TEXT("(%g, %g, %s): %.17g vs web %.17g"), MxNum(A[0]), MxNum(A[1]), *MxStr(A[2]), Got, MxNum(A[3])));
		}
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("hitChanceDefault")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
			T.Check(HKCase::HostileHitChance(MxNum(A[0])) == MxNum(A[1]), FString::Printf(TEXT("default (%g)"), MxNum(A[0])));
		}
	}
	{
		FMxTally T(*this, TEXT("applyHit / damagePlayer"));
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("applyHit")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
			int32 H = (int32)MxNum(A[0]); bool bA = MxBool(A[1]);
			HKCase::ApplyHit(H, bA, MxBool(A[2]));
			T.Check(H == (int32)MxNum(A[3]) && bA == MxBool(A[4]), FString::Printf(TEXT("applyHit(%d, %d)"), (int32)MxNum(A[0]), MxBool(A[2]) ? 1 : 0));
		}
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("damage")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
			int32 H = (int32)MxNum(A[0]); bool bA = MxBool(A[1]);
			if (A[2]->Type == EJson::Null) HKCase::DamagePlayer(H, bA); else HKCase::DamagePlayer(H, bA, (int32)MxNum(A[2]));
			T.Check(H == (int32)MxNum(A[3]) && bA == MxBool(A[4]), FString::Printf(TEXT("damagePlayer(%d)"), (int32)MxNum(A[0])));
		}
	}
	{
		FMxTally T(*this, TEXT("outcome"));
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("outcome")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
			TArray<HKCase::FRoleAlive> L;
			for (const TSharedPtr<FJsonValue>& X : A[0]->AsArray()) L.Add({MxStr(X->AsArray()[0]), MxBool(X->AsArray()[1])});
			bool bPlayer = true;
			const bool bHasPlayer = A[1]->Type == EJson::Array;
			if (bHasPlayer) bPlayer = MxBool(A[1]->AsArray()[1]);
			const FString Got = HKCase::Outcome(L, bHasPlayer ? &bPlayer : nullptr), Want = A[2]->Type == EJson::Null ? FString() : MxStr(A[2]);
			T.Check(Got == Want, FString::Printf(TEXT("'%s' vs web '%s'"), *Got, *Want));
		}
	}
	{
		FMxTally T(*this, TEXT("insidePolygon"));
		const TSharedPtr<FJsonObject> I = HKJson::Obj(R, TEXT("inside"));
		const TArray<FHKPt> Poly = HKJson::Pts(HKJson::Arr(I, TEXT("polygon")));
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(I, TEXT("tests")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
			T.Check(HKCase::InsidePolygon(MxNum(A[0]), MxNum(A[1]), Poly) == MxBool(A[2]), FString::Printf(TEXT("(%.4f, %.4f)"), MxNum(A[0]), MxNum(A[1])));
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKMissionSamplerTest, "Haarklous.Mission.Sampler", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKMissionSamplerTest::RunTest(const FString& Parameters)
{
	FHKData& Data = FHKData::Get();
	if (!TestTrue(TEXT("data loaded"), Data.bLoaded)) { AddError(Data.Error); return false; }
	const TSharedPtr<FJsonObject> R = MxLoadRef(*this);
	if (!R) return false;
	const TSharedPtr<FJsonObject> S = HKJson::Obj(R, TEXT("sampler"));
	// game.json must give the mission the same spawn data the web page used.
	{
		FMxTally T(*this, TEXT("mission data from game.json"));
		const TArray<FHKSpawnArea> Areas = HKMissionData::SpawnAreas();
		const TArray<TSharedPtr<FJsonValue>>& WA = HKJson::Arr(S, TEXT("spawnAreas"));
		T.Check(Areas.Num() == WA.Num(), TEXT("spawn area count"));
		for (int32 i = 0; i < FMath::Min(Areas.Num(), WA.Num()); i++)
		{
			const TSharedPtr<FJsonObject> O = WA[i]->AsObject();
			const TArray<FHKPt> P = HKJson::Pts(HKJson::Arr(O, TEXT("points")));
			bool bOk = Areas[i].Name == HKJson::Str(O, TEXT("name")) && Areas[i].Points.Num() == P.Num() && MxNear(Areas[i].CeilingHeight, HKJson::Num(O, TEXT("ceilingHeight")), 1e-12);
			for (int32 k = 0; bOk && k < P.Num(); k++) bOk &= Areas[i].Points[k].X == P[k].X && Areas[i].Points[k].Z == P[k].Z;
			T.Check(bOk, Areas[i].Name);
		}
		const TArray<FHKStairSpawn> Stairs = HKMissionData::StairSpawns();
		const TArray<TSharedPtr<FJsonValue>>& WS = HKJson::Arr(S, TEXT("stairSpawns"));
		T.Check(Stairs.Num() == WS.Num(), FString::Printf(TEXT("stair spawn count %d vs web %d"), Stairs.Num(), WS.Num()));
		for (int32 i = 0; i < FMath::Min(Stairs.Num(), WS.Num()); i++)
			T.Check(Stairs[i].Name == HKJson::Str(WS[i]->AsObject(), TEXT("name")) && Stairs[i].Y == HKJson::Num(WS[i]->AsObject(), TEXT("y")), Stairs[i].Name);
		const FVector E = HKMissionData::Entrance(), WE = HKJson::Vec(S, TEXT("entrance"));
		T.Check(E.Equals(WE, 1e-12), TEXT("entrance"));
		const TArray<FHKMissionDoor> Doors = HKMissionData::Doors();
		const TArray<TSharedPtr<FJsonValue>>& WD = HKJson::Arr(S, TEXT("doors"));
		T.Check(Doors.Num() == WD.Num(), TEXT("door count"));
		for (int32 i = 0; i < FMath::Min(Doors.Num(), WD.Num()); i++) T.Check(Doors[i].Name == MxStr(WD[i]), Doors[i].Name);
	}
	FHKWalkWorld& W = Data.WalkWorld;
	const TArray<FHKObstacle> SavedDynamic = W.Dynamic;
	TArray<FHKObstacle> Base = MxObstaclesOf(S->TryGetField(TEXT("dynamic")));
	W.SetDynamic(Base);
	FHKMission* MPtr = nullptr;
	FHKMission M(MxDataContext(&Base, &MPtr));
	MPtr = &M;
	{
		FMxTally T(*this, TEXT("explain (entrance, door sweep, canStand)"));
		const TArray<TSharedPtr<FJsonValue>>& List = HKJson::Arr(R, TEXT("explain"));
		TArray<FVector2D> Points;
		for (const TSharedPtr<FJsonValue>& V : List) Points.Add(FVector2D(MxNum(V->AsArray()[0]), MxNum(V->AsArray()[1])));
		const TArray<FHKMission::FExplain> All = M.ExplainAll(Points);
		for (int32 i = 0; i < List.Num(); i++)
		{
			const TArray<TSharedPtr<FJsonValue>>& A = List[i]->AsArray();
			const FHKMission::FExplain& E = All[i];
			T.Check(MxNear(E.Entrance, MxNum(A[2]), 1e-12) && MxNear(E.Door, MxNum(A[3]), 1e-9) && E.bStand == MxBool(A[4]),
				FString::Printf(TEXT("(%.4f, %.4f): %.6f %.6f %d vs web %.6f %.6f %d"), MxNum(A[0]), MxNum(A[1]), E.Entrance, E.Door, E.bStand ? 1 : 0, MxNum(A[2]), MxNum(A[3]), MxBool(A[4]) ? 1 : 0));
		}
	}
	{
		FMxTally T(*this, TEXT("probeArea"));
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("probe")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
			const int32 Got = M.ProbeArea(MxStr(A[0]), 300);
			T.Check(Got == (int32)MxNum(A[1]), FString::Printf(TEXT("%s: %d vs web %d"), *MxStr(A[0]), Got, (int32)MxNum(A[1])));
		}
	}
	{
		FMxTally T(*this, TEXT("planCase with the real sampler"));
		const TArray<TSharedPtr<FJsonValue>>& WP = HKJson::Arr(R, TEXT("samplePlans"));
		const TArray<HKCase::FPlan> Plans = M.SamplePlans(WP.Num());
		for (int32 i = 0; i < WP.Num(); i++)
		{
			FString Why;
			T.Check(MxSamePlan(Plans[i], WP[i]->AsObject(), Why), FString::Printf(TEXT("seed %d: %s"), i + 1, *Why));
		}
	}
	W.SetDynamic(SavedDynamic);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
namespace
{
	// Replays the recorded missions: same camera, stance and calls per frame; the world's raycast answers come from the
	// recording (the C++ code tests its own mannequins); everything the web recorded after the frame must match.
	struct FMxReplay
	{
		FAutomationTestBase& Test;
		FHKMission* M = nullptr;
		FVector CamPos = FVector::ZeroVector;
		FQuat CamQuat = FQuat::Identity;
		double Aspect = 1, Fov = 70, SensPad = 1, SensMouse = 1;
		FString Stance = TEXT("stand");
		TArray<FHKObstacle> Base;
		TArray<TSharedPtr<FJsonValue>> Rays;
		int32 RayIdx = 0;
		TArray<bool> DoorAnswers;
		int32 DoorIdx = 0;
		HKCase::FRng Rng = HKCase::FRng(1);
		FString RayError;
		explicit FMxReplay(FAutomationTestBase& T) : Test(T) {}

		FHKMissionContext Context()
		{
			FHKMissionContext Ctx = MxDataContext(&Base, &M);
			Ctx.CameraPosition = [this]() { return CamPos; };
			Ctx.CameraQuaternion = [this]() { return CamQuat; };
			Ctx.CameraAspect = [this]() { return Aspect; };
			Ctx.GetFov = [this]() { return Fov; };
			Ctx.SetFov = [this](double V) { Fov = V; };
			Ctx.Stance = [this]() { return Stance; };
			Ctx.SetPose = [](double, double, double, double, double) {};
			Ctx.Leave = []() {};
			Ctx.UseDoorAhead = [this]() { return DoorIdx < DoorAnswers.Num() ? DoorAnswers[DoorIdx++] : false; };
			// game-main.js setSens: round(min(3, max(.25, v)) * 100) / 100; step(k, d) = set(k, k + d * .1)
			auto Set = [this](const FString& K, double V) { const double R = HK::JsRound(FMath::Min(3.0, FMath::Max(.25, V)) * 100) / 100; (K == TEXT("pad") ? SensPad : SensMouse) = R; };
			Ctx.GetSensitivity = [this](const FString& K) { return K == TEXT("pad") ? SensPad : SensMouse; };
			Ctx.SetSensitivity = Set;
			Ctx.StepSensitivity = [this, Set](const FString& K, int32 D) { Set(K, (K == TEXT("pad") ? SensPad : SensMouse) + D * .1); };
			Ctx.Random = [this]() { return Rng(); };
			Ctx.Shoot = [this](const FVector& O, const FVector& D, double Far, FHKShotHit& Hit) -> bool
			{
				if (RayIdx >= Rays.Num()) { if (RayError.IsEmpty()) RayError = TEXT("more raycasts than the web made"); return false; }
				const TSharedPtr<FJsonObject> R = Rays[RayIdx++]->AsObject();
				const FVector WO = HKJson::Vec(R, TEXT("o")), WD = HKJson::Vec(R, TEXT("d"));
				if (RayError.IsEmpty() && (!O.Equals(WO, 1e-9) || !D.Equals(WD, 1e-9) || !MxNear(Far, HKJson::Num(R, TEXT("far")), 1e-9)))
					RayError = FString::Printf(TEXT("ray %d: origin %s dir %s far %.6f vs web %s %s %.6f"), RayIdx - 1, *O.ToString(), *D.ToString(), Far, *WO.ToString(), *WD.ToString(), HKJson::Num(R, TEXT("far")));
				const TArray<double> Wh = HKJson::Nums(R, TEXT("w"));
				if (Wh.Num() < 7) return false;
				Hit.Distance = Wh[0];
				Hit.Point = FVector(Wh[1], Wh[2], Wh[3]);
				Hit.Normal = FVector(Wh[4], Wh[5], Wh[6]);
				return true;
			};
			return Ctx;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKMissionReplayTest, "Haarklous.Mission.Replay", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKMissionReplayTest::RunTest(const FString& Parameters)
{
	FHKData& Data = FHKData::Get();
	if (!TestTrue(TEXT("data loaded"), Data.bLoaded)) { AddError(Data.Error); return false; }
	const TSharedPtr<FJsonObject> Ref = MxLoadRef(*this);
	if (!Ref) return false;
	FHKWalkWorld& W = Data.WalkWorld;
	const TArray<FHKObstacle> SavedDynamic = W.Dynamic;
	FMxReplay Rp(*this);
	// One mission object for all scenarios, like the one case game of the web page (some state carries over).
	FHKMission M(Rp.Context());
	Rp.M = &M;
	TArray<FHKMission::FRayLog> RayLog;
	M.RayLog = &RayLog;
	int32 Events[4] = {0};   // hostile shots that hit the player, kills, finished cases, actor hits
	for (const TSharedPtr<FJsonValue>& ScV : HKJson::Arr(Ref, TEXT("scenarios")))
	{
		const TSharedPtr<FJsonObject> Sc = ScV->AsObject();
		const FString Name = HKJson::Str(Sc, TEXT("name"));
		FMxTally T(*this, TEXT("replay ") + Name);
		T.MaxErrors = 15;
		Rp.Rng = HKCase::FRng((uint32)HKJson::Num(Sc, TEXT("seed")));
		Rp.Aspect = HKJson::Num(Sc, TEXT("aspect"), 1);
		Rp.Fov = HKJson::Num(Sc, TEXT("fov0"), 70);
		const TArray<double> Sens = HKJson::Nums(Sc, TEXT("sens"));
		if (Sens.Num() == 2) { Rp.SensPad = Sens[0]; Rp.SensMouse = Sens[1]; }
		int32 FirstBad = -1, FrameNo = 0;
		const TArray<TSharedPtr<FJsonValue>>& Frames = HKJson::Arr(Sc, TEXT("frames"));
		for (const TSharedPtr<FJsonValue>& FV : Frames)
		{
			const TSharedPtr<FJsonObject> F = FV->AsObject();
			const int32 Bad0 = T.Bad;
			const FString At = FString::Printf(TEXT("frame %d"), FrameNo);
			const TArray<double> Cam = HKJson::Nums(F, TEXT("cam"));
			Rp.CamPos = FVector(Cam[0], Cam[1], Cam[2]);
			Rp.CamQuat = FQuat(Cam[3], Cam[4], Cam[5], Cam[6]);
			Rp.Stance = HKJson::Str(F, TEXT("stance"), TEXT("stand"));
			Rp.Rays = HKJson::Arr(F, TEXT("rays"));
			Rp.RayIdx = 0;
			Rp.RayError.Empty();
			Rp.DoorAnswers.Reset();
			Rp.DoorIdx = 0;
			for (const TSharedPtr<FJsonValue>& D : HKJson::Arr(F, TEXT("doors"))) Rp.DoorAnswers.Add(MxBool(D));
			if (const TSharedPtr<FJsonValue> PD = F->TryGetField(TEXT("planDynamic")); PD.IsValid() && PD->Type == EJson::Array) Rp.Base = MxObstaclesOf(PD);
			RayLog.Reset();
			for (const TSharedPtr<FJsonValue>& CV : HKJson::Arr(F, TEXT("calls")))
			{
				const TArray<TSharedPtr<FJsonValue>>& C = CV->AsArray();
				const FString Fn = MxStr(C[0]);
				const TArray<TSharedPtr<FJsonValue>>& Args = C[1]->AsArray();
				const TSharedPtr<FJsonValue> Ret = C[2];
				auto CheckRet = [&](bool bGot) { T.Check(Ret->Type != EJson::Boolean || Ret->AsBool() == bGot, FString::Printf(TEXT("%s: %s(...) returned %d"), *At, *Fn, bGot ? 1 : 0)); };
				if (Fn == TEXT("start")) M.Start(Args.Num() && MxBool(Args[0]));
				else if (Fn == TEXT("stop")) M.Stop();
				else if (Fn == TEXT("pause")) M.Pause();
				else if (Fn == TEXT("padButton")) CheckRet(M.PadButton(MxStr(Args[0])));
				else if (Fn == TEXT("triggers")) M.Triggers(MxNum(Args[0]), MxNum(Args[1]));
				else if (Fn == TEXT("mouse")) CheckRet(M.Mouse((int32)MxNum(Args[0]), MxBool(Args[1])));
				else if (Fn == TEXT("key"))
				{
					const TSharedPtr<FJsonObject> E = Args[0]->AsObject();
					CheckRet(M.Key(HKJson::Str(E, TEXT("key")), HKJson::Str(E, TEXT("code")), HKJson::Bool(E, TEXT("repeat"))));
				}
				else if (Fn == TEXT("invulnerable")) M.bInvulnerable = MxBool(Args[0]);
				else T.Check(false, At + TEXT(": unknown call ") + Fn);
			}
			M.Tick(HKJson::Num(F, TEXT("dt")) / 1000);
			// Raycasts: the same number, the same rays, the same first hit.
			T.Check(Rp.RayError.IsEmpty() && Rp.RayIdx == Rp.Rays.Num(), FString::Printf(TEXT("%s: %d of %d raycasts used %s"), *At, Rp.RayIdx, Rp.Rays.Num(), *Rp.RayError));
			for (int32 i = 0; i < FMath::Min(RayLog.Num(), Rp.Rays.Num()); i++)
			{
				const TSharedPtr<FJsonValue> FirstV = Rp.Rays[i]->AsObject()->TryGetField(TEXT("f"));
				const FHKMission::FRayLog& L = RayLog[i];
				if (!FirstV.IsValid() || FirstV->Type != EJson::Array) { T.Check(!L.bHit, FString::Printf(TEXT("%s: ray %d hit %s, web hit nothing"), *At, i, *L.ActorId)); continue; }
				const TArray<TSharedPtr<FJsonValue>>& Fi = FirstV->AsArray();
				const bool bOk = L.bHit && L.ActorId == MxStr(Fi[0]) && L.bHead == MxBool(Fi[1]) && MxNear(L.Distance, MxNum(Fi[2]), 1e-9);
				T.Check(bOk, FString::Printf(TEXT("%s: ray %d hit '%s' head %d at %.9f vs web '%s' head %d at %.9f"), *At, i, *L.ActorId, L.bHead ? 1 : 0, L.Distance, *MxStr(Fi[0]), MxBool(Fi[1]) ? 1 : 0, MxNum(Fi[2])));
				if (L.bHit && !L.ActorId.IsEmpty()) Events[3]++;
			}
			T.Check(M.bTickedLastFrame == MxBool(F->TryGetField(TEXT("ticked"))), At + TEXT(": tick ran"));
			// A new plan: the same actors at the same places.
			if (const TSharedPtr<FJsonObject>* PlanJ = nullptr; F->TryGetObjectField(TEXT("plan"), PlanJ))
			{
				FString Why;
				T.Check(MxSamePlan(M.Plan, *PlanJ, Why), At + TEXT(": plan ") + Why);
			}
			// The state after the frame.
			const TSharedPtr<FJsonObject> S = HKJson::Obj(F, TEXT("s"));
			T.Check(M.GetState() == HKJson::Str(S, TEXT("st")), FString::Printf(TEXT("%s: state %s vs web %s"), *At, *M.GetState(), *HKJson::Str(S, TEXT("st"))));
			T.Check(M.IsActive() == (HKJson::Num(S, TEXT("act")) != 0), At + TEXT(": active"));
			T.Check(M.PlayerHealth == (int32)HKJson::Num(S, TEXT("hp")) && M.bPlayerAlive == (HKJson::Num(S, TEXT("alive")) != 0), FString::Printf(TEXT("%s: health %d vs web %d"), *At, M.PlayerHealth, (int32)HKJson::Num(S, TEXT("hp"))));
			T.Check(MxNear(Rp.Fov, HKJson::Num(S, TEXT("fov")), 1e-9), FString::Printf(TEXT("%s: fov %.9f vs web %.9f"), *At, Rp.Fov, HKJson::Num(S, TEXT("fov"))));
			if (M.IsActive())
			{
				const TArray<TSharedPtr<FJsonValue>>& H = HKJson::Arr(S, TEXT("hud"));
				const FString Mine[9] = {M.Hud.Id, M.Hud.Objective, M.Hud.Time, M.Hud.Mag, M.Hud.Reserve, M.Hud.Reloading, M.FeedText, M.FeedKind, M.Hud.Health};
				const TCHAR* Names[9] = {TEXT("case id"), TEXT("objective"), TEXT("clock"), TEXT("magazine"), TEXT("reserve"), TEXT("reloading"), TEXT("feed"), TEXT("feed kind"), TEXT("health text")};
				for (int32 i = 0; i < 9 && i < H.Num(); i++) T.Check(Mine[i] == MxStr(H[i]), FString::Printf(TEXT("%s: %s '%s' vs web '%s'"), *At, Names[i], *Mine[i], *MxStr(H[i])));
				const TArray<double> Hn = HKJson::Nums(S, TEXT("hn"));
				if (Hn.Num() == 6)
				{
					T.Check(MxNear(M.Hud.Gap, Hn[0], 1e-9), FString::Printf(TEXT("%s: crosshair gap %.9f vs web %.9f"), *At, M.Hud.Gap, Hn[0]));
					T.Check(FMath::Abs(M.Hud.CrossOpacity - Hn[1]) < 1e-5, FString::Printf(TEXT("%s: crosshair opacity %.6f vs web %.6f"), *At, M.Hud.CrossOpacity, Hn[1]));
					T.Check(M.Hud.HitMarker == Hn[2], At + TEXT(": hit marker"));
					T.Check(FMath::Abs(M.Hud.Damage - Hn[3]) < 1e-5, FString::Printf(TEXT("%s: damage tint %.6f vs web %.6f"), *At, M.Hud.Damage, Hn[3]));
					T.Check(FMath::Abs(M.Hud.ArrowOpacity - Hn[4]) < 1e-5, FString::Printf(TEXT("%s: damage arrow %.6f vs web %.6f"), *At, M.Hud.ArrowOpacity, Hn[4]));
					T.Check(FMath::Abs(M.Hud.ArrowAngle - Hn[5]) < 1e-4, FString::Printf(TEXT("%s: arrow angle %.6f vs web %.6f"), *At, M.Hud.ArrowAngle, Hn[5]));
				}
				T.Check(M.PanelState == HKJson::Str(S, TEXT("pan")), FString::Printf(TEXT("%s: panel '%s' vs web '%s'"), *At, *M.PanelState, *HKJson::Str(S, TEXT("pan"))));
				const FString WebSens = HKJson::Str(S, TEXT("sens"));
				if (!WebSens.IsEmpty())
				{
					const FString Mine2 = HKCase::JsToFixed(Rp.SensPad, 2) + TEXT("×|") + HKCase::JsToFixed(Rp.SensMouse, 2) + TEXT("×");
					T.Check(Mine2 == WebSens, FString::Printf(TEXT("%s: sensitivity '%s' vs web '%s'"), *At, *Mine2, *WebSens));
				}
			}
			const TArray<double> Gun = HKJson::Nums(S, TEXT("gun"));
			if (Gun.Num() == 9)
			{
				const FHKViewmodel& V = M.Viewmodel;
				const double Mine[9] = {V.Gun->Position.X, V.Gun->Position.Y, V.Gun->Position.Z, V.Gun->Rot().X, V.Gun->Rot().Y, V.Gun->Rot().Z, V.Slide->Position.Z, V.Flash->bVisible ? 1.0 : 0.0, V.Flash->Rot().Z};
				bool bOk = true;
				for (int32 i = 0; i < 9; i++) bOk &= MxNear(Mine[i], Gun[i], 1e-9);
				T.Check(bOk, FString::Printf(TEXT("%s: viewmodel (%.6f %.6f %.6f | %.6f %.6f %.6f | %.4f %d %.4f) vs web (%.6f %.6f %.6f | %.6f %.6f %.6f | %.4f %d %.4f)"), *At,
					Mine[0], Mine[1], Mine[2], Mine[3], Mine[4], Mine[5], Mine[6], (int32)Mine[7], Mine[8], Gun[0], Gun[1], Gun[2], Gun[3], Gun[4], Gun[5], Gun[6], (int32)Gun[7], Gun[8]));
			}
			const TArray<double> Fl = HKJson::Nums(S, TEXT("fl"));
			if (Fl.Num() == 8)
			{
				const FHKMission::FFlashLight& A = M.FlashLight;
				const FHKMission::FFlashLight& B = M.EnemyFlashLight;
				T.Check(A.Intensity == Fl[0] && A.Position.Equals(FVector(Fl[1], Fl[2], Fl[3]), 1e-9) && B.Intensity == Fl[4] && B.Position.Equals(FVector(Fl[5], Fl[6], Fl[7]), 1e-9),
					FString::Printf(TEXT("%s: flash lights %.0f %s / %.0f %s vs web %.0f / %.0f"), *At, A.Intensity, *A.Position.ToString(), B.Intensity, *B.Position.ToString(), Fl[0], Fl[4]));
			}
			const TArray<TSharedPtr<FJsonValue>>& WA = HKJson::Arr(S, TEXT("a"));
			T.Check(WA.Num() == M.Actors.Num(), FString::Printf(TEXT("%s: %d actors vs web %d"), *At, M.Actors.Num(), WA.Num()));
			for (int32 i = 0; i < FMath::Min(WA.Num(), M.Actors.Num()); i++)
			{
				const TArray<TSharedPtr<FJsonValue>>& X = WA[i]->AsArray();
				const FHKMissionActor& A = *M.Actors[i];
				const double Mine[15] = {0, A.bAlive ? 1.0 : 0.0, (double)A.Health, A.Root->Rot().Y, A.Fall, A.HitFlash, A.FlashT, A.bEngaged ? 1.0 : 0.0, A.FireIn, A.bSees ? 1.0 : 0.0,
					A.Body->Position.Y, A.Body->Position.Z, A.Body->Rot().X, A.MuzzleFlash ? (A.MuzzleFlash->bVisible ? 1.0 : 0.0) : -1.0, A.MuzzleFlash ? A.MuzzleFlash->Rot().Y : 0.0};
				const TCHAR* Field[15] = {TEXT("id"), TEXT("alive"), TEXT("health"), TEXT("turn"), TEXT("fall"), TEXT("hitFlash"), TEXT("flashT"), TEXT("engaged"), TEXT("fireIn"), TEXT("sees"),
					TEXT("body y"), TEXT("body z"), TEXT("body tilt"), TEXT("muzzle visible"), TEXT("muzzle spin")};
				FString Bad;
				if (A.Id != MxStr(X[0])) Bad += TEXT(" id");
				for (int32 k = 1; k < 15 && k < X.Num(); k++) if (!MxNear(Mine[k], MxNum(X[k]), 1e-8)) Bad += FString::Printf(TEXT(" %s %.9f vs web %.9f;"), Field[k], Mine[k], MxNum(X[k]));
				T.Check(Bad.IsEmpty(), FString::Printf(TEXT("%s: %s%s"), *At, *A.Id, *Bad));
			}
			if (T.Bad > Bad0 && FirstBad < 0) FirstBad = FrameNo;
			// Counters for the report.
			if (FrameNo > 0)
			{
				const TSharedPtr<FJsonObject> Prev = HKJson::Obj(Frames[FrameNo - 1]->AsObject(), TEXT("s"));
				if (HKJson::Num(S, TEXT("hp")) < HKJson::Num(Prev, TEXT("hp"))) Events[0]++;
				if ((HKJson::Str(S, TEXT("st")) == TEXT("success") || HKJson::Str(S, TEXT("st")) == TEXT("fail")) && HKJson::Str(Prev, TEXT("st")) != HKJson::Str(S, TEXT("st"))) Events[2]++;
			}
			FrameNo++;
		}
		AddInfo(FString::Printf(TEXT("replay %s: %d frames%s"), *Name, Frames.Num(), FirstBad >= 0 ? *FString::Printf(TEXT(", first difference at frame %d"), FirstBad) : TEXT(", no difference")));
	}
	AddInfo(FString::Printf(TEXT("replay covered %d player hits taken, %d finished cases, %d shots on mannequins"), Events[0], Events[2], Events[3]));
	M.RayLog = nullptr;
	W.SetDynamic(SavedDynamic);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKMissionRigsTest, "Haarklous.Mission.Rigs", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKMissionRigsTest::RunTest(const FString& Parameters)
{
	const FString Dir = FPaths::Combine(FHKData::DataDir(), TEXT("rigs"));
	{
		FHKRig R;
		FString Error;
		TestTrue(TEXT("hostile rig loads ") + Error, R.LoadFromJson(Dir / TEXT("mission_hostile.json"), &Error));
		FHKNode* Body = R.Find(TEXT("body"));
		FHKNode* Muzzle = R.Find(TEXT("muzzleFlash"));
		TestTrue(TEXT("hostile body and muzzle flash"), Body && Muzzle && Muzzle->Parent == Body && !Body->MeshKey.IsNone() && !Muzzle->MeshKey.IsNone() && !Muzzle->bVisible);
		if (Muzzle) TestTrue(TEXT("muzzle flash rest pose"), Muzzle->Position.Equals(FVector(0, 1.36, -.67), 1e-9) && Muzzle->Quat().Equals(HK::EulerToQuat(-HK::Pi / 2, 0, 0), 1e-9));
	}
	{
		FHKRig R;
		FString Error;
		TestTrue(TEXT("hostage rig loads ") + Error, R.LoadFromJson(Dir / TEXT("mission_hostage.json"), &Error));
		TestTrue(TEXT("hostage body"), R.Find(TEXT("body")) && !R.Find(TEXT("body"))->MeshKey.IsNone());
	}
	{
		FHKViewmodel V;
		TestTrue(TEXT("viewmodel rig loads"), V.bLoaded);
		TestTrue(TEXT("viewmodel nodes"), V.Gun && V.Slide && V.Flash && V.Slide->Parent == V.Gun && V.Flash->Parent == V.Gun && !V.Gun->MeshKey.IsNone() && !V.Flash->bVisible);
		TestTrue(TEXT("flash rest pose"), V.Flash->Position.Equals(FVector(0, 0, -.215), 1e-9) && FMath::IsNearlyEqual(V.Flash->Rot().X, -HK::Pi / 2, 1e-12));
		// The viewmodel projects through the world camera like through the web's 62-degree viewmodel camera.
		const FVector S62 = FHKViewmodel::RootScale(62, 1), S43 = FHKViewmodel::RootScale(43, .25);
		TestTrue(TEXT("no correction at 62 degrees"), S62.Equals(FVector(1, 1, 1), 1e-12));
		const FVector P(.12, -.075, -.44), Q(P.X * S43.X, P.Y * S43.Y, P.Z * S43.Z);
		const double Want = (P.X / -P.Z) / FMath::Tan(FMath::DegreesToRadians(62.0) / 2), Got = (Q.X / -Q.Z) / FMath::Tan(FMath::DegreesToRadians(43.0) / 2);
		TestTrue(FString::Printf(TEXT("same screen position (%.9f vs %.9f)"), Got, Want), FMath::IsNearlyEqual(Got, Want, 1e-12));
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// game-main.js lines 408-414 and the canvas mouse handlers, through FHKMission::RouteInput.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKMissionInputTest, "Haarklous.Mission.InputRouting", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKMissionInputTest::RunTest(const FString& Parameters)
{
	FHKData& Data = FHKData::Get();
	if (!TestTrue(TEXT("data loaded"), Data.bLoaded)) return false;
	FHKWalkWorld& W = Data.WalkWorld;
	const TArray<FHKObstacle> SavedDynamic = W.Dynamic;
	TArray<FHKObstacle> Base = SavedDynamic;
	FHKMission* MPtr = nullptr;
	FHKMissionContext Ctx = MxDataContext(&Base, &MPtr);
	FVector Cam(.25, -8, 5.83);
	Ctx.CameraPosition = [&Cam]() { return Cam; };
	Ctx.CameraQuaternion = []() { return FQuat::Identity; };
	double Pad = 1;
	Ctx.GetSensitivity = [&Pad](const FString&) { return Pad; };
	Ctx.StepSensitivity = [&Pad](const FString&, int32 D) { Pad = HK::JsRound(FMath::Min(3.0, FMath::Max(.25, Pad + D * .1)) * 100) / 100; };
	bool bLeft = false;
	Ctx.Leave = [&bLeft]() { bLeft = true; };
	HKCase::FRng Rng(3);
	Ctx.Random = [&Rng]() { return Rng(); };
	FHKMission M(Ctx);
	MPtr = &M;
	auto Frame = [&](std::initializer_list<const TCHAR*> Names, bool bGamepad = false, double L2 = 0, double R2 = 0, bool bLeftEdge = false, bool bRight = false)
	{
		FHKInputFrame In;
		for (const TCHAR* N : Names) In.Pressed.Add(FName(N));
		In.bGamepad = bGamepad; In.L2 = L2; In.R2 = R2;
		In.Mouse.bLeftEdge = bLeftEdge; In.Mouse.bLeft = bLeftEdge; In.Mouse.bRight = bRight;
		const bool bEnded = M.RouteInput(In);
		M.Tick(1.0 / 60);
		return TPair<bool, FHKInputFrame>(bEnded, In);
	};
	TestFalse(TEXT("inactive: nothing routed"), Frame({TEXT("cross")}).Key);
	M.Start(false);
	M.Tick(1.0 / 60);
	TestEqual(TEXT("briefing"), M.GetState(), FString(TEXT("briefing")));
	Frame({TEXT("right")});
	TestEqual(TEXT("D-pad right raises the pad sensitivity on the panel"), Pad, 1.1);
	Frame({TEXT("cross")});
	TestEqual(TEXT("cross begins"), M.GetState(), FString(TEXT("play")));
	const int32 Mag0 = M.Mag;
	Frame({}, false, 0, 0, true);
	TestEqual(TEXT("left mouse fires"), M.Mag, Mag0 - 1);
	Frame({}, true, 0, 1);
	TestEqual(TEXT("R2 fires once per pull"), M.Mag, Mag0 - 2);
	Frame({}, true, 0, 1);
	TestEqual(TEXT("holding R2 does not fire again"), M.Mag, Mag0 - 2);
	Frame({}, true, 0, 0);
	Frame({TEXT("reload")});
	TestTrue(TEXT("R reloads"), M.Reload > 0);
	Frame({}, false, 0, 0, false, true);
	TestTrue(TEXT("right mouse aims"), M.bMouseAim);
	Frame({}, false, 0, 0, false, false);
	TestFalse(TEXT("releasing it stops aiming"), M.bMouseAim);
	Frame({TEXT("cross"), TEXT("circle")});
	TestEqual(TEXT("cross and circle are left to the player while playing"), M.GetState(), FString(TEXT("play")));
	Frame({TEXT("options")});
	TestEqual(TEXT("options pauses"), M.GetState(), FString(TEXT("paused")));
	const TPair<bool, FHKInputFrame> Left = Frame({TEXT("circle"), TEXT("cross")});
	TestTrue(TEXT("circle on the pause panel leaves"), Left.Key && !M.IsActive() && bLeft);
	TestFalse(TEXT("the leaving button is consumed"), Left.Value.Pressed.Contains(FName(TEXT("circle"))) || Left.Value.Pressed.Contains(FName(TEXT("cross"))));
	W.SetDynamic(SavedDynamic);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKMissionNumbersTest, "Haarklous.Mission.Numbers", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKMissionNumbersTest::RunTest(const FString& Parameters)
{
	// Answers of node.js (V8) for the same calls.
	struct FFixed { double V; int32 D; const TCHAR* Js; };
	const FFixed Fixed[] = {{0.25, 1, TEXT("0.3")}, {0.35, 1, TEXT("0.3")}, {1.005, 2, TEXT("1.00")}, {2.5, 0, TEXT("3")}, {59.96, 1, TEXT("60.0")}, {-0.04, 1, TEXT("-0.0")}, {0, 1, TEXT("0.0")},
		{1.45, 1, TEXT("1.4")}, {0.05, 1, TEXT("0.1")}, {0.15, 1, TEXT("0.1")}, {12.345, 2, TEXT("12.35")}, {3, 2, TEXT("3.00")}, {0.125, 2, TEXT("0.13")}, {1.0 / 3, 3, TEXT("0.333")},
		{99.995, 2, TEXT("100.00")}, {0.0625, 3, TEXT("0.063")}};
	for (const FFixed& F : Fixed) TestEqual(FString::Printf(TEXT("(%.17g).toFixed(%d)"), F.V, F.D), HKCase::JsToFixed(F.V, F.D), FString(F.Js));
	struct FClock { double T; const TCHAR* Js; };
	const FClock Clocks[] = {{0, TEXT("00:00.0")}, {0.25, TEXT("00:00.3")}, {5.25, TEXT("00:05.3")}, {59.96, TEXT("00:60.0")}, {60, TEXT("01:00.0")}, {65.25, TEXT("01:05.3")}, {125.05, TEXT("02:05.0")}, {3599.99, TEXT("59:60.0")}};
	for (const FClock& C : Clocks) TestEqual(FString::Printf(TEXT("clock(%g)"), C.T), HKCase::Clock(C.T), FString(C.Js));
	TestEqual(TEXT("hypot(3, 4)"), HKCase::JsHypot(3, 4), 5.0);
	TestEqual(TEXT("hypot(1e-200, 1e-200)"), HKCase::JsHypot(1e-200, 1e-200), 1.414213562373095e-200);
	TestEqual(TEXT("hypot(0.1, 0.2)"), HKCase::JsHypot(.1, .2), 0.223606797749979);
	TestEqual(TEXT("hypot(1.0000001, 2.3)"), HKCase::JsHypot(1.0000001, 2.3), 2.507987280669503);
	return true;
}

#endif
