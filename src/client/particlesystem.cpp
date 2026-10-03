// Jupiter runtime/client/src/particlesystem.cpp
// Talon: functions take the client manager explicitly, there are no particle angles or
// collisions, and the bounding box ignores particle sizes.
// In Talon PSParticle+0x20 is the forward link (m_pNext).
#include <windows.h>		// before the StdLith headers (clientmgr.h brings in lthread.h)
#include <string.h>
#include <stdlib.h>
#include "bdefs.h"
#include "de_objects.h"
#include "clientmgr.h"
#include "sprite.h"
#include "dutil.h"
#include "iltclient.h"
#include "../../build/proj/LT2/lithshared/stdlith/struct_bank.h"

// LTParticleSystem::m_Unknown260 holds the PS_ flags.
#define m_psFlags m_Unknown260

// 0x00489710
LTRESULT LoadSprite(CClientMgr *pClientMgr, FileRef *pFilename, Sprite **ppSprite);

void ps_UpdateParticleBoundingBox(LTParticleSystem *pSystem);

// clientde_impl.cpp (0x004049c0)
LTBOOL ci_IntersectSegment(ClientIntersectQuery *pQuery, ClientIntersectInfo *pInfo);

#define VINTERP(dest, v1, v2, t1, t2, t3)  (dest).Init((v1).x+((v2).x-(v1).x)*(t1), (v1).y+((v2).y-(v1).y)*(t2), (v1).z+((v2).z-(v1).z)*t3);

// Stretch the bounding box to fit the vector's position (Talon ignores the particle size).
inline void ps_UpdateBox(LTParticleSystem *pSystem, LTVector *pPos)
{
	if(pPos->x < pSystem->m_MinPos.x)	pSystem->m_MinPos.x = pPos->x;
	if(pPos->y < pSystem->m_MinPos.y)	pSystem->m_MinPos.y = pPos->y;
	if(pPos->z < pSystem->m_MinPos.z)	pSystem->m_MinPos.z = pPos->z;
	if(pPos->x > pSystem->m_MaxPos.x)	pSystem->m_MaxPos.x = pPos->x;
	if(pPos->y > pSystem->m_MaxPos.y)	pSystem->m_MaxPos.y = pPos->y;
	if(pPos->z > pSystem->m_MaxPos.z)	pSystem->m_MaxPos.z = pPos->z;
}

// Remove a particle.
inline void ps_RemoveParticle(LTParticleSystem *pSystem, PSParticle *pParticle)
{
	pParticle->m_pPrev->m_pNext = pParticle->m_pNext;
	pParticle->m_pNext->m_pPrev = pParticle->m_pPrev;
	sb_Free((StructBank*)pSystem->m_pParticleBank, pParticle);
	pSystem->m_nParticles--;
}

// Add a particle.
inline PSParticle* ps_AddParticle(LTParticleSystem *pSystem, LTVector *pPos, LTVector *pColor, LTVector *pVel, float lifeTime)
{
	PSParticle *pParticle;

	pParticle = (PSParticle*)sb_Allocate((StructBank*)pSystem->m_pParticleBank);
	if(!pParticle)
		return LTNULL;

	ps_UpdateBox(pSystem, pPos);

	pParticle->m_Pos = *pPos;
	pParticle->m_Color = *pColor;
	pParticle->m_Velocity = *pVel;
	pParticle->m_Alpha = 1.0f;
	pParticle->m_Lifetime = lifeTime;
	pParticle->m_TotalLifetime = lifeTime;
	pParticle->m_Size = pSystem->m_ParticleRadius;

	// Add it to the end of the list.
	pParticle->m_pPrev = pSystem->m_ParticleHead.m_pPrev;
	pParticle->m_pNext = &pSystem->m_ParticleHead;
	pParticle->m_pNext->m_pPrev = pParticle;
	pParticle->m_pPrev->m_pNext = pParticle;

	++pSystem->m_nParticles;
	++pSystem->m_nChangedParticles;
	return pParticle;
}


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


