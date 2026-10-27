// Port of reference/web-game/game-minigames.js (disc golf and petanque). Keep it line by line with the JavaScript:
// same constants, same order of operations (also inside expressions) and the same order of Math.random calls.
#include "HKMinigames.h"
#include "HKMath.h"
#include "HKUi.h"
#include "Haarklous.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
	// three.js Vector3.normalize(): divideScalar(length() || 1), and divideScalar is multiplyScalar(1 / s).
	FVector Norm3(const FVector& V)
	{
		const double Len = std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z);
		const double S = 1 / (Len != 0 ? Len : 1);
		return FVector(V.X * S, V.Y * S, V.Z * S);
	}
	FLinearColor Web(uint32 Hex, float Alpha = 1)
	{
		FLinearColor C = FLinearColor::FromSRGBColor(FColor((Hex >> 16) & 255, (Hex >> 8) & 255, Hex & 255));
		C.A = Alpha;
		return C;
	}

	/* The web's .game-card (style.css): bottom left, 380 px wide, rows with a 3 px gap, a gradient power meter. */
	struct FCardRow
	{
		FString Text;
		float Size = 14;
		FLinearColor Color = FLinearColor::White;
		bool bBold = false;
		bool bMeter = false;
		float Fill = 0;
	};
	void DrawCard(UCanvas* C, const TArray<FCardRow>& Rows)
	{
		if (!C) return;
		const FVector2D Scr = FHKUi::Size(C);
		const float W = FMath::Min(380.f, (float)Scr.X - 36), PadY = 12, PadX = 14, Gap = 3, Inner = W - 2 * PadX;
		struct FLine { FString Text; float Size, H; FLinearColor Color; bool bBold, bMeter, bFirst; float Fill; };
		TArray<FLine> Lines;
		float H = 2 * PadY;
		for (int32 r = 0; r < Rows.Num(); r++)
		{
			const FCardRow& Row = Rows[r];
			if (r > 0) H += Gap;
			if (Row.bMeter) { Lines.Add({FString(), 0, 16, Row.Color, false, true, true, Row.Fill}); H += 16; continue; }
			TArray<FString> Wrapped = Row.Text.IsEmpty() ? TArray<FString>() : FHKUi::Wrap(C, Row.Text, Row.Size, Inner, Row.bBold);
			if (Wrapped.Num() == 0) { Lines.Add({FString(), Row.Size, 0, Row.Color, Row.bBold, false, true, 0}); continue; }
			for (int32 k = 0; k < Wrapped.Num(); k++) { const float Lh = Row.Size * 1.25f; Lines.Add({Wrapped[k], Row.Size, Lh, Row.Color, Row.bBold, false, k == 0, 0}); H += Lh; }
		}
		const float X = 18, Y0 = (float)Scr.Y - 18 - H;
		FHKUi::Panel(C, X, Y0, W, H, Web(0x11161c, 0xe8 / 255.f), 10);
		float Y = Y0 + PadY;
		for (int32 i = 0; i < Lines.Num(); i++)
		{
			const FLine& Ln = Lines[i];
			if (i > 0 && Ln.bFirst) Y += Gap;
			if (Ln.bMeter)
			{
				// .meter: 8 px, #ffffff1f, margin 4 px; the fill is linear-gradient(90deg, #9ff0a8, #ffd27a, #ff7a6b) over its own width.
				const float My = Y + 4, Mw = Inner, Fw = Mw * FMath::Clamp(Ln.Fill, 0.f, 1.f);
				FHKUi::Panel(C, X + PadX, My, Mw, 8, FLinearColor(1, 1, 1, 0x1f / 255.f), 4);
				const int32 N = FMath::Max(1, FMath::CeilToInt(Fw / 4));
				const FLinearColor A = Web(0x9ff0a8), B = Web(0xffd27a), Cc = Web(0xff7a6b);
				for (int32 k = 0; k < N && Fw > 0; k++)
				{
					const float T = (k + .5f) / N;
					const FLinearColor Col = T < .5f ? FLinearColor::LerpUsingHSV(A, B, T * 2) : FLinearColor::LerpUsingHSV(B, Cc, T * 2 - 1);
					FHKUi::Rect(C, X + PadX + Fw * k / N, My, Fw / N + .5f, 8, Col);
				}
				Y += Ln.H;
				continue;
			}
			if (!Ln.Text.IsEmpty()) FHKUi::Text(C, Ln.Text, X + PadX, Y, Ln.Size, Ln.Color, EHKAlign::Left, Ln.bBold);
			Y += Ln.H;
		}
	}
	const FLinearColor& Ink() { static const FLinearColor C = Web(0xf4f1e8); return C; }
	FLinearColor Small() { FLinearColor C = Web(0xf4f1e8); C.A = .8f; return C; }
}

/* ---------------- JavaScript number helpers ---------------- */

double HKJs::Hypot(double A, double B)
{
	// V8 (builtins/math.tq): normalise by the largest value, Kahan-compensated sum of squares, sqrt(sum) * max.
	if (FMath::IsNaN(A) || FMath::IsNaN(B))
	{
		if (!FMath::IsFinite(A) && !FMath::IsNaN(A)) return std::numeric_limits<double>::infinity();
		if (!FMath::IsFinite(B) && !FMath::IsNaN(B)) return std::numeric_limits<double>::infinity();
		return std::numeric_limits<double>::quiet_NaN();
	}
	const double X = FMath::Abs(A), Y = FMath::Abs(B);
	double Max = 0;
	if (X > Max) Max = X;
	if (Y > Max) Max = Y;
	if (Max == std::numeric_limits<double>::infinity()) return Max;
	if (Max == 0) return 0;
	double Sum = 0, Compensation = 0;
	for (const double V : {X, Y})
	{
		const double N = V / Max;
		const double Summand = N * N - Compensation;
		const double Preliminary = Sum + Summand;
		Compensation = (Preliminary - Sum) - Summand;
		Sum = Preliminary;
	}
	return std::sqrt(Sum) * Max;
}

