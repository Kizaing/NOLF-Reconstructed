// Jupiter runtime/shared/src/geomroutines.cpp
// FLAGS: /O2 /GX-
// Talon uses plain cos/sin (intrinsics) where Jupiter has ltcosf/ltsinf, and has no gr_IntersectPlanes.
#include <math.h>
#include "ltbasedefs.h"
#include "geomroutines.h"


// FUNCTION: LITHTECH 0x0043b2b0
void gr_GetPerpendicularVector(LTVector *pVec, LTVector *pRef, LTVector *pPerp)
{
	float dot, t;
	LTVector temp, tempRef;

	if(!pRef)
	{
		tempRef.Init(0, 1, 0);
		pRef = &tempRef;
	}

	*pPerp = *pRef;

	// Are pRef and pVec the same?  If not, we can exit.
	dot = pVec->Dot(*pPerp);
	if(dot > 0.99f || dot < -0.99f)
	{
		// Try to modify it as little as possible.
		pPerp->z += 5.0f;
		pPerp->Norm();
		dot = VEC_DOT(*pVec, *pPerp);
		if(dot > 0.99f || dot < -0.99f)
		{
			pPerp->x += 5.0f;
			pPerp->y += 5.0f;
			pPerp->Norm();

			dot = pVec->Dot(*pPerp);
			if(dot > 0.99f || dot < -0.99f)
			{
				pPerp->x += 5.0f;
				pPerp->y -= 2.0f;
				pPerp->z -= 5.0f;
				pPerp->Norm();
			}
		}
	}

	// Make pVec and pPerp linear independent.
	t = -pVec->Dot(*pPerp);
	temp = *pVec * t;
	*pPerp += temp;
	pPerp->Norm();
}


// FUNCTION: LITHTECH 0x0043b5b0
void gr_BuildFrameOfReference(LTVector *pVec, LTVector *pUpRef, LTVector *pRight, LTVector *pUp, LTVector *pForward)
{
	LTVector tempRef;

	*pForward = *pVec;
	pForward->Norm();

	// Treat the vector as the forward vector and come up with 2 other vectors.
	if(pUpRef)
	{
		tempRef = *pUpRef;
		tempRef.Norm();
		gr_GetPerpendicularVector(pForward, &tempRef, pUp);
	}
	else
	{
		gr_GetPerpendicularVector(pForward, LTNULL, pUp);
	}

	// Create the right vector.
	*pRight = pForward->Cross(*pUp);
}


// FUNCTION: LITHTECH 0x0043b710 ?gr_SetupMatrixEuler@@YAXV?$_CVector@M@@QAY03M@Z
void gr_SetupMatrixEuler(const LTVector vAngles, float mat[4][4])
{
	float yc = (float)cos(vAngles.y), ys = (float)sin(vAngles.y);
	float pc = (float)cos(vAngles.x), ps = (float)sin(vAngles.x);
	float rc = (float)cos(vAngles.z), rs = (float)sin(vAngles.z);

	mat[0][0] = rc*yc + rs*ps*ys;
	mat[0][1] = -rs*yc + rc*ps*ys;
	mat[0][2] = pc*ys;
	mat[0][3] = 0.0f;

	mat[1][0] = rs*pc;
	mat[1][1] = rc*pc;
	mat[1][2] = -ps;
	mat[1][3] = 0.0f;

	mat[2][0] = -rc*ys + rs*ps*yc;
	mat[2][1] = rs*ys + rc*ps*yc;
	mat[2][2] = pc*yc;
	mat[2][3] = 0.0f;

	mat[3][0] = mat[3][1] = mat[3][2] = 0.0f;
	mat[3][3] = 1.0f;
}


// FUNCTION: LITHTECH 0x0043b7d0 ?gr_SetupMatrixEuler@@YAXQAY03MMMM@Z
void gr_SetupMatrixEuler(float mat[4][4], float pitch, float yaw, float roll)
{
	gr_SetupMatrixEuler(LTVector(pitch, yaw, roll), mat);
}


// FUNCTION: LITHTECH 0x0043b820
void RotationToMatrix(LTRotation *pRot, LTMatrix *pMatrix)
{
	quat_ConvertToMatrix((float*)pRot, pMatrix->m);
}


// FUNCTION: LITHTECH 0x0043b830
void MatrixToRotation(LTMatrix *pMatrix, LTRotation *pRot)
{
	quat_ConvertFromMatrix((float*)pRot, pMatrix->m);
}


