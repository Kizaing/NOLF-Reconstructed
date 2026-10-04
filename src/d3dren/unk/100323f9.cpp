// d3d.ren unk/100323f9 (0x100323f9-0x10032583): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// D3DShadowTextureFactory::Get + static member, 2 shadow-texture cache helpers, 2 lightmap RLE decompressors. Split from the
// next object at 0x10032583 is soft (p=0.04); 0x1003249f/0x10032503 may belong to the lightmap side.
// FLAGS: /O1 /Ob2 /Oi
#include <windows.h>
#include "bdefs.h"
#include "de_world.h"
#include "de_objects.h"
#include "world_tree.h"
#include "fullintersectline.h"
#include "geomroutines.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/common_stuff.h"
extern LTObject *g_pIntersection;
extern float g_IntersectionBestDistSqr;
extern LTPlane g_IntersectionPlane;
extern LTVector g_IntersectionPos;
extern HPOLY g_hWorldPoly;
#define DO_PLANE_TEST_X(planeCoord, coord0, coord1, coord2, normalDirection) \
    t = (planeCoord - Point1.coord0) / (Point2.coord0 - Point1.coord0);\
    testCoords[0] = Point1.coord1 + ((Point2.coord1 - Point1.coord1) * t);\
    if (testCoords[0] > pServerObj->m_MinBox.coord1 && testCoords[0] < pServerObj->m_MaxBox.coord1)\
    {\
        testCoords[1] = Point1.coord2 + ((Point2.coord2 - Point1.coord2) * t);\
        if (testCoords[1] > pServerObj->m_MinBox.coord2 && testCoords[1] < pServerObj->m_MaxBox.coord2)\
        {\
            pIntersectPt->coord0 = planeCoord;\
            pIntersectPt->coord1 = testCoords[0];\
            pIntersectPt->coord2 = testCoords[1];\
            pIntersectPlane->m_Normal.x = normalDirection;\
            pIntersectPlane->m_Normal.y = 0.0f;\
            pIntersectPlane->m_Normal.z = 0.0f;\
            pIntersectPlane->m_Dist = pServerObj->m_MinBox.x * normalDirection;\
            return true;\
        }\
    }

#define DO_PLANE_TEST_Y(planeCoord, coord0, coord1, coord2, normalDirection) \
    t = (planeCoord - Point1.coord0) / (Point2.coord0 - Point1.coord0);\
    testCoords[0] = Point1.coord1 + ((Point2.coord1 - Point1.coord1) * t);\
    if (testCoords[0] > pServerObj->m_MinBox.coord1 && testCoords[0] < pServerObj->m_MaxBox.coord1)\
    {\
        testCoords[1] = Point1.coord2 + ((Point2.coord2 - Point1.coord2) * t);\
        if (testCoords[1] > pServerObj->m_MinBox.coord2 && testCoords[1] < pServerObj->m_MaxBox.coord2)\
        {\
            pIntersectPt->coord0 = planeCoord;\
            pIntersectPt->coord1 = testCoords[0];\
            pIntersectPt->coord2 = testCoords[1];\
            pIntersectPlane->m_Normal.x = 0.0f;\
            pIntersectPlane->m_Normal.y = normalDirection;\
            pIntersectPlane->m_Normal.z = 0.0f;\
            pIntersectPlane->m_Dist = pServerObj->m_MinBox.y * normalDirection;\
            return true;\
        }\
    }

