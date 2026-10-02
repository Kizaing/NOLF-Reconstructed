// Jupiter runtime/world/src/de_mainworld.cpp
// Talon's file is quite different: no WorldBsp loading here, but portal and name lookups.
#include <stdio.h>
#include <string.h>
#include "bdefs.h"
#include "de_world.h"
#include "de_objects.h"
#include "de_mainworld.h"


// -------------------------------------------------------------- //
// Interface functions.
// -------------------------------------------------------------- //

// The out-of-line copy of the SDK's inline MatVMul (ltmatrix.h).
// FUNCTION: LITHTECH 0x00428120 ?MatVMul@@YAXPAV?$_CVector@M@@PAVLTMatrix@@0@Z

// The points loop schedules its FPU loads differently (x*m00, z*m02, y*m01 order).
// STUB: LITHTECH 0x00427ed0
void w_TransformWorldModel(WorldModelInstance *pInst, LTMatrix *pMat, LTBOOL bPartial)
{
	uint32 i;
	LTVector planePt;
	WorldBsp *pSrc, *pDest;

	if (pInst->m_pOriginalBsp->IsUntransformed())
		return;

	pSrc = pInst->m_pOriginalBsp;
	pDest = pInst->m_pWorldBsp;
	if (!pSrc || !pDest)
		return;

	// Only do the root node center so DObject::GetCenter works.
	if (bPartial)
	{
		return;
	}

	// Transform the points.
	for (i=0; i < pDest->m_nPoints; i++)
	{
		MatVMul(&pDest->m_Points[i], pMat, &pSrc->m_Points[i]);
	}

	// Transform the planes!
	for (i=0; i < pDest->m_nPlanes; i++)
	{
		planePt = pSrc->m_Planes[i].m_Normal * pSrc->m_Planes[i].m_Dist;
		MatVMul_InPlace(pMat, &planePt);

		MatVMul_3x3(&pDest->m_Planes[i].m_Normal, pMat, &pSrc->m_Planes[i].m_Normal);
		pDest->m_Planes[i].m_Dist = pDest->m_Planes[i].m_Normal.Dot(planePt);
	}

	// Transform the polies...
	for (i=0; i < pDest->m_nPolies; i++)
	{
		MatVMul(&pDest->m_Polies[i]->m_Center, pMat, &pSrc->m_Polies[i]->m_Center);
	}
}


// FUNCTION: LITHTECH 0x00428180
BspPortal* w_FindBspPortal(WorldBsp *pBsp, const char *pName, uint32 *pIndex)
{
	uint32 i;

	for (i=0; i < pBsp->m_nPortals; i++)
	{
		if (strcmp(pBsp->m_Portals[i].m_pName, pName) == 0)
		{
			if (pIndex)
				*pIndex = i;

			return &pBsp->m_Portals[i];
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x00428200
BspPortal* w_FindPortal(MainWorld *pWorld, const char *pName, uint32 *pWorldIndex, uint32 *pPortalIndex)
{
	uint32 i;
	BspPortal *pPortal;

	for (i=0; i < pWorld->m_WorldModels.GetSize(); i++)
	{
		pPortal = w_FindBspPortal(pWorld->m_WorldModels[i]->m_pOriginalBsp, pName, pPortalIndex);
		if (pPortal)
		{
			if (pWorldIndex)
				*pWorldIndex = i;

			return pPortal;
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x00428260
MainWorldNamedEntry* w_FindNamedEntry(MainWorld *pWorld, const char *pName, uint32 *pIndex)
{
	uint32 i;

	for (i=0; i < pWorld->m_nNamedEntries; i++)
	{
		if (stricmp(pWorld->m_NamedEntries[i].m_Name, pName) == 0)
		{
			if (pIndex)
				*pIndex = i;

			return &pWorld->m_NamedEntries[i];
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x004282d0
LTBOOL w_MakeSpecialName(const char *pName, int id, char *pBuf, uint32 bufLen)
{
	if (bufLen < 8)
		return LTFALSE;

	if (strlen(pName) > (bufLen - 8))
		return LTFALSE;

	sprintf(pBuf, "%s%d %s", "#$#", id, pName);
	return LTTRUE;
}


// ----------------------------------------------------------------------------- //
// WorldData.
// ----------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00428360
WorldData::WorldData()
{
	Clear();
}

// FUNCTION: LITHTECH 0x00428370
WorldData::~WorldData()
{
	Term();
}

// FUNCTION: LITHTECH 0x00428380
void WorldData::Clear()
{
	m_Flags = 0;
	m_pOriginalBsp = NULL;
	m_pWorldBsp = NULL;
	m_pValidBsp = NULL;
}

// FUNCTION: LITHTECH 0x00428390
void WorldData::Term()
{
	if (m_Flags & WD_ORIGINALBSPALLOCED && m_pOriginalBsp)
		delete m_pOriginalBsp;

	if (m_Flags & WD_WORLDBSPALLOCED && m_pWorldBsp)
		delete m_pWorldBsp;

	Clear();
}
