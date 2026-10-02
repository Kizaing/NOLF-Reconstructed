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

#define IFLAG_HASCLIENTREF		(1<<7)	// A ClientRef keeps the object alive for a client that left.
#define IFLAG_HASCHILDMODELS	(1<<10)

#define RECORDTYPE_LTOBJECT		1

// Object change flags (ServerData::m_ChangeFlags, ObjInfo::m_ChangeFlags; Jupiter packetdefs.h).
#define CF_NEWOBJECT	(1<<0)	// Client(s) need to be told about this one.
#define CF_POSITION		(1<<1)	// Position changed.
#define CF_ROTATION		(1<<2)	// Rotation changed.
#define CF_FLAGS		(1<<3)	// Flags (and user flags) changed.
#define CF_SCALE		(1<<4)	// Scale changed.
#define CF_MODELINFO	(1<<5)	// For sprites, the change flags are followed by a sprite info.
#define CF_SOUNDINFO	(1<<5)	// For sounds, sound has had killsoundloop called on it.
#define CF_RENDERINFO	(1<<6)	// Render info changed (RGBA and light radius).
#define CF_OTHER		(1<<7)	// There's an extra change flag byte.
#define CF_ATTACHMENTS	(1<<8)	// Attachment info.
#define CF_TELEPORT		(1<<9)	// The object was teleported.
#define CF_SNAPROTATION	(1<<10)	// The object's rotation snapped.
#define CF_SENTINFO		(1<<12)	// Only used in the server's ObjInfos for each client.
#define CF_FORCEMODELINFO (1<<13)	// Force a model info change, even if NETFLAG_ANIMUNGUARANTEED is set.
#define CF_POSITION_PREDICTIONCAP (1<<14)	// An extra position message should be sent to cap off the prediction.
#define CF_OTHERFLAGMASK	(CF_ATTACHMENTS | CF_TELEPORT | CF_SNAPROTATION | CF_FORCEMODELINFO)
#define CF_CLEARMASK	(CF_POSITION_PREDICTIONCAP)	// Clears the client flags but keeps the frame-coherent ones.

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
ObjectMapEntry*	sm_FindRecord(CServerMgr *pServerMgr, uint16 objectID);	// 0x00477e10
LTRESULT	sm_SetupError(CServerMgr *pServerMgr, LTRESULT err, ...);	// 0x004864d0
LTLink*		sm_FindInFreeList(CServerMgr *pServerMgr, uint16 objectID);
void		sm_SetObjectStateFlags(CServerMgr *pServerMgr, LTObject *pObj, uint32 flags);
void		sm_ClearClientReferenceList(CServerMgr *pServerMgr);
ClientRef*	sm_FindClientRefFromObject(CServerMgr *pServerMgr, LTObject *pObj);
void		sm_RemoveOldClientRefObjects(CServerMgr *pServerMgr);

#endif  // __S_OBJECT_H__
