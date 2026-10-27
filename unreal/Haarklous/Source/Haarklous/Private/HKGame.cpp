// Port of game-main.js: the frame loop, the player on foot, stances, jumping, doors, interaction, drinks, the firepit seats.
// The numbers are the web version's; comments in quotes are its own.
#include "HKGame.h"
#include "HKAudio.h"
#include "HKData.h"
#include "HKMath.h"
#include "HKRigActor.h"
#include "HKUi.h"
#include "HKVehicle.h"
#include "Haarklous.h"
#include "Camera/CameraComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const double EyeStand = 1.65;
	FHKMat MatOf(const TSharedPtr<FJsonObject>& O, const FString& K)
	{
		return FHKMat::FromArray(HKJson::Nums(O, K));
	}
	// game-main.js inside(): even-odd test without the edge tolerance.
	bool InsideMain(double X, double Z, const TArray<FHKPt>& Pts)
	{
		bool R = false;
		for (int32 i = 0, j = Pts.Num() - 1; i < Pts.Num(); j = i++)
		{
			const FHKPt& A = Pts[i];
			const FHKPt& B = Pts[j];
			if ((A.Z > Z) != (B.Z > Z) && X < (B.X - A.X) * (Z - A.Z) / (B.Z - A.Z) + A.X) R = !R;
		}
		return R;
	}
	// polygonsOverlap(a, b): separating axis test on the xz plane.
	bool PolygonsOverlap(const TArray<FHKPt>& A, const TArray<FHKPt>& B)
	{
		for (const TArray<FHKPt>* Poly : {&A, &B})
			for (int32 i = 0; i < Poly->Num(); i++)
			{
				const FHKPt& P = (*Poly)[i];
				const FHKPt& Q = (*Poly)[(i + 1) % Poly->Num()];
				const double Nx = -(Q.Z - P.Z), Nz = Q.X - P.X;
				double AMin = TNumericLimits<double>::Max(), AMax = -TNumericLimits<double>::Max(), BMin = AMin, BMax = AMax;
				for (const FHKPt& V : A) { const double D = V.X * Nx + V.Z * Nz; AMin = FMath::Min(AMin, D); AMax = FMath::Max(AMax, D); }
				for (const FHKPt& V : B) { const double D = V.X * Nx + V.Z * Nz; BMin = FMath::Min(BMin, D); BMax = FMath::Max(BMax, D); }
				if (AMax <= BMin + 1e-8 || BMax <= AMin + 1e-8) return false;
			}
		return true;
	}
}

AHKGame::AHKGame()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	RootComponent = Camera;
	Camera->bConstrainAspectRatio = false;
	Camera->SetFieldOfView(90);
}

AHKGame* AHKGame::Get(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) return nullptr;
	for (TActorIterator<AHKGame> It(W); It; ++It) return *It;
	return nullptr;
}

void AHKGame::BeginPlay()
{
	Super::BeginPlay();
	FHKInputProcessor::Register();
	if (GEngine) GEngine->Exec(GetWorld(), TEXT("DisableAllScreenMessages"));
	InputProc = FHKInputProcessor::Get();
	LoadSettings();
	BuildWorld();
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0)) PC->SetViewTarget(this);
	ApplyQuality();
	UE_LOG(LogHaarklous, Log, TEXT("Game ready: player at (%.2f, %.2f) feet %.2f, %d doors, %d drinks, %d towers"), Player.X, Player.Z, Player.Feet, Doors.Num(), Drinks.Num(), Towers.Num());
}

void AHKGame::EndPlay(const EEndPlayReason::Type Reason)
{
	FHKInputProcessor::Unregister();
	Super::EndPlay(Reason);
}

