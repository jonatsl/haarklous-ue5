#include "HKInput.h"
#include "Haarklous.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <setupapi.h>
#include <hidsdi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// ---------- gamepad-controls.js ----------
namespace HKPad
{
	const TArray<FName>& Names()
	{
		static const TArray<FName> N = {"cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2", "share", "options", "l3", "r3", "up", "down", "left", "right", "ps", "touchpad"};
		return N;
	}

	FVector2D DeadZone(double X, double Y)
	{
		const double Length = FMath::Sqrt(X * X + Y * Y);
		if (Length <= DeadZoneValue) return FVector2D::ZeroVector;
		// "Rescale so output starts at 0 just outside the dead zone and is capped at 1."
		const double Scaled = FMath::Min(1.0, (Length - DeadZoneValue) / (1 - DeadZoneValue)) / Length;
		return FVector2D(X * Scaled, Y * Scaled);
	}

	FHKPadRead Read(const FHKPadState& Pad)
	{
		// The pad is always presented in the W3C standard mapping (see FHKInputProcessor::CurrentPad).
		FHKPadRead R;
		for (int32 i = 0; i < 18; i++) R.Buttons.Add(Names()[i], Pad.Pressed[i] || Pad.Buttons[i] > .5f);
		R.Look = DeadZone(Pad.Axes[2], Pad.Axes[3]);
		R.Move = DeadZone(Pad.Axes[0], Pad.Axes[1]);
		R.Run = Pad.Buttons[7] ? Pad.Buttons[7] : (Pad.Pressed[7] ? 1 : 0);
		R.Aim = Pad.Buttons[6] ? Pad.Buttons[6] : (Pad.Pressed[6] ? 1 : 0);
		return R;
	}

	TArray<FName> PressedEdges(const FHKPadRead* Previous, const FHKPadRead& Current)
	{
		TArray<FName> Out;
		for (const FName& N : Names())
		{
			const bool* Now = Current.Buttons.Find(N);
			const bool* Before = Previous ? Previous->Buttons.Find(N) : nullptr;
			if (Now && *Now && !(Before && *Before)) Out.Add(N);
		}
		return Out;
	}

	bool ParseDS4Report(const uint8* Data, int32 Size, FHKPadState& Out)
	{
		if (!Data || Size < 10) return false;
		int32 Base;
		if (Data[0] == 0x01) Base = 1;                  // USB, or Bluetooth before full reports are enabled
		else if (Data[0] == 0x11 && Size >= 13) Base = 3;  // Bluetooth full report
		else return false;
		const uint8* D = Data + Base;
		// Chromium maps HID axes 0..255 to -1..1 as 2 * v / 255 - 1.
		for (int32 i = 0; i < 4; i++) Out.Axes[i] = 2.f * D[i] / 255.f - 1.f;
		const uint8 B0 = D[4], B1 = D[5], B2 = D[6];
		const int32 Hat = B0 & 0x0F;
		auto Set = [&Out](int32 i, bool bOn) { Out.Buttons[i] = bOn ? 1.f : 0.f; Out.Pressed[i] = bOn; };
		Set(2, (B0 & 0x10) != 0); Set(0, (B0 & 0x20) != 0); Set(1, (B0 & 0x40) != 0); Set(3, (B0 & 0x80) != 0);
		Set(4, (B1 & 0x01) != 0); Set(5, (B1 & 0x02) != 0);
		Set(8, (B1 & 0x10) != 0); Set(9, (B1 & 0x20) != 0); Set(10, (B1 & 0x40) != 0); Set(11, (B1 & 0x80) != 0);
		Set(16, (B2 & 0x01) != 0); Set(17, (B2 & 0x02) != 0);
		Set(12, Hat == 0 || Hat == 1 || Hat == 7); Set(15, Hat == 1 || Hat == 2 || Hat == 3);
		Set(13, Hat == 3 || Hat == 4 || Hat == 5); Set(14, Hat == 5 || Hat == 6 || Hat == 7);
		// Analog triggers: value 0..1, "pressed" above Chromium's threshold of 30/255.
		const float L2 = D[7] / 255.f, R2 = D[8] / 255.f;
		Out.Buttons[6] = L2; Out.Pressed[6] = L2 > 30.f / 255.f;
		Out.Buttons[7] = R2; Out.Pressed[7] = R2 > 30.f / 255.f;
		Out.bConnected = true;
		return true;
	}
}

