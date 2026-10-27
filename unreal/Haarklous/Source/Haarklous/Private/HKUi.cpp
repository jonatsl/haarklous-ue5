#include "HKUi.h"
#include "HKMath.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Fonts/SlateFontInfo.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"

const FLinearColor FHKUi::PanelBg(FLinearColor(FColor(0x11, 0x16, 0x1c, 0xe8)));   // --card #11161ce8
const FLinearColor FHKUi::PanelLine(1, 1, 1, 0.14f);
const FLinearColor FHKUi::TextMain(0.96f, 0.96f, 0.94f, 1);
const FLinearColor FHKUi::TextDim(0.72f, 0.74f, 0.76f, 1);
const FLinearColor FHKUi::Accent(FLinearColor(FColor(0xd8, 0xb2, 0x5a)));   // --accent #d8b25a
const FLinearColor FHKUi::Warn(0.92f, 0.22f, 0.18f, 1);
const FLinearColor FHKUi::Good(0.36f, 0.78f, 0.45f, 1);

namespace
{
	UFont* UiFont()
	{
		static TWeakObjectPtr<UFont> Font;
		if (!Font.IsValid()) Font = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
		return Font.Get();
	}
	struct FView { FVector Pos = FVector::ZeroVector; FRotator Rot = FRotator::ZeroRotator; float Fov = 70; FVector2D Px = FVector2D(1920, 1080); } GView;
}

float FHKUi::Scale(const UCanvas* C) { return C ? C->ClipY / 1080.f : 1.f; }
FVector2D FHKUi::Size(const UCanvas* C) { const float S = Scale(C); return C ? FVector2D(C->ClipX / S, C->ClipY / S) : FVector2D(1920, 1080); }

void FHKUi::Rect(UCanvas* C, float X, float Y, float W, float H, const FLinearColor& Color)
{
	if (!C) return;
	const float S = Scale(C);
	FCanvasTileItem Tile(FVector2D(X * S, Y * S), FVector2D(W * S, H * S), Color);
	Tile.BlendMode = SE_BLEND_Translucent;
	C->DrawItem(Tile);
}

void FHKUi::Disc(UCanvas* C, const FVector2D& Centre, float Radius, const FLinearColor& Color, int32 Segments)
{
	if (!C) return;
	const float S = Scale(C);
	TArray<FCanvasUVTri> Tris;
	const FVector2D P = Centre * S;
	for (int32 i = 0; i < Segments; i++)
	{
		const float A0 = 2 * PI * i / Segments, A1 = 2 * PI * (i + 1) / Segments;
		FCanvasUVTri T;
		T.V0_Pos = P; T.V1_Pos = P + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Radius * S; T.V2_Pos = P + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Radius * S;
		T.V0_Color = T.V1_Color = T.V2_Color = Color;
		Tris.Add(T);
	}
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	C->DrawItem(Item);
}

void FHKUi::Panel(UCanvas* C, float X, float Y, float W, float H, const FLinearColor& Bg, float Radius)
{
	if (!C) return;
	Radius = FMath::Min(Radius, FMath::Min(W, H) / 2);
	const float S = Scale(C);
	TArray<FCanvasUVTri> Tris;
	auto Quad = [&](float x0, float y0, float x1, float y1)
	{
		FCanvasUVTri A, B;
		A.V0_Pos = FVector2D(x0, y0) * S; A.V1_Pos = FVector2D(x1, y0) * S; A.V2_Pos = FVector2D(x1, y1) * S;
		B.V0_Pos = FVector2D(x0, y0) * S; B.V1_Pos = FVector2D(x1, y1) * S; B.V2_Pos = FVector2D(x0, y1) * S;
		A.V0_Color = A.V1_Color = A.V2_Color = B.V0_Color = B.V1_Color = B.V2_Color = Bg;
		Tris.Add(A); Tris.Add(B);
	};
	Quad(X + Radius, Y, X + W - Radius, Y + H);
	Quad(X, Y + Radius, X + Radius, Y + H - Radius);
	Quad(X + W - Radius, Y + Radius, X + W, Y + H - Radius);
	const FVector2D Corners[4] = {FVector2D(X + Radius, Y + Radius), FVector2D(X + W - Radius, Y + Radius), FVector2D(X + W - Radius, Y + H - Radius), FVector2D(X + Radius, Y + H - Radius)};
	const float Start[4] = {PI, 1.5f * PI, 0, 0.5f * PI};
	for (int32 k = 0; k < 4; k++)
		for (int32 i = 0; i < 6; i++)
		{
			const float A0 = Start[k] + 0.5f * PI * i / 6, A1 = Start[k] + 0.5f * PI * (i + 1) / 6;
			FCanvasUVTri T;
			T.V0_Pos = Corners[k] * S;
			T.V1_Pos = (Corners[k] + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Radius) * S;
			T.V2_Pos = (Corners[k] + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Radius) * S;
			T.V0_Color = T.V1_Color = T.V2_Color = Bg;
			Tris.Add(T);
		}
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	C->DrawItem(Item);
}

