// Ports of the smaller web modules that belong to the main loop: chat and quick chat (game-main.js), the gear menu,
// the photo camera with its gallery (game-camera.js), the wristwatch (game-watch.js) and the map (game-map.js).
#include "HKGame.h"
#include "HKAudio.h"
#include "HKData.h"
#include "HKMath.h"
#include "HKRigActor.h"
#include "HKUi.h"
#include "HKVehicle.h"
#include "Haarklous.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UnrealClient.h"

namespace
{
	const TCHAR* Quick[] = {TEXT("Hei! 👋"), TEXT("Skål! 🍺"), TEXT("Kom til bålet 🔥"), TEXT("Følg meg!"), TEXT("Hopp på!"), TEXT("Vent litt"), TEXT("Haha 😂"), TEXT("Ha det!"), TEXT("⌨ Type a message…")};
	const int32 NQuick = UE_ARRAY_COUNT(Quick);
	// game-avatars.js SLOT_CSS (jacket colour per player slot).
	const FLinearColor SlotCss[5] = {FLinearColor(FColor(0xe0, 0x70, 0x3c)), FLinearColor(FColor(0x42, 0x8b, 0xe6)), FLinearColor(FColor(0x4c, 0xaf, 0x50)), FLinearColor(FColor(0xc9, 0x4f, 0xd6)), FLinearColor(FColor(0xe6, 0xc2, 0x29))};
	// The web's font has emoji; Roboto does not, so they are left out on screen (the chat text itself is unchanged).
	FString Displayable(const FString& S)
	{
		FString O;
		for (int32 i = 0; i < S.Len(); i++)
		{
			const TCHAR C = S[i];
			if (C >= 0xD800 && C <= 0xDFFF) continue;   // emoji (surrogate pairs)
			if (C == 0x2328 || C == 0xFE0F) continue;
			O.AppendChar(C);
		}
		return O.TrimEnd();
	}
	double EaseInOut(double T) { return T < .5 ? 2 * T * T : 1 - FMath::Pow(-2 * T + 2, 2.0) / 2; }
	// tan(70/2) / tan(55/2): the web draws the watch and camera with a 55 degree overlay camera.
	const double OverlayK = FMath::Tan(FMath::DegreesToRadians(35.0)) / FMath::Tan(FMath::DegreesToRadians(27.5));
	const double OverlayShrink = .4;   // everything shrunk toward the eye (same picture, no clipping into walls)
}

// ---------- Chat ----------
void AHKGame::ChatLine(const FString& Name, const FString& Text, int32 Slot, bool bSys, bool bMine, bool bOld)
{
	ChatLog.Add({Name.IsEmpty() ? FString(TEXT("Player")) : Name, Text, Slot, bSys, bOld ? 0.0 : Time});
	while (ChatLog.Num() > 30) ChatLog.RemoveAt(0);
	ChatRecentT = 12;
	if (!bMine && !bOld && !bSys) FHKAudio::Get().Sfx(TEXT("beep"), FHKSfxOpts(.4));
}

void AHKGame::OpenChat()
{
	bChatOpen = true;
	ChatDraft.Empty();
	if (InputProc) { InputProc->bTextEntry = true; InputProc->TypedText.Empty(); InputProc->TextKeys.Reset(); }
}

void AHKGame::CloseChat()
{
	bChatOpen = false;
	if (InputProc) InputProc->bTextEntry = false;
}

// ---------- Photo camera ----------
static FString PhotoDir() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Haarklous"), TEXT("Photos")); }

void AHKGame::LoadPhotos()
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(PhotoDir() / TEXT("*.jpg")), true, false);
	Files.Sort();
	for (const FString& F : Files)
	{
		FPhoto P;
		P.Id = FCString::Atoi64(*FPaths::GetBaseFilename(F));
		P.File = PhotoDir() / F;
		FFileHelper::LoadFileToString(P.Place, *(PhotoDir() / (FPaths::GetBaseFilename(F) + TEXT(".txt"))));
		Photo.Photos.Add(P);
	}
}

void AHKGame::PhotoToggle()
{
	Photo.bActive = !Photo.bActive; Photo.bGallery = false; Photo.Raised = 0; Photo.Zoom = 1;
}

void AHKGame::PhotoOpenGallery(bool bOpen)
{
	Photo.bGallery = bOpen;
	if (bOpen) Photo.Index = Photo.Photos.Num() - 1;
}

void AHKGame::PhotoCapture()
{
	// "Grab the frame that was just rendered (... without the HUD overlays)": hide the hands/camera, take a screenshot.
	if (bPhotoPending) return;
	bPhotoPending = true;
	PhotoPendingPlace = PlaceName();
	if (PhotoActor) PhotoActor->SetActorHiddenInGame(true);
	if (WatchActor) WatchActor->SetActorHiddenInGame(true);
	ShotHandle = UGameViewportClient::OnScreenshotCaptured().AddLambda([this](int32 W, int32 H, const TArray<FColor>& Colors)
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(ShotHandle);
		bPhotoPending = false;
		if (PhotoActor) PhotoActor->SetActorHiddenInGame(false);
		if (WatchActor) WatchActor->SetActorHiddenInGame(false);
		const int32 Ow = 1280, Oh = FMath::RoundToInt(1280.0 * H / FMath::Max(1, W));
		TArray<FColor> Out;
		Out.SetNumUninitialized(Ow * Oh);
		FImageUtils::ImageResize(W, H, Colors, Ow, Oh, Out, false);
		for (FColor& C : Out) C.A = 255;
		IImageWrapperModule& M = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> J = M.CreateImageWrapper(EImageFormat::JPEG);
		if (!J || !J->SetRaw(Out.GetData(), Out.Num() * 4, Ow, Oh, ERGBFormat::BGRA, 8)) return;
		const int64 Id = FDateTime::UtcNow().ToUnixTimestamp() * 1000 + FDateTime::UtcNow().GetMillisecond();
		const FString File = PhotoDir() / FString::Printf(TEXT("%lld.jpg"), Id);
		IFileManager::Get().MakeDirectory(*PhotoDir(), true);
		const TArray64<uint8>& Bytes = J->GetCompressed(86);
		FFileHelper::SaveArrayToFile(TArrayView<const uint8>(Bytes.GetData(), Bytes.Num()), *File);
		FFileHelper::SaveStringToFile(PhotoPendingPlace, *(PhotoDir() / FString::Printf(TEXT("%lld.txt"), Id)));
		Photo.Photos.Add({Id, File, PhotoPendingPlace});
		Photo.FlashT = .35; Photo.PreviewT = 2.2;
		GalleryTexFile.Empty();
		UE_LOG(LogHaarklous, Log, TEXT("Photo saved: %s (%s)"), *File, *PhotoPendingPlace);
	});
	FScreenshotRequest::RequestScreenshot(false);
	FHKAudio::Get().Sfx(TEXT("shutter"));
}