// FUNCTION: LITHTECH 0x00469810
void ps_AddParticles(LTParticleSystem *pSystem, uint32 nParticles,
					LTVector *minOffset, LTVector *maxOffset,
					LTVector *minVel, LTVector *maxVel,
					LTVector *minColor, LTVector *maxColor,
					LTFLOAT minLifetime, LTFLOAT maxLifetime )
{
	uint32		i;
	LTFLOAT		t[6];
	LTVector	offset, vel, color;
	LTFLOAT		lifeTime;

	for(i=0; i < nParticles; i++)
	{
		t[0] = (LTFLOAT)rand() / RAND_MAX;
		t[1] = (LTFLOAT)rand() / RAND_MAX;
		t[2] = (LTFLOAT)rand() / RAND_MAX;
		t[3] = (LTFLOAT)rand() / RAND_MAX;
		t[4] = (LTFLOAT)rand() / RAND_MAX;
		t[5] = (LTFLOAT)rand() / RAND_MAX;

		VINTERP(offset, (*minOffset), (*maxOffset), t[0], t[1], t[2]);
		VINTERP(vel, (*minVel), (*maxVel), t[3], t[4], t[5]);
		VINTERP(color, (*minColor), (*maxColor), t[0], t[5], t[3]);
		lifeTime = minLifetime + (maxLifetime-minLifetime) * t[3];

		ps_AddParticle(pSystem, &offset, &color, &vel, lifeTime);
	}
}


// FUNCTION: LITHTECH 0x00469b00
void ps_UpdateParticles(LTParticleSystem *pSystem, LTFLOAT t)
{
	ClientIntersectQuery iQuery;
	ClientIntersectInfo iInfo;
	LTVector basePos = pSystem->GetPos();
	uint32 flags = pSystem->m_psFlags;

	// See if this is a dumb particle system. If it is, bail.
	if(flags & PS_DUMB)
		return;

	float gravityAccel = t * pSystem->m_GravityAccel;

	// Take one of 2 loops.
	PSParticle *pParticle = pSystem->m_ParticleHead.m_pNext;
	PSParticle *pEnd = &pSystem->m_ParticleHead;
	PSParticle *pNext;

	if(flags & PS_NEVERDIE)
	{
		while(pParticle != pEnd)
		{
			pParticle->m_Pos.x += pParticle->m_Velocity.x * t;
			pParticle->m_Pos.y += pParticle->m_Velocity.y * t;
			pParticle->m_Pos.z += pParticle->m_Velocity.z * t;
			ps_UpdateBox(pSystem, &pParticle->m_Pos);
			pParticle->m_Velocity.y += gravityAccel;

			pParticle = pParticle->m_pNext;
		}
	}
	else
	{
		while(pParticle != pEnd)
		{
			pParticle->m_Lifetime -= t;
			if(pParticle->m_Lifetime < 0.0f)
			{
				pNext = pParticle->m_pNext;
				ps_RemoveParticle(pSystem, pParticle);
				pParticle = pNext;
				continue;
			}

			pParticle->m_Pos.x += pParticle->m_Velocity.x * t;
			pParticle->m_Pos.y += pParticle->m_Velocity.y * t;
			pParticle->m_Pos.z += pParticle->m_Velocity.z * t;
			ps_UpdateBox(pSystem, &pParticle->m_Pos);
			pParticle->m_Velocity.y += gravityAccel;

			pParticle = pParticle->m_pNext;
		}
	}

	// Bounce the particles.
	if(flags & PS_BOUNCE)
	{
		VEC_COPY(iQuery.m_From, basePos);
		VEC_COPY(iQuery.m_To, iQuery.m_From);
		iQuery.m_To.y -= 400.0f;

		if(ci_IntersectSegment(&iQuery, &iInfo))
		{
			float yCoord = iInfo.m_Plane.m_Normal.y * iInfo.m_Plane.m_Dist;
			yCoord -= basePos.y;

			pParticle = pSystem->m_ParticleHead.m_pNext;
			pEnd = &pSystem->m_ParticleHead;
			while(pParticle != pEnd)
			{
				if(pParticle->m_Velocity.y < 0.0f)
				{
					if(pParticle->m_Pos.y < yCoord)
					{
						pParticle->m_Pos.y = yCoord;
						pParticle->m_Velocity.y = pParticle->m_Velocity.y * -0.5f;
					}
				}

				pParticle = pParticle->m_pNext;
			}
		}
	}
}


// Temporaries and FPU scheduling differ (0x18-byte frame in the original; the original keeps halfBox in FPU registers
// instead of a spilled local). Tried compound forms (`+=`, `*= 0.5f`), operand orders and a single sum: size 288 reachable
// but never the same schedule.
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
