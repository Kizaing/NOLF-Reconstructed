// Jupiter runtime/server/src/s_net.cpp: implements net-related stuff in the ServerMgr.
// Talon passes the server manager explicitly and still uses the ref-counted CPacket.
#include <string.h>
#include "bdefs.h"
#include "servermgr.h"
#include "s_client.h"
#include "serverevent.h"
#include "ftserv.h"
#include "packet.h"
#include "server_interface.h"
#include "soundtrack.h"
#include "server_filemgr.h"

LTRESULT sm_HandleCommand(ConsoleState *pState, char *pCommand);	// s_concommand, 0x00473e80

// Talon client-to-server packet IDs.
#define CMSG_HELLO				5
#define CMSG_GOODBYE			6
#define CMSG_UPDATE				7
#define CMSG_SOUNDUPDATE		8
#define CMSG_CONNECTSTAGE		9
#define CMSG_COMMANDSTRING		10
#define CMSG_MESSAGE			11
#define CMSG_PEERTOPEERAUTH		13

#define SMSG_PACKETGROUP		14

#define NETMGR_TRAVELDIR_CLIENT2SERVER	2

#define DISCONNECTREASON_VOLUNTARY_SERVERSIDE	5

#define OBJINFOSOUNDF_CLIENTDONE	(1<<0)

ObjectMapEntry* sm_FindRecord(CServerMgr *pServerMgr, uint16 objectID);	// s_object, 0x00477e10

// GLOBAL: LITHTECH 0x004e49f8
ServerPacketHandlerFn g_ServerHandlers[256];


// ----------------------------------------------------------------------- //
// These 2 are notification messages from CNetMgr.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00473ee0
LTBOOL CServerMgr::NewConnectionNotify(CBaseConn *id, LTBOOL bIsLocal)
{
	SetupGlobals();
	sm_OnNewConnection(this, id, bIsLocal);
	return TRUE;
}


// FUNCTION: LITHTECH 0x00473f10
void CServerMgr::DisconnectNotify(CBaseConn *id)
{
	SetupGlobals();
	sm_OnBrokenConnection(this, id);
}


// FUNCTION: LITHTECH 0x00473f30
void CServerMgr::HandleUnknownPacket(CPacket *pPacket, uint8 senderAddr[4], uint16 senderPort)
{
	if (m_pServerAppHandler)
	{
		m_pServerAppHandler->ProcessPacket((char*)pPacket->m_Data.GetArray(), pPacket->m_DataLen,
			senderAddr, senderPort);
	}

	// Tell the server shell about unknown packet.
	if (m_ClassMgr.m_pServerShell)
	{
		m_ClassMgr.m_pServerShell->ProcessPacket((char*)pPacket->m_Data.GetArray(), pPacket->m_DataLen,
			senderAddr, senderPort);
	}
}


// Writes the model's file IDs (or just counts the bytes if pPacket is null).
// FUNCTION: LITHTECH 0x00473f90
void sm_WriteModelFiles(LTObject *pObj, CPacket *pPacket, uint32 *pSize)
{
	uint16 i;
	UsedFile *pFile;

	if (pPacket)
		pPacket->WriteType((uint16)pObj->sd->m_pFile->m_FileID);

	if (pSize)
		*pSize += 2;

	for (i=0; i < MAX_MODEL_TEXTURES; i++)
	{
		if (pPacket)
		{
			pFile = pObj->sd->m_pSkins[i];
			pPacket->WriteType(pFile ? (uint16)pFile->m_FileID : (uint16)0xFFFF);
		}

		if (pSize)
			*pSize += 2;
	}
}


// FUNCTION: LITHTECH 0x00475580
void sm_SendToAllClients(CServerMgr *pServerMgr, uint8 msgID, CPacket *pPacket, uint32 packetFlags)
{
	LTLink *pCur, *pListHead;
	Client *pClient;

	pPacket->m_Data[0] = msgID;

	pListHead = &pServerMgr->m_Clients.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pClient = (Client*)pCur->m_pData;

		if (pClient->m_ConnectionID)
		{
			pServerMgr->m_NetMgr.SendPacket(pPacket, pClient->m_ConnectionID, packetFlags);
		}
	}
}


