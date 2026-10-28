#include "HKRigActor.h"
#include "HKNode.h"
#include "HKMath.h"
#include "Haarklous.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
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
	InitPerson();
	Sync();
}

void AHKRigActor::InitPerson()
{
	if (!RigName.StartsWith(TEXT("avatar_")) || RigName == TEXT("avatar_drone")) return;
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/Haarklous/People/%s/SK_%s.SK_%s"), *RigName, *RigName, *RigName));
	if (!Mesh) return;
	// The rig's joints and the bones they turn (parents first).
	static const TCHAR* Map[][2] = {
		{TEXT("hips"), TEXT("pelvis")}, {TEXT("torso"), TEXT("spine_01")}, {TEXT("neck"), TEXT("neck_01")}, {TEXT("head"), TEXT("head")},
		{TEXT("L.arm.sh"), TEXT("upperarm_l")}, {TEXT("L.arm.el"), TEXT("lowerarm_l")}, {TEXT("R.arm.sh"), TEXT("upperarm_r")}, {TEXT("R.arm.el"), TEXT("lowerarm_r")},
		{TEXT("L.leg.hip"), TEXT("thigh_l")}, {TEXT("L.leg.kn"), TEXT("calf_l")}, {TEXT("R.leg.hip"), TEXT("thigh_r")}, {TEXT("R.leg.kn"), TEXT("calf_r")}};
	// Component-space reference pose (the figure's rest pose matches the rig with every joint at zero rotation).
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	TArray<FTransform> Cs;
	Cs.SetNum(Ref.GetNum());
	for (int32 i = 0; i < Ref.GetNum(); i++)
	{
		const int32 P = Ref.GetParentIndex(i);
		Cs[i] = P >= 0 ? Ref.GetRefBonePose()[i] * Cs[P] : Ref.GetRefBonePose()[i];
	}
	auto At = [&](const TCHAR* B) { const int32 i = Ref.FindBoneIndex(B); return i >= 0 ? Cs[i].GetLocation() : FVector::ZeroVector; };
	// The figure's up (pelvis to head) and forward (heels to toes) -> the rig's up and forward (web +y and -z).
	const FVector Up = (At(TEXT("head")) - At(TEXT("pelvis"))).GetSafeNormal();
	FVector Fwd = (At(TEXT("ball_l")) - At(TEXT("foot_l"))) + (At(TEXT("ball_r")) - At(TEXT("foot_r")));
	Fwd = (Fwd - Up * FVector::DotProduct(Fwd, Up)).GetSafeNormal();
	const FQuat Qc = FRotationMatrix::MakeFromXZ(Fwd, Up).ToQuat();
	const FQuat Qt = FRotationMatrix::MakeFromXZ(HK::DirToUE(FVector(0, 0, -1)), HK::DirToUE(FVector(0, 1, 0))).ToQuat();
	PersonFix = Qt * Qc.Inverse();
	RootIndex = Nodes.IndexOfByKey(Rig->Root);
	for (const auto& M : Map)
	{
		const int32 Ni = Nodes.IndexOfByPredicate([&](const FHKNode* N) { return N->Name == M[0]; });
		const int32 Bi = Ref.FindBoneIndex(M[1]);
		if (Ni < 0 || Bi < 0) { UE_LOG(LogHaarklous, Warning, TEXT("Person %s: no joint %s or bone %s"), *RigName, M[0], M[1]); return; }
		PersonBones.Add({FName(M[1]), Ni, Cs[Bi].GetRotation()});
		HiddenNodes.Add(Ni);
	}
	HipsIndex = PersonBones[0].Node;
	HipsRest = HK::ToUE(Nodes[HipsIndex]->Position);
	PelvisRest = At(TEXT("pelvis"));
	Person = NewObject<UPoseableMeshComponent>(this);
	Person->SetSkinnedAssetAndUpdate(Mesh);
	Person->SetMobility(EComponentMobility::Movable);
	Person->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Person->SetupAttachment(Comps[RootIndex]);
	Person->SetRelativeRotation(PersonFix);
	Person->RegisterComponent();
}

void AHKRigActor::SyncPerson()
{
	const USceneComponent* RootC = Comps[RootIndex];
	const bool bShow = Nodes[RootIndex]->bVisible && Nodes[HipsIndex]->bVisible;
	if (Person->IsVisible() != bShow) Person->SetVisibility(bShow);
	if (!bShow) return;
	// Each joint's rotation in the rig's frame, turned into the figure's component space, applied to the bone's rest.
	const FQuat ToRig = RootC->GetComponentQuat().Inverse(), Inv = PersonFix.Inverse();
	for (const FPersonBone& B : PersonBones)
	{
		const FQuat Q = Inv * (ToRig * Comps[B.Node]->GetComponentQuat()) * PersonFix;
		Person->SetBoneRotationByName(B.Bone, (Q * B.Rest).Rotator(), EBoneSpaces::ComponentSpace);
	}
	// The hips move (crouching, sitting, the walk's bob): the pelvis moves with them.
	const FVector Hips = RootC->GetComponentTransform().InverseTransformPosition(Comps[HipsIndex]->GetComponentLocation());
	Person->SetBoneLocationByName(PersonBones[0].Bone, PelvisRest + Inv.RotateVector(Hips - HipsRest), EBoneSpaces::ComponentSpace);
}

USceneComponent* AHKRigActor::NodeComponent(const FString& NodeName) const
{
	for (int32 i = 0; i < Nodes.Num(); i++) if (Nodes[i]->Name == NodeName) return Comps[i];
	return nullptr;
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
	if (Person) Person->SetCullDistance(Cm);
}

void AHKRigActor::Sync()
{
	for (int32 i = 0; i < Nodes.Num(); i++)
	{
		const FHKNode* N = Nodes[i];
		USceneComponent* C = Comps[i];
		const FTransform T = HK::TransformToUE(N->Position, N->Quat(), N->Scale);
		if (!C->GetRelativeTransform().Equals(T, 1e-4)) C->SetRelativeTransform(T);
		const bool bVisible = N->bVisible && !HiddenNodes.Contains(i);
		if (C->IsVisible() != bVisible) C->SetVisibility(bVisible, false);
	}
	if (Person) SyncPerson();
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
