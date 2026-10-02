// Jupiter runtime/shared/src/mathlt_impl.cpp. Talon's ILTMath is a concrete class (SDK
// iltmath.h) embedded in ILTCSBase, so its methods are defined directly.
// FLAGS: /O2 /GX-
#include <math.h>
#include "bdefs.h"
#include "iltmath.h"
#include "geomroutines.h"


// FUNCTION: LITHTECH 0x0044d190
LTRESULT ILTMath::GetRotationVectors(LTRotation &rot, LTVector &right, LTVector &up, LTVector &forward)
{
	gr_GetRotationVectors(&rot, &right, &up, &forward);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0044d1c0 ?SetupEuler@ILTMath@@UAEKAAVLTRotation@@MMM@Z
LTRESULT ILTMath::SetupEuler(LTRotation &rot, float pitch, float yaw, float roll)
{
	gr_EulerToRotation(pitch, yaw, roll, &rot);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0044d1f0 ?SetupEuler@ILTMath@@UAEKAAVLTRotation@@AAV?$_CVector@M@@@Z
LTRESULT ILTMath::SetupEuler(LTRotation &rot, LTVector &vAngles)
{
	return SetupEuler(rot, vAngles.x, vAngles.y, vAngles.z);
}

// FUNCTION: LITHTECH 0x0044d210
LTRESULT ILTMath::InterpolateRotation(LTRotation &rDest, LTRotation &rot1, LTRotation &rot2, float t)
{
	gr_InterpolateRotation(&rDest, &rot1, &rot2, t);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0044d240
LTRESULT ILTMath::SetupTransformationMatrix(LTMatrix &mMat, LTVector &vTranslation, LTRotation &rRot)
{
	gr_SetupTransformation(&vTranslation, &rRot, LTNULL, &mMat);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0044d260
LTRESULT ILTMath::SetupTranslationMatrix(LTMatrix &mMat, LTVector &vTranslation)
{
	gr_SetupTransformation(&vTranslation, LTNULL, LTNULL, &mMat);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0044d280
LTRESULT ILTMath::SetupRotationMatrix(LTMatrix &mMat, LTRotation &rRot)
{
	gr_SetupTransformation(LTNULL, &rRot, LTNULL, &mMat);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0044d2a0
LTRESULT ILTMath::SetupTranslationFromMatrix(LTVector &vTranslation, LTMatrix &mMat)
{
	mMat.GetTranslation(vTranslation);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0044d2c0
LTRESULT ILTMath::SetupRotationFromMatrix(LTRotation &rRot, LTMatrix &mMat)
{
	MatrixToRotation(&mMat, &rRot);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0044d2e0
LTRESULT ILTMath::SetupRotationAroundPoint(LTMatrix &mMat, LTRotation &rRot, LTVector &vPoint)
{
	LTMatrix mForward, mRotate, mBackward;
	LTVector negativePoint;

	negativePoint = -vPoint;
	SetupTranslationMatrix(mForward, vPoint);
	SetupTranslationMatrix(mBackward, negativePoint);
	SetupRotationMatrix(mRotate, rRot);

	mMat = mForward * mRotate * mBackward;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0044d3d0
LTRESULT ILTMath::AlignRotation(LTRotation &rOutRot, LTVector &vVector, LTVector &vUp)
{
	LTMatrix theMat;
	LTVector ref[3];

	// Get a frame of reference.
	gr_BuildFrameOfReference(&vVector, &vUp, &ref[0], &ref[1], &ref[2]);

	theMat.SetBasisVectors(&ref[0], &ref[1], &ref[2]);
	MatrixToRotation(&theMat, &rOutRot);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0044d490
LTRESULT ILTMath::EulerRotateX(LTRotation &rRot, float amount)
{
	LTVector vecs[3];

	gr_GetRotationVectors(&rRot, &vecs[0], &vecs[1], &vecs[2]);
	return RotateAroundAxis(rRot, vecs[0], amount);
}

// FUNCTION: LITHTECH 0x0044d4d0
LTRESULT ILTMath::EulerRotateY(LTRotation &rRot, float amount)
{
	LTVector vecs[3];

	gr_GetRotationVectors(&rRot, &vecs[0], &vecs[1], &vecs[2]);
	return RotateAroundAxis(rRot, vecs[1], amount);
}

// FUNCTION: LITHTECH 0x0044d510
LTRESULT ILTMath::EulerRotateZ(LTRotation &rRot, float amount)
{
	LTVector vecs[3];

	gr_GetRotationVectors(&rRot, &vecs[0], &vecs[1], &vecs[2]);
	return RotateAroundAxis(rRot, vecs[2], amount);
}

// FUNCTION: LITHTECH 0x0044d550
LTRESULT ILTMath::RotateAroundAxis(LTRotation &rRot, LTVector &vAxis, float amount)
{
	LTMatrix rotation, mat;

	// Get the quaternion.
	RotationToMatrix(&rRot, &mat);

	// Setup a rotation matrix and apply it.
	gr_SetupRotationAroundVector(&rotation, vAxis, amount);
	mat = rotation * mat;

	MatrixToRotation(&mat, &rRot);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0044d5f0
LTRESULT ILTMath::GetRotationVectorsFromMatrix(LTMatrix &mMat,
	LTVector &vRight, LTVector &vUp, LTVector &vForward)
{
	mMat.GetBasisVectors(&vRight, &vUp, &vForward);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0044d640 ?GetEulerAngles@ILTMath@@UAEKAAVLTRotation@@PAM11@Z
LTRESULT ILTMath::GetEulerAngles(LTRotation &rRot, float *pitch, float *yaw, float *roll)
{
	LTRotation rTemp;
	LTVector vRight, vUp, vForward, vRef, vRefUp, vRefForward;
	float dot, side;

	rTemp = rRot;
	GetRotationVectors(rTemp, vRight, vUp, vForward);

	// Roll: the angle between the right vector and the level right vector.
	vRef = vForward.Cross(LTVector(0.0f, 1.0f, 0.0f));
	vRef.Norm();
	vRefUp = vRef.Cross(vForward);
	if(roll)
	{
		dot = vRight.Dot(vRef);
		side = vRight.Dot(vRefUp);

		if(dot > 1.0f)
			dot = 1.0f;
		else if(dot < -1.0f)
			dot = -1.0f;

		if(side > 1.0f)
			side = 1.0f;
		else if(side < -1.0f)
			side = -1.0f;

		*roll = (float)acos(dot);
		if(side < 0.0f)
			*roll = -*roll;
	}

	// Take the roll out.
	AlignRotation(rTemp, vForward, vRefUp);
	GetRotationVectors(rTemp, vRight, vUp, vForward);

	// Pitch: the angle between the forward vector and the level forward vector.
	vRefUp.Init(0.0f, 1.0f, 0.0f);
	vRefForward = vRefUp.Cross(vRight);
	vRefForward.Norm();
	if(pitch)
	{
		dot = vRefForward.Dot(vForward);
		side = vForward.Dot(vRefUp);

		if(dot > 1.0f)
			dot = 1.0f;
		else if(dot < -1.0f)
			dot = -1.0f;

		if(side > 1.0f)
			side = 1.0f;
		else if(side < -1.0f)
			side = -1.0f;

		*pitch = (float)acos(dot);
		if(side > 0.0f)
			*pitch = -*pitch;
	}

	// Take the pitch out.
	AlignRotation(rTemp, vRefForward, vRefUp);
	GetRotationVectors(rTemp, vRight, vUp, vForward);

	// Yaw.
	if(yaw)
	{
		dot = vForward.Dot(LTVector(0.0f, 0.0f, 1.0f));
		side = vForward.Dot(LTVector(1.0f, 0.0f, 0.0f));

		if(dot > 1.0f)
			dot = 1.0f;
		else if(dot < -1.0f)
			dot = -1.0f;

		if(side > 1.0f)
			side = 1.0f;
		else if(side < -1.0f)
			side = -1.0f;

		*yaw = (float)acos(dot);
		if(side < 0.0f)
			*yaw = -*yaw;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0044db40 ?GetEulerAngles@ILTMath@@UAEKAAVLTRotation@@AAV?$_CVector@M@@@Z
LTRESULT ILTMath::GetEulerAngles(LTRotation &rRot, LTVector &vAngles)
{
	return GetEulerAngles(rRot, &vAngles.x, &vAngles.y, &vAngles.z);
}
