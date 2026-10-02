// Jupiter runtime/server/src/s_client.cpp: all the server-side client handling functions.
// Talon passes the server manager explicitly, uses the ref-counted CPacket, and also sends the
// world's light animations to the clients.
#include <string.h>
#include "bdefs.h"
#include "servermgr.h"
#include "s_client.h"
#include "s_object.h"
#include "serverevent.h"
#include "soundtrack.h"
#include "ftserv.h"
#include "dhashtable.h"
#include "impl_common.h"
#include "packet.h"
#include "de_memory.h"
#include "streamsim.h"

// Talon server-to-client packet IDs.
#define SMSG_UPDATE				8
#define SMSG_NETPROTOCOLVERSION	4
#define SMSG_UNLOADWORLD		5
#define SMSG_LOADWORLD			6
#define SMSG_CLIENTOBJECTID		7
#define SMSG_YOURID				12
#define SMSG_SKYDEF				17
#define SMSG_PACKETGROUP		14
#define SMSG_PRELOADLIST		21
#define SMSG_LIGHTANIMS			25

// Preload list types (SMSG_PRELOADLIST).
#define PRELOADTYPE_START		0
#define PRELOADTYPE_END			1
#define PRELOADTYPE_MODEL		2
#define PRELOADTYPE_TEXTURE		3
#define PRELOADTYPE_SPRITE		4
#define PRELOADTYPE_SOUND		5

// File types (de_codes.h).
#define FT_MODEL				0
#define FT_SPRITE				1
#define FT_TEXTURE				2
#define FT_SOUND				3

// Light animation change flags (LightAnimChange::m_ChangeFlags).
#define LIGHTANIMF_FRAMES		(1<<0)
#define LIGHTANIMF_PERCENT		(1<<1)
#define LIGHTANIMF_BLEND		(1<<2)
#define LIGHTANIMF_POS			(1<<3)
#define LIGHTANIMF_COLOR		(1<<4)
#define LIGHTANIMF_RADIUS		(1<<5)
#define LIGHTANIMF_SHADOWMAP	(LIGHTANIMF_POS | LIGHTANIMF_COLOR | LIGHTANIMF_RADIUS)
#define LIGHTANIMF_ALL			0xFF

#define SERV_RUNNINGWORLD		0

#define SIFLAG_LOCAL			(1<<1)	// A client is connected locally.

#define OBJINFOSOUNDF_CLIENTDONE	(1<<0)

#define IFLAG_HASCLIENTREF		(1<<7)
#define IFLAG_INACTIVE			(1<<3)
#define IFLAG_INACTIVE_TOUCH	(1<<4)
#define IFLAG_AUTODEACTIVATED	(1<<5)
#define IFLAG_INACTIVE_TICK		(1<<9)

void		sm_ResetDeactivateTimer(LTObject *pObj);	// servermgr, 0x004867f0

#define CFLAG_SENDSKYDEF		(1<<5)

#define IFLAG_INSKY				(1<<8)

void		clienthack_UnloadWorld();								// clientshell, 0x00416720

// What sm_UpdateClientInWorld passes around (Jupiter's UpdateInfo).
struct UpdateInfo
{
	CServerMgr	*m_pServerMgr;		// 0x00
	Client		*m_pClient;			// 0x04
	CPacketRef	m_cPacket;			// 0x08 guaranteed update packet
	CPacketRef	m_cUnguaranteed;	// 0x0c
	CPacketRef	m_cGroups[2];		// 0x10 the same two packets, by MESSAGE_GUARANTEED (unguaranteed, guaranteed)
	uint32		m_nPacketsSent;		// 0x18
	LTBOOL		m_bAutoActivate;	// 0x1c CFLAG_AUTOACTIVATEOBJECTS
};

#define ID_TIMESTAMP	0xFFFF

// ObjInfo::m_ChangeFlags.
#define CF_SOUNDINFO	(1<<5)
#define CF_SENTINFO		(1<<12)
#define CF_CLEARMASK	(1<<14)

// The list of objects sent this update (one of the client's m_SentLists).
// GLOBAL: LITHTECH 0x004e49ec
SentList *g_pCurSentList;

LTBOOL		FillPacketFromInfo(CServerMgr *pServerMgr, Client *pClient, LTObject *pObj, ObjInfo *pInfo,
				CPacket *pPacket, uint32 *pSize);	// s_net, 0x004745c0
void		FillSoundTrackPacketFromInfo(CServerMgr *pServerMgr, CSoundTrack *pSoundTrack, ObjInfo *pInfo,
				Client *pClient, CPacket *pPacket);	// s_net, 0x004759c0
void		WriteUnguaranteedInfo(UpdateInfo *pInfo, LTObject *pObject, ObjInfo *pObjInfo);	// 0x00471ca0

void		sm_TracePacket(CServerMgr *pServerMgr, CPacket *pPacket);	// 0x00471680

