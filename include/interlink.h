// Inter-object links (Talon layout recovered from lithtech.exe; Jupiter server/src/interlink.h).
#ifndef __INTERLINK_H__
#define __INTERLINK_H__

#include "ltbasedefs.h"

class CServerMgr;
class LTObject;

// Link types.
#define LINKTYPE_INTERLINK	0	// Two objects linked (MID_LINKBROKEN to the owner).
#define LINKTYPE_CONTAINER	1	// Owner is a container holding m_pOther.
#define LINKTYPE_SOUND		2	// m_pOther is a CSoundTrack.
#define LINKTYPE_OBJREF		3	// m_pOther is an ObjRefEntry (Talon only).

// 0x14 bytes (m_InterLinkBank).
struct InterLink
{
	uint32		m_Type;			// 0x00 LINKTYPE_
	LTObject	*m_pOwner;		// 0x04
	void		*m_pOther;		// 0x08
	LTLink		*m_pOwnerLink;	// 0x0c in m_pOwner->sd->m_Links
	LTLink		*m_pOtherLink;	// 0x10 in m_pOther->sd->m_Links (interlink and container)
};

// Talon object reference record (LINKTYPE_OBJREF). The SDK's LTSmartLink_Body (ltsmartlink.h) is one of
// these; they are kept in an STLport std::list (interlink.cpp).
struct ObjRefEntry
{
	ObjRefEntry(LTObject *pObject) : m_nRefs(0), m_pObject(pObject), m_pServer(LTNULL) {}

	int32		m_nRefs;		// 0x00 m_nCount
	LTObject	*m_pObject;		// 0x04
	void		*m_pServer;		// 0x08 ILTServer*
};

void		DisconnectLinks(CServerMgr *pServerMgr, LTObject *pOwner, void *pOther, LTBOOL bDisconnectAll);
void		BreakInterLinks(CServerMgr *pServerMgr, LTObject *pObj, uint32 linkType, LTBOOL bNotify);
LTRESULT	CreateInterLink(CServerMgr *pServerMgr, LTObject *pOwner, void *pOther, uint32 linkType);

// Reference counted object references (ILTPhysics::CreateSmartLink).
ObjRefEntry*	AddObjRef(LTObject *pObj);				// 0x00443da0
void		ReleaseObjRef(ObjRefEntry *pRef);		// 0x00443e60

#endif  // __INTERLINK_H__
