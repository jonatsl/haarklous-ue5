#pragma once

#include "CoreMinimal.h"

class UCanvas;

/*
 * Drawing helpers for the HUD and menus (all 2D UI is drawn on the Unreal canvas from C++).
 * Sizes are in "UI pixels" of a 1920 x 1080 screen; FHKUi scales them to the real resolution.
 * The look follows the web version's style.css: dark translucent rounded panels, white text, accent colours.
 */
enum class EHKAlign : uint8 { Left, Center, Right };

struct HAARKLOUS_API FHKUi
{
	// Colours of the web UI.
	static const FLinearColor PanelBg;
	static const FLinearColor PanelLine;
	static const FLinearColor TextMain;
	static const FLinearColor TextDim;
	static const FLinearColor Accent;
	static const FLinearColor Warn;
	static const FLinearColor Good;

	static float Scale(const UCanvas* C);                       // real pixels per UI pixel
	static FVector2D Size(const UCanvas* C);                    // screen size in UI pixels
	static void Rect(UCanvas* C, float X, float Y, float W, float H, const FLinearColor& Color);
	static void Panel(UCanvas* C, float X, float Y, float W, float H, const FLinearColor& Bg = PanelBg, float Radius = 14);
	static void Frame(UCanvas* C, float X, float Y, float W, float H, float Thickness, const FLinearColor& Color);
	// Returns the size of the drawn text. Size = font size in UI pixels. Text may contain \n.
	static FVector2D Text(UCanvas* C, const FString& S, float X, float Y, float Size, const FLinearColor& Color = TextMain, EHKAlign Align = EHKAlign::Left, bool bBold = false, bool bShadow = true);
	static FVector2D Measure(UCanvas* C, const FString& S, float Size, bool bBold = false);
	// Wraps text to MaxWidth; returns the lines.
	static TArray<FString> Wrap(UCanvas* C, const FString& S, float Size, float MaxWidth, bool bBold = false);
	static void Meter(UCanvas* C, float X, float Y, float W, float H, float Fraction, const FLinearColor& Fill = Accent);
	static void Line(UCanvas* C, const FVector2D& A, const FVector2D& B, float Thickness, const FLinearColor& Color);
	static void Circle(UCanvas* C, const FVector2D& Centre, float Radius, float Thickness, const FLinearColor& Color, int32 Segments = 48);
	static void Disc(UCanvas* C, const FVector2D& Centre, float Radius, const FLinearColor& Color, int32 Segments = 32);

	// Projection of a web-space point to UI pixels with the current camera (set by AHKHud every frame).
	static void SetView(const FVector& CamPosUE, const FRotator& CamRotUE, float FovDeg, const FVector2D& ScreenPx);
	static bool Project(const FVector& WebPos, FVector2D& OutUi);
};
