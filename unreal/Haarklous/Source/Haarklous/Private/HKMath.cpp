#include "HKMath.h"

FTransform HK::TransformToUE(const FVector& Pos, const FQuat& Quat, const FVector& Scale)
{
	return FTransform(QuatToUE(Quat), ToUE(Pos), ScaleToUE(Scale));
}

FQuat HK::EulerToQuat(double X, double Y, double Z, const TCHAR* Order)
{
	const double c1 = FMath::Cos(X / 2), c2 = FMath::Cos(Y / 2), c3 = FMath::Cos(Z / 2);
	const double s1 = FMath::Sin(X / 2), s2 = FMath::Sin(Y / 2), s3 = FMath::Sin(Z / 2);
	double x, y, z, w;
	const FString O(Order);
	if (O == TEXT("YXZ")) { x = s1 * c2 * c3 + c1 * s2 * s3; y = c1 * s2 * c3 - s1 * c2 * s3; z = c1 * c2 * s3 - s1 * s2 * c3; w = c1 * c2 * c3 + s1 * s2 * s3; }
	else if (O == TEXT("ZXY")) { x = s1 * c2 * c3 - c1 * s2 * s3; y = c1 * s2 * c3 + s1 * c2 * s3; z = c1 * c2 * s3 + s1 * s2 * c3; w = c1 * c2 * c3 - s1 * s2 * s3; }
	else if (O == TEXT("ZYX")) { x = s1 * c2 * c3 - c1 * s2 * s3; y = c1 * s2 * c3 + s1 * c2 * s3; z = c1 * c2 * s3 - s1 * s2 * c3; w = c1 * c2 * c3 + s1 * s2 * s3; }
	else if (O == TEXT("YZX")) { x = s1 * c2 * c3 + c1 * s2 * s3; y = c1 * s2 * c3 + s1 * c2 * s3; z = c1 * c2 * s3 - s1 * s2 * c3; w = c1 * c2 * c3 - s1 * s2 * s3; }
	else if (O == TEXT("XZY")) { x = s1 * c2 * c3 - c1 * s2 * s3; y = c1 * s2 * c3 - s1 * c2 * s3; z = c1 * c2 * s3 + s1 * s2 * c3; w = c1 * c2 * c3 + s1 * s2 * s3; }
	else { x = s1 * c2 * c3 + c1 * s2 * s3; y = c1 * s2 * c3 - s1 * c2 * s3; z = c1 * c2 * s3 + s1 * s2 * c3; w = c1 * c2 * c3 - s1 * s2 * s3; } // XYZ
	return FQuat(x, y, z, w);
}

FVector HK::QuatToEuler(const FQuat& Q, const TCHAR* Order)
{
	const FHKMat M = FHKMat::Compose(FVector::ZeroVector, Q, FVector::OneVector);
	const double* te = M.E;
	const double m11 = te[0], m12 = te[4], m13 = te[8], m21 = te[1], m22 = te[5], m23 = te[9], m31 = te[2], m32 = te[6], m33 = te[10];
	const double L = 0.9999999;
	double x = 0, y = 0, z = 0;
	const FString O(Order);
	if (O == TEXT("YXZ")) { x = FMath::Asin(-Clamp(m23, -1, 1)); if (FMath::Abs(m23) < L) { y = FMath::Atan2(m13, m33); z = FMath::Atan2(m21, m22); } else { y = FMath::Atan2(-m31, m11); z = 0; } }
	else if (O == TEXT("ZXY")) { x = FMath::Asin(Clamp(m32, -1, 1)); if (FMath::Abs(m32) < L) { y = FMath::Atan2(-m31, m33); z = FMath::Atan2(-m12, m22); } else { y = 0; z = FMath::Atan2(m21, m11); } }
	else if (O == TEXT("ZYX")) { y = FMath::Asin(-Clamp(m31, -1, 1)); if (FMath::Abs(m31) < L) { x = FMath::Atan2(m32, m33); z = FMath::Atan2(m21, m11); } else { x = 0; z = FMath::Atan2(-m12, m22); } }
	else if (O == TEXT("YZX")) { z = FMath::Asin(Clamp(m21, -1, 1)); if (FMath::Abs(m21) < L) { x = FMath::Atan2(-m23, m22); y = FMath::Atan2(-m31, m11); } else { x = 0; y = FMath::Atan2(m13, m33); } }
	else if (O == TEXT("XZY")) { z = FMath::Asin(-Clamp(m12, -1, 1)); if (FMath::Abs(m12) < L) { x = FMath::Atan2(m32, m22); y = FMath::Atan2(m13, m11); } else { x = FMath::Atan2(-m23, m33); y = 0; } }
	else { y = FMath::Asin(Clamp(m13, -1, 1)); if (FMath::Abs(m13) < L) { x = FMath::Atan2(-m23, m33); z = FMath::Atan2(-m12, m11); } else { x = FMath::Atan2(m32, m22); z = 0; } }
	return FVector(x, y, z);
}