// Console variables (STracePackets, DelimitPackets).
// GLOBAL: LITHTECH 0x004e36dc
extern int32 g_CV_STracePackets;
// GLOBAL: LITHTECH 0x004d213c
extern int32 g_CV_DelimitPackets;
LTBOOL		sm_SetClientState(CServerMgr *pServerMgr, Client *pClient, int state);
LTRESULT	sm_UpdatePuttingInWorld(CServerMgr *pServerMgr, Client *pClient);
LTRESULT	sm_ConnectClientToWorld(CServerMgr *pServerMgr, Client *pClient);
void		sm_GetClientOutOfWorld(CServerMgr *pServerMgr, Client *pClient);
void		sm_UpdateClientFileTransfer(CServerMgr *pServerMgr, Client *pClient);
void		sm_SendChangedLightAnims(CServerMgr *pServerMgr, Client *pClient);
static LTRESULT	sm_SendCacheListSection(CServerMgr *pServerMgr, Client *pClient, uint32 nStartIndex,
	CPacketRef &cPacket, uint8 nPacketID, uint16 nFileType);


// Sends the cached file list to the client
// FUNCTION: LITHTECH 0x0046f6a0
LTRESULT sm_SendCacheListToClient(CServerMgr *pServerMgr, Client *pClient, uint32 nStartIndex)
{
	CPacketRef cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	// Add models
	sm_SendCacheListSection(pServerMgr, pClient, nStartIndex, cPacket, PRELOADTYPE_MODEL, FT_MODEL);

	// Add sprites.
	sm_SendCacheListSection(pServerMgr, pClient, nStartIndex, cPacket, PRELOADTYPE_SPRITE, FT_SPRITE);

	// Add textures.
	sm_SendCacheListSection(pServerMgr, pClient, nStartIndex, cPacket, PRELOADTYPE_TEXTURE, FT_TEXTURE);

	// Add sounds.
	sm_SendCacheListSection(pServerMgr, pClient, nStartIndex, cPacket, PRELOADTYPE_SOUND, FT_SOUND);

	return LT_OK;
}


// STUB: LITHTECH 0x0046f730
// Inline budget: the original calls the first two WriteTypes out of line.
static LTRESULT sm_SendCacheListSection(CServerMgr *pServerMgr, Client *pClient, uint32 nStartIndex,
	CPacketRef &cPacket, uint8 nPacketID, uint16 nFileType)
{
	LTBOOL bPacketEmpty;
	uint32 i;

	bPacketEmpty = TRUE;
	for (i=nStartIndex; i < pServerMgr->m_CacheListSize; i++)
	{
		if (pServerMgr->m_CacheList[i].m_FileType == nFileType)
		{
			if (bPacketEmpty)
			{
				cPacket->WriteType(nPacketID);
				bPacketEmpty = FALSE;
			}

			cPacket->WriteType((uint16)pServerMgr->m_CacheList[i].m_FileID);

			// Send it when it gets full.
			if (cPacket->GetSpaceLeft() < 50)
			{
				SendToClient(pServerMgr, pClient, SMSG_PRELOADLIST, cPacket, FALSE, MESSAGE_GUARANTEED);
				cPacket->ResetWrite();
				cPacket->WriteType(nPacketID);
			}
		}
	}

	// Send what's left.
	if (!bPacketEmpty)
	{
		SendToClient(pServerMgr, pClient, SMSG_PRELOADLIST, cPacket, FALSE, MESSAGE_GUARANTEED);
		cPacket->ResetWrite();
	}

	return LT_OK;
}


// Writes a light animation's changed state.
// STUB: LITHTECH 0x0046f8e0
static void sm_WriteLightAnimInfo(CServerMgr *pServerMgr, LightAnim *pAnim, uint16 iLightAnim,
	CPacket *pPacket, uint32 flags)
{
	// Only shadow map lights have a position, color and radius.
	if (!pAnim->m_bShadowMap)
		flags &= ~LIGHTANIMF_SHADOWMAP;

	pPacket->WriteType(iLightAnim);
	pPacket->WriteType((uint8)flags);

	if (flags & LIGHTANIMF_FRAMES)
	{
		pPacket->WriteType((uint16)pAnim->m_iFrames[0]);
		pPacket->WriteType((uint16)pAnim->m_iFrames[1]);
	}

	if (flags & LIGHTANIMF_PERCENT)
		pPacket->WriteType((uint8)pAnim->m_PercentBetween);

	if (flags & LIGHTANIMF_BLEND)
		pPacket->WriteType((uint8)(pAnim->m_fBlendPercent * 255.0f));

	if (flags & LIGHTANIMF_POS)
		ic_WriteCompPos(&pPacket->m_Message, &pAnim->m_vLightPos, &pServerMgr->m_World);

	if (flags & LIGHTANIMF_COLOR)
	{
		pPacket->WriteType((uint8)pAnim->m_vLightColor.x);
		pPacket->WriteType((uint8)pAnim->m_vLightColor.y);
		pPacket->WriteType((uint8)pAnim->m_vLightColor.z);
	}

	if (flags & LIGHTANIMF_RADIUS)
		pPacket->WriteType(pAnim->m_fLightRadius);
}


// Sends all the world's light animations.
// FUNCTION: LITHTECH 0x0046fc40
void sm_SendAllLightAnims(CServerMgr *pServerMgr, Client *pClient)
{
	CPacket *pPacket;
	uint32 i;

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	for (i=0; i < pServerMgr->m_World.m_LightAnims.GetSize(); i++)
	{
		sm_WriteLightAnimInfo(pServerMgr, &pServerMgr->m_World.m_LightAnims[i], (uint16)i, pPacket, LIGHTANIMF_ALL);

		if (pPacket->GetSpaceLeft() <= 22)
		{
			SendToClient(pServerMgr, pClient, SMSG_LIGHTANIMS, pPacket, FALSE, MESSAGE_GUARANTEED);
			pPacket->ResetWrite();
		}
	}

	if (pPacket->m_DataLen - 1 > 0)
	{
		SendToClient(pServerMgr, pClient, SMSG_LIGHTANIMS, pPacket, FALSE, MESSAGE_GUARANTEED);
		pPacket->ResetWrite();
	}

	pPacket->Release();
}