// FUNCTION: LITHTECH 0x0043b850
void gr_GetRotationVectors(LTRotation *pRot, LTVector *pRight, LTVector *pUp, LTVector *pForward)
{
	quat_GetVectors((float*)pRot, (float*)pRight, (float*)pUp, (float*)pForward);
}


// FUNCTION: LITHTECH 0x0043b860
void gr_InterpolateRotation(LTRotation *pDest, LTRotation *pRot1, LTRotation *pRot2, float t)
{
	quat_Slerp((float*)pDest, (float*)pRot1, (float*)pRot2, t);
}


// FUNCTION: LITHTECH 0x0043b870 ?gr_EulerToRotation@@YAXV?$_CVector@M@@PAVLTRotation@@@Z
void gr_EulerToRotation(const LTVector vAngles, LTRotation *pRot)
{
	LTMatrix mat;

	gr_SetupMatrixEuler(mat.m, VEC_EXPAND(vAngles));
	MatrixToRotation(&mat, pRot);
}


// FUNCTION: LITHTECH 0x0043b8a0 ?gr_EulerToRotation@@YAXMMMPAVLTRotation@@@Z
void gr_EulerToRotation(float pitch, float yaw, float roll, LTRotation *pRot)
{
	gr_EulerToRotation(LTVector(pitch, yaw, roll), pRot);
}


// FUNCTION: LITHTECH 0x0043b8f0
void gr_GetEulerVectors(
	const LTVector vAngles,
	LTVector &vRight,
	LTVector &vUp,
	LTVector &vForward)
{
	LTMatrix mTemp;

	gr_SetupMatrixEuler(vAngles, mTemp.m);
	mTemp.GetBasisVectors(&vRight, &vUp, &vForward);
}


// FUNCTION: LITHTECH 0x0043b970
void gr_SetupTransformation(LTVector *pPos, LTRotation *pQuat, LTVector *pScale, LTMatrix *pMat)
{
	if( pQuat )
	{
		quat_ConvertToMatrix((float*)pQuat, pMat->m);
	}
	else
	{
		Mat_Identity( pMat );
	}

	if( pScale )
	{
		pMat->m[0][0] *= pScale->x;
		pMat->m[1][0] *= pScale->x;
		pMat->m[2][0] *= pScale->x;

		pMat->m[0][1] *= pScale->y;
		pMat->m[1][1] *= pScale->y;
		pMat->m[2][1] *= pScale->y;

		pMat->m[0][2] *= pScale->z;
		pMat->m[1][2] *= pScale->z;
		pMat->m[2][2] *= pScale->z;
	}

	if( pPos )
	{
		pMat->m[0][3] = pPos->x;
		pMat->m[1][3] = pPos->y;
		pMat->m[2][3] = pPos->z;
	}
}


// FUNCTION: LITHTECH 0x0043ba30
void gr_SetupRotationAroundVector(LTMatrix *pMat, LTVector v, float angle)
{
	pMat->SetupRot(v, angle);
}


// FUNCTION: LITHTECH 0x0043bb10
void gr_SetupWMTransform(
	LTVector *pWorldTranslation,
	const LTVector *pPos,			// Position and rotation.
	LTRotation *pRot,
	LTMatrix *pOutForward,	// Output forward and backwards transforms.
	LTMatrix *pOutBack)
{
	LTVector pos;
	LTMatrix mBack, mForward, mRotation;


	pos = *pPos - *pWorldTranslation;

	// Setup the matrices we'll be using.
	mBack.Init(
		1.0f, 0.0f, 0.0f, -pWorldTranslation->x,
		0.0f, 1.0f, 0.0f, -pWorldTranslation->y,
		0.0f, 0.0f, 1.0f, -pWorldTranslation->z,
		0.0f, 0.0f, 0.0f, 1);

	mForward.Init(
		1.0f, 0.0f, 0.0f, pWorldTranslation->x + pos.x,
		0.0f, 1.0f, 0.0f, pWorldTranslation->y + pos.y,
		0.0f, 0.0f, 1.0f, pWorldTranslation->z + pos.z,
		0.0f, 0.0f, 0.0f, 1);

	RotationToMatrix(pRot, &mRotation);

	// Transform order = tBack, Rotate, tForward
	// So multiply matrices as tForward*Rotate*tBack
	*pOutForward = mForward * mRotation * mBack;
	*pOutBack = pOutForward->MakeInverseTransform();
}

// Out-of-line copy of the SDK inline MatMul (ltmatrix.h), emitted by this object.
// FUNCTION: LITHTECH 0x0043bde0 ?MatMul@@YAXPAVLTMatrix@@00@Z