// ---------- World ----------
void AHKGame::BuildWorld()
{
	FHKData& Data = FHKData::Get();
	if (!Data.bLoaded) { UE_LOG(LogHaarklous, Error, TEXT("No world data: %s"), *Data.Error); return; }
	const TSharedPtr<FJsonObject> G = Data.Game;
	// Towers (own stair first, then the eight enterable neighbouring buildings).
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(G, TEXT("towers")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FHKTower T;
		T.bOwn = HKJson::Bool(O, TEXT("own"));
		T.Building = (int32)HKJson::Num(O, TEXT("building"), -1);
		T.Matrix = MatOf(O, TEXT("matrix"));
		T.Inverse = MatOf(O, TEXT("inverse"));
		Towers.Add(T);
	}
	// Apartment floors (inApartment, aptBox) and rooms (footsteps).
	AptFloorCount = (int32)HKJson::Num(HKJson::Obj(G, TEXT("counts")), TEXT("aptFloors"));
	for (int32 i = 0; i < AptFloorCount && i < Data.WalkWorld.Floors.Num(); i++) AptFloorPolys.Add(Data.WalkWorld.Floors[i].Points);
	{
		// "the whole flat (also doorways and wall gaps) counts as indoors; the balcony is open air."
		double X0 = 1e9, X1 = -1e9, Z0 = 1e9, Z1 = -1e9;
		for (int32 i = 0; i < AptFloorCount && i < Data.WalkWorld.Floors.Num(); i++)
		{
			if (Data.WalkWorld.Floors[i].Name.Contains(TEXT("balcony"), ESearchCase::IgnoreCase)) continue;
			for (const FHKPt& P : Data.WalkWorld.Floors[i].Points) { X0 = FMath::Min(X0, P.X); X1 = FMath::Max(X1, P.X); Z0 = FMath::Min(Z0, P.Z); Z1 = FMath::Max(Z1, P.Z); }
		}
		AptX0 = X0 - .15; AptX1 = X1 + .15; AptZ0 = Z0 - .15; AptZ1 = Z1 + .15;
	}
	const TSharedPtr<FJsonObject> Apt = HKJson::Obj(G, TEXT("apartment"));
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(Apt, TEXT("rooms")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		Rooms.Add({HKJson::Str(O, TEXT("id")), HKJson::Str(O, TEXT("name")), HKJson::Pts(HKJson::Arr(O, TEXT("points")))});
	}
	// Firepit seats, woodpile and crate (game-fire.js).
	const TSharedPtr<FJsonObject> Fire = HKJson::Obj(G, TEXT("fire"));
	FireCenter = HKJson::Vec(Fire, TEXT("center"));
	{
		const TArray<double> W = HKJson::Nums(Fire, TEXT("woodpile")), C = HKJson::Nums(Fire, TEXT("crate"));
		FireWoodpile = FVector2D(W[0], W[1]); FireCrate = FVector2D(C[0], C[1]);
	}
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(Fire, TEXT("spots")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FireSpots.Add({(int32)HKJson::Num(O, TEXT("id")), HKJson::Num(O, TEXT("x")), HKJson::Num(O, TEXT("y")), HKJson::Num(O, TEXT("z")), HKJson::Num(O, TEXT("yaw"))});
	}
	const TArray<double> Ms = HKJson::Nums(HKJson::Obj(G, TEXT("mission")), TEXT("start"));
	MissionStartLocal = FVector2D(Ms[0], Ms[1]);
	const TSharedPtr<FJsonObject> DO = HKJson::Obj(HKJson::Obj(G, TEXT("stair")), TEXT("doorOutside"));
	StairDoorOutside = FVector2D(HKJson::Num(DO, TEXT("x")), HKJson::Num(DO, TEXT("z")));

	LocalFire.T = FPlatformTime::Seconds();
	SpawnRigs();
	SpawnTrees();
	SpawnOverlay();
	SetupModules();
	// Drunk effect: a post-process material (blur, double image, saturation) on the camera.
	if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Haarklous/Materials/M_HK_Drunk.M_HK_Drunk")))
	{
		DrunkMid = UMaterialInstanceDynamic::Create(M, this);
		Camera->PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(0, DrunkMid));
	}
	// Doors: hinge nodes in the apartment and stair rigs.
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(G, TEXT("doors")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FHKDoor D;
		D.Id = HKJson::Str(O, TEXT("id")); D.Name = HKJson::Str(O, TEXT("name")); D.Motion = HKJson::Str(O, TEXT("motion")); D.Sound = HKJson::Str(O, TEXT("sound"));
		D.OpenAngle = HKJson::Num(O, TEXT("openAngle")); D.ClosedX = HKJson::Num(O, TEXT("closedX")); D.Travel = HKJson::Num(O, TEXT("travel"));
		D.LeafBox = HKJson::Vec(O, TEXT("leafBox")); D.LeafLocal = MatOf(O, TEXT("leafLocal"));
		D.bInApartment = HKJson::Bool(O, TEXT("inApartment")); D.Tower = (int32)HKJson::Num(O, TEXT("tower"), -1);
		FHKRig* R = D.bInApartment ? AptRig : (D.Tower >= 0 && D.Tower < StairRigs.Num() ? StairRigs[D.Tower] : nullptr);
		D.Hinge = R ? R->Find(TEXT("door:") + D.Id) : nullptr;
		if (!D.Hinge) UE_LOG(LogHaarklous, Warning, TEXT("Door %s has no hinge node"), *D.Id);
		Doors.Add(D);
	}
	// Drinks: six beers on the kitchen worktop, aquavit on the TV bench.
	int32 Di = 0;
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(G, TEXT("drinks")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FHKDrink Dk;
		Dk.Kind = HKJson::Str(O, TEXT("kind")); Dk.Name = HKJson::Str(O, TEXT("name")); Dk.Sips = (int32)HKJson::Num(O, TEXT("sips"));
		Dk.Object = AptRig ? AptRig->Find(FString::Printf(TEXT("drink:%d"), Di)) : nullptr;
		Dk.Liquid = AptRig ? AptRig->Find(FString::Printf(TEXT("liquid:%d"), Di)) : nullptr;
		Drinks.Add(Dk);
		Di++;
	}
	// Start position: "{const s = missionStart(), f = {x: Math.sin(s.yaw), z: Math.cos(s.yaw)};place(s.x + f.x * 4, s.z + f.z * 4, s.yaw);}"
	{
		const FVector2D P = Data.ToWorld(MissionStartLocal.X, MissionStartLocal.Y);
		const double Yaw = FMath::Atan2(-(StairDoorOutside.X - P.X), -(StairDoorOutside.Y - P.Y));
		Place(P.X + FMath::Sin(Yaw) * 4, P.Y + FMath::Cos(Yaw) * 4, Yaw);
	}
	UpdateDynamic();
	// DEBUG: where the street door is (web frame and Unreal).
	for (const FHKDoor& D : Doors)
		if (D.Id == TEXT("own-street-door") && D.Hinge)
		{
			const FVector W = D.Hinge->GetWorldPosition();
			UE_LOG(LogHaarklous, Display, TEXT("DEBUG door hinge web (%.3f, %.3f, %.3f) local (%.3f, %.3f, %.3f) parent %s"), W.X, W.Y, W.Z, D.Hinge->Position.X, D.Hinge->Position.Y, D.Hinge->Position.Z, D.Hinge->Parent ? *D.Hinge->Parent->Name : TEXT("-"));
			AHKRigActor* A = ActorOfRig.FindRef(TEXT("stair_own"));
			for (FHKNode* C : D.Hinge->Children)
				if (UStaticMeshComponent* M = A ? A->MeshOf(C) : nullptr)
					UE_LOG(LogHaarklous, Display, TEXT("DEBUG part %s at %s bounds %s visible %d mesh %s"), *C->Name, *M->GetComponentLocation().ToString(), *M->Bounds.GetBox().ToString(), M->IsVisible(), M->GetStaticMesh() ? *M->GetStaticMesh()->GetName() : TEXT("none"));
		}
	UpdateCamera();
}

void AHKGame::SpawnRigs()
{
	const FString Dir = FHKData::DataDir() / TEXT("rigs");
	auto Load = [&](const FString& Name, bool bSkipRoot) -> FHKRig*
	{
		TUniquePtr<FHKRig> R = MakeUnique<FHKRig>();
		FString Err;
		if (!R->LoadFromJson(Dir / (Name + TEXT(".json")), &Err)) { UE_LOG(LogHaarklous, Warning, TEXT("Rig %s: %s"), *Name, *Err); return nullptr; }
		FHKRig* Raw = R.Get();
		Rigs.Add(MoveTemp(R));
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AHKRigActor* A = GetWorld()->SpawnActor<AHKRigActor>(AHKRigActor::StaticClass(), FTransform::Identity, SP);
		A->Init(Raw, Name, bSkipRoot);
		A->SetAllCollision(true);
		RigActors.Add(A);
		ActorOfRig.Add(Name, A);
		return Raw;
	};
	AptRig = Load(TEXT("apartment"), true);
	StairRigs.Add(Load(TEXT("stair_own"), true));
	for (int32 i = 0; i < 8; i++) StairRigs.Add(Load(FString::Printf(TEXT("stair_b%d"), i), true));
	Load(TEXT("misc"), false);   // mission marker, the excavator's first dirt pile (fire and minigames: HKGameModules.cpp)
}

