// Jupiter runtime/server/src/interlink.cpp
// Talon passes the server manager explicitly, has a fourth link type (object references, kept
// in an STLport std::list) and DisconnectLinks never notifies the owner.
// FLAGS: /O2 /D__STL_NO_EXCEPTION_HEADER /D__STL_NO_NEW_NEW_HEADER /D__STL_NO_BAD_ALLOC /IE:/AVP2Source/build/proj/LT2/lithshared/stl /IE:/MSVC6/VC98/MFC
#include <list>
#include <algorithm>
#include "bdefs.h"
#include "ltengineobjects.h"
#include "interlink.h"
#include "servermgr.h"
#include "soundtrack.h"

static LTBOOL DoesLinkExist(LTObject *pOwner, void *pOther, uint32 linkType);

// The object references (std::list node allocator free list at 0x004defb8).
typedef std::list<ObjRefEntry> ObjRefList;
static ObjRefList g_ObjRefs;

// FUNCTION: LITHTECH 0x00443c60 _$E6
// FUNCTION: LITHTECH 0x00443c70 _$E3
// FUNCTION: LITHTECH 0x00443d10 _$E5
// FUNCTION: LITHTECH 0x00443d20 _$E4

// Finds a reference record by its object or by its address.
inline bool operator==(const ObjRefEntry &entry, LTObject *pObj)
{
	return entry.m_pObject == pObj;
}

inline bool operator==(const ObjRefEntry &entry, const ObjRefEntry *pRef)
{
	return &entry == pRef;
}

// Gets the reference record for an object, adding one if there isn't one yet.
// FUNCTION: LITHTECH 0x00443da0
ObjRefEntry* AddObjRef(LTObject *pObj)
{
	ObjRefList::iterator it;

	it = std::find(g_ObjRefs.begin(), g_ObjRefs.end(), pObj);
	if (it == g_ObjRefs.end())
	{
		g_ObjRefs.push_back(ObjRefEntry(pObj));
		return &g_ObjRefs.back();
	}

	return &(*it);
}


// Frees an object reference record once nothing refers to it any more.
// FUNCTION: LITHTECH 0x00443e60
void ReleaseObjRef(ObjRefEntry *pRef)
{
	ObjRefList::iterator it;

	it = std::find(g_ObjRefs.begin(), g_ObjRefs.end(), (const ObjRefEntry*)pRef);
	if (it != g_ObjRefs.end() && (*it).m_nRefs <= 0 && (*it).m_pObject == LTNULL)
		g_ObjRefs.erase(it);
}


// FUNCTION: LITHTECH 0x00443f10
void DisconnectLinks(CServerMgr *pServerMgr, LTObject *pOwner, void *pOther, LTBOOL bDisconnectAll)
{
	LTLink *pListHead, *pCur, *pNext;
	InterLink *pLink;

	pListHead = &pOwner->sd->m_Links;
	pCur = pListHead->m_pNext;
	while (pCur != pListHead)
	{
		pNext = pCur->m_pNext;
		pLink = (InterLink*)pCur->m_pData;

		if ((pLink->m_Type == LINKTYPE_INTERLINK || pLink->m_Type == LINKTYPE_SOUND) &&
			pLink->m_pOwner == pOwner && pLink->m_pOther == pOther)
		{
			if (pLink->m_Type == LINKTYPE_SOUND)
			{
				((CSoundTrack*)pLink->m_pOther)->m_pInterLink = LTNULL;
			}
			else
			{
				dl_Remove(pLink->m_pOtherLink);
				g_DLinkBank.Free(pLink->m_pOtherLink);
			}

			dl_Remove(pLink->m_pOwnerLink);
			g_DLinkBank.Free(pLink->m_pOwnerLink);
			sb_Free(&pServerMgr->m_InterLinkBank, pLink);

			// Return if we aren't disconnecting ALL the links...
			if (!bDisconnectAll)
				return;
		}

		pCur = pNext;
	}
}