// ---------- Map ----------
void AHKGame::MapToggle()
{
	Map.bOpen = !Map.bOpen;
	if (Map.bOpen) { const FVector2D L = FHKData::Get().ToLocal(Player.X, Player.Z); Map.Cx = L.X; Map.Cs = L.Y; }
}

// ---------- Overlay models ----------
void AHKGame::SpawnOverlay()
{
	const FString Dir = FHKData::DataDir() / TEXT("rigs");
	auto Make = [&](const FString& Name, FHKRig*& Out) -> AHKRigActor*
	{
		TUniquePtr<FHKRig> R = MakeUnique<FHKRig>();
		if (!R->LoadFromJson(Dir / (Name + TEXT(".json")))) return nullptr;
		Out = R.Get();
		Rigs.Add(MoveTemp(R));
		AHKRigActor* A = GetWorld()->SpawnActor<AHKRigActor>();
		A->Init(Out, Name, false, false);
		Out->Root->bVisible = false;
		return A;
	};
	WatchActor = Make(TEXT("watch"), WatchRig);
	PhotoActor = Make(TEXT("photo_camera"), PhotoRig);
	// The dial is drawn live (PC local time) into a render target, like the web's canvas texture.
	if (WatchRig && WatchActor)
	{
		if (FHKNode* Dial = WatchRig->Find(TEXT("dial")))
			if (UStaticMeshComponent* M = WatchActor->MeshOf(Dial))
			{
				DialRT = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(this, UCanvasRenderTarget2D::StaticClass(), 256, 256);
				DialRT->OnCanvasRenderTargetUpdate.AddDynamic(this, &AHKGame::DrawDialRT);
				UMaterialInstanceDynamic* Mid = M->CreateAndSetMaterialInstanceDynamic(0);
				Mid->SetTextureParameterValue(FName("BaseMap"), DialRT);
				Mid->SetVectorParameterValue(FName("EmissiveColor"), FLinearColor(.25f, .25f, .25f));
				Mid->SetTextureParameterValue(FName("EmissiveMap"), DialRT);
				DialRT->UpdateResource();
			}
	}
	LoadPhotos();
}

void AHKGame::DrawDialRT(UCanvas* C, int32 W, int32 H) { DrawDial(C, W, H); }

void AHKGame::DrawDial(UCanvas* C, int32 W, int32 H)
{
	// drawDial(now): cream dial, minute ticks, 12/3/6/9, HAARKLOUS, a digital window and three hands.
	const FDateTime Now = FDateTime::Now();
	const double Hh = Now.GetHour(), Mm = Now.GetMinute(), Ss = Now.GetSecond() + Now.GetMillisecond() / 1000.0, R = 128;
	auto Px = [&](double X, double Y) { return FVector2D(R + X, R + Y); };
	auto Line = [&](FVector2D A, FVector2D B, float T, FLinearColor Col) { FCanvasLineItem L(A, B); L.SetColor(Col); L.LineThickness = T; C->DrawItem(L); };
	FCanvasTileItem Bg(FVector2D(0, 0), FVector2D(256, 256), FLinearColor(FColor(0xee, 0xea, 0xe0)));
	C->DrawItem(Bg);
	const FLinearColor Ink(FColor(0x1b, 0x1b, 0x1b));
	for (int32 i = 0; i < 60; i++)
	{
		const double A = i / 60.0 * 2 * PI;
		const bool bBig = i % 5 == 0;
		Line(Px(FMath::Sin(A) * (R - 8), -FMath::Cos(A) * (R - 8)), Px(FMath::Sin(A) * (R - (bBig ? 26 : 16)), -FMath::Cos(A) * (R - (bBig ? 26 : 16))), bBig ? 6 : 2, Ink);
	}
	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	auto Label = [&](const FString& S, double X, double Y, float Scale, FLinearColor Col)
	{
		FCanvasTextItem T(FVector2D::ZeroVector, FText::FromString(S), Font, Col);
		T.Scale = FVector2D(Scale, Scale);
		T.bCentreX = T.bCentreY = true;
		T.Position = Px(X, Y);
		C->DrawItem(T);
	};
	const int32 Nums[4] = {12, 3, 6, 9};
	for (int32 k = 0; k < 4; k++) { const double A = k * PI / 2; Label(FString::FromInt(Nums[k]), FMath::Sin(A) * (R - 46), -FMath::Cos(A) * (R - 46), 1.4f, Ink); }
	Label(TEXT("HAARKLOUS"), 0, -38, .62f, Ink);
	FCanvasTileItem Win(Px(-40, 26), FVector2D(80, 30), FLinearColor(FColor(0x20, 0x26, 0x2b)));
	C->DrawItem(Win);
	Label(FString::Printf(TEXT("%02d:%02d"), (int32)Hh, (int32)Mm), 0, 42, 1.0f, FLinearColor(FColor(0xd8, 0xf5, 0xe0)));
	auto Hand = [&](double A, double Len, float Wd, FLinearColor Col) { Line(Px(-FMath::Sin(A) * 14, FMath::Cos(A) * 14), Px(FMath::Sin(A) * Len, -FMath::Cos(A) * Len), Wd, Col); };
	Hand((FMath::Fmod(Hh, 12.0) + Mm / 60) / 12 * 2 * PI, R * .5, 9, Ink);
	Hand((Mm + Ss / 60) / 60 * 2 * PI, R * .74, 6, Ink);
	Hand(Ss / 60 * 2 * PI, R - 14, 2, FLinearColor(FColor(0xc0, 0x26, 0x2d)));
	FCanvasTileItem Dot(Px(-5, -5), FVector2D(10, 10), FLinearColor(FColor(0xc0, 0x26, 0x2d)));
	C->DrawItem(Dot);
}

