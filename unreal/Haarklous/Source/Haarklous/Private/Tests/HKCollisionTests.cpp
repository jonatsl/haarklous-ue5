// Automation tests: the C++ port must answer exactly like the web version's own code.
// Reference answers come from tools/web-export/reference-tests.js (Content/Data/tests/collision.json).
// Run: tools\run-tests.ps1  (UnrealEditor-Cmd ... -ExecCmds="Automation RunTests Haarklous")
#include "Misc/AutomationTest.h"
#include "HKData.h"
#include "HKMath.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TSharedPtr<FJsonObject> LoadRef(FAutomationTestBase& Test)
	{
		FString Error;
		TSharedPtr<FJsonObject> J = HKJson::LoadFile(FHKData::DataDir() / TEXT("tests") / TEXT("collision.json"), &Error);
		if (!J) Test.AddError(Error);
		return J;
	}
	bool Near(double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol || (A >= 1e8 && B >= 1e8) || (!FMath::IsFinite(A) && B >= 1e8); }
	// Reports how many of N cases matched and lists the first few failures.
	struct FTally
	{
		FAutomationTestBase& Test; FString Name; int32 Total = 0, Bad = 0;
		FTally(FAutomationTestBase& T, const FString& N) : Test(T), Name(N) {}
		void Check(bool bOk, const FString& What) { Total++; if (!bOk) { if (Bad < 8) Test.AddError(Name + TEXT(": ") + What); Bad++; } }
		~FTally() { Test.AddInfo(FString::Printf(TEXT("%s: %d / %d match the web version"), *Name, Total - Bad, Total)); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKMathTest, "Haarklous.Core.Math", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKMathTest::RunTest(const FString& Parameters)
{
	// Axis conversion: web +x east-ish stays +X, web +z becomes +Y, web +y (up) becomes +Z, metres -> cm.
	TestEqual(TEXT("ToUE"), HK::ToUE(FVector(1, 2, 3)), FVector(100, 300, 200));
	TestEqual(TEXT("FromUE"), HK::FromUE(FVector(100, 300, 200)), FVector(1, 2, 3));
	// A web yaw of +90 degrees (about +y) maps web +x to web -z; in Unreal that is +X -> -Y.
	const FQuat W = HK::AxisAngle(FVector(0, 1, 0), HK::Pi / 2);
	const FVector WebRot = W.RotateVector(FVector(1, 0, 0));
	TestTrue(TEXT("web yaw"), WebRot.Equals(FVector(0, 0, -1), 1e-9));
	const FVector UeRot = HK::QuatToUE(W).RotateVector(FVector(1, 0, 0));
	TestTrue(TEXT("converted yaw"), UeRot.Equals(HK::DirToUE(WebRot), 1e-9));
	// Converting a rotated, scaled, translated point must commute with the conversion.
	const FQuat Q = HK::EulerToQuat(.3, -1.1, .7, TEXT("YXZ"));
	const FVector P(1.5, -2, .25), S(1, 2, 3), V(.4, .5, -.6);
	const FVector WebP = FHKMat::Compose(P, Q, S).TransformPoint(V);
	const FVector UeP = HK::TransformToUE(P, Q, S).TransformPosition(HK::ToUE(V));
	TestTrue(TEXT("transform commutes"), UeP.Equals(HK::ToUE(WebP), 1e-6));
	// Euler round trip in every order.
	for (const TCHAR* O : {TEXT("XYZ"), TEXT("YXZ"), TEXT("ZXY"), TEXT("ZYX"), TEXT("YZX"), TEXT("XZY")})
	{
		const FVector E = HK::QuatToEuler(HK::EulerToQuat(.2, -.4, .9, O), O);
		TestTrue(FString::Printf(TEXT("euler %s"), O), E.Equals(FVector(.2, -.4, .9), 1e-9));
	}
	// Matrix inverse.
	const FHKMat M = FHKMat::Compose(P, Q, S), I = M * M.Inverse();
	bool bIdentity = true;
	for (int32 i = 0; i < 16; i++) bIdentity &= FMath::Abs(I.E[i] - ((i % 5 == 0) ? 1.0 : 0.0)) < 1e-9;
	TestTrue(TEXT("inverse"), bIdentity);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKTerrainTest, "Haarklous.World.TerrainAndFrame", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKTerrainTest::RunTest(const FString& Parameters)
{
	FHKData& Data = FHKData::Get();
	if (!TestTrue(TEXT("data loaded"), Data.bLoaded)) { AddError(Data.Error); return false; }
	const TSharedPtr<FJsonObject> R = LoadRef(*this);
	if (!R) return false;
	{
		FTally T(*this, TEXT("heightWorld"));
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("heights")))
		{
			const TArray<double> A = HKJson::Nums(V);
			const double H = Data.HeightWorld(A[0], A[1]);
			T.Check(Near(H, A[2], 1e-9), FString::Printf(TEXT("(%.3f, %.3f): %.6f vs web %.6f"), A[0], A[1], H, A[2]));
		}
	}
	{
		FTally T(*this, TEXT("toLocal/toWorld"));
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("local")))
		{
			const TArray<double> A = HKJson::Nums(V);
			const FVector2D L = Data.ToLocal(A[0], A[1]), W = Data.ToWorld(L.X, L.Y);
			T.Check(Near(L.X, A[2], 1e-9) && Near(L.Y, A[3], 1e-9) && Near(W.X, A[4], 1e-9) && Near(W.Y, A[5], 1e-9), FString::Printf(TEXT("(%.3f, %.3f)"), A[0], A[1]));
		}
	}
	{
		FTally T(*this, TEXT("surfaceAt"));
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("surface")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
			const FString S = Data.SurfaceAt(A[0]->AsNumber(), A[1]->AsNumber());
			T.Check(S == A[2]->AsString(), FString::Printf(TEXT("(%.2f, %.2f): %s vs web %s"), A[0]->AsNumber(), A[1]->AsNumber(), *S, *A[2]->AsString()));
		}
	}
	{
		FTally T(*this, TEXT("trafficDistance/greenery"));
		const TArray<TSharedPtr<FJsonValue>>& Tr = HKJson::Arr(R, TEXT("traffic"));
		const TArray<TSharedPtr<FJsonValue>>& Gr = HKJson::Arr(R, TEXT("green"));
		for (int32 i = 0; i < Tr.Num(); i++)
		{
			const TArray<double> A = HKJson::Nums(Tr[i]), B = HKJson::Nums(Gr[i]);
			T.Check(Near(Data.TrafficDistance(A[0], A[1]), A[2], 1e-9) && Near(Data.Greenery(B[0], B[1]), B[2], 1e-9), FString::Printf(TEXT("(%.2f, %.2f)"), A[0], A[1]));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKCollisionTest, "Haarklous.World.WalkCollision", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKCollisionTest::RunTest(const FString& Parameters)
{
	FHKData& Data = FHKData::Get();
	if (!TestTrue(TEXT("data loaded"), Data.bLoaded)) { AddError(Data.Error); return false; }
	const TSharedPtr<FJsonObject> R = LoadRef(*this);
	if (!R) return false;
	FHKWalkWorld& W = Data.WalkWorld;
	// The boundary pieces must be the web version's (exposedBoundary).
	{
		const TArray<TSharedPtr<FJsonValue>>& B = HKJson::Arr(Data.Game, TEXT("boundary"));
		FTally T(*this, TEXT("boundary pieces"));
		T.Check(B.Num() == W.Boundary.Num(), FString::Printf(TEXT("count %d vs web %d"), W.Boundary.Num(), B.Num()));
		for (int32 i = 0; i < FMath::Min(B.Num(), W.Boundary.Num()); i++)
		{
			const TSharedPtr<FJsonObject> O = B[i]->AsObject();
			const TArray<double> A = HKJson::Nums(O, TEXT("a")), Bb = HKJson::Nums(O, TEXT("b"));
			const FHKBoundaryPiece& P = W.Boundary[i];
			T.Check(Near(P.A.X, A[0], 1e-9) && Near(P.A.Z, A[1], 1e-9) && Near(P.B.X, Bb[0], 1e-9) && Near(P.B.Z, Bb[1], 1e-9) && P.Floor == (int32)HKJson::Num(O, TEXT("floor")), FString::Printf(TEXT("piece %d"), i));
		}
	}
	// Same dynamic obstacles as when the web answers were made (closed doors, parked vehicles).
	W.SetDynamic(FHKData::ObstaclesFromJson(HKJson::Arr(R, TEXT("dynamic"))));
	{
		FTally T(*this, TEXT("supportAt/canStand/ceilingAt/clampEyeHeight"));
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("support")))
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			const TArray<double> P = HKJson::Nums(O, TEXT("p"));
			const FHKPt Pt(P[0], P[1]);
			const double Eye = HKJson::Num(O, TEXT("eye"));
			const FHKSupport S = W.SupportAt(Pt, P[2]);
			const TSharedPtr<FJsonValue> WebS = O->TryGetField(TEXT("support"));
			bool bOk = (WebS->Type == EJson::Null) ? !S.bFound : (S.bFound && Near(S.Y, WebS->AsNumber(), 1e-9));
			bOk &= W.CanStand(Pt, Eye, P[2]) == HKJson::Bool(O, TEXT("stand"));
			bOk &= Near(W.CeilingAt(Pt, P[2]), HKJson::Num(O, TEXT("ceiling")), 1e-9);
			double C = 0;
			const bool bClamp = W.ClampEyeHeight(Pt, Eye, C, .3, P[2]);
			const TSharedPtr<FJsonValue> WebC = O->TryGetField(TEXT("clamp"));
			bOk &= (WebC->Type == EJson::Null) ? !bClamp : (bClamp && Near(C, WebC->AsNumber(), 1e-9));
			T.Check(bOk, FString::Printf(TEXT("(%.4f, %.4f, y %.4f)"), P[0], P[1], P[2]));
		}
	}
	{
		FTally T(*this, TEXT("move"));
		double Worst = 0;
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("move")))
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			const TArray<double> P = HKJson::Nums(O, TEXT("p")), Dl = HKJson::Nums(O, TEXT("d")), Rr = HKJson::Nums(O, TEXT("r"));
			const FVector M = W.Move(FHKPt(P[0], P[1]), FHKPt(Dl[0], Dl[1]), HKJson::Num(O, TEXT("eye")), P[2]);
			const double Err = FMath::Max3(FMath::Abs(M.X - Rr[0]), FMath::Abs(M.Y - Rr[1]), FMath::Abs(M.Z - Rr[2]));
			Worst = FMath::Max(Worst, Err);
			T.Check(Err < 1e-7, FString::Printf(TEXT("from (%.4f, %.4f, %.4f) by (%.4f, %.4f): (%.6f, %.6f, %.6f) vs web (%.6f, %.6f, %.6f)"), P[0], P[1], P[2], Dl[0], Dl[1], M.X, M.Y, M.Z, Rr[0], Rr[1], Rr[2]));
		}
		AddInfo(FString::Printf(TEXT("move: largest difference %.3g m"), Worst));
	}
	{
		FTally T(*this, TEXT("hitsSolid"));
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("hits")))
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			const TArray<double> P = HKJson::Nums(O, TEXT("p"));
			T.Check(W.HitsSolid(FHKPt(P[0], P[1]), HKJson::Num(O, TEXT("y0")), HKJson::Num(O, TEXT("y1"))) == HKJson::Bool(O, TEXT("hit")), FString::Printf(TEXT("(%.3f, %.3f)"), P[0], P[1]));
		}
	}
	{
		FTally T(*this, TEXT("findSafeSpawn"));
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("spawn")))
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			const TArray<double> P = HKJson::Nums(O, TEXT("p"));
			FVector Out;
			const bool bFound = W.FindSafeSpawn(FHKPt(P[0], P[1]), 1.65, Out, 6, .08, P[2]);
			const TSharedPtr<FJsonValue> Web = O->TryGetField(TEXT("r"));
			bool bOk = (Web->Type == EJson::Null) ? !bFound : bFound;
			if (bOk && bFound) { const TArray<double> Rr = HKJson::Nums(Web); bOk = Near(Out.X, Rr[0], 1e-9) && Near(Out.Y, Rr[1], 1e-9) && Near(Out.Z, Rr[2], 1e-9); }
			T.Check(bOk, FString::Printf(TEXT("(%.3f, %.3f, %.3f)"), P[0], P[1], P[2]));
		}
	}
	W.SetDynamic({});
	return true;
}

#endif
