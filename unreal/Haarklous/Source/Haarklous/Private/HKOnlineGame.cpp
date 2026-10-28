#include "HKOnlineGame.h"
#include "HKMultiplayer.h"
#include "HKPaintball.h"
#include "Haarklous.h"
#include "HKAircraft.h"
#include "HKAvatars.h"
#include "HKGame.h"
#include "HKInput.h"
#include "HKRigActor.h"
#include "HKUi.h"
#include "HKVehicle.h"
#include "HKVoice.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AHKOnline::AHKOnline()
{
	PrimaryActorTick.bCanEverTick = false;
}

AHKOnline* AHKOnline::Setup(AHKGame* InGame)
{
	if (!InGame || !InGame->GetWorld()) return nullptr;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AHKOnline* O = InGame->GetWorld()->SpawnActor<AHKOnline>(AHKOnline::StaticClass(), FTransform::Identity, SP);
	if (!O) return nullptr;
	O->Game = InGame;
	O->Wire();
	return O;
}

FHKPlayerState AHKOnline::LocalState() const
{
	// game-main.js localState(): what other players see of us.
	FHKPlayerState S;
	const AHKGame* G = Game.Get();
	if (!G) return S;
	const FHKPlayer& P = G->Player;
	auto R = [](double V) { return FHKPlayerState::R(V); };
	auto Q = [](double V) { return FHKPlayerState::Q(V); };
	S.P = FVector(R(P.X), R(P.Feet + P.JumpY), R(P.Z)); S.Y = R(P.Yaw); S.St = P.Stance; S.B = G->BeerT > 0 ? 1 : 0;
	if ((P.Mode == TEXT("vehicle") || P.Mode == TEXT("passenger")) && P.Vehicle) { S.V = G->IndexOf(P.Vehicle); S.S = P.Mode == TEXT("vehicle") ? TEXT("d") : TEXT("p"); }
	if (P.Mode == TEXT("sit") && P.SitSpot) { const FHKSitSpot& Sp = *P.SitSpot; S.Sit = Sp.Id + 1; S.P = FVector(R(Sp.X), R(Sp.Y), R(Sp.Z)); S.Y = R(Sp.Yaw); }
	if (P.Mode == TEXT("drone") && G->Drone)
	{
		const FHKDrone& D = *G->Drone;
		S.St = TEXT("drone"); S.bDr = true;
		const double Dr[7] = {R(D.Pos.X), R(D.Pos.Y), R(D.Pos.Z), Q(D.Q.X), Q(D.Q.Y), Q(D.Q.Z), Q(D.Q.W)};
		for (int32 i = 0; i < 7; i++) S.Dr[i] = Dr[i];
	}
	if (G->Paintball) S.Pb = G->Paintball->LocalState();
	return S;
}