void AHKGame::UpdateOverlayModels(double Dt)
{
	// Wristwatch: "t += Math.sign(target - t) * Math.min(Math.abs(target - t), dt / .32); if (target && (holdT += dt) > 6) target = 0;"
	FWatch& Wt = Watch;
	Wt.T += HK::Sign(Wt.Target - Wt.T) * FMath::Min(FMath::Abs(Wt.Target - Wt.T), Dt / .32);
	if (Wt.Target && (Wt.HoldT += Dt) > 6) Wt.Target = 0;
	const bool bShowWatch = Wt.T > .001 && !(MissionActive && MissionActive());
	if (WatchRig)
	{
		WatchRig->Root->bVisible = bShowWatch;
		if (bShowWatch)
		{
			static FQuat RaisedQ, LoweredQ;
			static bool bInit = false;
			if (!bInit)
			{
				const FVector Y = FVector(0, .3, 1).GetSafeNormal();
				FVector X = FVector(.86, .5, -.1);
				X = (X - Y * FVector::DotProduct(X, Y)).GetSafeNormal();
				const FVector Z = FVector::CrossProduct(X, Y);
				FHKMat B;
				B.E[0] = X.X; B.E[1] = X.Y; B.E[2] = X.Z; B.E[4] = Y.X; B.E[5] = Y.Y; B.E[6] = Y.Z; B.E[8] = Z.X; B.E[9] = Z.Y; B.E[10] = Z.Z;
				RaisedQ = B.GetRotation();
				LoweredQ = RaisedQ * HK::EulerToQuat(-.9, 0, .5);
				bInit = true;
			}
			const double E = EaseInOut(Wt.T);
			const FVector RaisedP(-.02, -.05, -.34), LoweredP(-.16, -.46, -.36), P = LoweredP + (RaisedP - LoweredP) * E;
			if (FHKNode* Wrist = WatchRig->Find(TEXT("wrist")))
			{
				Wrist->Position = FVector(P.X * OverlayK, P.Y * OverlayK, P.Z);
				Wrist->SetQuat(HK::Slerp(LoweredQ, RaisedQ, E));
				Wrist->Scale = FVector(OverlayK);
			}
			WatchRig->Root->Position = CamPos;
			WatchRig->Root->SetQuat(CamQuat);
			WatchRig->Root->Scale = FVector(OverlayShrink);
			if (DialRT && Time - Wt.LastDraw > .12) { Wt.LastDraw = Time; DialRT->UpdateResource(); }
		}
	}
	if (PhotoRig)
	{
		const bool bShow = Photo.bActive && Photo.Raised < .5 && !Photo.bGallery && !(MissionActive && MissionActive());
		PhotoRig->Root->bVisible = bShow;
		if (bShow)
		{
			// photo.model.position.set(.08, -.13, -.32); rotation.set(.1, -.15, 0)
			const FVector P(.08 * OverlayK, -.13 * OverlayK, -.32);
			PhotoRig->Root->Position = CamPos + CamQuat.RotateVector(P * OverlayShrink);
			PhotoRig->Root->SetQuat(CamQuat * HK::EulerToQuat(.1, -.15, 0));
			PhotoRig->Root->Scale = FVector(OverlayShrink * OverlayK);
		}
	}
}

