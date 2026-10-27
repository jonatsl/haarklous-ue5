#include "HKRigActor.h"
#include "HKNode.h"
#include "HKMath.h"
#include "Haarklous.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

AHKRigActor::AHKRigActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

void AHKRigActor::Init(FHKRig* InRig, const FString& InRigName, bool bSkipRootMesh, bool bCastShadows)
{
	Rig = InRig;
	RigName = InRigName;
	if (!Rig) return;
	TMap<const FHKNode*, USceneComponent*> CompOf;
	// Nodes in creation order: parents come before children.
	for (const TUniquePtr<FHKNode>& Ptr : Rig->All())
	{
		FHKNode* N = Ptr.Get();
		USceneComponent* Parent = N->Parent ? CompOf.FindRef(N->Parent) : RootComponent.Get();
		if (!Parent) Parent = RootComponent;
		USceneComponent* C = nullptr;
		const bool bWantMesh = !N->MeshKey.IsNone() && !(bSkipRootMesh && N == Rig->Root);
		UStaticMesh* Mesh = bWantMesh ? LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Haarklous/Meshes/%s/%s.%s"), *RigName, *N->MeshKey.ToString(), *N->MeshKey.ToString())) : nullptr;
		if (bWantMesh && !Mesh) UE_LOG(LogHaarklous, Warning, TEXT("Rig %s: mesh %s missing"), *RigName, *N->MeshKey.ToString());
		if (Mesh)
		{
			UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this);
			M->SetStaticMesh(Mesh);
			M->SetMobility(EComponentMobility::Movable);
			M->SetCastShadow(bCastShadows);
			M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			NodeByComp.Add(M, N);
			C = M;
		}
		else C = NewObject<USceneComponent>(this);
		C->SetMobility(EComponentMobility::Movable);
		C->SetupAttachment(N == Rig->Root ? RootComponent.Get() : Parent);
		C->RegisterComponent();
		CompOf.Add(N, C);
		Comps.Add(C);
		Nodes.Add(N);
	}
	Sync();
}

void AHKRigActor::SetAllCollision(bool bTrace)
{
	for (USceneComponent* C : Comps)
		if (UStaticMeshComponent* M = Cast<UStaticMeshComponent>(C))
		{
			M->SetCollisionEnabled(bTrace ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
			M->SetCollisionResponseToAllChannels(ECR_Ignore);
			if (bTrace) { M->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block); M->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block); }
		}
}

void AHKRigActor::SetCullDistance(float Cm)
{
	for (USceneComponent* C : Comps)
		if (UStaticMeshComponent* M = Cast<UStaticMeshComponent>(C)) M->SetCullDistance(Cm);
}

void AHKRigActor::Sync()
{
	for (int32 i = 0; i < Nodes.Num(); i++)
	{
		const FHKNode* N = Nodes[i];
		USceneComponent* C = Comps[i];
		const FTransform T = HK::TransformToUE(N->Position, N->Quat(), N->Scale);
		if (!C->GetRelativeTransform().Equals(T, 1e-4)) C->SetRelativeTransform(T);
		if (C->IsVisible() != N->bVisible) C->SetVisibility(N->bVisible, false);
	}
}

void AHKRigActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bAutoSync) Sync();
}

void AHKRigActor::SetPaint(const FLinearColor& Color)
{
	if (PaintMids.Num() == 0)
	{
		for (USceneComponent* C : Comps)
			if (UStaticMeshComponent* M = Cast<UStaticMeshComponent>(C))
				for (int32 s = 0; s < M->GetNumMaterials(); s++)
				{
					UMaterialInterface* Mat = M->GetMaterial(s);
					// Car and motorcycle body paint: the clear-coated physical materials (not glass).
					if (Mat && Mat->GetName().StartsWith(TEXT("MI_Physical")) && !Mat->GetName().Contains(TEXT("glass")))
					{
						float Opacity = 1;
						Mat->GetScalarParameterValue(FName("Opacity"), Opacity);
						if (Opacity < .99f) continue;
						UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Mat, this);
						M->SetMaterial(s, Mid);
						PaintMids.Add(Mid);
					}
				}
	}
	for (UMaterialInstanceDynamic* Mid : PaintMids) Mid->SetVectorParameterValue(FName("Color"), Color);
}

UStaticMeshComponent* AHKRigActor::MeshOf(const FHKNode* Node) const
{
	const int32 i = Nodes.IndexOfByKey(Node);
	return i >= 0 ? Cast<UStaticMeshComponent>(Comps[i]) : nullptr;
}

FHKNode* AHKRigActor::NodeOfComponent(const UPrimitiveComponent* C) const
{
	FHKNode* const* N = NodeByComp.Find(C);
	return N ? *N : nullptr;
}