void AHKOnline::Wire()
{
	AHKGame* G = Game.Get();
	FHKMultiplayerContext Ctx;
	TWeakObjectPtr<AHKOnline> Self(this);
	Ctx.NumVehicles = [this]() { return Game.IsValid() ? Game->VehicleList.Num() : 0; };
	Ctx.Vehicle = [this](int32 I) -> FHKVehicle* { return Game.IsValid() && Game->VehicleList.IsValidIndex(I) ? Game->VehicleList[I] : nullptr; };
	Ctx.LocalState = [this]() { return LocalState(); };
	Ctx.DrivingIndex = [this]() { const AHKGame* X = Game.Get(); return X && X->Player.Mode == TEXT("vehicle") && X->Player.Vehicle ? X->IndexOf(X->Player.Vehicle) : -1; };
	Ctx.Listener = [this]() { const AHKGame* X = Game.Get(); return X ? FVector(X->Player.X, X->Player.Feet + 1.5, X->Player.Z) : FVector::ZeroVector; };
	Ctx.CameraPosition = [this]() { return Game.IsValid() ? Game->CamPos : FVector::ZeroVector; };
	Ctx.ApplyDoor = [this](const FString& Id, bool bOpen, bool bInstant) { if (Game.IsValid()) Game->ApplyDoor(Id, bOpen, bInstant); };
	Ctx.OnChat = [this](const FHKChatLine& L, bool bMine, bool bOld) { if (Game.IsValid()) Game->ChatLine(L.Name, L.Text, L.Slot, L.bSys, bMine, bOld); };
	Ctx.Notice = [this](const FString& T, double S) { if (Game.IsValid()) Game->Notice(T, S); };
	Ctx.OnRemoteWood = [this](const FHKRemote& R) { if (Game.IsValid() && Game->ThrowLog) Game->ThrowLog(FVector(R.Pos.X, R.Pos.Y + 1.3, R.Pos.Z)); };
	Ctx.AddToScene = [this](FHKRig& Rig, const FString& RigName)
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AHKRigActor* A = GetWorld() ? GetWorld()->SpawnActor<AHKRigActor>(AHKRigActor::StaticClass(), FTransform::Identity, SP) : nullptr;
		if (!A) return;
		A->Init(&Rig, RigName);
		RigActors.Add(A);
		RigKeys.Add(&Rig);
	};
	Ctx.RemoveFromScene = [this](FHKRig& Rig)
	{
		const int32 I = RigKeys.IndexOfByKey(&Rig);
		if (I == INDEX_NONE) return;
		if (RigActors[I] && !RigActors[I]->IsActorBeingDestroyed()) RigActors[I]->Destroy();
		RigActors.RemoveAt(I); RigKeys.RemoveAt(I);
	};
	Ctx.PlayerVehicleIndex = [this]() { const AHKGame* X = Game.Get(); return X && X->Player.Vehicle ? X->IndexOf(X->Player.Vehicle) : -1; };
	Ctx.PlayerDriving = [this]() { return Game.IsValid() && Game->Player.Mode == TEXT("vehicle"); };
	Ctx.HoldsMarker = [this](int32 Id) { return Game.IsValid() && Game->Paintball && Game->Paintball->MemberPlaying(Id); };
	Ctx.FireDecayS = G->FireDecayS;
	Ctx.FireMax = G->FireMax;
	TUniquePtr<IHKVoice> Voice;
	if (FHKWebVoice::Available())
	{
		FHKWebVoiceOptions VO;
		VO.bTestTone = FParse::Param(FCommandLine::Get(), TEXT("HKVoiceTone"));   // tests: a 440 Hz tone instead of the microphone
		Voice = MakeUnique<FHKWebVoice>(VO);
	}
	MpPtr = MakeUnique<FHKMultiplayer>(Ctx, FHKNet::DefaultOptions(), MoveTemp(Voice));
	FHKMultiplayer* M = MpPtr.Get();

	// ---- The game's hooks ----
	G->Occupant = [M](int32 I, const FString& Seat) { return M->Occupant(I, Seat); };
	G->NameOf = [M](int32 Id) { return M->NameOf(Id); };
	G->Claim = [M](int32 I, const FString& Seat, TFunction<void(bool, int32, bool)> Done) { M->Claim(I, Seat, [Done](const FHKClaimResult& R) { if (Done) Done(R.bOk, R.By, R.bTimeout); }); };
	G->Release = [M](int32 I, const FString& Seat, const TArray<double>* Final) { M->Release(I, Seat, Final); };
	G->RemoteDriven = [M](int32 I) { return M->RemoteDriven(I); };
	G->SitTaken = [M](int32 Spot) { return M->SitTaken(Spot); };
	G->SharedFireLevel = [M]() { return M->FireLevel(); };
	G->AddWood = [M]() { M->AddWood(); };
	G->OnDoorChanged = [M](const FString& Id, bool bOpen) { M->SendDoor(Id, bOpen); };
	G->SendChat = [M](const FString& T) { return M->SendChat(T); };
	// Paintball: the others' states (with their smoothed avatar positions), the lobby clock, our id and name, avatars.
	G->PbOthers = [M](TArray<FHKPbMember>& Out)
	{
		for (const TUniquePtr<FHKRemote>& R : M->Remotes())
		{
			if (!R->bHasSt || !R->St.Pb.IsValid()) continue;
			FHKPbMember P;
			if (!FHKPbMember::FromJson(R->St.Pb, P)) continue;
			P.Id = R->Id; P.Name = R->Name; P.Slot = R->Slot;
			P.Feet = R->bSeen ? R->Pos : R->St.P; P.Yaw = R->bSeen ? R->Yaw : R->St.Y; P.Stance = R->St.St;
			Out.Add(P);
		}
	};
	G->PbServerNow = [M]() { return M->Net().Online() ? M->Net().ServerNow() : FPlatformTime::Seconds() * 1000; };
	G->PbYou = [M]() { return M->Net().Online() ? M->Net().You() : 0; };
	G->PbName = [M]() { return M->Net().GetName(); };
	G->PbAvatarOf = [this, M](int32 Id) -> AHKRigActor*
	{
		const FHKRemote* R = M->Remote(Id);
		if (!R || !R->Av) return nullptr;
		const int32 I = RigKeys.IndexOfByKey(&R->Av->Rig);
		return I == INDEX_NONE ? nullptr : RigActors[I].Get();
	};
	G->IsOnline = [M]() { return M->Net().Online(); };
	G->RemotePoi = [M]()
	{
		TArray<TTuple<FVector2D, FLinearColor, FString>> Out;
		for (const TUniquePtr<FHKRemote>& R : M->Remotes()) if (R->bSeen) Out.Add(MakeTuple(FVector2D(R->Pos.X, R->Pos.Z), HKAvatars::SlotCss(R->Slot), R->Name));
		return Out;
	};
	// begin(): "const q = ?lobby; if (!(q && mp.net.connect(q))) mp.net.home();"
	// (These three wrap the game's own functions, so they check that the online module is still there.)
	TFunction<void()> OldBegin = G->OnBegin;
	G->OnBegin = [OldBegin, Self]() { if (OldBegin) OldBegin(); if (Self.IsValid() && Self->MpPtr) Self->MpPtr->Net().Begin(); };
	// frame(): "vehicles.forEach(update); beerT ...; mp.update(dt); fire.update(...)"
	TFunction<void(double)> OldPost = G->PostModeUpdate;
	G->PostModeUpdate = [OldPost, Self](double Dt) { if (OldPost) OldPost(Dt); if (Self.IsValid() && Self->MpPtr) { Self->MpPtr->Update(Dt); Self->MpPtr->EndFrame(); } };
	// Name tags and chat bubbles (the web's sprites) under the rest of the HUD.
	TFunction<void(UCanvas*)> OldHud = G->HudExtra;
	G->HudExtra = [OldHud, Self](UCanvas* C) { if (Self.IsValid() && Self->MpPtr) Self->MpPtr->DrawHud(C); if (OldHud) OldHud(C); };
	G->DrawLobby = [Self](UCanvas* C, float X, float Y, float W) { return Self.IsValid() ? Self->DrawLobby(C, X, Y, W) : Y; };
	G->LobbyInput = [Self](const FHKInputFrame& F) { return Self.IsValid() && Self->LobbyInput(F); };
	UE_LOG(LogHaarklous, Log, TEXT("Online: %s (own lobby %s, server %s)"), M->Net().Enabled() ? TEXT("on") : TEXT("off"), *M->Net().GetOwnCode(), *M->Net().GetBase());
}

