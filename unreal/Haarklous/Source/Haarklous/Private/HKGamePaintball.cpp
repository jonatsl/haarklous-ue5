#include "HKGame.h"
#include "HKAudio.h"
#include "HKData.h"
#include "HKMath.h"
#include "HKNode.h"
#include "HKPaintball.h"
#include "HKRigActor.h"
#include "Haarklous.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

/*
 * The paintball course in the game (new feature, asked for by the user on 28 Oct 2026; rules and sessions in
 * HKPaintball): the course rig, the lobby furniture, the flags, the balls in flight, paint splats, the marker in our
 * hands and the gear (marker, goggles, team armbands) of the other players in a game.
 */
namespace
{
	UStaticMesh* PbMesh(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("/Game/Haarklous/Meshes/paintball_parts/%s.%s"), Name, Name);
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *Path);
		if (!M) UE_LOG(LogHaarklous, Warning, TEXT("Paintball mesh %s missing"), Name);
		return M;
	}
}

void AHKGame::LoadPaintball()
{
	// Before the trees: none grow in the yard.
	Paintball = MakeUnique<FHKPaintball>();
	if (!Paintball->Load(FPaths::Combine(FHKData::DataDir(), TEXT("paintball.json"))))
	{
		UE_LOG(LogHaarklous, Warning, TEXT("No paintball course (paintball.json missing or bad)"));
		Paintball.Reset();
	}
}

void AHKGame::SetupPaintball()
{
	if (!Paintball) return;
	FHKPaintball* P = Paintball.Get();
	// The yard's floor for walking and driving; the walls, the net and the cover block.
	TFunction<bool(double, double, double&)> Prev = FHKData::Get().SiteGround;
	FHKData::Get().SiteGround = [P, Prev](double X, double Z, double& Y) { return P->Ground(X, Z, Y) || (Prev && Prev(X, Z, Y)); };
	UpdateDynamic();
	// The course rig: its root on the play area's centre, turned by the map's theta.
	PbRig = MakeUnique<FHKRig>();
	FString Error;
	if (PbRig->LoadFromJson(FPaths::Combine(FHKData::DataDir(), TEXT("rigs"), TEXT("paintball.json")), &Error))
	{
		const FVector R = P->RootPos();
		PbRig->Root->SetPos(R.X, P->Floor, R.Z);
		PbRig->Root->SetRotation(0, P->Theta, 0);
		PbRig->Root->UpdateMatrixWorld();
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PbCourse = GetWorld()->SpawnActor<AHKRigActor>(AHKRigActor::StaticClass(), FTransform::Identity, SP);
		PbCourse->Init(PbRig.Get(), TEXT("paintball"));
		PbCourse->SetCullDistance(60000);
		PbCourse->bAutoSync = false;
		PbCourse->Sync();
	}
	else UE_LOG(LogHaarklous, Warning, TEXT("Paintball rig: %s"), *Error);
	auto Comp = [this](UStaticMesh* M, bool bShadow, float Cull) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		if (M) C->SetStaticMesh(M);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(bShadow);
		C->SetCullDistance(Cull);
		C->RegisterComponent();
		return C;
	};
	// Lobby furniture on the lawn.
	for (const FHKPaintball::FLobbySpot& L : P->LobbyProps)
	{
		UStaticMeshComponent* C = Comp(PbMesh(*(TEXT("SM_PB_") + L.Kind)), true, 30000);
		C->SetWorldTransform(HK::TransformToUE(FVector(L.X, FHKData::Get().GroundWorld(L.X, L.Z) - .02, L.Z), HK::AxisAngle(FVector(0, 1, 0), L.Yaw), FVector::OneVector));
		PbProps.Add(C);
	}
	PbFlags.Add(Comp(PbMesh(TEXT("SM_PB_Flag_Blue")), true, 30000));
	PbFlags.Add(Comp(PbMesh(TEXT("SM_PB_Flag_Red")), true, 30000));
	for (const TCHAR* Team : {TEXT("Blue"), TEXT("Red")})
	{
		UInstancedStaticMeshComponent* B = NewObject<UInstancedStaticMeshComponent>(this);
		B->SetStaticMesh(PbMesh(*FString::Printf(TEXT("SM_PB_Ball_%s"), Team)));
		B->SetMobility(EComponentMobility::Movable); B->SetCollisionEnabled(ECollisionEnabled::NoCollision); B->SetCastShadow(false);
		B->RegisterComponent();
		PbBalls.Add(B);
		UInstancedStaticMeshComponent* S = NewObject<UInstancedStaticMeshComponent>(this);
		S->SetStaticMesh(PbMesh(*FString::Printf(TEXT("SM_PB_Splat_%s"), Team)));
		S->SetMobility(EComponentMobility::Movable); S->SetCollisionEnabled(ECollisionEnabled::NoCollision); S->SetCastShadow(false);
		S->SetCullDistances(0, 6000);
		S->RegisterComponent();
		PbSplats.Add(S);
	}
	// The marker in our hands, on the camera (Unreal axes: x forward, y right, z up; the mesh's barrel is along -y).
	PbMarker = NewObject<UStaticMeshComponent>(this);
	PbMarker->SetStaticMesh(PbMesh(TEXT("SM_PB_Marker")));
	PbMarker->SetMobility(EComponentMobility::Movable);
	PbMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PbMarker->SetCastShadow(false);
	PbMarker->RegisterComponent();
	PbMarker->SetVisibility(false);
	// The rules' view of the game.
	FHKPaintballContext& C = P->Ctx;
	C.ServerNow = [this]() { return PbServerNow ? PbServerNow() : Time * 1000; };
	C.You = [this]() { return PbYou ? PbYou() : 0; };
	C.YourName = [this]() { return PbName ? PbName() : FString(TEXT("You")); };
	C.YourSlot = [this]() { return PbSlot ? PbSlot() : 0; };
	C.Others = [this](TArray<FHKPbMember>& Out) { if (PbOthers) PbOthers(Out); };
	C.Feet = [this]() { return FVector(Player.X, Player.Feet, Player.Z); };
	C.Yaw = [this]() { return Player.Yaw; };
	C.Stance = [this]() { return Player.Stance; };
	C.Teleport = [this](double X, double Z, double Yaw) { if (Player.Mode == TEXT("foot")) Place(X, Z, Yaw); };
	C.CameraPos = [this]() { return CamPos; };
	C.CameraFwd = [this]() { return HK::DirFromUE(Camera->GetForwardVector()); };
	C.Ground = [](double X, double Z) { return FHKData::Get().GroundWorld(X, Z); };
	C.Notice = [this](const FString& T, double S) { Notice(T, S); };
	C.Sound = [](const FString& N, double G) { FHKAudio::Get().Sfx(N, FHKSfxOpts(G)); };
}

