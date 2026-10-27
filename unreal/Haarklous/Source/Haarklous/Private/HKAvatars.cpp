#include "HKAvatars.h"
#include "Haarklous.h"
#include "HKData.h"
#include "HKUi.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Misc/Paths.h"

namespace
{
	const uint32 SLOT_COLORS[5] = {0xc0392b, 0x2471a3, 0x1e8449, 0xd68910, 0x7d3c98};
	const uint32 SLOT_CSS[5] = {0xe05a4c, 0x4d9de0, 0x3fbf6f, 0xf0a830, 0xa768c9};
	// Canvas sizes of the web's sprites (512 px wide; height = round(512 * h / w)).
	constexpr double TagW = 512, TagH = 102, BubbleW = 512, BubbleH = 149;

	FLinearColor Css(uint8 R, uint8 G, uint8 B, double A = 1) { return FLinearColor::FromSRGBColor(FColor(R, G, B, (uint8)FMath::RoundToInt(A * 255))); }
	FLinearColor CssHex(uint32 Hex, double A = 1) { return Css((Hex >> 16) & 255, (Hex >> 8) & 255, Hex & 255, A); }

	// Where a camera-facing sprite of the web lands on screen: its centre in UI pixels and UI pixels per metre there.
	bool SpriteOnScreen(const FVector& Centre, const FVector& Cam, FVector2D& OutCentre, double& OutPxPerM)
	{
		if (!FHKUi::Project(Centre, OutCentre)) return false;
		FVector Dir = Centre - Cam;
		if (Dir.IsNearlyZero()) return false;
		Dir.Normalize();
		FVector Right = FVector::CrossProduct(Dir, FVector(0, 1, 0));
		if (Right.SizeSquared() < 1e-8) Right = FVector(1, 0, 0);
		Right.Normalize();
		FVector2D A, B;
		if (!FHKUi::Project(Centre - Right * .5, A) || !FHKUi::Project(Centre + Right * .5, B)) return false;
		OutPxPerM = FVector2D::Distance(A, B);
		return OutPxPerM > 0;
	}

	void Triangle(UCanvas* C, const FVector2D& A, const FVector2D& B, const FVector2D& D, const FLinearColor& Color)
	{
		const float S = FHKUi::Scale(C);
		TArray<FCanvasUVTri> Tris;
		FCanvasUVTri T;
		T.V0_Pos = A * S; T.V1_Pos = B * S; T.V2_Pos = D * S;
		T.V0_Color = T.V1_Color = T.V2_Color = Color;
		Tris.Add(T);
		FCanvasTriangleItem Item(Tris, nullptr);
		Item.BlendMode = SE_BLEND_Translucent;
		C->DrawItem(Item);
	}
}

uint32 HKAvatars::SlotColorHex(int32 Slot) { return SLOT_COLORS[((Slot % 5) + 5) % 5]; }
uint32 HKAvatars::SlotCssHex(int32 Slot) { return SLOT_CSS[((Slot % 5) + 5) % 5]; }
FLinearColor HKAvatars::SlotCss(int32 Slot) { return CssHex(SlotCssHex(Slot)); }

FString HKAvatars::RigPath(const FString& RigName) { return FHKData::DataDir() / TEXT("rigs") / (RigName + TEXT(".json")); }

TUniquePtr<FHKAvatar> HKAvatars::Avatar(int32 Slot, const FString& Name) { return MakeUnique<FHKAvatar>(Slot, Name); }

TUniquePtr<FHKRig> HKAvatars::Drone()
{
	TUniquePtr<FHKRig> Rig = MakeUnique<FHKRig>();
	FString Error;
	if (!Rig->LoadFromJson(RigPath(TEXT("avatar_drone")), &Error)) UE_LOG(LogHaarklous, Warning, TEXT("Remote drone: %s"), *Error);
	Rig->Root->Position = FVector::ZeroVector;
	Rig->Root->SetQuat(FQuat::Identity);
	return Rig;
}

