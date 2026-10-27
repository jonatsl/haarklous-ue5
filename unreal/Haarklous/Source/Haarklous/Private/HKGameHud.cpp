// HUD and menus of game-main.js / index.html, drawn with FHKUi on the Unreal canvas.
// Texts are the web version's; layout follows style.css (prompt at 16 % from the bottom, notice at 14 % from the top ...).
#include "HKGame.h"
#include "HKAudio.h"
#include "HKUi.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	struct FHit { FString Id; float X, Y, W, H; };
	TArray<FHit> GHits;                // clickable rectangles of the menu drawn last frame
	FString GDrag;                     // slider being dragged
	void Clickable(const FString& Id, float X, float Y, float W, float H) { GHits.Add({Id, X, Y, W, H}); }

	const TCHAR* StartText = TEXT("Walk the neighbourhood, go home to your 4th-floor apartment (street door of 17A, up the stairs), go into the stairwells, ride the Honda CBF600, drive the parked cars or the excavator, play disc golf in Torshovdalen and petanque in the park, take photos, and start the hostage-rescue mission at the red marker beside the entrance of number 17A. Play together: up to five players per lobby – your lobby code is in the pause menu.");
	const TCHAR* StartControls[][2] = {
		{TEXT("Move / look"), TEXT("Left stick / WASD · Right stick / mouse")},
		{TEXT("✕ / Space"), TEXT("Jump")},
		{TEXT("○ / C · hold ○ / Z"), TEXT("Crouch · lie prone")},
		{TEXT("□ / E"), TEXT("Use: doors, drinks, vehicles, games, mission")},
		{TEXT("△ / F"), TEXT("Tap: photo camera (R2 / left mouse takes a photo, □ / G gallery) · hold: choose camera or FPV drone")},
		{TEXT("D-pad ↑ / ↑"), TEXT("Check the time on your wristwatch")},
		{TEXT("Touchpad / M"), TEXT("Map")},
		{TEXT("Share / Enter or T"), TEXT("Chat with the other players (quick chat on the controller)")},
		{TEXT("R2 / L3 / Shift"), TEXT("Run")},
		{TEXT("Options / Esc"), TEXT("Pause and settings")}};
	const TCHAR* PauseControls[][2] = {
		{TEXT("Motorcycle / car"), TEXT("□ get on/in · R2 throttle · L2 brake/reverse · left stick steer · right stick look")},
		{TEXT("Excavator"), TEXT("L1/L2 left track forward/back · R1/R2 right track · left stick: swing and stick · right stick: boom and bucket")},
		{TEXT("Disc golf / petanque"), TEXT("Aim with the camera · hold R2, release to throw · ○ quit")},
		{TEXT("Helicopter"), TEXT("Mode 2: left stick ↑↓ collective (centre = hover), ←→ pedals · right stick pitch and roll · R3 / V chase camera · □ get out after landing")},
		{TEXT("Plane"), TEXT("Mode 2: left stick ↑↓ throttle (stays where you leave it), ←→ rudder and nose wheel · right stick pitch and roll (pull back to lift off at about 95 km/h) · L2 brakes · R3 / V view")},
		{TEXT("FPV drone"), TEXT("Hold △, pick the drone · left stick ↑↓ thrust, ←→ yaw · right stick tilt (angle) or rates (acro) · L1 / Q angle ↔ acro · ✕ reset after a crash · ○ pack away")},
		{TEXT("Multiplayer"), TEXT("Up to five players per lobby (code above) · □ next to a vehicle someone is driving: ride along as passenger (motorcycle, cars, helicopter, plane) · Share / Enter: chat")},
		{TEXT("Firepit"), TEXT("In the park in front of the building: □ sit on a bench, R2 have a beer, □ at the woodpile throws a log on the fire")},
		{TEXT("Drinks"), TEXT("Beer and aquavit in the apartment: each beer or sip = 30 s drunk")}};

	float DrawControls(UCanvas* C, const TCHAR* Rows[][2], int32 N, float X, float Y, float W, float Size)
	{
		const float Kw = 210;
		for (int32 i = 0; i < N; i++)
		{
			FHKUi::Text(C, Rows[i][0], X, Y, Size, FHKUi::Accent);
			const TArray<FString> L = FHKUi::Wrap(C, Rows[i][1], Size, W - Kw);
			for (const FString& Line : L) { FHKUi::Text(C, Line, X + Kw, Y, Size); Y += Size * 1.3f; }
			Y += 3;
		}
		return Y;
	}

	void Slider(UCanvas* C, const FString& Id, const FString& Label, const FString& Value, float Frac, float X, float& Y, float W)
	{
		FHKUi::Text(C, Label, X, Y, 17);
		FHKUi::Text(C, Value, X + W, Y, 17, FHKUi::TextMain, EHKAlign::Right);
		Y += 26;
		FHKUi::Meter(C, X, Y + 4, W, 8, Frac, FHKUi::Accent);
		FHKUi::Disc(C, FVector2D(X + W * Frac, Y + 8), 9, FLinearColor::White);
		Clickable(Id, X - 6, Y - 6, W + 12, 28);
		Y += 32;
	}

	void Button(UCanvas* C, const FString& Id, const FString& Label, float X, float Y, float& OutW)
	{
		const FVector2D S = FHKUi::Measure(C, Label, 16, true);
		OutW = S.X + 32;
		FHKUi::Panel(C, X, Y, OutW, 40, FHKUi::Accent, 6);
		FHKUi::Text(C, Label, X + 16, Y + 9, 16, FLinearColor(FColor(0x11, 0x16, 0x1c)), EHKAlign::Left, true, false);
		Clickable(Id, X, Y, OutW, 40);
	}
}