FString HKJs::ToFixed(double V, int32 Digits)
{
	if (FMath::IsNaN(V)) return TEXT("NaN");
	if (!FMath::IsFinite(V)) return V > 0 ? TEXT("Infinity") : TEXT("-Infinity");
	const bool bNeg = V < 0;
	const double A = FMath::Abs(V);
	// Exact decimal digits of the double, then round half up like Number.prototype.toFixed ("pick the larger n").
	char Buf[512];
	std::snprintf(Buf, sizeof(Buf), "%.*f", Digits + 60, A);
	const FString Long = ANSI_TO_TCHAR(Buf);
	int32 Dot = INDEX_NONE;
	Long.FindChar(TEXT('.'), Dot);
	FString Int = Dot == INDEX_NONE ? Long : Long.Left(Dot), Frac = Dot == INDEX_NONE ? FString() : Long.Mid(Dot + 1);
	FString Kept = Int + Frac.Left(Digits);
	const bool bUp = Frac.Len() > Digits && Frac[Digits] >= TEXT('5');
	if (bUp)
	{
		int32 i = Kept.Len() - 1;
		for (; i >= 0; i--) { if (Kept[i] == TEXT('9')) Kept[i] = TEXT('0'); else { Kept[i] = Kept[i] + 1; break; } }
		if (i < 0) Kept = TEXT("1") + Kept;
	}
	const int32 IntLen = Kept.Len() - Digits;
	FString Out = Digits > 0 ? Kept.Left(IntLen) + TEXT(".") + Kept.Mid(IntLen) : Kept;
	return bNeg ? TEXT("-") + Out : Out;   // (-0.004).toFixed(2) is "-0.00" in JavaScript too
}

FString HKJs::Num(double V)
{
	if (FMath::IsNaN(V)) return TEXT("NaN");
	if (!FMath::IsFinite(V)) return V > 0 ? TEXT("Infinity") : TEXT("-Infinity");
	if (V == FMath::FloorToDouble(V) && FMath::Abs(V) < 1e21) return FString::Printf(TEXT("%.0f"), V == 0 ? 0.0 : V);
	char Buf[64];
	for (int32 P = 1; P <= 17; P++) { std::snprintf(Buf, sizeof(Buf), "%.*g", P, V); if (std::strtod(Buf, nullptr) == V) break; }
	return ANSI_TO_TCHAR(Buf);
}

/* ---------------- Shared ---------------- */

FHKMinigamesContext FHKMinigamesContext::FromGame()
{
	FHKMinigamesContext C;
	C.HeightAt = [](double X, double Z) { return FHKData::Get().HeightWorld(X, Z); };
	C.ToWorld = [](double X, double S) { return FHKData::Get().ToWorld(X, S); };
	C.Sfx = [](const FString& Name, const FHKSfxOpts& Opts) { FHKAudio::Get().Sfx(Name, Opts); };
	TSharedRef<FRandomStream> Stream = MakeShared<FRandomStream>((int32)(FPlatformTime::Cycles64() & 0x7fffffff));
	C.Random = [Stream]() { return (double)Stream->GetFraction(); };
	C.NowMs = []() { return FPlatformTime::Seconds() * 1000.0; };
	C.Data = FHKData::Get().D;
	C.Trees = FHKData::Get().AllTrees;
	C.RigDir = FHKData::DataDir() / TEXT("rigs");
	return C;
}

void FHKMinigame::TakeCharge(double Dt, bool bHolding, FHKMiniInput& In)
{
	if (bHolding) Charge = FMath::Min(1.0, Charge + Dt / 1.2);
	const bool bRelease = !bHolding && Charge > 0;
	const double Power = Charge;
	if (bRelease) Charge = 0;
	In.Charge = Charge;
	In.Power = Power;
	In.bRelease = bRelease;
}

double FHKMinigame::Gauss() const
{
	double U = 0, V = 0;
	while (U == 0) U = Rand();
	while (V == 0) V = Rand();
	return std::sqrt(-2 * std::log(U)) * std::cos(2 * HK::Pi * V);
}

void FHKMinigame::LoadRig(const FString& RigName)
{
	FString Error;
	if (!Rig.LoadFromJson(Ctx.RigDir / (RigName + TEXT(".json")), &Error)) UE_LOG(LogHaarklous, Warning, TEXT("Minigame rig %s: %s"), *RigName, *Error);
}

/* ======================= DISC GOLF ======================= */

FHKDiscGolf::FHKDiscGolf(const FHKMinigamesContext& InCtx) : FHKMinigame(InCtx)
{
	for (const FHKData::FTree& T : Ctx.Trees)
	{
		const FVector2D Wp = Ctx.ToWorld(T.X, T.S);
		TreeWorld.Add({Wp.X, Wp.Y, T.H != 0 && !FMath::IsNaN(T.H) ? T.H : 11});
	}
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (Ctx.Data && Ctx.Data->TryGetArrayField(TEXT("discgolf"), List))
		for (int32 i = 0; i < List->Num(); i++)
		{
			const TSharedPtr<FJsonObject> H = (*List)[i]->AsObject();
			const TArray<double> TeeA = HKJson::Nums(H, TEXT("tee")), BasketA = HKJson::Nums(H, TEXT("basket"));
			if (TeeA.Num() < 2 || BasketA.Num() < 2) continue;
			const FVector2D Tee = Ctx.ToWorld(TeeA[0], TeeA[1]), Basket = Ctx.ToWorld(BasketA[0], BasketA[1]);
			FHole Ho;
			Ho.N = i + 1;
			Ho.Par = HKJson::Num(H, TEXT("par"), 3);
			Ho.Tee = FVector(Tee.X, Ctx.HeightAt(Tee.X, Tee.Y), Tee.Y);
			Ho.Basket = FVector(Basket.X, Ctx.HeightAt(Basket.X, Basket.Y), Basket.Y);
			Ho.Length = HKJs::Hypot(Ho.Basket.X - Ho.Tee.X, Ho.Basket.Z - Ho.Tee.Z);
			Holes.Add(Ho);
		}
	LoadRig(TEXT("discgolf"));
	DiscNode = Rig.Find(TEXT("disc"));
	if (!DiscNode) DiscNode = Rig.Create(TEXT("disc"), Rig.Root);
	DiscNode->bVisible = false;
}

TOptional<FString> FHKDiscGolf::Near(const FVector& P) const
{
	if (bActive) return {};
	for (const FHole& H : Holes)
		if (HKJs::Hypot(P.X - H.Tee.X, P.Z - H.Tee.Z) < 4 && FMath::Abs(P.Y - H.Tee.Y) < 3)
			return TOptional<FString>(FString::Printf(TEXT("□ Play disc golf · Hull %d (par 3, %s m)"), H.N, *HKJs::Num(HK::JsRound(H.Length))));
	return {};
}

TOptional<FHKMiniPose> FHKDiscGolf::Start(const FVector& P)
{
	if (Holes.Num() == 0) return {};
	const FHole* H = &Holes[0];
	for (const FHole& X : Holes) if (HKJs::Hypot(P.X - X.Tee.X, P.Z - X.Tee.Z) < HKJs::Hypot(P.X - H->Tee.X, P.Z - H->Tee.Z)) H = &X;
	bActive = true;
	Hole = H->N - 1;
	Strokes.Init(0, Holes.Num());
	bHasLie = true;
	Lie = H->Tee;
	Disc.bFlying = false;
	DiscNode->bVisible = false;
	Message = FString::Printf(TEXT("Hull %d · Par 3 · Hold R2 (or left mouse) to throw, aim with the camera, left stick X for hyzer/anhyzer."), H->N);
	FHKMiniPose Pose;
	Pose.X = H->Tee.X;
	Pose.Z = H->Tee.Z;
	Pose.Yaw = std::atan2(-(H->Basket.X - H->Tee.X), -(H->Basket.Z - H->Tee.Z));
	return TOptional<FHKMiniPose>(Pose);
}