FQuat HK::FromUnitVectors(const FVector& From, const FVector& To)
{
	double r = FVector::DotProduct(From, To) + 1;
	double x, y, z, w;
	if (r < 2.220446049250313e-16)
	{
		r = 0;
		if (FMath::Abs(From.X) > FMath::Abs(From.Z)) { x = -From.Y; y = From.X; z = 0; w = r; }
		else { x = 0; y = -From.Z; z = From.Y; w = r; }
	}
	else
	{
		x = From.Y * To.Z - From.Z * To.Y; y = From.Z * To.X - From.X * To.Z; z = From.X * To.Y - From.Y * To.X; w = r;
	}
	FQuat Q(x, y, z, w);
	const double L = FMath::Sqrt(x * x + y * y + z * z + w * w);
	return L > 0 ? FQuat(x / L, y / L, z / L, w / L) : FQuat::Identity;
}

FQuat HK::Slerp(const FQuat& A, const FQuat& B, double T)
{
	if (T == 0) return A;
	if (T == 1) return B;
	const double x = A.X, y = A.Y, z = A.Z, w = A.W;
	double cosHalf = w * B.W + x * B.X + y * B.Y + z * B.Z;
	FQuat R = B;
	if (cosHalf < 0) { R = FQuat(-B.X, -B.Y, -B.Z, -B.W); cosHalf = -cosHalf; }
	if (cosHalf >= 1.0) return A;
	const double sqrSin = 1.0 - cosHalf * cosHalf;
	if (sqrSin <= 2.220446049250313e-16)
	{
		const double s = 1 - T;
		FQuat Q(s * x + T * R.X, s * y + T * R.Y, s * z + T * R.Z, s * w + T * R.W);
		Q.Normalize();
		return Q;
	}
	const double sinHalf = FMath::Sqrt(sqrSin), halfTheta = FMath::Atan2(sinHalf, cosHalf);
	const double ra = FMath::Sin((1 - T) * halfTheta) / sinHalf, rb = FMath::Sin(T * halfTheta) / sinHalf;
	return FQuat(x * ra + R.X * rb, y * ra + R.Y * rb, z * ra + R.Z * rb, w * ra + R.W * rb);
}