// FUNCTION: LITHTECH 0x00443ff0
void BreakInterLinks(CServerMgr *pServerMgr, LTObject *pObj, uint32 linkType, LTBOOL bNotify)
{
	LTLink *pListHead, *pCur, *pNext;
	InterLink *pLink;
	CSoundTrack *pSoundTrack;
	ObjRefEntry *pRef;

	pListHead = &pObj->sd->m_Links;
	pCur = pListHead->m_pNext;
	while (pCur != pListHead)
	{
		pNext = pCur->m_pNext;

		pLink = (InterLink*)pCur->m_pData;
		if (pLink->m_Type == linkType)
		{
			// Kill the sound...
			if (pLink->m_Type == LINKTYPE_SOUND)
			{
				pSoundTrack = (CSoundTrack*)pLink->m_pOther;
				if (!(pSoundTrack->m_dwFlags & PLAYSOUND_GETHANDLE))
				{
					pSoundTrack->m_fTimeLeft = 0.0f;
					pSoundTrack->SetRemove(LTTRUE);
				}
				pSoundTrack->m_pInterLink = LTNULL;
			}
			else if (pLink->m_Type == LINKTYPE_OBJREF)
			{
				pRef = (ObjRefEntry*)pLink->m_pOther;
				if (pRef)
				{
					pRef->m_pObject = LTNULL;
					ReleaseObjRef(pRef);
				}
			}
			else
			{
				// Notify the owner that the link is being broken.
				if (bNotify)
				{
					if (pLink->m_pOwner->sd->m_pObject)
					{
						pLink->m_pOwner->sd->m_pObject->EngineMessageFn(MID_LINKBROKEN,
							pLink->m_pOther, 0.0f);
					}
				}

				// Detach the links between the two.
				dl_Remove(pLink->m_pOtherLink);

				// Free stuff.
				g_DLinkBank.Free(pLink->m_pOtherLink);
			}

			// Detach the links between the two.
			dl_Remove(pLink->m_pOwnerLink);

			// Free stuff.
			g_DLinkBank.Free(pLink->m_pOwnerLink);
			sb_Free(&pServerMgr->m_InterLinkBank, pLink);
		}

		pCur = pNext;
	}
}


// FUNCTION: LITHTECH 0x00444100
LTRESULT CreateInterLink(CServerMgr *pServerMgr, LTObject *pOwner, void *pOther, uint32 linkType)
{
	InterLink *pLink;
	LTLink *pOwnerLink, *pOtherLink;

	// Don't link them if they already are.
	if (linkType == LINKTYPE_OBJREF || linkType == LINKTYPE_CONTAINER)
	{
		if (DoesLinkExist(pOwner, pOther, linkType))
			return LT_OK;
	}

	if (pOwner == pOther)
		return LT_ERROR;

	pLink = (InterLink*)sb_Allocate(&pServerMgr->m_InterLinkBank);
	pLink->m_Type = linkType;
	pLink->m_pOwner = pOwner;
	pLink->m_pOther = pOther;

	pOwnerLink = g_DLinkBank.Allocate();
	pOwnerLink->m_pData = pLink;
	pLink->m_pOwnerLink = pOwnerLink;
	dl_Insert(&pOwner->sd->m_Links, pOwnerLink);

	if (linkType == LINKTYPE_SOUND)
	{
		((CSoundTrack*)pOther)->m_pInterLink = pLink;
	}
	else if (linkType == LINKTYPE_INTERLINK || linkType == LINKTYPE_CONTAINER)
	{
		pOtherLink = g_DLinkBank.Allocate();
		pOtherLink->m_pData = pLink;
		pLink->m_pOtherLink = pOtherLink;
		dl_Insert(&((LTObject*)pOther)->sd->m_Links, pOtherLink);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00444230
static LTBOOL DoesLinkExist(LTObject *pOwner, void *pOther, uint32 linkType)
{
	LTLink *pListHead, *pCur;
	InterLink *pLink;

	pListHead = &pOwner->sd->m_Links;
	pCur = pListHead->m_pNext;
	while (pCur != pListHead)
	{
		pLink = (InterLink*)pCur->m_pData;

		if (pLink->m_Type == linkType && pLink->m_pOwner == pOwner && pLink->m_pOther == pOther)
			return LTTRUE;

		pCur = pCur->m_pNext;
	}

	return LTFALSE;
}