void AHKGame::TickPaintball(double Dt)
{
	FHKPaintball& P = *Paintball;
	FHKPaintball::FInput In;
	const bool bFoot = Player.Mode == TEXT("foot");
	if (bFoot && !bPaused)
	{
		In.R2 = FMath::Max((double)Frame.R2, Frame.Mouse.bLeft ? 1.0 : 0.0);
		In.L2 = FMath::Max((double)Frame.L2, Frame.Mouse.bRight ? 1.0 : 0.0);
		In.bReload = Frame.WasPressed("square") || Frame.WasPressed("reload");
		In.bUp = Frame.WasPressed("up"); In.bDown = Frame.WasPressed("down");
		In.bLeft = Frame.WasPressed("left"); In.bRight = Frame.WasPressed("right");
		In.bTriangle = Frame.WasPressed("triangle"); In.bCross = Frame.WasPressed("cross");
		In.bCrossHeld = Frame.IsHeld("cross"); In.bTriangleHeld = Frame.IsHeld("triangle");
	}
	P.Update(Dt, In);
	// A new game: a clean course.
	if (P.Session().Id != PbSesSeen && P.Session().State == TEXT("count"))
	{
		PbSesSeen = P.Session().Id;
		for (UInstancedStaticMeshComponent* S : PbSplats) S->ClearInstances();
		PbSplatNext[0] = PbSplatNext[1] = 0;
	}
	// Balls in flight.
	for (int32 T = 0; T < 2; T++)
	{
		TArray<FTransform> Xf;
		for (const FHKPbBall& B : P.Balls) if (B.Team == T) Xf.Add(FTransform(FQuat::Identity, HK::ToUE(B.P)));
		UInstancedStaticMeshComponent* I = PbBalls[T];
		if (I->GetInstanceCount() != Xf.Num()) { I->ClearInstances(); I->AddInstances(Xf, false); }
		else for (int32 k = 0; k < Xf.Num(); k++) I->UpdateInstanceTransform(k, Xf[k], true, k == Xf.Num() - 1, true);
	}
	// Paint: a splat where a ball broke (on the course; those on players are shown by their hit).
	for (const FHKPbSplat& S : P.NewSplats)
	{
		if (S.OnFigure) continue;
		const FQuat Q = FQuat::FindBetweenNormals(FVector(0, 1, 0), S.N) * HK::AxisAngle(FVector(0, 1, 0), FMath::FRand() * 2 * PI);
		const FTransform X = HK::TransformToUE(S.P, Q, FVector(FMath::FRandRange(.6, 1.4)));
		UInstancedStaticMeshComponent* I = PbSplats[S.Team];
		if (I->GetInstanceCount() < 400) I->AddInstance(X, true);
		else { I->UpdateInstanceTransform(PbSplatNext[S.Team], X, true, true, true); PbSplatNext[S.Team] = (PbSplatNext[S.Team] + 1) % 400; }
	}
	P.NewSplats.Reset();
	for (int32 T : P.NewFired) (void)T;
	// Flags (capture the flag only), on a carrier's back when carried.
	const bool bCtf = P.Session().Mode == TEXT("ctf") && P.Session().State != TEXT("lobby");
	for (int32 T = 0; T < 2; T++)
	{
		int32 By = -1;
		FVector F = P.FlagPos(T, By);
		double Yaw = P.Theta;
		if (By >= 0)
			for (const FHKPbMember& M : P.Members())
				if (M.Id == By) { const FVector2D Fw(-FMath::Sin(M.Yaw), -FMath::Cos(M.Yaw)); F = M.Feet + FVector(-Fw.X * .3, .35, -Fw.Y * .3); Yaw = M.Yaw; }
		PbFlags[T]->SetVisibility(bCtf);
		if (bCtf) PbFlags[T]->SetWorldTransform(HK::TransformToUE(F, HK::AxisAngle(FVector(0, 1, 0), Yaw), FVector::OneVector));
	}
	// The marker in our hands (hip, or at the eye when aiming; a kick on each shot).
	const bool bArmed = P.Armed() && P.Alive() && Player.Mode == TEXT("foot");
	PbMarker->SetVisibility(bArmed);
	if (bArmed)
	{
		const double A = P.AimAmount(), K = P.Recoil() / .08;
		const double Reload = P.ReloadLeft() > 0 ? FMath::Sin(PI * (1 - P.ReloadLeft() / HKPb::ReloadS)) : 0;
		const FVector Loc(46 - 3 * K, 15 * (1 - A), -27 + 14 * A - 8 * Reload);
		// Placed from the camera's position and rotation each frame (not attached: no scale from the camera).
		const FTransform Cam(Camera->GetComponentQuat(), Camera->GetComponentLocation());
		PbMarker->SetWorldTransform(FTransform(FRotator(-2 * K - 25 * Reload, 90 + 4 * (1 - A), 15 * Reload), Loc) * Cam);
	}
	// The gear of other players in the game: the marker in the right hand, goggles, armbands in the team colour.
	TSet<int32> Seen;
	for (const FHKPbMember& M : P.Members())
	{
		if (M.bLocal || !P.MemberPlaying(M.Id)) continue;
		AHKRigActor* A = PbAvatarOf ? PbAvatarOf(M.Id) : nullptr;
		if (!A) continue;
		Seen.Add(M.Id);
		FPbGear& G = PbGear.FindOrAdd(M.Id);
		const int32 Team = FMath::Max(0, P.TeamOfMember(M.Id));
		if (G.Actor.Get() != A || G.Parts.Num() != 4 || !G.Parts[0].IsValid())
		{
			for (TWeakObjectPtr<UStaticMeshComponent>& X : G.Parts) if (X.IsValid()) X->DestroyComponent();
			G.Parts.Reset();
			G.Actor = A;
			const struct { const TCHAR* Node; const TCHAR* Mesh; FVector Pos; FVector Euler; } Parts[] = {
				{TEXT("R.arm.el"), TEXT("SM_PB_Marker"), FVector(0, -.28, -.02), FVector(-HK::Pi / 2, 0, 0)},
				{TEXT("head"), TEXT("SM_PB_Mask"), FVector(0, -.02, -.01), FVector::ZeroVector},
				{TEXT("L.arm.sh"), Team ? TEXT("SM_PB_Band_Red") : TEXT("SM_PB_Band_Blue"), FVector(0, -.12, 0), FVector(0, 0, HK::Pi / 2)},
				{TEXT("R.arm.sh"), Team ? TEXT("SM_PB_Band_Red") : TEXT("SM_PB_Band_Blue"), FVector(0, -.12, 0), FVector(0, 0, HK::Pi / 2)}};
			for (const auto& Pt : Parts)
			{
				USceneComponent* At = A->NodeComponent(Pt.Node);
				UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(A);
				C->SetStaticMesh(PbMesh(Pt.Mesh));
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				C->SetMobility(EComponentMobility::Movable);
				C->SetupAttachment(At ? At : A->GetRootComponent());
				C->RegisterComponent();
				C->SetRelativeTransform(HK::TransformToUE(Pt.Pos, HK::EulerToQuat(Pt.Euler.X, Pt.Euler.Y, Pt.Euler.Z, TEXT("XYZ")), FVector::OneVector));
				G.Parts.Add(C);
			}
		}
	}
	for (auto It = PbGear.CreateIterator(); It; ++It)
		if (!Seen.Contains(It.Key()))
		{
			for (TWeakObjectPtr<UStaticMeshComponent>& X : It.Value().Parts) if (X.IsValid()) X->DestroyComponent();
			It.RemoveCurrent();
		}
}