void FHKDiscGolf::Stop()
{
	bActive = false;
	DiscNode->bVisible = false;
	bHasHud = false;
}

void FHKDiscGolf::ThrowDisc(const FHKMiniInput& In)
{
	const FHole& H = Holes[Hole];
	const double Dist = HKJs::Hypot(H.Basket.X - Lie.X, H.Basket.Z - Lie.Z);
	const bool bPutt = Dist < 11;
	const double Power = In.Power, Speed = bPutt ? 3 + Power * 9 : 9 + Power * 20, Err = (bPutt ? .025 : .05) * (1 + In.Drunk * 2);
	const double Yaw = In.AimYaw + Gauss() * Err;
	const double Pitch = In.AimPitch + (bPutt ? .06 : .09) + Gauss() * Err * .6;
	const FVector D(-std::sin(Yaw) * std::cos(Pitch), std::sin(Pitch), -std::cos(Yaw) * std::cos(Pitch));
	Disc.Pos = FVector(Lie.X + D.X * .5, Lie.Y + 1.2 + D.Y * .5, Lie.Z + D.Z * .5);
	Disc.Vel = FVector(D.X * Speed, D.Y * Speed, D.Z * Speed);
	Disc.Bank = In.Hyzer * .55;
	Disc.bFlying = true;
	Disc.bPutt = bPutt;
	Disc.Time = 0;
	DiscNode->bVisible = true;
	Strokes[Hole]++;
	Sfx(TEXT("throw"));
	FollowT = 0;
}

int32 FHKDiscGolf::Step(double Dt)
{
	FDisc& D = Disc;
	FVector& V = D.Vel;
	const double Speed = std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z);
	D.Time += Dt;
	if (Speed > .3 && D.bFlying)
	{
		// Drag and lift (mid-range driver: glide 4, turn -1, fade 1). Bank tilts lift sideways: turn at high speed, fade when slow.
		const double K = .0135, Lift = .085 * Speed * Speed * .195 * (D.bPutt ? .55 : 1);
		const FVector Up(0, 1, 0), Dir = Norm3(V);
		const FVector Side = Norm3(FVector(Dir.Y * Up.Z - Dir.Z * Up.Y, Dir.Z * Up.X - Dir.X * Up.Z, Dir.X * Up.Y - Dir.Y * Up.X));
		D.Bank += ((Speed > 18 ? -.22 : 0) + (Speed < 15 ? .55 * (1 - Speed / 15) : 0)) * Dt * (D.bPutt ? .3 : 1);
		const double Cb = std::cos(D.Bank), Sb = std::sin(D.Bank);
		const FVector LiftDir(Up.X * Cb + Side.X * Sb, Up.Y * Cb + Side.Y * Sb, Up.Z * Cb + Side.Z * Sb);
		const double DragS = -K * Speed * Speed * Dt, LiftS = Lift * Dt;
		V.X += Dir.X * DragS; V.Y += Dir.Y * DragS; V.Z += Dir.Z * DragS;
		V.X += LiftDir.X * LiftS; V.Y += LiftDir.Y * LiftS; V.Z += LiftDir.Z * LiftS;
		V.Y -= 9.81 * Dt;
	}
	D.Pos.X += V.X * Dt; D.Pos.Y += V.Y * Dt; D.Pos.Z += V.Z * Dt;
	// Trees: trunk or crown hits knock the disc down.
	for (const FTreeW& T : TreeWorld)
	{
		const double Dx = D.Pos.X - T.X, Dz = D.Pos.Z - T.Z;
		if (FMath::Abs(Dx) > 5 || FMath::Abs(Dz) > 5) continue;
		const double Gy = Ctx.HeightAt(T.X, T.Z), R = HKJs::Hypot(Dx, Dz), CrownY = Gy + T.H * .62, CrownR = T.H * .28;
		if ((R < .3 && D.Pos.Y < Gy + T.H * .5) || (HKJs::Hypot(R, (D.Pos.Y - CrownY) * 1.3) < CrownR && Rand() < Dt * 12))
		{
			V.X *= .25; V.Y *= .25; V.Z *= .25;
			V.X += Gauss() * 1.5;
			V.Z += Gauss() * 1.5;
			Sfx(TEXT("thud"), .5);
		}
	}
	// Basket: chains catch a disc at the right height when it is not too fast.
	const FHole& H = Holes[Hole];
	const double Bx = D.Pos.X - H.Basket.X, Bz = D.Pos.Z - H.Basket.Z, By = D.Pos.Y - H.Basket.Y, Rb = HKJs::Hypot(Bx, Bz);
	if (Rb < .33 && By > .55 && By < 1.4)
	{
		if (Speed < 13 || Rand() < .3) { Sfx(TEXT("chains")); D.bFlying = false; V = FVector::ZeroVector; D.Pos = FVector(H.Basket.X, H.Basket.Y + .62, H.Basket.Z); return 1; }
		V.X *= -.25; V.Y *= -.25; V.Z *= -.25;
		Sfx(TEXT("chains"), .5);
	}
	if (Rb < .05 + .025 && By > 0 && By < 1.6) { V.X *= -.4; V.Z *= -.4; Sfx(TEXT("click")); }
	const double G = Ctx.HeightAt(D.Pos.X, D.Pos.Z);
	// Once down, the disc stays on the grass (also downhill) and skids to a stop.
	if (!D.bFlying || D.Pos.Y < G + .015)
	{
		D.Pos.Y = G + .015;
		if (D.bFlying) { D.bFlying = false; Sfx(TEXT("thud"), .4); V.Y = 0; V.X *= .35; V.Y *= .35; V.Z *= .35; }
		V.Y = 0;
		const double Hs = HKJs::Hypot(V.X, V.Z), Dec = 6 * Dt;
		if (Hs <= Dec || D.Time > 12) { V = FVector::ZeroVector; return 2; }
		const double S = (Hs - Dec) / Hs;
		V.X *= S; V.Y *= S; V.Z *= S;
	}
	if (D.Time > 12) { V = FVector::ZeroVector; D.bFlying = false; D.Pos.Y = G + .015; return 2; }
	return 0;
}