FHKAvatar::FHKAvatar(int32 InSlot, const FString& InName) : Slot(InSlot)
{
	RigName = FString::Printf(TEXT("avatar_%d"), ((Slot % 5) + 5) % 5);
	FString Error;
	bFromRigFile = Rig.LoadFromJson(HKAvatars::RigPath(RigName), &Error);
	auto Find = [this](const TCHAR* N) { return Rig.Find(N); };
	if (bFromRigFile)
	{
		Hips = Find(TEXT("hips")); Torso = Find(TEXT("torso")); Neck = Find(TEXT("neck")); Head = Find(TEXT("head")); Bottle = Find(TEXT("bottle"));
		L.Arm.Sh = Find(TEXT("L.arm.sh")); L.Arm.El = Find(TEXT("L.arm.el")); L.Leg.Hip = Find(TEXT("L.leg.hip")); L.Leg.Kn = Find(TEXT("L.leg.kn"));
		R.Arm.Sh = Find(TEXT("R.arm.sh")); R.Arm.El = Find(TEXT("R.arm.el")); R.Leg.Hip = Find(TEXT("R.leg.hip")); R.Leg.Kn = Find(TEXT("R.leg.kn"));
		const FHKNode* All[] = {Hips, Torso, Neck, Head, Bottle, L.Arm.Sh, L.Arm.El, L.Leg.Hip, L.Leg.Kn, R.Arm.Sh, R.Arm.El, R.Leg.Hip, R.Leg.Kn};
		for (const FHKNode* N : All) if (!N) { bFromRigFile = false; Error = TEXT("a part is missing in ") + RigName; }
	}
	if (!bFromRigFile)
	{
		UE_LOG(LogHaarklous, Warning, TEXT("Avatar rig: %s; building the skeleton without meshes"), *Error);
		BuildSkeleton();
	}
	// The export's root carries the export pose; the group starts at the origin.
	Rig.Root->Position = FVector::ZeroVector;
	Rig.Root->SetQuat(FQuat::Identity);
	Rig.Root->Scale = FVector::OneVector;
	SetName(InName);
	Pose(FHKPoseArgs());
}

void FHKAvatar::BuildSkeleton()
{
	// Same groups and positions as game-avatars.js avatar().
	FHKRig& G = Rig;
	Hips = G.Create(TEXT("hips"), G.Root); Hips->SetPos(0, .95, 0);
	Torso = G.Create(TEXT("torso"), Hips);
	Neck = G.Create(TEXT("neck"), Torso); Neck->SetPos(0, .58, 0);
	Head = G.Create(TEXT("head"), Neck); Head->SetPos(0, .14, 0);
	auto Arm = [&](double S, const TCHAR* P)
	{
		FArm A;
		A.Sh = G.Create(FString(P) + TEXT(".arm.sh"), Torso); A.Sh->SetPos(S * .22, .5, 0);
		A.El = G.Create(FString(P) + TEXT(".arm.el"), A.Sh); A.El->SetPos(0, -.29, 0);
		return A;
	};
	auto Leg = [&](double S, const TCHAR* P)
	{
		FLeg Out;
		Out.Hip = G.Create(FString(P) + TEXT(".leg.hip"), Hips); Out.Hip->SetPos(S * .1, -.05, 0);
		Out.Kn = G.Create(FString(P) + TEXT(".leg.kn"), Out.Hip); Out.Kn->SetPos(0, -.43, 0);
		return Out;
	};
	L.Arm = Arm(-1, TEXT("L")); L.Leg = Leg(-1, TEXT("L"));
	R.Arm = Arm(1, TEXT("R")); R.Leg = Leg(1, TEXT("R"));
	Bottle = G.Create(TEXT("bottle"), R.Arm.El); Bottle->SetPos(0, -.3, -.04); Bottle->bVisible = false;
}

void FHKAvatar::SetName(const FString& N)
{
	if (N == NameNow) return;
	NameNow = N;
}

void FHKAvatar::Say(const FString& Text)
{
	BubbleText = Text;
	bBubbleVisible = true;
	BubbleT = 7;
}

