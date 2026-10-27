// Automation tests for the minigames and the firepit: the C++ port replays the controls the web version was played
// with (tools/web-export/reference-minigames.js -> Content/Data/tests/minigames.json) and must give the same states.
// Run: tools\run-tests.ps1 -Filter Haarklous.Minigames   /   -Filter Haarklous.Fire
#include "Misc/AutomationTest.h"
#include "HKData.h"
#include "HKFire.h"
#include "HKMath.h"
#include "HKMinigames.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "StaticMeshResources.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const TSharedPtr<FJsonObject>& Reference(FAutomationTestBase& Test)
	{
		static TSharedPtr<FJsonObject> J;
		if (!J)
		{
			FString Error;
			J = HKJson::LoadFile(FHKData::DataDir() / TEXT("tests") / TEXT("minigames.json"), &Error);
			if (!J) Test.AddError(Error);
		}
		return J;
	}
	// Reports how many checks matched and the first few failures (with the step where they happened).
	struct FTally
	{
		FAutomationTestBase& Test;
		FString Name;
		int32 Total = 0, Bad = 0;
		double Worst = 0;
		FTally(FAutomationTestBase& T, const FString& N) : Test(T), Name(N) {}
		bool Check(bool bOk, const FString& What) { Total++; if (!bOk) { if (Bad < 12) Test.AddError(Name + TEXT(": ") + What); Bad++; } return bOk; }
		bool Num(double Got, double Want, double Tol, const FString& What)
		{
			const double Err = FMath::Abs(Got - Want), Rel = Err / FMath::Max(1.0, FMath::Abs(Want));
			if (FMath::IsFinite(Rel)) Worst = FMath::Max(Worst, Rel);
			return Check(Rel <= Tol || (Want >= 1e8 && Got >= 1e8) || (Want <= -1e8 && Got <= -1e8), FString::Printf(TEXT("%s: %.12g vs web %.12g"), *What, Got, Want));
		}
		~FTally() { Test.AddInfo(FString::Printf(TEXT("%s: %d / %d match the web version (largest relative difference %.3g)"), *Name, Total - Bad, Total, Worst)); }
	};
	const TArray<TSharedPtr<FJsonValue>>& Arr(const TSharedPtr<FJsonValue>& V) { return V->AsArray(); }
	double N(const TSharedPtr<FJsonValue>& V) { return V->Type == EJson::Null ? NAN : V->AsNumber(); }
	bool IsNull(const TSharedPtr<FJsonValue>& V) { return !V.IsValid() || V->Type == EJson::Null; }
	TSharedPtr<FJsonValue> Field(const TSharedPtr<FJsonObject>& O, const TCHAR* K) { return O->TryGetField(K); }

	// The recording's Math.random, performance.now and audio.
	struct FPlay
	{
		HK::FParkMiller Rng = HK::FParkMiller(1);
		double NowMs = 5000;
		int32 Step = 0;
		struct FSound { int32 Step; FString Name; double Gain; };
		TArray<FSound> Sounds;
	};
	FHKMinigamesContext MakeCtx(TSharedRef<FPlay> P)
	{
		FHKMinigamesContext C = FHKMinigamesContext::FromGame();
		C.Random = [P]() { return P->Rng.Next(); };
		C.NowMs = [P]() { return P->NowMs; };
		C.Sfx = [P](const FString& Name, const FHKSfxOpts& O) { P->Sounds.Add({P->Step, Name, O.Gain}); };
		return C;
	}
	void CheckSounds(FTally& T, const FPlay& P, const TArray<TSharedPtr<FJsonValue>>& Want)
	{
		T.Check(P.Sounds.Num() == Want.Num(), FString::Printf(TEXT("%d sounds vs web %d"), P.Sounds.Num(), Want.Num()));
		for (int32 i = 0; i < FMath::Min(P.Sounds.Num(), Want.Num()); i++)
		{
			const TArray<TSharedPtr<FJsonValue>>& W = Arr(Want[i]);
			const FPlay::FSound& S = P.Sounds[i];
			if (!T.Check(S.Step == (int32)N(W[0]) && S.Name == W[1]->AsString() && FMath::Abs(S.Gain - N(W[2])) < 1e-9,
				FString::Printf(TEXT("sound %d: step %d %s %.4f vs web step %d %s %.4f"), i, S.Step, *S.Name, S.Gain, (int32)N(W[0]), *W[1]->AsString(), N(W[2])))) break;
		}
	}
	struct FControl { bool bHeld = false; double Yaw = 0, Pitch = 0; bool bAlt = false; double Hyzer = 0; bool bCancel = false; double Drunk = 0; };
	FControl ControlOf(const TArray<TSharedPtr<FJsonValue>>& C)
	{
		FControl R;
		R.bHeld = N(C[1]) != 0; R.Yaw = N(C[2]); R.Pitch = N(C[3]); R.bAlt = N(C[4]) != 0; R.Hyzer = N(C[5]); R.bCancel = N(C[6]) != 0; R.Drunk = N(C[7]);
		return R;
	}
	FHKMiniInput InputOf(FHKMinigame& G, const FControl& C, double Dt)
	{
		FHKMiniInput In;
		In.AimYaw = C.Yaw; In.AimPitch = C.Pitch; In.bAlt = C.bAlt; In.Hyzer = C.Hyzer; In.bCancel = C.bCancel; In.Drunk = C.Drunk;
		G.TakeCharge(Dt, C.bHeld, In);
		return In;
	}
	// update() result vs the recorded {teleport, stand, face, done, camera, lock}.
	void CheckResult(FTally& T, const FHKMiniResult& R, const TSharedPtr<FJsonValue>& WantV, const FString& At)
	{
		if (IsNull(WantV)) { T.Check(R.bNull, At + TEXT(": result should be null")); return; }
		if (!T.Check(!R.bNull, At + TEXT(": result is null"))) return;
		const TSharedPtr<FJsonObject> W = WantV->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* A;
		T.Check(R.bTeleport == W->HasField(TEXT("teleport")), At + TEXT(": teleport"));
		if (R.bTeleport && W->TryGetArrayField(TEXT("teleport"), A)) { T.Num(R.Teleport.X, N((*A)[0]), 1e-9, At + TEXT(" teleport.x")); T.Num(R.Teleport.Z, N((*A)[1]), 1e-9, At + TEXT(" teleport.z")); T.Num(R.Teleport.Yaw, N((*A)[2]), 1e-9, At + TEXT(" teleport.yaw")); }
		T.Check(R.bStand == W->HasField(TEXT("stand")), At + TEXT(": stand"));
		if (R.bStand && W->TryGetArrayField(TEXT("stand"), A)) { T.Num(R.Stand.X, N((*A)[0]), 1e-9, At + TEXT(" stand.x")); T.Num(R.Stand.Y, N((*A)[1]), 1e-9, At + TEXT(" stand.y")); T.Num(R.Stand.Z, N((*A)[2]), 1e-9, At + TEXT(" stand.z")); }
		T.Check(R.bFace == W->HasField(TEXT("face")), At + TEXT(": face"));
		if (R.bFace && W->HasField(TEXT("face"))) T.Num(R.Face, W->GetNumberField(TEXT("face")), 1e-9, At + TEXT(" face"));
		T.Check(R.bDone == W->HasField(TEXT("done")), At + TEXT(": done"));
		if (R.bDone && W->HasField(TEXT("done"))) T.Check(R.Done == W->GetStringField(TEXT("done")), At + FString::Printf(TEXT(": done '%s' vs web '%s'"), *R.Done, *W->GetStringField(TEXT("done"))));
		T.Check(R.bCamera == W->HasField(TEXT("camera")), At + TEXT(": camera"));
		if (R.bCamera && W->TryGetArrayField(TEXT("camera"), A))
		{
			const double Got[6] = {R.CameraPos.X, R.CameraPos.Y, R.CameraPos.Z, R.CameraLookAt.X, R.CameraLookAt.Y, R.CameraLookAt.Z};
			for (int32 k = 0; k < 6; k++) T.Num(Got[k], N((*A)[k]), 1e-7, At + FString::Printf(TEXT(" camera[%d]"), k));
		}
		T.Check(R.bLock == W->HasField(TEXT("lock")), At + TEXT(": lock"));
	}
	TOptional<FString> OptStr(const TSharedPtr<FJsonValue>& V) { return IsNull(V) ? TOptional<FString>() : TOptional<FString>(V->AsString()); }
	FString Show(const TOptional<FString>& S) { return S.IsSet() ? S.GetValue() : FString(TEXT("null")); }

	/* ---------------- Disc golf ---------------- */
	void ReplayDisc(FTally& T, FHKDiscGolf& G, FPlay& P, const TSharedPtr<FJsonObject>& Sc, double Dt, const FString& Label)
	{
		const TArray<TSharedPtr<FJsonValue>>& Steps = HKJson::Arr(Sc, TEXT("steps"));
		const TArray<TSharedPtr<FJsonValue>>& Controls = HKJson::Arr(Sc, TEXT("controls"));
		const TArray<TSharedPtr<FJsonValue>>* Lies = nullptr;
		Sc->TryGetArrayField(TEXT("lies"), Lies);
		int32 Ci = 0, Li = 0, Bad0 = T.Bad;
		FControl C;
		FString WantMsg;
		for (int32 i = 0; i < Steps.Num(); i++)
		{
			P.Step = i;
			while (Ci < Controls.Num() && (int32)N(Arr(Controls[Ci])[0]) == i) C = ControlOf(Arr(Controls[Ci++]));
			while (Lies && Li < Lies->Num() && (int32)N(Arr((*Lies)[Li])[0]) == i) { const TArray<TSharedPtr<FJsonValue>>& L = Arr((*Lies)[Li++]); G.Lie = FVector(N(L[1]), N(L[2]), N(L[3])); G.bHasLie = true; }
			const FHKMiniInput In = InputOf(G, C, Dt);
			const FHKMiniResult R = G.Update(Dt, In);
			const TSharedPtr<FJsonObject> S = Steps[i]->AsObject();
			const FString At = FString::Printf(TEXT("%s step %d"), *Label, i);
			CheckResult(T, R, Field(S, TEXT("r")), At);
			const TArray<double> Pos = HKJson::Nums(S, TEXT("pos")), Vel = HKJson::Nums(S, TEXT("vel")), Disc = HKJson::Nums(S, TEXT("disc"));
			for (int32 k = 0; k < 3; k++) T.Num(G.Disc.Pos[k], Pos[k], 1e-7, At + FString::Printf(TEXT(" disc.pos[%d]"), k));
			for (int32 k = 0; k < 3; k++) T.Num(G.Disc.Vel[k], Vel[k], 1e-7, At + FString::Printf(TEXT(" disc.vel[%d]"), k));
			T.Num(G.Disc.Bank, HKJson::Num(S, TEXT("bank")), 1e-7, At + TEXT(" bank"));
			T.Num(G.Disc.Time, HKJson::Num(S, TEXT("time")), 1e-9, At + TEXT(" time"));
			T.Check(G.Disc.bFlying == (HKJson::Num(S, TEXT("fl")) != 0) && G.Disc.bRest == (HKJson::Num(S, TEXT("rest")) != 0), At + TEXT(": flying/rest"));
			T.Check(G.Hole == (int32)HKJson::Num(S, TEXT("hole")), At + TEXT(": hole"));
			const TArray<double> Strokes = HKJson::Nums(S, TEXT("strokes"));
			bool bStrokes = Strokes.Num() == G.Strokes.Num();
			for (int32 k = 0; bStrokes && k < Strokes.Num(); k++) bStrokes = Strokes[k] == G.Strokes[k];
			T.Check(bStrokes, At + TEXT(": strokes"));
			const TArray<double> Lie = HKJson::Nums(S, TEXT("lie"));
			if (Lie.Num() == 3) for (int32 k = 0; k < 3; k++) T.Num(G.Lie[k], Lie[k], 1e-7, At + FString::Printf(TEXT(" lie[%d]"), k));
			if (S->HasField(TEXT("msg"))) WantMsg = S->GetStringField(TEXT("msg"));
			T.Check(G.Message == WantMsg, At + FString::Printf(TEXT(": message '%s' vs web '%s'"), *G.Message, *WantMsg));
			const TSharedPtr<FJsonValue> Pend = Field(S, TEXT("pend"));
			T.Check(G.bHasPending == !IsNull(Pend), At + TEXT(": pending"));
			if (G.bHasPending && !IsNull(Pend)) { T.Check(G.Pending.bNext == (N(Arr(Pend)[0]) != 0), At + TEXT(": pending.next")); T.Num(G.Pending.T, N(Arr(Pend)[1]), 1e-9, At + TEXT(" pending.t")); }
			T.Check(G.DiscNode->bVisible == (Disc[0] != 0), At + TEXT(": disc visible"));
			for (int32 k = 0; k < 3; k++) T.Num(G.DiscNode->Position[k], Disc[1 + k], 1e-7, At + FString::Printf(TEXT(" disc mesh pos[%d]"), k));
			for (int32 k = 0; k < 3; k++) T.Num(G.DiscNode->Rot()[k], Disc[4 + k], 1e-7, At + FString::Printf(TEXT(" disc mesh rot[%d]"), k));
			T.Num(G.Charge, HKJson::Num(S, TEXT("charge")), 1e-9, At + TEXT(" charge"));
			T.Check(G.bActive == (HKJson::Num(S, TEXT("active")) != 0), At + TEXT(": active"));
			const TSharedPtr<FJsonValue> Hud = Field(S, TEXT("hud"));
			if (T.Check(G.bHasHud == !IsNull(Hud), At + TEXT(": hud shown")) && G.bHasHud)
			{
				const TSharedPtr<FJsonObject> H = Hud->AsObject();
				T.Check(G.Hud.Hole == (int32)HKJson::Num(H, TEXT("hole")) && G.Hud.Holes == (int32)HKJson::Num(H, TEXT("holes")) && G.Hud.Par == HKJson::Num(H, TEXT("par")) && G.Hud.Distance == HKJson::Num(H, TEXT("distance")), At + TEXT(": hud hole/par/distance"));
				T.Num(G.Hud.Charge, HKJson::Num(H, TEXT("charge")), 1e-9, At + TEXT(" hud charge"));
				const FString HudMsg = H->HasField(TEXT("message")) ? H->GetStringField(TEXT("message")) : WantMsg;
				T.Check(G.Hud.Message == HudMsg, At + TEXT(": hud message"));
			}
			P.NowMs += Dt * 1000;
			if (T.Bad > Bad0 + 30) { T.Check(false, At + TEXT(": stopping after many differences")); break; }
		}
		CheckSounds(T, P, HKJson::Arr(Sc, TEXT("sfx")));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKMiniJsTest, "Haarklous.Minigames.JsNumbers", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKMiniJsTest::RunTest(const FString& Parameters)
{
	// Math.hypot values from V8 where the plain sqrt(a*a + b*b) differs in the last bit.
	const double H[6][3] = {{-148.17567332562788, -0.26562475148384679, 148.17591140941835}, {-118.39939633775474, -0.11596274593191348, 118.39945312586191},
		{11.907128878825880, 0.069345199279182215, 11.907330804745129}, {124.18819096600087, 0.39277669672983545, 124.18881209248035},
		{-34.019353885212610, 0.11015775369487595, 34.019532235144410}, {-80.549918036232668, 0.29258269511283502, 80.550449410771648}};
	for (const auto& V : H) TestEqual(FString::Printf(TEXT("hypot(%g, %g)"), V[0], V[1]), HKJs::Hypot(V[0], V[1]), V[2]);
	TestEqual(TEXT("hypot(3, 4)"), HKJs::Hypot(3, 4), 5.0);
	TestEqual(TEXT("hypot(0, 0)"), HKJs::Hypot(0, 0), 0.0);
	// Number.prototype.toFixed answers from V8.
	struct FCase { double V; int32 D; const TCHAR* Want; };
	for (const FCase& C : {FCase{0.125, 2, TEXT("0.13")}, FCase{2.5, 0, TEXT("3")}, FCase{1.005, 2, TEXT("1.00")}, FCase{-0.004, 2, TEXT("-0.00")}, FCase{9.995, 2, TEXT("9.99")},
		FCase{3.14159, 1, TEXT("3.1")}, FCase{0.05, 1, TEXT("0.1")}, FCase{1e9, 2, TEXT("1000000000.00")}, FCase{std::numeric_limits<double>::infinity(), 2, TEXT("Infinity")}})
		TestEqual(FString::Printf(TEXT("(%g).toFixed(%d)"), C.V, C.D), HKJs::ToFixed(C.V, C.D), FString(C.Want));
	TestEqual(TEXT("String(70)"), HKJs::Num(70), FString(TEXT("70")));
	TestEqual(TEXT("String(-1)"), HKJs::Num(-1), FString(TEXT("-1")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKDiscGolfTest, "Haarklous.Minigames.DiscGolf", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKDiscGolfTest::RunTest(const FString& Parameters)
{
	FHKData& Data = FHKData::Get();
	if (!TestTrue(TEXT("data loaded"), Data.bLoaded)) { AddError(Data.Error); return false; }
	const TSharedPtr<FJsonObject>& Ref = Reference(*this);
	if (!Ref) return false;
	const double Dt = Ref->GetNumberField(TEXT("dt"));
	const TSharedPtr<FJsonObject> Dg = Ref->GetObjectField(TEXT("discgolf"));
	TSharedRef<FPlay> P = MakeShared<FPlay>();
	FHKDiscGolf G(MakeCtx(P));
	{
		FTally T(*this, TEXT("disc golf holes and prompts"));
		const TArray<TSharedPtr<FJsonValue>>& Holes = HKJson::Arr(Dg, TEXT("holes"));
		T.Check(G.Holes.Num() == Holes.Num(), TEXT("number of holes"));
		for (int32 i = 0; i < FMath::Min(Holes.Num(), G.Holes.Num()); i++)
		{
			const TSharedPtr<FJsonObject> H = Holes[i]->AsObject();
			const TArray<double> Tee = HKJson::Nums(H, TEXT("tee")), Basket = HKJson::Nums(H, TEXT("basket"));
			for (int32 k = 0; k < 3; k++) { T.Num(G.Holes[i].Tee[k], Tee[k], 1e-9, FString::Printf(TEXT("hole %d tee[%d]"), i, k)); T.Num(G.Holes[i].Basket[k], Basket[k], 1e-9, FString::Printf(TEXT("hole %d basket[%d]"), i, k)); }
			T.Num(G.Holes[i].Length, HKJson::Num(H, TEXT("length")), 1e-9, FString::Printf(TEXT("hole %d length"), i));
			T.Check(G.Holes[i].N == (int32)HKJson::Num(H, TEXT("n")) && G.Holes[i].Par == HKJson::Num(H, TEXT("par")), TEXT("hole number and par"));
		}
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(Dg, TEXT("near")))
		{
			const TArray<TSharedPtr<FJsonValue>>& E = Arr(V);
			const TArray<double> Pt = HKJson::Nums(E[0]);
			const TOptional<FString> Got = G.Near(FVector(Pt[0], Pt[1], Pt[2])), Want = OptStr(E[1]);
			T.Check(Got == Want, FString::Printf(TEXT("near(%.2f, %.2f, %.2f): '%s' vs web '%s'"), Pt[0], Pt[1], Pt[2], *Show(Got), *Show(Want)));
		}
	}
	{
		FTally T(*this, TEXT("disc golf round (drive, approach, putts, chains, hole 2, finish)"));
		P->Rng = HK::FParkMiller(HKJson::Num(Dg, TEXT("seed")));
		P->NowMs = 5000;
		const TSharedPtr<FJsonObject> St = Dg->GetObjectField(TEXT("start"));
		const TArray<double> Sp = HKJson::Nums(St, TEXT("p")), Sr = HKJson::Nums(St, TEXT("r"));
		const TOptional<FHKMiniPose> Pose = G.Start(FVector(Sp[0], Sp[1], Sp[2]));
		if (T.Check(Pose.IsSet(), TEXT("start returns a pose"))) { T.Num(Pose->X, Sr[0], 1e-9, TEXT("start x")); T.Num(Pose->Z, Sr[1], 1e-9, TEXT("start z")); T.Num(Pose->Yaw, Sr[2], 1e-9, TEXT("start yaw")); }
		T.Check(!G.Near(G.Holes[0].Tee).IsSet() && IsNull(Field(Dg, TEXT("nearActive"))), TEXT("no prompt while playing"));
		ReplayDisc(T, G, *P, Dg, Dt, TEXT("round"));
		T.Check(G.Disc.Time >= 0 && !G.bActive, TEXT("round finished"));
		AddInfo(FString::Printf(TEXT("disc golf round: %d steps, %d sounds"), HKJson::Arr(Dg, TEXT("steps")).Num(), P->Sounds.Num()));
	}
	{
		FTally T(*this, TEXT("disc golf extra throws (fast disc at the basket, chains bounce, trees)"));
		const TSharedPtr<FJsonObject> Ex = Dg->GetObjectField(TEXT("extra"));
		P->Rng = HK::FParkMiller(HKJson::Num(Ex, TEXT("seed")));
		P->NowMs = 5000;
		P->Sounds.Reset();
		G.Start(G.Holes[0].Tee);
		ReplayDisc(T, G, *P, Ex, Dt, TEXT("extra"));
	}
	{
		FTally T(*this, TEXT("disc golf cancel"));
		const TSharedPtr<FJsonObject> Ca = Dg->GetObjectField(TEXT("cancel"));
		P->Rng = HK::FParkMiller(777);
		const TOptional<FHKMiniPose> Pose = G.Start(G.Holes[1].Tee);
		const TArray<double> S = HKJson::Nums(Ca, TEXT("start"));
		T.Check(Pose.IsSet() && FMath::Abs(Pose->Yaw - S[2]) < 1e-9, TEXT("start at hole 2"));
		FHKMiniInput In;
		In.bCancel = true;
		CheckResult(T, G.Update(Dt, In), Field(Ca, TEXT("r")), TEXT("cancel"));
		CheckResult(T, G.Update(Dt, FHKMiniInput()), Field(Ca, TEXT("after")), TEXT("after cancel"));
		T.Check(!G.bActive && !G.HudVisible(), TEXT("stopped, HUD hidden"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKPetanqueTest, "Haarklous.Minigames.Petanque", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKPetanqueTest::RunTest(const FString& Parameters)
{
	FHKData& Data = FHKData::Get();
	if (!TestTrue(TEXT("data loaded"), Data.bLoaded)) { AddError(Data.Error); return false; }
	const TSharedPtr<FJsonObject>& Ref = Reference(*this);
	if (!Ref) return false;
	const double Dt = Ref->GetNumberField(TEXT("dt"));
	const TSharedPtr<FJsonObject> Pq = Ref->GetObjectField(TEXT("petanque"));
	TSharedRef<FPlay> P = MakeShared<FPlay>();
	FHKPetanque G(MakeCtx(P));
	{
		FTally T(*this, TEXT("petanque prompts and start"));
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(Pq, TEXT("near")))
		{
			const TArray<TSharedPtr<FJsonValue>>& E = Arr(V);
			const TArray<double> Pt = HKJson::Nums(E[0]);
			const TOptional<FString> Got = G.Near(FVector(Pt[0], Pt[1], Pt[2])), Want = OptStr(E[1]);
			T.Check(Got == Want, FString::Printf(TEXT("near(%.2f, %.2f): '%s' vs web '%s'"), Pt[0], Pt[2], *Show(Got), *Show(Want)));
		}
		P->Rng = HK::FParkMiller(HKJson::Num(Pq, TEXT("seed")));
		const TSharedPtr<FJsonObject> St = Pq->GetObjectField(TEXT("start"));
		const TArray<double> Sp = HKJson::Nums(St, TEXT("p")), Sr = HKJson::Nums(St, TEXT("r"));
		const TOptional<FHKMiniPose> Pose = G.Start(FVector(Sp[0], Sp[1], Sp[2]));
		if (T.Check(Pose.IsSet(), TEXT("start returns a pose"))) { T.Num(Pose->X, Sr[0], 1e-9, TEXT("start x")); T.Num(Pose->Z, Sr[1], 1e-9, TEXT("start z")); T.Num(Pose->Yaw, Sr[2], 1e-9, TEXT("start yaw")); }
		T.Check(!G.Near(FVector(G.Centre.X, 0, G.Centre.Y)).IsSet(), TEXT("no prompt while playing"));
	}
	{
		FTally T(*this, TEXT("petanque game (jack, point, shoot, Arne, mènes, circle, to 13)"));
		const TArray<TSharedPtr<FJsonValue>>& Steps = HKJson::Arr(Pq, TEXT("steps"));
		const TArray<TSharedPtr<FJsonValue>>& Controls = HKJson::Arr(Pq, TEXT("controls"));
		int32 Ci = 0;
		FControl C;
		FString WantMsg;
		int32 Full = 0;
		auto Side = [](const TSharedPtr<FJsonValue>& V) { return IsNull(V) ? EHKSide::None : V->AsString() == TEXT("you") ? EHKSide::You : EHKSide::Npc; };
		for (int32 i = 0; i < Steps.Num(); i++)
		{
			P->Step = i;
			while (Ci < Controls.Num() && (int32)N(Arr(Controls[Ci])[0]) == i) C = ControlOf(Arr(Controls[Ci++]));
			const FHKMiniInput In = InputOf(G, C, Dt);
			const FHKMiniResult R = G.Update(Dt, In);
			const TSharedPtr<FJsonObject> S = Steps[i]->AsObject();
			const FString At = FString::Printf(TEXT("step %d"), i);
			CheckResult(T, R, Field(S, TEXT("r")), At);
			if (S->HasField(TEXT("full")))
			{
				Full++;
				// The seven balls (jack, your three, Arne's three): mesh visible and position.
				const TArray<TSharedPtr<FJsonValue>>& Balls = HKJson::Arr(S, TEXT("balls"));
				for (int32 b = 0; b < Balls.Num() && b < G.Balls.Num(); b++)
				{
					const TArray<double> Bv = HKJson::Nums(Balls[b]);
					const FHKNode* M = G.Balls[b].Mesh;
					T.Check(M->bVisible == (Bv[0] != 0), At + FString::Printf(TEXT(": ball %d visible"), b));
					if (Bv[0] != 0) for (int32 k = 0; k < 3; k++) T.Num(M->Position[k], Bv[1 + k], 1e-7, At + FString::Printf(TEXT(" ball %d pos[%d]"), b, k));
				}
				const TSharedPtr<FJsonObject> Dbg = S->GetObjectField(TEXT("dbg")), Mine = G.Debug();
				T.Check(Mine->GetStringField(TEXT("phase")) == Dbg->GetStringField(TEXT("phase")), At + FString::Printf(TEXT(": phase %s vs web %s"), *Mine->GetStringField(TEXT("phase")), *Dbg->GetStringField(TEXT("phase"))));
				T.Check(G.Current == Side(Field(Dbg, TEXT("current"))), At + TEXT(": current"));
				const TSharedPtr<FJsonValue> Aim = Field(Dbg, TEXT("aim"));
				T.Check((G.AimingFor == FHKPetanque::EAim::None) == IsNull(Aim) && (IsNull(Aim) || (G.AimingFor == FHKPetanque::EAim::Jack) == (Aim->AsString() == TEXT("jack"))), At + TEXT(": aimingFor"));
				const TArray<double> Circle = HKJson::Nums(Dbg, TEXT("circle")), Jack = HKJson::Nums(Dbg, TEXT("jack"));
				T.Num(G.Circle.X, Circle[0], 1e-7, At + TEXT(" circle.u"));
				T.Num(G.Circle.Y, Circle[1], 1e-7, At + TEXT(" circle.v"));
				T.Num(G.Jack().U, Jack[0], 1e-7, At + TEXT(" jack.u"));
				T.Num(G.Jack().V, Jack[1], 1e-7, At + TEXT(" jack.v"));
				T.Check(G.Jack().bAlive == (Jack[2] != 0) && G.Jack().bDead == (Jack[3] != 0), At + TEXT(": jack alive/dead"));
				for (const EHKSide Sd : {EHKSide::You, EHKSide::Npc})
				{
					const TArray<TSharedPtr<FJsonValue>>& Bs = HKJson::Arr(Dbg, Sd == EHKSide::You ? TEXT("you") : TEXT("npc"));
					for (int32 k = 0; k < 3 && k < Bs.Num(); k++)
					{
						const TArray<double> Bv = HKJson::Nums(Bs[k]);
						const FHKPetanque::FBall& B = G.Balls[(Sd == EHKSide::You ? 1 : 4) + k];
						T.Check(B.bAlive == (Bv[2] != 0) && B.bDead == (Bv[3] != 0), At + FString::Printf(TEXT(": %s boule %d alive/dead"), Sd == EHKSide::You ? TEXT("your") : TEXT("Arne's"), k));
						T.Check(FMath::Abs(FCString::Atod(*HKJs::ToFixed(B.U, 2)) - Bv[0]) < 1e-9 && FMath::Abs(FCString::Atod(*HKJs::ToFixed(B.V, 2)) - Bv[1]) < 1e-9, At + FString::Printf(TEXT(": boule %d position (debug)"), k));
					}
				}
				const TArray<double> Score = HKJson::Nums(S, TEXT("score"));
				T.Check(G.ScoreYou == (int32)Score[0] && G.ScoreNpc == (int32)Score[1], At + FString::Printf(TEXT(": score %d-%d vs web %d-%d"), G.ScoreYou, G.ScoreNpc, (int32)Score[0], (int32)Score[1]));
				T.Check(G.Mene == (int32)HKJson::Num(S, TEXT("mene")), At + TEXT(": mène"));
				WantMsg = S->GetStringField(TEXT("msg"));
				T.Check(G.Message == WantMsg, At + FString::Printf(TEXT(": message '%s' vs web '%s'"), *G.Message, *WantMsg));
				T.Check(G.TurnId == (int32)HKJson::Num(S, TEXT("turnId")) && (G.bHasFaced ? G.Faced : -1) == (int32)HKJson::Num(S, TEXT("faced")), At + TEXT(": turnId/faced"));
				const TSharedPtr<FJsonValue> Fin = Field(S, TEXT("finish"));
				T.Check(G.bHasFinish == !IsNull(Fin), At + TEXT(": finish timer"));
				if (G.bHasFinish && !IsNull(Fin)) T.Num(G.Finish, N(Fin), 1e-9, At + TEXT(" finish"));
				T.Check(G.NextFirst == Side(Field(S, TEXT("nextFirst"))), At + TEXT(": nextFirst"));
				const TArray<double> Npc = HKJson::Nums(S, TEXT("npc"));
				for (int32 k = 0; k < 3; k++) T.Num(G.Npc->Position[k], Npc[k], 1e-9, At + FString::Printf(TEXT(" Arne pos[%d]"), k));
				T.Num(G.Npc->Rot().Y, Npc[3], 1e-9, At + TEXT(" Arne turn"));
				T.Num(G.ArmR->Rot().X, Npc[4], 1e-9, At + TEXT(" Arne's throwing arm"));
				const TSharedPtr<FJsonValue> HudV = Field(S, TEXT("hud"));
				if (T.Check(G.bHasHud == !IsNull(HudV), At + TEXT(": hud shown")) && G.bHasHud)
				{
					const TSharedPtr<FJsonObject> H = HudV->AsObject();
					const FHKPetanque::FHud& Hd = G.Hud;
					const TArray<double> Hs = HKJson::Nums(H, TEXT("score"));
					T.Check(Hd.ScoreYou == (int32)Hs[0] && Hd.ScoreNpc == (int32)Hs[1] && Hd.Mene == (int32)HKJson::Num(H, TEXT("mene")) && Hd.You == (int32)HKJson::Num(H, TEXT("you")) && Hd.Npc == (int32)HKJson::Num(H, TEXT("npc")), At + TEXT(": hud score/mène/boules left"));
					T.Check(Hd.Turn == Side(Field(H, TEXT("turn"))) && Hd.Hold == Side(Field(H, TEXT("hold"))), At + TEXT(": hud turn/hold"));
					T.Num(Hd.Margin, HKJson::Num(H, TEXT("margin")), 1e-7, At + TEXT(" hud margin"));
					T.Num(Hd.Charge, HKJson::Num(H, TEXT("charge")), 1e-9, At + TEXT(" hud charge"));
					T.Check(Hd.bShoot == HKJson::Bool(H, TEXT("shoot")), At + TEXT(": hud shoot"));
					T.Check(Hd.Message == (H->HasField(TEXT("message")) ? H->GetStringField(TEXT("message")) : WantMsg), At + TEXT(": hud message"));
					const TArray<TSharedPtr<FJsonValue>>& Marks = HKJson::Arr(H, TEXT("markers"));
					if (T.Check(Hd.Markers.Num() == Marks.Num(), At + FString::Printf(TEXT(": %d markers vs web %d"), Hd.Markers.Num(), Marks.Num())))
						for (int32 k = 0; k < Marks.Num(); k++)
						{
							const TArray<TSharedPtr<FJsonValue>>& Mk = Arr(Marks[k]);
							const FHKPetanque::FMarker& M = Hd.Markers[k];
							const FString Owner = M.Owner == EHKBallOwner::Jack ? TEXT("jack") : M.Owner == EHKBallOwner::You ? TEXT("you") : TEXT("npc");
							T.Check(M.Label == Mk[0]->AsString() && Owner == Mk[1]->AsString() && M.bBest == (N(Mk[2]) != 0), At + FString::Printf(TEXT(": marker %d '%s' %s %d vs web '%s' %s %d"), k, *M.Label, *Owner, M.bBest, *Mk[0]->AsString(), *Mk[1]->AsString(), (int32)N(Mk[2])));
						}
				}
			}
			P->NowMs += Dt * 1000;
			if (T.Bad > 40) { T.Check(false, At + TEXT(": stopping after many differences")); break; }
		}
		CheckSounds(T, *P, HKJson::Arr(Pq, TEXT("sfx")));
		const TSharedPtr<FJsonObject> After = Pq->GetObjectField(TEXT("afterStop"));
		const TArray<double> Npc = HKJson::Nums(After, TEXT("npc"));
		for (int32 k = 0; k < 3; k++) T.Num(G.Npc->Position[k], Npc[k], 1e-9, FString::Printf(TEXT("Arne back home [%d]"), k));
		bool bHidden = true;
		for (const FHKPetanque::FBall& B : G.Balls) bHidden &= !B.Mesh->bVisible;
		T.Check(!G.bActive && bHidden && !G.HudVisible(), TEXT("stopped: balls hidden, HUD hidden"));
		AddInfo(FString::Printf(TEXT("petanque: %d steps (%d with full state), %d mènes, final %d-%d, %d sounds"), Steps.Num(), Full, G.Mene, G.ScoreYou, G.ScoreNpc, P->Sounds.Num()));
	}
	return true;
}

namespace
{
	FHKFireContext FireCtx(TSharedRef<FPlay> P)
	{
		FHKFireContext C = FHKFireContext::FromGame();
		C.Random = [P]() { return P->Rng.Next(); };
		return C;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKFireLayoutTest, "Haarklous.Fire.Layout", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKFireLayoutTest::RunTest(const FString& Parameters)
{
	FHKData& Data = FHKData::Get();
	if (!TestTrue(TEXT("data loaded"), Data.bLoaded)) { AddError(Data.Error); return false; }
	const TSharedPtr<FJsonObject>& Ref = Reference(*this);
	if (!Ref) return false;
	const TSharedPtr<FJsonObject> Fi = Ref->GetObjectField(TEXT("fire"));
	TSharedRef<FPlay> P = MakeShared<FPlay>();
	FHKFire F(-2.7, 34.7, FireCtx(P));
	FTally T(*this, TEXT("firepit layout (benches, seats, woodpile, crate, obstacles)"));
	// Against the recording and against game.json (the page's own fire, exported by game-data.js).
	const TSharedPtr<FJsonObject> L = Fi->GetObjectField(TEXT("layout")), Gj = HKJson::Obj(Data.Game, TEXT("fire"));
	for (const TSharedPtr<FJsonObject>& Src : {L, Gj})
	{
		const TArray<double> C = HKJson::Nums(Src, TEXT("center")), Wp = HKJson::Nums(Src, TEXT("woodpile")), Cr = HKJson::Nums(Src, TEXT("crate"));
		for (int32 k = 0; k < 3; k++) T.Num(F.Center[k], C[k], 1e-9, FString::Printf(TEXT("center[%d]"), k));
		T.Num(F.Woodpile.X, Wp[0], 1e-9, TEXT("woodpile x")); T.Num(F.Woodpile.Y, Wp[1], 1e-9, TEXT("woodpile z"));
		T.Num(F.Crate.X, Cr[0], 1e-9, TEXT("crate x")); T.Num(F.Crate.Y, Cr[1], 1e-9, TEXT("crate z"));
		const TArray<TSharedPtr<FJsonValue>>& Spots = HKJson::Arr(Src, TEXT("spots"));
		T.Check(Spots.Num() == F.Spots.Num(), TEXT("ten seats"));
		for (int32 i = 0; i < FMath::Min(Spots.Num(), F.Spots.Num()); i++)
		{
			const FHKFireSpot& S = F.Spots[i];
			if (Spots[i]->Type == EJson::Array)
			{
				const TArray<double> V = HKJson::Nums(Spots[i]);
				T.Check(S.Id == (int32)V[0], TEXT("seat id"));
				T.Num(S.X, V[1], 1e-9, TEXT("seat x")); T.Num(S.Y, V[2], 1e-9, TEXT("seat y")); T.Num(S.Z, V[3], 1e-9, TEXT("seat z")); T.Num(S.Yaw, V[4], 1e-9, TEXT("seat yaw"));
			}
			else
			{
				const TSharedPtr<FJsonObject> O = Spots[i]->AsObject();
				T.Check(S.Id == (int32)HKJson::Num(O, TEXT("id")), TEXT("seat id"));
				T.Num(S.X, HKJson::Num(O, TEXT("x")), 1e-9, TEXT("seat x")); T.Num(S.Y, HKJson::Num(O, TEXT("y")), 1e-9, TEXT("seat y")); T.Num(S.Z, HKJson::Num(O, TEXT("z")), 1e-9, TEXT("seat z")); T.Num(S.Yaw, HKJson::Num(O, TEXT("yaw")), 1e-9, TEXT("seat yaw"));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>& Obs = HKJson::Arr(Src, TEXT("obstacles"));
		T.Check(Obs.Num() == F.Obstacles.Num(), TEXT("eight obstacles"));
		for (int32 i = 0; i < FMath::Min(Obs.Num(), F.Obstacles.Num()); i++)
		{
			const TSharedPtr<FJsonObject> O = Obs[i]->AsObject();
			const TArray<TSharedPtr<FJsonValue>>& Pts = HKJson::Arr(O, TEXT("points"));
			T.Check(Pts.Num() == F.Obstacles[i].Points.Num() && F.Obstacles[i].Kind == HKJson::Str(O, TEXT("kind")), TEXT("obstacle shape"));
			for (int32 k = 0; k < FMath::Min(Pts.Num(), F.Obstacles[i].Points.Num()); k++)
			{
				const TArray<double> Pt = HKJson::Nums(Pts[k]);
				T.Num(F.Obstacles[i].Points[k].X, Pt[0], 1e-9, FString::Printf(TEXT("obstacle %d point %d x"), i, k));
				T.Num(F.Obstacles[i].Points[k].Z, Pt[1], 1e-9, FString::Printf(TEXT("obstacle %d point %d z"), i, k));
			}
			T.Num(F.Obstacles[i].MinY, HKJson::Num(O, TEXT("minY")), 1e-9, TEXT("obstacle minY"));
			T.Num(F.Obstacles[i].MaxY, HKJson::Num(O, TEXT("maxY")), 1e-9, TEXT("obstacle maxY"));
		}
	}
	T.Check(FHKFire::DECAY_S == HKJson::Num(L, TEXT("DECAY_S")) && FHKFire::MAX == HKJson::Num(L, TEXT("MAX")), TEXT("DECAY_S and MAX"));
	// Flame parameters from the module's own generator (seed 7, after the stones).
	const TArray<TSharedPtr<FJsonValue>>& Fl = HKJson::Arr(Fi->GetObjectField(TEXT("initial")), TEXT("flames"));
	for (int32 i = 0; i < 6 && i < Fl.Num(); i++)
	{
		const TArray<double> V = HKJson::Nums(Fl[i]);
		T.Num(F.Flames[i].K, V[0], 1e-9, FString::Printf(TEXT("flame %d k"), i));
		T.Num(F.Flames[i].Off[0], V[1], 1e-9, FString::Printf(TEXT("flame %d off x"), i));
		T.Num(F.Flames[i].Off[1], V[2], 1e-9, FString::Printf(TEXT("flame %d off z"), i));
		T.Num(F.Flames[i].Time, V[3], 1e-9, FString::Printf(TEXT("flame %d time"), i));
	}
	TestNotNull(TEXT("rig node charred"), F.Charred);
	TestNotNull(TEXT("rig node log template"), F.Rig.Find(TEXT("log")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKFireUpdateTest, "Haarklous.Fire.Update", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKFireUpdateTest::RunTest(const FString& Parameters)
{
	FHKData& Data = FHKData::Get();
	if (!TestTrue(TEXT("data loaded"), Data.bLoaded)) { AddError(Data.Error); return false; }
	const TSharedPtr<FJsonObject>& Ref = Reference(*this);
	if (!Ref) return false;
	const double Dt = Ref->GetNumberField(TEXT("dt"));
	const TSharedPtr<FJsonObject> Fi = Ref->GetObjectField(TEXT("fire"));
	TSharedRef<FPlay> P = MakeShared<FPlay>();
	P->Rng = HK::FParkMiller(HKJson::Num(Fi, TEXT("seed")));
	FHKFire F(-2.7, 34.7, FireCtx(P));
	FTally T(*this, TEXT("fire levels 0-6 (flames, light, glow, embers, smoke, thrown logs)"));
	TMap<int32, TArray<double>> Throws;
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(Fi, TEXT("throws"))) { const TArray<double> A = HKJson::Nums(V); Throws.Add((int32)A[0], A); }
	const TArray<TSharedPtr<FJsonValue>>& Steps = HKJson::Arr(Fi, TEXT("steps"));
	int32 MaxFlying = 0, Samples = 0;
	for (int32 i = 0; i < Steps.Num(); i++)
	{
		const FString At = FString::Printf(TEXT("step %d"), i);
		if (const TArray<double>* Th = Throws.Find(i)) F.ThrowLog(FVector((*Th)[1], (*Th)[2], (*Th)[3]), (*Th)[4] != 0);
		const TSharedPtr<FJsonObject> S = Steps[i]->AsObject();
		const TArray<double> Cam = HKJson::Nums(S, TEXT("cam"));
		F.Update(Dt, HKJson::Num(S, TEXT("level")), FVector(Cam[0], Cam[1], Cam[2]));
		const TArray<double> Light = HKJson::Nums(S, TEXT("light"));
		T.Num(F.LightIntensity, Light[0], 1e-9, At + TEXT(" light intensity"));
		T.Num(F.LightDistance, Light[1], 1e-9, At + TEXT(" light distance"));
		T.Num(F.CharredEmissiveIntensity, HKJson::Num(S, TEXT("charred")), 1e-9, At + TEXT(" glowing logs"));
		const TArray<TSharedPtr<FJsonValue>>& Fl = HKJson::Arr(S, TEXT("flames"));
		for (int32 k = 0; k < 6 && k < Fl.Num(); k++)
		{
			const TArray<double> V = HKJson::Nums(Fl[k]);
			const FHKFire::FFlame& Fm = F.Flames[k];
			T.Check(Fm.bVisible == (V[0] != 0), At + FString::Printf(TEXT(": flame %d visible"), k));
			const double Got[8] = {Fm.Scale.X, Fm.Scale.Y, Fm.Position.X, Fm.Position.Y, Fm.Position.Z, Fm.RotY, Fm.Time, Fm.Power};
			for (int32 q = 0; q < 8; q++) T.Num(Got[q], V[1 + q], 1e-9, At + FString::Printf(TEXT(" flame %d value %d"), k, q));
		}
		const TArray<double> Em = HKJson::Nums(S, TEXT("embers"));
		T.Check(F.bEmbersVisible == (Em[0] != 0), At + TEXT(": embers visible"));
		T.Num(F.EmberSize, Em[1], 1e-9, At + TEXT(" ember size"));
		T.Num(F.SmokeOpacity, HKJson::Num(S, TEXT("smoke")), 1e-9, At + TEXT(" smoke opacity"));
		const TArray<TSharedPtr<FJsonValue>>& Logs = HKJson::Arr(S, TEXT("logs"));
		MaxFlying = FMath::Max(MaxFlying, Logs.Num());
		if (T.Check(Logs.Num() == F.Flying.Num(), At + FString::Printf(TEXT(": %d flying logs vs web %d"), F.Flying.Num(), Logs.Num())))
			for (int32 k = 0; k < Logs.Num(); k++)
			{
				const TArray<double> V = HKJson::Nums(Logs[k]);
				const FHKNode* Nd = F.Flying[k].Node;
				for (int32 q = 0; q < 3; q++) T.Num(Nd->Position[q], V[q], 1e-9, At + FString::Printf(TEXT(" log %d pos[%d]"), k, q));
				for (int32 q = 0; q < 3; q++) T.Num(Nd->Rot()[q], V[3 + q], 1e-9, At + FString::Printf(TEXT(" log %d rot[%d]"), k, q));
				T.Check(Nd->bVisible, At + TEXT(": flying log visible"));
			}
		if (S->HasField(TEXT("ep")))
		{
			Samples++;
			const TArray<double> Ep = HKJson::Nums(S, TEXT("ep")), Sp = HKJson::Nums(S, TEXT("sp"));
			T.Check(Ep.Num() == F.EmberPos.Num() && Sp.Num() == F.SmokePos.Num(), At + TEXT(": point counts"));
			for (int32 k = 0; k < FMath::Min(Ep.Num(), F.EmberPos.Num()); k++) T.Num(F.EmberPos[k], Ep[k], 1e-7, At + FString::Printf(TEXT(" ember %d[%d]"), k / 3, k % 3));
			for (int32 k = 0; k < FMath::Min(Sp.Num(), F.SmokePos.Num()); k++) T.Num(F.SmokePos[k], Sp[k], 1e-7, At + FString::Printf(TEXT(" smoke %d[%d]"), k / 3, k % 3));
		}
		if (T.Bad > 40) { T.Check(false, At + TEXT(": stopping after many differences")); break; }
	}
	int32 Hidden = 0;
	for (const TUniquePtr<FHKNode>& Nd : F.Rig.All()) if (Nd->Name.StartsWith(TEXT("log")) && Nd->Name != TEXT("log") && !Nd->bVisible) Hidden++;
	T.Check(F.Flying.Num() == 0 && Hidden == FHKFire::LogPool, TEXT("all thrown logs landed and hidden"));
	AddInfo(FString::Printf(TEXT("fire: %d steps, %d point samples, up to %d logs in the air"), Steps.Num(), Samples, MaxFlying));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKFireLevelTest, "Haarklous.Fire.Level", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKFireLevelTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject>& Ref = Reference(*this);
	if (!Ref) return false;
	const TSharedPtr<FJsonObject> Lv = Ref->GetObjectField(TEXT("fireLevel"));
	const double T0 = HKJson::Num(Lv, TEXT("t0"));
	FHKFireLevel L;
	L.T = T0 - 1e9;   // the lobby's fire at start (level 2) has long burnt down by then, like in the recording
	FTally T(*this, TEXT("fire level from logs and decay (+1 per log, max 6, -1 per 90 s)"));
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(Lv, TEXT("ops")))
	{
		const TArray<TSharedPtr<FJsonValue>>& Op = Arr(V);
		const double Now = T0 + N(Op[1]);
		if (Op[0]->AsString() == TEXT("add")) L.AddWood(Now);
		T.Num(L.At(Now), N(Op[2]), 1e-9, FString::Printf(TEXT("%s at +%.0f ms"), *Op[0]->AsString(), N(Op[1])));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKFireEffectsTest, "Haarklous.Fire.Effects", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKFireEffectsTest::RunTest(const FString& Parameters)
{
	// The engine plane: which local axis its texture goes up along (the flame cards depend on it).
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (!TestNotNull(TEXT("engine plane"), Plane)) return false;
	if (Plane->GetRenderData() && Plane->GetRenderData()->LODResources.Num() > 0)
	{
		const FStaticMeshLODResources& Lod = Plane->GetRenderData()->LODResources[0];
		const uint32 Count = Lod.VertexBuffers.PositionVertexBuffer.GetNumVertices();
		if (Count > 0 && Lod.VertexBuffers.StaticMeshVertexBuffer.GetTexCoordData())
		{
			FVector Grad = FVector::ZeroVector;   // direction of decreasing V
			for (uint32 v = 0; v < Count; v++)
			{
				const FVector3f Pv = Lod.VertexBuffers.PositionVertexBuffer.VertexPosition(v);
				const FVector2f Uv = Lod.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(v, 0);
				AddInfo(FString::Printf(TEXT("plane vertex %u: (%.0f, %.0f, %.0f) uv (%.2f, %.2f)"), v, Pv.X, Pv.Y, Pv.Z, Uv.X, Uv.Y));
				Grad += FVector(Pv) * (0.5 - Uv.Y);
			}
			Grad = Grad.GetSafeNormal();
			const FVector Axis = FMath::Abs(Grad.X) > FMath::Abs(Grad.Y) ? FVector(FMath::Sign(Grad.X), 0, 0) : FVector(0, FMath::Sign(Grad.Y), 0);
			TestEqual(TEXT("plane texture up axis"), AHKFireEffects::PlaneUpAxis(), Axis);
		}
		else AddWarning(TEXT("plane vertex data not readable here; up axis not checked"));
	}
	// Materials: log their parameters and how the additive one is built.
	for (const TCHAR* Path : {TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"), TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent.Widget3DPassThrough_Translucent")})
	{
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Path);
		if (!TestNotNull(Path, M)) continue;
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		FString List;
		M->GetAllVectorParameterInfo(Infos, Ids); for (const FMaterialParameterInfo& I : Infos) List += TEXT(" vector:") + I.Name.ToString();
		M->GetAllTextureParameterInfo(Infos, Ids); for (const FMaterialParameterInfo& I : Infos) List += TEXT(" texture:") + I.Name.ToString();
		M->GetAllScalarParameterInfo(Infos, Ids); for (const FMaterialParameterInfo& I : Infos) List += TEXT(" scalar:") + I.Name.ToString();
#if WITH_EDITORONLY_DATA
		if (UMaterial* Base = M->GetMaterial()) for (UMaterialExpression* E : Base->GetExpressions()) if (E) List += TEXT(" [") + E->GetClass()->GetName() + TEXT("]");
#endif
		AddInfo(FString::Printf(TEXT("%s:%s"), Path, *List));
	}
	// Spawn the effects in a throw-away world and copy a burning fire into it.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("HKFireEffectsTest"));
	if (!TestNotNull(TEXT("world"), World)) return false;
	FWorldContext& WorldCtx = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldCtx.SetCurrentWorld(World);
	TSharedRef<FPlay> P = MakeShared<FPlay>();
	FHKFire F(-2.7, 34.7, FireCtx(P));
	const FVector Cam(-2.7 + 6, -8, 34.7 + 3);
	for (int32 i = 0; i < 120; i++) F.Update(1.0 / 60, 4.5, Cam);
	AHKFireEffects* E = World->SpawnActor<AHKFireEffects>();
	if (TestNotNull(TEXT("effects actor"), E))
	{
		E->Init(nullptr);
		E->Sync(F, Cam);
		TestTrue(TEXT("built"), E->IsBuilt());
		TestEqual(TEXT("flame cards"), E->FlameCards.Num(), 6);
		TestEqual(TEXT("embers"), E->EmberMeshes.Num(), FHKFire::EN);
		TestEqual(TEXT("smoke puffs"), E->SmokeMeshes.Num(), FHKFire::SN);
		TestFalse(TEXT("additive colour parameter found"), E->AdditiveColorParam.IsNone());
		TestFalse(TEXT("additive texture parameter found"), E->AdditiveTextureParam.IsNone());
		TestFalse(TEXT("translucent colour parameter found"), E->TranslucentColorParam.IsNone());
		TestFalse(TEXT("translucent texture parameter found"), E->TranslucentTextureParam.IsNone());
		AddInfo(FString::Printf(TEXT("parameters: additive %s / %s, translucent %s / %s"), *E->AdditiveColorParam.ToString(), *E->AdditiveTextureParam.ToString(), *E->TranslucentColorParam.ToString(), *E->TranslucentTextureParam.ToString()));
		if (TestNotNull(TEXT("light"), E->Light.Get())) TestTrue(TEXT("light intensity follows the fire"), FMath::Abs(E->Light->Intensity - F.LightIntensity) < 1e-3);
		TestTrue(TEXT("actor at the firepit"), E->GetActorLocation().Equals(HK::ToUE(F.Center), 1));
		// A flame card stands upright and faces the camera's yaw; its size follows the web scale.
		const FHKFire::FFlame& Fm = F.Flames[0];
		UStaticMeshComponent* Card = E->FlameCards[0];
		TestTrue(TEXT("flame visible"), Card->IsVisible() == Fm.bVisible);
		const FTransform Tr = Card->GetRelativeTransform();
		const FVector Normal = Tr.GetRotation().GetAxisZ(), Up = Tr.GetRotation().RotateVector(AHKFireEffects::PlaneUpAxis());
		TestTrue(TEXT("card up is world up"), Up.Equals(FVector(0, 0, 1), 1e-6));
		TestTrue(TEXT("card faces the camera yaw"), Normal.Equals(HK::DirToUE(FVector(FMath::Sin(Fm.RotY), 0, FMath::Cos(Fm.RotY))), 1e-6));
		TestTrue(TEXT("card position"), Tr.GetLocation().Equals(HK::ToUE(Fm.Position), 1e-3));
		const FVector Sc = Tr.GetScale3D(), UpScale = AHKFireEffects::PlaneUpAxis().X != 0 ? FVector(Sc.X, 0, 0) : FVector(0, Sc.Y, 0);
		TestTrue(TEXT("card height = 1.6 x scale.y"), FMath::Abs(UpScale.Size() - Fm.Scale.Y * 1.6) < 1e-6);
		// Level 0: flames out, embers hidden, light .4.
		for (int32 i = 0; i < 60; i++) F.Update(1.0 / 60, 0, Cam);
		E->Sync(F, Cam);
		int32 Lit = 0;
		for (UStaticMeshComponent* C : E->FlameCards) if (C->IsVisible()) Lit++;
		TestEqual(TEXT("no flames at level 0"), Lit, 0);
		TestFalse(TEXT("no embers at level 0"), E->EmberMeshes[0]->IsVisible());
		TestTrue(TEXT("light .4 at level 0"), FMath::Abs(E->Light->Intensity - .4f) < 1e-4);
		// Far away: embers and smoke are not drawn.
		for (int32 i = 0; i < 10; i++) F.Update(1.0 / 60, 6, Cam);
		E->Sync(F, FVector(400, 0, 400));
		TestFalse(TEXT("smoke hidden far away"), E->SmokeMeshes[0]->IsVisible());
		E->Destroy();
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
