// Scripted play-throughs (run with -hktest=<name>). Positions are in the web frame; buttons use the standard mapping:
// 0 cross, 1 circle, 2 square, 3 triangle, 4 L1, 5 R1, 6 L2, 7 R2, 8 share, 9 options, 10 L3, 11 R3, 12-15 D-pad, 16 PS, 17 touchpad.
#include "HKTestDriver.h"
#include "HKData.h"
#include "HKGame.h"
#include "HKInput.h"
#include "HKMath.h"

namespace
{
	// A camera standing H metres above the ground at (x, z), looking along yaw (web yaw, 0 = toward -z) and pitch.
	void Ground(AHKTestDriver& T, double X, double Z, double H, double Yaw, double Pitch)
	{
		T.Pose(X, FHKData::Get().HeightWorld(X, Z) + H, Z, Yaw, Pitch);
	}
	// Yaw that looks from (x0, z0) toward (x1, z1).
	double Toward(double X0, double Z0, double X1, double Z1) { return FMath::Atan2(-(X1 - X0), -(Z1 - Z0)); }

	struct FRegister
	{
		FRegister()
		{
			// Views of the whole world for checking the art (no gameplay).
			FHKTestScripts::All().Add(TEXT("shots"), [](AHKTestDriver& T)
			{
				T.Wait(3);
				T.Shot(TEXT("00-start-screen"));
				T.Press(0);
				T.Wait(1.5);
				T.Shot(TEXT("01-player-start"));
				Ground(T, 6, 24, 1.7, Toward(6, 24, 1, 6), .05); T.Shot(TEXT("02-front-of-17A"));
				Ground(T, -12, 44, 1.7, Toward(-12, 44, -2.7, 34.7), -.1); T.Shot(TEXT("03-firepit"));
				Ground(T, 20, 30, 1.7, Toward(20, 30, 26, 14), -.05); T.Shot(TEXT("04-helipad"));
				T.Pose(3.2, 1.62, 9.5, Toward(3.2, 9.5, 3, 3), -.1); T.Shot(TEXT("05-apartment-a"));
				T.Pose(4.5, 1.62, 3.0, Toward(4.5, 3, 2, 8), -.15); T.Shot(TEXT("06-apartment-b"));
				T.Pose(0.2, -8.3, 8.0, HK::Pi, .25); T.Shot(TEXT("07-stairwell"));
				T.Pose(40, 70, 90, Toward(40, 90, 0, 0), -.55); T.Shot(TEXT("08-aerial-home"));
				T.Pose(-60, 120, 120, Toward(-60, 120, 60, -60), -.45); T.Shot(TEXT("09-aerial-district"));
				Ground(T, -116.5, 0, 1.7, .5236, 0); T.Shot(TEXT("10-airstrip"));
				T.FreeCamera();
			});

			// Frame rate in the busiest places (run at the real resolution, e.g. -Res 1920x1080 -Extra "-fullscreen").
			FHKTestScripts::All().Add(TEXT("fps"), [](AHKTestDriver& T)
			{
				T.Wait(3);
				T.Press(0);
				T.Wait(4);
				auto Sweep = [&T](const FString& Label, double X, double Y, double Z, double Pitch)
				{
					// Look around slowly (one full turn over the measurement) so every direction counts.
					TSharedPtr<double> Yaw = MakeShared<double>(0);
					T.Steps.Add([&T, Yaw, X, Y, Z, Pitch](double Dt)
					{
						AHKGame* G = T.Game();
						*Yaw += Dt * 2 * HK::Pi / 12;
						G->bCameraOverride = true;
						G->OverridePos = FVector(X, Y, Z);
						G->OverrideQuat = HK::EulerToQuat(Pitch, *Yaw, 0, TEXT("YXZ"));
						return *Yaw >= 2 * HK::Pi / 12 * 1.2;   // 1.2 s to settle
					});
					T.Shot(TEXT("fps-") + Label);
					// Measure 12 s while turning one full circle.
					struct FAcc { double T = 0; TArray<double> Frames; };
					TSharedPtr<FAcc> A = MakeShared<FAcc>();
					T.Steps.Add([&T, Yaw, A, Label, Pitch](double Dt)
					{
						AHKGame* G = T.Game();
						*Yaw += Dt * 2 * HK::Pi / 12;
						G->OverrideQuat = HK::EulerToQuat(Pitch, *Yaw, 0, TEXT("YXZ"));
						A->T += Dt; A->Frames.Add(Dt);
						if (A->T < 12) return false;
						TArray<double> F = A->Frames;
						F.Sort();
						const double Avg = F.Num() / A->T, Low = 1 / F[FMath::Min(F.Num() - 1, (int32)(F.Num() * .99))], Worst = 1 / F.Last();
						UE_LOG(LogTemp, Display, TEXT("HK-TEST fps %s: average %.1f, 1%% low %.1f, slowest frame %.1f fps (%d frames, %.1f s)"), *Label, Avg, Low, Worst, F.Num(), A->T);
						return true;
					});
				};
				Sweep(TEXT("home"), 3.2, 1.62, 6.5, -.05);
				Sweep(TEXT("firepit"), -6, FHKData::Get().HeightWorld(-6, 38) + 1.65, 38, -.05);
				Sweep(TEXT("park"), FHKData::Get().ToWorld(110, 40).X, FHKData::Get().HeightWorld(FHKData::Get().ToWorld(110, 40).X, FHKData::Get().ToWorld(110, 40).Y) + 1.65, FHKData::Get().ToWorld(110, 40).Y, 0);
				Sweep(TEXT("street"), 2, -8.3, 22, 0);
				Sweep(TEXT("flying"), 30, 110, 60, -.35);
				T.FreeCamera();
			});

			// On foot: street door -> stairs -> 4th floor -> entrance door -> the flat -> a beer (the web's verified route).
			FHKTestScripts::All().Add(TEXT("walkhome"), [](AHKTestDriver& T)
			{
				T.Wait(2);
				T.Press(0);
				T.Wait(1);
				T.Log([&T]() { return TEXT("start ") + T.P(); });
				T.WalkTo(0.75, 12.4);
				T.Do([&T]() { T.Game()->Player.Yaw = 0; T.Game()->Player.Pitch = -.1; });
				T.Wait(.4);
				T.Expect(TEXT("prompt at the street door"), [&T]() { return T.Game()->LastPromptText().Contains(TEXT("street door")); });
				T.Log([&T]() { return TEXT("prompt: ") + T.Game()->LastPromptText(); });
				T.Shot(TEXT("01-street-door"));
				T.Press(2);
				T.Wait(1.2);
				T.Expect(TEXT("street door open"), [&T]() { const FHKDoor* D = T.Game()->Doors.FindByPredicate([](const FHKDoor& X) { return X.Id == TEXT("own-street-door"); }); return D && D->bOpen; });
				T.Shot(TEXT("02-door-open"));
				T.WalkTo(0.75, 10.2);
				// Four storeys: west flight up to each landing, east flight up to each half landing.
				for (int32 n = 1; n <= 4; n++)
				{
					T.WalkTo(-0.62, 9.4); T.WalkTo(-0.62, 6.4);
					if (n < 4) { T.WalkTo(0.82, 6.6); T.WalkTo(0.82, 9.6); }
					T.Log([&T, n]() { return FString::Printf(TEXT("landing %d %s"), n, *T.P()); });
				}
				T.Expect(TEXT("on the 4th floor landing (feet ~0)"), [&T]() { return FMath::Abs(T.Game()->Player.Feet) < .05; });
				T.WalkTo(0.25, 5.83);
				T.Do([&T]() { T.Game()->Player.Yaw = -HK::Pi / 2; T.Game()->Player.Pitch = -.05; });
				T.Wait(.4);
				T.Shot(TEXT("03-entrance-door"));
				T.Log([&T]() { return TEXT("prompt: ") + T.Game()->LastPromptText(); });
				T.Press(2);
				T.Wait(1.2);
				T.WalkTo(2.6, 5.8);
				T.Expect(TEXT("in the apartment"), [&T]() { return T.Game()->InApartment(); });
				T.Shot(TEXT("04-hall"));
				// A beer from the kitchen worktop: walk near it, look at it, press square.
				T.Do([&T]()
				{
					AHKGame* G = T.Game();
					const FVector B = G->Drinks[0].Object ? G->Drinks[0].Object->GetWorldPosition() : FVector::ZeroVector;
					UE_LOG(LogTemp, Display, TEXT("HK-TEST beer at (%.2f, %.2f, %.2f)"), B.X, B.Y, B.Z);
				});
				T.Steps.Add([&T](double)
				{
					AHKGame* G = T.Game();
					const FVector B = G->Drinks[0].Object->GetWorldPosition();
					const FVector2D Dir = FVector2D(B.X - G->Player.X, B.Z - G->Player.Z);
					const double D = Dir.Size();
					if (D > .9) { G->Player.Yaw = FMath::Atan2(-Dir.X, -Dir.Y); if (TSharedPtr<FHKInputProcessor> In = FHKInputProcessor::Get()) In->SimulatedPad.Axes[1] = -1; return false; }
					if (TSharedPtr<FHKInputProcessor> In = FHKInputProcessor::Get()) In->SimulatedPad.Axes[1] = 0;
					return true;
				});
				T.Do([&T]()
				{
					AHKGame* G = T.Game();
					const FVector B = G->Drinks[0].Object->GetWorldPosition() + FVector(0, .1, 0);
					const FVector E = FVector(G->Player.X, G->Player.Feet + G->Player.EyeNow, G->Player.Z);
					G->Player.Yaw = FMath::Atan2(-(B.X - E.X), -(B.Z - E.Z));
					G->Player.Pitch = FMath::Atan2(B.Y - E.Y, HK::Hypot(B.X - E.X, B.Z - E.Z));
				});
				T.Wait(.5);
				T.Log([&T]() { return TEXT("prompt: ") + T.Game()->LastPromptText(); });
				T.Shot(TEXT("05-beer"));
				T.Press(2);
				T.Wait(.5);
				T.Expect(TEXT("drunk 30 s after a beer"), [&T]() { return T.Game()->Drunk > 29 && T.Game()->Drunk <= 30; });
				T.Wait(3);
				T.Shot(TEXT("06-drunk"));
			});
		}
	} GRegister;
}
