#pragma once

#include "CoreMinimal.h"
#include "HKMath.h"

/*
 * A small three.js-style scene graph in web space (metres, y up), so code that moves parts of a model
 * (handlebars, wheels, rotor, boom, door leaves ...) ports line by line from the JavaScript:
 *   this.bars.rotation.y = -bar          ->  Bars->SetRotY(-Bar)
 *   this.group.rotateX(this.pitch)       ->  Group->RotateX(Pitch)
 *   obj.localToWorld(v)                  ->  Obj->LocalToWorld(V)
 * AHKRigActor (HKRigActor.h) shows a node tree with meshes and copies every node's local pose to Unreal each frame
 * (converted with HK::TransformToUE). Rotation (Euler, with its order) and Quaternion are kept in sync like three.js.
 */
class HAARKLOUS_API FHKNode
{
public:
	FString Name;
	FHKNode* Parent = nullptr;
	TArray<FHKNode*> Children;
	FVector Position = FVector::ZeroVector;
	FVector Scale = FVector::OneVector;
	bool bVisible = true;
	FHKMat Matrix, MatrixWorld;
	// Optional visual payload: mesh key (asset name) and colour override, read by AHKRigActor.
	FName MeshKey;
	int32 UserIndex = -1;
	// Interaction flags of exported parts (userData in the web): door id, locked flat door, drink index, label.
	FString DoorId, PartName;
	bool bLockedDoor = false;
	int32 DrinkIndex = -1;

	const FQuat& Quat() const { return Quaternion; }
	const FVector& Rot() const { return Rotation; }
	const FString& Order() const { return EulerOrder; }
	void SetOrder(const FString& InOrder) { EulerOrder = InOrder; Rotation = HK::QuatToEuler(Quaternion, *EulerOrder); }
	void SetQuat(const FQuat& Q) { Quaternion = Q; Rotation = HK::QuatToEuler(Q, *EulerOrder); }
	void SetRotation(double X, double Y, double Z) { Rotation = FVector(X, Y, Z); Quaternion = HK::EulerToQuat(X, Y, Z, *EulerOrder); }
	void SetRotation(double X, double Y, double Z, const FString& InOrder) { EulerOrder = InOrder; SetRotation(X, Y, Z); }
	void SetRotX(double V) { SetRotation(V, Rotation.Y, Rotation.Z); }
	void SetRotY(double V) { SetRotation(Rotation.X, V, Rotation.Z); }
	void SetRotZ(double V) { SetRotation(Rotation.X, Rotation.Y, V); }
	void SetPos(double X, double Y, double Z) { Position = FVector(X, Y, Z); }
	void SetScale(double X, double Y, double Z) { Scale = FVector(X, Y, Z); }

	void RotateOnAxis(const FVector& Axis, double Angle) { SetQuat(Quaternion * HK::AxisAngle(Axis, Angle)); }
	void RotateOnWorldAxis(const FVector& Axis, double Angle) { SetQuat(HK::AxisAngle(Axis, Angle) * Quaternion); }
	void RotateX(double A) { RotateOnAxis(FVector(1, 0, 0), A); }
	void RotateY(double A) { RotateOnAxis(FVector(0, 1, 0), A); }
	void RotateZ(double A) { RotateOnAxis(FVector(0, 0, 1), A); }
	void TranslateOnAxis(const FVector& Axis, double D) { Position += Quaternion.RotateVector(Axis) * D; }
	void TranslateX(double D) { TranslateOnAxis(FVector(1, 0, 0), D); }
	void TranslateY(double D) { TranslateOnAxis(FVector(0, 1, 0), D); }
	void TranslateZ(double D) { TranslateOnAxis(FVector(0, 0, 1), D); }
	// Object3D.lookAt for a parentless object (cameras look down their -z).
	void LookAt(const FVector& Target, bool bCamera = false) { SetQuat(HK::LookAtQuat(Position, Target, FVector(0, 1, 0), bCamera)); }

	void Add(FHKNode* Child);
	void Remove(FHKNode* Child);
	void UpdateMatrix() { Matrix = FHKMat::Compose(Position, Quaternion, Scale); }
	void UpdateMatrixWorld();                                  // this node and all children
	void UpdateWorldMatrix(bool bParents, bool bChildren);     // three.js updateWorldMatrix
	FVector LocalToWorld(const FVector& V) const { return MatrixWorld.TransformPoint(V); }
	FVector WorldToLocal(const FVector& V) const { return MatrixWorld.Inverse().TransformPoint(V); }
	FVector GetWorldPosition() { UpdateWorldMatrix(true, false); return MatrixWorld.GetPosition(); }
	FQuat GetWorldQuaternion() { UpdateWorldMatrix(true, false); return MatrixWorld.GetRotation(); }
	bool IsVisibleInTree() const { for (const FHKNode* N = this; N; N = N->Parent) if (!N->bVisible) return false; return true; }
	void Traverse(TFunctionRef<void(FHKNode*)> Fn) { Fn(this); for (FHKNode* C : Children) C->Traverse(Fn); }

private:
	FQuat Quaternion = FQuat::Identity;
	FVector Rotation = FVector::ZeroVector;
	FString EulerOrder = TEXT("XYZ");
};

/* Owns a tree of nodes (one vehicle, one door set, one prop ...). */
class HAARKLOUS_API FHKRig
{
public:
	FHKRig() { Root = Create(TEXT("root"), nullptr); }
	FHKRig(const FHKRig&) = delete;
	FHKRig& operator=(const FHKRig&) = delete;
	FHKNode* Root = nullptr;
	FHKNode* Create(const FString& Name, FHKNode* Parent);
	FHKNode* Find(const FString& Name) const;
	const TArray<TUniquePtr<FHKNode>>& All() const { return Nodes; }
	// Builds nodes from a rig file written by the asset pipeline (Content/Data/rigs/<name>.json):
	// {"nodes":[{"name","parent","pos":[x,y,z],"quat":[x,y,z,w],"scale":[x,y,z],"visible","mesh"}]}
	bool LoadFromJson(const FString& Path, FString* Error = nullptr);
private:
	TArray<TUniquePtr<FHKNode>> Nodes;
};
