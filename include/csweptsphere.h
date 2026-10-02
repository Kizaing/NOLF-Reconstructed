#ifndef __CSWEPTSPHERE_H__
#define __CSWEPTSPHERE_H__

// Talon's swept sphere vs polygon tests (shared/csweptsphere.cpp, 0x00424970-0x00425cd0). Not in Jupiter, whose
// intersectsweptsphere.cpp is a later rewrite. The names are ours except SweptSphereOrient (from its assert string).
// collision.cpp's sphere physics (MoveSphere, OrientMovement) is the caller.

struct WorldPoly;
class LTObject;

// Sweeps the sphere against one polygon: the fraction of the move (*pT) and the hit point (*pHitPos).
LTBOOL SweptSphereToPoly(LTVector *pStart, LTVector *pEnd, float fRadius, WorldPoly *pPoly, float *pT,
	LTVector *pHitPos);	// 0x00424970

// Pushes the sphere at pPos out of the polygons (up to 10 passes); nonzero when it ends up overlapping none.
uint16 SpherePosTestPolys(LTVector *pPos, float fRadius, WorldPoly **pPolies, int nPolies);	// 0x00425630

// The nearest hit of the sphere against the solid polygons of the list.
LTBOOL SweptSphereToPolys(LTVector *pStart, LTVector *pEnd, float fRadius, WorldPoly **ppPolys, int nPolys,
	LTVector *pHitPos, LTVector *pHitNormal, float *pFraction, int *pbClear);	// 0x004258b0

// Turns the object to stand on the surface with this normal.
void SweptSphereOrient(LTVector *pNormal, LTObject *pObj);	// 0x004259a0

#endif
