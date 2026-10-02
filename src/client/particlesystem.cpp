// Jupiter runtime/client/src/particlesystem.cpp
// Talon: functions take the client manager explicitly, there are no particle angles or
// collisions, and the bounding box ignores particle sizes.
// In Talon PSParticle+0x20 is the forward link (m_pNext).
#include <string.h>
#include "bdefs.h"
#include "de_objects.h"
#include "clientmgr.h"
#include "sprite.h"

// 0x00435960. Returns TRUE if the strings match (case-insensitive).
LTBOOL du_UpperStrcmp(const char *pInputString, const char *pUpperString);

// 0x00489710
LTRESULT LoadSprite(CClientMgr *pClientMgr, FileRef *pFilename, Sprite **ppSprite);

void ps_UpdateParticleBoundingBox(LTParticleSystem *pSystem);


// FUNCTION: LITHTECH 0x00469710
LTRESULT ps_SetTexture(LTParticleSystem *pSystem, CClientMgr *pClientMgr, const char *pName)
{
	FileRef ref;
	int len;
	char endChars[4];
	LTRESULT dResult;

	pSystem->m_pCurTexture = LTNULL;
	pSystem->m_pSprite = LTNULL;

	ref.m_FileType = FILE_CLIENTFILE;
	ref.m_pFilename = pName;

	len = strlen(pName);
	if(len > 3)
	{
		endChars[0] = pName[len-3];
		endChars[1] = pName[len-2];
		endChars[2] = pName[len-1];
		endChars[3] = 0;

		if(du_UpperStrcmp(endChars, "DTX"))
		{
			pSystem->m_pCurTexture = cm_AddSharedTexture(pClientMgr, &ref);
		}
		else if(du_UpperStrcmp(endChars, "SPR"))
		{
			dResult = LoadSprite(pClientMgr, &ref, &pSystem->m_pSprite);
			if(dResult != LT_OK)
				return dResult;

			spr_InitTracker((SpriteTracker*)pSystem->m_SpriteTracker, pSystem->m_pSprite);
			if(((SpriteTracker*)pSystem->m_SpriteTracker)->m_pCurFrame)
				pSystem->m_pCurTexture = ((SpriteTracker*)pSystem->m_SpriteTracker)->m_pCurFrame->m_pTex;
		}
	}

	return LT_OK;
}


// STUB: LITHTECH 0x00469810
void ps_AddParticles(LTParticleSystem *pSystem, uint32 nParticles,
					LTVector *minOffset, LTVector *maxOffset,
					LTVector *minVel, LTVector *maxVel,
					LTVector *minColor, LTVector *maxColor,
					LTFLOAT minLifetime, LTFLOAT maxLifetime )
{
}


// STUB: LITHTECH 0x00469b00
void ps_UpdateParticles(LTParticleSystem *pSystem, LTFLOAT t)
{
}


// Temporaries and FPU scheduling differ (0x18-byte frame in the original).
// STUB: LITHTECH 0x00469ea0
void ps_UpdateParticleBoundingBox(LTParticleSystem *pSystem)
{
	LTVector halfBox = (pSystem->m_MaxPos - pSystem->m_MinPos) * 0.5f;

	pSystem->m_SystemCenter = pSystem->m_MinPos + halfBox;
	pSystem->m_SystemCenter = pSystem->GetPos() + pSystem->m_SystemCenter;

	pSystem->m_SystemRadius = halfBox.Mag();
}


// FUNCTION: LITHTECH 0x00469fc0
void ps_StartUpdatingPositions(LTParticleSystem *pSystem)
{
	pSystem->m_OldCenter = pSystem->m_SystemCenter;
	pSystem->m_OldRadius = pSystem->m_SystemRadius;
}


// FUNCTION: LITHTECH 0x00469ff0
LTBOOL ps_EndUpdatingPositions(LTParticleSystem *pSystem)
{
	ps_UpdateParticleBoundingBox(pSystem);

	if(pSystem->m_OldCenter.x != pSystem->m_SystemCenter.x ||
		pSystem->m_OldCenter.y != pSystem->m_SystemCenter.y ||
		pSystem->m_OldCenter.z != pSystem->m_SystemCenter.z ||
		pSystem->m_SystemRadius > pSystem->m_OldRadius)
	{
		return LTTRUE;
	}

	return LTFALSE;
}


// FUNCTION: LITHTECH 0x0046a060
void ps_OptimizeParticles(LTParticleSystem *pSystem)
{
	PSParticle *pCur;

	if(pSystem->m_ParticleHead.m_pNext == &pSystem->m_ParticleHead)
	{
		pSystem->m_MinPos.Init();
		pSystem->m_MaxPos.Init();
		pSystem->m_SystemCenter.Init();
		pSystem->m_SystemRadius = 1.0f;
	}
	else
	{
		pSystem->m_MinPos.Init(100000.0f, 100000.0f, 100000.0f);
		pSystem->m_MaxPos = -pSystem->m_MinPos;

		pCur = pSystem->m_ParticleHead.m_pNext;
		while(pCur != &pSystem->m_ParticleHead)
		{
			if (pCur->m_Pos.x < pSystem->m_MinPos.x)	pSystem->m_MinPos.x = pCur->m_Pos.x;
			if (pCur->m_Pos.y < pSystem->m_MinPos.y)	pSystem->m_MinPos.y = pCur->m_Pos.y;
			if (pCur->m_Pos.z < pSystem->m_MinPos.z)	pSystem->m_MinPos.z = pCur->m_Pos.z;

			if (pCur->m_Pos.x > pSystem->m_MaxPos.x)	pSystem->m_MaxPos.x = pCur->m_Pos.x;
			if (pCur->m_Pos.y > pSystem->m_MaxPos.y)	pSystem->m_MaxPos.y = pCur->m_Pos.y;
			if (pCur->m_Pos.z > pSystem->m_MaxPos.z)	pSystem->m_MaxPos.z = pCur->m_Pos.z;

			pCur = pCur->m_pNext;
		}

		ps_UpdateParticleBoundingBox(pSystem);
	}
}
