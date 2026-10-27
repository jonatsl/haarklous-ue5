// Controller input: the web's tests/gamepad-controls.test.cjs ported, plus the DualShock 4 HID report parser
// (USB and Bluetooth layouts) that replaces the browser's Gamepad API.
#include "Misc/AutomationTest.h"
#include "HKInput.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKPadReadTest, "Haarklous.Input.StandardMapping", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKPadReadTest::RunTest(const FString& Parameters)
{
	// "standard DualShock 4 mapping reads sticks, R2 and face buttons"
	FHKPadState Pad;
	Pad.bConnected = true;
	Pad.Axes[0] = 0; Pad.Axes[1] = -1; Pad.Axes[2] = .5f; Pad.Axes[3] = 0;
	Pad.Buttons[0] = 1; Pad.Pressed[0] = true; Pad.Buttons[9] = 1; Pad.Pressed[9] = true;
	Pad.Buttons[7] = .4f; Pad.Buttons[6] = .7f;
	const FHKPadRead S = HKPad::Read(Pad);
	TestEqual(TEXT("move.x"), S.Move.X, 0.0);
	TestEqual(TEXT("move.y"), S.Move.Y, -1.0);
	TestTrue(TEXT("0 < look.x < .5"), S.Look.X > 0 && S.Look.X < .5);
	TestEqual(TEXT("run = R2 value"), S.Run, .4, 1e-6);
	TestEqual(TEXT("aim = L2 value"), S.Aim, .7, 1e-6);
	TestTrue(TEXT("cross"), S.Buttons.FindRef("cross"));
	TestTrue(TEXT("options"), S.Buttons.FindRef("options"));
	TestFalse(TEXT("square"), S.Buttons.FindRef("square"));
	// "dead zone removes stick drift and rescales to full deflection"
	TestTrue(TEXT("dead zone"), HKPad::DeadZone(.1, -.08).IsZero());
	TestEqual(TEXT("full deflection"), HKPad::DeadZone(1, 0).X, 1.0);
	TestTrue(TEXT("diagonal capped"), HKPad::DeadZone(1, 1).Size() <= 1 + 1e-12);
	// "pressedEdges reports only new presses"
	FHKPadRead A, B;
	A.Buttons = {{FName("cross"), true}, {FName("circle"), false}};
	B.Buttons = {{FName("cross"), true}, {FName("circle"), true}};
	TestEqual(TEXT("edges from nothing"), HKPad::PressedEdges(nullptr, A).Num(), 1);
	TArray<FName> E = HKPad::PressedEdges(&A, B);
	TestTrue(TEXT("edges a -> b = circle"), E.Num() == 1 && E[0] == FName("circle"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKDS4ReportTest, "Haarklous.Input.DualShock4Reports", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKDS4ReportTest::RunTest(const FString& Parameters)
{
	// USB report 0x01: sticks centred except left stick full up, D-pad up, cross + R1 + options + touchpad click, L2 half, R2 full.
	uint8 Usb[64] = {0};
	Usb[0] = 0x01; Usb[1] = 128; Usb[2] = 0; Usb[3] = 255; Usb[4] = 128;
	Usb[5] = 0x20 | 0x00;            // cross, hat 0 (up)
	Usb[6] = 0x02 | 0x20;            // R1, options
	Usb[7] = 0x02;                   // touchpad click
	Usb[8] = 128; Usb[9] = 255;
	FHKPadState S;
	TestTrue(TEXT("USB report parsed"), HKPad::ParseDS4Report(Usb, 64, S));
	TestEqual(TEXT("left y full up = -1"), S.Axes[1], -1.f);
	TestEqual(TEXT("right x full right = 1"), S.Axes[2], 1.f);
	TestTrue(TEXT("left x centred (within the dead zone)"), FMath::Abs(S.Axes[0]) < .01f);
	TestTrue(TEXT("cross"), S.Pressed[0]);
	TestFalse(TEXT("circle"), S.Pressed[1]);
	TestTrue(TEXT("R1"), S.Pressed[5]);
	TestTrue(TEXT("options"), S.Pressed[9]);
	TestTrue(TEXT("touchpad"), S.Pressed[17]);
	TestTrue(TEXT("D-pad up"), S.Pressed[12]);
	TestFalse(TEXT("D-pad right"), S.Pressed[15]);
	TestEqual(TEXT("L2 value"), S.Buttons[6], 128.f / 255.f, 1e-6f);
	TestTrue(TEXT("L2 pressed above 30/255"), S.Pressed[6]);
	TestEqual(TEXT("R2 value"), S.Buttons[7], 1.f);
	// Hat 3 = down-right, 8 = released.
	Usb[5] = 0x03; HKPad::ParseDS4Report(Usb, 64, S);
	TestTrue(TEXT("hat 3 = down + right"), S.Pressed[13] && S.Pressed[15] && !S.Pressed[12] && !S.Pressed[14]);
	Usb[5] = 0x08; HKPad::ParseDS4Report(Usb, 64, S);
	TestTrue(TEXT("hat 8 = none"), !S.Pressed[12] && !S.Pressed[13] && !S.Pressed[14] && !S.Pressed[15]);
	// Bluetooth full report 0x11: the same data starts two bytes later.
	uint8 Bt[78] = {0};
	Bt[0] = 0x11; Bt[1] = 0xC0; Bt[2] = 0x00;
	FMemory::Memcpy(Bt + 3, Usb + 1, 9);
	Bt[3 + 4] = 0x40 | 0x06;           // circle, hat 6 (left)
	FHKPadState B;
	TestTrue(TEXT("Bluetooth report parsed"), HKPad::ParseDS4Report(Bt, 78, B));
	TestTrue(TEXT("BT circle"), B.Pressed[1]);
	TestTrue(TEXT("BT D-pad left"), B.Pressed[14]);
	TestEqual(TEXT("BT left y"), B.Axes[1], -1.f);
	TestFalse(TEXT("other report ids are ignored"), HKPad::ParseDS4Report((const uint8*)"\x05\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 11, B));
	// Read() on the parsed pad behaves like the browser's standard gamepad.
	const FHKPadRead R = HKPad::Read(S);
	TestEqual(TEXT("move.y after dead zone (a centred stick reads 128/127.5 - 1 = .004, as in Chrome)"), R.Move.Y, -1.0, 1e-4);
	return true;
}

#endif
