// Scripted play-through "people" (-hktest=people): the realistic people (MakeHuman figures, 28 Oct 2026). The five
// player avatars (as other players see them) in the web avatar's poses: standing, walking, crouching, sitting with a
// beer, prone, driving, riding; and the two mission figures (the kneeling hostage, the armed suspect). Screenshots from
// the front, the side and close up go to the -hkout folder.
#include "HKTestDriver.h"
#include "Haarklous.h"
#include "HKAvatars.h"
#include "HKData.h"
#include "HKGame.h"
#include "HKMath.h"
#include "HKNode.h"
#include "HKRigActor.h"
#include "Engine/World.h"

namespace HKPeopleTest
{
	struct FDemo
	{
		TUniquePtr<FHKAvatar> Av;
		TUniquePtr<FHKRig> Rig;              // mission figures
		TWeakObjectPtr<AHKRigActor> Actor;
		FString Stance = TEXT("stand");
		double Speed = 0, X = 0, Z = 0, Lift = 0, Yaw = HK::Pi;
		bool bBeer = false;
	};
	struct FScene { TArray<FDemo> People; };

	const double Cx = -6, Cz = 33;           // a lawn by the fire pit
	double Ground(double X, double Z) { return FHKData::Get().GroundWorld(X, Z); }

	void Place(FDemo& D, double Dt)
	{
		if (D.Av)
		{
			FHKPoseArgs A; A.Stance = D.Stance; A.Speed = D.Speed; A.Dt = Dt; A.bBeer = D.bBeer;
			D.Av->Pose(A);
		}
		FHKNode* Root = D.Av ? D.Av->Group() : D.Rig->Root;
		Root->SetPos(D.X, Ground(D.X, D.Z) + D.Lift, D.Z);
		Root->SetRotation(0, D.Yaw, 0);
		Root->UpdateMatrixWorld();
	}

	// Keep everyone posed (the walk cycle runs) for Seconds.
	void Run(AHKTestDriver& T, TSharedPtr<FScene> S, double Seconds)
	{
		TSharedPtr<double> Time = MakeShared<double>(0);
		T.Steps.Add([S, Seconds, Time](double Dt)
		{
			for (FDemo& D : S->People) Place(D, Dt);
			return (*Time += Dt) >= Seconds;
		});
	}

	void Look(AHKTestDriver& T, double X, double H, double Z, double TX, double TH, double TZ)
	{
		const double Y0 = Ground(X, Z) + H, Y1 = Ground(TX, TZ) + TH;
		const double Yaw = FMath::Atan2(-(TX - X), -(TZ - Z)), Pitch = FMath::Atan2(Y1 - Y0, FVector2D(TX - X, TZ - Z).Size());
		T.Pose(X, Y0, Z, Yaw, Pitch);
	}

	struct FRegister
	{
		FRegister()
		{
			FHKTestScripts::All().Add(TEXT("people"), [](AHKTestDriver& T)
			{
				TSharedPtr<FScene> S = MakeShared<FScene>();
				T.Wait(3);
				T.Press(0);
				T.Wait(1.5);
				T.Do([&T, S]()
				{
					auto Spawn = [&T](FDemo& D, FHKRig* Rig, const FString& RigName)
					{
						FActorSpawnParameters P;
						P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
						AHKRigActor* A = T.GetWorld()->SpawnActor<AHKRigActor>(AHKRigActor::StaticClass(), FTransform::Identity, P);
						A->Init(Rig, RigName);
						D.Actor = A;
					};
					const TCHAR* Stances[5] = {TEXT("stand"), TEXT("stand"), TEXT("crouch"), TEXT("sit"), TEXT("stand")};
					for (int32 i = 0; i < 5; i++)
					{
						FDemo D;
						D.Av = HKAvatars::Avatar(i, FString::Printf(TEXT("Player %d"), i + 1));
						D.X = Cx - 3.2 + i * 1.6; D.Z = Cz;
						D.Stance = Stances[i];
						D.Speed = i == 1 ? 1.6 : 0;
						D.Lift = i == 3 ? .5 : 0;          // seated poses have their origin at the hip
						D.bBeer = i == 4;
						Spawn(D, &D.Av->Rig, D.Av->RigName);
						S->People.Add(MoveTemp(D));
					}
					for (int32 k = 0; k < 2; k++)
					{
						FDemo D;
						D.Rig = MakeUnique<FHKRig>();
						const FString Name = k ? TEXT("mission_hostile") : TEXT("mission_hostage");
						D.Rig->LoadFromJson(FPaths::Combine(FHKData::DataDir(), TEXT("rigs"), Name + TEXT(".json")));
						D.X = Cx - 1.2 + k * 2.4; D.Z = Cz - 4;
						Spawn(D, D.Rig.Get(), Name);
						S->People.Add(MoveTemp(D));
					}
					UE_LOG(LogHaarklous, Display, TEXT("HK-TEST people: %d figures by the fire pit"), S->People.Num());
				});
				Run(T, S, 1);
				Look(T, Cx, 1.5, Cz + 6.5, Cx, 1.0, Cz);
				Run(T, S, .6);
				T.WaitShaders();
				Run(T, S, .5);
				T.Shot(TEXT("01-players"));
				Look(T, Cx - 3.2, 1.65, Cz + 1.3, Cx - 3.2, 1.6, Cz);
				Run(T, S, .4);
				T.Shot(TEXT("02-face-player1"));
				Look(T, Cx - 1.6, 1.6, Cz + 1.4, Cx - 1.6, 1.5, Cz);
				Run(T, S, .4);
				T.Shot(TEXT("03-face-player2"));
				Look(T, Cx + 3.2, 1.6, Cz + 1.6, Cx + 3.2, 1.4, Cz);
				Run(T, S, .4);
				T.Shot(TEXT("04-player5-beer"));
				Look(T, Cx + 6, 1.4, Cz + 1.5, Cx, 1.0, Cz);
				Run(T, S, .6);
				T.Shot(TEXT("05-players-side"));
				// Other poses: prone, driving, riding, the drone pilot, walking fast.
				T.Do([S]()
				{
					const TCHAR* Stances[5] = {TEXT("prone"), TEXT("stand"), TEXT("drive"), TEXT("ride"), TEXT("drone")};
					for (int32 i = 0; i < 5; i++)
					{
						FDemo& D = S->People[i];
						D.Stance = Stances[i]; D.Speed = i == 1 ? 3.6 : 0; D.bBeer = false;
						D.Lift = (i == 2 || i == 3) ? .55 : 0;
					}
				});
				Run(T, S, .5);
				Look(T, Cx + 2.5, 1.7, Cz + 5.5, Cx, .8, Cz);
				Run(T, S, .5);
				T.Shot(TEXT("06-more-poses"));
				Look(T, Cx, 1.4, Cz - 1.2, Cx, 1.0, Cz - 4);
				Run(T, S, .4);
				T.Shot(TEXT("07-mission-figures"));
				Look(T, Cx + 1.2, 1.5, Cz - 2.4, Cx + 1.2, 1.4, Cz - 4);
				Run(T, S, .4);
				T.Shot(TEXT("08-suspect-close"));
				T.FreeCamera();
				T.Do([S]() { for (FDemo& D : S->People) if (D.Actor.IsValid()) D.Actor->Destroy(); S->People.Reset(); });
			});
		}
	} GPeopleRegister;
}