void AHKOnline::EndPlay(const EEndPlayReason::Type Reason)
{
	if (AHKGame* G = Game.Get())
	{
		G->Occupant = nullptr; G->NameOf = nullptr; G->Claim = nullptr; G->Release = nullptr; G->RemoteDriven = nullptr; G->SitTaken = nullptr;
		G->SharedFireLevel = nullptr; G->AddWood = nullptr; G->OnDoorChanged = nullptr; G->SendChat = nullptr; G->IsOnline = nullptr; G->RemotePoi = nullptr;
		G->DrawLobby = nullptr; G->LobbyInput = nullptr;
	}
	StopTextEntry();
	MpPtr.Reset();
	RigActors.Reset(); RigKeys.Reset();
	Super::EndPlay(Reason);
}

// ---------------------------------------------------------------------------------------------------------------
// Pause menu: renderMp(), the code pad and the lobby buttons of game-main.js
// ---------------------------------------------------------------------------------------------------------------
void AHKOnline::JoinCode(const FString& InCode)
{
	const FString C = InCode.ToUpper().TrimStartAndEnd();
	if (!MpPtr->Net().Connect(C)) { if (Game.IsValid()) Game->Notice(TEXT("A lobby code is 4 letters/digits (no O, 0, I or 1)."), 4); }
	else if (Game.IsValid()) Game->Notice(FString::Printf(TEXT("Joining lobby %s…"), *C), 3);
}

void AHKOnline::GoHome()
{
	MpPtr->Net().Home();
	if (Game.IsValid()) Game->Notice(TEXT("Back in your own lobby."), 3);
}

void AHKOnline::ToggleMic()
{
	const bool bWant = !MpPtr->Mic();
	TWeakObjectPtr<AHKOnline> Self(this);
	MpPtr->SetMic(bWant, [Self, bWant](bool bOk)
	{
		if (!bOk && bWant && Self.IsValid() && Self->Game.IsValid()) Self->Game->Notice(TEXT("The microphone could not be opened (permission denied or no microphone)."), 5);
	});
}

