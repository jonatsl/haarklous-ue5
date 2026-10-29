// Port of the "Stash boosts" and "Shops" parts of reference/web-game/game-main.js (Unreal issues #5 and #6): the three
// hidden stashes and their 30-second boosts, the Joker and Kiwi shops, money in kroner, the basket and the bag.
// Keep it line by line with the JavaScript; the HUD boxes are drawn with FHKUi (#boost, #boostFx, #wallet, #basket, #bag).
#include "HKGame.h"
#include "HKAudio.h"
#include "HKAvatars.h"
#include "HKData.h"
#include "HKRigActor.h"
#include "HKUi.h"
#include "Haarklous.h"
#include "Camera/CameraComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"

namespace
{
	// BOOSTS of game-main.js, without the emoji (the game's font has none; the badge shows the boost's colour).
	const TCHAR* BoostName(const FString& K)
	{
		return K == TEXT("speed") ? TEXT("3× running speed") : K == TEXT("jump") ? TEXT("3× jump height") : TEXT("Bullet time – the world slows down");
	}
	const TCHAR* BoostKinds[] = {TEXT("speed"), TEXT("jump"), TEXT("slowmo")};
	const TCHAR* Greet[] = {TEXT("Hei! Velkommen inn."), TEXT("Hei hei!"), TEXT("God dag!"), TEXT("Hei, si fra om du trenger hjelp.")};
	FString UseText(const FString& Use)
	{
		return Use == TEXT("beer") ? TEXT("drink: +30 s drunk, a bottle for the deposit") : Use == TEXT("soda") ? TEXT("drink: a bottle for the deposit")
			: Use == TEXT("energy") ? TEXT("drink: 15 s of 3× speed") : Use == TEXT("food") ? TEXT("eat: sobers you up") : TEXT("empty onto the firepit (+3)");
	}
	FLinearColor Css(uint32 Rgb, float A = 1) { return FLinearColor(FColor((Rgb >> 16) & 255, (Rgb >> 8) & 255, Rgb & 255, (uint8)(A * 255))); }
}