FHKMiniResult FHKDiscGolf::Update(double Dt, const FHKMiniInput& In)
{
	FHKMiniResult R;
	if (!bActive) return R;
	R.bNull = false;
	const int32 HoleIndex = Hole;
	const FHole& H = Holes[HoleIndex];
	FDisc& D = Disc;
	if (In.bCancel) { Stop(); R.bDone = true; R.Done = TEXT("Disc golf round ended."); return R; }
	if (D.bFlying || (DiscNode->bVisible && !D.bRest && (D.Vel.X * D.Vel.X + D.Vel.Y * D.Vel.Y + D.Vel.Z * D.Vel.Z) > 0))
	{
		int32 Rs = 0;
		for (int32 i = 0; i < 4 && !Rs; i++) Rs = Step(Dt / 4);
		DiscNode->SetPos(D.Pos.X, D.Pos.Y, D.Pos.Z);
		DiscNode->SetRotation(std::sin(D.Bank) * .6, D.Time * 40, 0);
		if (Rs == 1)
		{
			const int32 Total = Strokes[Hole];
			const double Diff = Total - H.Par;
			const FString Name = Total == 1 ? FString(TEXT("Hole in one!")) : Diff <= -2 ? FString(TEXT("Eagle!")) : Diff == -1 ? FString(TEXT("Birdie!")) : Diff == 0 ? FString(TEXT("Par")) : Diff == 1 ? FString(TEXT("Bogey")) : TEXT("+") + HKJs::Num(Diff);
			Message = FString::Printf(TEXT("Hull %d: %d kast · %s"), H.N, Total, *Name);
			if (Diff < 0) Sfx(TEXT("cheer"));
			bHasPending = true;
			Pending = FPending();
			Pending.bNext = true;
			Pending.T = 2.5;
			D.bRest = true;
		}
		else if (Rs == 2)
		{
			D.bRest = true;
			bHasLie = true;
			Lie = FVector(D.Pos.X, Ctx.HeightAt(D.Pos.X, D.Pos.Z), D.Pos.Z);
			Message = FString::Printf(TEXT("%s m to the basket. Throw again from where the disc landed."), *HKJs::Num(HK::JsRound(HKJs::Hypot(H.Basket.X - D.Pos.X, H.Basket.Z - D.Pos.Z))));
			bHasPending = true;
			Pending = FPending();
			Pending.bLie = true;
			Pending.T = .8;
		}
		// Follow camera behind and above the disc.
		const FVector Back = Norm3(FVector(D.Pos.X - Lie.X, 0, D.Pos.Z - Lie.Z));
		SetHud(State());
		R.bCamera = true;
		R.CameraPos = FVector(D.Pos.X - Back.X * 4, D.Pos.Y + 1.8, D.Pos.Z - Back.Z * 4);
		R.CameraLookAt = D.Pos;
		return R;
	}
	if (bHasPending)
	{
		Pending.T -= Dt;
		if (Pending.T <= 0)
		{
			if (Pending.bNext)
			{
				bHasPending = false;
				DiscNode->bVisible = false;
				D.bRest = false;
				if (Hole + 1 < Holes.Num())
				{
					Hole++;
					const FHole& N = Holes[Hole];
					Lie = N.Tee;
					Message = FString::Printf(TEXT("Hull %d · Par 3"), N.N);
					SetHud(State());
					R.bTeleport = true;
					R.Teleport.X = N.Tee.X;
					R.Teleport.Z = N.Tee.Z;
					R.Teleport.Yaw = std::atan2(-(N.Basket.X - N.Tee.X), -(N.Basket.Z - N.Tee.Z));
					return R;
				}
				double Total = 0, Par = 0;
				for (const int32 S : Strokes) Total = Total + S;
				for (const FHole& X : Holes) Par = Par + X.Par;
				Stop();
				R.bDone = true;
				R.Done = FString::Printf(TEXT("Disc golf finished: %s throws (%s%s vs par %s)."), *HKJs::Num(Total), Total - Par >= 0 ? TEXT("+") : TEXT(""), *HKJs::Num(Total - Par), *HKJs::Num(Par));
				return R;
			}
			bHasPending = false;
			D.bRest = false;
			R.bTeleport = true;
			R.Teleport.X = Lie.X;
			R.Teleport.Z = Lie.Z;
			R.Teleport.Yaw = std::atan2(-(H.Basket.X - Lie.X), -(H.Basket.Z - Lie.Z));
			return R;
		}
	}
	if (In.bRelease && In.Power > .03) ThrowDisc(In);
	SetHud(State(In.Charge));
	R.bLock = true;
	return R;
}

FHKDiscGolf::FHud FHKDiscGolf::State(double InCharge) const
{
	FHud S;
	if (!Holes.IsValidIndex(Hole)) return S;
	const FHole& H = Holes[Hole];
	S.Hole = H.N;
	S.Holes = Holes.Num();
	S.Par = H.Par;
	S.Strokes = Strokes;
	S.Distance = bHasLie ? HK::JsRound(HKJs::Hypot(H.Basket.X - Lie.X, H.Basket.Z - Lie.Z)) : 0;
	S.Charge = InCharge;
	S.Message = Message;
	return S;
}

void FHKDiscGolf::DrawHud(UCanvas* C) const
{
	if (!bHasHud || !C) return;
	const FHud& S = Hud;
	TArray<FString> Throws;
	for (int32 i = 0; i < S.Strokes.Num(); i++) Throws.Add(FString::Printf(TEXT("H%d %d"), i + 1, S.Strokes[i]));
	TArray<FCardRow> Rows;
	Rows.Add({FString::Printf(TEXT("Disc golf · Hull %d/%d · Par %s"), S.Hole, S.Holes, *HKJs::Num(S.Par)), 16, FLinearColor::White, true});
	Rows.Add({TEXT("Throws: ") + FString::Join(Throws, TEXT(" · ")), 14, Ink()});
	Rows.Add({FString::Printf(TEXT("%s m to the basket"), *HKJs::Num(S.Distance)), 14, Ink()});
	FCardRow Meter; Meter.bMeter = true; Meter.Fill = HK::JsRound(S.Charge * 100) / 100.f; Rows.Add(Meter);
	Rows.Add({S.Message, 11.5f, Small()});
	Rows.Add({TEXT("Hold R2 / left mouse, release to throw · left stick ←/→ hyzer · ○ quit"), 11.5f, Small()});
	DrawCard(C, Rows);
}

/* ======================= PETANQUE ======================= */