// ---------- The part of frame() between the pause check and the mode update ----------
bool AHKGame::ExtrasUpdate(double Dt)
{
	// Text chat box (Enter / T).
	if (bChatOpen && InputProc)
	{
		ChatDraft += InputProc->TypedText;
		InputProc->TypedText.Empty();
		ChatDraft = ChatDraft.Left(200);
		for (const FName& K : InputProc->TextKeys)
		{
			if (K == TEXT("Backspace")) ChatDraft.LeftChopInline(1);
			else if (K == TEXT("Escape")) CloseChat();
			else if (K == TEXT("Enter"))
			{
				const FString T = ChatDraft.TrimStartAndEnd();
				if (!T.IsEmpty())
				{
					const bool bSent = SendChat ? SendChat(T) : false;
					if (!SendChat) ChatLine(TEXT("You"), T, 0, false, true, false);
					if (!bSent && !(IsOnline && IsOnline())) Notice(TEXT("You are offline – only you can see that."), 3);
				}
				CloseChat();
			}
		}
		InputProc->TextKeys.Reset();
		return false;
	}
	if ((Frame.WasPressed("key:Enter") || Frame.WasPressed("key:KeyT")) && !bChatOpen) { OpenChat(); return true; }
	// "Chat: the controller's Share button opens quick chat; keyboard Enter / T opens the text box."
	if (Frame.WasPressed("share") && !Map.bOpen) { bQuick = !bQuick; QuickSel = 0; bQuickStick = false; }
	if (bQuick)
	{
		const double Y = Frame.Move.Y;
		if (Frame.WasPressed("up") || (Y < -.5 && !bQuickStick)) QuickSel = (QuickSel + NQuick - 1) % NQuick;
		if (Frame.WasPressed("down") || (Y > .5 && !bQuickStick)) QuickSel = (QuickSel + 1) % NQuick;
		bQuickStick = FMath::Abs(Y) > .5;
		if (Frame.WasPressed("cross"))
		{
			const int32 i = QuickSel;
			bQuick = false;
			if (i == NQuick - 1) OpenChat();
			else
			{
				const bool bSent = SendChat ? SendChat(Quick[i]) : false;
				if (!SendChat) ChatLine(TEXT("You"), Quick[i], 0, false, true, false);
				if (!bSent && !(IsOnline && IsOnline())) Notice(TEXT("You are offline – nobody else can hear you."), 3);
			}
		}
		else if (Frame.WasPressed("circle")) bQuick = false;
		// "The menu owns the sticks and buttons while it is open."
		Frame.Move = FVector2D::ZeroVector;
		for (const TCHAR* B : {TEXT("cross"), TEXT("circle"), TEXT("up"), TEXT("down"), TEXT("square"), TEXT("triangle")}) Frame.Pressed.Remove(FName(B));
	}
	if (MissionRoute) MissionRoute();
	const bool bMission = MissionActive && MissionActive();
	if (Frame.WasPressed("touchpad") && !bMission) MapToggle();
	if (Map.bOpen)
	{
		if (Frame.WasPressed("circle")) MapToggle();
		Map.Cx += Frame.Move.X * 400 * Dt / Map.Scale;
		Map.Cs += Frame.Move.Y * 400 * Dt / Map.Scale;
		if (Frame.Look.Y) Map.Scale = HK::Clamp(Map.Scale * FMath::Exp(-Frame.Look.Y * Dt * 2), .5, 12);
		if (Frame.Mouse.bLeft) { Map.Cx -= Frame.Mouse.Dx / Map.Scale; Map.Cs -= Frame.Mouse.Dy / Map.Scale; }   // drag pans
		if (Frame.Mouse.Wheel) Map.Scale = HK::Clamp(Map.Scale * FMath::Exp(Frame.Mouse.Wheel * 120 * .0015), .5, 12);
		return true;
	}
	// "△: a tap takes out the photo camera (or puts it away); holding it opens the gear menu: camera or FPV drone."
	if (Player.Mode == TEXT("foot") && !bMission)
	{
		if (Frame.WasPressed("triangle")) { if (Photo.bActive) PhotoToggle(); else { bGear = true; GearT0 = Time; bGearOpen = false; GearSel = GearLast; bGearStick = false; } }
		if (bGear)
		{
			if (Frame.IsHeld("triangle"))
			{
				if (!bGearOpen && Time - GearT0 > .28) { bGearOpen = true; WatchHide(); }
				if (bGearOpen)
				{
					const double Sx = FMath::Abs(Frame.Move.X) > FMath::Abs(Frame.Move.Y) ? Frame.Move.X : Frame.Move.Y;
					if (Frame.WasPressed("left") || Frame.WasPressed("up") || (Sx < -.5 && !bGearStick)) GearSel = 0;
					if (Frame.WasPressed("right") || Frame.WasPressed("down") || (Sx > .5 && !bGearStick)) GearSel = 1;
					bGearStick = FMath::Abs(Sx) > .5;
					if (Frame.Mouse.Dx < -4) GearSel = 0; else if (Frame.Mouse.Dx > 4) GearSel = 1;
					Frame.Move = Frame.Look = FVector2D::ZeroVector;
					Frame.Mouse.Dx = Frame.Mouse.Dy = 0;
				}
			}
			else
			{
				const bool bWasOpen = bGearOpen;
				bGear = false;
				if (bWasOpen) { GearLast = GearSel; if (GearSel == 1) { if (StartDrone) StartDrone(); } else PhotoToggle(); }
				else PhotoToggle();
			}
		}
		if (Frame.WasPressed("up") && !Photo.bActive && !bGear) WatchToggle();
	}
	else if (bGear) bGear = false;
	if (Photo.bActive || Player.Mode != TEXT("foot")) WatchHide();
	if (Photo.bActive)
	{
		if (Frame.WasPressed("square") || Frame.WasPressed("gallery")) PhotoOpenGallery(!Photo.bGallery);
		if (Photo.bGallery)
		{
			const int32 N = Photo.Photos.Num();
			auto Browse = [&](int32 D) { Photo.Index = FMath::Clamp(Photo.Index + D, 0, FMath::Max(0, N - 1)); };
			if (Frame.WasPressed("left")) Browse(-1);
			if (Frame.WasPressed("right")) Browse(1);
			if (Frame.Move.X > .6 && !Photo.bStick) { Browse(1); Photo.bStick = true; }
			else if (Frame.Move.X < -.6 && !Photo.bStick) { Browse(-1); Photo.bStick = true; }
			else if (FMath::Abs(Frame.Move.X) < .3) Photo.bStick = false;
			if (Frame.WasPressed("circle")) PhotoOpenGallery(false);
			return true;
		}
		if (Frame.WasPressed("circle")) PhotoToggle();
		Photo.Raised = Frame.L2 > .4 ? 1 : 0;
		if (Photo.Raised > .5 && Frame.Look.Y) Photo.Zoom = HK::Clamp(Photo.Zoom * FMath::Exp(-Frame.Look.Y * Dt * 1.5), 1, 4);
		if (Frame.R2 > .6 && !Photo.bShutterHeld) { Photo.bShutterHeld = true; PhotoCapture(); return true; }
		if (Frame.R2 < .3) Photo.bShutterHeld = false;
	}
	return false;
}