void AHKGame::SetupShops()
{
	const FHKData& Data = FHKData::Get();
	const FString Dir = FHKData::DataDir() / TEXT("rigs");
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Stashes: the crates are a rig (the used item's parts hide), the crate obstacles are in the walk world already.
	Stash = MakeUnique<FHKStash>(HKJson::Obj(Data.Game, TEXT("stash")), Dir / TEXT("stash.json"));
	StashActor = GetWorld()->SpawnActor<AHKRigActor>(AHKRigActor::StaticClass(), FTransform::Identity, SP);
	StashActor->Init(&Stash->Rig, TEXT("stash"));
	// Shops: the buildings are level geometry; the rig actor shows the sliding doors.
	Shops = MakeUnique<FHKShops>(HKJson::Obj(Data.Game, TEXT("shops")), Dir / TEXT("shops.json"));
	ShopsActor = GetWorld()->SpawnActor<AHKRigActor>(AHKRigActor::StaticClass(), FTransform::Identity, SP);
	ShopsActor->Init(&Shops->Rig, TEXT("shops"), true);
	Shops->Wallet.SavePath = FPaths::ProjectSavedDir() / TEXT("HKWallet.json");   // per player, like the web's localStorage
	Shops->Wallet.Load();
	// The cashiers: a person in the shop's colour behind the till (avatars.avatar(0, 'Kasserer', shirt)).
	for (int32 i = 0; i < Shops->Shops.Num(); i++)
	{
		FCashier Ca;
		Ca.Av = HKAvatars::Avatar(i, TEXT("Kasserer"));
		Ca.Av->bTagVisible = false;
		AHKRigActor* A = GetWorld()->SpawnActor<AHKRigActor>(AHKRigActor::StaticClass(), FTransform::Identity, SP);
		A->Init(&Ca.Av->Rig, Ca.Av->RigName);
		A->TintPerson(HKAvatars::SlotColorHex(i), Shops->Shops[i].Shirt);
		Ca.Actor = A;
		Cashiers.Add(MoveTemp(Ca));
	}
	UE_LOG(LogHaarklous, Log, TEXT("Stash: %d items; shops: %d, %d hit boxes; wallet %.2f kr, %d bottles"), Stash->Items.Num(), Shops->Shops.Num(), Shops->Boxes.Num(), Shops->Wallet.Money, Shops->Wallet.Pant);
}

// ---------- Stash boosts (30 s, random): 3x running speed, 3x jump height or bullet time ----------
FHKPrompt AHKGame::StashPrompt()
{
	if (Player.Mode != TEXT("foot") || !Stash) return FHKPrompt();
	const FLookHit O = LookTarget();
	const int32 It = O.Stash >= 0 ? O.Stash : Stash->Nearest(FVector(Player.X, 0, Player.Z), Player.Feet);
	if (It < 0) return FHKPrompt();
	if (!Stash->Items[It].bReady) return {TEXT("Nothing left here – come back later"), []() {}};
	return {Stash->Items[It].Kind == TEXT("line") ? TEXT("□ Snort a line") : TEXT("□ Use the needle"), [this, It]() { TakeBoost(It); }};
}

void AHKGame::TakeBoost(int32 Item)
{
	Stash->Use(Item);
	const FString Kind = BoostKinds[FMath::Min(2, (int32)FMath::FloorToDouble(FMath::FRand() * 3))];
	BoostKind = Kind;
	BoostT = 30;
	FHKAudio::Get().Sfx(Stash->Items[Item].Kind == TEXT("line") ? TEXT("sniff") : TEXT("needle"));
	FHKAudio::Get().Muffle(Kind == TEXT("slowmo"));
	Notice(FString::Printf(TEXT("%s for 30 seconds!"), BoostName(Kind)), 4);
}

// ---------- Shops (Joker and Kiwi) and money in kroner ----------
void AHKGame::Earn(double Amount, const FString& Why)
{
	if (!(Amount > 0) || !Shops) return;
	Shops->Wallet.Earn(Amount);
	WalletT = 6;
	FHKAudio::Get().Sfx(TEXT("coin"));
	Notice(FString::Printf(TEXT("+%s · %s"), *FHKShops::Kr(Amount), *Why), 4);
}

void AHKGame::ShopSay(int32 Shop, const FString& Text)
{
	if (Cashiers.IsValidIndex(Shop)) Cashiers[Shop].Av->Say(Text);
}

FHKPrompt AHKGame::ShopPrompt()
{
	if (Player.Mode != TEXT("foot") || !Shops) return FHKPrompt();
	const int32 S = Shops->ShopAt(Player.X, Player.Z, Player.Feet);
	const FLookHit O = LookTarget();
	const FHKShopBox* U = O.ShopBox >= 0 ? &Shops->Boxes[O.ShopBox] : nullptr;
	if (U && U->Kind == TEXT("item") && U->Product >= 0)
	{
		const int32 Sh = U->Shop, P = U->Product;
		const double Price = FHKShops::PriceOf(Shops->Shops[Sh], Shops->Products[P]);
		return {FString::Printf(TEXT("□ Take %s – %s"), *Shops->Products[P].Name, *FHKShops::Kr(Price)), [this, Sh, P]() { TakeProduct(Sh, P); }};
	}
	if (U && U->Kind == TEXT("pant"))
	{
		const int32 N = Shops->Wallet.Pant;
		if (N) return {FString::Printf(TEXT("□ Return %d empty bottle%s – %s"), N, N > 1 ? TEXT("s") : TEXT(""), *FHKShops::Kr(N * FHKShops::PANT)), [this]() { ReturnPant(); }};
		return {TEXT("Bottle return – no empty bottles yet"), []() {}};
	}
	if (S >= 0 && ((U && U->Kind == TEXT("till")) || Shops->AtTill(S, Player.X, Player.Z)))
	{
		int32 N = 0;
		double Total = 0;
		for (const FBasketItem& B : Basket) if (B.Shop == S) { N++; Total += B.Price; }
		if (N) return {FString::Printf(TEXT("□ Pay %s (%d item%s)"), *FHKShops::Kr(Total), N, N > 1 ? TEXT("s") : TEXT("")), [this, S]() { PayAt(S); }};
		return {TEXT("□ Talk to the cashier"), [this, S]() { ShopSay(S, Greet[FMath::Min(3, (int32)FMath::FloorToDouble(FMath::FRand() * 4))]); }};
	}
	return FHKPrompt();
}

void AHKGame::TakeProduct(int32 Shop, int32 Product)
{
	const double Price = FHKShops::PriceOf(Shops->Shops[Shop], Shops->Products[Product]);
	Basket.Add({Shop, Product, Price});
	FHKAudio::Get().Sfx(TEXT("click"));
	WalletT = 8;
	Notice(FString::Printf(TEXT("%s in the basket – pay at the till"), *Shops->Products[Product].Name), 2);
}

void AHKGame::ReturnPant()
{
	const int32 N = Shops->Wallet.CashPant();
	FHKAudio::Get().Sfx(TEXT("pant"));
	WalletT = 6;
	Notice(FString::Printf(TEXT("The machine takes %d bottle%s: +%s"), N, N > 1 ? TEXT("s") : TEXT(""), *FHKShops::Kr(N * FHKShops::PANT)), 3);
}

void AHKGame::PayAt(int32 S)
{
	const bool bBeerOk = FHKShops::BeerAllowed(ShopClock ? ShopClock() : FDateTime::Now());
	TArray<FBasketItem> Buy;
	int32 Refused = 0;
	for (const FBasketItem& B : Basket)
		if (B.Shop == S) { if (!bBeerOk && Shops->Products[B.Product].bBeer) Refused++; else Buy.Add(B); }
	double Sum = 0;
	for (const FBasketItem& B : Buy) Sum += B.Price;
	const double Total = HK::JsRound(Sum * 100) / 100;
	if (!Buy.Num())
	{
		Basket.RemoveAll([S](const FBasketItem& B) { return B.Shop == S; });
		ShopSay(S, TEXT("Beklager, jeg kan ikke selge øl nå. Ølsalget er man–fre 08–20 og lør 08–18."));
		Notice(TEXT("No beer sales now (Norwegian law: Mon–Fri 08–20, Sat 08–18, never on Sunday). The beer goes back."), 5);
		return;
	}
	if (!Shops->Wallet.Pay(Total))
	{
		ShopSay(S, FString::Printf(TEXT("Kortet ble avvist – du mangler %s."), *FHKShops::Kr(Total - Shops->Wallet.Money)));
		FHKAudio::Get().Sfx(TEXT("locked"));
		Notice(FString::Printf(TEXT("Card declined: you have %s, the total is %s."), *FHKShops::Kr(Shops->Wallet.Money), *FHKShops::Kr(Total)), 4);
		return;
	}
	for (const FBasketItem& B : Buy) Shops->Wallet.Add(Shops->Products[B.Product].Id);
	Basket.RemoveAll([S](const FBasketItem& B) { return B.Shop == S; });
	ShopSay(S, Refused ? FString::Printf(TEXT("Det blir %s. Ølet må jeg ta tilbake – ølsalget er stengt nå."), *FHKShops::Kr(Total)) : FString::Printf(TEXT("Det blir %s. Takk for handelen!"), *FHKShops::Kr(Total)));
	FHKAudio::Get().Sfx(TEXT("register"));
	WalletT = 8;
	Notice(FString::Printf(TEXT("Paid %s%s · D-pad ↓ or the ↓ key opens your bag"), *FHKShops::Kr(Total), Refused ? TEXT(" (no beer at this hour)") : TEXT("")), 4);
}

void AHKGame::TickShops(double Dt)
{
	// "if (boost && (boost.t -= dt) <= 0) {boost = null; audio.muffle(false); notice('The boost wears off.', 2);}"
	if (!BoostKind.IsEmpty() && (BoostT -= Dt) <= 0) { BoostKind.Empty(); FHKAudio::Get().Muffle(false); Notice(TEXT("The boost wears off."), 2); }
	if (Stash) Stash->Update(Dt);
	BoostLook();
	if (!Shops) return;
	if (Shops->Update(Dt, Player.X, Player.Z, Player.Feet)) FHKAudio::Get().Sfx(TEXT("shopDoor"));
	WalletT = FMath::Max(0.0, WalletT - Dt);
	const int32 S = Player.Mode == TEXT("foot") ? Shops->ShopAt(Player.X, Player.Z, Player.Feet) : -1;
	if (S >= 0 && S != LastShop) ShopSay(S, Greet[FMath::Min(1, (int32)FMath::FloorToDouble(FMath::FRand() * 2))]);
	// Walking out with unpaid items sets off the alarm, and the items go back on the shelves.
	if (LastShop >= 0 && S != LastShop && Basket.ContainsByPredicate([this](const FBasketItem& B) { return B.Shop == LastShop; }))
	{
		FHKAudio::Get().Sfx(TEXT("alarm"));
		const int32 L = LastShop;
		Basket.RemoveAll([L](const FBasketItem& B) { return B.Shop == L; });
		Notice(TEXT("BEEP BEEP – the alarm at the door goes off. You put the unpaid items back."), 4);
	}
	LastShop = S;
	if (bBag && Player.Mode != TEXT("foot")) bBag = false;
	// The cashiers: behind the till, turning toward you (shops.update), with their speech bubbles.
	for (int32 i = 0; i < Cashiers.Num() && i < Shops->Shops.Num(); i++)
	{
		const FHKShop& Sh = Shops->Shops[i];
		FHKAvatar& Av = *Cashiers[i].Av;
		const FVector2D W = Sh.ToWorld(2.4, .5);
		Av.Group()->Position = FVector(W.X, Sh.FloorY, W.Y);
		Av.Group()->SetRotY(Sh.Yaw() + Sh.CashierYaw);
		FHKPoseArgs Pa;
		Pa.Dt = Dt;
		Av.Pose(Pa);
		Av.Update(Dt, CamPos);
	}
}

// The bag: what you have bought (D-pad ↓ / ↓ key on foot).
TArray<TPair<int32, int32>> AHKGame::BagItems() const
{
	TArray<TPair<int32, int32>> Out;
	if (!Shops) return Out;
	for (const TPair<FString, int32>& E : Shops->Wallet.Bag) { const int32 P = Shops->ProductIndex(E.Key); if (P >= 0) Out.Add({P, E.Value}); }
	return Out;
}

void AHKGame::OpenBag()
{
	bBag = true;
	BagSel = 0;
	bBagStick = false;
	WatchHide();
}

void AHKGame::BagInput()
{
	const double Y = Frame.Move.Y;
	const TArray<TPair<int32, int32>> Items = BagItems();
	if (Frame.WasPressed("up") || (Y < -.5 && !bBagStick)) BagSel = FMath::Max(0, BagSel - 1);
	if (Frame.WasPressed("down") || (Y > .5 && !bBagStick)) BagSel = FMath::Min(FMath::Max(0, Items.Num() - 1), BagSel + 1);
	bBagStick = FMath::Abs(Y) > .5;
	if (Frame.WasPressed("cross") && Items.IsValidIndex(BagSel)) UseItem(Items[BagSel].Key);
	if (Frame.WasPressed("circle")) bBag = false;
	BagSel = FMath::Clamp(BagSel, 0, FMath::Max(0, BagItems().Num() - 1));
	// The bag owns the stick and these buttons while it is open.
	Frame.Move = FVector2D::ZeroVector;
	for (const TCHAR* B : {TEXT("cross"), TEXT("circle"), TEXT("up"), TEXT("down"), TEXT("square"), TEXT("triangle")}) Frame.Pressed.Remove(FName(B));
}

void AHKGame::UseItem(int32 Product)
{
	const FHKProduct& P = Shops->Products[Product];
	FHKAudio& Au = FHKAudio::Get();
	if (P.Use == TEXT("wood"))
	{
		if (HK::Hypot(Player.X - FirePos.X, Player.Z - FirePos.Y) > 8) { Notice(TEXT("Take the firewood to the firepit in the park first."), 3); return; }
		// Three logs, 350 ms apart, each shared online like the woodpile.
		for (int32 i = 0; i < 3; i++)
		{
			FTimerHandle H;
			GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				// fire.throwLog(...); mp.addWood() (offline: the local fire, as AddLog does)
				if (ThrowLog) ThrowLog(FVector(Player.X, Player.Feet + 1.3, Player.Z));
				if (AddWood) AddWood();
				else { LocalFire.Level = FMath::Min((double)FireMax, FireLevel() + 1); LocalFire.T = FPlatformTime::Seconds(); }
			}), .001 + i * .35, false);
		}
		Notice(TEXT("You empty the sack of firewood onto the fire."), 3);
	}
	else if (P.Use == TEXT("beer")) { Au.Sfx(TEXT("bottleOpen")); Au.Sfx(TEXT("gulp")); Drunk += 30; BeerT = 20; Shops->Wallet.AddPant(1); Notice(FString::Printf(TEXT("You drink a %s. (+30 s drunk, +1 bottle for the deposit)"), *P.Name), 3); }
	else if (P.Use == TEXT("soda")) { Au.Sfx(TEXT("bottleOpen")); Au.Sfx(TEXT("gulp")); Drunk = FMath::Max(0.0, Drunk - 10); Shops->Wallet.AddPant(1); Notice(FString::Printf(TEXT("%s – refreshing. (+1 bottle for the deposit)"), *P.Name), 3); }
	else if (P.Use == TEXT("energy")) { Au.Sfx(TEXT("bottleOpen")); Au.Sfx(TEXT("gulp")); BoostKind = TEXT("speed"); BoostT = 15; Shops->Wallet.AddPant(1); Notice(FString::Printf(TEXT("%s: 15 s of 3× speed! (+1 can for the deposit)"), *P.Name), 3); }
	else
	{
		Drunk = FMath::Max(0.0, Drunk - (P.Sober ? P.Sober : 10));
		Au.Sfx(TEXT("gulp"));
		Notice(FString::Printf(TEXT("You eat %s.%s"), *P.Name, P.Sober ? *FString::Printf(TEXT(" (−%s s drunk)"), *HKJs::Num(P.Sober)) : TEXT("")), 3);
	}
	Shops->Wallet.Take(P.Id);
	WalletT = 4;
}

