// Scripted play-through "look" (-hktest=look, 2 Nov 2026): the realism pass the user asked for.
//   - Flicker: buildings that twitched (two copies of a wall in one place). Each view is shot three times with the
//     camera turned by 0.1 degree and moved 3 cm: surfaces that fight show different noise in the three shots.
//   - The streets around Haarklous plass from Google Street View's own spots, to compare with the real photos.
//   - The real cars (car_model.py, a VW Golf Mk7-style hatchback; car_id3.py, a VW ID.3 for the silver, white and blue
//     cars): the first Golf and the first ID.3 from the front, side, back, close to a headlight and a wheel, and the
//     view from the driver's seat.
#include "HKTestDriver.h"
#include "HKData.h"
#include "HKGame.h"
#include "HKMath.h"
#include "HKShops.h"
#include "HKVehicle.h"
#include "HKVehicles.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/UObjectIterator.h"

namespace
{
	double Toward3(double X0, double Z0, double X1, double Z1) { return FMath::Atan2(-(X1 - X0), -(Z1 - Z0)); }

	// Fixed camera at (x, y, z) looking at the point (tx, ty, tz), all in the web frame.
	void LookFrom(AHKTestDriver& T, double X, double Y, double Z, double Tx, double Ty, double Tz, double DYaw = 0)
	{
		const double Yaw = Toward3(X, Z, Tx, Tz) + DYaw;
		const double Pitch = FMath::Atan2(Ty - Y, FVector2D(Tx - X, Tz - Z).Size());
		T.Pose(X, Y, Z, Yaw, Pitch);
	}

	// Three shots of one view, turned by 0.1 degree and moved 3 cm in between.
	void Triple(AHKTestDriver& T, const FString& Name, double X, double H, double Z, double Tx, double Th, double Tz)
	{
		FHKData& D = FHKData::Get();
		const double Y = D.HeightWorld(X, Z) + H, Ty = D.HeightWorld(Tx, Tz) + Th;
		for (int32 k = 0; k < 3; k++)
		{
			LookFrom(T, X + k * .03, Y, Z, Tx, Ty, Tz, k * .00175);
			T.Wait(k == 0 ? 1.5 : .4);
			T.Shot(FString::Printf(TEXT("%s-%c"), *Name, TCHAR('a' + k)));
		}
	}

	struct FCarView { const TCHAR* Name; double F, R, H, Tf, Tr, Th; };