// ---------- DualShock 4 over Windows HID (background thread) ----------
class FHKDualShockReader : public FRunnable
{
public:
	FHKDualShockReader() { Thread = FRunnableThread::Create(this, TEXT("HKDualShock4"), 0, TPri_AboveNormal); }
	virtual ~FHKDualShockReader() override
	{
		bStop = true;
		if (Thread) { Thread->WaitForCompletion(); delete Thread; }
	}
	FHKPadState Latest() { FScopeLock L(&Lock); return State; }
	FString Description() { FScopeLock L(&Lock); return Desc; }

	virtual uint32 Run() override
	{
#if PLATFORM_WINDOWS
		TArray<uint8> Buf;
		double NextScan = 0;
		while (!bStop)
		{
			if (Handle == INVALID_HANDLE_VALUE)
			{
				if (FPlatformTime::Seconds() < NextScan) { FPlatformProcess::Sleep(.2f); continue; }
				NextScan = FPlatformTime::Seconds() + 2.0;
				if (!Open()) continue;
				Buf.SetNumZeroed(FMath::Max(64, InputLen));
			}
			OVERLAPPED Ov = {};
			Ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			DWORD Got = 0;
			bool bOk = ReadFile(Handle, Buf.GetData(), (DWORD)Buf.Num(), &Got, &Ov) != 0;
			if (!bOk && GetLastError() == ERROR_IO_PENDING)
			{
				if (WaitForSingleObject(Ov.hEvent, 250) == WAIT_OBJECT_0) bOk = GetOverlappedResult(Handle, &Ov, &Got, FALSE) != 0;
				else { CancelIo(Handle); bOk = true; Got = 0; }
			}
			CloseHandle(Ov.hEvent);
			if (!bOk) { Close(); continue; }
			if (Got > 0)
			{
				FHKPadState S;
				S.Id = Desc;
				if (HKPad::ParseDS4Report(Buf.GetData(), (int32)Got, S)) { FScopeLock L(&Lock); State = S; }
			}
		}
		Close();
#endif
		return 0;
	}

private:
	FRunnableThread* Thread = nullptr;
	FThreadSafeBool bStop = false;
	FCriticalSection Lock;
	FHKPadState State;
	FString Desc;
	int32 InputLen = 64;
#if PLATFORM_WINDOWS
	HANDLE Handle = INVALID_HANDLE_VALUE;
	void Close()
	{
		if (Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle);
		Handle = INVALID_HANDLE_VALUE;
		FScopeLock L(&Lock);
		State = FHKPadState();
	}
	bool Open()
	{
		GUID HidGuid;
		HidD_GetHidGuid(&HidGuid);
		HDEVINFO Info = SetupDiGetClassDevsW(&HidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
		if (Info == INVALID_HANDLE_VALUE) return false;
		SP_DEVICE_INTERFACE_DATA Ifd;
		Ifd.cbSize = sizeof(Ifd);
		bool bFound = false;
		for (DWORD i = 0; !bFound && SetupDiEnumDeviceInterfaces(Info, nullptr, &HidGuid, i, &Ifd); i++)
		{
			DWORD Size = 0;
			SetupDiGetDeviceInterfaceDetailW(Info, &Ifd, nullptr, 0, &Size, nullptr);
			if (!Size) continue;
			TArray<uint8> Mem;
			Mem.SetNumZeroed(Size);
			PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)Mem.GetData();
			Detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
			if (!SetupDiGetDeviceInterfaceDetailW(Info, &Ifd, Detail, Size, nullptr, nullptr)) continue;
			HANDLE H = CreateFileW(Detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
			if (H == INVALID_HANDLE_VALUE) continue;
			HIDD_ATTRIBUTES A;
			A.Size = sizeof(A);
			const bool bSony = HidD_GetAttributes(H, &A) && A.VendorID == 0x054C && (A.ProductID == 0x05C4 || A.ProductID == 0x09CC || A.ProductID == 0x0BA0);
			HIDP_CAPS Caps = {};
			PHIDP_PREPARSED_DATA Pp = nullptr;
			if (bSony && HidD_GetPreparsedData(H, &Pp)) { HidP_GetCaps(Pp, &Caps); HidD_FreePreparsedData(Pp); }
			if (bSony && Caps.UsagePage == 0x01 && Caps.Usage == 0x05)
			{
				Handle = H;
				InputLen = Caps.InputReportByteLength;
				const bool bBluetooth = InputLen > 64;
				if (bBluetooth)
				{
					// Bluetooth sends short reports until a calibration feature report is read; then full 0x11 reports.
					uint8 Feature[64] = {0x05};
					if (!HidD_GetFeature(H, Feature, 41)) { FMemory::Memzero(Feature); Feature[0] = 0x02; HidD_GetFeature(H, Feature, 37); }
				}
				FScopeLock L(&Lock);
				Desc = FString::Printf(TEXT("Wireless Controller (STANDARD GAMEPAD Vendor: 054c Product: %04x) %s"), A.ProductID, bBluetooth ? TEXT("Bluetooth") : TEXT("USB"));
				bFound = true;
				UE_LOG(LogHaarklous, Log, TEXT("DualShock 4 found: %s, report length %d"), *Desc, InputLen);
			}
			else CloseHandle(H);
		}
		SetupDiDestroyDeviceInfoList(Info);
		return bFound;
	}
#endif
};

// ---------- Input processor ----------
static TSharedPtr<FHKInputProcessor> GProcessor;

FHKInputProcessor::FHKInputProcessor()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("hknods4"))) DS4 = new FHKDualShockReader();
}