void AHKGame::DrawHud(UCanvas* C)
{
	if (!C) return;
	GHits.Reset();
	DrawWorldHud(C);
	if (bStarted) DrawModuleHud(C);
	if (HudExtra) HudExtra(C);
	if (bStarted) DrawExtras(C);
	if (Mission && Mission->IsActive()) Mission->DrawHud(C);
	if (!bStarted) DrawStart(C);
	else if (bPaused) DrawPause(C);
}

void AHKGame::DrawWorldHud(UCanvas* C)
{
	const FVector2D Sz = FHKUi::Size(C);
	if (!bStarted) return;
	// #crosshair: a 4 px white dot.
	if (!(HideCrosshair && HideCrosshair())) FHKUi::Disc(C, Sz / 2, 2.2f, FLinearColor::White, 12);
	// #prompt
	if (!PromptAction.Text.IsEmpty() && !bPaused)
	{
		const FVector2D T = FHKUi::Measure(C, PromptAction.Text, 18, true);
		// #prompt sits 16 % from the bottom (24 % during the mission, above its HUD).
		const float W = T.X + 32, X = Sz.X / 2 - W / 2, Y = Sz.Y * ((MissionActive && MissionActive()) ? .76f : .84f) - 40;
		FHKUi::Panel(C, X, Y, W, 40, FLinearColor(0, 0, 0, .6f), 20);
		FHKUi::Text(C, PromptAction.Text, Sz.X / 2, Y + 8, 18, FHKUi::TextMain, EHKAlign::Center, true);
	}
	// #notice
	if (!NoticeText.IsEmpty())
	{
		const TArray<FString> L = FHKUi::Wrap(C, NoticeText, 18, 690);
		float W = 0;
		for (const FString& S : L) W = FMath::Max(W, (float)FHKUi::Measure(C, S, 18).X);
		const float H = L.Num() * 23 + 16, X = Sz.X / 2 - (W + 32) / 2, Y = Sz.Y * .14f;
		FHKUi::Panel(C, X, Y, W + 32, H, FLinearColor(0, 0, 0, .66f), 8);
		for (int32 i = 0; i < L.Num(); i++) FHKUi::Text(C, L[i], Sz.X / 2, Y + 8 + i * 23, 18, FHKUi::TextMain, EHKAlign::Center);
	}
	// #drunk
	const double Dl = DrunkLevel();
	if (Dl > 0)
	{
		const FString S = FString::Printf(TEXT("Drunk · %d s"), (int32)FMath::CeilToDouble(Drunk));
		const float W = FHKUi::Measure(C, S, 16, true).X + 24;
		FHKUi::Panel(C, 22, 18, W, 30, FLinearColor(FColor(0x7a, 0x3b, 0x10, 0xcc)), 14);
		FHKUi::Text(C, S, 34, 23, 16, FHKUi::TextMain, EHKAlign::Left, true);
	}
	// #speedo
	if (!SpeedoText.IsEmpty())
	{
		const bool bAir = bSpeedoAir;
		TArray<FString> L = bAir ? FHKUi::Wrap(C, SpeedoText, 16, Sz.X * .7f) : TArray<FString>{SpeedoText};
		for (int32 i = 0; i < L.Num(); i++) FHKUi::Text(C, L[i], Sz.X - 22, Sz.Y - 18 - (L.Num() - i) * (bAir ? 21 : 26), bAir ? 16 : 22, FHKUi::TextMain, EHKAlign::Right, true);
	}
}

