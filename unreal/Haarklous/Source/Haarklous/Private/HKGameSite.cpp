#include "HKGame.h"
#include "HKData.h"
#include "HKMath.h"
#include "HKSite.h"
#include "HKSiteVehicles.h"
#include "HKRigActor.h"
#include "Haarklous.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

/*
 * The construction site (new feature, asked for by the user on 28 Oct 2026; not in the web game): soil, site
 * furniture, the machines (HKSiteVehicles) and the crane loads. Offline and in online games between Unreal players;
 * web players do not see it (they ignore vehicle numbers they do not have).
 */
void AHKGame::SetupSite()
{
	Site = MakeUnique<FHKDirtField>();
	if (!Site->Load(FPaths::Combine(FHKData::DataDir(), TEXT("site.json"))))
	{
		UE_LOG(LogHaarklous, Warning, TEXT("No construction site (site.json missing or bad)"));
		Site.Reset();
		return;
	}
	FHKDirtField* Field = Site.Get();
	FHKData::Get().SiteGround = [Field](double X, double Z, double& Y) { return Field->Surface(X, Z, Y); };
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SiteActor = GetWorld()->SpawnActor<AHKSiteActor>(AHKSiteActor::StaticClass(), FTransform::Identity, SP);
	SiteActor->Init(Field);
	// Furniture: fence panels, site offices, toilet, signs, pallets, pipes, a skip, floodlight masts.
	const FHKData& D = FHKData::Get();
	for (const FHKDirtField::FPlaced& P : Site->Props)
	{
		const FString Name = TEXT("SM_Site_") + P.Kind;
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Haarklous/Meshes/site/%s.%s"), *Name, *Name));
		if (!M) { UE_LOG(LogHaarklous, Warning, TEXT("Site prop mesh %s missing"), *Name); continue; }
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(M);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCullDistance(P.Kind == TEXT("fence") ? 15000 : 30000);
		C->SetCastShadow(P.Kind != TEXT("sign"));
		C->RegisterComponent();
		const FVector Scale(P.Kind == TEXT("fence") && P.Len > 0 ? P.Len / 3.5 : 1, 1, 1);
		const FQuat Q = HK::AxisAngle(FVector(0, 1, 0), P.Yaw);
		C->SetWorldTransform(HK::TransformToUE(FVector(P.X, D.GroundWorld(P.X, P.Z) - .02, P.Z), Q, Scale));
		SiteProps.Add(C);
	}
	// Machines: after the web's vehicles (web players ignore numbers they do not have).
	FHKSiteMachine::Site = Field;
	TArray<TUniquePtr<FHKVehicle>> Machines;
	HKSiteVehicles::CreateAll(*Field, Machines);
	for (TUniquePtr<FHKVehicle>& V : Machines)
	{
		FHKSiteMachine* M = static_cast<FHKSiteMachine*>(V.Get());
		M->OnSpill = [this](double X, double Z, double Vol) { SpillSoil(X, Z, Vol); };
		AHKRigActor* A = GetWorld()->SpawnActor<AHKRigActor>(AHKRigActor::StaticClass(), FTransform::Identity, SP);
		A->Init(&V->Rig, V->RigFile);
		A->SetCullDistance(V->Kind == TEXT("tower") ? 150000 : 30000);
		VehicleList.Add(V.Get());
		VehicleActors.Add(A);
		Vehicles.Add(MoveTemp(V));
	}
	// Crane loads: brick pallets, a bundle of steel beams, a concrete skip.
	for (const FHKDirtField::FPlaced& P : Site->Loads)
	{
		TUniquePtr<FHKSiteLoad> L = MakeUnique<FHKSiteLoad>();
		L->Kind = P.Kind; L->Yaw = P.Yaw;
		L->P = FVector(P.X, D.GroundWorld(P.X, P.Z), P.Z);
		L->Mass = P.Kind == TEXT("beams") ? 2400 : P.Kind == TEXT("skip") ? 900 : 1100;
		L->Height = P.Kind == TEXT("beams") ? .65 : P.Kind == TEXT("skip") ? 1.5 : 1.15;
		const FString Name = TEXT("SM_Load_") + P.Kind;
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Haarklous/Meshes/site/%s.%s"), *Name, *Name));
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		if (M) C->SetStaticMesh(M);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCullDistance(30000);
		C->RegisterComponent();
		FHKSiteMachine::Loads().Add(L.Get());
		SiteLoadMeshes.Add(C);
		SiteLoads.Add(MoveTemp(L));
	}
	SyncSiteLoads();
}

void AHKGame::SpillSoil(double X, double Z, double Volume)
{
	// Soil put down outside the site: a heap like the web excavator's (a cone of that volume).
	SpilledSoil += Volume;
	if (SpilledSoil < .4) return;
	const double R = FMath::Pow(SpilledSoil * 3 / (PI * .7), 1.0 / 3);
	AddDirtPile(X, Z, FMath::Clamp(R, .4, 2.2));
	SpilledSoil = 0;
}

void AHKGame::SyncSiteLoads()
{
	for (int32 i = 0; i < SiteLoads.Num(); i++)
	{
		const FHKSiteLoad& L = *SiteLoads[i];
		SiteLoadMeshes[i]->SetWorldTransform(HK::TransformToUE(L.P, HK::AxisAngle(FVector(0, 1, 0), L.Yaw), FVector(1, 1, 1)));
	}
}