#define DO_PLANE_TEST_Z(planeCoord, coord0, coord1, coord2, normalDirection) \
    t = (planeCoord - Point1.coord0) / (Point2.coord0 - Point1.coord0);\
    testCoords[0] = Point1.coord1 + ((Point2.coord1 - Point1.coord1) * t);\
    if (testCoords[0] > pServerObj->m_MinBox.coord1 && testCoords[0] < pServerObj->m_MaxBox.coord1)\
    {\
        testCoords[1] = Point1.coord2 + ((Point2.coord2 - Point1.coord2) * t);\
        if (testCoords[1] > pServerObj->m_MinBox.coord2 && testCoords[1] < pServerObj->m_MaxBox.coord2)\
        {\
            pIntersectPt->coord0 = planeCoord;\
            pIntersectPt->coord1 = testCoords[0];\
            pIntersectPt->coord2 = testCoords[1];\
            pIntersectPlane->m_Normal.x = 0.0f;\
            pIntersectPlane->m_Normal.y = 0.0f;\
            pIntersectPlane->m_Normal.z = normalDirection;\
            pIntersectPlane->m_Dist = pServerObj->m_MinBox.z * normalDirection;\
            return true;\
        }\
    }


// Just sets up the current 'closest object'.
#define USE_THIS_OBJECT(pServerObj, distSqr, plane, intersectionPt, hPoly) \
    g_IntersectionBestDistSqr = distSqr;\
    g_pIntersection = pServerObj;\
    g_IntersectionPlane = plane;\
    g_IntersectionPos = intersectionPt;\
    g_hWorldPoly = hPoly;


// ---------------------------------------------------------------------------------------------------------------------------
// geomroutines.cpp (engine twin src/shared/geomroutines.cpp) and intersect_line.cpp (engine twin src/world/intersect_line.cpp)
#include <math.h>


#define INTERSECT_EPSILON	0.01f

#define FrontSide	1
#define BackSide	0

// d3d_surface.h declares a class with a static member whose definition consumes a static-initialiser number: included here, after
// the static initialisers of the first object, so that their `_$E` numbers stay those of the exe.
#include "d3dren/d3d_surface.h"		// D3DShadowTextureFactory, IShadowTexture

// NAME: Get: Jupiter d3dshadowtexture.h D3DShadowTextureFactory::Get() (declared public in d3d_surface.h by the W3 agent).
// FUNCTION: D3DREN 0x100323f9 ?Get@D3DShadowTextureFactory@@SAPAV1@XZ
D3DShadowTextureFactory *D3DShadowTextureFactory::Get()
{
	return m_pShadowTextureFactory;
}

// guess: these are the two observed 12-byte shadow-cache records (pointer, width, height); the original type name is unknown.
struct UnkType_ShadowTextureCache
{
	IShadowTexture *m_Unk00;
	int m_Unk04;
	int m_Unk08;
};
// GLOBAL: D3DREN 0x10076750
UnkType_ShadowTextureCache DAT_10076750[2];

// Returns a cached shadow texture of nWidth x nHeight (re-created when the size changes).
// Both original return paths leave the cached/new pointer in EAX; the shadow caller consumes it.
// FUNCTION: D3DREN 0x100323ff
IShadowTexture *FUN_100323ff(uint32 nWidth, uint32 nHeight)
{
	if (!DAT_10076750[0].m_Unk00 || nWidth != DAT_10076750[0].m_Unk04 || nHeight != DAT_10076750[0].m_Unk08)
	{
		D3DShadowTextureFactory::Get()->FreeShadowTexture(DAT_10076750[0].m_Unk00);
		DAT_10076750[0].m_Unk00 = D3DShadowTextureFactory::Get()->AllocShadowTexture(nWidth, nHeight);
		DAT_10076750[0].m_Unk04 = nWidth;
		DAT_10076750[0].m_Unk08 = nHeight;
	}
	return DAT_10076750[0].m_Unk00;
}

// guess: the second record caches the companion scratch texture.
// FUNCTION: D3DREN 0x1003244f
IShadowTexture *FUN_1003244f(uint32 nWidth, uint32 nHeight)
{
	if (!DAT_10076750[1].m_Unk00 || nWidth != DAT_10076750[1].m_Unk04 || nHeight != DAT_10076750[1].m_Unk08)
	{
		D3DShadowTextureFactory::Get()->FreeShadowTexture(DAT_10076750[1].m_Unk00);
		DAT_10076750[1].m_Unk00 = D3DShadowTextureFactory::Get()->AllocShadowTexture(nWidth, nHeight);
		DAT_10076750[1].m_Unk04 = nWidth;
		DAT_10076750[1].m_Unk08 = nHeight;
	}
	return DAT_10076750[1].m_Unk00;
}