void AHKGame::SpawnTrees()
{
	// Trees and bushes: the web's instanced leaf-card crowns, trunks and bushes, at the same places and sizes.
	for (const TCHAR* Rig : {TEXT("world_near"), TEXT("world_far")})
	{
		const FString Path = FPaths::Combine(FHKData::DataDir(), TEXT("instances"), FString(Rig) + TEXT(".json"));
		const TSharedPtr<FJsonObject> J = HKJson::LoadFile(Path);
		if (!J) { UE_LOG(LogHaarklous, Warning, TEXT("No instance list %s"), *Path); continue; }
		for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(J, TEXT("instances")))
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			const FString Asset = HKJson::Str(O, TEXT("asset"));
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Haarklous/Meshes/%s/%s.%s"), Rig, *Asset, *Asset));
			if (!Mesh) continue;
			UHierarchicalInstancedStaticMeshComponent* H = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
			H->SetStaticMesh(Mesh);
			H->SetMobility(EComponentMobility::Static);
			H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			H->SetCastShadow(HKJson::Bool(O, TEXT("cast"), true));
			H->SetCullDistances(0, FString(Rig) == TEXT("world_far") ? 150000 : 90000);
			H->RegisterComponent();
			TArray<FTransform> Xf;
			for (const TSharedPtr<FJsonValue>& T : HKJson::Arr(O, TEXT("transforms")))
			{
				const TArray<double> A = HKJson::Nums(T);
				Xf.Add(FTransform(FQuat(A[3], A[4], A[5], A[6]), FVector(A[0], A[1], A[2]), FVector(A[7], A[8], A[9])));
			}
			H->AddInstances(Xf, false, true);
			TreeComps.Add(H);
		}
	}
}

// ---------- Player ----------
void AHKGame::Place(double X, double Z, double Yaw, double Y, bool bKeepYaw)
{
	FHKData& Data = FHKData::Get();
	const double Yy = FMath::IsNaN(Y) ? Data.HeightWorld(X, Z) : Y;
	FVector Sp;
	const bool bFound = Data.WalkWorld.FindSafeSpawn(FHKPt(X, Z), StanceEye(TEXT("stand")), Sp, 6, .08, Yy);
	Player.X = bFound ? Sp.X : X;
	Player.Z = bFound ? Sp.Y : Z;
	Player.Feet = bFound ? Sp.Z : Yy;
	if (!bKeepYaw) Player.Yaw = Yaw;
	Player.Pitch = 0; Player.Stance = TEXT("stand"); Player.EyeNow = Player.Eye; Player.JumpY = Player.JumpV = 0;
}

void AHKGame::SetStance(const FString& Next)
{
	if (Next == Player.Stance) return;
	if (StanceEye(Next) > StanceEye(Player.Stance) && !FHKData::Get().WalkWorld.CanStand(FHKPt(Player.X, Player.Z), StanceEye(Next), Player.Feet)) { Notice(TEXT("No room to stand up here.")); return; }
	Player.Stance = Next;
}

void AHKGame::MovePlayer(double Dx, double Dz)
{
	const FVector N = FHKData::Get().WalkWorld.Move(FHKPt(Player.X, Player.Z), FHKPt(Dx, Dz), StanceEye(Player.Stance), Player.Feet);
	Player.X = N.X; Player.Z = N.Y; Player.Feet = N.Z;
}

bool AHKGame::InApartment() const
{
	if (!(Player.Feet > -.5 && Player.Feet < 2.5)) return false;
	for (const TArray<FHKPt>& F : AptFloorPolys) if (InsideMain(Player.X, Player.Z, F)) return true;
	return false;
}

bool AHKGame::InFlat() const
{
	return Player.Feet > -.5 && Player.Feet < 2.5 && Player.X > AptX0 && Player.X < AptX1 && Player.Z > AptZ0 && Player.Z < AptZ1;
}

bool AHKGame::TowerAt(double X, double Z, double Feet, int32& OutTower, FVector& OutLocal) const
{
	for (int32 i = 0; i < Towers.Num(); i++)
	{
		const FHKTower& T = Towers[i];
		const FVector L = T.Inverse.TransformPoint(FVector(X, 0, Z));
		if (L.X > -1.45 && L.X < 1.665 && L.Z > 4.9 && L.Z < 11.12 && Feet > T.Matrix.E[13] - 10.5 && Feet < T.Matrix.E[13] + 2.7) { OutTower = i; OutLocal = L; return true; }
	}
	return false;
}

FString AHKGame::RoomAt() const
{
	for (const FRoom& R : Rooms) if (InsideMain(Player.X, Player.Z, R.Points)) return R.Id;
	return FString();
}

FString AHKGame::PlaceName() const
{
	if (InApartment()) return TEXT("Haarklous plass 17A, 4th floor");
	int32 T; FVector L;
	if (TowerAt(Player.X, Player.Z, Player.Feet, T, L)) return Towers[T].bOwn ? TEXT("Stairwell, Haarklous plass 17A") : TEXT("Stairwell, Torshov");
	return TEXT("Torshov, Oslo");
}

void AHKGame::Footstep(double Soft)
{
	FString Surface = TEXT("grass");
	bool bStair = false;
	int32 T; FVector L;
	if (InApartment()) { const FString R = RoomAt(); Surface = R == TEXT("bath") ? TEXT("tile") : !R.IsEmpty() ? TEXT("wood") : TEXT("metal"); }
	else if (TowerAt(Player.X, Player.Z, Player.Feet, T, L)) { Surface = TEXT("stone"); bStair = true; }
	else
	{
		const FString S = FHKData::Get().SurfaceAt(Player.X, Player.Z);
		Surface = S == TEXT("asphalt") ? TEXT("asphalt") : S == TEXT("gravel") ? TEXT("gravel") : S == TEXT("sand") ? TEXT("sand") : TEXT("grass");
	}
	FHKAudio::Get().Footstep(Surface, bStair, Soft);
}

// ---------- Doors ----------
void AHKGame::SetDoor(FHKDoor& D, double T)
{
	if (!D.Hinge) return;
	if (D.Motion == TEXT("slide")) D.Hinge->Position.X = D.ClosedX + D.Travel * T;
	else D.Hinge->SetRotY(D.OpenAngle * T);
	D.Hinge->UpdateWorldMatrix(true, true);
}

