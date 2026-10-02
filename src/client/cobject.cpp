// Talon client object updates (Jupiter keeps these in runtime/client/src/clientmgr.cpp as
// CClientMgr::UpdateAnimations etc.; the Talon helpers are free functions taking the manager,
// and the file sorts between cnet.cpp and collision.cpp).
#include <stdio.h>
#include "bdefs.h"
#include "clientmgr.h"
#include "clientshell.h"
#include "iclientshell.h"
#include "counter.h"
#include "sprite.h"
#include "animtracker.h"
#include "client_filemgr.h"

// Client object flags (ILTClient::SetObjectClientFlags).
#define CF_NOTIFYMODELKEYS	(1<<1)

// GLOBAL: LITHTECH 0x004e048c
uint32 g_Ticks_UpdateObjects;

void ps_UpdateParticles(LTParticleSystem *pSystem, LTFLOAT t);	// 0x00469b00
void ps_StartUpdatingPositions(LTParticleSystem *pSystem);		// 0x00469fc0
LTBOOL ps_EndUpdatingPositions(LTParticleSystem *pSystem);		// 0x00469ff0
// 0x00426010
LTRESULT cm_AddSharedTexture2(CClientMgr *pClientMgr, FileRef *pRef, SharedTexture* &pTexture);


// Animated world textures.
// FUNCTION: LITHTECH 0x00417d80
static void UpdateAnimations(CClientMgr *pClientMgr)
{
	SurfaceSprite *pCur;
	uint32 msFrameTime;

	if (pClientMgr->m_pCurShell)
	{
		msFrameTime = (uint32)(pClientMgr->m_pCurShell->m_GameFrameTime * 1000.0f);
		if (msFrameTime > 0)
		{
			for (pCur=pClientMgr->m_SurfaceSprites; pCur; pCur=pCur->m_pNext)
			{
				spr_UpdateTracker(&pCur->m_SpriteTracker, msFrameTime);
				if (pCur->m_SpriteTracker.m_pCurFrame)
				{
					pCur->m_pSurface->m_pTexture = pCur->m_SpriteTracker.m_pCurFrame->m_pTex;
				}
			}
		}
	}
}

static void UpdateModelAnimations(CClientMgr *pClientMgr);
static void UpdateParticleSystems(CClientMgr *pClientMgr);
static void UpdatePolyGrids(CClientMgr *pClientMgr);
static void UpdateLineSystems(CClientMgr *pClientMgr);
static void UpdateModels(CClientMgr *pClientMgr);

// FUNCTION: LITHTECH 0x00417de0
void CClientMgr::UpdateObjects()
{
	g_Ticks_UpdateObjects = 0;
	CountAdder cntUpdate(&g_Ticks_UpdateObjects);

	UpdateAnimations(this);
	UpdateModelAnimations(this);
	UpdateParticleSystems(this);
	UpdatePolyGrids(this);
	UpdateLineSystems(this);
	UpdateModels(this);
}


// FUNCTION: LITHTECH 0x00417e50
static void UpdateParticleSystems(CClientMgr *pClientMgr)
{
	LTLink *pCur;
	LTParticleSystem *pSystem;
	uint32 flags;
	int msFrameTime, nChangedParticles;
	LTBOOL bMoved;

	msFrameTime = (int)(pClientMgr->m_FrameTime * 1000.0f);
	if (msFrameTime > 0)
	{
		for (pCur=pClientMgr->m_ObjectMgr.m_ObjectLists[OT_PARTICLESYSTEM].m_Head.m_pNext;
			pCur != &pClientMgr->m_ObjectMgr.m_ObjectLists[OT_PARTICLESYSTEM].m_Head; pCur=pCur->m_pNext)
		{
			pSystem = (LTParticleSystem*)pCur->m_pData;

			// Set its FLAG_WASDRAWN appropriately and avoid updating if possible.
			flags = pSystem->m_Flags;
			pSystem->m_Flags &= ~(FLAG_WASDRAWN | FLAG_INTERNAL1);
			if (!(flags & FLAG_UPDATEUNSEEN) && !(flags & FLAG_INTERNAL1) && !pSystem->m_nChangedParticles)
			{
				continue;
			}
			pSystem->m_Flags |= FLAG_WASDRAWN;

			// Update its sprite.
			if (pSystem->m_pSprite)
			{
				spr_UpdateTracker((SpriteTracker*)pSystem->m_SpriteTracker, msFrameTime);
				if (((SpriteTracker*)pSystem->m_SpriteTracker)->m_pCurFrame)
					pSystem->m_pCurTexture = ((SpriteTracker*)pSystem->m_SpriteTracker)->m_pCurFrame->m_pTex;
				else
					pSystem->m_pCurTexture = LTNULL;
			}

			nChangedParticles = pSystem->m_nChangedParticles;
			ps_StartUpdatingPositions(pSystem);
			ps_UpdateParticles(pSystem, pClientMgr->m_FrameTime);
			bMoved = ps_EndUpdatingPositions(pSystem);

			// Do MoveObject to get it located correctly.
			if (nChangedParticles > 0 || bMoved)
			{
				cm_MoveObject(pClientMgr, pSystem, &pSystem->m_Pos, LTTRUE);
			}

			pSystem->m_nChangedParticles = 0;
		}
	}
}


static void ClientStringKeyCallback(LTAnimTracker *pTracker, AnimKeyFrame *pFrame, char *pExtraCmd, LTBOOL bReplace);

