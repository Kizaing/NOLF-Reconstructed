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

// The setup matches; the clipping loop keeps its pointers in other registers and stack slots.
// STUB: LITHTECH 0x00416f10 ?PolyTouchesBox@@YAIPAUWorldPoly@@PAX1@Z
LTBOOL PolyTouchesBox(WorldPoly *pPoly, void *pUnknown1, void *pUnknown2)
{
	static PolyClipBuffer s_Buf;
	LTVector *pMin, *pMax, vDiff, *pPrev, *pCur, *pNew, **pIn, **pOut, **pOutStart;
	float fRadius, sign, prevDist, curDist, t;
	int nIn, nNewVerts, iPlane, axis, i;
	LTBOOL bPrevInside, bCurInside;
	float *pPlaneDist;

	pMin = (LTVector*)pUnknown1;
	pMax = (LTVector*)pUnknown2;

	g_nPolyTouchesBoxCalls++;
	CountAdder cntAdd(&g_Ticks_PolyTouchesBox);

	if (!(((Surface*)pPoly->m_pSurface)->m_Flags & SURF_SOLID))
		return LTFALSE;

	// Quick sphere test.
	fRadius = g_BoxFindRadius + pPoly->m_Radius;
	VEC_SUB(vDiff, g_BoxFindCenter, pPoly->m_Center);
	if (VEC_MAGSQR(vDiff) > fRadius * fRadius)
		return LTFALSE;

	// Clip the poly into the box's planes.
	nIn = pPoly->m_nVertices;
	pIn = s_Buf.m_In;
	for (i=0; i < pPoly->m_nVertices; i++)
	{
		pIn[i] = ((SPolyVertex*)(pPoly + 1))[i].m_Vec;
	}

	nNewVerts = 0;
	pOut = pOutStart = s_Buf.m_Out;
	pPlaneDist = &g_BoxFindPlanes[0].m_Dist;
	for (iPlane=0; iPlane < 6; iPlane++)
	{
		sign = g_ClipSigns[iPlane];
		axis = iPlane >> 1;

		pPrev = pIn[nIn - 1];
		prevDist = sign * (&pPrev->x)[axis] - *pPlaneDist;
		bPrevInside = prevDist > 0.001f;

		for (i=0; i < nIn; i++)
		{
			pCur = pIn[i];
			curDist = sign * (&pCur->x)[axis] - *pPlaneDist;
			bCurInside = curDist > 0.001f;

			if (bPrevInside)
				*pOut++ = pPrev;

			if (bPrevInside != bCurInside)
			{
				t = prevDist / (prevDist - curDist);
				pNew = &s_Buf.m_NewVerts[nNewVerts++];
				pNew->x = pPrev->x + (pCur->x - pPrev->x) * t;
				pNew->y = pPrev->y + (pCur->y - pPrev->y) * t;
				pNew->z = pPrev->z + (pCur->z - pPrev->z) * t;
				*pOut++ = pNew;
			}

			prevDist = curDist;
			bPrevInside = bCurInside;
			pPrev = pCur;
		}

		// The next plane clips what this one left.
		pIn = pOutStart;
		nIn = pOut - pOutStart;
		pOutStart = pOut;
		if (!nIn)
			return LTFALSE;

		pPlaneDist += sizeof(LTPlane) / sizeof(float);
	}

	if (pMin && pMax)
	{
		pMin->x = pMin->y = pMin->z = (float)MAX_CREAL;
		pMax->x = pMax->y = pMax->z = (float)-MAX_CREAL;

		for (i=0; i < nIn; i++)
		{
			VEC_MIN(*pMin, *pMin, *pIn[i]);
			VEC_MAX(*pMax, *pMax, *pIn[i]);
		}
	}

	return LTTRUE;
}
