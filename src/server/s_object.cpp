// Jupiter runtime/server/src/s_object.cpp
// Talon passes the server manager explicitly to most of these.
#include <string.h>
#include <stdio.h>
#include "bdefs.h"
#include "ltengineobjects.h"
#include "s_object.h"
#include "servermgr.h"
#include "dhashtable.h"
#include "de_memory.h"
#include "model.h"

// Change flags (ServerData::m_ChangeFlags).
#define CF_NEWOBJECT		(1<<0)
#define CF_FLAGS			(1<<3)
#define CF_SCALE			(1<<4)
#define CF_MODELINFO		(1<<5)
#define CF_RENDERINFO		(1<<6)
#define CF_ATTACHMENTS		(1<<8)
#define CF_TELEPORT			(1<<9)
#define CF_SNAPROTATION		(1<<10)

#define IFLAG_INACTIVE_MASK		0x38
#define IFLAG_FROMCLIENTREF		(1<<7)	// Created from a client reference (keepalive).
#define IFLAG_HASCHILDMODELS	(1<<10)

void w_RemoveObjectFromLeaf(LTObject *pObj);	// 0x00430680
LTRESULT sm_RemoveObjectFromWorld(CServerMgr *pServerMgr, LPBASECLASS pObject);


// FUNCTION: LITHTECH 0x00476e80
LTRESULT sm_UpdateInBspStatus(CServerMgr *pServerMgr, LTObject *pObject)
{
	if (!CanOptimizeObject(pObject))
	{
		if (pObject->m_WTFrameCode == FRAMECODE_NOTINTREE)
		{
			pServerMgr->m_World.m_WorldTree.InsertObject(pObject, 0);
		}
	}
	else
	{
		pObject->RemoveFromWorldTree();
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00476ed0
uint32 sm_GetNewObjectChangeFlags(CServerMgr *pServerMgr, LTObject *pObject)
{
	uint32 changeFlags;

	changeFlags = CF_NEWOBJECT | CF_TELEPORT;

	if (pObject->m_Rotation.m_Quat[0] != 0.0f || pObject->m_Rotation.m_Quat[1] != 0.0f ||
		pObject->m_Rotation.m_Quat[2] != 0.0f || pObject->m_Rotation.m_Quat[3] != 1.0f)
	{
		changeFlags |= CF_SNAPROTATION;
	}

	if (pObject->m_Flags & CLIENT_FLAGMASK || pObject->m_UserFlags != 0)
	{
		changeFlags |= CF_FLAGS;
	}

	if (pObject->m_Scale.x != 1.0f || pObject->m_Scale.y != 1.0f || pObject->m_Scale.z != 1.0f)
	{
		changeFlags |= CF_SCALE;
	}

	// Sprites default to 255:255:255:255, everything else to 0:0:0:255.
	if (pObject->m_ObjectType == OT_SPRITE)
	{
		if (pObject->m_ColorR != 255 || pObject->m_ColorG != 255 || pObject->m_ColorB != 255 || pObject->m_ColorA != 255)
			changeFlags |= CF_RENDERINFO;
	}
	else
	{
		if (pObject->m_ColorR != 0 || pObject->m_ColorG != 0 || pObject->m_ColorB != 0 || pObject->m_ColorA != 255)
			changeFlags |= CF_RENDERINFO;
	}

	if (pObject->m_ObjectType == OT_MODEL)
	{
		changeFlags |= CF_MODELINFO;
	}

	if (pObject->m_Attachments ||
		(pObject->m_ObjectType == OT_MODEL && ((ModelInstance*)pObject)->m_HiddenPieces) ||
		(pObject->m_InternalFlags & IFLAG_HASCHILDMODELS))
	{
		changeFlags |= CF_ATTACHMENTS;
	}

	return changeFlags;
}

// Frees all the cached models.
// FUNCTION: LITHTECH 0x00476ff0
void sm_FreeAllModels(CServerMgr *pServerMgr)
{
	HHashIterator *hIterator;
	HHashElement *hElement;
	Model *pModel;

	hIterator = hs_GetFirstElement(pServerMgr->m_hModelTable);
	while (hIterator)
	{
		pModel = (Model*)hs_GetElementUserData(hs_GetNextElement(hIterator));
		pModel->m_RefCount++;
	}

	hIterator = hs_GetFirstElement(pServerMgr->m_hModelTable);
	while (hIterator)
	{
		hElement = hs_GetNextElement(hIterator);
		pModel = (Model*)hs_GetElementUserData(hElement);

		if (pModel->m_RefCount > 0)
			pModel->m_RefCount--;

		if (pModel->m_RefCount == 0)
		{
			DEBUG_PRINT(3, ("Removing model from resource list:  %s\n", pModel->GetFilename()));

			hs_RemoveElement(pServerMgr->m_hModelTable, hElement);
			pModel->Delete();
		}
	}

	// Whine about the ones still in use.
	hIterator = hs_GetFirstElement(pServerMgr->m_hModelTable);
	while (hIterator)
	{
		pModel = (Model*)hs_GetElementUserData(hs_GetNextElement(hIterator));
		DEBUG_PRINT(3, ("Warning!  Server unable to unload model %s due to reference count!", pModel->GetFilename()));
	}
}

// STUB: LITHTECH 0x00477120
void sm_UpdateObject(CServerMgr *pServerMgr, LTObject *pObj)
{
}

// The out-of-line copy of the s_object.h inline.
// FUNCTION: LITHTECH 0x00477540 ?SetObjectChangeFlags@@YAKPAVCServerMgr@@PAVLTObject@@K@Z
LTRESULT (*g_pfnSetObjectChangeFlags)(CServerMgr *pServerMgr, LTObject *pObj, uint32 flags) = SetObjectChangeFlags;

// Model string key callback. Talon can append (or substitute) an extra command string.
// FUNCTION: LITHTECH 0x00477640
void ServerStringKeyCallback(LTAnimTracker *pTracker, AnimKeyFrame *pFrame, char *pExtraCmd, LTBOOL bReplace)
{
	LTObject *pObj;
	ArgList argList;
	char cmd[128];
	ConParse parse;

	// Make sure we have a server object for it.
	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		// Does this object care?
		if (pObj->m_Flags & FLAG_MODELKEYS)
		{
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
					pObj->sd->m_pObject->EngineMessageFn(MID_MODELSTRINGKEY, &argList, (float)pTracker->m_Index);
				}
			}
		}
	}
}