void AHKOnline::StopTextEntry()
{
	if (Edit != EEdit::None && Game.IsValid() && Game->InputProc) Game->InputProc->bTextEntry = false;
	Edit = EEdit::None;
}

bool AHKOnline::MenuClick(const FString& Id)
{
	if (Id == TEXT("mpJoinCode")) { Edit = EEdit::Code; Draft.Empty(); if (Game.IsValid() && Game->InputProc) { Game->InputProc->bTextEntry = true; Game->InputProc->TypedText.Empty(); Game->InputProc->TextKeys.Reset(); } return true; }
	if (Id == TEXT("mpName")) { Edit = EEdit::Name; Draft = MpPtr->Net().GetName(); if (Game.IsValid() && Game->InputProc) { Game->InputProc->bTextEntry = true; Game->InputProc->TypedText.Empty(); Game->InputProc->TextKeys.Reset(); } return true; }
	if (Id == TEXT("mpJoin")) { const FString C = Edit == EEdit::Code ? Draft : FString(); StopTextEntry(); JoinCode(C); return true; }
	if (Id == TEXT("mpHome")) { GoHome(); return true; }
	if (Id == TEXT("mpMic")) { ToggleMic(); return true; }
	return false;
}

bool AHKOnline::LobbyInput(const FHKInputFrame& Frame)
{
	// Typing a lobby code or a name (the web's text fields).
	if (Edit != EEdit::None && Game.IsValid() && Game->InputProc)
	{
		FHKInputProcessor& In = *Game->InputProc;
		Draft += In.TypedText;
		In.TypedText.Empty();
		Draft = Draft.Left(Edit == EEdit::Code ? 4 : 24);
		for (const FName& K : In.TextKeys)
		{
			if (K == TEXT("Backspace")) Draft.LeftChopInline(1);
			else if (K == TEXT("Escape")) StopTextEntry();
			else if (K == TEXT("Enter"))
			{
				const EEdit Was = Edit;
				StopTextEntry();
				if (Was == EEdit::Code) JoinCode(Draft); else MpPtr->Net().SetName(Draft);
			}
		}
		In.TextKeys.Reset();
		return true;
	}
	// The controller code pad (codePadInput).
	if (CodePad.IsSet())
	{
		FHKCodePad& C = CodePad.GetValue();
		if (Frame.WasPressed("left")) C.Left();
		if (Frame.WasPressed("right")) C.Right();
		const int32 Step = Frame.WasPressed("up") ? 1 : Frame.WasPressed("down") ? -1 : 0;
		if (Step) C.Step(Step);
		if (Frame.WasPressed("cross"))
		{
			const FString Code = C.Code();
			CodePad.Reset();
			if (MpPtr->Net().Connect(Code) && Game.IsValid()) Game->Notice(FString::Printf(TEXT("Joining lobby %s…"), *Code), 3);
		}
		if (Frame.WasPressed("circle")) CodePad.Reset();
		return true;
	}
	// "if (input.pressed.has('triangle')) openCodePad(); if (input.pressed.has('square') && mp.net.code !== mp.net.ownCode) {...}"
	if (Frame.WasPressed("triangle")) CodePad = FHKCodePad::Open(MpPtr->Net());
	if (Frame.WasPressed("square") && MpPtr->Net().GetCode() != MpPtr->Net().GetOwnCode()) GoHome();
	return false;
}