	struct FRegisterLook
	{
		FRegisterLook()
		{
			FHKTestScripts::All().Add(TEXT("look"), [](AHKTestDriver& T)
			{
				T.Wait(3);
				T.Press(0);
				T.Wait(2);
				T.WaitShaders();
				// Street parking (-HKParkedCars, HKParking.h): a parked car is solid to walk into.
				T.Expect(TEXT("parked cars block walking"), [&T]()
				{
					const AHKGame* G = T.Game();
					const FHKData& D = FHKData::Get();
					for (int32 i = 0; i < FMath::Min(10, G->ParkedCars.Num()); i++)
						if (D.WalkWorld.CanStand(FHKPt(G->ParkedCars[i]->X, G->ParkedCars[i]->Z), 1.6, G->ParkedCars[i]->Y)) return false;
					return true;
				});
				// ---- Flicker ----
				Triple(T, TEXT("f1-front-of-17A"), 6, 1.7, 24, 1, 8, 6);
				Triple(T, TEXT("f2-street-view-spot"), -31.42, 2.2, -2.52, -21.78, 5, .15);
				Triple(T, TEXT("f3-street-west"), -224.5, 1.7, -38, -230, 6, -54.5);
				Triple(T, TEXT("f4-across-the-park"), -12, 1.7, 44, 60, 8, 120);
				T.Do([&T]()
				{
					// The Joker shop's building (its upper windows twitched).
					AHKGame* G = T.Game();
					const FHKShop& S = G->Shops->Shops[0];
					const FVector2D A = S.ToWorld(S.W / 2, -9), B = S.ToWorld(S.W / 2, 3);
					const double Y = FHKData::Get().HeightWorld(A.X, A.Y) + 1.7;
					G->bCameraOverride = true;
					G->OverridePos = FVector(A.X, Y, A.Y);
					G->OverrideQuat = HK::EulerToQuat(.18, std::atan2(-(B.X - A.X), -(B.Y - A.Y)), 0, TEXT("YXZ"));
				});
				T.Wait(1.5); T.Shot(TEXT("f5-joker-a"));
				T.Do([&T]() { AHKGame* G = T.Game(); G->OverridePos.X += .03; G->OverrideQuat = G->OverrideQuat * FQuat(FVector::UpVector, .00175); });
				T.Wait(.4); T.Shot(TEXT("f5-joker-b"));
				T.Pose(40, 70, 90, Toward3(40, 90, 0, 0), -.55); T.Wait(1.5); T.Shot(TEXT("f6-aerial-a"));
				T.Pose(40.03, 70, 90, Toward3(40, 90, 0, 0) + .00175, -.55); T.Wait(.4); T.Shot(TEXT("f6-aerial-b"));

				// ---- Flicker inside the flat, on the paintball course and on the construction site (the user, 3 Nov 2026):
				// two frames from the same still camera; surfaces that fight change between them (the frames' jitter). ----
				{
					FHKData& D = FHKData::Get();
					struct FStill { const TCHAR* Name; double X, Y, Z, Tx, Ty, Tz; };
					// Paintball play area centre local (-40, 318), floor -27.05 m; site pad centre local (-88, 416).
					const FVector2D P0 = D.ToWorld(-40, 318), P1 = D.ToWorld(-60, 300), P2 = D.ToWorld(-15, 335);
					const FVector2D S0 = D.ToWorld(-88, 416), S1 = D.ToWorld(-50, 440), S2 = D.ToWorld(-130, 390);
					const double Pb = -27.05 + 1.65, Sg = D.GroundWorld(S0.X, S0.Y) + 1.65;
					const FStill Stills[] = {
						{TEXT("p1-kitchen"), 2.0, 1.62, 6.6, 5.4, 1.0, 8.6}, {TEXT("p2-living-room"), 0.4, 1.62, 0.4, 2.6, 1.2, 4.5},
						{TEXT("p3-bedroom"), 2.0, 1.62, 9.7, 5.5, 1.1, 8.7},
						{TEXT("p4-paintball"), P0.X, Pb, P0.Y, P1.X, Pb - 1.2, P1.Y}, {TEXT("p5-paintball-2"), P0.X, Pb, P0.Y, P2.X, Pb - 1.2, P2.Y},
						{TEXT("p6-site"), S0.X, Sg, S0.Y, S1.X, Sg - 2.0, S1.Y}, {TEXT("p7-site-2"), S0.X, Sg, S0.Y, S2.X, Sg - 2.0, S2.Y},
						{TEXT("p8-site-above"), S0.X, Sg + 30.0, S0.Y, S0.X + .01, Sg - 2.0, S0.Y + .01}};
					// Everything drawn where the site's ground is (a probe for surfaces lying on the soil).
					T.Log([S1]()
					{
						FString Out = TEXT("site ground probe:");
						const FHKData& D = FHKData::Get();
						for (double F : {.0, .25, .5})
						{
							const FVector2D Q = D.ToWorld(-88 + 20 * F, 416 + 12 * F);
							const FVector P = HK::ToUE(FVector(Q.X, D.GroundWorld(Q.X, Q.Y), Q.Y));
							for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
								if (It->IsRegistered() && It->IsVisible() && It->GetWorld() && It->GetWorld()->IsGameWorld() && It->Bounds.GetBox().ExpandBy(10).IsInside(P))
									Out += FString::Printf(TEXT(" [%s %s %s]"), *It->GetClass()->GetName(), It->GetOwner() ? *It->GetOwner()->GetName() : TEXT("-"),
										Cast<UStaticMeshComponent>(*It) && Cast<UStaticMeshComponent>(*It)->GetStaticMesh() ? *Cast<UStaticMeshComponent>(*It)->GetStaticMesh()->GetName() : TEXT(""));
						}
						return Out;
					});
					for (const FStill& S : Stills)
					{
						LookFrom(T, S.X, S.Y, S.Z, S.Tx, S.Ty, S.Tz);
						T.Wait(1.5); T.Shot(FString::Printf(TEXT("%s-a"), S.Name));
						T.Wait(.25); T.Shot(FString::Printf(TEXT("%s-b"), S.Name));
					}
				}

				// ---- Street View spots (Sigurd Lies gate 19, bearing 322 and 230; Haarklous plass) ----
				LookFrom(T, -31.42, FHKData::Get().HeightWorld(-31.42, -2.52) + 2.2, -2.52, -21.78, FHKData::Get().HeightWorld(-21.78, .15) + 4.5, .15);
				T.Wait(1); T.Shot(TEXT("s1-sigurd-lies-gate"));
				LookFrom(T, -31.42, FHKData::Get().HeightWorld(-31.42, -2.52) + 2.2, -2.52, -60, FHKData::Get().HeightWorld(-60, 20) + 3, 20);
				T.Wait(1); T.Shot(TEXT("s2-sigurd-lies-gate-south"));
				LookFrom(T, 18, FHKData::Get().HeightWorld(18, 2) + 1.7, 2, -20, FHKData::Get().HeightWorld(-20, 30) + 6, 30);
				T.Wait(1); T.Shot(TEXT("s3-haarklous-plass"));
				// Two blocks OSM gives no colour, from the Street View spots of the survey of 3 Nov 2026
				// (tools/street-view-looks.json): Sigurd Lies gate 21 (yellow plaster), Nordkappgata 38 (cream plaster).
				LookFrom(T, -67.42, FHKData::Get().HeightWorld(-67.42, -26.83) + 2.2, -26.83, -42.7, FHKData::Get().HeightWorld(-42.7, -43.84) + 4, -43.84);
				T.Wait(1); T.Shot(TEXT("s4-sigurd-lies-gate-21"));
				LookFrom(T, -43.8, FHKData::Get().HeightWorld(-43.8, -122.52) + 2.2, -122.52, -40.94, FHKData::Get().HeightWorld(-40.94, -152.38) + 4, -152.38);
				T.Wait(1); T.Shot(TEXT("s5-nordkappgata-38"));

				// ---- The real cars: the first Golf (Pick 0) and the first ID.3 (Pick 1, silver / white / blue) ----
				for (int32 Pick = 0; Pick < 2; Pick++)
				{
					static const FCarView Views[] = {
						{TEXT("front34"), 4.6, -3.0, 1.45, 0, 0, .7}, {TEXT("side"), 0, 5.6, 1.2, 0, 0, .75}, {TEXT("rear34"), -4.8, 3.2, 1.5, 0, 0, .75},
						{TEXT("headlight"), 2.9, -1.2, 1.05, 2.0, -.55, .75}, {TEXT("wheel"), 1.3, 2.1, .65, 1.3, .8, .35}, {TEXT("low-front"), 6.5, -.8, .5, 0, 0, .7}};
					for (const FCarView& V : Views)
					{
						T.Do([&T, Pick, V]()
						{
							AHKGame* G = T.Game();
							for (FHKVehicle* C : G->VehicleList)
							{
								if (C->Kind != TEXT("car")) continue;
								const int32 M = ((static_cast<FHKCar*>(C)->Model % 7) + 7) % 7;
								if ((M == 2 || M == 3 || M == 4) != (Pick == 1)) continue;
								const FVector2D F(-FMath::Sin(C->Yaw), -FMath::Cos(C->Yaw)), R(FMath::Cos(C->Yaw), -FMath::Sin(C->Yaw));
								const FVector2D P = FVector2D(C->X, C->Z) + F * V.F + R * V.R, Tg = FVector2D(C->X, C->Z) + F * V.Tf + R * V.Tr;
								const double Y = C->Y + V.H, Ty = C->Y + V.Th;
								G->bCameraOverride = true;
								G->OverridePos = FVector(P.X, Y, P.Y);
								G->OverrideQuat = HK::EulerToQuat(FMath::Atan2(Ty - Y, (Tg - P).Size()), Toward3(P.X, P.Y, Tg.X, Tg.Y), 0, TEXT("YXZ"));
								break;
							}
						});
						T.Wait(1.2);
						T.Shot(FString::Printf(TEXT("c%d-%s"), Pick + 1, V.Name));
					}
				}
				// The driver's seat of the first car (left-hand drive: eye at x -0.37, y 1.24, z 0.22 in the car frame).
				T.Do([&T]()
				{
					AHKGame* G = T.Game();
					for (FHKVehicle* C : G->VehicleList)
					{
						if (C->Kind != TEXT("car")) continue;
						const FVector2D F(-FMath::Sin(C->Yaw), -FMath::Cos(C->Yaw)), R(FMath::Cos(C->Yaw), -FMath::Sin(C->Yaw));
						const FVector2D P = FVector2D(C->X, C->Z) + R * -.37 - F * .22;
						G->bCameraOverride = true;
						G->OverridePos = FVector(P.X, C->Y + 1.22, P.Y);
						G->OverrideQuat = HK::EulerToQuat(-.08, C->Yaw, 0, TEXT("YXZ"));
						break;
					}
				});
				T.Wait(1.2);
				T.Shot(TEXT("c9-driver-seat"));
				T.FreeCamera();
			});
		}
	} GRegisterLook;
}