FHKPetanque::FHKPetanque(const FHKMinigamesContext& InCtx) : FHKMinigame(InCtx)
{
	const TSharedPtr<FJsonObject> P = Ctx.Data ? HKJson::Obj(Ctx.Data, TEXT("petanque")) : TSharedPtr<FJsonObject>();
	const double Px = P ? HKJson::Num(P, TEXT("x")) : 0, Ps = P ? HKJson::Num(P, TEXT("s")) : 0, Rot = P ? HKJson::Num(P, TEXT("rot")) : 0;
	L = P ? HKJson::Num(P, TEXT("length"), 15) : 15;
	W = P ? HKJson::Num(P, TEXT("width"), 4) : 4;
	Centre = Ctx.ToWorld(Px, Ps);
	{
		const FVector2D A = Ctx.ToWorld(Px + std::cos(Rot), Ps + std::sin(Rot));
		Along = FVector2D(A.X - Centre.X, A.Y - Centre.Y);
	}
	Across = FVector2D(-Along.Y, Along.X);
	// The court is levelled at its highest corner so the gravel is never buried in the slope.
	{
		const double Corners[5][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {0, 0}};
		double Max = -std::numeric_limits<double>::infinity();
		for (const auto& C : Corners)
		{
			const double A = C[0], B = C[1];
			const double X = Centre.X + Along.X * A * L / 2 + (-Along.Y) * B * W / 2, Z = Centre.Y + Along.Y * A * L / 2 + Along.X * B * W / 2;
			const double Hh = Ctx.HeightAt(X, Z);
			if (Hh > Max) Max = Hh;
		}
		BaseY = Max + .03;
	}
	Ang = std::atan2(Along.X, Along.Y);
	LoadRig(TEXT("petanque"));
	// Opponent: a CGI player waiting at the court (always there).
	Npc = Rig.Find(TEXT("npc"));
	if (!Npc) Npc = Rig.Create(TEXT("npc"), Rig.Root);
	ArmL = Rig.Find(TEXT("armL"));
	if (!ArmL) { ArmL = Rig.Create(TEXT("armL"), Npc); ArmL->SetPos(-.24, 1.42, 0); }
	ArmR = Rig.Find(TEXT("armR"));
	if (!ArmR) { ArmR = Rig.Create(TEXT("armR"), Npc); ArmR->SetPos(.24, 1.42, 0); }
	ArmR->SetRotation(0, 0, 0);
	{
		const FVector2D H = CourtPoint(-L / 2 - 1.2, 1.4);
		NpcHome = FVector(H.X, Ctx.HeightAt(H.X, H.Y), H.Y);
	}
	Npc->SetPos(NpcHome.X, NpcHome.Y, NpcHome.Z);
	Npc->SetRotation(0, Ang + HK::Pi, 0);
	// Seven ball nodes from the templates (makeBall makes new meshes each mène; reusing seven nodes looks the same).
	const FHKNode* Templates[3] = {Rig.Find(TEXT("jack")), Rig.Find(TEXT("boule_you")), Rig.Find(TEXT("boule_npc"))};
	for (int32 i = 0; i < 7; i++)
	{
		const FHKNode* T = Templates[i == 0 ? 0 : i < 4 ? 1 : 2];
		FHKNode* N = Rig.Create(FString::Printf(TEXT("ball%d"), i), Rig.Root);
		if (T) N->MeshKey = T->MeshKey;
		N->bVisible = false;
		BallNodes.Add(N);
	}
	Rig.Root->UpdateMatrixWorld();
}

FVector2D FHKPetanque::ToCourt(double X, double Z) const
{
	const double Dx = X - Centre.X, Dz = Z - Centre.Y;
	return FVector2D(Dx * Along.X + Dz * Along.Y, Dx * Across.X + Dz * Across.Y);
}

TOptional<FString> FHKPetanque::Near(const FVector& P) const
{
	if (bActive) return {};
	const FVector2D C = ToCourt(P.X, P.Z);
	if (FMath::Abs(C.X) < L / 2 + 3 && FMath::Abs(C.Y) < W / 2 + 3) return TOptional<FString>(FString::Printf(TEXT("□ Play petanque against %s (first to 13)"), *NpcName));
	return {};
}

void FHKPetanque::NewMene(EHKSide First)
{
	Mene++;
	for (FBall& B : Balls) B.Mesh->bVisible = false;
	Balls.Reset();
	auto Make = [this](EHKBallOwner Owner, double R, int32 Index)
	{
		FBall B;
		B.Owner = Owner;
		B.R = R;
		B.Mesh = BallNodes[Index];
		B.Mesh->bVisible = false;
		B.Mesh->SetPos(0, 0, 0);
		B.Mesh->SetScale(1, 1, 1);
		return B;
	};
	Balls.Add(Make(EHKBallOwner::Jack, .015, 0));
	Balls[0].Mesh->SetScale(2, 2, 2);
	for (int32 k = 0; k < 3; k++) Balls.Add(Make(EHKBallOwner::You, .037, 1 + k));
	for (int32 k = 0; k < 3; k++) Balls.Add(Make(EHKBallOwner::Npc, .037, 4 + k));
	bHasBalls = true;
	Thrower = First;
	Phase = EPhase::Jack;
	if (!bHasCircle) { Circle = FVector2D(-L / 2 + 1.2, 0); bHasCircle = true; }
	Message = FString::Printf(TEXT("Mène %d: %s the jack (6–10 m)."), Mene, Thrower == EHKSide::You ? TEXT("you throw") : *(NpcName + TEXT(" throws")));
	AiTimer = 1.6;
}

TOptional<FHKMiniPose> FHKPetanque::Start(const FVector& P)
{
	bActive = true;
	ScoreYou = ScoreNpc = 0;
	Mene = 0;
	Circle = FVector2D(-L / 2 + 1.2, 0);
	bHasCircle = true;
	NewMene(Rand() < .5 ? EHKSide::You : EHKSide::Npc);
	const FVector2D S = CourtPoint(Circle.X, Circle.Y);
	FHKMiniPose Pose;
	Pose.X = S.X;
	Pose.Z = S.Y;
	Pose.Yaw = std::atan2(-Along.X, -Along.Y);
	return TOptional<FHKMiniPose>(Pose);
}

void FHKPetanque::Stop()
{
	bActive = false;
	for (FBall& B : Balls) B.Mesh->bVisible = false;
	bHasHud = false;
	Npc->SetPos(NpcHome.X, Ctx.HeightAt(NpcHome.X, NpcHome.Z), NpcHome.Z);
}

// Throw: aim direction in court space, target distance from power; scatter grows with distance (and drink).
void FHKPetanque::Launch(FBall& Ball, double DirU, double DirV, double Distance, bool bShoot, double Drunk)
{
	const double Sigma = .05 * Distance * (1 + Drunk * 2) + .08;
	const double AimU = Circle.X + DirU * Distance + Gauss() * Sigma;
	const double AimV = Circle.Y + DirV * Distance + Gauss() * Sigma * .8;
	Ball.bAlive = true;
	Ball.Mesh->bVisible = true;
	Ball.U = Circle.X;
	Ball.V = Circle.Y;
	const double LandU = AimU - DirU * (bShoot ? 0 : .5), LandV = AimV - DirV * (bShoot ? 0 : .5), FlightTime = bShoot ? .45 : .9 + Distance * .04;
	FFlight F;
	F.FromU = Circle.X;
	F.FromV = Circle.Y;
	F.FromY = 1.0;
	F.ToU = LandU;
	F.ToV = LandV;
	F.T = 0;
	F.Total = FlightTime;
	F.Peak = bShoot ? .6 : 1.2 + Distance * .25;
	F.RollU = DirU * (bShoot ? 7 : 1.1);
	F.RollV = DirV * (bShoot ? 7 : 1.1);
	Ball.bFlight = true;
	Ball.Flight = F;
	Sfx(TEXT("throw"), .5);
}

