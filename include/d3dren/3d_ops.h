// d3d.ren 3d_ops.h: the shared inline helper d3d_SetupTransformation (Jupiter render_a/src/sys/d3d/3d_ops.h).  One definition for every
// object of the renderer; the exe's out-of-line COMDAT copy is 0x10009240 (emitted in the drawparticles object, the first one that did not
// expand it), the objects that expand it are the A objects (/O2 /Ob2: drawlinesystem, drawpolygrid, drawsprite), the P objects (/O1) call it.
//
// NAME: ROTATION_MAX, d3d_SetupTransformation: Jupiter render_a/src/sys/d3d/3d_ops.h (the d3d.ren body is identical: clamp the quaternion,
// convert it to a matrix, scale the basis vectors, store the position).
#ifndef __D3DREN_3D_OPS_H__
#define __D3DREN_3D_OPS_H__

#include "ltbasedefs.h"
#include "ltmatrix.h"
#include "ltquatbase.h"

#define ROTATION_MAX	100000.0f

inline void d3d_SetupTransformation(const LTVector *pPos, float *pRotation, LTVector *pScale, LTMatrix *pMat)
{
	if (pRotation[0] > ROTATION_MAX || pRotation[0] < -ROTATION_MAX)
		pRotation[0] = 0.0f;

	if (pRotation[1] > ROTATION_MAX || pRotation[1] < -ROTATION_MAX)
		pRotation[1] = 0.0f;

	if (pRotation[2] > ROTATION_MAX || pRotation[2] < -ROTATION_MAX)
		pRotation[2] = 0.0f;

	if (pRotation[3] > ROTATION_MAX || pRotation[3] < -ROTATION_MAX)
		pRotation[3] = 1.0f;

	quat_ConvertToMatrix(pRotation, pMat->m);

	pMat->m[0][0] *= pScale->x;
	pMat->m[1][0] *= pScale->x;
	pMat->m[2][0] *= pScale->x;

	pMat->m[0][1] *= pScale->y;
	pMat->m[1][1] *= pScale->y;
	pMat->m[2][1] *= pScale->y;

	pMat->m[0][2] *= pScale->z;
	pMat->m[1][2] *= pScale->z;
	pMat->m[2][2] *= pScale->z;

	pMat->m[0][3] = pPos->x;
	pMat->m[1][3] = pPos->y;
	pMat->m[2][3] = pPos->z;
}

#endif