float AHKOnline::DrawLobby(UCanvas* C, float X, float Y, float W)
{
	const FHKNet& N = MpPtr->Net();
	auto Button = [&](const FString& Id, const FString& Label, float Bx, float By, float& OutW)
	{
		OutW = FHKUi::Measure(C, Label, 15).X + 28;
		FHKUi::Panel(C, Bx, By, OutW, 32, FLinearColor(1, 1, 1, .12f), 8);
		FHKUi::Text(C, Label, Bx + 14, By + 6, 15);
		if (Clickable) Clickable(Id, Bx, By, OutW, 32);
	};
	FHKUi::Text(C, TEXT("Multiplayer"), X, Y, 19, FHKUi::TextMain, EHKAlign::Left, true);
	const FString UpTo = FString::Printf(TEXT("(up to %d players)"), N.Max());
	FHKUi::Text(C, UpTo, X + FHKUi::Measure(C, TEXT("Multiplayer"), 19, true).X + 8, Y + 3, 14, FHKUi::TextDim);
	Y += 30;
	FHKUi::Text(C, N.StatusText(), X, Y, 14, FHKUi::TextDim); Y += 24;
	FHKUi::Text(C, TEXT("Your lobby code"), X, Y + 4, 16);
	FHKUi::Text(C, N.GetOwnCode(), X + FHKUi::Measure(C, TEXT("Your lobby code"), 16).X + 14, Y, 24, FHKUi::Accent, EHKAlign::Left, true); Y += 36;
	FHKUi::Text(C, N.WhereText(), X, Y, 14, FHKUi::TextDim); Y += 22;
	for (const FString& L : FHKUi::Wrap(C, TEXT("Share link: ") + N.Link(), 14, W)) { FHKUi::Text(C, L, X, Y, 14, FHKUi::TextDim); Y += 19; }
	Y += 9;
	// Join field + buttons.
	float Bw = 0;
	const bool bCode = Edit == EEdit::Code;
	FHKUi::Panel(C, X, Y, 110, 32, FLinearColor(1, 1, 1, bCode ? .22f : .1f), 6);
	FHKUi::Text(C, bCode ? Draft + TEXT("|") : FString(TEXT("CODE")), X + 12, Y + 5, 17, bCode ? FHKUi::TextMain : FHKUi::TextDim, EHKAlign::Left, true);
	if (Clickable) Clickable(TEXT("mpJoinCode"), X, Y, 110, 32);
	Button(TEXT("mpJoin"), TEXT("Join lobby"), X + 120, Y, Bw);
	if (!N.HomeHidden()) { float Hw = 0; Button(TEXT("mpHome"), TEXT("Back to my own lobby"), X + 130 + Bw, Y, Hw); }
	Y += 42;
	// Code pad (controller).
	if (CodePad.IsSet())
	{
		const FHKCodePad& P = CodePad.GetValue();
		FHKUi::Panel(C, X, Y, 320, 104, FHKUi::PanelBg, 10);
		FHKUi::Text(C, TEXT("Join lobby"), X + 14, Y + 8, 16, FHKUi::TextMain, EHKAlign::Left, true);
		for (int32 i = 0; i < 4; i++)
		{
			const float Sx = X + 14 + i * 52;
			FHKUi::Panel(C, Sx, Y + 34, 44, 40, i == P.Pos ? FHKUi::Accent : FLinearColor(1, 1, 1, .12f), 6);
			FHKUi::Text(C, FString::Chr(P.Chars[i]), Sx + 22, Y + 40, 22, FHKUi::TextMain, EHKAlign::Center, true);
		}
		FHKUi::Text(C, TEXT("←→ move · ↑↓ change · ✕ join · ○ cancel"), X + 14, Y + 80, 13, FHKUi::TextDim);
		Y += 114;
	}
	// Name and microphone.
	const bool bName = Edit == EEdit::Name;
	FHKUi::Text(C, TEXT("Your name"), X, Y + 6, 16);
	FHKUi::Panel(C, X + 110, Y, 260, 32, FLinearColor(1, 1, 1, bName ? .22f : .1f), 6);
	FHKUi::Text(C, bName ? Draft + TEXT("|") : N.GetName(), X + 122, Y + 6, 16);
	if (Clickable) Clickable(TEXT("mpName"), X + 110, Y, 260, 32);
	Y += 42;
	FHKUi::Frame(C, X, Y + 4, 18, 18, 2, FHKUi::TextMain);
	if (MpPtr->Mic()) FHKUi::Rect(C, X + 5, Y + 9, 8, 8, FHKUi::Accent);
	FHKUi::Text(C, TEXT("Voice chat – microphone on (everyone in the lobby hears you)"), X + 28, Y + 3, 15);
	if (Clickable) Clickable(TEXT("mpMic"), X, Y, W, 26);
	Y += 34;
	// Players (sorted by slot): colour, name, (you), microphone.
	for (const FHKNetPlayer& P : N.PlayersBySlot())
	{
		FHKUi::Disc(C, FVector2D(X + 8, Y + 10), 6, HKAvatars::SlotCss(P.Slot));
		FHKUi::Text(C, P.Name + (P.Id == N.You() ? TEXT(" (you)") : TEXT("")) + (P.bMic ? TEXT("  · mic on") : TEXT("")), X + 22, Y, 15);
		Y += 24;
	}
	Y += 4;
	for (const FString& L : FHKUi::Wrap(C, TEXT("Controller: △ enter a lobby code · □ back to your own lobby · Share: quick chat in the game · Keyboard: Enter or T to chat"), 13, W)) { FHKUi::Text(C, L, X, Y, 13, FHKUi::TextDim); Y += 18; }
	return Y + 8;
}
