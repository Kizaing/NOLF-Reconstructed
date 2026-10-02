// Talon's swept sphere vs polygon tests (not in Jupiter; Jupiter's intersectsweptsphere.cpp is a later rewrite).
// Only the polygon list walker (0x004258b0) is written; the five geometry functions it and the sphere
// physics (collision.cpp's MoveSphere/OrientMovement) call are not decompiled yet:
//   0x00424970 sweeps the sphere against one polygon (first three vertices), 0x00425000, 0x004253b0,
//   0x00425630 (pushes the sphere out of the polygons, up to 10 passes) and 0x004259a0 (SweptSphereOrient, which
//   prints "SweptSphereOrient Rotation Invalid!!" when the rotation is NaN).
// Names are ours.
#include "bdefs.h"
#include "de_objects.h"
#include "de_world.h"
#include "csweptsphere.h"

// Returns the nearest hit of the sphere against the solid polygons of the list.
// FUNCTION: LITHTECH 0x004258b0
LTBOOL SweptSphereToPolys(LTVector *pStart, LTVector *pEnd, float fRadius, WorldPoly **ppPolys, int nPolys,
	LTVector *pHitPos, LTVector *pHitNormal, float *pFraction, int *pbClear)
{
	LTBOOL bHit;
	int i;
	WorldPoly *pPoly;
	LTVector vHit;
	float t;

	bHit = LTFALSE;
	*pFraction = 1.0f;
	*pbClear = LTTRUE;

	for(i=0; i < nPolys; i++)
	{
		pPoly = ppPolys[i];
		if(((Surface*)pPoly->m_pSurface)->m_Flags & SURF_SOLID)
		{
			if(SweptSphereToPoly(pStart, pEnd, fRadius, pPoly, &t, &vHit) && t < *pFraction)
			{
				*pFraction = t;
				*pHitPos = vHit;
				bHit = LTTRUE;
				*pHitNormal = pPoly->GetPlane()->m_Normal;
				if(*pbClear && (((Surface*)pPoly->m_pSurface)->m_Flags & 0x800000))
					*pbClear = LTFALSE;
			}
		}
	}

	return bHit;
}
