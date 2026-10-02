// Jupiter runtime/server/src/server_extradata.cpp
// Talon passes the server manager to the per-type init/term functions, loads models through
// its own model cache (m_hModelTable) and still uses .abc files.
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include "bdefs.h"
#include "servermgr.h"
#include "server_filemgr.h"
#include "moveobject.h"
#include "smoveabstract.h"
#include "de_world.h"
#include "dhashtable.h"
#include "model.h"
#include "server_extradata.h"
#include "serverde_impl.h"
#include "animtracker.h"
#include "../../build/proj/LT2/lithshared/stdlith/helpers.h"
#include "nexus.h"
#include "de_memory.h"
#include "lthread.h"
#include "sloaderthread.h"

// What the server's leech on a model gets (s_object.cpp's sm_OnModelUnload).
struct ModelUnloadRequest
{
	ModelUnloadRequest(CServerMgr *pServerMgr, Model *pModel)
	{
		m_pServerMgr = pServerMgr;
		m_pModel = pModel;
	}

	CServerMgr	*m_pServerMgr;	// 0x00
	Model		*m_pModel;		// 0x04
};


// What se_LoadChildModel gets as ModelLoadRequest::m_pLoadFnUserData.
struct ServerLoadChildInfo
{
	CServerMgr	*m_pServerMgr;		// 0x00
	const char	*m_pFilename;		// 0x04 the parent model
};

Leech* nexus_CreateLeech(LeechDef *pDef, void *pUserData);	// 0x004448f0
LTRESULT nexus_AddLeech(Nexus *pNexus, Leech *pLeech);		// 0x00444970
void clienthack_ModelLoaded(Model *pModel);					// 0x00416750
LTRESULT sm_OnModelUnload(void *pUser, struct ModelUnloadMsg *pMsg, LTRESULT status);	// 0x00478150

// The leech the server puts on the models it loads: sm_OnModelUnload drops them from the cache.
// GLOBAL: LITHTECH 0x004d5a30
extern LeechDef g_ServerModelLeechDef;

// Loads a child model (the model's path is the parent's directory).
// Remaining diff: the two LT_MISSINGMODELFILE error tails swap places (the original keeps the first
// copy of the shared epilogue, VC6 keeps the last).
// STUB: LITHTECH 0x004781d0
LTRESULT se_LoadChildModel(ModelLoadRequest *pRequest, Model **ppModel)
{
	ServerLoadChildInfo *pInfo;
	UsedFile *pUsedFile;
	HHashElement *hElement;
	Model *pModel;
	LTRESULT dResult;
	char fullName[512];
	char path[512];

	*ppModel = LTNULL;
	pInfo = (ServerLoadChildInfo*)pRequest->m_pLoadFnUserData;

	// The child model is in the same directory as its parent.
	CHelpers::ExtractNames(pInfo->m_pFilename, path, LTNULL, LTNULL, LTNULL);
	if (path[0] == 0)
		sprintf(fullName, "%s", pRequest->m_pFilename);
	else
		sprintf(fullName, "%s\\%s", path, pRequest->m_pFilename);

	// Already loaded?
	hElement = hs_FindElement(pInfo->m_pServerMgr->m_hModelTable, fullName, strlen(fullName));
	if (hElement)
	{
		*ppModel = (Model*)hs_GetElementUserData(hElement);
		return LT_OK;
	}

	if (!sf_AddUsedFile(&pInfo->m_pServerMgr->m_FileMgr, fullName, 0, &pUsedFile))
	{
		RETURN_ERROR_PARAM(1, se_LoadChildModel, LT_MISSINGMODELFILE, fullName);
	}

	pModel = new Model(&g_DefAlloc, &g_DefAlloc);
	if (!pModel)
	{
		RETURN_ERROR(1, se_LoadChildModel, LT_OUTOFMEMORY);
	}

	pRequest->m_pFile = sf_OpenFile3(&pInfo->m_pServerMgr->m_FileMgr, pUsedFile);
	if (!pRequest->m_pFile)
	{
		RETURN_ERROR_PARAM(1, se_LoadChildModel, LT_MISSINGMODELFILE, fullName);
	}

	dResult = pModel->InitAllocations(*pRequest->m_pFile, &g_DefAlloc);
	if (dResult == LT_OK)
	{
		DEBUG_PRINT(3, ("Calling Model::Load():  %s\n", pRequest->m_pFilename));
		dResult = pModel->Load(pRequest);
	}

	pRequest->m_pFile->Release();

	if (dResult == LT_OK && pModel->SetFilename(fullName))
	{
		pModel->m_FileID = pUsedFile->m_FileID;
		if (se_LoadChildModels(pInfo->m_pServerMgr, pModel, pUsedFile, 0) == 0)
		{
			*ppModel = pModel;
			return LT_OK;
		}
	}

	delete pModel;
	RETURN_ERROR(1, se_LoadChildModel, LT_ERROR);
}