// FUNCTION: LITHTECH 0x004755d0
void SendToClient(CServerMgr *pServerMgr, Client *pClient, uint8 msgID, CPacket *pPacket,
	LTBOOL bSendToAttachments, uint32 packetFlags)
{
	LTLink *pCur;
	Client *pAttachment;

	pPacket->m_Data[0] = msgID;

	if (pClient->m_ConnectionID)
	{
		if (!pServerMgr->m_NetMgr.SendPacket(pPacket, pClient->m_ConnectionID, packetFlags))
			++pServerMgr->m_nDroppedSendPackets;

		++pServerMgr->m_nSendPackets;
	}

	// Send it to the clients attached to this one.
	if (bSendToAttachments)
	{
		for (pCur=pClient->m_Attachments.m_pNext; pCur != &pClient->m_Attachments; pCur=pCur->m_pNext)
		{
			pAttachment = (Client*)pCur->m_pData;

			if (pAttachment->m_ConnectionID)
			{
				pServerMgr->m_NetMgr.SendPacket(pPacket, pAttachment->m_ConnectionID, packetFlags);
			}
		}
	}
}


// Sends the packet to the client, grouping small naggled packets into the client's packet buffers.
// FUNCTION: LITHTECH 0x00475660
LTRESULT sm_SendToClient(CServerMgr *pServerMgr, Client *pClient, uint8 msgID, CPacket *pPacket, uint32 packetFlags)
{
	ClientPacketBuf *pBuf;

	if ((pClient->m_ClientFlags & CFLAG_LOCAL) || !(packetFlags & MESSAGE_NAGGLEMASK) || pPacket->m_DataLen > 255)
	{
		SendToClient(g_pServerMgr, pClient, msgID, pPacket, TRUE, packetFlags);
		return LT_OK;
	}

	pBuf = &pClient->m_PacketBufs[(packetFlags >> 7) & 1][(packetFlags >> 1) & 1];

	// Flush the group if this one won't fit.
	if (pPacket->m_DataLen + 1 > pBuf->m_pPacket->GetSpaceLeft())
	{
		SendToClient(g_pServerMgr, pClient, SMSG_PACKETGROUP, pBuf->m_pPacket, TRUE, pBuf->m_Unknown8);
		pBuf->m_pPacket->ResetWrite();
	}

	// Still too big?  Send it by itself.
	if (pPacket->m_DataLen + 1 > pBuf->m_pPacket->GetSpaceLeft())
	{
		SendToClient(g_pServerMgr, pClient, SMSG_PACKETGROUP, pPacket, TRUE, packetFlags);
		return LT_OK;
	}

	pPacket->m_Data[0] = msgID;
	pBuf->m_pPacket->WriteType((uint8)pPacket->m_DataLen);
	pBuf->m_pPacket->WriteRaw(pPacket->m_Data.GetArray(), pPacket->m_DataLen);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00475830
void sm_SendToAllClientsInWorld(CServerMgr *pServerMgr, uint8 msgID, CPacket *pPacket)
{
	LTLink *pCur, *pListHead;
	Client *pClient;

	pPacket->m_Data[0] = msgID;

	pListHead = &pServerMgr->m_Clients.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pClient = (Client*)pCur->m_pData;

		if (pClient->m_ConnectionID && pClient->m_State == CLIENT_INWORLD)
		{
			pServerMgr->m_NetMgr.SendPacket(pPacket, pClient->m_ConnectionID, MESSAGE_GUARANTEED);
		}
	}
}


// ----------------------------------------------------------------------- //
// Creates an event of the specified type and adds it to the client's structures.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00475890
CServerEvent* CreateServerEvent(CServerMgr *pServerMgr, int type)
{
	CServerEvent *pRet;
	LTLink *pCur, *pListHead;
	Client *pClient;
	ClientStructNode *pNode;

	pRet = (CServerEvent*)sb_Allocate(&pServerMgr->m_ServerEventBank);
	memset(pRet, 0, sizeof(*pRet));
	dl_InitList(&pRet->m_ClientStructNodeList);

	// Add the events to the client structs.
	pListHead = &pServerMgr->m_Clients.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pClient = (Client*)pCur->m_pData;
		if (pClient->m_State != CLIENT_INWORLD)
			continue;

		pNode = (ClientStructNode*)sb_Allocate(&pServerMgr->m_ClientStructNodeBank);
		dl_AddHead(&pRet->m_ClientStructNodeList, &pNode->m_Link, pNode);
		dl_AddHead(&pClient->m_Events, &pNode->m_mllNode, pRet);
		pRet->m_RefCount++;
	}

	pRet->m_EventType = type;
	return pRet;
}