TArray<FHKPt> AHKGame::DoorPolygon(const FHKDoor& D, double& MinY, double& MaxY)
{
	// meshObstacle(leaf): the leaf box's footprint corners and its height span, in world space.
	TArray<FHKPt> Pts;
	if (!D.Hinge) { MinY = MaxY = 0; return Pts; }
	D.Hinge->UpdateWorldMatrix(true, false);
	const FHKMat LeafWorld = D.Hinge->MatrixWorld * D.LeafLocal;
	const double W = D.LeafBox.X, H = D.LeafBox.Y, Dp = D.LeafBox.Z;
	for (const FVector2D& C : {FVector2D(-W / 2, -Dp / 2), FVector2D(W / 2, -Dp / 2), FVector2D(W / 2, Dp / 2), FVector2D(-W / 2, Dp / 2)})
	{
		const FVector V = LeafWorld.TransformPoint(FVector(C.X, 0, C.Y));
		Pts.Add(FHKPt(V.X, V.Z));
	}
	const FVector Cc = LeafWorld.GetPosition();
	MinY = Cc.Y - H / 2; MaxY = Cc.Y + H / 2;
	return Pts;
}

void AHKGame::ToggleDoor(FHKDoor& D)
{
	if (D.bAnim) return;
	const double Target = D.bOpen ? 0 : 1;
	SetDoor(D, Target);
	double MinY, MaxY;
	const TArray<FHKPt> Poly = DoorPolygon(D, MinY, MaxY);
	SetDoor(D, D.bOpen ? 1 : 0);
	const TArray<FHKPt> Me = {FHKPt(Player.X - .2, Player.Z - .2), FHKPt(Player.X + .2, Player.Z - .2), FHKPt(Player.X + .2, Player.Z + .2), FHKPt(Player.X - .2, Player.Z + .2)};
	if (MaxY > Player.Feet && MinY < Player.Feet + 1.8 && PolygonsOverlap(Poly, Me)) { Notice(TEXT("Step back from the door, then try again.")); return; }
	// (Mission actors in the door's path are checked here once the mission module is merged.)
	D.bAnim = true; D.AnimFrom = D.bOpen ? 1 : 0; D.AnimTo = Target; D.AnimT = 0;
	int32 Tw; FVector L;
	FHKAudio::Get().Sfx(Target ? TEXT("doorOpen") : TEXT("doorClose"), FHKSfxOpts(1, TowerAt(Player.X, Player.Z, Player.Feet, Tw, L) ? .6 : .1));
	if (OnDoorChanged) OnDoorChanged(D.Id, Target == 1);
}

void AHKGame::ApplyDoor(const FString& Id, bool bOpenIt, bool bInstant)
{
	FHKDoor* D = Doors.FindByPredicate([&](const FHKDoor& X) { return X.Id == Id; });
	if (!D) return;
	if (bInstant) { D->bAnim = false; D->bOpen = bOpenIt; SetDoor(*D, bOpenIt ? 1 : 0); UpdateDynamic(); return; }
	if (D->bOpen == bOpenIt || D->bAnim) return;
	D->bAnim = true; D->AnimFrom = D->bOpen ? 1 : 0; D->AnimTo = bOpenIt ? 1 : 0; D->AnimT = 0;
	if (D->Hinge)
	{
		const FVector P = D->Hinge->GetWorldPosition();
		const double Dist = HK::Hypot(P.X - Player.X, P.Z - Player.Z);
		if (Dist < 25) FHKAudio::Get().Sfx(bOpenIt ? TEXT("doorOpen") : TEXT("doorClose"), FHKSfxOpts(FMath::Max(.1, 1 - Dist / 25)));
	}
}

void AHKGame::CloseAllDoors()
{
	for (FHKDoor& D : Doors) { D.bAnim = false; D.bOpen = false; SetDoor(D, 0); }
	UpdateDynamic();
}

void AHKGame::UpdateDynamic()
{
	FHKData& Data = FHKData::Get();
	TArray<FHKObstacle> List;
	for (FHKDoor& D : Doors)
	{
		if (D.bAnim || !D.Hinge) continue;
		FHKObstacle O;
		O.Points = DoorPolygon(D, O.MinY, O.MaxY);
		List.Add(O);
	}
	TArray<FHKObstacle> VList;
	for (FHKVehicle* V : VehicleList)
		if (V && V != Player.Vehicle) for (const FHKObstacle& O : V->Footprint()) { List.Add(O); VList.Add(O); }
	if (ExtraDynamic) List.Append(ExtraDynamic());
	Data.WalkWorld.SetDynamic(List);
	// "Other vehicles block the one being driven; it must never collide with its own (parked) footprint."
	// (Only the vehicle worlds that exist already, like the web's vehicleWorlds.forEach.)
	Data.ForEachVehicleWorld([&VList](FHKWalkWorld& W) { W.SetDynamic(VList); });
}

// ---------- Notices, prompts, interaction ----------
void AHKGame::Notice(const FString& Text, double Seconds)
{
	NoticeText = Text;
	NoticeT = Text.IsEmpty() ? 0 : Seconds;
}

AHKGame::FLookHit AHKGame::LookTarget(double Range) const
{
	// The web raycasts only against interactable meshes (door parts, locked doors, drinks), so walls do not block it.
	FLookHit Hit;
	const FVector From = HK::ToUE(CamPos), Dir = HK::DirToUE(CamQuat.RotateVector(FVector(0, 0, -1)));
	TArray<FHitResult> Hits;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(HKLook), true);
	GetWorld()->LineTraceMultiByChannel(Hits, From, From + Dir * Range * 100, ECC_GameTraceChannel1, Q);
	Hits.Sort([](const FHitResult& A, const FHitResult& B) { return A.Distance < B.Distance; });
	for (const FHitResult& H : Hits)
	{
		const AHKRigActor* A = Cast<AHKRigActor>(H.GetActor());
		FHKNode* N = A ? A->NodeOfComponent(H.GetComponent()) : nullptr;
		if (!N || !N->IsVisibleInTree()) continue;
		// The part's flags (or, for merged door meshes, the door node above it).
		for (FHKNode* P = N; P; P = P->Parent)
		{
			if (!P->DoorId.IsEmpty()) { Hit.DoorId = P->DoorId; break; }
			if (P->bLockedDoor) { Hit.bLocked = true; break; }
			if (P->DrinkIndex >= 0) { Hit.Drink = P->DrinkIndex; break; }
			if (P->Name.StartsWith(TEXT("door:"))) { Hit.DoorId = P->Name.Mid(5); break; }
		}
		if (!Hit.DoorId.IsEmpty() || Hit.bLocked || Hit.Drink >= 0) { Hit.bHit = true; return Hit; }
	}
	return Hit;
}

