// Talon's built-in surface effects (Pan, Rotate, Warble, Portal, Mirror), registered on the
// client manager with cm_AddSurfaceEffect (cutil.cpp). Jupiter dropped these; the file name
// is a guess (the object sits between dutil.cpp and engine_vars.cpp, so it must sort there).
// FLAGS: /O2 /GX-
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "bdefs.h"
#include "clientmgr.h"
#include "de_world.h"
#include "de_mainworld.h"
#include "de_memory.h"
#include "effects.h"

LTBOOL ci_GetSurfaceBounds(SurfaceData *pSurface, LTVector *pMin, LTVector *pMax);	// 0x00407f60
LTRESULT cm_AddSurfaceEffect(CClientMgr *pClientMgr, SurfaceEffectDesc *pDesc);		// 0x00425cd0


// The data the Pan, Rotate and Warble effects keep (0x40 bytes).
struct SEData
{
	LTVector	m_O, m_P, m_Q;		// 0x00 the surface's original texture vectors
	LTVector	m_Normal;			// 0x24 Rotate: the rotation axis
	float		m_Speed[2];			// 0x30
	float		m_Offset[2];		// 0x38
};


// FUNCTION: LITHTECH 0x004359b0
static void se_NullUpdate(SurfaceData *pSurfaceData, void *pData)
{
}

static void se_NullTerm(void *pData)
{
}


// Portal: the surface is a portal (named in the world's portal list).
// FUNCTION: LITHTECH 0x004359c0
static void* Portal_Init(SurfaceData *pSurfaceData, int argc, char **argv)
{
	Surface *pSurface;
	uint32 index;

	if(argc < 1 || !pSurfaceData->m_pInternalWorld)
		return LTNULL;

	pSurface = (Surface*)pSurfaceData->m_pInternalSurface;
	if(w_FindNamedEntry((MainWorld*)pSurfaceData->m_pInternalWorld, argv[0], &index))
	{
		pSurface->m_Unknown3A = (uint16)index;
	}

	return LTNULL;
}