// Set the change flags based on what's different...
// FUNCTION: LITHTECH 0x004760a0
void GetSoundFileIDInfoFlags(FileIDInfo *pFileIDInfo, FileIDInfo *pCurrent)
{
	if (pFileIDInfo->m_nChangeFlags & FILEIDINFOF_SOUNDPLAYSOUNDFLAGS || pFileIDInfo->m_wSoundPlaySoundFlags != pCurrent->m_wSoundPlaySoundFlags)
	{
		pFileIDInfo->m_nChangeFlags |= FILEIDINFOF_SOUNDPLAYSOUNDFLAGS;
		pFileIDInfo->m_wSoundPlaySoundFlags = pCurrent->m_wSoundPlaySoundFlags;
	}
	if (pFileIDInfo->m_nChangeFlags & FILEIDINFOF_SOUNDPRIORITY || pFileIDInfo->m_nSoundPriority != pCurrent->m_nSoundPriority)
	{
		pFileIDInfo->m_nChangeFlags |= FILEIDINFOF_SOUNDPRIORITY;
		pFileIDInfo->m_nSoundPriority = pCurrent->m_nSoundPriority;
	}
	if (pFileIDInfo->m_nChangeFlags & FILEIDINFOF_RADIUS || pFileIDInfo->m_nSoundOuterRadius != pCurrent->m_nSoundOuterRadius || pFileIDInfo->m_nSoundInnerRadius != pCurrent->m_nSoundInnerRadius)
	{
		pFileIDInfo->m_nChangeFlags |= FILEIDINFOF_RADIUS;
		pFileIDInfo->m_nSoundOuterRadius = pCurrent->m_nSoundOuterRadius;
		pFileIDInfo->m_nSoundInnerRadius = pCurrent->m_nSoundInnerRadius;
	}
}


// ----------------------------------------------------------------------- //
// Reads in all packets from the net.
// ----------------------------------------------------------------------- //

// STUB: LITHTECH 0x00476710
// Loads pClient->m_hFTServ into ecx where the original uses eax (2 bytes).
LTBOOL ProcessIncomingPackets(CServerMgr *pServerMgr)
{
	CPacket *pPacket;
	ServerPacketHandlerFn *pHandler;
	Client *pClient;
	LTRESULT dResult;

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	pServerMgr->m_NetMgr.StartGettingPackets();

	// Talon's second GetPacket parameter is a constant 2 (the travel direction in Jupiter).
	while (pServerMgr->m_NetMgr.GetPacket(pPacket, (CBaseConn**)NETMGR_TRAVELDIR_CLIENT2SERVER))
	{
		pHandler = &g_ServerHandlers[pPacket->m_Data[0] & 0x3f];
		if (*pHandler)
		{
			pClient = pPacket->m_pSender ? sm_FindClient(pServerMgr, pPacket->m_pSender) : LTNULL;

			dResult = (*pHandler)(pServerMgr, pPacket, pClient);
			if (dResult != LT_OK)
			{
				pServerMgr->m_NetMgr.EndGettingPackets();
				pPacket->Release();
				return dResult;
			}
		}
		else
		{
			// Let the file transfer manager have the packet (including the ID...)
			pClient = sm_FindClient(pServerMgr, pPacket->m_pSender);
			if (pClient)
				fts_ProcessPacket(pClient->m_hFTServ, pPacket);
		}
	}

	pServerMgr->m_NetMgr.EndGettingPackets();
	if (pPacket)
		pPacket->Release();

	return LT_OK;
}