FHKInputProcessor::~FHKInputProcessor()
{
	delete DS4;
}

TSharedPtr<FHKInputProcessor> FHKInputProcessor::Get() { return GProcessor; }

void FHKInputProcessor::Register()
{
	if (GProcessor || !FSlateApplication::IsInitialized()) return;
	GProcessor = MakeShared<FHKInputProcessor>();
	FSlateApplication::Get().RegisterInputPreProcessor(GProcessor, 0);
}

void FHKInputProcessor::Unregister()
{
	if (GProcessor && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(GProcessor);
	GProcessor.Reset();
}

FString FHKInputProcessor::CodeOf(const FKey& Key)
{
	const FString N = Key.GetFName().ToString();
	if (N.Len() == 1 && FChar::IsAlpha(N[0])) return TEXT("Key") + N.ToUpper();
	static const TMap<FString, FString> Map = {
		{TEXT("SpaceBar"), TEXT("Space")}, {TEXT("LeftShift"), TEXT("ShiftLeft")}, {TEXT("RightShift"), TEXT("ShiftRight")},
		{TEXT("Escape"), TEXT("Escape")}, {TEXT("Enter"), TEXT("Enter")}, {TEXT("Tab"), TEXT("Tab")}, {TEXT("BackSpace"), TEXT("Backspace")},
		{TEXT("Up"), TEXT("ArrowUp")}, {TEXT("Down"), TEXT("ArrowDown")}, {TEXT("Left"), TEXT("ArrowLeft")}, {TEXT("Right"), TEXT("ArrowRight")},
		{TEXT("LeftControl"), TEXT("ControlLeft")}, {TEXT("LeftAlt"), TEXT("AltLeft")},
		{TEXT("Zero"), TEXT("Digit0")}, {TEXT("One"), TEXT("Digit1")}, {TEXT("Two"), TEXT("Digit2")}, {TEXT("Three"), TEXT("Digit3")}, {TEXT("Four"), TEXT("Digit4")},
		{TEXT("Five"), TEXT("Digit5")}, {TEXT("Six"), TEXT("Digit6")}, {TEXT("Seven"), TEXT("Digit7")}, {TEXT("Eight"), TEXT("Digit8")}, {TEXT("Nine"), TEXT("Digit9")}};
	const FString* Found = Map.Find(N);
	return Found ? *Found : N;
}

// XInput pad buttons (Slate key names) -> standard mapping index.
static int32 XButtonIndex(const FKey& K)
{
	if (K == EKeys::Gamepad_FaceButton_Bottom) return 0;
	if (K == EKeys::Gamepad_FaceButton_Right) return 1;
	if (K == EKeys::Gamepad_FaceButton_Left) return 2;
	if (K == EKeys::Gamepad_FaceButton_Top) return 3;
	if (K == EKeys::Gamepad_LeftShoulder) return 4;
	if (K == EKeys::Gamepad_RightShoulder) return 5;
	if (K == EKeys::Gamepad_Special_Left) return 8;
	if (K == EKeys::Gamepad_Special_Right) return 9;
	if (K == EKeys::Gamepad_LeftThumbstick) return 10;
	if (K == EKeys::Gamepad_RightThumbstick) return 11;
	if (K == EKeys::Gamepad_DPad_Up) return 12;
	if (K == EKeys::Gamepad_DPad_Down) return 13;
	if (K == EKeys::Gamepad_DPad_Left) return 14;
	if (K == EKeys::Gamepad_DPad_Right) return 15;
	return -1;
}

bool FHKInputProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& E)
{
	const FKey K = E.GetKey();
	if (K.IsGamepadKey())
	{
		const int32 i = XButtonIndex(K);
		if (i >= 0) { XPad.Buttons[i] = 1; XPad.Pressed[i] = true; XPadLastInput = FPlatformTime::Seconds(); }
		return false;
	}
	const FString Code = CodeOf(K);
	if (bTextEntry)
	{
		if (Code == TEXT("Enter") || Code == TEXT("Escape") || Code == TEXT("Backspace")) TextKeys.Add(FName(*Code));
		return Code != TEXT("F11");
	}
	if (E.IsRepeat()) return false;
	Keys.Add(Code);
	KeyEdges.Add(Code);
	return false;
}

