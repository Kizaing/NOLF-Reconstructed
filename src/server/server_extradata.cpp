// Jupiter runtime/server/src/server_extradata.cpp
// Talon passes the server manager to the per-type init/term functions, loads models through
// its own model cache (m_hModelTable) and still uses .abc files.
#include <string.h>
#include "bdefs.h"
#include "servermgr.h"
#include "server_filemgr.h"
#include "moveobject.h"
#include "smoveabstract.h"
#include "de_world.h"
#include "dhashtable.h"
#include "model.h"

class ExtraDataBackup;

#define AT_MODELCHANGED		(1<<3)

// Loads a child model (.abc) for a model.
// STUB: LITHTECH 0x004781d0
LTRESULT se_LoadChildModel(void *pRequest, Model **ppModel)
{
	return LT_OK;
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

// STUB: LITHTECH 0x004784c0
LTRESULT se_LoadModel(CServerMgr *pServerMgr, const char *pFilename, Model **ppModel)
{
	return LT_OK;
}

// STUB: LITHTECH 0x00478780
LTRESULT se_LoadChildModels(CServerMgr *pServerMgr, Model *pModel, UsedFile *pFile, uint32 flags)
{
	return LT_OK;
}

// STUB: LITHTECH 0x004788c0
LTRESULT se_GetModel(CServerMgr *pServerMgr, char *pFilename, Model **ppModel, UsedFile **ppFile,
	LTBOOL bAddRef, uint32 flags)
{
	return LT_OK;
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
			pInstance->m_AnimTracker.m_Flags |= AT_MODELCHANGED;
			pInstance->m_AnimTracker.SetModel(pServerMgr->m_pDefaultModel);
			nFound++;
		}
	}

	if (nFound)
		pFile->m_Flags |= 1;

	return LT_OK;
}

// STUB: LITHTECH 0x00478ae0
static LTRESULT se_InitModelObject(CServerMgr *pServerMgr, LTObject *pObject, ObjectCreateStruct *pStruct)
{
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


// 0x58 bytes.
class ExtraDataBackup
{
public:
	LTAnimTracker	m_AnimTracker;	// 0x00
	UsedFile		*m_pFile;		// 0x50
	UsedFile		*m_pSkin;		// 0x54
};

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