// ----------------------------------------------------------------------- //
//   Packet handlers
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00476870
static LTRESULT OnSoundUpdatePacket(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient)
{
	uint16 objectID;
	ObjectMapEntry *pRecord;
	CSoundTrack *pSoundTrack;

	if (!pClient)
		return LT_OK;

	// Client has sent the sounds it has finished playing...
	while (pPacket->m_DataLen - pPacket->m_Pos != 0)
	{
		// Pull the sound track info out of the message...
		objectID = pPacket->ReadType((uint16*)0);
		pRecord = sm_FindRecord(pServerMgr, objectID);
		if (!pRecord || pRecord->m_nRecordType != RECORDTYPE_SOUND)
			continue;

		pSoundTrack = (CSoundTrack*)pRecord->m_pRecordData;
		if (!pSoundTrack)
			continue;

		// Skip it if the sound isn't done yet
		if (pClient->m_ObjInfos[objectID].m_nSoundFlags & OBJINFOSOUNDF_CLIENTDONE)
			continue;

		pSoundTrack->Release(&pClient->m_ObjInfos[objectID].m_nSoundFlags);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00476990
static LTRESULT OnClientUpdatePacket(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient)
{
	uint8 *pPrevCommands, *pCurCommands;
	float fRate;
	uint16 i, nCommands;
	int nChanged;
	uint8 changed[256];

	if (!pClient)
		return LT_OK;

	// Swap the command buffers.
	pPrevCommands = pClient->m_Commands[pClient->m_iCurCommands];
	pCurCommands = pClient->m_Commands[!pClient->m_iCurCommands];
	memset(pCurCommands, 0, MAX_CLIENT_COMMANDS);
	pClient->m_iCurCommands = !pClient->m_iCurCommands;

	fRate = (float)pPacket->ReadType((uint8*)0);
	if (fRate != pClient->m_Unknown128)
	{
		pClient->m_Unknown128 = fRate;
		pClient->m_Timer.SetUpdateRate(fRate);
	}

	// Read the commands that are on.
	nCommands = pPacket->ReadType((uint8*)0);
	for (i=nCommands; i > 0; i--)
	{
		pCurCommands[pPacket->ReadType((uint8*)0)] = 1;
	}

	// Tell the server about the ones that changed.
	nChanged = 0;
	for (i=0; i < MAX_CLIENT_COMMANDS; i++)
	{
		if (pCurCommands[i] != pPrevCommands[i])
			changed[nChanged++] = (uint8)i;
	}

	pServerMgr->ProcessClientCommands(pClient, changed, nChanged);

	if (pPacket->m_DataLen - pPacket->m_Pos > 0)
	{
		return OnSoundUpdatePacket(pServerMgr, pPacket, pClient);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00476c20
static LTRESULT OnClientDisconnectPacket(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient)
{
	if (pPacket->m_pSender)
	{
		// This will in turn call DisconnectNotify().
		pServerMgr->m_NetMgr.Disconnect(pPacket->m_pSender, DISCONNECTREASON_VOLUNTARY_SERVERSIDE);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00476c40
static LTRESULT OnCommandStringPacket(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient)
{
	if (pClient->m_ClientFlags & CFLAG_LOCAL)
	{
		return sm_HandleCommand(&pServerMgr->m_ConsoleState, pPacket->ReadString());
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00476c70
static LTRESULT OnPeerToPeerAuthPacket(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient)
{
	if (pClient && pServerMgr->m_ClassMgr.m_pServerShell)
	{
		pServerMgr->OnPeerToPeerAuthPacket(pClient, pPacket);
	}

	return LT_OK;
}


// STUB: LITHTECH 0x00476ca0
// eax/edx swapped for the message ID and the shell vtable (4 bytes).
static LTRESULT OnMessagePacket(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient)
{
	uint8 messageID;

	if (pClient && pServerMgr->m_ClassMgr.m_pServerShell)
	{
		pServerMgr->SetupPacketMessage(pPacket);

		// The message ID is the last byte.
		if ((uint32)(pPacket->m_DataLen - pPacket->m_Pos) >= 1)
		{
			messageID = pPacket->m_Data.GetArray()[pPacket->m_DataLen - 1];
			pPacket->m_DataLen--;
		}
		else
		{
			messageID = 0;
		}

		pServerMgr->m_ClassMgr.m_pServerShell->OnMessage((HCLIENT)pClient, messageID,
			(HMESSAGEREAD)&pPacket->m_Message);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00476d20
static LTRESULT OnConnectStagePacket(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient)
{
	uint8 type;

	type = pPacket->ReadType((uint8*)0);
	if (type == 0)
	{
		pClient->m_PuttingIntoWorldStage = PUTTINGINTOWORLD_LOADEDWORLD;
	}
	else
	{
		pClient->m_PuttingIntoWorldStage = PUTTINGINTOWORLD_PRELOADED;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00476dc0
static LTRESULT OnHelloPacket(CServerMgr *pServerMgr, CPacket *pPacket, Client *pClient)
{
	pClient->m_ClientDataLen = pPacket->ReadType((uint16*)0);
	if (pClient->m_ClientDataLen)
	{
		pClient->m_pClientData = new char[pClient->m_ClientDataLen];
		pPacket->ReadRaw(pClient->m_pClientData, (uint16)pClient->m_ClientDataLen);
	}

	pClient->m_ClientFlags |= CFLAG_GOT_HELLO;
	return LT_OK;
}


// ----------------------------------------------------------------------- //
// Just sets up some pointers to functions for packet receivers.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00476800
void InitServerNetHandlers()
{
	memset(g_ServerHandlers, 0, sizeof(g_ServerHandlers));

	g_ServerHandlers[CMSG_GOODBYE] = OnClientDisconnectPacket;
	g_ServerHandlers[CMSG_UPDATE] = OnClientUpdatePacket;
	g_ServerHandlers[CMSG_SOUNDUPDATE] = OnSoundUpdatePacket;
	g_ServerHandlers[CMSG_COMMANDSTRING] = OnCommandStringPacket;
	g_ServerHandlers[CMSG_MESSAGE] = OnMessagePacket;
	g_ServerHandlers[CMSG_CONNECTSTAGE] = OnConnectStagePacket;
	g_ServerHandlers[CMSG_HELLO] = OnHelloPacket;
	g_ServerHandlers[CMSG_PEERTOPEERAUTH] = OnPeerToPeerAuthPacket;
}
