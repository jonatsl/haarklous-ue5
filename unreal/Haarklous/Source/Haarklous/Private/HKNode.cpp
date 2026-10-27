#include "HKNode.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void FHKNode::Add(FHKNode* Child)
{
	if (!Child || Child == this) return;
	if (Child->Parent) Child->Parent->Remove(Child);
	Child->Parent = this;
	Children.Add(Child);
}

void FHKNode::Remove(FHKNode* Child)
{
	if (Children.Remove(Child) > 0) Child->Parent = nullptr;
}

void FHKNode::UpdateMatrixWorld()
{
	UpdateMatrix();
	MatrixWorld = Parent ? Parent->MatrixWorld * Matrix : Matrix;
	for (FHKNode* C : Children) C->UpdateMatrixWorld();
}

void FHKNode::UpdateWorldMatrix(bool bParents, bool bChildren)
{
	if (bParents && Parent) Parent->UpdateWorldMatrix(true, false);
	UpdateMatrix();
	MatrixWorld = Parent ? Parent->MatrixWorld * Matrix : Matrix;
	if (bChildren) for (FHKNode* C : Children) C->UpdateWorldMatrix(false, true);
}

FHKNode* FHKRig::Create(const FString& Name, FHKNode* Parent)
{
	TUniquePtr<FHKNode> N = MakeUnique<FHKNode>();
	N->Name = Name;
	FHKNode* Raw = N.Get();
	Nodes.Add(MoveTemp(N));
	if (Parent) Parent->Add(Raw);
	return Raw;
}

FHKNode* FHKRig::Find(const FString& Name) const
{
	for (const TUniquePtr<FHKNode>& N : Nodes) if (N->Name == Name) return N.Get();
	return nullptr;
}

static FVector JsonVec(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FVector& Default)
{
	const TArray<TSharedPtr<FJsonValue>>* A;
	if (!O->TryGetArrayField(Key, A) || A->Num() < 3) return Default;
	return FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
}

bool FHKRig::LoadFromJson(const FString& Path, FString* Error)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path)) { if (Error) *Error = TEXT("missing ") + Path; return false; }
	TSharedPtr<FJsonObject> J;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) || !J.IsValid()) { if (Error) *Error = TEXT("bad JSON ") + Path; return false; }
	const TArray<TSharedPtr<FJsonValue>>* List;
	if (!J->TryGetArrayField(TEXT("nodes"), List)) { if (Error) *Error = TEXT("no nodes in ") + Path; return false; }
	TArray<FHKNode*> Made;
	for (int32 i = 0; i < List->Num(); i++)
	{
		const TSharedPtr<FJsonObject> O = (*List)[i]->AsObject();
		const int32 P = O->HasField(TEXT("parent")) ? (int32)O->GetNumberField(TEXT("parent")) : -1;
		FHKNode* N = (i == 0) ? Root : Create(O->GetStringField(TEXT("name")), (P >= 0 && P < Made.Num()) ? Made[P] : Root);
		if (i == 0) N->Name = O->GetStringField(TEXT("name"));
		N->Position = JsonVec(O, TEXT("pos"), FVector::ZeroVector);
		const TArray<TSharedPtr<FJsonValue>>* Q;
		if (O->TryGetArrayField(TEXT("quat"), Q) && Q->Num() == 4) N->SetQuat(FQuat((*Q)[0]->AsNumber(), (*Q)[1]->AsNumber(), (*Q)[2]->AsNumber(), (*Q)[3]->AsNumber()));
		N->Scale = JsonVec(O, TEXT("scale"), FVector::OneVector);
		bool bVis = true;
		if (O->TryGetBoolField(TEXT("visible"), bVis)) N->bVisible = bVis;
		FString Mesh;
		if (O->TryGetStringField(TEXT("mesh"), Mesh) && !Mesh.IsEmpty()) N->MeshKey = FName(*Mesh);
		Made.Add(N);
	}
	Root->UpdateMatrixWorld();
	return true;
}