FHKPrompt AHKGame::FirePrompt()
{
	auto Near = [&](const FVector2D& P, double R) { return HK::Hypot(Player.X - P.X, Player.Z - P.Y) < R && FMath::Abs(Player.Feet - FireCenter.Y) < 1.5; };
	if (Near(FireWoodpile, 1.9)) return {FString::Printf(TEXT("□ Throw a log on the fire (fire %d %%)"), (int32)HK::JsRound(FireLevel() / FireMax * 100)), [this]() { AddLog(); }};
	if (Near(FireCrate, 1.7)) return {TEXT("□ Take a beer from the crate"), [this]() { DrinkBeer(); }};
	const FHKSitSpot* Best = nullptr;
	double Bd = 1.0;
	for (const FHKSitSpot& S : FireSpots)
	{
		const double D = HK::Hypot(Player.X - S.X, Player.Z - S.Z);
		if (D < Bd && !(SitTaken && SitTaken(S.Id))) { Bd = D; Best = &S; }
	}
	if (Best) return {TEXT("□ Sit down by the fire"), [this, Best]() { SitDown(*Best); }};
	return FHKPrompt();
}

FHKPrompt AHKGame::FindPrompt()
{
	if (Player.Mode == TEXT("sit")) return {TEXT("□ Stand up · R2 have a beer"), [this]() { StandUp(); }};
	if (ModePrompt) { FHKPrompt P; if (ModePrompt(P)) return P; }
	auto DoorPrompt = [this](const FString& Id) -> FHKPrompt
	{
		FHKDoor* D = Doors.FindByPredicate([&](const FHKDoor& X) { return X.Id == Id; });
		if (!D) return FHKPrompt();
		return {FString::Printf(TEXT("□ %s %s"), D->bOpen ? TEXT("Close") : TEXT("Open"), *D->Name.ToLower()), [this, D]() { ToggleDoor(*D); }};
	};
	// "During the mission only doors can be used (□ otherwise reloads)."
	if (MissionActive && MissionActive())
	{
		const FLookHit O = LookTarget();
		if (!O.DoorId.IsEmpty()) { const FHKPrompt P = DoorPrompt(O.DoorId); if (P.IsSet()) return P; }
		if (O.bLocked) return {TEXT("□ Try the door"), [this]() { FHKAudio::Get().Sfx(TEXT("locked")); Notice(TEXT("Locked.")); }};
		return FHKPrompt();
	}
	// "Doors first (the street door of 17A opens normally), then the mission marker beside the entrance."
	{
		const FLookHit O = LookTarget();
		if (!O.DoorId.IsEmpty()) { const FHKPrompt P = DoorPrompt(O.DoorId); if (P.IsSet()) return P; }
	}
	if (HK::Hypot(Player.X - MissionMark.X, Player.Z - MissionMark.Y) < 1.6 && FMath::Abs(Player.Feet - FHKData::Get().HeightWorld(MissionMark.X, MissionMark.Y)) < 2)
		return {TEXT("□ Start mission: rescue the hostage (4th floor)"), [this]() { if (StartMission) StartMission(); }};
	{
		const FHKPrompt Fp = FirePrompt();
		if (Fp.IsSet()) return Fp;
	}
	if (VehiclePrompt) { FHKPrompt P; if (VehiclePrompt(P)) return P; }
	if (MinigamePrompt) { FHKPrompt P; if (MinigamePrompt(P)) return P; }
	const FLookHit O = LookTarget();
	if (O.Drink >= 0 && Drinks.IsValidIndex(O.Drink) && !Drinks[O.Drink].bUsed)
	{
		FHKDrink* D = &Drinks[O.Drink];
		return {D->Kind == TEXT("beer") ? FString(TEXT("□ Drink a beer")) : FString::Printf(TEXT("□ Take a sip of %s"), *D->Name), [this, D]() { Drink(*D); }};
	}
	if (!O.DoorId.IsEmpty()) { const FHKPrompt P = DoorPrompt(O.DoorId); if (P.IsSet()) return P; }
	if (O.bLocked) return {TEXT("□ Try the door"), [this]() { FHKAudio::Get().Sfx(TEXT("locked")); Notice(TEXT("Locked. Someone lives here.")); }};
	return FHKPrompt();
}

bool AHKGame::Interact(bool bSilent)
{
	const FHKPrompt P = FindPrompt();
	if (P.IsSet()) { if (P.Act) P.Act(); return true; }
	if (!bSilent) Notice(TEXT("Nothing to use here."), 1.5);
	return false;
}

// ---------- Drinking ----------
void AHKGame::Drink(FHKDrink& D)
{
	if (D.Kind == TEXT("beer"))
	{
		if (D.Object) D.Object->bVisible = false;
		D.bUsed = true;
		FHKAudio::Get().Sfx(TEXT("bottleOpen")); FHKAudio::Get().Sfx(TEXT("gulp"));
		Drunk += 30;
		Notice(TEXT("You drink a beer. (+30 s drunk)"));
	}
	else
	{
		if (D.Sips <= 0) { Notice(TEXT("The bottle is empty.")); return; }
		D.Sips--;
		FHKAudio::Get().Sfx(TEXT("gulp"));
		Drunk += 30;
		Notice(FString::Printf(TEXT("A sip of %s. (+30 s drunk) · %d sips left"), *D.Name, D.Sips));
		if (D.Liquid) D.Liquid->Scale.Y = FMath::Max(.05, D.Sips / 12.0);
	}
}

void AHKGame::DrinkBeer()
{
	FHKAudio::Get().Sfx(TEXT("bottleOpen")); FHKAudio::Get().Sfx(TEXT("gulp"));
	Drunk += 30;
	BeerT = 20;
	Notice(TEXT("Skål! A beer from the crate. (+30 s drunk)"), 3);
}

// ---------- Firepit seats ----------
void AHKGame::SitDown(const FHKSitSpot& Spot)
{
	if (OnLeaveFoot) OnLeaveFoot();
	Player.Mode = TEXT("sit");
	Player.SitSpot = &Spot;
	Player.LookYaw = 0;
	Player.LookPitch = -.32;
	Notice(TEXT("Sitting by the fire · R2 / left mouse: have a beer · □ stand up"), 4);
}

void AHKGame::StandUp()
{
	const FHKSitSpot* Sp = Player.SitSpot;
	Player.Mode = TEXT("foot");
	Player.SitSpot = nullptr;
	if (!Sp) return;
	const double Dx = FireCenter.X - Sp->X, Dz = FireCenter.Z - Sp->Z, L = HK::Hypot(Dx, Dz) ? HK::Hypot(Dx, Dz) : 1;
	Place(Sp->X + Dx / L * .7, Sp->Z + Dz / L * .7, Sp->Yaw);
}