void FHKAvatar::Pose(const FHKPoseArgs& A)
{
	const FString& Stance = A.Stance;
	Phase += A.Speed * A.Dt * 3.2;
	const double Sw = FMath::Sin(Phase), Amp = FMath::Min(1.0, A.Speed / 3.6);
	auto Set = [](FHKNode* O, double X = 0, double Y = 0, double Z = 0) { O->SetRotation(X, Y, Z); };
	Hips->SetPos(0, .95, 0); Set(Hips); Set(Torso); Set(Neck); Set(Head);
	{
		const double S[2] = {1, -1};
		FSide* Sides[2] = {&L, &R};
		for (int32 i = 0; i < 2; i++) { Set(Sides[i]->Leg.Hip); Set(Sides[i]->Leg.Kn); Set(Sides[i]->Arm.Sh, 0, 0, S[i] * .08); Set(Sides[i]->Arm.El); }
	}
	FSide* Both[2] = {&L, &R};
	const bool bRide = Stance == TEXT("ride") || Stance == TEXT("pillion");
	const bool bSeated = Stance == TEXT("sit") || Stance == TEXT("drive") || bRide;
	if (bSeated)
	{
		Hips->SetPos(0, 0, 0);
		for (FSide* Side : Both) { Set(Side->Leg.Hip, bRide ? 1.15 : 1.45); Set(Side->Leg.Kn, bRide ? -1.75 : -1.45); }
		if (bRide) { L.Leg.Hip->SetRotZ(-.22); R.Leg.Hip->SetRotZ(.22); Torso->SetRotX(Stance == TEXT("ride") ? -.42 : -.15); }
		if (Stance == TEXT("drive") || Stance == TEXT("ride")) for (FSide* Side : Both) { Side->Arm.Sh->SetRotX(Stance == TEXT("ride") ? 1.1 : .95); Side->Arm.El->SetRotX(.45); }
		else for (FSide* Side : Both) { Side->Arm.Sh->SetRotX(.35); Side->Arm.El->SetRotX(.9); }
	}
	else if (Stance == TEXT("crouch"))
	{
		Hips->Position.Y = .62; Torso->SetRotX(-.35);
		for (FSide* Side : Both) { Set(Side->Leg.Hip, 1.25 + Sw * .25 * Amp); Set(Side->Leg.Kn, -1.95); Side->Arm.Sh->SetRotX(.5); Side->Arm.El->SetRotX(.6); }
	}
	else if (Stance == TEXT("prone"))
	{
		Hips->Position.Y = .17; Hips->SetRotX(-HK::Pi / 2 + .05); Neck->SetRotX(.9);
		L.Leg.Hip->SetRotX(Sw * .25 * Amp); R.Leg.Hip->SetRotX(-Sw * .25 * Amp);
		for (FSide* Side : Both) { Side->Arm.Sh->SetRotX(2.6); Side->Arm.El->SetRotX(.9); }
	}
	else
	{
		L.Leg.Hip->SetRotX(Sw * .55 * Amp); R.Leg.Hip->SetRotX(-Sw * .55 * Amp);
		L.Leg.Kn->SetRotX(-FMath::Max(0.0, -Sw) * .9 * Amp); R.Leg.Kn->SetRotX(-FMath::Max(0.0, Sw) * .9 * Amp);
		L.Arm.Sh->SetRotX(-Sw * .45 * Amp); R.Arm.Sh->SetRotX(Sw * .45 * Amp);
		L.Arm.El->SetRotX(.25 + .3 * Amp); R.Arm.El->SetRotX(.25 + .3 * Amp);
		Hips->Position.Y = .95 + FMath::Abs(FMath::Cos(Phase)) * .03 * Amp; Torso->SetRotX(-.06 * Amp);
		if (Stance == TEXT("drone")) for (FSide* Side : Both) { Side->Arm.Sh->SetRotX(.8); Side->Arm.El->SetRotX(.9); }
	}
	Bottle->bVisible = A.bBeer;
	if (A.bBeer && !bSeated) { R.Arm.Sh->SetRotX(.55); R.Arm.El->SetRotX(1.5); }
	if (A.bBeer && bSeated) { R.Arm.Sh->SetRotX(.5); R.Arm.El->SetRotX(1.6); }
	TagY = bSeated ? 1.2 : Stance == TEXT("prone") ? .7 : Stance == TEXT("crouch") ? 1.75 : 2.08;
	BubbleY = TagY + .45;
}

void FHKAvatar::Update(double Dt, const FVector& CameraPos)
{
	if (BubbleT > 0 && (BubbleT -= Dt) <= 0) bBubbleVisible = false;
	LastCamera = CameraPos;
	const double D = FVector::Distance(CameraPos, Rig.Root->GetWorldPosition());
	bTagVisible = D < 90;
	TagK = FMath::Max(1.0, D / 14);
	BubbleK = FMath::Max(1.0, D / 18);
	Rig.Root->UpdateMatrixWorld();
}