// Sends the light animations that changed since the last update.
// STUB: LITHTECH 0x0046fd30
void sm_SendChangedLightAnims(CServerMgr *pServerMgr, Client *pClient)
{
	CPacket *pPacket;
	uint32 i, iLightAnim;
	LightAnimChange *pChange;

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	for (i=0; i < pClient->m_nLightAnimChanges; i++)
	{
		pChange = &pClient->m_LightAnimChanges[i];

		iLightAnim = pChange->m_iLightAnim;
		if (iLightAnim < pServerMgr->m_World.m_LightAnims.GetSize())
		{
			sm_WriteLightAnimInfo(pServerMgr, &pServerMgr->m_World.m_LightAnims[iLightAnim], (uint16)iLightAnim,
				pPacket, pChange->m_ChangeFlags);

			if (pPacket->GetSpaceLeft() <= 22)
			{
				SendToClient(pServerMgr, pClient, SMSG_LIGHTANIMS, pPacket, FALSE, MESSAGE_GUARANTEED);
				pPacket->ResetWrite();
			}
		}
	}

	if (pPacket->m_DataLen - 1 > 0)
	{
		SendToClient(pServerMgr, pClient, SMSG_LIGHTANIMS, pPacket, FALSE, MESSAGE_GUARANTEED);
		pPacket->ResetWrite();
	}

	pClient->m_nLightAnimChanges = 0;
	pPacket->Release();
}




int sm_FTCantOpenFileFn(FTServ *hServ, char *pFilename);	// 0x004a02b0 (shared return 1)


// ----------------------------------------------------------------------- //
// Interface functions.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00470bb0
ILTStream* sm_FTOpenFn(FTServ *hServ, char *pFilename)
{
	CServerMgr *pServerMgr = (CServerMgr*)fts_GetUserData1(hServ);
	return sf_OpenFile(&pServerMgr->m_FileMgr, pFilename);
}


// FUNCTION: LITHTECH 0x00470bd0
void sm_FTCloseFn(FTServ *hServ, ILTStream *pStream)
{
	pStream->Release();
}


// FUNCTION: LITHTECH 0x00470be0
void sm_OnBrokenConnection(CServerMgr *pServerMgr, CBaseConn *id)
{
	Client *pClient = sm_FindClient(pServerMgr, id);

	if (pClient)
	{
		sm_RemoveClient(pServerMgr, pClient);
	}
}