void AHKGame::AddLog()
{
	if (ThrowLog) ThrowLog(FVector(Player.X, Player.Feet + 1.3, Player.Z));
	FHKAudio::Get().Sfx(TEXT("throw"));
	PendingThud = .6;
	if (AddWood) AddWood();
	else { LocalFire.Level = FMath::Min((double)FireMax, FireLevel() + 1); LocalFire.T = FPlatformTime::Seconds(); }
	Notice(TEXT("You throw a log on the fire."), 2);
}

double AHKGame::FireLevel() const
{
	if (SharedFireLevel) return SharedFireLevel();
	return FMath::Max(0.0, LocalFire.Level - (FPlatformTime::Seconds() - LocalFire.T) / FireDecayS);
}

// ---------- Frame ----------
void AHKGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// "const dt = Math.min(clock.getDelta(), .05)"
	const double Dt = FMath::Min((double)DeltaSeconds, FixedDt > 0 ? FixedDt : .05);
	Frame_(FixedDt > 0 ? FixedDt : Dt);
}

void AHKGame::Frame_(double Dt)
{
	Time += Dt;
	Frame = InputProc ? InputProc->ReadFrame(Player.Mode == TEXT("vehicle")) : FHKInputFrame();
	FpsAvg += (1 / FMath::Max(Dt, .001) - FpsAvg) * .05;
	if (NoticeT > 0 && (NoticeT -= Dt) <= 0) NoticeText.Empty();
	if (PendingThud > 0 && (PendingThud -= Dt) <= 0) FHKAudio::Get().Sfx(TEXT("thud"), FHKSfxOpts(.4));
	if (!bStarted)
	{
		if (Frame.WasPressed("cross") || Frame.WasPressed("options") || Frame.Mouse.bLeftEdge || Frame.WasPressed("key:Enter")) Begin();
		UpdateCamera();
		return;
	}
	// Global toggles.
	if (Frame.WasPressed("options") && !(MissionActive && MissionActive()))
	{
		if (Map.bOpen) MapToggle();
		else if (Photo.bGallery) PhotoOpenGallery(false);
		else if (OverlayBack && OverlayBack()) {}
		else SetPaused(!bPaused);
	}
	if (bPaused)
	{
		PauseInput(Dt);
		UpdateCamera();
		return;
	}
	if (ExtrasUpdate(Dt) || (PreModeUpdate && PreModeUpdate(Dt))) { UpdateCamera(); UpdateOverlayModels(Dt); return; }
	// "Drunk effect: decays in real time."
	Drunk = FMath::Max(0.0, Drunk - Dt);
	const double Dl = DrunkLevel();
	if (Player.Mode == TEXT("sit")) UpdateSit(Dt, Dl);
	else if (Player.Mode == TEXT("foot")) UpdateFoot(Dt, Dl);
	else if (ModeUpdate) ModeUpdate(Dt, Dl);
	if (PostModeUpdate) PostModeUpdate(Dt);
	BeerT = FMath::Max(0.0, BeerT - Dt);
	// Door animations.
	bool bChanged = false;
	for (FHKDoor& D : Doors)
	{
		if (!D.bAnim) continue;
		D.AnimT = FMath::Min(1.0, D.AnimT + Dt * 2.2);
		const double T = D.AnimT, E = T < .5 ? 2 * T * T : 1 - FMath::Pow(-2 * T + 2, 2.0) / 2;
		SetDoor(D, D.AnimFrom + (D.AnimTo - D.AnimFrom) * E);
		if (D.AnimT >= 1) { D.bOpen = D.AnimTo == 1; D.bAnim = false; bChanged = true; }
	}
	if (bChanged || bDynamicEveryFrame) UpdateDynamic();
	PromptTimer -= Dt;
	if (PromptTimer <= 0)
	{
		PromptTimer = .15;
		// "promptAction = player.mode === 'minigame' || player.mode === 'drone' || gear?.open || quick || photo.active || (mission not playing) ? null : findPrompt()"
		const bool bBlocked = Player.Mode == TEXT("minigame") || Player.Mode == TEXT("drone") || (bGear && bGearOpen) || bQuick || Photo.bActive || (PromptBlocked && PromptBlocked());
		PromptAction = bBlocked ? FHKPrompt() : FindPrompt();
	}
	AmbienceTimer -= Dt;
	if (AmbienceTimer <= 0) { AmbienceTimer = .25; UpdateAmbience(); }
	UpdateCamera();
	UpdateOverlayModels(Dt);
	UpdateDrunkEffect();
}

void AHKGame::UpdateDrunkEffect()
{
	// renderFrame(): "canvas.style.filter = blur(dl * 2.2 px) saturate(1 + dl * .3)", ghost image at alpha .35 * dl drawn at
	// half resolution with offset (sin(t / 650) * 12 * dl, cos(t / 900) * 4 * dl) half-resolution pixels.
	if (!DrunkMid) return;
	const double Dl = DrunkLevel(), Ms = Time * 1000;
	Camera->PostProcessSettings.WeightedBlendables.Array[0].Weight = Dl > 0 ? 1.f : 0.f;
	if (Dl <= 0) return;
	DrunkMid->SetScalarParameterValue(FName("Level"), Dl);
	DrunkMid->SetScalarParameterValue(FName("BlurPx"), Dl * 2.2);
	DrunkMid->SetVectorParameterValue(FName("GhostOffset"), FLinearColor(FMath::Sin(Ms / 650) * 12 * Dl * 2, FMath::Cos(Ms / 900) * 4 * Dl * 2, 0, 0));
}