bool FHKInputProcessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& E)
{
	const FKey K = E.GetKey();
	if (K.IsGamepadKey())
	{
		const int32 i = XButtonIndex(K);
		if (i >= 0) { XPad.Buttons[i] = 0; XPad.Pressed[i] = false; }
		return false;
	}
	Keys.Remove(CodeOf(K));
	return false;
}

bool FHKInputProcessor::HandleKeyCharEvent(FSlateApplication& SlateApp, const FCharacterEvent& E)
{
	if (!bTextEntry) return false;
	const TCHAR C = E.GetCharacter();
	if (C >= 32 && C != 127) TypedText.AppendChar(C);
	return true;
}

bool FHKInputProcessor::HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& E)
{
	const FKey K = E.GetKey();
	const float V = E.GetAnalogValue();
	// Unreal's sticks are y-up; the standard gamepad is y-down.
	if (K == EKeys::Gamepad_LeftX) XPad.Axes[0] = V;
	else if (K == EKeys::Gamepad_LeftY) XPad.Axes[1] = -V;
	else if (K == EKeys::Gamepad_RightX) XPad.Axes[2] = V;
	else if (K == EKeys::Gamepad_RightY) XPad.Axes[3] = -V;
	else if (K == EKeys::Gamepad_LeftTriggerAxis) { XPad.Buttons[6] = V; XPad.Pressed[6] = V > 30.f / 255.f; }
	else if (K == EKeys::Gamepad_RightTriggerAxis) { XPad.Buttons[7] = V; XPad.Pressed[7] = V > 30.f / 255.f; }
	else return false;
	if (FMath::Abs(V) > .2f) XPadLastInput = FPlatformTime::Seconds();
	return false;
}

bool FHKInputProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& E)
{
	if (bMouseCaptured) { Mouse.Dx += E.GetCursorDelta().X; Mouse.Dy += E.GetCursorDelta().Y; }
	return false;
}

bool FHKInputProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& E)
{
	if (E.GetEffectingButton() == EKeys::LeftMouseButton) { Mouse.bLeft = true; Mouse.bLeftEdge = true; }
	if (E.GetEffectingButton() == EKeys::RightMouseButton) Mouse.bRight = true;
	return false;
}

bool FHKInputProcessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& E)
{
	if (E.GetEffectingButton() == EKeys::LeftMouseButton) { if (Mouse.bLeft) Mouse.bLeftUp = true; Mouse.bLeft = false; }
	if (E.GetEffectingButton() == EKeys::RightMouseButton) Mouse.bRight = false;
	return false;
}

bool FHKInputProcessor::HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& E, const FPointerEvent* Gesture)
{
	Mouse.Wheel += E.GetWheelDelta();
	return false;
}

void FHKInputProcessor::SimulateKey(const FString& Code, bool bDown)
{
	if (bDown) { if (!Keys.Contains(Code)) KeyEdges.Add(Code); Keys.Add(Code); }
	else Keys.Remove(Code);
}

void FHKInputProcessor::SimulateMouse(double Dx, double Dy, int32 Button, bool bDown)
{
	Mouse.Dx += Dx; Mouse.Dy += Dy;
	if (Button == 0) { if (bDown) { Mouse.bLeft = true; Mouse.bLeftEdge = true; } else { if (Mouse.bLeft) Mouse.bLeftUp = true; Mouse.bLeft = false; } }
	if (Button == 2) Mouse.bRight = bDown;
}

