#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"

/*
 * Input, the same way the web game reads it (gamepad-controls.js + readInput() in game-main.js).
 *
 * The controller is read as a W3C "standard gamepad": 18 buttons with values 0..1 and 4 axes (y positive = stick down).
 * Button indices: 0 cross, 1 circle, 2 square, 3 triangle, 4 L1, 5 R1, 6 L2, 7 R2, 8 share, 9 options, 10 L3, 11 R3,
 * 12-15 D-pad up/down/left/right, 16 PS, 17 touchpad. Sources, in order of preference:
 *   1. a PS4 DualShock 4 read directly over Windows HID (USB or Bluetooth; Unreal does not see it as a gamepad),
 *   2. an XInput pad (Xbox controller, or a DualShock 4 through Steam Input / DS4Windows),
 *   3. a simulated pad set by the test driver (like tests/browser-harness.js's fakePad).
 * Keyboard and mouse come from a Slate input pre-processor (it sees everything before the viewport does).
 */
struct FHKPadState
{
	bool bConnected = false;
	FString Id;
	float Axes[4] = {0, 0, 0, 0};
	float Buttons[18] = {0};
	bool Pressed[18] = {false};
};

// gamepad-controls.js read(): {move, look, run (R2), aim (L2), buttons}
struct FHKPadRead
{
	FVector2D Move = FVector2D::ZeroVector, Look = FVector2D::ZeroVector;
	double Run = 0, Aim = 0;
	TMap<FName, bool> Buttons;
};

struct FHKMouse
{
	double Dx = 0, Dy = 0, Wheel = 0;
	bool bLeft = false, bRight = false, bLeftEdge = false, bLeftUp = false;
};

// readInput() result
struct HAARKLOUS_API FHKInputFrame
{
	FVector2D Move = FVector2D::ZeroVector, Look = FVector2D::ZeroVector;
	TSet<FName> Held, Pressed, Released;
	double R2 = 0, L2 = 0;
	bool bGamepad = false;
	FHKMouse Mouse;
	bool IsHeld(const FName& N) const { return Held.Contains(N); }
	bool WasPressed(const FName& N) const { return Pressed.Contains(N); }
};

namespace HKPad
{
	constexpr double DeadZoneValue = .14;
	HAARKLOUS_API FVector2D DeadZone(double X, double Y);
	HAARKLOUS_API FHKPadRead Read(const FHKPadState& Pad);
	HAARKLOUS_API TArray<FName> PressedEdges(const FHKPadRead* Previous, const FHKPadRead& Current);
	HAARKLOUS_API const TArray<FName>& Names();   // standard mapping names in index order
	// Parses one DualShock 4 HID input report (USB 0x01, Bluetooth short 0x01 or full 0x11) into a standard pad.
	HAARKLOUS_API bool ParseDS4Report(const uint8* Data, int32 Size, FHKPadState& Out);
}

/* Collects keyboard, mouse, XInput and DualShock 4 input between frames; ReadFrame() is the web's readInput(). */
class HAARKLOUS_API FHKInputProcessor : public IInputProcessor
{
public:
	FHKInputProcessor();
	virtual ~FHKInputProcessor();
	static TSharedPtr<FHKInputProcessor> Get();
	static void Register();
	static void Unregister();

	// IInputProcessor
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent, const FPointerEvent* InGestureEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("Haarklous input"); }

	// Called once per game frame. Mode is the player's mode (W/S drive vehicles, as in the web code).
	FHKInputFrame ReadFrame(bool bInVehicle);
	// Typed characters (from UHKGameViewportClient::InputChar).
	void HandleChar(TCHAR C);
	// Text entry (chat, lobby code, name): while a text field is open, typed characters go here instead of the game.
	bool bTextEntry = false;
	FString TypedText;          // characters typed since the last read
	TArray<FName> TextKeys;     // Enter, Escape, BackSpace pressed while typing
	// Tests: a simulated controller replaces the real ones when set.
	bool bUseSimulatedPad = false;
	FHKPadState SimulatedPad;
	// Keys held (web key codes: KeyW, Space, ShiftLeft, ArrowUp ...), for tests too.
	void SimulateKey(const FString& Code, bool bDown);
	void SimulateMouse(double Dx, double Dy, int32 Button = -1, bool bDown = false);
	FHKPadState CurrentPad();
	bool bMouseCaptured = true;
	bool bWasActive = true;     // the window had focus last frame (keys are cleared when it loses it)
	FString PadDescription() const;

private:
	TSet<FString> Keys, KeyEdges;
	FHKMouse Mouse;
	FHKPadRead PadPrev;
	bool bHasPadPrev = false;
	FHKPadState XPad;           // XInput, built from Slate gamepad events
	double XPadLastInput = -1;
	class FHKDualShockReader* DS4 = nullptr;
	static FString CodeOf(const FKey& Key);
};