// The boost's screen colours (renderFrame boostFilter): speed saturate(1.35) contrast(1.06), jump hue-rotate(-14deg)
// saturate(1.25), bullet time saturate(.35) contrast(1.15) brightness(.96). Unreal has no hue rotation in its colour
// grading, so the jump boost gets the saturation and a slight green-blue white balance shift instead.
void AHKGame::BoostLook()
{
	if (!Camera) return;
	FPostProcessSettings& P = Camera->PostProcessSettings;
	const bool bOn = !BoostKind.IsEmpty();
	P.bOverride_ColorSaturation = P.bOverride_ColorContrast = P.bOverride_ColorGain = P.bOverride_WhiteTint = bOn;
	if (!bOn) return;
	const bool bSpeed = BoostKind == TEXT("speed"), bJump = BoostKind == TEXT("jump");
	const float Sat = bSpeed ? 1.35f : bJump ? 1.25f : .35f, Con = bSpeed ? 1.06f : bJump ? 1.f : 1.15f, Gain = bSpeed || bJump ? 1.f : .96f;
	P.ColorSaturation = FVector4(Sat, Sat, Sat, 1);
	P.ColorContrast = FVector4(Con, Con, Con, 1);
	P.ColorGain = FVector4(Gain, Gain, Gain, 1);
	P.WhiteTint = bJump ? -.12f : 0.f;
}