// Turns forward slashes into backslashes, in place.
// FUNCTION: LITHTECH 0x00478490
char* se_FixSlashes(char *pFilename)
{
	char *pRet, c;

	if (!pFilename)
		return LTNULL;

	pRet = pFilename;
	c = *pFilename;
	while (c)
	{
		if (c == '/')
			*pFilename = '\\';
		c = *++pFilename;
	}

	return pRet;
}

// Loads a model's data from its used file (not the cache).
// FUNCTION: LITHTECH 0x004784c0
LTRESULT se_LoadModelData(CServerMgr *pServerMgr, const char *pFilename, UsedFile *pFile, Model **ppModel)
{
	ModelLoadRequest request;
	ServerLoadChildInfo childInfo;
	Model *pModel;
	LTRESULT dResult;

	request.m_pFilename = LTNULL;
	request.m_pFile = LTNULL;
	request.m_LoadChildFn = DefaultLoadChildFn;
	request.m_pLoadFnUserData = LTNULL;
	request.m_bLoadChildModels = LTTRUE;
	request.m_Unknown18 = LTTRUE;
	request.m_bTreesValid = LTTRUE;
	request.m_bAllChildrenLoaded = LTTRUE;
	request.m_pExtraChildModels = LTNULL;

	*ppModel = LTNULL;

	pModel = new Model(&g_DefAlloc, &g_DefAlloc);
	if (!pModel)
	{
		RETURN_ERROR(1, se_LoadModelData, LT_OUTOFMEMORY);
	}

	se_FixSlashes((char*)pFilename);

	request.m_pFile = sf_OpenFile3(&pServerMgr->m_FileMgr, pFile);
	if (!request.m_pFile)
	{
		delete pModel;
		DEBUG_PRINT(1, ("Couldn't open model file %s", pFilename));
		RETURN_ERROR_PARAM(1, se_LoadModelData, LT_MISSINGFILE, pFilename);
	}

	dResult = pModel->InitAllocations(*request.m_pFile, &g_DefAlloc);
	if (dResult == LT_OK)
	{
		childInfo.m_pServerMgr = pServerMgr;
		childInfo.m_pFilename = pFilename;
		request.m_LoadChildFn = se_LoadChildModel;
		request.m_pLoadFnUserData = &childInfo;
		request.m_pExtraChildModels = ((CLTServer*)pServerMgr->m_pServerInterface)->GetChildModelLinkMap();
		DEBUG_PRINT(3, ("Calling Model::Load():  %s\n", pFilename));
		dResult = pModel->Load(&request);
	}

	request.m_pFile->Release();

	if (dResult == LT_OK && pModel->SetFilename(pFilename))
	{
		// (The trees-valid flag Talon checks is the one at 0x18, model.h's m_Unknown18.)
		if (!request.m_Unknown18)
		{
			delete pModel;
			DEBUG_PRINT(1, ("Child model trees invalid in model %s", pFilename));
			RETURN_ERROR(1, se_LoadModelData, LT_INVALIDMODELFILE);
		}
		else if (!request.m_bAllChildrenLoaded)
		{
			delete pModel;
			DEBUG_PRINT(1, ("Missing one or more child models in model %s", pFilename));
			RETURN_ERROR(1, se_LoadModelData, LT_INVALIDMODELFILE);
		}

		pModel->m_FileID = pFile->m_FileID;
		*ppModel = pModel;
		return LT_OK;
	}

	delete pModel;
	DEBUG_PRINT(1, ("Error %d loading model %s", dResult, pFilename));
	return dResult;
}