void FHKUi::Frame(UCanvas* C, float X, float Y, float W, float H, float T, const FLinearColor& Color)
{
	Rect(C, X, Y, W, T, Color); Rect(C, X, Y + H - T, W, T, Color);
	Rect(C, X, Y + T, T, H - 2 * T, Color); Rect(C, X + W - T, Y + T, T, H - 2 * T, Color);
}

// The controller symbols of the web UI (□ ✕ ○ △) and arrows are drawn as small icons, so no special font is needed.
static bool IsIcon(TCHAR Ch)
{
	return Ch == 0x25A1 || Ch == 0x2715 || Ch == 0x25CB || Ch == 0x25B3 || Ch == 0x2190 || Ch == 0x2192 || Ch == 0x2191 || Ch == 0x2193 || Ch == 0x25CF || Ch == 0x25CE;
}

static FVector2D MeasureRun(UCanvas* C, const FString& S, float Size, bool bBold)
{
	if (S.IsEmpty()) return FVector2D(0, Size * 1.25f);
	const float Sc = FHKUi::Scale(C);
	const FSlateFontInfo Info(UiFont(), FMath::Max(1, FMath::RoundToInt(Size * Sc)), bBold ? FName("Bold") : FName("Regular"));
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
	{
		const TSharedRef<FSlateFontMeasure> M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		return M->Measure(S, Info) / Sc;
	}
	return FVector2D(S.Len() * Size * .52f, Size * 1.25f);
}

static void DrawIcon(UCanvas* C, TCHAR Ch, float X, float Y, float Size, const FLinearColor& Color)
{
	// Icon box: Size wide, centred on the text line.
	const float R = Size * .36f, Cx = X + Size * .5f, Cy = Y + Size * .68f, T = FMath::Max(1.5f, Size * .1f);
	switch (Ch)
	{
	case 0x25A1: FHKUi::Frame(C, Cx - R, Cy - R, 2 * R, 2 * R, T, Color); break;                                   // □
	case 0x25CB: FHKUi::Circle(C, FVector2D(Cx, Cy), R, T, Color, 20); break;                                       // ○
	case 0x25CF: FHKUi::Disc(C, FVector2D(Cx, Cy), R * .8f, Color, 16); break;                                     // ●
	case 0x25CE: FHKUi::Circle(C, FVector2D(Cx, Cy), R, T, Color, 20); FHKUi::Disc(C, FVector2D(Cx, Cy), R * .4f, Color, 12); break;   // ◎
	case 0x2715: FHKUi::Line(C, FVector2D(Cx - R, Cy - R), FVector2D(Cx + R, Cy + R), T, Color); FHKUi::Line(C, FVector2D(Cx - R, Cy + R), FVector2D(Cx + R, Cy - R), T, Color); break;   // ✕
	case 0x25B3: { const FVector2D A(Cx, Cy - R), B(Cx + R, Cy + R * .8f), D(Cx - R, Cy + R * .8f); FHKUi::Line(C, A, B, T, Color); FHKUi::Line(C, B, D, T, Color); FHKUi::Line(C, D, A, T, Color); break; }   // △
	default:
	{
		// Arrows ← → ↑ ↓
		FVector2D Dir = Ch == 0x2190 ? FVector2D(-1, 0) : Ch == 0x2192 ? FVector2D(1, 0) : Ch == 0x2191 ? FVector2D(0, -1) : FVector2D(0, 1);
		const FVector2D Tip(Cx + Dir.X * R, Cy + Dir.Y * R), Tail(Cx - Dir.X * R, Cy - Dir.Y * R), Side(-Dir.Y, Dir.X);
		FHKUi::Line(C, Tail, Tip, T, Color);
		FHKUi::Line(C, Tip, Tip - Dir * R * .7f + Side * R * .6f, T, Color);
		FHKUi::Line(C, Tip, Tip - Dir * R * .7f - Side * R * .6f, T, Color);
	}
	}
}

FVector2D FHKUi::Measure(UCanvas* C, const FString& S, float Size, bool bBold)
{
	if (S.IsEmpty()) return FVector2D::ZeroVector;
	TArray<FString> Lines;
	S.ParseIntoArray(Lines, TEXT("\n"), false);
	FVector2D Out(0, 0);
	for (const FString& L : Lines)
	{
		float W = 0;
		FString Run;
		for (TCHAR Ch : L)
		{
			if (IsIcon(Ch)) { W += MeasureRun(C, Run, Size, bBold).X + Size; Run.Empty(); }
			else Run.AppendChar(Ch);
		}
		W += MeasureRun(C, Run, Size, bBold).X;
		Out.X = FMath::Max(Out.X, W);
		Out.Y += Size * 1.25f;
	}
	return Out;
}