void FHKAvatar::DrawHud(UCanvas* C) const
{
	if (!C || !Rig.Root->IsVisibleInTree()) return;
	const FVector Cam = LastCamera;
	// Name tag: rounded dark plate, slot colour square, bold name.
	if (bTagVisible && !NameNow.IsEmpty())
	{
		FVector2D P; double Ppm = 0;
		if (SpriteOnScreen(Rig.Root->LocalToWorld(FVector(0, TagY, 0)), Cam, P, Ppm))
		{
			const double S = 1.3 * TagK * Ppm / TagW;          // UI pixels per canvas pixel
			const double X0 = P.X - TagW / 2 * S, Y0 = P.Y - TagH / 2 * S;
			const double W = FMath::Min(TagW - 10, (double)FHKUi::Measure(C, NameNow, 54, true).X + 50);
			FHKUi::Panel(C, X0 + (TagW - W) / 2 * S, Y0 + 8 * S, W * S, (TagH - 16) * S, Css(10, 14, 20, .62), 26 * S);
			FHKUi::Rect(C, X0 + ((TagW - W) / 2 + 16) * S, Y0 + (TagH / 2 - 8) * S, 16 * S, 16 * S, HKAvatars::SlotCss(Slot));
			const float Size = 54 * S;
			const FVector2D Sz = FHKUi::Measure(C, NameNow, Size, true);
			FHKUi::Text(C, NameNow, X0 + (TagW / 2 + 12) * S, Y0 + (TagH / 2 + 2) * S - Sz.Y / 2, Size, FLinearColor::White, EHKAlign::Center, true, false);
		}
	}
	// Chat bubble: white rounded box with a pointer, up to three lines.
	if (bBubbleVisible && !BubbleText.IsEmpty())
	{
		FVector2D P; double Ppm = 0;
		if (SpriteOnScreen(Rig.Root->LocalToWorld(FVector(0, BubbleY, 0)), Cam, P, Ppm))
		{
			const double S = 2.4 * BubbleK * Ppm / BubbleW;
			const double X0 = P.X - BubbleW / 2 * S, Y0 = P.Y - BubbleH / 2 * S;
			TArray<FString> Words, Lines;
			BubbleText.ParseIntoArrayWS(Words);
			FString Line;
			for (const FString& Wd : Words)
			{
				const FString T = Line.IsEmpty() ? Wd : Line + TEXT(" ") + Wd;
				if (FHKUi::Measure(C, T, 40, true).X > BubbleW - 60 && !Line.IsEmpty()) { Lines.Add(Line); Line = Wd; }
				else Line = T;
			}
			if (!Line.IsEmpty()) Lines.Add(Line);
			const int32 Shown = FMath::Min(3, Lines.Num());
			const double H = 26 + Shown * 46, Top = BubbleH - H - 18, ClipTop = FMath::Max(0.0, Top);
			const FLinearColor Fill = Css(255, 255, 255, .93);
			FHKUi::Panel(C, X0 + 10 * S, Y0 + ClipTop * S, (BubbleW - 20) * S, (BubbleH - 18 - ClipTop) * S, Fill, 24 * S);
			Triangle(C, FVector2D(X0 + (BubbleW / 2 - 16) * S, Y0 + (BubbleH - 19) * S), FVector2D(X0 + BubbleW / 2 * S, Y0 + (BubbleH - 2) * S), FVector2D(X0 + (BubbleW / 2 + 16) * S, Y0 + (BubbleH - 19) * S), Fill);
			const float Size = 40 * S;
			for (int32 i = 0; i < Shown; i++)
			{
				const double Cy = Top + 36 + i * 46;
				if (Cy < 20) continue;   // outside the web's canvas
				const FVector2D Sz = FHKUi::Measure(C, Lines[i], Size, true);
				FHKUi::Text(C, Lines[i], X0 + BubbleW / 2 * S, Y0 + Cy * S - Sz.Y / 2, Size, Css(0x16, 0x19, 0x1d), EHKAlign::Center, true, false);
			}
		}
	}
}