FQuat HK::LookAtQuat(const FVector& Eye, const FVector& Target, const FVector& Up, bool bCamera)
{
	// Matrix4.lookAt(eye, target, up) for cameras, (target, eye, up) for other objects.
	FVector A = bCamera ? Eye : Target, B = bCamera ? Target : Eye;
	FVector Zv = A - B;
	if (Zv.SizeSquared() == 0) Zv.Z = 1;
	Zv.Normalize();
	FVector Xv = FVector::CrossProduct(Up, Zv);
	if (Xv.SizeSquared() == 0)
	{
		if (FMath::Abs(Up.Z) == 1) Zv.X += 0.0001; else Zv.Z += 0.0001;
		Zv.Normalize();
		Xv = FVector::CrossProduct(Up, Zv);
	}
	Xv.Normalize();
	const FVector Yv = FVector::CrossProduct(Zv, Xv);
	FHKMat M;
	M.E[0] = Xv.X; M.E[4] = Yv.X; M.E[8] = Zv.X;
	M.E[1] = Xv.Y; M.E[5] = Yv.Y; M.E[9] = Zv.Y;
	M.E[2] = Xv.Z; M.E[6] = Yv.Z; M.E[10] = Zv.Z;
	return M.GetRotation();
}

bool HK::InsideXZ(double X, double Z, const TArray<FVector2D>& Pts)
{
	bool R = false;
	const int32 N = Pts.Num();
	for (int32 i = 0, j = N - 1; i < N; j = i++)
	{
		const FVector2D& A = Pts[i];
		const FVector2D& B = Pts[j];
		if ((A.Y > Z) != (B.Y > Z) && X < (B.X - A.X) * (Z - A.Y) / (B.Y - A.Y) + A.X) R = !R;
	}
	return R;
}

void FHKMat::SetIdentity()
{
	for (int32 i = 0; i < 16; i++) E[i] = (i % 5 == 0) ? 1.0 : 0.0;
}

FHKMat FHKMat::FromArray(const TArray<double>& A)
{
	FHKMat M;
	for (int32 i = 0; i < 16 && i < A.Num(); i++) M.E[i] = A[i];
	return M;
}

FHKMat FHKMat::Compose(const FVector& P, const FQuat& Q, const FVector& S)
{
	FHKMat M;
	double* te = M.E;
	const double x = Q.X, y = Q.Y, z = Q.Z, w = Q.W;
	const double x2 = x + x, y2 = y + y, z2 = z + z;
	const double xx = x * x2, xy = x * y2, xz = x * z2, yy = y * y2, yz = y * z2, zz = z * z2, wx = w * x2, wy = w * y2, wz = w * z2;
	te[0] = (1 - (yy + zz)) * S.X; te[1] = (xy + wz) * S.X; te[2] = (xz - wy) * S.X; te[3] = 0;
	te[4] = (xy - wz) * S.Y; te[5] = (1 - (xx + zz)) * S.Y; te[6] = (yz + wx) * S.Y; te[7] = 0;
	te[8] = (xz + wy) * S.Z; te[9] = (yz - wx) * S.Z; te[10] = (1 - (xx + yy)) * S.Z; te[11] = 0;
	te[12] = P.X; te[13] = P.Y; te[14] = P.Z; te[15] = 1;
	return M;
}

FQuat FHKMat::GetRotation() const
{
	// Quaternion.setFromRotationMatrix on the matrix with its scale removed.
	const double sx = FVector(E[0], E[1], E[2]).Size(), sy = FVector(E[4], E[5], E[6]).Size(), sz = FVector(E[8], E[9], E[10]).Size();
	const double ix = sx > 0 ? 1 / sx : 0, iy = sy > 0 ? 1 / sy : 0, iz = sz > 0 ? 1 / sz : 0;
	const double m11 = E[0] * ix, m21 = E[1] * ix, m31 = E[2] * ix, m12 = E[4] * iy, m22 = E[5] * iy, m32 = E[6] * iy, m13 = E[8] * iz, m23 = E[9] * iz, m33 = E[10] * iz;
	const double trace = m11 + m22 + m33;
	double x, y, z, w;
	if (trace > 0) { const double s = 0.5 / FMath::Sqrt(trace + 1.0); w = 0.25 / s; x = (m32 - m23) * s; y = (m13 - m31) * s; z = (m21 - m12) * s; }
	else if (m11 > m22 && m11 > m33) { const double s = 2.0 * FMath::Sqrt(1.0 + m11 - m22 - m33); w = (m32 - m23) / s; x = 0.25 * s; y = (m12 + m21) / s; z = (m13 + m31) / s; }
	else if (m22 > m33) { const double s = 2.0 * FMath::Sqrt(1.0 + m22 - m11 - m33); w = (m13 - m31) / s; x = (m12 + m21) / s; y = 0.25 * s; z = (m23 + m32) / s; }
	else { const double s = 2.0 * FMath::Sqrt(1.0 + m33 - m11 - m22); w = (m21 - m12) / s; x = (m13 + m31) / s; y = (m23 + m32) / s; z = 0.25 * s; }
	return FQuat(x, y, z, w);
}