// FUNCTION: LITHTECH 0x00417f60
static void UpdateModelAnimations(CClientMgr *pClientMgr)
{
	LTLink *pCur, *pListHead;
	ModelInstance *pInst;
	LTAnimTracker *pTracker;
	int msFrameTime;

	msFrameTime = (int)(pClientMgr->m_FrameTime * 1000.0f);
	if (msFrameTime > 0)
	{
		// Update model animations.
		pListHead = &pClientMgr->m_ObjectMgr.m_ObjectLists[OT_MODEL].m_Head;
		for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			pInst = (ModelInstance*)pCur->m_pData;

			for (pTracker=pInst->m_AnimTrackers; pTracker; pTracker=pTracker->GetNext())
			{
				pTracker->m_StringKeyCallback = ClientStringKeyCallback;
				trk_Update(pTracker, msFrameTime);
			}
		}

		// Update sprite instances.
		pListHead = &pClientMgr->m_ObjectMgr.m_ObjectLists[OT_SPRITE].m_Head;
		for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			spr_UpdateTracker((SpriteTracker*)((SpriteInstance*)pCur->m_pData)->m_SpriteTracker, msFrameTime);
		}
	}
}


// FUNCTION: LITHTECH 0x00418000
static void ClientStringKeyCallback(LTAnimTracker *pTracker, AnimKeyFrame *pFrame, char *pExtraCmd, LTBOOL bReplace)
{
	ArgList argList;
	char cmd[128];
	ConParse parse;
	ModelInstance *pInst;
	CClientMgr *pClientMgr;

	pInst = pTracker->GetModelInstance();
	if (!pInst)
		return;

	if (!(pInst->m_Unknown188 & CF_NOTIFYMODELKEYS))
		return;

	pClientMgr = g_pClientMgr;
	if (!pClientMgr->m_pClientShell)
		return;

	if (pExtraCmd)
	{
		if (pFrame->m_pString[0] && !bReplace)
			sprintf(cmd, "%s; %s", pFrame->m_pString, pExtraCmd);
		else
			sprintf(cmd, "%s", pExtraCmd);

		parse.Init(cmd);
	}
	else
	{
		parse.Init(pFrame->m_pString);
	}

	argList.argv = parse.m_Args;
	while (parse.Parse())
	{
		if (parse.m_nArgs > 0)
		{
			argList.argc = parse.m_nArgs;
			pClientMgr->m_pClientShell->OnModelKey((HLOCALOBJ)pInst, &argList, pTracker->m_Index);
		}
	}
}


// FUNCTION: LITHTECH 0x00418120
static void UpdatePolyGrids(CClientMgr *pClientMgr)
{
	LTLink *pListHead, *pCur;
	LTPolyGrid *pGrid;
	uint32 flags, msFrameTime;

	msFrameTime = (uint32)(pClientMgr->m_FrameTime * 1000.0f);
	if (msFrameTime)
	{
		pListHead = &pClientMgr->m_ObjectMgr.m_ObjectLists[OT_POLYGRID].m_Head;
		for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			pGrid = (LTPolyGrid*)pCur->m_pData;

			// Set its FLAG_WASDRAWN appropriately and avoid updating if possible.
			flags = pGrid->m_Flags;
			pGrid->m_Flags &= ~(FLAG_WASDRAWN | FLAG_INTERNAL1);
			if (!(flags & FLAG_UPDATEUNSEEN) && !(flags & FLAG_INTERNAL1))
			{
				continue;
			}
			pGrid->m_Flags |= FLAG_WASDRAWN;

			if (pGrid->m_pSprite)
			{
				spr_UpdateTracker((SpriteTracker*)pGrid->m_SpriteTracker, msFrameTime);
			}
		}
	}
}


// FUNCTION: LITHTECH 0x004181a0
static void UpdateLineSystems(CClientMgr *pClientMgr)
{
	LTLink *pListHead, *pCur;
	LineSystem *pSystem;

	pListHead = &pClientMgr->m_ObjectMgr.m_ObjectLists[OT_LINESYSTEM].m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pSystem = (LineSystem*)pCur->m_pData;

		if (pSystem->m_bChanged)
		{
			// Do MoveObject to get it located correctly.
			cm_MoveObject(pClientMgr, pSystem, &pSystem->m_Pos, LTTRUE);
		}

		pSystem->m_bChanged = LTFALSE;
	}
}


// FUNCTION: LITHTECH 0x004181f0
static void UpdateModels(CClientMgr *pClientMgr)
{
	LTLink *pListHead, *pCur;
	ModelInstance *pModel;
	uint32 i, msFrameTime;
	FileRef skinName;

	msFrameTime = (uint32)(pClientMgr->m_FrameTime * 1000.0f);
	if (msFrameTime)
	{
		pListHead = &pClientMgr->m_ObjectMgr.m_ObjectLists[OT_MODEL].m_Head;
		for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			pModel = (ModelInstance*)pCur->m_pData;

			for (i=0; i < MAX_MODEL_TEXTURES; i++)
			{
				if (pModel->m_pSprites[i])
				{
					spr_UpdateTracker((SpriteTracker*)pModel->m_SpriteTrackers[i], msFrameTime);

					skinName.m_pFilename = ((SpriteTracker*)pModel->m_SpriteTrackers[i])->m_pCurFrame->m_pTex->m_pFile->m_Filename;
					skinName.m_FileType = FILE_CLIENTFILE;
					cm_AddSharedTexture2(g_pClientMgr, &skinName, pModel->m_pSkins[i]);
				}
			}
		}
	}
}