void AHKGame::UpdateFoot(double Dt, double Dl)
{
	const bool bMission = MissionActive && MissionActive();
	// Look.
	const double MouseScale = .0024 * Sensitivity.Mouse, PadScale = Sensitivity.Pad * (Photo.Raised > .5 ? 1 / Photo.Zoom : 1) * (bMission && LookScaleExtra ? LookScaleExtra() : 1);
	auto Curve = [](double V) { return HK::Sign(V) * V * V; };
	Player.Yaw -= Frame.Mouse.Dx * MouseScale + Curve(Frame.Look.X) * 2.8 * PadScale * Dt;
	Player.Pitch = HK::Clamp(Player.Pitch - Frame.Mouse.Dy * MouseScale - Curve(Frame.Look.Y) * 1.9 * PadScale * Dt, -1.5, 1.5);
	// "Buttons: ✕ jump, ○ tap crouch / hold prone, □ interact, L3 run toggle."
	if (!bMission || (MissionPlaying && MissionPlaying()))
	{
		if (Frame.WasPressed("cross"))
		{
			if (Player.Stance != TEXT("stand")) SetStance(TEXT("stand"));
			else if (!Player.JumpY && !Player.JumpV) { Player.JumpV = 3.1; FHKAudio::Get().Footstep(TEXT("grass"), false, .4); }
		}
		if (Frame.WasPressed("circle")) { Player.CircleAt = Time; Player.bCircleHeld = false; }
		if (Player.CircleAt >= 0)
		{
			if (!Frame.IsHeld("circle")) { if (!Player.bCircleHeld) SetStance(Player.Stance == TEXT("crouch") ? TEXT("stand") : TEXT("crouch")); Player.CircleAt = -1; }
			else if (!Player.bCircleHeld && Time - Player.CircleAt > .32) { Player.bCircleHeld = true; SetStance(Player.Stance == TEXT("prone") ? TEXT("stand") : TEXT("prone")); }
		}
		if (Frame.WasPressed("prone")) SetStance(Player.Stance == TEXT("prone") ? TEXT("stand") : TEXT("prone"));
		if (Frame.WasPressed("l3")) Player.bRun = !Player.bRun;
		if (Frame.WasPressed("square") && !Photo.bActive && !bMission) Interact();
	}
	// Move.
	const bool bCanMove = !bMission || (MissionCanMove && MissionCanMove());
	double Mx = Frame.Move.X, My = Frame.Move.Y;
	if (Dl > 0) { Mx += FMath::Sin(Time * 1000 / 700) * .55 * Dl; My += FMath::Sin(Time * 1000 / 1100) * .15 * Dl; }
	const double Mag = FMath::Min(1.0, HK::Hypot(Mx, My));
	if (bCanMove && Mag > .05)
	{
		const bool bRunning = Player.Stance == TEXT("stand") && (Player.bRun || (!bMission && !Photo.bActive && Frame.R2 > .3) || Frame.IsHeld("l3"));
		const double Speed = (bRunning ? 3.6 : 1.45) * StanceSpeed() * (bMission && MissionMoveScale ? MissionMoveScale() : 1) * (1 - Dl * .25), Fwd = -My, Right = Mx;
		const double Dx = (-FMath::Sin(Player.Yaw) * Fwd + FMath::Cos(Player.Yaw) * Right) * Speed * Dt, Dz = (-FMath::Cos(Player.Yaw) * Fwd - FMath::Sin(Player.Yaw) * Right) * Speed * Dt;
		const double Bx = Player.X, Bz = Player.Z;
		MovePlayer(Dx, Dz);
		Player.Stride += HK::Hypot(Player.X - Bx, Player.Z - Bz);
		if (Player.Stride > (bRunning ? 1.05 : .72) * (Player.Stance == TEXT("stand") ? 1 : .8)) { Player.Stride = 0; Footstep(bRunning ? 1.2 : Player.Stance == TEXT("stand") ? .8 : .45); }
	}
	else if (!Mag) Player.bRun = false;
	// Jump and stance easing.
	Player.EyeNow += (StanceEye(Player.Stance) - Player.EyeNow) * FMath::Min(1.0, Dt * 9);
	if (Player.JumpV || Player.JumpY)
	{
		Player.JumpV -= 9.81 * Dt;
		Player.JumpY += Player.JumpV * Dt;
		const double Head = FHKData::Get().WalkWorld.CeilingAt(FHKPt(Player.X, Player.Z), Player.Feet) - .1 - Player.EyeNow;
		if (Player.JumpY > Head) { Player.JumpY = FMath::Max(0.0, Head); Player.JumpV = FMath::Min(0.0, Player.JumpV); }
		if (Player.JumpY <= 0 && Player.JumpV <= 0) { if (Player.JumpV < -2) Footstep(1.3); Player.JumpY = 0; Player.JumpV = 0; }
	}
	// Camera.
	FVector Sway = FVector::ZeroVector;   // r, y, p
	if (Dl) Sway = FVector(FMath::Sin(Time * 1000 / 1300) * .09 * Dl, FMath::Sin(Time * 1000 / 1700) * .05 * Dl, FMath::Sin(Time * 1000 / 900) * .03 * Dl);
	CamPos = FVector(Player.X, Player.Feet + Player.EyeNow + Player.JumpY, Player.Z);
	SetCameraFromEuler(Player.Pitch + Sway.Z, Player.Yaw + Sway.Y, Sway.X);
	// "const fov = photo.active && photo.raised > .5 ? 70 / photo.zoom : caseGame?.active ? camera.fov : 70"
	if (!bMission) Fov = Photo.bActive && Photo.Raised > .5 ? 70 / Photo.Zoom : 70;
	else if (FootFov) Fov = FootFov();
}

void AHKGame::UpdateSit(double Dt, double Dl)
{
	const FHKSitSpot* Sp = Player.SitSpot;
	if (!Sp) { Player.Mode = TEXT("foot"); return; }
	if (Frame.WasPressed("square") || Frame.WasPressed("cross")) { StandUp(); return; }
	if ((Frame.R2 > .6 || Frame.Mouse.bLeft) && !Player.bR2Held) { Player.bR2Held = true; DrinkBeer(); }
	if (Frame.R2 < .3 && !Frame.Mouse.bLeft) Player.bR2Held = false;
	auto Curve = [](double V) { return HK::Sign(V) * V * V; };
	Player.LookYaw = HK::Clamp(Player.LookYaw - Frame.Mouse.Dx * .0024 * Sensitivity.Mouse - Curve(Frame.Look.X) * 2.2 * Sensitivity.Pad * Dt, -2, 2);
	Player.LookPitch = HK::Clamp(Player.LookPitch - Frame.Mouse.Dy * .0024 * Sensitivity.Mouse - Curve(Frame.Look.Y) * 1.6 * Sensitivity.Pad * Dt, -1.2, 1.1);
	const double Sway = Dl ? FMath::Sin(Time * 1000 / 1300) * .07 * Dl : 0;
	CamPos = FVector(Sp->X, Sp->Y + .78, Sp->Z);
	SetCameraFromEuler(Player.LookPitch, Sp->Yaw + Player.LookYaw, Sway);
	Fov = 70;
	Player.X = Sp->X; Player.Z = Sp->Z; Player.Feet = Sp->Y - .47; Player.Yaw = Sp->Yaw + Player.LookYaw;
}