bool FHKPetanque::Physics(double Dt)
{
	bool bMoving = false;
	for (FBall& B : Balls)
	{
		if (!B.bAlive) continue;
		if (B.bFlight)
		{
			FFlight& F = B.Flight;
			F.T += Dt;
			const double K = FMath::Min(1.0, F.T / F.Total);
			B.U = F.FromU + (F.ToU - F.FromU) * K;
			B.V = F.FromV + (F.ToV - F.FromV) * K;
			B.Y = F.FromY * (1 - K) + 4 * F.Peak * K * (1 - K);
			if (K >= 1) { B.bFlight = false; B.Y = 0; B.VU = F.RollU; B.VV = F.RollV; Sfx(B.Owner == EHKBallOwner::Jack ? TEXT("click") : TEXT("gravel")); }
			bMoving = true;
			continue;
		}
		const double Sp = HKJs::Hypot(B.VU, B.VV);
		if (Sp > 0) { bMoving = true; const double Dec = 3.2 * Dt, S = FMath::Max(0.0, Sp - Dec) / Sp; B.VU *= S; B.VV *= S; B.U += B.VU * Dt; B.V += B.VV * Dt; }
		// Out of the court (dead ball) if it leaves the boards.
		if (FMath::Abs(B.U) > L / 2 || FMath::Abs(B.V) > W / 2) { B.bAlive = false; B.bDead = true; B.Mesh->bVisible = false; }
	}
	// Collisions on the ground: elastic, mass-weighted (boule 700 g, jack 16 g).
	for (int32 i = 0; i < Balls.Num(); i++)
		for (int32 j = i + 1; j < Balls.Num(); j++)
		{
			FBall& A = Balls[i];
			FBall& B = Balls[j];
			if (!A.bAlive || !B.bAlive || A.bFlight || B.bFlight) continue;
			const double Du = B.U - A.U, Dv = B.V - A.V, D = HKJs::Hypot(Du, Dv), Min = A.R + B.R;
			if (D < Min && D > 1e-6)
			{
				const double Nu = Du / D, Nv = Dv / D, Rel = (A.VU - B.VU) * Nu + (A.VV - B.VV) * Nv;
				const double Ma = A.Owner == EHKBallOwner::Jack ? .016 : .7, Mb = B.Owner == EHKBallOwner::Jack ? .016 : .7;
				if (Rel > 0)
				{
					const double Imp = 2 * Rel / (Ma + Mb) * .9;
					A.VU -= Imp * Mb * Nu; A.VV -= Imp * Mb * Nv; B.VU += Imp * Ma * Nu; B.VV += Imp * Ma * Nv;
					Sfx(TEXT("boule"), FMath::Min(1.0, Rel / 3));
				}
				const double Push = (Min - D) / 2;
				A.U -= Nu * Push; A.V -= Nv * Push; B.U += Nu * Push; B.V += Nv * Push;
			}
		}
	for (FBall& B : Balls)
		if (B.Mesh->bVisible)
		{
			const FVector2D P = CourtPoint(B.U, B.V);
			B.Mesh->SetPos(P.X, BaseY + .02 + B.R * (B.Owner == EHKBallOwner::Jack ? 2 : 1) + B.Y, P.Y);
		}
	return bMoving;
}

double FHKPetanque::Dist(const FBall& B) const { return HKJs::Hypot(B.U - Jack().U, B.V - Jack().V); }

double FHKPetanque::Best(EHKSide Owner) const
{
	double M = std::numeric_limits<double>::infinity();
	if (Owner == EHKSide::None) return M;
	for (int32 k = 0; k < 3; k++) { const FBall& B = Balls[BallIndex(Owner, k)]; if (B.bAlive) M = FMath::Min(M, Dist(B)); }
	return M;
}

EHKSide FHKPetanque::Holding() const
{
	const double Y = Best(EHKSide::You), N = Best(EHKSide::Npc), Inf = std::numeric_limits<double>::infinity();
	return Y == Inf && N == Inf ? EHKSide::None : Y < N ? EHKSide::You : EHKSide::Npc;
}

int32 FHKPetanque::Left(EHKSide Owner) const
{
	int32 N = 0;
	if (Owner == EHKSide::None) return 0;
	for (int32 k = 0; k < 3; k++) { const FBall& B = Balls[BallIndex(Owner, k)]; if (!B.bAlive && !B.bDead) N++; }
	return N;
}

int32 FHKPetanque::FirstFree(EHKSide Owner) const
{
	for (int32 k = 0; k < 3; k++) { const int32 I = BallIndex(Owner, k); if (!Balls[I].bAlive && !Balls[I].bDead) return I; }
	return -1;
}

bool FHKPetanque::NextThrower(EHKSide& Out)
{
	if (Phase == EPhase::Jack) { Phase = EPhase::First; Out = Thrower; return true; }
	if (Phase == EPhase::First) { Phase = EPhase::Play; Out = Thrower == EHKSide::You ? EHKSide::Npc : EHKSide::You; return true; }
	const EHKSide Hold = Holding(), Other = Hold == EHKSide::You ? EHKSide::Npc : EHKSide::You;
	if (Left(Other) > 0) { Out = Other; return true; }
	// Nobody holds (every boule left the court) and you have none left: the web code calls left(null) here, which throws
	// a TypeError, so its update() stops at this point every frame until the player quits. Same here.
	if (Hold == EHKSide::None) return false;
	if (Left(Hold) > 0) { Out = Hold; return true; }
	Out = EHKSide::None;
	return true;
}

EHKSide FHKPetanque::EndMene()
{
	const EHKSide Hold = Holding();
	if (Hold == EHKSide::None) { Message = TEXT("No points this mène."); return EHKSide::Npc; }
	const EHKSide Other = Hold == EHKSide::You ? EHKSide::Npc : EHKSide::You;
	const double Ob = Best(Other);
	int32 Pts = 0;
	for (int32 k = 0; k < 3; k++) { const FBall& B = Balls[BallIndex(Hold, k)]; if (B.bAlive && Dist(B) < Ob) Pts++; }
	(Hold == EHKSide::You ? ScoreYou : ScoreNpc) += Pts;
	Message = FString::Printf(TEXT("%s %d point%s. %d–%d"), Hold == EHKSide::You ? TEXT("You score") : *(NpcName + TEXT(" scores")), Pts, Pts > 1 ? TEXT("s") : TEXT(""), ScoreYou, ScoreNpc);
	if (Pts && Hold == EHKSide::You) Sfx(TEXT("cheer"));
	Circle = FVector2D(FMath::Max(-L / 2 + 1, FMath::Min(L / 2 - 1, Jack().U)), FMath::Max(-W / 2 + .6, FMath::Min(W / 2 - .6, Jack().V)));
	return Hold;
}