// A model finished loading: adds it to the cache, gives it to the objects that were waiting for
// it and lets the client share it.
// STUB: LITHTECH 0x00478780
// Fragile: matched in wave 4, then the reloads of pServerMgr/pModel before `new` swapped after header changes.
LTBOOL se_LoadChildModels(CServerMgr *pServerMgr, Model *pModel, UsedFile *pFile, uint32 flags)
{
	HHashElement *hElement;
	LTLink *pCur, *pListHead;
	ModelInstance *pInstance;
	Model *pOldModel;
	ModelUnloadRequest *pRequest;

	DEBUG_PRINT(2, ("(Server) loaded model: %s", pModel->GetFilename()));
	DEBUG_PRINT(3, ("Adding model to resource list: %s\n", pModel->GetFilename()));

	hElement = hs_AddElement(pServerMgr->m_hModelTable, pModel->GetFilename(), strlen(pModel->GetFilename()));
	hs_SetElementUserData(hElement, pModel);

	// Give it to the objects that were waiting for it.
	if (flags && (pFile->m_Flags & 1))
	{
		pListHead = &pServerMgr->m_ObjectMgr.m_ObjectLists[OT_MODEL].m_Head;
		pCur = pListHead->m_pNext;
		for (; pCur != pListHead; pCur=pCur->m_pNext)
		{
			pInstance = (ModelInstance*)pCur->m_pData;
			if (pInstance->sd->m_pFile == pFile)
			{
				pOldModel = pInstance->GetModelDB();
				if (pOldModel->m_RefCount > 0)
					--pOldModel->m_RefCount;

				pInstance->m_AnimTracker.SetModel(pModel);
				++pModel->m_RefCount;
				pInstance->m_AnimTracker.m_Flags &= ~AT_ALLOWINVALID;
			}
		}

		pFile->m_Flags &= ~1;
	}

	// The server's leech on the model, and let the client share it.
	pRequest = new ModelUnloadRequest(pServerMgr, pModel);
	nexus_AddLeech(&pModel->m_Nexus, nexus_CreateLeech(&g_ServerModelLeechDef, pRequest));
	clienthack_ModelLoaded(pModel);

	return hElement == LTNULL;
}

// Drops a model from the server's cache; objects using it fall back to the default model.
// Register allocation: pServerMgr/pModel/nFound land in esi/edi/edx instead of edi/ebx/esi.
// STUB: LITHTECH 0x00478a20
LTRESULT se_UncacheModel(CServerMgr *pServerMgr, const char *pFilename, UsedFile *pFile)
{
	HHashElement *hElement;
	Model *pModel;
	LTLink *pCur;
	ModelInstance *pInstance;
	uint32 nFound;

	hElement = hs_FindElement(pServerMgr->m_hModelTable, pFilename, strlen(pFilename));
	if (!hElement)
		return LT_NOTFOUND;

	pModel = (Model*)hs_GetElementUserData(hElement);
	DEBUG_PRINT(3, ("Removing model from resource list: %s\n", pModel->GetFilename()));
	delete pModel;

	nFound = 0;
	for (pCur=pServerMgr->m_ObjectMgr.m_ObjectLists[OT_MODEL].m_Head.m_pNext;
		pCur != &pServerMgr->m_ObjectMgr.m_ObjectLists[OT_MODEL].m_Head; pCur=pCur->m_pNext)
	{
		pInstance = (ModelInstance*)pCur->m_pData;
		if (pInstance->GetModelDB() == pModel)
		{
			pInstance->m_AnimTracker.m_Flags |= AT_ALLOWINVALID;
			pInstance->m_AnimTracker.SetModel(pServerMgr->m_pDefaultModel);
			nFound++;
		}
	}

	if (nFound)
		pFile->m_Flags |= 1;

	return LT_OK;
}