// ---------- Drawing ----------
namespace
{
	// Ear clipping for the map's filled polygons (OSM rings, local frame).
	TArray<int32> Triangulate(const TArray<FVector2D>& P)
	{
		TArray<int32> Out, V;
		const int32 N = P.Num();
		if (N < 3) return Out;
		double Area = 0;
		for (int32 i = 0; i < N; i++) { const FVector2D& A = P[i]; const FVector2D& B = P[(i + 1) % N]; Area += A.X * B.Y - B.X * A.Y; }
		for (int32 i = 0; i < N; i++) V.Add(Area > 0 ? i : N - 1 - i);
		auto Cross = [](const FVector2D& O, const FVector2D& A, const FVector2D& B) { return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X); };
		int32 Guard = 0;
		while (V.Num() > 3 && Guard++ < 10000)
		{
			bool bClipped = false;
			for (int32 i = 0; i < V.Num(); i++)
			{
				const int32 a = V[(i + V.Num() - 1) % V.Num()], b = V[i], c = V[(i + 1) % V.Num()];
				if (Cross(P[a], P[b], P[c]) <= 0) continue;
				bool bInside = false;
				for (int32 k : V)
				{
					if (k == a || k == b || k == c) continue;
					if (Cross(P[a], P[b], P[k]) >= 0 && Cross(P[b], P[c], P[k]) >= 0 && Cross(P[c], P[a], P[k]) >= 0) { bInside = true; break; }
				}
				if (bInside) continue;
				Out.Append({a, b, c});
				V.RemoveAt(i);
				bClipped = true;
				break;
			}
			if (!bClipped) break;
		}
		if (V.Num() == 3) Out.Append({V[0], V[1], V[2]});
		return Out;
	}
	struct FMapPoly { TArray<FVector2D> P; TArray<int32> Tri; FLinearColor Fill, Stroke; bool bStroke = false; };
	struct FMapRoad { TArray<FVector2D> P; double W = 0; bool bPath = false, bTram = false; int32 Order = 0; };
	struct FMapLabel { FString Name; FVector2D A, B; };
	struct FMapData { bool bReady = false; TArray<FMapPoly> Polys; TArray<FMapRoad> Roads; TArray<FMapLabel> Labels; } GMap;
	FLinearColor Hex(uint32 C) { return FLinearColor(FColor((C >> 16) & 255, (C >> 8) & 255, C & 255)); }
	TArray<FVector2D> PairsOf(const TArray<double>& F) { TArray<FVector2D> O; for (int32 i = 0; i + 1 < F.Num(); i += 2) O.Add(FVector2D(F[i], F[i + 1])); return O; }

	void BuildMap()
	{
		const TSharedPtr<FJsonObject> D = FHKData::Get().D;
		const TMap<FString, uint32> AreaCol = {{TEXT("grass"), 0xb9d39b}, {TEXT("wood"), 0x9cbf83}, {TEXT("playground"), 0xe8d9a8}, {TEXT("pitch"), 0xa8cf8f}, {TEXT("parking"), 0xd6d3cc}, {TEXT("yard"), 0xddd6c4}, {TEXT("flowerbed"), 0xc9d6a0}, {TEXT("water"), 0xa8c8e0}};
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(D, TEXT("areas")))
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FMapPoly M; M.P = PairsOf(HKJson::Nums(O, TEXT("o"))); M.Tri = Triangulate(M.P);
			const uint32* C = AreaCol.Find(HKJson::Str(O, TEXT("k")));
			M.Fill = Hex(C ? *C : 0xdddddd);
			GMap.Polys.Add(M);
		}
		static const TArray<FString> Order = {TEXT("footway"), TEXT("path"), TEXT("cycleway"), TEXT("steps"), TEXT("pedestrian"), TEXT("service"), TEXT("track"), TEXT("living_street"), TEXT("unclassified"), TEXT("residential"), TEXT("tertiary"), TEXT("secondary"), TEXT("primary"), TEXT("trunk_link"), TEXT("trunk"), TEXT("tram")};
		TMap<FString, FMapLabel> Named;
		TMap<FString, double> NamedLen;
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(D, TEXT("roads")))
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			const FString K = HKJson::Str(O, TEXT("k"));
			FMapRoad R; R.P = PairsOf(HKJson::Nums(O, TEXT("p"))); R.W = HKJson::Num(O, TEXT("w"));
			R.bPath = K == TEXT("footway") || K == TEXT("path") || K == TEXT("steps") || K == TEXT("cycleway") || K == TEXT("track") || K == TEXT("pedestrian");
			R.bTram = K == TEXT("tram");
			R.Order = Order.IndexOfByKey(K);
			GMap.Roads.Add(R);
			const FString Name = HKJson::Str(O, TEXT("n"));
			if (!Name.IsEmpty() && K != TEXT("footway") && K != TEXT("path") && K != TEXT("steps") && K != TEXT("cycleway"))
				for (int32 i = 1; i < R.P.Num(); i++)
				{
					const double L = FVector2D::Distance(R.P[i], R.P[i - 1]);
					if (!NamedLen.Contains(Name) || NamedLen[Name] < L) { NamedLen.Add(Name, L); Named.Add(Name, {Name, R.P[i - 1], R.P[i]}); }
				}
		}
		GMap.Roads.StableSort([](const FMapRoad& A, const FMapRoad& B) { return A.Order < B.Order; });
		for (const TPair<FString, FMapLabel>& L : Named) GMap.Labels.Add(L.Value);
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(D, TEXT("buildings")))
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FMapPoly M; M.P = PairsOf(HKJson::Nums(HKJson::Obj(O, TEXT("r")), TEXT("o"))); M.Tri = Triangulate(M.P);
			M.Fill = Hex(HKJson::Str(O, TEXT("s")) == TEXT("brick") ? 0xc9a08a : 0xc8c2b8); M.Stroke = Hex(0xa8826e); M.bStroke = true;
			GMap.Polys.Add(M);
		}
		FMapPoly Own; Own.P = PairsOf(HKJson::Nums(D, TEXT("ownLocal"))); Own.Tri = Triangulate(Own.P); Own.Fill = Hex(0xe0703c); Own.Stroke = Hex(0x7a2e10); Own.bStroke = true;
		GMap.Polys.Add(Own);
		GMap.bReady = true;
	}
}

