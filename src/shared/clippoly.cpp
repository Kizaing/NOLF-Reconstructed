// Talon poly/box clipping (not in Jupiter). The file sorts between clientshell.cpp and
// cloaderthread.cpp; its name is unknown. PolyTouchesBox clips a world poly against the box planes
// set up in g_BoxFindPlanes (collision.cpp SetupBox and si_FindPoliesTouchingBox) and returns the
// clipped poly's extents.
#include "bdefs.h"
#include "de_objects.h"
#include "de_world.h"
#include "counter.h"

#define MAX_CLIP_VERTS		256
#define MAX_CLIP_NEWVERTS	264

// collision.cpp: the box being tested (see SetupBox).
// GLOBAL: LITHTECH 0x004e245c
extern float g_BoxFindRadius;
// GLOBAL: LITHTECH 0x004e2460
extern LTVector g_BoxFindCenter;
// GLOBAL: LITHTECH 0x004e2f10
extern LTPlane g_BoxFindPlanes[6];

// Profiling.
// GLOBAL: LITHTECH 0x004e24a8
extern uint32 g_nPolyTouchesBoxCalls;
// GLOBAL: LITHTECH 0x004e0490
static uint32 g_Ticks_PolyTouchesBox;

// Box plane i is the axis i/2 facing this way.
// GLOBAL: LITHTECH 0x004d0798
static float g_ClipSigns[6] = { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f };

// The clipping buffers.
struct PolyClipBuffer
{
	~PolyClipBuffer() {}

	LTVector	*m_In[MAX_CLIP_VERTS];			// the poly's vertices
	LTVector	m_NewVerts[MAX_CLIP_NEWVERTS];	// vertices made by clipping
	LTVector	*m_Out[MAX_CLIP_VERTS];			// the clipped polies, one after another
};


// The function static's destructor (registered with atexit).
// FUNCTION: LITHTECH 0x004172c0 _$E2

// Same size as the original (944) and the same shape: the sphere test (VEC_SUB written z, y, x as the original's
// loads show), `pIn` copied before the plane loop, vertices addressed as pIn[iPrev]/pIn[i] and reloaded each time,
// the plane loop run on a walking pointer to the plane's dist (`cmp ptr, &g_BoxFindPlanes[6].m_Dist`), the min/max
// loop as `for (; nIn > 0; nIn--, pIn++)`. What differs is the register assignment: the original keeps pPoly in esi,
// pOut in ebp, the plane index in ebx and pNew in edx, with nIn spilled to [esp+0x10]; ours has pOut in ebx and
// the plane index in edi. The order of the local declarations has no effect (VC6 assigns slots by use).
// STUB: LITHTECH 0x00416f10 ?PolyTouchesBox@@YAIPAUWorldPoly@@PAX1@Z
LTBOOL PolyTouchesBox(WorldPoly *pPoly, void *pUnknown1, void *pUnknown2)
{
	static PolyClipBuffer s_Buf;
	LTVector *pMin, *pMax, vDiff, *pNew, **pIn, **pOut, **pOutStart;
	float fRadius, sign, prevDist, curDist, t;
	int nIn, nNewVerts, iPlane, axis, i, iPrev;
	float *pDist;
	LTBOOL bPrevInside, bCurInside;

	pMin = (LTVector*)pUnknown1;
	pMax = (LTVector*)pUnknown2;

	g_nPolyTouchesBoxCalls++;
	CountAdder cntAdd(&g_Ticks_PolyTouchesBox);

	if (!(((Surface*)pPoly->m_pSurface)->m_Flags & SURF_SOLID))
		return LTFALSE;

	// Quick sphere test.
	fRadius = g_BoxFindRadius + pPoly->m_Radius;
	vDiff.z = g_BoxFindCenter.z - pPoly->m_Center.z;
	vDiff.y = g_BoxFindCenter.y - pPoly->m_Center.y;
	vDiff.x = g_BoxFindCenter.x - pPoly->m_Center.x;
	if (VEC_MAGSQR(vDiff) > fRadius * fRadius)
		return LTFALSE;

	// Clip the poly into the box's planes.
	pIn = s_Buf.m_In;
	pOut = pOutStart = s_Buf.m_Out;
	nIn = pPoly->m_nVertices;
	nNewVerts = 0;
	for (i=0; i < pPoly->m_nVertices; i++)
	{
		pIn[i] = ((SPolyVertex*)(pPoly + 1))[i].m_Vec;
	}

	for (iPlane=0, pDist = &g_BoxFindPlanes[0].m_Dist; pDist < &g_BoxFindPlanes[6].m_Dist; iPlane++, pDist += 4)
	{
		sign = g_ClipSigns[iPlane];
		axis = iPlane >> 1;

		iPrev = nIn - 1;
		prevDist = sign * (&pIn[iPrev]->x)[axis] - *pDist;
		bPrevInside = LTTRUE;
		if (!(prevDist > 0.001f))
			bPrevInside = LTFALSE;

		for (i=0; i < nIn; i++)
		{
			curDist = sign * (&pIn[i]->x)[axis] - *pDist;
			bCurInside = LTTRUE;
			if (!(curDist > 0.001f))
				bCurInside = LTFALSE;

			if (bPrevInside)
				*pOut++ = pIn[iPrev];

			if (bPrevInside != bCurInside)
			{
				t = prevDist / (prevDist - curDist);
				pNew = &s_Buf.m_NewVerts[nNewVerts++];
				pNew->x = pIn[iPrev]->x + (pIn[i]->x - pIn[iPrev]->x) * t;
				pNew->y = pIn[iPrev]->y + (pIn[i]->y - pIn[iPrev]->y) * t;
				pNew->z = pIn[iPrev]->z + (pIn[i]->z - pIn[iPrev]->z) * t;
				*pOut++ = pNew;
			}

			prevDist = curDist;
			bPrevInside = bCurInside;
			iPrev = i;
		}

		// The next plane clips what this one left.
		pIn = pOutStart;
		nIn = pOut - pOutStart;
		pOutStart = pOut;
		if (!nIn)
			return LTFALSE;
	}

	if (pMin && pMax)
	{
		pMin->x = pMin->y = pMin->z = (float)MAX_CREAL;
		pMax->x = pMax->y = pMax->z = (float)-MAX_CREAL;

		for (; nIn > 0; nIn--, pIn++)
		{
			VEC_MIN(*pMin, *pMin, **pIn);
			VEC_MAX(*pMax, *pMax, **pIn);
		}
	}

	return LTTRUE;
}