// FUNCTION: LITHTECH 0x00470c10
LTRESULT sm_AttachClient(CServerMgr *pServerMgr, Client *pParent, Client *pChild)
{
	CPacket *pPacket;

	if (pParent == pChild)
	{
		RETURN_ERROR(1, sm_AttachClient, LT_INVALIDPARAMS);
	}

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	sm_DetachClient(pServerMgr, pChild);

	pChild->m_pAttachmentParent = pParent;
	dl_Insert(&pParent->m_Attachments, &pChild->m_AttachmentLink);

	// Tell the client to use the new object ID.
	if (pParent->m_pObject)
	{
		pPacket->ResetWrite();
		pPacket->WriteType(pParent->m_pObject->m_ObjectID);
		SendToClient(pServerMgr, pChild, SMSG_CLIENTOBJECTID, pPacket, TRUE, MESSAGE_GUARANTEED);
	}

	if (pPacket)
		pPacket->Release();

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00470db0
LTRESULT sm_DetachClient(CServerMgr *pServerMgr, Client *pClient)
{
	// Talon never gets a packet here, so this writes through a null packet.
	CPacketRef cPacket;

	if (pClient->m_pAttachmentParent)
	{
		dl_Remove(&pClient->m_AttachmentLink);
		pClient->m_pAttachmentParent = LTNULL;

		// Tell the client to use its normal object.
		if (pClient->m_pObject)
		{
			cPacket->ResetWrite();
			cPacket->WriteType(pClient->m_pObject->m_ObjectID);
			SendToClient(pServerMgr, pClient, SMSG_CLIENTOBJECTID, cPacket, TRUE, MESSAGE_GUARANTEED);
		}
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00470f10
LTRESULT sm_DetachClientChildren(CServerMgr *pServerMgr, Client *pClient)
{
	LTLink *pCur, *pNext;

	pCur = pClient->m_Attachments.m_pNext;
	while (pCur != &pClient->m_Attachments)
	{
		pNext = pCur->m_pNext;
		sm_DetachClient(pServerMgr, (Client*)pCur->m_pData);
		pCur = pNext;
	}

	return LT_OK;
}


static void sm_FreeClient(CServerMgr *pServerMgr, Client *pClient);

// FUNCTION: LITHTECH 0x00470f50
void sm_RemoveClient(CServerMgr *pServerMgr, Client *pClient)
{
	LTLink *pCur;
	CSoundTrack *pSoundTrack;
	ObjInfo *pInfo;

	dsi_ConsolePrint("Removing client, id %d, leaving %d.",
		pClient->m_ClientID, pServerMgr->m_Clients.m_nElements-1);

	// Undo all attachments.
	sm_DetachClient(pServerMgr, pClient);
	sm_DetachClientChildren(pServerMgr, pClient);

	// Remove the client reference in any sound tracks...
	for (pCur=pServerMgr->m_SoundTrackList.m_Head.m_pNext; pCur != &pServerMgr->m_SoundTrackList.m_Head;
		pCur=pCur->m_pNext)
	{
		pSoundTrack = (CSoundTrack*)pCur->m_pData;

		pInfo = &pClient->m_ObjInfos[GetLinkID(pSoundTrack->m_pIDLink)];
		if (!(pInfo->m_nSoundFlags & OBJINFOSOUNDF_CLIENTDONE))
		{
			pSoundTrack->Release(&pInfo->m_nSoundFlags);
		}
	}

	// If they had a local connection, clear the server's local flag.
	if (pClient->m_ClientFlags & CFLAG_LOCAL)
	{
		pServerMgr->m_InternalFlags &= ~SIFLAG_LOCAL;
	}

	// Get them out of the world..
	sm_SetClientState(pServerMgr, pClient, CLIENT_CONNECTED);

	// Notify the shell.
	pServerMgr->m_ClassMgr.m_pServerShell->OnRemoveClient((HCLIENT)pClient);

	// Remove the client.
	dl_RemoveAt(&pServerMgr->m_Clients, &pClient->m_Link);
	sm_FreeClient(pServerMgr, pClient);
}


// Frees up everything (Jupiter's Client::~Client).
// FUNCTION: LITHTECH 0x00471050
static void sm_FreeClient(CServerMgr *pServerMgr, Client *pClient)
{
	LTLink *pCur, *pNext;
	CServerEvent *pEvent;
	HHashIterator *hIterator;
	HHashElement *hElement;
	void *pInfo;

	fts_Term(pClient->m_hFTServ);

	pCur = pClient->m_Events.m_Head.m_pNext;
	while (pCur != &pClient->m_Events.m_Head)
	{
		pNext = pCur->m_pNext;
		pEvent = (CServerEvent*)pCur->m_pData;
		pEvent->DecrementRefCount();
		pCur = pNext;
	}

	dfree(pClient->m_ObjInfos);
	dfree(pClient->m_SentLists[0].m_ObjectIDs);
	dfree(pClient->m_SentLists[1].m_ObjectIDs);

	dfree(pClient->m_Name);

	// Free the fileid info structures...
	hIterator = hs_GetFirstElement(pClient->m_hFileIDTable);
	while (hIterator)
	{
		hElement = hs_GetNextElement(hIterator);
		if (hElement)
		{
			pInfo = hs_GetElementUserData(hElement);
			sb_Free(&g_pServerMgr->m_BankCAC, pInfo);
		}
	}
	hs_DestroyHashTable(pClient->m_hFileIDTable);

	delete pClient->m_pClientData;

	delete pClient;
}


// FUNCTION: LITHTECH 0x00471160
LTBOOL sm_SetClientState(CServerMgr *pServerMgr, Client *pClient, int state)
{
	if (pClient->m_State == state)
		return TRUE;

	if (state == CLIENT_INWORLD)
	{
		if (sm_CanClientEnterWorld(pServerMgr, pClient))
		{
			sm_ConnectClientToWorld(pServerMgr, pClient);
			return TRUE;
		}
		else
		{
			return FALSE;
		}
	}
	else
	{
		// If they're in the world, remove them from the world.
		if (pClient->m_State == CLIENT_INWORLD)
		{
			sm_GetClientOutOfWorld(pServerMgr, pClient);
		}
	}

	pClient->m_State = state;
	return TRUE;
}


// FUNCTION: LITHTECH 0x004711d0
LTBOOL sm_CanClientEnterWorld(CServerMgr *pServerMgr, Client *pClient)
{
	if (pServerMgr->m_State != SERV_RUNNINGWORLD)
		return FALSE;

	if (!(pClient->m_ClientFlags & CFLAG_GOT_HELLO))
		return FALSE;

	if (pClient->m_ClientFlags & CFLAG_LOCAL)
	{
		return TRUE;
	}
	else
	{
		if (pClient->m_ClientFlags & CFLAG_WANTALLFILES)
		{
			return (fts_GetNumNeededFiles(pClient->m_hFTServ) == 0) &&
				(fts_GetNumTotalFiles(pClient->m_hFTServ) == 0);
		}
		else
		{
			return fts_GetNumNeededFiles(pClient->m_hFTServ) == 0;
		}
	}
}


// FUNCTION: LITHTECH 0x00471560
void sm_GetClientOutOfWorld(CServerMgr *pServerMgr, Client *pClient)
{
	CPacket *pPacket;

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	pServerMgr->m_ClassMgr.m_pServerShell->OnClientExitWorld((HCLIENT)pClient);

	if (pClient->m_pObject)
	{
		pClient->m_pObject->sd->m_pClient = LTNULL;
	}

	pClient->m_pObject = LTNULL;

	// Send them an UNLOADWORLD packet.
	pPacket->ResetWrite();
	SendToClient(pServerMgr, pClient, SMSG_UNLOADWORLD, pPacket, FALSE, MESSAGE_GUARANTEED);

	// If they're local, call the function to get them out of the world
	// so the local client removes its objects.
	if (pClient->m_ClientFlags & CFLAG_LOCAL)
	{
		clienthack_UnloadWorld();
	}

	pPacket->Release();
}


// FUNCTION: LITHTECH 0x00471610
void sm_UpdateClientState(CServerMgr *pServerMgr, Client *pClient)
{
	// Try to put them in the world.
	if (pClient->m_State == CLIENT_WAITINGTOENTERWORLD)
	{
		sm_SetClientState(pServerMgr, pClient, CLIENT_INWORLD);
	}
	else if (pClient->m_State == CLIENT_PUTTINGINWORLD)
	{
		sm_UpdatePuttingInWorld(pServerMgr, pClient);
	}

	// Update any files they're transferring.
	sm_UpdateClientFileTransfer(pServerMgr, pClient);
}


// FUNCTION: LITHTECH 0x00471660
void sm_UpdateClientFileTransfer(CServerMgr *pServerMgr, Client *pClient)
{
	fts_Update(pClient->m_hFTServ, pServerMgr->m_TrueFrameTime);
}


// Sends the packet and starts a new one if it doesn't have room for nRoomNeeded more bytes
// (always if nRoomNeeded is -1). Returns TRUE if it sent the packet.
// Prints all the packet data into the packet.trc file.
// FUNCTION: LITHTECH 0x00471680
void sm_TracePacket(CServerMgr *pServerMgr, CPacket *pPacket)
{
	uint32 i;

	if (!g_CV_STracePackets)
		return;

	if (!pServerMgr->m_pTracePacketFile)
	{
		pServerMgr->m_pTracePacketFile = streamsim_Open("packet.trc", "wb");
		if (!pServerMgr->m_pTracePacketFile)
			return;
	}

	for (i=0; i < pPacket->m_DataLen; i++)
	{
		pServerMgr->m_pTracePacketFile->Write(&pPacket->m_Data.GetArray()[i], 1);
	}

	if (g_CV_DelimitPackets)
	{
		*pServerMgr->m_pTracePacketFile << '*' << 'E' << 'N' << 'D' << '*';
	}
}


// STUB: LITHTECH 0x00471760
// One byte: the inlined CMoArray::Insert2 shift loop adds m_pArray + i with the operands swapped.
LTBOOL sm_FlushUpdate(UpdateInfo *pInfo, CPacket *pPacket, uint8 packetID, int nRoomNeeded)
{
	uint32 packetFlags;

	if (nRoomNeeded != -1 && pPacket->GetSpaceLeft() > nRoomNeeded + 4)
		return LTFALSE;

	packetFlags = 0;
	if (packetID == SMSG_UPDATE)
		packetFlags = MESSAGE_GUARANTEED;

	sm_TracePacket(pInfo->m_pServerMgr, pPacket);
	SendToClient(pInfo->m_pServerMgr, pInfo->m_pClient, packetID, pPacket, FALSE, packetFlags);

	pPacket->ResetWrite();
	pPacket->WriteType((uint8)0);
	pInfo->m_nPacketsSent++;
	return LTTRUE;
}


inline LTBOOL ShouldSendToClient(CServerMgr *pServerMgr, LTObject *pObject)
{
	if (!(pObject->m_Flags & FLAG_FORCECLIENTUPDATE))
	{
		// See if this object can't be interacted with.
		if (!(pObject->m_Flags & (FLAG_VISIBLE | FLAG_SOLID | FLAG_RAYHIT)))
			return LTFALSE;

		// If it is a normal object type, only inform the client about it if it has a special
		// effect message.
		if (pObject->m_ObjectType == OT_NORMAL && !pObject->sd->m_pSFXMsg)
			return LTFALSE;
	}

	// This happens sometimes when an object removes another object in its
	// destructor.. not a big deal.
	if (!(pObject->m_InternalFlags & IFLAG_INWORLD))
		return LTFALSE;

	// Don't tell them about models that failed to load.
	if (pObject->m_ObjectType == OT_MODEL &&
		((ModelInstance*)pObject)->GetModelDB() == pServerMgr->m_pDefaultModel)
		return LTFALSE;

	return LTTRUE;
}


// Adds the object to g_pCurSentList.
inline void AddObjectIdToSentList(LTObject *pObject)
{
	uint16 *pNewIDs;

	if (g_pCurSentList->m_nObjectIDs >= g_pCurSentList->m_AllocatedSize)
	{
		pNewIDs = (uint16*)dalloc(sizeof(uint16) * (g_pCurSentList->m_AllocatedSize + 200));
		memcpy(pNewIDs, g_pCurSentList->m_ObjectIDs, sizeof(uint16) * g_pCurSentList->m_nObjectIDs);
		dfree(g_pCurSentList->m_ObjectIDs);
		g_pCurSentList->m_ObjectIDs = pNewIDs;
		g_pCurSentList->m_AllocatedSize += 200;
	}

	g_pCurSentList->m_ObjectIDs[g_pCurSentList->m_nObjectIDs] = pObject->m_ObjectID;
	g_pCurSentList->m_nObjectIDs++;
}


// Marks the object with CF_SENTINFO and sends any change info it has.
inline void sm_AddObjectChangeInfo(UpdateInfo *pInfo, LTObject *pObject, ObjInfo *pObjInfo)
{
	uint32 size;

	// Check if we already sent this.
	if (pObjInfo->m_ChangeFlags & CF_SENTINFO)
		return;

	AddObjectIdToSentList(pObject);

	size = 0;
	FillPacketFromInfo(pInfo->m_pServerMgr, pInfo->m_pClient, pObject, pObjInfo, LTNULL, &size);
	sm_FlushUpdate(pInfo, pInfo->m_cPacket, SMSG_UPDATE, size);
	FillPacketFromInfo(pInfo->m_pServerMgr, pInfo->m_pClient, pObject, pObjInfo, pInfo->m_cPacket, LTNULL);
	WriteUnguaranteedInfo(pInfo, pObject, pObjInfo);

	// Clear 'em.
	pObjInfo->m_ChangeFlags = (uint16)((pObjInfo->m_ChangeFlags & CF_CLEARMASK) | CF_SENTINFO);
}


// FUNCTION: LITHTECH 0x00471920
void UpdateSendToClientState(LTObject *pObject, UpdateInfo *pInfo)
{
	Attachment *pAttachment;
	LTObject *pAttachedObj;

	if (pObject->m_ObjType != WTObj_DObject)
		return;

	// Don't send over the main world model.
	if (pObject->IsMainWorldModel())
		return;

	if (pObject->m_ObjectType == OT_WORLDMODEL &&
		((WorldModelInstance*)pObject)->m_pOriginalBsp->IsUntransformed() == 1)
		return;

	if (!ShouldSendToClient(pInfo->m_pServerMgr, pObject))
		return;

	sm_AddObjectChangeInfo(pInfo, pObject, &pInfo->m_pClient->m_ObjInfos[pObject->m_ObjectID]);

	for (pAttachment=pObject->m_Attachments; pAttachment; pAttachment=pAttachment->m_pNext)
	{
		pAttachedObj = sm_FindObject(pInfo->m_pServerMgr, pAttachment->m_nChildID);
		if (!pAttachedObj)
			continue;

		if (!ShouldSendToClient(pInfo->m_pServerMgr, pAttachedObj))
			continue;

		sm_AddObjectChangeInfo(pInfo, pAttachedObj, &pInfo->m_pClient->m_ObjInfos[pAttachedObj->m_ObjectID]);
	}
}


// Mark the end-update info (or just count its bytes if pPacket is null).
// FUNCTION: LITHTECH 0x00471e60
void WriteEndUpdateInfo(CServerMgr *pServerMgr, Client *pClient, CPacket *pPacket, uint32 *pSize)
{
	if (pPacket)
	{
		pPacket->WriteType((uint16)ID_TIMESTAMP);
		pPacket->WriteType(pServerMgr->m_GameTime);
	}

	if (pSize)
		*pSize += 6;
}


// Moves the naggled packets the client has waiting into the update packets.
// FUNCTION: LITHTECH 0x00471fe0
void sm_FlushPacketGroups(CServerMgr *pServerMgr, UpdateInfo *pInfo, Client *pClient)
{
	uint32 i, j;
	CPacket *pDest;
	ClientPacketBuf *pBuf;

	for (i=0; i < 2; i++)
	{
		pDest = pInfo->m_cGroups[i];
		for (j=0; j < 2; j++)
		{
			pBuf = &pClient->m_PacketBufs[i][j];
			if (pBuf->m_pPacket->m_DataLen - 1 > 0)
			{
				if (pBuf->m_pPacket->m_DataLen - 1 < pDest->GetSpaceLeft())
				{
					pDest->WriteRaw(&pBuf->m_pPacket->m_Data.GetArray()[1], pBuf->m_pPacket->m_DataLen - 1);
				}
				else
				{
					SendToClient(pServerMgr, pClient, SMSG_PACKETGROUP, pBuf->m_pPacket, TRUE, pBuf->m_Unknown8);
				}

				pBuf->m_pPacket->ResetWrite();
			}
		}

		pDest->WriteType((uint8)0);
	}
}


// Activates objects near the client (a world tree callback).
// FUNCTION: LITHTECH 0x00471dd0
void sm_ActivateObjectCB(LTObject *pObject, UpdateInfo *pInfo)
{
	if (pObject->m_ObjType != WTObj_DObject)
		return;

	if (!ShouldSendToClient(pInfo->m_pServerMgr, pObject))
		return;

	if (pInfo->m_bAutoActivate)
	{
		pObject->m_InternalFlags &= ~IFLAG_INACTIVE_TICK;
		sm_ResetDeactivateTimer(pObject);
	}
}


// The current visibility query's internal state (vis query code at 0x0049e330).
struct VisQueryInfo
{
	uint8		m_Pad00[0x18];
	void		*m_pUserData;		// 0x18 VisQueryRequest::m_pUserData
};

// GLOBAL: LITHTECH 0x004e6270
extern VisQueryInfo *g_pCurVisQuery;

// Collects the objects in a visible node the client should be sent (a vis query callback).
// FUNCTION: LITHTECH 0x00472d10
void sm_GetVisibleObjectsCB(LTLink *pListHead, LTObject ***pppObjects, int *pnObjects)
{
	UpdateInfo *pInfo;
	LTLink *pCur;
	LTObject *pObject;

	pInfo = (UpdateInfo*)g_pCurVisQuery->m_pUserData;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pObject = (LTObject*)pCur->m_pData;

		// The client objects come after the server objects.
		if (!pObject->sd)
			return;

		if (ShouldSendToClient(pInfo->m_pServerMgr, pObject))
		{
			(*pppObjects)[*pnObjects] = pObject;
			(*pnObjects)++;
		}
	}
}


// Sends all the objects.
// FUNCTION: LITHTECH 0x004724a0
void SendAllObjects(CServerMgr *pServerMgr, ObjectMgr *pObjectMgr, UpdateInfo *pInfo)
{
	uint32 i;
	LTLink *pListHead, *pCur;
	LTObject *pObject;

	for (i=0; i < NUM_OBJECTTYPES; i++)
	{
		pListHead = &pObjectMgr->m_ObjectLists[i].m_Head;
		for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
		{
			pObject = (LTObject*)pCur->m_pData;

			if (pObject->m_InternalFlags & IFLAG_INACTIVE_TICK)
			{
				pObject->m_InternalFlags &= ~IFLAG_INACTIVE_TICK;
				sm_SetObjectStateFlags(pServerMgr, pObject, (pObject->m_InternalFlags & (IFLAG_INACTIVE | IFLAG_INACTIVE_TOUCH)) | IFLAG_AUTODEACTIVATED);
			}

			UpdateSendToClientState(pObject, pInfo);
		}
	}
}


// FUNCTION: LITHTECH 0x00472db0
void sm_SendSoundTracks(UpdateInfo *pInfo, CPacket *pPacket)
{
	LTLink *pCur;
	CSoundTrack *pSoundTrack;
	ObjInfo *pObjInfo;
	float fMaxDistSqr;
	uint16 *pNewIDs;

	for (pCur=pInfo->m_pServerMgr->m_SoundTrackList.m_Head.m_pNext; pCur != &pInfo->m_pServerMgr->m_SoundTrackList.m_Head;
		pCur=pCur->m_pNext)
	{
		pSoundTrack = (CSoundTrack*)pCur->m_pData;

		if (!(pInfo->m_pClient->m_ObjInfos[GetLinkID(pSoundTrack->m_pIDLink)].m_ChangeFlags & CF_SOUNDINFO))
		{
			// Check if the sound was removed.
			if (pSoundTrack->GetRemove())
				continue;

			// Check if the sound is done...
			if (pSoundTrack->m_pSoundData && pSoundTrack->GetTimeLeft() <= 0.0f)
				continue;

			// Check if the sound is within 2x its outer radius from the client or the viewer pos...
			if (pSoundTrack->m_dwFlags & (PLAYSOUND_3D | PLAYSOUND_AMBIENT))
			{
				fMaxDistSqr = 4.0f * pSoundTrack->m_fOuterRadius * pSoundTrack->m_fOuterRadius;
				if ((pSoundTrack->m_vPosition.DistSqr(pInfo->m_pClient->m_pObject->GetPos()) < fMaxDistSqr) ||
					(pSoundTrack->m_vPosition.DistSqr(pInfo->m_pClient->m_ViewPos) < fMaxDistSqr))
				{
				}
				else
				{
					continue;
				}
			}
		}

		sm_FlushUpdate(pInfo, pPacket, SMSG_UPDATE, 45);

		pObjInfo = &pInfo->m_pClient->m_ObjInfos[GetLinkID(pSoundTrack->m_pIDLink)];

		if (g_pCurSentList->m_nObjectIDs >= g_pCurSentList->m_AllocatedSize)
		{
			pNewIDs = (uint16*)dalloc(sizeof(uint16) * (g_pCurSentList->m_AllocatedSize + 200));
			memcpy(pNewIDs, g_pCurSentList->m_ObjectIDs, sizeof(uint16) * g_pCurSentList->m_nObjectIDs);
			dfree(g_pCurSentList->m_ObjectIDs);
			g_pCurSentList->m_ObjectIDs = pNewIDs;
			g_pCurSentList->m_AllocatedSize += 200;
		}

		g_pCurSentList->m_ObjectIDs[g_pCurSentList->m_nObjectIDs] = (uint16)GetLinkID(pSoundTrack->m_pIDLink);
		g_pCurSentList->m_nObjectIDs++;

		// If the client already told us the sound is done, then don't send it again...
		if (pObjInfo->m_ChangeFlags && !(pObjInfo->m_nSoundFlags & OBJINFOSOUNDF_CLIENTDONE))
		{
			FillSoundTrackPacketFromInfo(pInfo->m_pServerMgr, pSoundTrack, pObjInfo, pInfo->m_pClient, pPacket);
		}

		// Clear 'em.
		pObjInfo->m_ChangeFlags = CF_SENTINFO;
	}
}


// FUNCTION: LITHTECH 0x00472fe0
Client* sm_FindClient(CServerMgr *pServerMgr, CBaseConn *connID)
{
	LTLink *pCur;
	Client *pClient;

	for (pCur=pServerMgr->m_Clients.m_Head.m_pNext; pCur != &pServerMgr->m_Clients.m_Head; pCur=pCur->m_pNext)
	{
		pClient = (Client*)pCur->m_pData;
		if (pClient->m_ConnectionID == connID)
			return pClient;
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x00473010
void sm_SetSendSkyDef(CServerMgr *pServerMgr)
{
	LTLink *pCur;

	for (pCur=pServerMgr->m_Clients.m_Head.m_pNext; pCur != &pServerMgr->m_Clients.m_Head; pCur=pCur->m_pNext)
	{
		((Client*)pCur->m_pData)->m_ClientFlags |= CFLAG_SENDSKYDEF;
	}
}


// FUNCTION: LITHTECH 0x00473040
void sm_TellClientAboutSky(CServerMgr *pServerMgr, Client *pClient)
{
	CPacket *pPacket;
	uint16 i;

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	pPacket->WriteRaw(&pServerMgr->m_SkyDef, sizeof(pServerMgr->m_SkyDef));
	pPacket->WriteType((uint16)MAX_SKYOBJECTS);
	for (i=0; i < MAX_SKYOBJECTS; i++)
	{
		pPacket->WriteType(pServerMgr->m_SkyObjects[i]);
	}

	SendToClient(pServerMgr, pClient, SMSG_SKYDEF, pPacket, FALSE, MESSAGE_GUARANTEED);
	pPacket->Release();
}


// FUNCTION: LITHTECH 0x004733f0
LTRESULT sm_RemoveObjectFromSky(CServerMgr *pServerMgr, LTObject *pObject)
{
	uint32 i;

	if (!(~pObject->m_InternalFlags & IFLAG_INSKY))
	{
		pObject->m_InternalFlags &= ~IFLAG_INSKY;
		sm_SetSendSkyDef(pServerMgr);

		for (i=0; i < MAX_SKYOBJECTS; i++)
		{
			if (g_pServerMgr->m_SkyObjects[i] == pObject->m_ObjectID)
			{
				pServerMgr->m_SkyObjects[i] = INVALID_OBJECTID;
				sm_SetSendSkyDef(g_pServerMgr);
				break;
			}
		}
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00473460
FileIDInfo* sm_GetClientFileIDInfo(Client *pClient, uint16 wFileID)
{
	HHashElement *hElement;
	FileIDInfo *pFileIDInfo;

	hElement = hs_FindElement(pClient->m_hFileIDTable, &wFileID, 2);
	if (hElement)
	{
		pFileIDInfo = (FileIDInfo*)hs_GetElementUserData(hElement);
		if (pFileIDInfo)
			return pFileIDInfo;
	}

	pFileIDInfo = (FileIDInfo*)sb_Allocate_z(&g_pServerMgr->m_BankCAC);
	if (!pFileIDInfo)
		return LTNULL;

	pFileIDInfo->m_nChangeFlags = FILEIDINFOF_SOUNDPLAYSOUNDFLAGS | FILEIDINFOF_SOUNDPRIORITY | FILEIDINFOF_RADIUS;

	hElement = hs_AddElement(pClient->m_hFileIDTable, &wFileID, 2);
	if (!hElement)
	{
		dfree(pFileIDInfo);
		return LTNULL;
	}

	hs_SetElementUserData(hElement, pFileIDInfo);
	return pFileIDInfo;
}


// Finds the client's pending change for a light animation.
// FUNCTION: LITHTECH 0x00473510
LTBOOL sm_FindLightAnimChange(Client *pClient, uint32 iLightAnim, uint32 *pIndex)
{
	uint32 i;

	for (i=0; i < pClient->m_nLightAnimChanges; i++)
	{
		if (pClient->m_LightAnimChanges[i].m_iLightAnim == (uint16)iLightAnim)
		{
			*pIndex = i;
			return TRUE;
		}
	}

	return FALSE;
}


// Queues a light animation change for all the clients.
// FUNCTION: LITHTECH 0x00473550
void sm_SetLightAnimChanged(CServerMgr *pServerMgr, uint32 iLightAnim, uint32 flags)
{
	LTLink *pCur, *pListHead;
	Client *pClient;
	uint32 index;

	if (iLightAnim >= pServerMgr->m_World.m_LightAnims.GetSize())
		return;

	pListHead = &pServerMgr->m_Clients.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pClient = (Client*)pCur->m_pData;

		if (sm_FindLightAnimChange(pClient, iLightAnim, &index))
		{
			pClient->m_LightAnimChanges[index].m_ChangeFlags |= (uint16)flags;
		}
		else
		{
			// Full?  Flush them.
			if (pClient->m_nLightAnimChanges >= MAX_LIGHTANIM_CHANGES)
				sm_SendChangedLightAnims(pServerMgr, pClient);

			pClient->m_LightAnimChanges[pClient->m_nLightAnimChanges].m_iLightAnim = (uint16)iLightAnim;
			pClient->m_LightAnimChanges[pClient->m_nLightAnimChanges].m_ChangeFlags = (uint16)flags;
			pClient->m_nLightAnimChanges++;
		}
	}
}


// Out-of-line copies (inline budget).
// FUNCTION: LITHTECH 0x00473600 ?WriteTypeImpl@CPacket@@QAEXM@Z
// Forces the out-of-line copy now that WriteType wraps WriteTypeImpl (not in lithtech.exe).
void (CPacket::*g_pfnWriteTypeImplFloat)(float val) = &CPacket::WriteTypeImpl;