void AHKGame::DrawShopsHud(UCanvas* C)
{
	const FVector2D Sz = FHKUi::Size(C);
	// #boostFx: the screen edge glows in the boost colour and pulses (1.6 s, bullet time 3.2 s; opacity .75 ... 1).
	if (!BoostKind.IsEmpty())
	{
		const bool bSlow = BoostKind == TEXT("slowmo");
		const FLinearColor Col = BoostKind == TEXT("speed") ? Css(0x00d0ff) : BoostKind == TEXT("jump") ? Css(0x7dff6a) : Css(0x1a2a5a);
		const float Peak = BoostKind == TEXT("speed") ? .19f : BoostKind == TEXT("jump") ? .18f : .40f, Inner = bSlow ? .45f : .55f;
		const float Pulse = .875f - .125f * FMath::Cos((float)(Time * 2 * PI / (bSlow ? 3.2 : 1.6)));
		// radial-gradient(ellipse at center, transparent Inner, colour at the edge): bands from the inner ellipse out.
		const int32 N = 14;
		for (int32 k = 0; k < N; k++)
		{
			const float T0 = Inner + (1 - Inner) * k / N, T1 = Inner + (1 - Inner) * (k + 1) / N, A = Peak * Pulse * (k + .5f) / N;
			// The frame between the ellipse "radii" T0 and T1 (half the screen = 1), drawn as four bars.
			const float X0 = Sz.X / 2 * (1 - T1 * 1.414f), X1 = Sz.X / 2 * (1 - T0 * 1.414f), Y0 = Sz.Y / 2 * (1 - T1 * 1.414f), Y1 = Sz.Y / 2 * (1 - T0 * 1.414f);
			const FLinearColor Ck(Col.R, Col.G, Col.B, A);
			if (X1 <= 0 && Y1 <= 0) continue;
			const float Bx = FMath::Max(0.f, X0), By = FMath::Max(0.f, Y0);
			FHKUi::Rect(C, Bx, By, Sz.X - 2 * Bx, FMath::Max(0.f, Y1 - By), Ck);                                   // top
			FHKUi::Rect(C, Bx, Sz.Y - Y1, Sz.X - 2 * Bx, FMath::Max(0.f, Y1 - By), Ck);                            // bottom
			FHKUi::Rect(C, Bx, FMath::Max(By, Y1), FMath::Max(0.f, X1 - Bx), Sz.Y - 2 * FMath::Max(By, Y1), Ck);   // left
			FHKUi::Rect(C, Sz.X - X1, FMath::Max(By, Y1), FMath::Max(0.f, X1 - Bx), Sz.Y - 2 * FMath::Max(By, Y1), Ck);   // right
		}
		// #boost: the badge under the drunk badge.
		const FString S = FString::Printf(TEXT("%s · %d s"), BoostName(BoostKind), (int32)FMath::CeilToDouble(BoostT));
		const float W = FHKUi::Measure(C, S, 16, true).X + 24;
		FHKUi::Panel(C, 22, 52, W, 30, Css(0x0d3b4f, .8f), 14);
		FHKUi::Text(C, S, 34, 57, 16, FHKUi::TextMain, EHKAlign::Left, true);
	}
	if (!Shops) return;
	// The cashiers' speech bubbles.
	for (const FCashier& Ca : Cashiers) Ca.Av->DrawHud(C);
	// #wallet: the web's "💰 500,00 kr · 🍾 0" (no emoji in the game's font) while in a shop, with a basket or the bag, or a few seconds after money changed.
	const int32 S = Player.Mode == TEXT("foot") ? Shops->ShopAt(Player.X, Player.Z, Player.Feet) : -1;
	if (S >= 0 || Basket.Num() || bBag || WalletT > 0)
	{
		const FString T = FString::Printf(TEXT("%s · %d bottle%s"), *FHKShops::Kr(Shops->Wallet.Money), Shops->Wallet.Pant, Shops->Wallet.Pant == 1 ? TEXT("") : TEXT("s"));
		const float W = FHKUi::Measure(C, T, 16, true).X + 28;
		FHKUi::Panel(C, Sz.X - 20 - W, 18, W, 32, Css(0x10241a, .8f), 14);
		FHKUi::Text(C, T, Sz.X - 20 - W + 14, 24, 16, FHKUi::TextMain, EHKAlign::Left, true);
	}
	// #basket
	if (Basket.Num())
	{
		const float W = 260, X = Sz.X - 20 - W, H = 34 + Basket.Num() * 20 + 26 + 20;
		float Y = 56;
		FHKUi::Panel(C, X, Y, W, H, Css(0x0b0f14, .85f), 12);
		Y += 10;
		FHKUi::Text(C, TEXT("Basket"), X + 14, Y, 15, FHKUi::TextMain, EHKAlign::Left, true); Y += 22;
		double Total = 0;
		for (const FBasketItem& B : Basket)
		{
			FHKUi::Text(C, Shops->Products[B.Product].Name, X + 14, Y, 14);
			FHKUi::Text(C, FHKShops::Kr(B.Price), X + W - 14, Y, 14, FHKUi::TextMain, EHKAlign::Right);
			Total += B.Price; Y += 20;
		}
		FHKUi::Rect(C, X + 14, Y + 2, W - 28, 1, FLinearColor(1, 1, 1, .2f)); Y += 6;
		FHKUi::Text(C, TEXT("Total"), X + 14, Y, 14, FHKUi::TextMain, EHKAlign::Left, true);
		FHKUi::Text(C, FHKShops::Kr(Total), X + W - 14, Y, 14, FHKUi::TextMain, EHKAlign::Right, true); Y += 20;
		FHKUi::Text(C, TEXT("Pay at the till"), X + 14, Y, 13, FHKUi::TextDim);
	}
	// #bag: left, vertically centred; the chosen line highlighted.
	if (bBag)
	{
		const TArray<TPair<int32, int32>> Items = BagItems();
		const int32 Rows = FMath::Max(1, Items.Num());
		const float W = 340, X = 18, H = 12 + 24 + Rows * 42 + 28, Y0 = Sz.Y / 2 - H / 2;
		FHKUi::Panel(C, X, Y0, W, H, Css(0x0b0f14, .9f), 12);
		float Y = Y0 + 12;
		FHKUi::Text(C, FString::Printf(TEXT("Bag · %s · %d empty bottle%s"), *FHKShops::Kr(Shops->Wallet.Money), Shops->Wallet.Pant, Shops->Wallet.Pant == 1 ? TEXT("") : TEXT("s")), X + 12, Y, 15, FHKUi::TextMain, EHKAlign::Left, true);
		Y += 24;
		if (!Items.Num()) { FHKUi::Text(C, TEXT("Empty – buy something at Joker or Kiwi"), X + 22, Y + 10, 15); Y += 42; }
		for (int32 i = 0; i < Items.Num(); i++)
		{
			const bool bSel = i == BagSel;
			if (bSel) FHKUi::Panel(C, X + 10, Y, W - 20, 40, Css(0xffd166), 7);
			const FLinearColor Ink = bSel ? Css(0x111111) : FHKUi::TextMain;
			FHKUi::Text(C, FString::Printf(TEXT("%d × %s"), Items[i].Value, *Shops->Products[Items[i].Key].Name), X + 20, Y + 3, 15, Ink, EHKAlign::Left, bSel);
			FHKUi::Text(C, UseText(Shops->Products[Items[i].Key].Use), X + 20, Y + 21, 12, bSel ? Ink : FHKUi::TextDim);
			Y += 42;
		}
		FHKUi::Text(C, TEXT("↑↓ choose · ✕ / Space use · ○ / C close"), X + 12, Y + 4, 13, FLinearColor(1, 1, 1, .7f));
	}
}