void FHKPetanque::NpcTurn()
{
	// Opponent: points at the jack, or shoots your boule when you hold close and it has boules to spare.
	const int32 BallI = Phase == EPhase::Jack ? 0 : FirstFree(EHKSide::Npc);
	const double DirSign = Circle.X < 0 ? 1 : -1;
	if (Phase == EPhase::Jack)
	{
		const double D = 6.5 + Rand() * 3;
		const double V = (Rand() - .5) * .15;
		Launch(Jack(), DirSign, V, D, false);
		return;
	}
	if (BallI < 0) return;
	int32 YourBest = -1;
	for (int32 k = 0; k < 3; k++) { const int32 I = BallIndex(EHKSide::You, k); if (Balls[I].bAlive && (YourBest < 0 || Dist(Balls[I]) < Dist(Balls[YourBest]))) YourBest = I; }
	const bool bShoot = YourBest >= 0 && Dist(Balls[YourBest]) < .35 && Left(EHKSide::Npc) >= 2 && Rand() < .6;
	const FBall& Target = bShoot ? Balls[YourBest] : Jack();
	const double Du = Target.U - Circle.X, Dv = Target.V - Circle.Y, D = HKJs::Hypot(Du, Dv);
	Launch(Balls[BallI], Du / D, Dv / D, D + (bShoot ? 0 : .1), bShoot);
	NpcThrowAnim = .8;
}

FHKMiniResult FHKPetanque::Update(double Dt, const FHKMiniInput& In)
{
	FHKMiniResult R;
	if (!bActive) return R;
	R.bNull = false;
	if (In.bCancel) { Stop(); R.bDone = true; R.Done = FString::Printf(TEXT("Petanque stopped at %d–%d."), ScoreYou, ScoreNpc); return R; }
	const bool bMoving = Physics(Dt);
	NpcThrowAnim = FMath::Max(0.0, NpcThrowAnim - Dt);
	ArmR->SetRotX(NpcThrowAnim > 0 ? -std::sin((1 - NpcThrowAnim / .8) * HK::Pi) * 2.2 : std::sin(Ctx.NowMs() / 900) * .05);
	Npc->SetRotY(Ang + HK::Pi + std::sin(Ctx.NowMs() / 1300) * .1);
	if (!bMoving)
	{
		if (Waiting > 0) Waiting -= Dt;
		else if (Current == EHKSide::None)
		{
			FBall& J = Jack();
			if (Phase == EPhase::Jack && J.bAlive && !J.bFlight)
			{
				const double Jd = HKJs::Hypot(J.U - Circle.X, J.V - Circle.Y);
				if (Jd < 6 || Jd > 10 || J.bDead)
				{
					Message = FString::Printf(TEXT("Jack %s: thrown again (6–10 m)."), *(J.bDead ? FString(TEXT("out")) : TEXT("at ") + HKJs::ToFixed(Jd, 1) + TEXT(" m")));
					J.bAlive = false; J.bDead = false; J.Mesh->bVisible = false; Current = Thrower; AimingFor = EAim::Jack;
				}
				else { EHKSide Next; NextThrower(Next); Current = Next; AimingFor = EAim::Boule; }
			}
			else if (Phase == EPhase::Jack) { Current = Thrower; AimingFor = EAim::Jack; }
			else
			{
				EHKSide Next;
				if (!NextThrower(Next)) return R;
				Current = Next;
				AimingFor = EAim::Boule;
				if (Current == EHKSide::None)
				{
					const EHKSide Winner = EndMene();
					if (ScoreYou >= 13 || ScoreNpc >= 13)
					{
						const bool bWon = ScoreYou >= 13;
						Message = FString::Printf(TEXT("%s %d–%d!"), bWon ? TEXT("You win") : *(NpcName + TEXT(" wins")), ScoreYou, ScoreNpc);
						if (bWon) Sfx(TEXT("cheer"));
						bHasFinish = true;
						Finish = 4;
					}
					else { Waiting = 2.5; NextFirst = Winner; }
				}
			}
			if (Current == EHKSide::Npc) AiTimer = 1.4;
			TurnId = TurnId + 1;
		}
	}
	if (bHasFinish) { Finish -= Dt; if (Finish <= 0) { const FString Msg = Message; bHasFinish = false; Stop(); R.bDone = true; R.Done = Msg; return R; } }
	if (NextFirst != EHKSide::None && Waiting <= 0 && Current == EHKSide::None && !bMoving) { const EHKSide F = NextFirst; NextFirst = EHKSide::None; NewMene(F); Current = EHKSide::None; }
	if (Current == EHKSide::Npc && !bMoving) { AiTimer -= Dt; if (AiTimer <= 0) { NpcTurn(); Current = EHKSide::None; Waiting = .3; } }
	if (Current == EHKSide::You && !bMoving && In.bRelease && In.Power > .03)
	{
		const int32 BallI = AimingFor == EAim::Jack ? 0 : FirstFree(EHKSide::You);
		if (BallI >= 0)
		{
			if (BallI == 0) Jack().Mesh->bVisible = true;
			const double Fx = -std::sin(In.AimYaw), Fz = -std::cos(In.AimYaw);
			const double Du = Fx * Along.X + Fz * Along.Y, Dv = Fx * Across.X + Fz * Across.Y;
			double N = HKJs::Hypot(Du, Dv);
			if (!(N > 0)) N = 1;
			Launch(Balls[BallI], Du / N, Dv / N, 1.5 + In.Power * 11, In.bAlt, In.Drunk);
			Current = EHKSide::None;
			Waiting = .3;
		}
	}
	// HUD: score, mène, boules left, who holds the point and by how much, markers over boules.
	const EHKSide Hold = bHasBalls && Jack().bAlive ? Holding() : EHKSide::None;
	FHud S;
	for (const FBall& B : Balls)
		if (B.bAlive && B.Mesh->bVisible)
		{
			FMarker M;
			M.Pos = B.Mesh->Position;
			M.Label = B.Owner == EHKBallOwner::Jack ? FString(TEXT("jack")) : HKJs::ToFixed(Dist(B), 2) + TEXT(" m");
			M.Owner = B.Owner;
			const EHKSide Side = B.Owner == EHKBallOwner::You ? EHKSide::You : B.Owner == EHKBallOwner::Npc ? EHKSide::Npc : EHKSide::None;
			M.bBest = B.Owner != EHKBallOwner::Jack && Hold == Side && Dist(B) == Best(Side);
			S.Markers.Add(M);
		}
	S.ScoreYou = ScoreYou;
	S.ScoreNpc = ScoreNpc;
	S.Mene = Mene;
	S.You = Left(EHKSide::You);
	S.Npc = Left(EHKSide::Npc);
	S.Turn = Current;
	S.Aim = AimingFor;
	S.Hold = Hold;
	S.Margin = Hold != EHKSide::None ? FMath::Abs(Best(EHKSide::You) - Best(EHKSide::Npc)) : 0;
	S.Message = Message;
	S.Charge = Current == EHKSide::You ? In.Charge : 0;
	S.NpcName = NpcName;
	S.bShoot = In.bAlt;
	SetHud(S);
	if (Current == EHKSide::You && !bMoving)
	{
		const FVector2D Sp = CourtPoint(Circle.X, Circle.Y);
		R.bLock = true;
		R.bStand = true;
		R.Stand = FVector(Sp.X, BaseY, Sp.Y);
		// Turn to face the jack (or down the court for the jack throw) when your turn starts.
		if (bHasFaced ? Faced != TurnId : TurnId != 0)
		{
			bHasFaced = true;
			Faced = TurnId;
			const FVector2D T = AimingFor == EAim::Jack || !Jack().bAlive ? CourtPoint(Circle.X < 0 ? L / 2 : -L / 2, 0) : FVector2D(Jack().Mesh->Position.X, Jack().Mesh->Position.Z);
			R.bFace = true;
			R.Face = std::atan2(-(T.X - Sp.X), -(T.Y - Sp.Y));
		}
		return R;
	}
	R.bLock = true;
	return R;
}