void FHKMat::Decompose(FVector& P, FQuat& Q, FVector& S) const
{
	double sx = FVector(E[0], E[1], E[2]).Size();
	const double sy = FVector(E[4], E[5], E[6]).Size(), sz = FVector(E[8], E[9], E[10]).Size();
	if (Determinant() < 0) sx = -sx;
	P = FVector(E[12], E[13], E[14]);
	FHKMat R = *this;
	const double ix = 1 / sx, iy = 1 / sy, iz = 1 / sz;
	R.E[0] *= ix; R.E[1] *= ix; R.E[2] *= ix;
	R.E[4] *= iy; R.E[5] *= iy; R.E[6] *= iy;
	R.E[8] *= iz; R.E[9] *= iz; R.E[10] *= iz;
	Q = R.GetRotation();
	S = FVector(sx, sy, sz);
}

FHKMat FHKMat::operator*(const FHKMat& B) const
{
	FHKMat C;
	for (int32 col = 0; col < 4; col++)
		for (int32 row = 0; row < 4; row++)
		{
			double s = 0;
			for (int32 k = 0; k < 4; k++) s += E[k * 4 + row] * B.E[col * 4 + k];
			C.E[col * 4 + row] = s;
		}
	return C;
}

double FHKMat::Determinant() const
{
	const double* te = E;
	const double n11 = te[0], n12 = te[4], n13 = te[8], n14 = te[12];
	const double n21 = te[1], n22 = te[5], n23 = te[9], n24 = te[13];
	const double n31 = te[2], n32 = te[6], n33 = te[10], n34 = te[14];
	const double n41 = te[3], n42 = te[7], n43 = te[11], n44 = te[15];
	return n41 * (+n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34)
		+ n42 * (+n11 * n23 * n34 - n11 * n24 * n33 + n14 * n21 * n33 - n13 * n21 * n34 + n13 * n24 * n31 - n14 * n23 * n31)
		+ n43 * (+n11 * n24 * n32 - n11 * n22 * n34 - n14 * n21 * n32 + n12 * n21 * n34 + n14 * n22 * n31 - n12 * n24 * n31)
		+ n44 * (-n13 * n22 * n31 - n11 * n23 * n32 + n11 * n22 * n33 + n13 * n21 * n32 - n12 * n21 * n33 + n12 * n23 * n31);
}