FHKPadState FHKInputProcessor::CurrentPad()
{
	// pickGamepad(): the simulated pad in tests, else a DualShock 4 on HID, else an XInput pad that has been used.
	if (bUseSimulatedPad) { FHKPadState S = SimulatedPad; S.bConnected = true; if (S.Id.IsEmpty()) S.Id = TEXT("Simulated Wireless Controller (STANDARD GAMEPAD Vendor: 054c Product: 05c4)"); return S; }
	if (DS4) { FHKPadState S = DS4->Latest(); if (S.bConnected) return S; }
	if (XPadLastInput > 0) { FHKPadState S = XPad; S.bConnected = true; S.Id = TEXT("XInput controller"); return S; }
	return FHKPadState();
}

FString FHKInputProcessor::PadDescription() const
{
	if (bUseSimulatedPad) return TEXT("simulated DualShock 4");
	if (DS4) { const FString D = DS4->Description(); if (!D.IsEmpty()) return D; }
	return XPadLastInput > 0 ? TEXT("XInput controller") : TEXT("none");
}

FHKInputFrame FHKInputProcessor::ReadFrame(bool bInVehicle)
{
	FHKInputFrame In;
	const FHKPadState Pad = CurrentPad();
	if (Pad.bConnected)
	{
		const FHKPadRead St = HKPad::Read(Pad);
		const TArray<FName> Edges = HKPad::PressedEdges(bHasPadPrev ? &PadPrev : nullptr, St);
		In.Move = St.Move; In.Look = St.Look;
		for (const TPair<FName, bool>& B : St.Buttons) if (B.Value) In.Held.Add(B.Key);
		for (const FName& N : Edges) In.Pressed.Add(N);
		if (bHasPadPrev) for (const TPair<FName, bool>& B : St.Buttons) if (!B.Value && PadPrev.Buttons.FindRef(B.Key)) In.Released.Add(B.Key);
		In.R2 = St.Run; In.L2 = St.Aim; In.bGamepad = true;
		PadPrev = St; bHasPadPrev = true;
	}
	else bHasPadPrev = false;
	In.Mouse = Mouse;
	auto K = [this](const TCHAR* C) { return Keys.Contains(C); };
	auto KE = [this](const TCHAR* C) { return KeyEdges.Contains(C); };
	const double Kx = (K(TEXT("KeyD")) ? 1 : 0) - (K(TEXT("KeyA")) ? 1 : 0), Ky = (K(TEXT("KeyS")) ? 1 : 0) - (K(TEXT("KeyW")) ? 1 : 0);
	if (Kx || Ky) In.Move = FVector2D(Kx, Ky);
	static const TPair<const TCHAR*, const TCHAR*> Map[] = {
		{TEXT("Space"), TEXT("cross")}, {TEXT("KeyC"), TEXT("circle")}, {TEXT("KeyE"), TEXT("square")}, {TEXT("KeyF"), TEXT("triangle")},
		{TEXT("Escape"), TEXT("options")}, {TEXT("KeyM"), TEXT("touchpad")}, {TEXT("KeyG"), TEXT("gallery")}, {TEXT("KeyZ"), TEXT("prone")},
		{TEXT("KeyR"), TEXT("reload")}, {TEXT("ArrowLeft"), TEXT("left")}, {TEXT("ArrowRight"), TEXT("right")}, {TEXT("ArrowUp"), TEXT("up")},
		{TEXT("ArrowDown"), TEXT("down")}, {TEXT("ShiftLeft"), TEXT("l3")}, {TEXT("KeyQ"), TEXT("l1")}, {TEXT("KeyV"), TEXT("view")}, {TEXT("KeyY"), TEXT("share")}};
	for (const auto& M : Map)
	{
		if (KE(M.Key)) In.Pressed.Add(FName(M.Value));
		if (K(M.Key)) In.Held.Add(FName(M.Value));
	}
	if (Mouse.bLeft) In.R2 = FMath::Max(In.R2, 1.0);
	if (Mouse.bRight) In.L2 = FMath::Max(In.L2, 1.0);
	if (Mouse.bLeftUp) In.Released.Add(FName("mouseLeft"));
	if (Mouse.bLeftEdge) In.Pressed.Add(FName("mouseLeft"));
	if (K(TEXT("KeyW")) && bInVehicle) In.R2 = 1;
	if (K(TEXT("KeyS")) && bInVehicle) In.L2 = 1;
	// Raw key edges for chat (Enter / T) and other keyboard-only shortcuts.
	for (const FString& C : KeyEdges) In.Pressed.Add(FName(*(TEXT("key:") + C)));
	Mouse.Dx = Mouse.Dy = 0; Mouse.bLeftEdge = Mouse.bLeftUp = false; Mouse.Wheel = 0;
	KeyEdges.Reset();
	return In;
}
