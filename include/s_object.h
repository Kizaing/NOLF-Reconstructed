// Server object helpers (Talon, recovered from lithtech.exe; Jupiter server/src/s_object.h).
// Talon passes the server manager explicitly.
#ifndef __S_OBJECT_H__
#define __S_OBJECT_H__

#include "servermgr.h"

class CSoundTrack;
struct ClientRef;

// Internal object flags (m_InternalFlags).
#define IFLAG_OBJECTGOINGAWAY	(1<<0)
#define IFLAG_INWORLD			(1<<2)

#define RECORDTYPE_LTOBJECT		1

// ORs the object's flags with the flags you specify and adds the object to the
// 'changed object' list. Talon inlines this in most callers (out of line at 0x00477540).
inline LTRESULT SetObjectChangeFlags(CServerMgr *pServerMgr, LTObject *pObj, uint32 flags)
{
	// This object should be in the world..
	if (!(pObj->m_InternalFlags & IFLAG_INWORLD) || (pObj->m_InternalFlags & IFLAG_OBJECTGOINGAWAY))
	{
		RETURN_ERROR_PARAM(1, SetObjectChangeFlags, LT_OBJECTNOTINWORLD,
			pObj->sd->m_pClass->m_ClassName);
	}

	if (pServerMgr->m_ObjectMap[pObj->m_ObjectID].m_nRecordType != RECORDTYPE_LTOBJECT ||
		!pServerMgr->m_ObjectMap[pObj->m_ObjectID].m_pRecordData)
	{
		RETURN_ERROR_PARAM(1, SetObjectChangeFlags, LT_ERROR,
			pObj->sd->m_pClass->m_ClassName);
	}

	// Make sure not to re-add it and screw it up.
	if (pObj->sd->m_ChangeFlags == 0 && pServerMgr->m_bTrackChanges)
	{
		pObj->sd->m_pChangeNext = pServerMgr->m_pChangeListHead;
		pServerMgr->m_pChangeListHead = pObj;
	}

	pObj->sd->m_ChangeFlags |= flags;
	return LT_OK;
}

// Can the object be left out of the world tree?
inline LTBOOL CanOptimizeObject(LTObject *pObj)
{
	return !pObj->sd->m_pSFXMsg &&
		(!(pObj->m_Flags & FLAG_OPTIMIZEMASK) || (pObj->m_Flags & FLAG_FORCEOPTIMIZEOBJECT));
}

LTRESULT	sm_UpdateInBspStatus(CServerMgr *pServerMgr, LTObject *pObject);
uint32		sm_GetNewObjectChangeFlags(CServerMgr *pServerMgr, LTObject *pObject);
void		AddObjectToRemoveList(CServerMgr *pServerMgr, LTObject *pObj);
void		sm_RemoveObjectsThatNeedToGetRemoved(CServerMgr *pServerMgr);
LTObject*	sm_FindObject(CServerMgr *pServerMgr, uint16 objectID);
LTLink*		sm_FindInFreeList(CServerMgr *pServerMgr, uint16 objectID);
void		sm_SetObjectStateFlags(CServerMgr *pServerMgr, LTObject *pObj, uint32 flags);
void		sm_ClearClientReferenceList(CServerMgr *pServerMgr);
ClientRef*	sm_FindClientRefFromObject(CServerMgr *pServerMgr, LTObject *pObj);
void		sm_RemoveOldClientRefObjects(CServerMgr *pServerMgr);

#endif  // __S_OBJECT_H__