// guess: expands run-length coded lightmap data (a dword texel, with the top bit set a following count byte) into at most 0x400
// dwords (a 32x32 lightmap); returns 0 when the data would overflow.
// FUNCTION: D3DREN 0x1003249f
int FUN_1003249f(uint32 *pIn, int nBytes, uint32 *pOut)
{
	uint32 *pCur;
	uint32 *pEnd;
	uint32 dwTexel;
	uint8 nCount;
	int nLeft;

	if (!pIn)
		return 0;

	nLeft = 0x400;
	pCur = pIn;
	pEnd = (uint32 *)((uint8 *)pIn + nBytes);
	while (pCur < pEnd)
	{
		dwTexel = *pCur++;
		if (dwTexel & 0x80000000)
		{
			nCount = *(uint8 *)pCur;
			dwTexel &= 0x7fffffff;
			pCur = (uint32 *)((uint8 *)pCur + 1);
		}
		else
		{
			nCount = 1;
		}
		nLeft -= nCount;
		if (nLeft < 0)
			return 0;
		if (nCount)
		{
			for (; nCount; nCount--)
				*pOut++ = dwTexel;
		}
	}
	return 1;
}

// guess: expands run lengths (bytes, alternating between 0x00 and 0xff runs) into a 0x400 byte shadow mask; returns 0 on overflow.
// PRAGMA EVIDENCE: the exe expands the variable-length memset of this function inline (`mov bh, bl; ... rep stosd; ... rep stosb`, the /Oi
// intrinsic), while the memsets of FUN_1003273a / FUN_1003287a (same unit, 0x100325e8 on) are `call _memset`.
//#pragma intrinsic(memset)
// STUB diagnosis: the target extent is 128 bytes; the current body is 129 bytes (56 aligned instructions, 28 mismatches / 27 ignoring stack offsets).
//   Target checks pRuns before initializing values, then copies pOut to EDI, stores values[1]=0xff and values[0]=0, and reuses the
//   dead pOut slot [ebp+0x10] for iValue while pEnd is at [ebp-8]. In the loop it loads iValue, compares nRun with zero in EDX,
//   reads values[iValue] into AL before the branch, and expands memset inline. Best of 19 natural declaration/initialization-order,
//   local-pointer, and byte-hoist variants remained 129 bytes with 82 differing bytes and 23/23 aligned instruction mismatches.
//   All four FUNCTIONs in this unit stayed MATCH in each private check; none of the candidates matched this STUB.
// STUB: D3DREN 0x10032503
int FUN_10032503(uint8 *pRuns, int nRuns, uint8 *pOut)
{
	uint8 values[2] = { 0x00, 0xff };
	uint32 iValue;
	uint8 *pEnd;
	uint8 *pNext;
	uint32 nRun;

	if (!pRuns)
		return 0;

	iValue = 0;
	pEnd = pOut + 0x400;
	while (nRuns)
	{
		nRuns--;
		nRun = *pRuns++;
		pNext = pOut + nRun;
		if (pNext > pEnd)
			return 0;
		if (nRun)
		{
			memset(pOut, values[iValue], nRun);
			pOut = pNext;
		}
		iValue = (iValue == 0);
	}
	return 1;
}
//#pragma function(memset)

// ---- dynamic lightmap console variables -----------------------------------------------------------------------------------------
#include "d3dren/rendererconsolevars.h"

// ---- dynamic lights on the lightmaps (LMDynamic) ----------------------------------------------------------------------------------
#include "pixelformat.h"
#include "ltmatrix.h"
#include "d3dren/tlvertex.h"
#include "d3dren/lightmap.h"
