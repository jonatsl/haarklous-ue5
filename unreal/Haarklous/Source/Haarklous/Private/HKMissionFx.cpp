#include "HKMissionFx.h"
#include "HKMission.h"
#include "HKMath.h"
#include "HKRigActor.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

AHKMissionFx::AHKMissionFx()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
	auto MakeLight = [this](const TCHAR* Name) -> UPointLightComponent*
	{
		UPointLightComponent* L = CreateDefaultSubobject<UPointLightComponent>(Name);
		L->SetupAttachment(RootComponent);
		L->SetMobility(EComponentMobility::Movable);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetIntensity(0);
		L->SetCastShadows(false);
		L->bUseInverseSquaredFalloff = true;   // three.js decay 2
		return L;
	};
	FlashLight = MakeLight(TEXT("FlashLight"));
	EnemyFlashLight = MakeLight(TEXT("EnemyFlashLight"));
}

void AHKMissionFx::Bind(FHKMission* InMission, AActor* TickAfter)
{
	Mission = InMission;
	RigsVersion = -1;
	if (TickAfter) AddTickPrerequisiteActor(TickAfter);
	if (Mission)
	{
		FlashLight->SetLightColor(Mission->FlashLight.Color);
		FlashLight->SetAttenuationRadius(Mission->FlashLight.Distance * 100);
		EnemyFlashLight->SetLightColor(Mission->EnemyFlashLight.Color);
		EnemyFlashLight->SetAttenuationRadius(Mission->EnemyFlashLight.Distance * 100);
	}
	// Bullet holes: an engine cylinder flattened to a 22 mm disc, dark grey (CircleGeometry(.011), colour 0x1b1b1b).
	if (!HoleMesh) HoleMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (!HoleMaterial)
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{
			HoleMaterial = UMaterialInstanceDynamic::Create(Base, this);
			HoleMaterial->SetVectorParameterValue(FName("Color"), FLinearColor(FColor(0x1b, 0x1b, 0x1b)));
		}
}

void AHKMissionFx::ClearFigures()
{
	for (AHKRigActor* A : Figures) if (A) A->Destroy();
	Figures.Reset();
}

void AHKMissionFx::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearFigures();
	if (Viewmodel) Viewmodel->Destroy();
	Viewmodel = nullptr;
	Super::EndPlay(Reason);
}

void AHKMissionFx::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Sync();
}

void AHKMissionFx::Sync()
{
	UWorld* World = GetWorld();
	if (!Mission || !World) return;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Figures: rebuilt whenever the mission builds or removes its actors.
	if (RigsVersion != Mission->RigsVersion)
	{
		ClearFigures();
		RigsVersion = Mission->RigsVersion;
		for (const TUniquePtr<FHKMissionActor>& A : Mission->Actors)
		{
			AHKRigActor* R = World->SpawnActor<AHKRigActor>(AHKRigActor::StaticClass(), FTransform::Identity, P);
			if (!R) continue;
			R->bAutoSync = false;
			R->Init(A->Rig.Get(), FHKMission::RigNameOf(*A));
			Figures.Add(R);
		}
	}
	for (AHKRigActor* R : Figures) if (R) R->Sync();
	// Pistol and hands: the rig root is at the camera (FHKMission::Tick), hidden with the whole actor when off.
	if (!Viewmodel)
	{
		Viewmodel = World->SpawnActor<AHKRigActor>(AHKRigActor::StaticClass(), FTransform::Identity, P);
		if (Viewmodel) { Viewmodel->bAutoSync = false; Viewmodel->Init(&Mission->Viewmodel.Rig, TEXT("mission_viewmodel"), false, false); }
	}
	if (Viewmodel)
	{
		Viewmodel->SetActorHiddenInGame(!Mission->ShowViewmodel());
		Viewmodel->Sync();
	}
	// Flash lights.
	FlashLight->SetWorldLocation(HK::ToUE(Mission->FlashLight.Position));
	FlashLight->SetIntensity(Mission->FlashLight.Intensity);
	EnemyFlashLight->SetWorldLocation(HK::ToUE(Mission->EnemyFlashLight.Position));
	EnemyFlashLight->SetIntensity(Mission->EnemyFlashLight.Intensity);
	// Bullet holes: one component per decal serial; gone ones are removed.
	TSet<int32> Live;
	for (const FHKMission::FDecal& D : Mission->Decals) Live.Add(D.Serial);
	for (int32 i = Holes.Num() - 1; i >= 0; i--)
		if (!Live.Contains(HoleSerials[i]))
		{
			if (Holes[i]) Holes[i]->DestroyComponent();
			Holes.RemoveAt(i);
			HoleSerials.RemoveAt(i);
		}
	if (!HoleMesh) return;
	for (const FHKMission::FDecal& D : Mission->Decals)
	{
		if (HoleSerials.Contains(D.Serial)) continue;
		UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this);
		M->SetStaticMesh(HoleMesh);
		if (HoleMaterial) M->SetMaterial(0, HoleMaterial);
		M->SetMobility(EComponentMobility::Movable);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCastShadow(false);
		M->RegisterComponent();
		// mark.position = point + normal * .002, facing along the normal (the cylinder's axis is its local Z).
		const FVector N = HK::DirToUE(D.Normal).GetSafeNormal();
		const FVector At = HK::ToUE(D.Point + D.Normal * .002);
		M->SetWorldTransform(FTransform(FRotationMatrix::MakeFromZ(N).ToQuat(), At, FVector(.022, .022, .0005)));
		if (USceneComponent* Parent = D.Component.Get()) M->AttachToComponent(Parent, FAttachmentTransformRules::KeepWorldTransform);
		Holes.Add(M);
		HoleSerials.Add(D.Serial);
	}
}
