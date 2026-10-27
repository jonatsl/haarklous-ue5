#pragma once

#include "CoreMinimal.h"
#include "HKMath.h"
#include <cmath>

/*
 * three.js maths used by the vehicle and aircraft ports, written out with the same formulas and the same order of
 * operations as three.js r160 (Vector3, Quaternion, Euler), so the C++ simulation follows the web one to the last digits.
 * Private to HKVehicles.cpp / HKAircraft.cpp / their tests.
 */
namespace HKV
{
	// Vector3.length()
	inline double Len(const FVector& V) { return std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z); }
	// Vector3.multiplyScalar / divideScalar (divideScalar(s) is multiplyScalar(1 / s))
	inline FVector Mul(const FVector& V, double S) { return FVector(V.X * S, V.Y * S, V.Z * S); }
	inline FVector Div(const FVector& V, double S) { return Mul(V, 1 / S); }
	// Vector3.normalize(): divideScalar(length() || 1)
	inline FVector Norm(const FVector& V) { const double L = Len(V); return Div(V, L != 0 ? L : 1); }
	// a.addScaledVector(b, s)
	inline FVector AddScaled(const FVector& A, const FVector& B, double S) { return FVector(A.X + B.X * S, A.Y + B.Y * S, A.Z + B.Z * S); }
	// a.lerp(b, t)
	inline FVector Lerp(const FVector& A, const FVector& B, double T) { return FVector(A.X + (B.X - A.X) * T, A.Y + (B.Y - A.Y) * T, A.Z + (B.Z - A.Z) * T); }
	inline double Dot(const FVector& A, const FVector& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
	// Vector3.crossVectors(a, b)
	inline FVector Cross(const FVector& A, const FVector& B)
	{
		return FVector(A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X);
	}
	// Vector3.applyQuaternion(q)
	inline FVector ApplyQ(const FVector& V, const FQuat& Q)
	{
		const double vx = V.X, vy = V.Y, vz = V.Z, qx = Q.X, qy = Q.Y, qz = Q.Z, qw = Q.W;
		const double tx = 2 * (qy * vz - qz * vy), ty = 2 * (qz * vx - qx * vz), tz = 2 * (qx * vy - qy * vx);
		return FVector(vx + qw * tx + qy * tz - qz * ty, vy + qw * ty + qz * tx - qx * tz, vz + qw * tz + qx * ty - qy * tx);
	}
	// Quaternion.multiplyQuaternions(a, b) = a * b
	inline FQuat QMul(const FQuat& A, const FQuat& B)
	{
		const double qax = A.X, qay = A.Y, qaz = A.Z, qaw = A.W, qbx = B.X, qby = B.Y, qbz = B.Z, qbw = B.W;
		return FQuat(qax * qbw + qaw * qbx + qay * qbz - qaz * qby, qay * qbw + qaw * qby + qaz * qbx - qax * qbz,
			qaz * qbw + qaw * qbz + qax * qby - qay * qbx, qaw * qbw - qax * qbx - qay * qby - qaz * qbz);
	}
	// Quaternion.normalize()
	inline FQuat QNorm(const FQuat& Q)
	{
		double L = std::sqrt(Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W);
		if (L == 0) return FQuat(0, 0, 0, 1);
		L = 1 / L;
		return FQuat(Q.X * L, Q.Y * L, Q.Z * L, Q.W * L);
	}
	// Quaternion.invert() (conjugate: unit quaternions)
	inline FQuat QInv(const FQuat& Q) { return FQuat(-Q.X, -Q.Y, -Q.Z, Q.W); }
	// Quaternion.setFromAxisAngle(axis, angle)
	inline FQuat AxisAngle(const FVector& Axis, double Angle)
	{
		const double H = Angle / 2, S = std::sin(H);
		return FQuat(Axis.X * S, Axis.Y * S, Axis.Z * S, std::cos(H));
	}
	// Quaternion.setFromUnitVectors(from, to)
	inline FQuat FromUnitVectors(const FVector& From, const FVector& To)
	{
		double r = Dot(From, To) + 1, x, y, z, w;
		if (r < 2.220446049250313e-16)
		{
			r = 0;
			if (FMath::Abs(From.X) > FMath::Abs(From.Z)) { x = -From.Y; y = From.X; z = 0; w = r; }
			else { x = 0; y = -From.Z; z = From.Y; w = r; }
		}
		else { x = From.Y * To.Z - From.Z * To.Y; y = From.Z * To.X - From.X * To.Z; z = From.X * To.Y - From.Y * To.X; w = r; }
		return QNorm(FQuat(x, y, z, w));
	}
	// new Quaternion().setFromEuler(new Euler(x, y, z, order))
	inline FQuat Euler(double X, double Y, double Z, const TCHAR* Order = TEXT("XYZ")) { return HK::EulerToQuat(X, Y, Z, Order); }
	// new Euler().setFromQuaternion(q, order)
	inline FVector ToEuler(const FQuat& Q, const TCHAR* Order) { return HK::QuatToEuler(Q, Order); }
	// Math.hypot(a, b) as V8 computes it (scaled by the largest value, Kahan summation).
	inline double Hypot(double A, double B)
	{
		const double Abs[2] = {std::fabs(A), std::fabs(B)};
		if (std::isnan(A) || std::isnan(B)) return (std::isinf(A) || std::isinf(B)) ? INFINITY : NAN;
		const double Max = FMath::Max(Abs[0], Abs[1]);
		if (std::isinf(Max)) return INFINITY;
		if (Max == 0) return 0;
		double Sum = 0, Compensation = 0;
		for (int32 i = 0; i < 2; i++)
		{
			const double N = Abs[i] / Max, Summand = N * N - Compensation, Preliminary = Sum + Summand;
			Compensation = (Preliminary - Sum) - Summand;
			Sum = Preliminary;
		}
		return std::sqrt(Sum) * Max;
	}
	// JavaScript Math.round
	inline double JsRound(double V)
	{
		const double F = std::floor(V);
		return (V - F >= .5) ? F + 1 : F;
	}
	// Math.round(v * k) / k (netGet)
	inline double RoundTo(double V, double K) { return JsRound(V * K) / K; }
	inline double Sign(double V) { return V > 0 ? 1.0 : (V < 0 ? -1.0 : V); }
	inline double Clamp(double V, double Lo, double Hi) { return FMath::Max(Lo, FMath::Min(Hi, V)); }
	// Number.prototype.toFixed(digits) for the HUD strings (ties round away from zero like JavaScript, -0 prints as 0).
	inline FString ToFixed(double V, int32 Digits)
	{
		if (V == 0) V = 0;
		FString S = FString::Printf(TEXT("%.*f"), Digits, V);
		const double P = std::pow(10.0, Digits), Scaled = std::fabs(V) * P, Fl = std::floor(Scaled);
		if (Scaled - Fl == .5 && std::fabs(V) == (Fl + .5) / P)
			S = FString::Printf(TEXT("%.*f"), Digits, (V < 0 ? -1 : 1) * (Fl + 1) / P);
		if (S.StartsWith(TEXT("-")))
		{
			// "-0.0" only when the number really is negative (JavaScript keeps the sign for -0.04.toFixed(1)).
			if (!(V < 0)) S.RemoveAt(0);
		}
		return S;
	}
}