// Mirror: the surface reflects.
// FUNCTION: LITHTECH 0x00435a00
static void* Mirror_Init(SurfaceData *pSurfaceData, int argc, char **argv)
{
	Surface *pSurface;

	if(!pSurfaceData->m_pInternalWorld)
		return LTNULL;

	pSurface = (Surface*)pSurfaceData->m_pInternalSurface;
	pSurface->m_Unknown3A = 0x7FFE;
	if(argc >= 1 && stricmp(argv[0], "OVERLAY") == 0)
	{
		pSurface->m_Unknown3A |= 0x8000;
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x00435a90
static SEData* se_CreateData(SurfaceData *pSurfaceData, LTBOOL bSetOrigin, int argc, char **argv)
{
	SEData *pData;
	LTVector vMin, vMax;

	pData = (SEData*)dalloc_z(sizeof(SEData));
	pData->m_P = pSurfaceData->P;
	pData->m_Q = pSurfaceData->Q;
	pData->m_Normal.Init();

	// Set the origin to the center or a corner of the surface.
	if(bSetOrigin && argc > 0)
	{
		if(ci_GetSurfaceBounds(pSurfaceData, &vMin, &vMax))
		{
			if(toupper(argv[0][0]) == 'C')
			{
				pSurfaceData->O = vMax - vMin;
				pSurfaceData->O.x = pSurfaceData->O.x * 0.5f + vMin.x;
				pSurfaceData->O.y = pSurfaceData->O.y * 0.5f + vMin.y;
				pSurfaceData->O.z = pSurfaceData->O.z * 0.5f + vMin.z;
			}
			else
			{
				pSurfaceData->O.x = (toupper(argv[0][0]) == 'B') ? vMin.x : vMax.x;
				pSurfaceData->O.y = (toupper(argv[0][1]) == 'B') ? vMin.y : vMax.y;
				pSurfaceData->O.z = (toupper(argv[0][2]) == 'B') ? vMin.z : vMax.z;
			}
		}
	}

	pData->m_O = pSurfaceData->O;
	return pData;
}


// FUNCTION: LITHTECH 0x00435d00
static void se_FreeData(void *pData)
{
	if(pData)
		dfree(pData);
}


// Pan: scrolls the texture.
// FUNCTION: LITHTECH 0x00435a40
static void* Pan_Init(SurfaceData *pSurfaceData, int argc, char **argv)
{
	SEData *pData;

	pData = se_CreateData(pSurfaceData, LTFALSE, argc, argv);
	if(pData)
	{
		if(argc >= 1)
			pData->m_Speed[0] = (float)atof(argv[0]);

		if(argc >= 2)
			pData->m_Speed[1] = (float)atof(argv[1]);
	}

	return pData;
}

// FUNCTION: LITHTECH 0x00435c10
static void Pan_Update(SurfaceData *pSurfaceData, void *pVoidData)
{
	SEData *pData = (SEData*)pVoidData;

	pData->m_Offset[0] += g_pClientMgr->m_FrameTime * pData->m_Speed[0];
	pData->m_Offset[1] += g_pClientMgr->m_FrameTime * pData->m_Speed[1];

	pSurfaceData->O = pData->m_P * pData->m_Offset[0];
	pSurfaceData->O += pData->m_Q * pData->m_Offset[1];
	pSurfaceData->O += pData->m_O;
}

// FUNCTION: LITHTECH 0x00435cf0
static void Pan_Term(void *pData)
{
	se_FreeData(pData);
}


// Rotate: spins the texture around the surface normal.
// FUNCTION: LITHTECH 0x00435d20
static void* Rotate_Init(SurfaceData *pSurfaceData, int argc, char **argv)
{
	SEData *pData;

	pData = se_CreateData(pSurfaceData, LTTRUE, argc, argv);
	if(pData)
	{
		if(argc >= 2)
			pData->m_Speed[0] = (float)atof(argv[1]) * 0.017453292f;

		pData->m_Normal = pSurfaceData->P.Cross(pSurfaceData->Q);
	}

	return pData;
}

// Close: the original keeps m00/m02 in FPU registers and sums each P/Q row loading Px before Pz
// (here Pz is loaded first), and its frame is a 4x4 matrix (0x54 bytes).
// STUB: LITHTECH 0x00435de0
static void Rotate_Update(SurfaceData *pSurfaceData, void *pVoidData)
{
	SEData *pData = (SEData*)pVoidData;
	float fSin, fCos, fOneMinusCos;
	float tx, ty, sx, sy, sz;
	LTMatrix mat;
	LTVector *pAxis;

	// The current angle is kept at 0x34.
	pData->m_Speed[1] += g_pClientMgr->m_FrameTime * pData->m_Speed[0];

	fSin = (float)sin(pData->m_Speed[1]);
	fCos = (float)cos(pData->m_Speed[1]);
	fOneMinusCos = 1.0f - fCos;

	pAxis = &pData->m_Normal;
	tx = fOneMinusCos * pAxis->x;
	ty = fOneMinusCos * pAxis->y;
	sx = fSin * pAxis->x;
	sy = fSin * pAxis->y;
	sz = fSin * pAxis->z;

	mat.m[0][0] = tx * pAxis->x + fCos;
	mat.m[1][0] = tx * pAxis->y + sz;
	mat.m[2][0] = tx * pAxis->z - sy;
	mat.m[0][1] = tx * pAxis->y - sz;
	mat.m[1][1] = ty * pAxis->y + fCos;
	mat.m[2][1] = ty * pAxis->z + sx;
	mat.m[0][2] = tx * pAxis->z + sy;
	mat.m[1][2] = ty * pAxis->z - sx;
	mat.m[2][2] = fOneMinusCos * pAxis->z * pAxis->z + fCos;

	pSurfaceData->P.x = mat.m[0][0] * pData->m_P.x + mat.m[0][2] * pData->m_P.z + mat.m[0][1] * pData->m_P.y;
	pSurfaceData->P.y = mat.m[1][0] * pData->m_P.x + mat.m[1][2] * pData->m_P.z + mat.m[1][1] * pData->m_P.y;
	pSurfaceData->P.z = mat.m[2][0] * pData->m_P.x + mat.m[2][2] * pData->m_P.z + mat.m[2][1] * pData->m_P.y;

	pSurfaceData->Q.x = mat.m[0][0] * pData->m_Q.x + mat.m[0][2] * pData->m_Q.z + mat.m[0][1] * pData->m_Q.y;
	pSurfaceData->Q.y = mat.m[1][0] * pData->m_Q.x + mat.m[1][2] * pData->m_Q.z + mat.m[1][1] * pData->m_Q.y;
	pSurfaceData->Q.z = mat.m[2][0] * pData->m_Q.x + mat.m[2][2] * pData->m_Q.z + mat.m[2][1] * pData->m_Q.y;
}

// FUNCTION: LITHTECH 0x00435f60
static void Rotate_Term(void *pData)
{
	se_FreeData(pData);
}


// Warble: stretches the texture back and forth.
// FUNCTION: LITHTECH 0x00435f70
static void* Warble_Init(SurfaceData *pSurfaceData, int argc, char **argv)
{
	SEData *pData;

	pData = se_CreateData(pSurfaceData, LTTRUE, argc, argv);
	if(pData)
	{
		if(argc >= 2)
			pData->m_Speed[0] = (float)atof(argv[1]);

		if(argc >= 3)
			pData->m_Speed[1] = (float)atof(argv[2]);

		pData->m_Offset[1] = 0.5f;
	}

	return pData;
}

// FUNCTION: LITHTECH 0x00435fd0
static void Warble_Update(SurfaceData *pSurfaceData, void *pVoidData)
{
	SEData *pData = (SEData*)pVoidData;
	float fPScale, fQScale;

	pData->m_Offset[0] += g_pClientMgr->m_FrameTime * pData->m_Speed[0];
	pData->m_Offset[1] += g_pClientMgr->m_FrameTime * pData->m_Speed[1];

	fPScale = (float)cos(pData->m_Offset[0]) + 1.2f;
	fQScale = (float)sin(pData->m_Offset[1]) + 1.3f;
	pSurfaceData->P = pData->m_P * fPScale;
	pSurfaceData->Q = pData->m_Q * fQScale;
}

// FUNCTION: LITHTECH 0x00436090
static void Warble_Term(void *pData)
{
	se_FreeData(pData);
}


// GLOBAL: LITHTECH 0x004d20a8
static SurfaceEffectDesc g_SurfaceEffects[] =
{
	{"Pan", Pan_Init, Pan_Update, Pan_Term},
	{"Rotate", Rotate_Init, Rotate_Update, Rotate_Term},
	{"Warble", Warble_Init, Warble_Update, Warble_Term},
	{"Portal", Portal_Init, se_NullUpdate, se_NullTerm},
	{"Mirror", Mirror_Init, se_NullUpdate, se_NullTerm}
};


// FUNCTION: LITHTECH 0x004360a0
void se_AddSurfaceEffects(CClientMgr *pClientMgr)
{
	uint32 i;

	for(i=0; i < sizeof(g_SurfaceEffects) / sizeof(g_SurfaceEffects[0]); i++)
	{
		cm_AddSurfaceEffect(pClientMgr, &g_SurfaceEffects[i]);
	}
}


// FUNCTION: LITHTECH 0x004360d0
void se_RemoveSurfaceEffects(CClientMgr *pClientMgr)
{
	SurfaceEffect *pCur, *pNext;

	pCur = (SurfaceEffect*)pClientMgr->m_Unknown12e8;
	while(pCur)
	{
		pNext = pCur->m_pNext;
		dfree(pCur);
		pCur = pNext;
	}
}