void AHKGame::DrawStart(UCanvas* C)
{
	const FVector2D Sz = FHKUi::Size(C);
	FHKUi::Rect(C, 0, 0, Sz.X, Sz.Y, FLinearColor(FColor(0x0b, 0x0f, 0x13, 0xb8)));
	const float W = 660, X = Sz.X / 2 - W / 2;
	const TArray<FString> P = FHKUi::Wrap(C, StartText, 16, W - 52);
	const float H = 150 + P.Num() * 21 + 10 * 22 + 150;
	float Y = Sz.Y / 2 - H / 2;
	FHKUi::Panel(C, X, Y, W, H, FLinearColor(FColor(0x11, 0x16, 0x1c, 0xf4)), 12);
	const float Ix = X + 26;
	Y += 24;
	FHKUi::Text(C, TEXT("HAARKLOUS PLASS 17A · TORSHOV · OSLO"), Ix, Y, 13, FHKUi::Accent, EHKAlign::Left, true); Y += 24;
	FHKUi::Text(C, TEXT("Haarklous online"), Ix, Y, 38, FHKUi::TextMain, EHKAlign::Left, true); Y += 58;
	for (const FString& L : P) { FHKUi::Text(C, L, Ix, Y, 16, FHKUi::TextDim); Y += 21; }
	Y += 12;
	Y = DrawControls(C, StartControls, UE_ARRAY_COUNT(StartControls), Ix, Y, W - 52, 15);
	float Bw;
	Button(C, TEXT("start"), TEXT("Click or press ✕ to start"), Ix, Y + 12, Bw);
	FHKUi::Text(C, TEXT("Map data © OpenStreetMap contributors (ODbL) · Height data © Kartverket (CC BY 4.0)"), Sz.X / 2, Sz.Y - 34, 13, FHKUi::TextDim, EHKAlign::Center);
}

void AHKGame::DrawPause(UCanvas* C)
{
	const FVector2D Sz = FHKUi::Size(C);
	FHKUi::Rect(C, 0, 0, Sz.X, Sz.Y, FLinearColor(FColor(0x0b, 0x0f, 0x13, 0xb8)));
	const float W = 700, X = Sz.X / 2 - W / 2, H = FMath::Min(Sz.Y - 40, 1040.f);
	float Y = Sz.Y / 2 - H / 2;
	FHKUi::Panel(C, X, Y, W, H, FLinearColor(FColor(0x11, 0x16, 0x1c, 0xf4)), 12);
	const float Ix = X + 26, Iw = W - 52;
	Y += 20;
	FHKUi::Text(C, TEXT("PAUSED"), Ix, Y, 13, FHKUi::Accent, EHKAlign::Left, true); Y += 20;
	FHKUi::Text(C, TEXT("Settings"), Ix, Y, 26, FHKUi::TextMain, EHKAlign::Left, true); Y += 42;
	Slider(C, TEXT("sensPad"), TEXT("Controller look sensitivity"), FString::Printf(TEXT("%.2f×"), Sensitivity.Pad), (Sensitivity.Pad - .25) / 2.75, Ix, Y, Iw);
	Slider(C, TEXT("sensMouse"), TEXT("Mouse look sensitivity"), FString::Printf(TEXT("%.2f×"), Sensitivity.Mouse), (Sensitivity.Mouse - .25) / 2.75, Ix, Y, Iw);
	const double Vol = FHKMusic::Get().GetVolume();
	Slider(C, TEXT("musicVol"), TEXT("Music volume"), Vol > 0 ? FString::Printf(TEXT("%d %%"), (int32)HK::JsRound(Vol * 100)) : FString(TEXT("off")), Vol, Ix, Y, Iw);
	FString Title; double Len = 0;
	FHKMusic::Get().Position(Title, Len);
	const FString Now = Title.IsEmpty() ? FString(TEXT("–")) : FString::Printf(TEXT("%s (%d:%02d loop)"), *Title, (int32)(Len / 60), (int32)HK::JsRound(FMath::Fmod(Len, 60.0)));
	FHKUi::Text(C, TEXT("Controller: D-pad ←/→ changes the music volume. Now playing: ") + Now, Ix, Y, 14, FHKUi::TextDim); Y += 30;
	// Multiplayer section (drawn by the online module when it is merged).
	if (DrawLobby) Y = DrawLobby(C, Ix, Y, Iw);
	// Graphics quality: Fast (no shadows) / Balanced / Sharp.
	static const TCHAR* Qn[] = {TEXT("Fast (no shadows)"), TEXT("Balanced"), TEXT("Sharp")};
	FHKUi::Text(C, TEXT("Graphics"), Ix, Y + 6, 17);
	FHKUi::Panel(C, Ix + 120, Y, 260, 34, FLinearColor(1, 1, 1, .1f), 6);
	FHKUi::Text(C, FString(Qn[Quality]), Ix + 134, Y + 6, 16);
	Clickable(TEXT("quality"), Ix + 120, Y, 260, 34);
	Y += 48;
	Y = DrawControls(C, PauseControls, UE_ARRAY_COUNT(PauseControls), Ix, Y, Iw, 13);
	Y += 4;
	FHKUi::Text(C, TEXT("Map data © OpenStreetMap contributors (ODbL) · Height data © Kartverket (CC BY 4.0)"), Ix, Y, 13, FHKUi::TextDim); Y += 24;
	float Bw;
	Button(C, TEXT("resume"), TEXT("Resume"), Ix, Y, Bw);
	// Mouse cursor.
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		float Mx, My;
		if (PC->GetMousePosition(Mx, My)) { const float S = FHKUi::Scale(C); MenuMouse = FVector2D(Mx / S, My / S); }
	}
}