// STUB: LITHTECH 0x00477750
LTRESULT LoadObjects(CServerMgr *pServerMgr, ILTStream *pStream, char *pWorldName, LTBOOL bAllObjects)
{
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00477ce0
void AddObjectToRemoveList(CServerMgr *pServerMgr, LTObject *pObj)
{
	LTLink *pLink;

	if ((~pServerMgr->m_InternalFlags & SIFLAG_REMOVINGALLOBJECTS) &&
		(~pObj->m_InternalFlags & IFLAG_OBJECTGOINGAWAY))
	{
		pLink = g_DLinkBank.Allocate();
		pLink->m_pData = pObj;
		dl_Insert(&pServerMgr->m_RemovedObjectHead, pLink);

		pObj->m_InternalFlags |= IFLAG_OBJECTGOINGAWAY;
		w_RemoveObjectFromLeaf(pObj);
		pObj->m_InternalFlags &= ~IFLAG_INWORLD;
	}
}

// FUNCTION: LITHTECH 0x00477d80
void sm_RemoveObjectsThatNeedToGetRemoved(CServerMgr *pServerMgr)
{
	LTLink *pCur, *pNext;
	LTObject *pObj;

	pCur = pServerMgr->m_RemovedObjectHead.m_pNext;
	while (pCur != &pServerMgr->m_RemovedObjectHead)
	{
		pObj = (LTObject*)pCur->m_pData;
		sm_RemoveObjectFromWorld(pServerMgr, pObj->sd->m_pObject);

		pNext = pCur->m_pNext;
		dl_Remove(pCur);
		g_DLinkBank.Free(pCur);
		pCur = pNext;
	}
}

// FUNCTION: LITHTECH 0x00477de0
LTObject* sm_FindObject(CServerMgr *pServerMgr, uint16 objectID)
{
	ObjectMapEntry *pRecord;

	if (objectID < pServerMgr->m_ObjectMap.GetSize())
	{
		pRecord = &pServerMgr->m_ObjectMap[objectID];
		if (pRecord->m_nRecordType == RECORDTYPE_LTOBJECT)
			return (LTObject*)pRecord->m_pRecordData;
	}

	return LTNULL;
}

// FUNCTION: LITHTECH 0x00477e10
ObjectMapEntry* sm_FindRecord(CServerMgr *pServerMgr, uint16 objectID)
{
	if (objectID < pServerMgr->m_ObjectMap.GetSize())
		return &pServerMgr->m_ObjectMap[objectID];

	return LTNULL;
}

// FUNCTION: LITHTECH 0x00477e40
LTLink* sm_FindInFreeList(CServerMgr *pServerMgr, uint16 objectID)
{
	LTLink *pCur, *pListHead;

	pListHead = &pServerMgr->m_FreeIDs;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		if ((GetLinkID(pCur) & ~IDFLAG_MASK) == objectID)
			return pCur;
	}

	return LTNULL;
}

