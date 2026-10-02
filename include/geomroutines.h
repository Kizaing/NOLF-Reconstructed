// Jupiter runtime/shared/src/geomroutines.h (the subset present in Talon).
#ifndef __GEOMROUTINES_H__
#define __GEOMROUTINES_H__

#include "ltbasedefs.h"

// Build a frame of reference given a direction vector.  pUpRef may be LTNULL (uses 0,1,0).
void gr_GetPerpendicularVector(LTVector *pVec, LTVector *pUpRef, LTVector *pPerp);
void gr_BuildFrameOfReference(LTVector *pVec, LTVector *pUpRef, LTVector *pRight, LTVector *pUp, LTVector *pForward);

// Setup a matrix from euler angles.
#define SetupMatrixEuler gr_SetupMatrixEuler
void gr_SetupMatrixEuler(const LTVector vAngles, float mat[4][4]);
void gr_SetupMatrixEuler(float mat[4][4], float pitch, float yaw, float roll);

// Convert euler angles to a LTRotation.
void gr_EulerToRotation(const LTVector vAngles, LTRotation *pRot);
void gr_EulerToRotation(float pitch, float yaw, float roll, LTRotation *pRot);

// Convert euler angles to orientation vectors.
void gr_GetEulerVectors(const LTVector vAngles, LTVector &vRight, LTVector &vUp, LTVector &vForward);

void RotationToMatrix(LTRotation *pRot, LTMatrix *pMatrix);
void MatrixToRotation(LTMatrix *pMatrix, LTRotation *pRot);

void gr_GetRotationVectors(LTRotation *pRot, LTVector *pRight, LTVector *pUp, LTVector *pForward);
void gr_InterpolateRotation(LTRotation *pDest, LTRotation *pRot1, LTRotation *pRot2, float t);

// Setup a transformation matrix (any of pPos, pQuat, pScale may be LTNULL).
void gr_SetupTransformation(LTVector *pPos, LTRotation *pQuat, LTVector *pScale, LTMatrix *pMat);
void gr_SetupRotationAroundVector(LTMatrix *pMat, LTVector v, float angle);

// Setup world model forward/backward transforms.
void gr_SetupWMTransform(LTVector *pWorldTranslation, const LTVector *pPos, LTRotation *pRot,
	LTMatrix *pOutForward, LTMatrix *pOutBack);

#endif