void AHKGame::DrawExtras(UCanvas* C)
{
	const FVector2D Sz = FHKUi::Size(C);
	const float S = FHKUi::Scale(C);
	// Chat log (#chat, bottom left above the cards): the last 6 lines while recent (12 s) or all while typing.
	ChatRecentT = FMath::Max(0.0, ChatRecentT - FApp::GetDeltaTime());
	if (ChatLog.Num() && (ChatRecentT > 0 || bChatOpen))
	{
		const int32 N = bChatOpen ? FMath::Min(ChatLog.Num(), 14) : FMath::Min(ChatLog.Num(), 6);
		float Y = Sz.Y - 64 - (bChatOpen ? 40 : 0);
		for (int32 i = ChatLog.Num() - 1; i >= ChatLog.Num() - N; i--)
		{
			const FChatLine& L = ChatLog[i];
			const FString Who = Displayable(L.Name) + (L.bSys ? TEXT(" ") : TEXT(": ")), Txt = Displayable(L.Text);
			const float Ww = FHKUi::Measure(C, Who, 15, true).X, Tw = FHKUi::Measure(C, Txt, 15).X;
			Y -= 27;
			FHKUi::Panel(C, 18, Y, FMath::Min(440.f, Ww + Tw + 18), 24, FLinearColor(0, 0, 0, .53f), 7);
			FHKUi::Text(C, Who, 27, Y + 3, 15, SlotCss[FMath::Abs(L.Slot) % 5], EHKAlign::Left, true);
			FHKUi::Text(C, Txt, 27 + Ww, Y + 3, 15, L.bSys ? FLinearColor(FColor(0xcf, 0xd8, 0xdc)) : FHKUi::TextMain);
		}
	}
	if (bChatOpen)
	{
		FHKUi::Panel(C, 18, Sz.Y - 98, 440, 32, FLinearColor(0, 0, 0, .72f), 7);
		const bool bBlink = FMath::Fmod(Time, 1.0) < .5;
		FHKUi::Text(C, ChatDraft.IsEmpty() ? FString(TEXT("Say something… (Enter sends, Esc cancels)")) : ChatDraft + (bBlink ? TEXT("|") : TEXT("")), 28, Sz.Y - 92, 15, ChatDraft.IsEmpty() ? FHKUi::TextDim : FHKUi::TextMain);
	}
	if (bQuick)
	{
		const float W = 300, H = 40 + NQuick * 28 + 26, X = 18, Y = Sz.Y / 2 - H / 2;
		FHKUi::Panel(C, X, Y, W, H, FHKUi::PanelBg, 10);
		FHKUi::Text(C, TEXT("Quick chat"), X + 14, Y + 10, 16, FHKUi::TextMain, EHKAlign::Left, true);
		for (int32 i = 0; i < NQuick; i++)
		{
			if (i == QuickSel) FHKUi::Panel(C, X + 8, Y + 38 + i * 28, W - 16, 26, FLinearColor(FHKUi::Accent.R, FHKUi::Accent.G, FHKUi::Accent.B, .3f), 6);
			FHKUi::Text(C, Displayable(Quick[i]), X + 16, Y + 41 + i * 28, 15);
		}
		FHKUi::Text(C, TEXT("↑↓ choose · ✕ send · ○ close"), X + 14, Y + H - 24, 13, FHKUi::TextDim);
	}
	// Gear menu (#gearMenu): two cards in the middle of the screen.
	if (bGear && bGearOpen)
	{
		const float W = 190, H = 150, Gap = 14, X0 = Sz.X / 2 - W - Gap / 2, Y = Sz.Y / 2 - H / 2 - 16;
		const TCHAR* Title[2] = {TEXT("Camera"), TEXT("FPV drone")};
		const TCHAR* Sub[2] = {TEXT("Take photos · gallery"), TEXT("Fly it in first person")};
		for (int32 i = 0; i < 2; i++)
		{
			const float X = X0 + i * (W + Gap);
			const bool bSel = i == GearSel;
			FHKUi::Panel(C, X, Y, W, H, bSel ? FLinearColor(FColor(0x1d, 0x26, 0x33, 0xe6)) : FLinearColor(FColor(0x0b, 0x0f, 0x14, 0xd9)), 14);
			FHKUi::Frame(C, X, Y, W, H, 2, bSel ? FLinearColor(FColor(0xff, 0xd1, 0x66)) : FLinearColor(1, 1, 1, .13f));
			const FLinearColor Ic = bSel ? FLinearColor::White : FLinearColor(FColor(0xdf, 0xe6, 0xec));
			const FVector2D Cc(X + W / 2, Y + 46);
			if (i == 0)
			{
				FHKUi::Frame(C, Cc.X - 22, Cc.Y - 14, 44, 32, 3, Ic);
				FHKUi::Circle(C, FVector2D(Cc.X, Cc.Y + 2), 9, 3, Ic, 24);
				FHKUi::Rect(C, Cc.X - 8, Cc.Y - 20, 16, 6, Ic);
			}
			else
			{
				for (const FVector2D& O : {FVector2D(-14, -14), FVector2D(14, -14), FVector2D(-14, 14), FVector2D(14, 14)}) FHKUi::Circle(C, Cc + O, 8, 3, Ic, 16);
				FHKUi::Line(C, Cc + FVector2D(-9, -9), Cc + FVector2D(9, 9), 3, Ic);
				FHKUi::Line(C, Cc + FVector2D(9, -9), Cc + FVector2D(-9, 9), 3, Ic);
				FHKUi::Rect(C, Cc.X - 5, Cc.Y - 5, 10, 10, Ic);
			}
			FHKUi::Text(C, Title[i], X + W / 2, Y + 82, 17, Ic, EHKAlign::Center, true);
			FHKUi::Text(C, Sub[i], X + W / 2, Y + 108, 13, FLinearColor(Ic.R, Ic.G, Ic.B, .7f), EHKAlign::Center);
		}
		FHKUi::Text(C, TEXT("Keep holding △ · ←/→ (or ↑/↓) to choose · release to take it out"), Sz.X / 2, Y + H + 10, 13, FHKUi::TextMain, EHKAlign::Center);
	}
	// Photo camera: viewfinder, flash, preview, gallery, help.
	if (Photo.bActive)
	{
		if (Photo.bGallery)
		{
			FHKUi::Rect(C, 0, 0, Sz.X, Sz.Y, FLinearColor(0, 0, 0, .92f));
			if (!Photo.Photos.Num()) FHKUi::Text(C, TEXT("No photos yet"), Sz.X / 2, Sz.Y / 2, 20, FHKUi::TextMain, EHKAlign::Center);
			else
			{
				Photo.Index = FMath::Clamp(Photo.Index, 0, Photo.Photos.Num() - 1);
				const FPhoto& P = Photo.Photos[Photo.Index];
				if (GalleryTexFile != P.File) { GalleryTex = FImageUtils::ImportFileAsTexture2D(P.File); GalleryTexFile = P.File; }
				if (GalleryTex)
				{
					const float Aspect = GalleryTex->GetSizeX() / FMath::Max(1.f, (float)GalleryTex->GetSizeY());
					float H = Sz.Y - 140, W = H * Aspect;
					if (W > Sz.X - 80) { W = Sz.X - 80; H = W / Aspect; }
					FCanvasTileItem Img(FVector2D((Sz.X / 2 - W / 2) * S, (Sz.Y / 2 - H / 2 - 20) * S), GalleryTex->GetResource(), FVector2D(W * S, H * S), FLinearColor::White);
					C->DrawItem(Img);
				}
				const FDateTime Dt = FDateTime::FromUnixTimestamp(P.Id / 1000) + (FDateTime::Now() - FDateTime::UtcNow());
				FHKUi::Text(C, FString::Printf(TEXT("%d / %d"), Photo.Index + 1, Photo.Photos.Num()), 40, Sz.Y - 56, 17, FHKUi::TextMain, EHKAlign::Left, true);
				FHKUi::Text(C, Dt.ToString(TEXT("%d.%m.%Y, %H:%M:%S")) + (P.Place.IsEmpty() ? FString() : TEXT(" · ") + P.Place), Sz.X / 2, Sz.Y - 56, 16, FHKUi::TextDim, EHKAlign::Center);
				FHKUi::Text(C, TEXT("←/→ browse · ○ close"), Sz.X - 40, Sz.Y - 56, 15, FHKUi::TextDim, EHKAlign::Right);
			}
		}
		else
		{
			if (Photo.Raised > .5)
			{
				const float M = 60, L = 40;
				for (const FVector2D& Cn : {FVector2D(M, M), FVector2D(Sz.X - M, M), FVector2D(M, Sz.Y - M), FVector2D(Sz.X - M, Sz.Y - M)})
				{
					const float Dx = Cn.X < Sz.X / 2 ? 1 : -1, Dy = Cn.Y < Sz.Y / 2 ? 1 : -1;
					FHKUi::Line(C, Cn, Cn + FVector2D(Dx * L, 0), 3, FLinearColor(1, 1, 1, .85f));
					FHKUi::Line(C, Cn, Cn + FVector2D(0, Dy * L), 3, FLinearColor(1, 1, 1, .85f));
				}
				FHKUi::Frame(C, Sz.X / 2 - 22, Sz.Y / 2 - 16, 44, 32, 2, FLinearColor(1, 1, 1, .7f));
				FHKUi::Text(C, FString::Printf(TEXT("%d photo%s · %.1f×"), Photo.Photos.Num(), Photo.Photos.Num() == 1 ? TEXT("") : TEXT("s"), Photo.Zoom), Sz.X - M - 6, M + 10, 16, FHKUi::TextMain, EHKAlign::Right, true);
			}
			FHKUi::Text(C, TEXT("R2 / left mouse: take photo · L2 / right mouse: viewfinder · right stick up/down: zoom · □ / G: gallery · △ / F: put away"), Sz.X / 2, Sz.Y - 40, 14, FHKUi::TextMain, EHKAlign::Center);
		}
		if (Photo.FlashT > 0) { FHKUi::Rect(C, 0, 0, Sz.X, Sz.Y, FLinearColor(1, 1, 1, FMath::Min(1.0, Photo.FlashT / .35) * .85f)); Photo.FlashT -= FApp::GetDeltaTime(); }
	}
	if (Photo.PreviewT > 0 && Photo.Photos.Num() && !Photo.bGallery)
	{
		Photo.PreviewT -= FApp::GetDeltaTime();
		const FPhoto& P = Photo.Photos.Last();
		if (GalleryTexFile != P.File) { GalleryTex = FImageUtils::ImportFileAsTexture2D(P.File); GalleryTexFile = P.File; }
		if (GalleryTex)
		{
			const float W = 260, H = W * GalleryTex->GetSizeY() / FMath::Max(1.f, (float)GalleryTex->GetSizeX());
			FHKUi::Rect(C, Sz.X - W - 26, 22, W + 8, H + 8, FLinearColor(1, 1, 1, .9f));
			FCanvasTileItem Img(FVector2D((Sz.X - W - 22) * S, 26 * S), GalleryTex->GetResource(), FVector2D(W * S, H * S), FLinearColor::White);
			C->DrawItem(Img);
		}
	}
	// Map overlay (#mapOverlay).
	if (Map.bOpen)
	{
		if (!GMap.bReady) BuildMap();
		FHKUi::Rect(C, 0, 0, Sz.X, Sz.Y, Hex(0xe8e4d8));
		const double K = Map.Scale;
		auto P = [&](const FVector2D& L) { return FVector2D(Sz.X / 2 + (L.X - Map.Cx) * K, Sz.Y / 2 + (L.Y - Map.Cs) * K); };
		auto OnScreen = [&](const TArray<FVector2D>& Pts) { for (const FVector2D& L : Pts) { const FVector2D Q = P(L); if (Q.X > -200 && Q.X < Sz.X + 200 && Q.Y > -200 && Q.Y < Sz.Y + 200) return true; } return false; };
		TArray<FCanvasUVTri> Tris;
		auto Flush = [&]() { if (!Tris.Num()) return; FCanvasTriangleItem I(Tris, GWhiteTexture); C->DrawItem(I); Tris.Reset(); };
		auto Poly = [&](const FMapPoly& M)
		{
			if (!OnScreen(M.P)) return;
			for (int32 i = 0; i + 2 < M.Tri.Num(); i += 3)
			{
				FCanvasUVTri T;
				T.V0_Pos = P(M.P[M.Tri[i]]) * S; T.V1_Pos = P(M.P[M.Tri[i + 1]]) * S; T.V2_Pos = P(M.P[M.Tri[i + 2]]) * S;
				T.V0_Color = T.V1_Color = T.V2_Color = M.Fill;
				Tris.Add(T);
			}
		};
		const int32 NAreas = HKJson::Arr(FHKData::Get().D, TEXT("areas")).Num();
		for (int32 i = 0; i < NAreas && i < GMap.Polys.Num(); i++) Poly(GMap.Polys[i]);
		Flush();
		for (const FMapRoad& R : GMap.Roads)
		{
			if (!OnScreen(R.P)) continue;
			const FLinearColor Col = R.bPath ? Hex(0xc9a77b) : R.bTram ? Hex(0x888888) : FLinearColor::White;
			const float Wd = FMath::Max(1.0, (R.bPath ? 1.4 : R.W) * K * .9);
			for (int32 i = 1; i < R.P.Num(); i++)
			{
				const FVector2D A = P(R.P[i - 1]), B = P(R.P[i]);
				if (R.bPath)
				{
					// setLineDash([4, 3])
					const double L = FVector2D::Distance(A, B);
					for (double d = 0; d < L; d += 7) FHKUi::Line(C, A + (B - A) * (d / L), A + (B - A) * (FMath::Min(L, d + 4) / L), Wd, Col);
				}
				else FHKUi::Line(C, A, B, Wd, Col);
				if (!R.bPath && Wd > 3) FHKUi::Disc(C, B, Wd / 2, Col, 10);
			}
		}
		for (int32 i = NAreas; i < GMap.Polys.Num(); i++) Poly(GMap.Polys[i]);
		Flush();
		for (int32 i = NAreas; i < GMap.Polys.Num(); i++)
		{
			const FMapPoly& M = GMap.Polys[i];
			if (!M.bStroke || !OnScreen(M.P)) continue;
			for (int32 k = 0; k < M.P.Num(); k++) FHKUi::Line(C, P(M.P[k]), P(M.P[(k + 1) % M.P.Num()]), 1, M.Stroke);
		}
		// Street names (one label per named street, at its longest segment; drawn level).
		for (const FMapLabel& L : GMap.Labels)
		{
			const FVector2D A = P(L.A), B = P(L.B);
			if (FVector2D::Distance(A, B) < FHKUi::Measure(C, L.Name, 12).X * .6) continue;
			FHKUi::Text(C, L.Name, (A.X + B.X) / 2, (A.Y + B.Y) / 2 - 10, 12, Hex(0x4a4a4a), EHKAlign::Center, false, false);
		}
		// Points of interest.
		FHKData& Data = FHKData::Get();
		auto Icon = [&](const FVector2D& Local, const FLinearColor& Col, const FString& Label, const FString& Glyph)
		{
			const FVector2D Q = P(Local);
			FHKUi::Disc(C, Q, 11, Col, 20);
			FHKUi::Circle(C, Q, 11, 2, FLinearColor::White, 20);
			FHKUi::Text(C, Glyph, Q.X, Q.Y - 8, 12, FLinearColor::White, EHKAlign::Center, true, false);
			if (!Label.IsEmpty()) FHKUi::Text(C, Label, Q.X, Q.Y - 11 - 5 - 16, 12, Hex(0x222222), EHKAlign::Center, true, false);
		};
		const TArray<double> Ms = HKJson::Nums(HKJson::Obj(Data.Game, TEXT("mission")), TEXT("start"));
		Icon(Data.ToLocal(2.8, 5), Hex(0xe0703c), TEXT("Home · 4th floor"), TEXT("H"));
		Icon(Data.ToLocal(4.8, 14.2), Hex(0xb71c1c), TEXT("Hostage rescue"), TEXT("◎"));
		int32 Hi = 0;
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(Data.D, TEXT("discgolf")))
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			const TArray<double> T = HKJson::Nums(O, TEXT("tee")), B = HKJson::Nums(O, TEXT("basket"));
			Icon(FVector2D(T[0], T[1]), Hex(0x6a1b9a), FString::Printf(TEXT("Disc golf %d"), ++Hi), TEXT("●"));
			Icon(FVector2D(B[0], B[1]), Hex(0x9c4dcc), FString(), TEXT("○"));
		}
		const TSharedPtr<FJsonObject> Pq = HKJson::Obj(Data.D, TEXT("petanque"));
		if (Pq) Icon(FVector2D(HKJson::Num(Pq, TEXT("x")), HKJson::Num(Pq, TEXT("s"))), Hex(0x00838f), TEXT("Petanque"), TEXT("●"));
		Icon(Data.ToLocal(26, 14), Hex(0x1565c0), TEXT("Helipad"), TEXT("H"));
		Icon(Data.ToLocal(-116.5, 0), Hex(0x1565c0), TEXT("Airstrip"), TEXT("A"));
		Icon(Data.ToLocal(FirePos.X, FirePos.Y), Hex(0xe65100), TEXT("Firepit"), TEXT("F"));
		if (RemotePoi) for (const TTuple<FVector2D, FLinearColor, FString>& R : RemotePoi()) Icon(Data.ToLocal(R.Get<0>().X, R.Get<0>().Y), R.Get<1>(), R.Get<2>(), TEXT("●"));
		// You: an arrow along headingLocal = -(yaw - theta) + PI.
		{
			const FVector2D Q = P(Data.ToLocal(Player.X, Player.Z));
			const double A = -(Player.Yaw - Data.Theta) + PI, R = 9;
			auto Rot = [&](double X, double Y) { return Q + FVector2D(X * FMath::Cos(A) - Y * FMath::Sin(A), X * FMath::Sin(A) + Y * FMath::Cos(A)); };
			TArray<FCanvasUVTri> Arrow;
			const FVector2D Tip = Rot(0, -R * 1.4), Lr = Rot(R, R), Mid = Rot(0, R * .45), Ll = Rot(-R, R);
			for (const TPair<FVector2D, FVector2D>& E : {TPair<FVector2D, FVector2D>(Tip, Lr), TPair<FVector2D, FVector2D>(Lr, Mid), TPair<FVector2D, FVector2D>(Mid, Ll), TPair<FVector2D, FVector2D>(Ll, Tip)}) FHKUi::Line(C, E.Key, E.Value, 2, FLinearColor::White);
			FCanvasUVTri T1, T2;
			T1.V0_Pos = Tip * S; T1.V1_Pos = Lr * S; T1.V2_Pos = Mid * S;
			T2.V0_Pos = Tip * S; T2.V1_Pos = Mid * S; T2.V2_Pos = Ll * S;
			T1.V0_Color = T1.V1_Color = T1.V2_Color = T2.V0_Color = T2.V1_Color = T2.V2_Color = Hex(0x1565c0);
			Arrow.Add(T1); Arrow.Add(T2);
			FCanvasTriangleItem I(Arrow, GWhiteTexture);
			C->DrawItem(I);
		}
		// North arrow and scale bar.
		FHKUi::Text(C, TEXT("N ↑"), Sz.X - 40, 20, 14, Hex(0x222222), EHKAlign::Center, true, false);
		const float Bar = 100 * K;
		FHKUi::Rect(C, 20, Sz.Y - 30, Bar, 4, Hex(0x222222));
		FHKUi::Text(C, TEXT("100 m"), 20 + Bar / 2, Sz.Y - 54, 14, Hex(0x222222), EHKAlign::Center, true, false);
		// Legend.
		const float Lx = 20, Ly = 20;
		FHKUi::Panel(C, Lx, Ly, 330, 196, FLinearColor(1, 1, 1, .9f), 10);
		FHKUi::Text(C, TEXT("Map"), Lx + 14, Ly + 10, 16, Hex(0x222222), EHKAlign::Left, true, false);
		const TPair<FLinearColor, const TCHAR*> Leg[] = {{Hex(0xe0703c), TEXT("Your apartment (Haarklous plass 17A)")}, {Hex(0xb71c1c), TEXT("Hostage rescue")}, {Hex(0x6a1b9a), TEXT("Disc golf")}, {Hex(0x00838f), TEXT("Petanque")}, {Hex(0x1565c0), TEXT("You")}};
		for (int32 i = 0; i < 5; i++) { FHKUi::Disc(C, FVector2D(Lx + 22, Ly + 46 + i * 22), 6, Leg[i].Key, 12); FHKUi::Text(C, Leg[i].Value, Lx + 36, Ly + 37 + i * 22, 13, Hex(0x222222), EHKAlign::Left, false, false); }
		FHKUi::Text(C, TEXT("Left stick / drag: pan · Right stick / wheel: zoom · ○ / M: close"), Lx + 14, Ly + 150, 11, Hex(0x444444), EHKAlign::Left, false, false);
		FHKUi::Text(C, TEXT("© OpenStreetMap contributors"), Lx + 14, Ly + 170, 11, Hex(0x444444), EHKAlign::Left, false, false);
	}
}