void AHKGame::PauseInput(double Dt)
{
	if (LobbyInput && LobbyInput(Frame)) return;
	// "if (input.pressed.has('left') || input.pressed.has('right')) {music.setVolume(music.volume + (right ? .1 : -.1))}"
	if (Frame.WasPressed("left") || Frame.WasPressed("right")) { FHKMusic::Get().SetVolume(FHKMusic::Get().GetVolume() + (Frame.WasPressed("right") ? .1 : -.1)); SaveSettings(); }
	// Mouse: click buttons, drag sliders.
	const FVector2D M = MenuMouse;
	auto HitAt = [&](const FVector2D& P) -> const FHit*
	{
		for (const FHit& H : GHits) if (P.X >= H.X && P.X <= H.X + H.W && P.Y >= H.Y && P.Y <= H.Y + H.H) return &H;
		return nullptr;
	};
	if (Frame.Mouse.bLeftEdge) { const FHit* H = HitAt(M); GDrag = H ? H->Id : FString(); if (H && OnMenuClick) OnMenuClick(H->Id); }
	if (!Frame.Mouse.bLeft) GDrag.Empty();
	if (!GDrag.IsEmpty())
	{
		const FHit* H = GHits.FindByPredicate([](const FHit& X) { return X.Id == GDrag; });
		if (H)
		{
			const double F = FMath::Clamp((M.X - (H->X + 6)) / FMath::Max(1.f, H->W - 12), 0.0, 1.0);
			if (GDrag == TEXT("sensPad")) SetSens(TEXT("pad"), HK::JsRound((.25 + F * 2.75) / .05) * .05);
			else if (GDrag == TEXT("sensMouse")) SetSens(TEXT("mouse"), HK::JsRound((.25 + F * 2.75) / .05) * .05);
			else if (GDrag == TEXT("musicVol")) { FHKMusic::Get().SetVolume(HK::JsRound(F / .05) * .05); SaveSettings(); }
			else if (GDrag == TEXT("quality") && Frame.Mouse.bLeftEdge) { Quality = (Quality + 1) % 3; SaveSettings(); ApplyQuality(); }
			else if (GDrag == TEXT("resume") && Frame.Mouse.bLeftEdge) SetPaused(false);
		}
	}
}

void AHKGame::RenderSettingsLog() const
{
	UE_LOG(LogTemp, Log, TEXT("Settings: pad %.2f mouse %.2f quality %d music %.2f"), Sensitivity.Pad, Sensitivity.Mouse, Quality, FHKMusic::Get().GetVolume());
}