FVector2D FHKUi::Text(UCanvas* C, const FString& S, float X, float Y, float Size, const FLinearColor& Color, EHKAlign Align, bool bBold, bool bShadow)
{
	if (!C || S.IsEmpty()) return FVector2D::ZeroVector;
	const float Sc = Scale(C);
	const FSlateFontInfo Info(UiFont(), FMath::Max(1, FMath::RoundToInt(Size * Sc)), bBold ? FName("Bold") : FName("Regular"));
	TArray<FString> Lines;
	S.ParseIntoArray(Lines, TEXT("\n"), false);
	FVector2D Total(0, 0);
	float Ly = Y;
	for (const FString& L : Lines)
	{
		const float Lw = Measure(C, L, Size, bBold).X;
		float Cx = X + (Align == EHKAlign::Center ? -Lw / 2 : Align == EHKAlign::Right ? -Lw : 0);
		FString Run;
		auto Flush = [&]()
		{
			if (Run.IsEmpty()) return;
			FCanvasTextItem Item(FVector2D(Cx * Sc, Ly * Sc), FText::FromString(Run), Info, Color);
			if (bShadow) Item.EnableShadow(FLinearColor(0, 0, 0, 0.7f * Color.A), FVector2D(1.5f, 1.5f));
			C->DrawItem(Item);
			Cx += MeasureRun(C, Run, Size, bBold).X;
			Run.Empty();
		};
		for (TCHAR Ch : L)
		{
			if (IsIcon(Ch)) { Flush(); if (bShadow) DrawIcon(C, Ch, Cx + 1.2f, Ly + 1.2f, Size, FLinearColor(0, 0, 0, .6f * Color.A)); DrawIcon(C, Ch, Cx, Ly, Size, Color); Cx += Size; }
			else Run.AppendChar(Ch);
		}
		Flush();
		Total.X = FMath::Max(Total.X, Lw);
		Ly += Size * 1.25f;
		Total.Y += Size * 1.25f;
	}
	return Total;
}

TArray<FString> FHKUi::Wrap(UCanvas* C, const FString& S, float Size, float MaxWidth, bool bBold)
{
	TArray<FString> Out, Paras;
	S.ParseIntoArray(Paras, TEXT("\n"), false);
	for (const FString& P : Paras)
	{
		TArray<FString> Words;
		P.ParseIntoArray(Words, TEXT(" "), true);
		FString Line;
		for (const FString& W : Words)
		{
			const FString Try = Line.IsEmpty() ? W : Line + TEXT(" ") + W;
			if (!Line.IsEmpty() && Measure(C, Try, Size, bBold).X > MaxWidth) { Out.Add(Line); Line = W; }
			else Line = Try;
		}
		Out.Add(Line);
	}
	return Out;
}

void FHKUi::Meter(UCanvas* C, float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill)
{
	Panel(C, X, Y, W, H, FLinearColor(1, 1, 1, 0.16f), H / 2);
	const float F = FMath::Clamp(Fraction, 0.f, 1.f);
	if (F > 0) Panel(C, X, Y, FMath::Max(H, W * F), H, Fill, H / 2);
}

void FHKUi::Line(UCanvas* C, const FVector2D& A, const FVector2D& B, float Thickness, const FLinearColor& Color)
{
	if (!C) return;
	const float S = Scale(C);
	FCanvasLineItem L(A * S, B * S);
	L.SetColor(Color);
	L.LineThickness = Thickness * S;
	L.BlendMode = SE_BLEND_Translucent;
	C->DrawItem(L);
}

void FHKUi::Circle(UCanvas* C, const FVector2D& Centre, float Radius, float Thickness, const FLinearColor& Color, int32 Segments)
{
	for (int32 i = 0; i < Segments; i++)
	{
		const float A0 = 2 * PI * i / Segments, A1 = 2 * PI * (i + 1) / Segments;
		Line(C, Centre + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Radius, Centre + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Radius, Thickness, Color);
	}
}

void FHKUi::SetView(const FVector& CamPosUE, const FRotator& CamRotUE, float FovDeg, const FVector2D& ScreenPx)
{
	GView.Pos = CamPosUE; GView.Rot = CamRotUE; GView.Fov = FovDeg; GView.Px = ScreenPx;
}

bool FHKUi::Project(const FVector& WebPos, FVector2D& OutUi)
{
	// Perspective projection with a horizontal field of view (Unreal's convention).
	const FVector Rel = GView.Rot.UnrotateVector(HK::ToUE(WebPos) - GView.Pos);   // X forward, Y right, Z up
	if (Rel.X <= 1) return false;
	const double Half = FMath::Tan(FMath::DegreesToRadians(GView.Fov) / 2);
	const double Aspect = GView.Px.X / FMath::Max(1.0, GView.Px.Y);
	const double Sx = Rel.Y / (Rel.X * Half), Sy = Rel.Z / (Rel.X * Half) * Aspect;
	const double UiW = 1080.0 * Aspect, UiH = 1080.0;
	OutUi = FVector2D((Sx * .5 + .5) * UiW, (-Sy * .5 + .5) * UiH);
	return true;
}