// Sets up a model object: its file, skins and animation trackers.
// Remaining diff: register allocation (the original keeps pModel in ebx and pStruct in edi).
// STUB: LITHTECH 0x00478ae0
static LTRESULT se_InitModelObject(CServerMgr *pServerMgr, LTObject *pObject, ObjectCreateStruct *pStruct)
{
	ModelInstance *pInstance;
	char *pFilename;
	LTAnimTracker *pTracker;
	LTRESULT dResult;
	ServerFileMgr *pFileMgr;
	uint32 i;

	pInstance = (ModelInstance*)pObject;
	pFileMgr = &pServerMgr->m_FileMgr;

	pFilename = pStruct->m_Filename;
	if (!sf_AddUsedFile(pFileMgr, pFilename, 0, &pObject->sd->m_pFile))
	{
		DEBUG_PRINT(1, ("Couldn't find model file %s.  Trying models/default.abc", pFilename));

		pFilename = "models\\default.abc";
		if (!sf_AddUsedFile(pFileMgr, pFilename, 0, &pObject->sd->m_pFile))
		{
			RETURN_ERROR_PARAM(1, se_InitModel, LT_MISSINGFILE, pStruct->m_Filename);
		}
	}

	// Setup the skins.
	for (i=0; i < MAX_MODEL_TEXTURES; i++)
	{
		if (pStruct->m_SkinNames[i][0] != 0)
		{
			if (!sf_AddUsedFile(pFileMgr, pStruct->m_SkinNames[i], 0, &pObject->sd->m_pSkins[i]))
			{
				dsi_ConsolePrint("Couldn't find model skin %s.", pStruct->m_SkinNames[i]);
				sf_AddUsedFile(pFileMgr, "skins\\default.dtx", 0, &pObject->sd->m_pSkins[i]);
			}
		}
	}

	Model *pModel;
	UsedFile *pFile;

	dResult = se_GetModel(pServerMgr, pFilename, &pModel, &pFile, pStruct->m_CreateFlags & OCS_AUTOLOAD,
		pStruct->m_CreateFlags & OCS_AUTOLOAD);
	if (dResult == LT_OK)
	{
		// Point the trackers at the model.
		for (pTracker=pInstance->m_AnimTrackers; pTracker; pTracker=pTracker->GetNext())
		{
			if (!pTracker->IsValid() || pTracker->GetModel() != pModel)
				trk_Init(pTracker, pModel, 0);
		}
	}
	else if (dResult == LT_INPROGRESS)
	{
		// Still loading: use the default model until it's here.
		for (pTracker=pInstance->m_AnimTrackers; pTracker; pTracker=pTracker->GetNext())
		{
			if (!pTracker->IsValid() || pTracker->GetModel() != pModel)
			{
				trk_Init(pTracker, pServerMgr->m_pDefaultModel, 0);
				pTracker->m_Flags |= AT_ALLOWINVALID;
			}
		}

		pObject->sd->m_pFile->m_Flags |= 1;
	}
	else
	{
		return dResult;
	}

	pModel->m_RefCount++;
	pInstance->m_AnimTracker.m_Flags |= AT_DOCALLBACKS;
	pInstance->m_Unknown2CC = 1;
	pInstance->m_AnimTracker.m_pUser1 = pInstance;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00478cf0
static LTRESULT se_TermModelObject(CServerMgr *pServerMgr, LTObject *pObject)
{
	ModelInstance *pModelInstance;
	Model *pModel;

	// Get the model instance
	pModelInstance = (ModelInstance*)pObject;
	if (!pModelInstance)
		return LT_ERROR;

	pModel = pModelInstance->GetModelDB();
	if (pModel->m_RefCount > 0)
		--pModel->m_RefCount;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00478d20
static LTRESULT se_InitSprite(CServerMgr *pServerMgr, LTObject *pObject, ObjectCreateStruct *pStruct)
{
	SpriteInstance *pSpriteInstance;
	char *pFilename;
	const char *pTestFilename;

	pFilename = pStruct->m_Filename;
	pSpriteInstance = (SpriteInstance*)pObject;
	pSpriteInstance->m_ClipperPoly = INVALID_HPOLY;

	if (sf_AddUsedFile(&pServerMgr->m_FileMgr, pFilename, 0, &pObject->sd->m_pFile) == 0)
	{
		DEBUG_PRINT(1, ("Couldn't find sprite file %s, using sprites/default.spr.", pFilename));

		pTestFilename = "sprites\\default.spr";
		if (sf_AddUsedFile(&pServerMgr->m_FileMgr, pTestFilename, 0, &pObject->sd->m_pFile) == 0)
		{
			RETURN_ERROR_PARAM(1, se_InitSprite, LT_MISSINGFILE, pTestFilename);
		}
	}

	return LT_OK;
}

// Register allocation: pServerMgr and the zero constant swap ebx/edi.
// STUB: LITHTECH 0x00478de0
static LTRESULT se_InitWorldModel(CServerMgr *pServerMgr, LTObject *pObject, ObjectCreateStruct *pStruct)
{
	WorldModelInstance *pInstance;
	WorldBsp *pModelBsp;
	LTVector newDims;
	MoveState moveState;

	pInstance = (WorldModelInstance*)pObject;
	if (pServerMgr->m_World.InitWorldModel(pInstance, pStruct->m_Filename))
	{
		if (!pInstance->m_pOriginalBsp->IsUntransformed())
		{
			pModelBsp = pInstance->m_pOriginalBsp;
			newDims = (pModelBsp->m_MaxBox - pModelBsp->m_MinBox) * 0.5f;

			// Move the OBJECT to the center of the world model geometry.
			pObject->SetPos(pModelBsp->m_WorldTranslation);

			// Set its dims for it.
			moveState.Setup(&pServerMgr->m_World.m_WorldTree, (MoveAbstract*)pServerMgr->m_MoveAbstract,
				pObject, pObject->m_BPriority);
			ChangeObjectDimensions(&moveState, &newDims, LTFALSE, LTTRUE);
		}

		return LT_OK;
	}
	else
	{
		RETURN_ERROR(1, se_InitWorldModel, LT_MISSINGWORLDMODEL);
	}
}

// FUNCTION: LITHTECH 0x00478f70
static LTRESULT se_InitContainer(CServerMgr *pServerMgr, LTObject *pObject, ObjectCreateStruct *pStruct)
{
	return se_InitWorldModel(pServerMgr, pObject, pStruct);
}


struct ExtraDataStruct
{
	LTRESULT (*Init)(CServerMgr *pServerMgr, LTObject *pObject, ObjectCreateStruct *pStruct);
	LTRESULT (*Term)(CServerMgr *pServerMgr, LTObject *pObject);
};

// GLOBAL: LITHTECH 0x004d5a38
ExtraDataStruct g_ExtraDataStructs[NUM_OBJECTTYPES] =
{
	LTNULL, LTNULL,							// OT_NORMAL
	se_InitModelObject, se_TermModelObject,	// OT_MODEL
	se_InitWorldModel, LTNULL,				// OT_WORLDMODEL
	se_InitSprite, LTNULL,					// OT_SPRITE
	LTNULL, LTNULL,							// OT_LIGHT
	LTNULL, LTNULL,							// OT_CAMERA
	LTNULL, LTNULL,							// OT_PARTICLESYSTEM
	LTNULL, LTNULL,							// OT_POLYGRID
	LTNULL, LTNULL,							// OT_LINESYSTEM
	se_InitContainer, LTNULL,				// OT_CONTAINER
	LTNULL, LTNULL							// OT_CANVAS
};


// FUNCTION: LITHTECH 0x00478f80
LTRESULT sm_InitExtraData(CServerMgr *pServerMgr, LTObject *pObject, ObjectCreateStruct *pStruct)
{
	if (g_ExtraDataStructs[(char)pObject->m_ObjectType].Init)
		return g_ExtraDataStructs[(char)pObject->m_ObjectType].Init(pServerMgr, pObject, pStruct);
	else
		return LT_OK;
}

// FUNCTION: LITHTECH 0x00478fa0
LTRESULT sm_TermExtraData(CServerMgr *pServerMgr, LTObject *pObject)
{
	if (g_ExtraDataStructs[(char)pObject->m_ObjectType].Term)
		return g_ExtraDataStructs[(char)pObject->m_ObjectType].Term(pServerMgr, pObject);
	else
		return LT_OK;
}


// FUNCTION: LITHTECH 0x00478fc0
LTRESULT BackupExtraData(LTObject *pObject, ExtraDataBackup *pBackup)
{
	if (!pObject || !pObject->sd)
	{
		RETURN_ERROR(1, BackupExtraData, LT_INVALIDPARAMS);
	}

	pBackup->m_pFile = pObject->sd->m_pFile;
	pBackup->m_pSkin = pObject->sd->m_pSkins[0];

	if (pObject->m_ObjectType == OT_MODEL)
	{
		pBackup->m_AnimTracker = ((ModelInstance*)pObject)->m_AnimTracker;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00479040
LTRESULT RestoreExtraData(LTObject *pObject, ExtraDataBackup *pBackup)
{
	if (!pObject || !pObject->sd)
	{
		RETURN_ERROR(1, RestoreExtraData, LT_INVALIDPARAMS);
	}

	pObject->sd->m_pFile = pBackup->m_pFile;
	pObject->sd->m_pSkins[0] = pBackup->m_pSkin;

	if (pObject->m_ObjectType == OT_MODEL)
	{
		((ModelInstance*)pObject)->m_AnimTracker = pBackup->m_AnimTracker;
	}

	return LT_OK;
}
