#ifndef __CSWEPTSPHERE_H__
#define __CSWEPTSPHERE_H__

// Talon's swept sphere vs polygon tests (shared/csweptsphere.cpp, 0x00424970-0x00425cd0). Not in Jupiter, whose
// intersectsweptsphere.cpp is a later rewrite. The names are ours except SweptSphereOrient (from its assert string).
// collision.cpp's sphere physics (MoveSphere, OrientMovement) is the caller.

struct WorldPoly;
class LTObject;

// Which test of SweptSphereToPoly found the hit: 0 the face, 1 an edge, 2 a vertex (-1 before it runs).
extern int g_SweptSphereHitType;	// 0x004d1808

// Sphere moving from pStart to pEnd against the line segment pV0 - pV1 (0x00425000).
LTBOOL SweptSphereToEdge(LTVector *pStart, LTVector *pEnd, float fRadius, LTVector *pV0, LTVector *pV1,
	float *pT, LTVector *pNormal);

// Sphere moving from pStart to pEnd against a point (0x004253b0).
LTBOOL SweptSphereToPoint(LTVector *pStart, LTVector *pEnd, float fRadius, LTVector *pVertex, float *pT,
	LTVector *pNormal);

// Sweeps the sphere against one polygon: the fraction of the move (*pT) and the direction the sphere is pushed
// away from the polygon at the hit (*pNormal: the plane's normal for a face hit, else from the edge or vertex).
LTBOOL SweptSphereToPoly(LTVector *pStart, LTVector *pEnd, float fRadius, WorldPoly *pPoly, float *pT,
	LTVector *pNormal);	// 0x00424970

// Pushes the sphere at pPos out of the polygons (up to 10 passes); nonzero when it ends up overlapping none.
uint32 SpherePosTestPolys(LTVector *pPos, float fRadius, WorldPoly **pPolies, int nPolies);	// 0x00425630

// The nearest hit of the sphere against the solid polygons of the list.
LTBOOL SweptSphereToPolys(LTVector *pStart, LTVector *pEnd, float fRadius, WorldPoly **ppPolys, int nPolys,
	LTVector *pHitPos, LTVector *pHitNormal, float *pFraction, int *pbClear);	// 0x004258b0

// Turns the object to stand on the surface with this normal.
LTBOOL SweptSphereOrient(LTVector *pNormal, LTObject *pObj);	// 0x004259a0

#endif