FHKMat FHKMat::Inverse() const
{
	const double* te = E;
	const double n11 = te[0], n21 = te[1], n31 = te[2], n41 = te[3], n12 = te[4], n22 = te[5], n32 = te[6], n42 = te[7];
	const double n13 = te[8], n23 = te[9], n33 = te[10], n43 = te[11], n14 = te[12], n24 = te[13], n34 = te[14], n44 = te[15];
	const double t11 = n23 * n34 * n42 - n24 * n33 * n42 + n24 * n32 * n43 - n22 * n34 * n43 - n23 * n32 * n44 + n22 * n33 * n44;
	const double t12 = n14 * n33 * n42 - n13 * n34 * n42 - n14 * n32 * n43 + n12 * n34 * n43 + n13 * n32 * n44 - n12 * n33 * n44;
	const double t13 = n13 * n24 * n42 - n14 * n23 * n42 + n14 * n22 * n43 - n12 * n24 * n43 - n13 * n22 * n44 + n12 * n23 * n44;
	const double t14 = n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34;
	const double det = n11 * t11 + n21 * t12 + n31 * t13 + n41 * t14;
	FHKMat R;
	if (det == 0) { for (double& v : R.E) v = 0; return R; }
	const double d = 1 / det;
	double* o = R.E;
	o[0] = t11 * d;
	o[1] = (n24 * n33 * n41 - n23 * n34 * n41 - n24 * n31 * n43 + n21 * n34 * n43 + n23 * n31 * n44 - n21 * n33 * n44) * d;
	o[2] = (n22 * n34 * n41 - n24 * n32 * n41 + n24 * n31 * n42 - n21 * n34 * n42 - n22 * n31 * n44 + n21 * n32 * n44) * d;
	o[3] = (n23 * n32 * n41 - n22 * n33 * n41 - n23 * n31 * n42 + n21 * n33 * n42 + n22 * n31 * n43 - n21 * n32 * n43) * d;
	o[4] = t12 * d;
	o[5] = (n13 * n34 * n41 - n14 * n33 * n41 + n14 * n31 * n43 - n11 * n34 * n43 - n13 * n31 * n44 + n11 * n33 * n44) * d;
	o[6] = (n14 * n32 * n41 - n12 * n34 * n41 - n14 * n31 * n42 + n11 * n34 * n42 + n12 * n31 * n44 - n11 * n32 * n44) * d;
	o[7] = (n12 * n33 * n41 - n13 * n32 * n41 + n13 * n31 * n42 - n11 * n33 * n42 - n12 * n31 * n43 + n11 * n32 * n43) * d;
	o[8] = t13 * d;
	o[9] = (n14 * n23 * n41 - n13 * n24 * n41 - n14 * n21 * n43 + n11 * n24 * n43 + n13 * n21 * n44 - n11 * n23 * n44) * d;
	o[10] = (n12 * n24 * n41 - n14 * n22 * n41 + n14 * n21 * n42 - n11 * n24 * n42 - n12 * n21 * n44 + n11 * n22 * n44) * d;
	o[11] = (n13 * n22 * n41 - n12 * n23 * n41 - n13 * n21 * n42 + n11 * n23 * n42 + n12 * n21 * n43 - n11 * n22 * n43) * d;
	o[12] = t14 * d;
	o[13] = (n13 * n24 * n31 - n14 * n23 * n31 + n14 * n21 * n33 - n11 * n24 * n33 - n13 * n21 * n34 + n11 * n23 * n34) * d;
	o[14] = (n14 * n22 * n31 - n12 * n24 * n31 - n14 * n21 * n32 + n11 * n24 * n32 + n12 * n21 * n34 - n11 * n22 * n34) * d;
	o[15] = (n12 * n23 * n31 - n13 * n22 * n31 + n13 * n21 * n32 - n11 * n23 * n32 - n12 * n21 * n33 + n11 * n22 * n33) * d;
	return R;
}

FVector FHKMat::TransformPoint(const FVector& V) const
{
	const double w = 1.0 / (E[3] * V.X + E[7] * V.Y + E[11] * V.Z + E[15]);
	return FVector((E[0] * V.X + E[4] * V.Y + E[8] * V.Z + E[12]) * w, (E[1] * V.X + E[5] * V.Y + E[9] * V.Z + E[13]) * w, (E[2] * V.X + E[6] * V.Y + E[10] * V.Z + E[14]) * w);
}

FVector FHKMat::TransformDir(const FVector& V) const
{
	return FVector(E[0] * V.X + E[4] * V.Y + E[8] * V.Z, E[1] * V.X + E[5] * V.Y + E[9] * V.Z, E[2] * V.X + E[6] * V.Y + E[10] * V.Z);
}