TSharedPtr<FJsonObject> FHKPetanque::Debug() const
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	auto Round2 = [](double V) { return FCString::Atod(*HKJs::ToFixed(V, 2)); };
	if (bHasBalls)
	{
		TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetNumberField(TEXT("u"), Jack().U);
		J->SetNumberField(TEXT("v"), Jack().V);
		J->SetBoolField(TEXT("alive"), Jack().bAlive);
		J->SetBoolField(TEXT("dead"), Jack().bDead);
		O->SetObjectField(TEXT("jack"), J);
		TSharedPtr<FJsonObject> Bo = MakeShared<FJsonObject>();
		for (const EHKSide Side : {EHKSide::You, EHKSide::Npc})
		{
			TArray<TSharedPtr<FJsonValue>> List;
			for (int32 k = 0; k < 3; k++)
			{
				const FBall& B = Balls[BallIndex(Side, k)];
				TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
				E->SetNumberField(TEXT("u"), Round2(B.U));
				E->SetNumberField(TEXT("v"), Round2(B.V));
				E->SetBoolField(TEXT("alive"), B.bAlive);
				E->SetBoolField(TEXT("dead"), B.bDead);
				List.Add(MakeShared<FJsonValueObject>(E));
			}
			Bo->SetArrayField(SideName(Side), List);
		}
		O->SetObjectField(TEXT("boules"), Bo);
	}
	if (bHasCircle)
	{
		TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetNumberField(TEXT("u"), Circle.X);
		C->SetNumberField(TEXT("v"), Circle.Y);
		O->SetObjectField(TEXT("circle"), C);
	}
	if (bHasBalls) O->SetStringField(TEXT("phase"), Phase == EPhase::Jack ? TEXT("jack") : Phase == EPhase::First ? TEXT("first") : TEXT("play"));
	if (Current == EHKSide::None) O->SetField(TEXT("current"), MakeShared<FJsonValueNull>()); else O->SetStringField(TEXT("current"), SideName(Current));
	if (AimingFor == EAim::None) O->SetField(TEXT("aimingFor"), MakeShared<FJsonValueNull>()); else O->SetStringField(TEXT("aimingFor"), AimingFor == EAim::Jack ? TEXT("jack") : TEXT("boule"));
	return O;
}

void FHKPetanque::DrawHud(UCanvas* C) const
{
	if (!bHasHud || !C) return;
	const FHud& S = Hud;
	// Markers over the boules (the web's #markers): label 0.25 m above the ball, outlined when it holds the point.
	for (const FMarker& M : S.Markers)
	{
		FVector2D P;
		if (!FHKUi::Project(M.Pos + FVector(0, .25, 0), P)) continue;
		const FVector2D Sz = FHKUi::Measure(C, M.Label, 11, true);
		const float Bw = Sz.X + 12, Bh = 11 * 1.4f + 2, X = P.X - Bw / 2, Y = P.Y - Bh;
		FHKUi::Panel(C, X, Y, Bw, Bh, FLinearColor(0, 0, 0, .67f), 8);
		if (M.bBest) FHKUi::Frame(C, X - 2, Y - 2, Bw + 4, Bh + 4, 2, Web(0x9ff0a8));
		const FLinearColor Col = M.Owner == EHKBallOwner::You ? Web(0xcfe3ff) : M.Owner == EHKBallOwner::Npc ? Web(0xffd8a8) : Web(0xffe36b);
		FHKUi::Text(C, M.Label, P.X, Y + 1, 11, Col, EHKAlign::Center, true, false);
	}
	const FString Holds = S.Hold != EHKSide::None ? (S.Hold == EHKSide::You ? FString::Printf(TEXT("You hold the point by %s m"), *HKJs::ToFixed(S.Margin, 2)) : FString::Printf(TEXT("%s holds the point by %s m"), *S.NpcName, *HKJs::ToFixed(S.Margin, 2))) : FString(TEXT("Nobody holds the point"));
	const FString Turn = S.Turn == EHKSide::You ? FString::Printf(TEXT("Your turn: %s"), S.Aim == EAim::Jack ? TEXT("throw the jack (6–10 m)") : (S.bShoot ? TEXT("shoot (L2 held)") : TEXT("point (hold L2 to shoot)"))) : S.Turn == EHKSide::Npc ? FString::Printf(TEXT("%s is throwing…"), *S.NpcName) : FString();
	TArray<FCardRow> Rows;
	Rows.Add({FString::Printf(TEXT("Petanque · You %d – %d %s"), S.ScoreYou, S.ScoreNpc, *S.NpcName), 16, FLinearColor::White, true});
	Rows.Add({FString::Printf(TEXT("Mène %d · Boules left: you %d, %s %d"), S.Mene, S.You, *S.NpcName, S.Npc), 14, Ink()});
	Rows.Add({Holds, 14, Ink()});
	Rows.Add({Turn, 14, Ink()});
	FCardRow Meter; Meter.bMeter = true; Meter.Fill = HK::JsRound(S.Charge * 100) / 100.f; Rows.Add(Meter);
	Rows.Add({S.Message, 11.5f, Small()});
	Rows.Add({TEXT("Hold R2 / left mouse for power, release to throw · ○ quit"), 11.5f, Small()});
	DrawCard(C, Rows);
}
