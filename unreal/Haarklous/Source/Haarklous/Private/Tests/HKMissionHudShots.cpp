// Haarklous.Mission.HudShots: draws the mission HUD and its panels into a 1920 x 1080 render target and saves PNGs to
// tools/out/screens/mission-*.png, to look at the layout next to the web version. Needs a renderer: under -nullrhi
// (tools/run-tests.ps1) it only reports that it was skipped. Run it with rendering:
//   UnrealEditor-Cmd.exe <project> -ExecCmds="Automation RunTests Haarklous.Mission.HudShots;Quit" -unattended -nosplash
#include "Misc/AutomationTest.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HKData.h"
#include "HKMission.h"
#include "HKUi.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "TextureResource.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKMissionHudShotsTest, "Haarklous.Mission.HudShots", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKMissionHudShotsTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender() || !GEngine)
	{
		AddInfo(TEXT("No renderer (-nullrhi): HUD screenshots skipped."));
		return true;
	}
	FHKData& Data = FHKData::Get();
	if (!TestTrue(TEXT("data loaded"), Data.bLoaded)) return false;
	FHKWalkWorld& W = Data.WalkWorld;
	const TArray<FHKObstacle> SavedDynamic = W.Dynamic;
	FHKMissionContext Ctx;
	Ctx.WalkWorld = [&W]() { return &W; };
	Ctx.Doors = []() { return HKMissionData::Doors(); };
	Ctx.SpawnAreas = []() { return HKMissionData::SpawnAreas(); };
	Ctx.StairSpawns = []() { return HKMissionData::StairSpawns(); };
	Ctx.Entrance = []() { return HKMissionData::Entrance(); };
	Ctx.MissionStart = []() { return HKMissionData::MissionStart(); };
	FVector CamPos(1, 1.65, 3);
	double Fov = 70, Pad = 1, Mouse = 1.35;
	Ctx.CameraPosition = [&CamPos]() { return CamPos; };
	Ctx.CameraQuaternion = []() { return FQuat::Identity; };
	Ctx.CameraAspect = []() { return 16.0 / 9; };
	Ctx.GetFov = [&Fov]() { return Fov; };
	Ctx.SetFov = [&Fov](double V) { Fov = V; };
	Ctx.GetSensitivity = [&](const FString& K) { return K == TEXT("pad") ? Pad : Mouse; };
	HKCase::FRng Rng(5);
	Ctx.Random = [&Rng]() { return Rng(); };
	FHKMission M(Ctx);

	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
	RT->RenderTargetFormat = RTF_RGBA8;
	RT->ClearColor = FLinearColor(0.16f, 0.18f, 0.2f, 1);
	RT->InitAutoFormat(1920, 1080);
	RT->UpdateResourceImmediate(true);
	const FString Dir = FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT(".."), TEXT("tools"), TEXT("out"), TEXT("screens"));
	auto Shot = [&](const FString& Name)
	{
		FTextureRenderTargetResource* Res = RT->GameThread_GetRenderTargetResource();
		{
			FCanvas Canvas(Res, nullptr, nullptr, GMaxRHIFeatureLevel);
			Canvas.Clear(FLinearColor(0.16f, 0.18f, 0.2f, 1));
			UCanvas* C = NewObject<UCanvas>();
			C->Init(1920, 1080, nullptr, &Canvas);
			C->Update();
			// A few light and dark bands behind, so the translucent panels read as they do over the 3D view.
			for (int32 i = 0; i < 8; i++) FHKUi::Rect(C, i * 240, 0, 120, 1080, FLinearColor(.45f, .5f, .55f, 1));
			M.DrawHud(C);
			Canvas.Flush_GameThread(true);
		}
		FlushRenderingCommands();
		TArray<FColor> Px;
		Res->ReadPixels(Px);
		for (FColor& P : Px) P.A = 255;
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(1920, 1080, Px, Png);
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(Dir, TEXT("mission-") + Name + TEXT(".png")));
		TestTrue(TEXT("saved ") + Path, Px.Num() == 1920 * 1080 && FFileHelper::SaveArrayToFile(Png, *Path));
		AddInfo(TEXT("HUD screenshot: ") + Path);
	};
	M.Start(false);
	M.Tick(1.0 / 64);
	Shot(TEXT("briefing"));
	M.PadButton(TEXT("cross"));
	for (int32 i = 0; i < 20; i++) M.Tick(1.0 / 64);
	M.Say(TEXT("Hostile down · headshot"));
	M.FeedKind = TEXT("ok");
	M.HitMarkerT = .25;
	M.Recoil = .6;
	M.Tick(1.0 / 64);
	Shot(TEXT("play"));
	M.PlayerHealth = 25;
	M.DamageT = .4;
	M.DamageDirT = 1;
	M.bHasDamageFrom = true;
	M.DamageFrom = FVector2D(3, 3);
	M.Mouse(2, true);
	for (int32 i = 0; i < 30; i++) M.Tick(1.0 / 64);
	M.Say(TEXT("You are hit · health 25"));
	M.FeedKind = TEXT("fail");
	M.Tick(1.0 / 64);
	Shot(TEXT("hit-aiming"));
	M.Pause();
	Shot(TEXT("paused"));
	M.DebugFinish(TEXT("success"));
	Shot(TEXT("success"));
	M.DebugFinish(TEXT("fail"));
	Shot(TEXT("fail"));
	M.Stop();
	W.SetDynamic(SavedDynamic);
	return true;
}

#endif