void AHKGame::SetCameraFromEuler(double X, double Y, double Z)
{
	CamQuat = HK::EulerToQuat(X, Y, Z, TEXT("YXZ"));
}

void AHKGame::UpdateCamera()
{
	// The web camera looks down its -z; an Unreal camera looks down +X: a fixed -90 degree turn about Unreal's Z.
	static const FQuat CamFix(FVector(0, 0, 1), -UE_DOUBLE_PI / 2);
	if (bCameraOverride) { CamPos = OverridePos; CamQuat = OverrideQuat; Fov = 70; }
	Camera->SetWorldLocationAndRotation(HK::ToUE(CamPos), HK::QuatToUE(CamQuat) * CamFix);
	// three.js fov is vertical; Unreal's is horizontal.
	FVector2D Vp(1920, 1080);
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Vp);
	const double Aspect = Vp.Y > 0 ? Vp.X / Vp.Y : 16.0 / 9.0;
	const double H = 2 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Fov) / 2) * Aspect);
	Camera->SetFieldOfView(FMath::RadiansToDegrees(H));
	FHKUi::SetView(Camera->GetComponentLocation(), Camera->GetComponentRotation(), FMath::RadiansToDegrees(H), Vp);
	FHKAudio::Get().SetListener(CamPos, FMath::Atan2(-CamQuat.RotateVector(FVector(0, 0, -1)).X, -CamQuat.RotateVector(FVector(0, 0, -1)).Z));
}

void AHKGame::UpdateAmbience()
{
	FHKData& Data = FHKData::Get();
	const bool bInApt = InFlat();
	int32 Tw; FVector L;
	const bool bTower = TowerAt(Player.X, Player.Z, Player.Feet, Tw, L);
	FHKAmbience A;
	A.Indoor = bInApt || bTower ? 1 : (Player.Vehicle && Player.Vehicle->Kind == TEXT("car")) ? .7 : (Player.Vehicle && Player.Vehicle->bAircraft) ? .4 : 0;
	A.Green = Data.Greenery(Player.X, Player.Z);
	A.Traffic = Data.TrafficDistance(Player.X, Player.Z);
	A.Height = FMath::Max(0.0, Player.Feet - Data.HeightWorld(Player.X, Player.Z)) + (bInApt ? 10 : 0);
	A.bStair = bTower;
	A.bKitchen = bInApt && RoomAt() == TEXT("kitchen");
	FHKAudio::Get().Ambience(A);
	FHKMusic::Get().Play(MusicTrack ? MusicTrack() : TEXT("main"));
	FHKAudio::Get().Fire(FireLevel(), FMath::Max(0.0, 1 - HK::Hypot(Player.X - FirePos.X, Player.Z - FirePos.Y) / 28));
}

void AHKGame::Begin()
{
	if (bStarted) return;
	bStarted = true;
	FHKAudio::Get().Resume();
	if (OnBegin) OnBegin();
}

void AHKGame::SetPaused(bool bP)
{
	bPaused = bP;
	PauseSel = 0;
	if (InputProc) InputProc->bMouseCaptured = !bP;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->bShowMouseCursor = bP;
		if (bP) PC->SetInputMode(FInputModeGameAndUI()); else PC->SetInputMode(FInputModeGameOnly());
	}
}

// ---------- Settings (saved like the web's localStorage keys) ----------
static FString SettingsPath() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Haarklous"), TEXT("settings.ini")); }

void AHKGame::SetSens(const FString& K, double V)
{
	const double R = HK::JsRound(FMath::Min(3.0, FMath::Max(.25, V)) * 100) / 100;
	if (K == TEXT("pad")) Sensitivity.Pad = R; else Sensitivity.Mouse = R;
	SaveSettings();
}

void AHKGame::SaveSettings() const
{
	FString S = FString::Printf(TEXT("[haarklous]\nlook-sensitivity-pad=%g\nlook-sensitivity-mouse=%g\nquality=%d\nmusic-volume=%g\n"), Sensitivity.Pad, Sensitivity.Mouse, Quality, FHKMusic::Get().GetVolume());
	FFileHelper::SaveStringToFile(S, *SettingsPath());
}

void AHKGame::LoadSettings()
{
	FString S;
	if (!FFileHelper::LoadFileToString(S, *SettingsPath())) return;
	TArray<FString> Lines;
	S.ParseIntoArrayLines(Lines);
	for (const FString& L : Lines)
	{
		FString K, V;
		if (!L.Split(TEXT("="), &K, &V)) continue;
		const double D = FCString::Atod(*V);
		if (K == TEXT("look-sensitivity-pad")) Sensitivity.Pad = D;
		else if (K == TEXT("look-sensitivity-mouse")) Sensitivity.Mouse = D;
		else if (K == TEXT("quality")) Quality = FMath::Clamp((int32)D, 0, 2);
		else if (K == TEXT("music-volume")) FHKMusic::Get().SetVolume(D);
	}
}

void AHKGame::ApplyQuality()
{
	// Web: 0 Fast (no shadows), 1 Balanced, 2 Sharp. Unreal: scalability levels and screen percentage for this laptop.
	if (!GEngine) return;
	const TCHAR* Cmds0[] = {TEXT("sg.ShadowQuality 0"), TEXT("sg.PostProcessQuality 0"), TEXT("sg.EffectsQuality 0"), TEXT("sg.FoliageQuality 0"), TEXT("sg.TextureQuality 1"), TEXT("r.ScreenPercentage 65")};
	const TCHAR* Cmds1[] = {TEXT("sg.ShadowQuality 1"), TEXT("sg.PostProcessQuality 1"), TEXT("sg.EffectsQuality 1"), TEXT("sg.FoliageQuality 1"), TEXT("sg.TextureQuality 2"), TEXT("r.ScreenPercentage 75")};
	const TCHAR* Cmds2[] = {TEXT("sg.ShadowQuality 2"), TEXT("sg.PostProcessQuality 2"), TEXT("sg.EffectsQuality 2"), TEXT("sg.FoliageQuality 2"), TEXT("sg.TextureQuality 2"), TEXT("r.ScreenPercentage 100")};
	const TCHAR** Cmds = Quality == 0 ? Cmds0 : Quality == 2 ? Cmds2 : Cmds1;
	for (int32 i = 0; i < 6; i++) GEngine->Exec(GetWorld(), Cmds[i]);
	ScreenPercentage = Quality == 0 ? 65 : Quality == 2 ? 100 : 75;
}
