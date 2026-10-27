#pragma once

#include "CoreMinimal.h"

/*
 * Web-space maths.
 *
 * All game logic works in the web version's frame so the numbers can be copied 1:1 from the JavaScript:
 * metres, y up, three.js axes (right-handed; the 4th-floor flat floor is y = 0, the street about y = -10).
 * Unreal is centimetres, z up, left-handed. The conversion happens here and nowhere else:
 *   UE.X = web.x * 100,  UE.Y = web.z * 100,  UE.Z = web.y * 100
 * (swapping two axes flips handedness, so shapes are not mirrored). A web rotation (quaternion x,y,z,w)
 * becomes FQuat(-x, -z, -y, w); a web yaw about +y becomes an Unreal yaw of -yaw.
 *
 * FQuat and FVector are used for web-space maths too (same quaternion algebra as three.js);
 * FHKMat is a three.js-style 4x4 matrix (column vectors, column-major storage) for exact ports.
 */
namespace HK
{
	constexpr double Big = 1e9;          // stands in for Infinity in exported data
	constexpr double Pi = UE_DOUBLE_PI;

	inline FVector ToUE(const FVector& W) { return FVector(W.X * 100.0, W.Z * 100.0, W.Y * 100.0); }
	inline FVector FromUE(const FVector& U) { return FVector(U.X * 0.01, U.Z * 0.01, U.Y * 0.01); }
	inline FVector DirToUE(const FVector& W) { return FVector(W.X, W.Z, W.Y); }
	inline FVector DirFromUE(const FVector& U) { return FVector(U.X, U.Z, U.Y); }
	inline FVector ScaleToUE(const FVector& S) { return FVector(S.X, S.Z, S.Y); }
	inline FQuat QuatToUE(const FQuat& Q) { return FQuat(-Q.X, -Q.Z, -Q.Y, Q.W); }
	inline FQuat QuatFromUE(const FQuat& Q) { return FQuat(-Q.X, -Q.Z, -Q.Y, Q.W); }
	HAARKLOUS_API FTransform TransformToUE(const FVector& Pos, const FQuat& Quat, const FVector& Scale);

	// three.js Quaternion.setFromEuler / Euler.setFromRotationMatrix for every rotation order ("XYZ", "YXZ", ...).
	HAARKLOUS_API FQuat EulerToQuat(double X, double Y, double Z, const TCHAR* Order = TEXT("XYZ"));
	HAARKLOUS_API FVector QuatToEuler(const FQuat& Q, const TCHAR* Order = TEXT("XYZ"));
	inline FQuat AxisAngle(const FVector& Axis, double Angle) { return FQuat(Axis.GetSafeNormal(), Angle); }
	// three.js Quaternion.setFromUnitVectors(from, to)
	HAARKLOUS_API FQuat FromUnitVectors(const FVector& From, const FVector& To);
	// three.js Quaternion.slerp (this toward B by t)
	HAARKLOUS_API FQuat Slerp(const FQuat& A, const FQuat& B, double T);
	// Rotation that makes an object look from Eye at Target (three.js Object3D.lookAt; cameras look down their -z).
	HAARKLOUS_API FQuat LookAtQuat(const FVector& Eye, const FVector& Target, const FVector& Up = FVector(0, 1, 0), bool bCamera = true);

	inline double Clamp(double V, double Lo, double Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
	inline double Lerp(double A, double B, double T) { return A + (B - A) * T; }
	inline double Sign(double V) { return V > 0 ? 1.0 : (V < 0 ? -1.0 : 0.0); }
	inline double Hypot(double A, double B) { return FMath::Sqrt(A * A + B * B); }
	inline double Hypot(double A, double B, double C) { return FMath::Sqrt(A * A + B * B + C * C); }
	// JavaScript's % (sign follows the dividend), Math.round (halves toward +infinity).
	inline double JsMod(double A, double B) { return FMath::Fmod(A, B); }
	inline double JsRound(double V) { return FMath::FloorToDouble(V + 0.5); }
	// Shortest signed angle step from A to B scaled by K (the web's angLerp).
	inline double AngLerp(double A, double B, double K) { return A + FMath::Atan2(FMath::Sin(B - A), FMath::Cos(B - A)) * K; }
	// Even-odd point in polygon on the xz plane (the web's inside()).
	HAARKLOUS_API bool InsideXZ(double X, double Z, const TArray<FVector2D>& Pts);
	// Seeded Park-Miller generator used all over the web code: seed = seed * 16807 % 2147483647.
	struct FParkMiller
	{
		double Seed;
		explicit FParkMiller(double InSeed) : Seed(InSeed) {}
		double Next() { Seed = FMath::Fmod(Seed * 16807.0, 2147483647.0); return Seed / 2147483647.0; }
	};
}

/* three.js Matrix4: column vectors, elements stored column-major (E[0..3] = first column). */
struct HAARKLOUS_API FHKMat
{
	double E[16];

	FHKMat() { SetIdentity(); }
	void SetIdentity();
	static FHKMat FromArray(const TArray<double>& A);
	static FHKMat Compose(const FVector& P, const FQuat& Q, const FVector& S);
	void Decompose(FVector& P, FQuat& Q, FVector& S) const;
	FHKMat operator*(const FHKMat& B) const;           // this * B
	FHKMat Inverse() const;
	double Determinant() const;
	FVector TransformPoint(const FVector& V) const;     // applyMatrix4
	FVector TransformDir(const FVector& V) const;       // rotation/scale only
	FVector GetPosition() const { return FVector(E[12], E[13], E[14]); }
	FQuat GetRotation() const;                         // Quaternion.setFromRotationMatrix (scale removed)
};