// Copies a special effect message (minus its packet ID) into the object.
// FUNCTION: LITHTECH 0x00477e80
void sm_SetObjectSpecialEffectMessage(CServerMgr *pServerMgr, LTObject *pObj, CPacket *pPacket)
{
	if (!pObj->sd->m_pSFXMsg)
		pObj->sd->m_pSFXMsg = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	pObj->sd->m_pSFXMsg->Init(pPacket->m_DataLen, MAX_PACKET_LEN);
	memcpy(pObj->sd->m_pSFXMsg->m_Data.GetArray(), pPacket->m_Data.GetArray() + 1, pPacket->m_DataLen - 1);
	pObj->sd->m_pSFXMsg->m_DataLen = pPacket->m_DataLen - 1;

	sm_UpdateInBspStatus(pServerMgr, pObj);
}

// FUNCTION: LITHTECH 0x00477fb0
void sm_SetObjectStateFlags(CServerMgr *pServerMgr, LTObject *pObj, uint32 flags)
{
	if (flags == (pObj->m_InternalFlags & IFLAG_INACTIVE_MASK))
		return;

	pObj->m_InternalFlags = (pObj->m_InternalFlags & ~IFLAG_INACTIVE_MASK) | flags;

	// Inactive objects live at the end of the list.
	dl_RemoveAt(&g_pServerMgr->m_Objects, &pObj->sd->m_ListNode);
	if (flags)
	{
		dl_AddTail(&g_pServerMgr->m_Objects, &pObj->sd->m_ListNode, pObj);
		pObj->sd->m_pObject->EngineMessageFn(MID_DEACTIVATING, LTNULL, 0.0f);
	}
	else
	{
		dl_AddHead(&g_pServerMgr->m_Objects, &pObj->sd->m_ListNode, pObj);
		pObj->sd->m_pObject->EngineMessageFn(MID_ACTIVATING, LTNULL, 0.0f);
	}
}

// FUNCTION: LITHTECH 0x00478070
void sm_ClearClientReferenceList(CServerMgr *pServerMgr)
{
	LTLink *pCur, *pNext, *pListHead;

	pListHead = &pServerMgr->m_ClientReferences.m_Head;
	pCur = pListHead->m_pNext;
	while (pCur != pListHead)
	{
		pNext = pCur->m_pNext;
		dfree(pCur->m_pData);
		pCur = pNext;
	}

	dl_InitList(&pServerMgr->m_ClientReferences);
}

// FUNCTION: LITHTECH 0x004780c0
ClientRef* sm_FindClientRefFromObject(CServerMgr *pServerMgr, LTObject *pObj)
{
	LTLink *pCur, *pListHead;
	ClientRef *pRef;

	pListHead = &pServerMgr->m_ClientReferences.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pRef = (ClientRef*)pCur->m_pData;
		if (pRef->m_ObjectID == pObj->m_ObjectID)
			return pRef;
	}

	return LTNULL;
}

// FUNCTION: LITHTECH 0x00478100
void sm_RemoveOldClientRefObjects(CServerMgr *pServerMgr)
{
	LTLink *pCur, *pNext, *pListHead;
	LTObject *pObj;

	pListHead = &pServerMgr->m_Objects.m_Head;
	pCur = pListHead->m_pNext;
	while (pCur != pListHead)
	{
		pNext = pCur->m_pNext;
		pObj = (LTObject*)pCur->m_pData;

		if (pObj->m_InternalFlags & IFLAG_FROMCLIENTREF)
			sm_RemoveObjectFromWorld(pServerMgr, pObj->sd->m_pObject);

		pCur = pNext;
	}
}

// A model the loader thread unloaded.
struct ModelUnloadRequest
{
	uint32		m_Unknown0;		// 0x00
	Model		*m_pModel;		// 0x04
};

struct ModelUnloadMsg
{
	uint32				m_Unknown0;		// 0x00
	ModelUnloadRequest	*m_pRequest;	// 0x04
};

// Model unload callback: drops the model from the server's cache.
// FUNCTION: LITHTECH 0x00478150
LTRESULT sm_OnModelUnload(void *pUser, ModelUnloadMsg *pMsg, LTRESULT status)
{
	ModelUnloadRequest *pRequest;
	CServerMgr *pServerMgr;
	HHashElement *hElement;
	char *pFilename;

	if (status == LT_OK)
	{
		pRequest = pMsg->m_pRequest;
		pServerMgr = g_pServerMgr;
		if (pServerMgr)
		{
			pFilename = pRequest->m_pModel->GetFilename();
			delete pRequest;

			hElement = hs_FindElement(pServerMgr->m_hModelTable, pFilename, strlen(pFilename));
			if (hElement)
			{
				DEBUG_PRINT(3, ("Removing model from resource list:  %s\n", pFilename));
				hs_RemoveElement(pServerMgr->m_hModelTable, hElement);
			}
		}
	}

	return LT_OK;
}
