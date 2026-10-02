// Jupiter runtime/client/src/shellnet.cpp
// Talon's packet handlers read reference-counted CPackets and reach the client manager
// through CClientShell::m_pClientMgr.
// FLAGS: /O2 /GX-
#include <windows.h>
#include <string.h>
#include "bdefs.h"
#include "clientshell.h"
#include "clientmgr.h"
#include "iclientshell.h"
#include "console.h"
#include "packet.h"
#include "concommand.h"
#include "client_filemgr.h"
#include "de_world.h"
#include "de_mainworld.h"
#include "model.h"
#include "sprite.h"
#include "iltclient.h"
#include "setupobject.h"

#define SMSG_NETPROTOCOLVERSION		4
#define SMSG_UNLOADWORLD			5
#define SMSG_LOADWORLD				6
#define SMSG_CLIENTOBJECTID			7
#define SMSG_UPDATE					8
#define SMSG_UNGUARANTEEDUPDATE		10
#define SMSG_YOURID					12
#define SMSG_MESSAGE				13
#define SMSG_PACKETGROUP			14
#define SMSG_CHANGEOBJECTFILENAMES	15
#define SMSG_CONSOLEVAR				16
#define SMSG_SKYDEF					17
#define SMSG_INSTANTSPECIALEFFECT	18
#define SMSG_PORTALFLAGS			19
#define SMSG_PRELOADLIST			21
#define SMSG_THREADLOAD				23
#define SMSG_UNLOAD					24
#define SMSG_LIGHTANIMINFO			25
#define SMSG_GLOBALLIGHT			26
#define SMSG_PEERAUTH				27

#define CMSG_GOODBYE				6
#define CMSG_CONNECTSTAGE			9

#define LT_NETVERSION				6

#define LTEVENT_DISCONNECT			1

void cs_UnloadWorld(CClientShell *pShell);		// 0x00416160 (clientshell.cpp)
void r_UnbindTexture(SharedTexture *pTexture);		// 0x0046f660

#define TYPECODE_MODEL		1


// GLOBAL: LITHTECH 0x004e376c
extern int32 g_bDebugPackets;

// Object change flags (CF_).
#define CF_NEWOBJECT		(1<<0)
#define CF_POSITION			(1<<1)
#define CF_ROTATION			(1<<2)
#define CF_FLAGS			(1<<3)
#define CF_SCALE			(1<<4)
#define CF_MODELINFO		(1<<5)
#define CF_COLORINFO		(1<<6)
#define CF_ATTACHMENTS		(1<<8)


// The main list of packet handlers.
typedef LTRESULT (*ShellPacketHandlerFn)(CClientShell *pShell, CPacket *pPacket);
struct ShellPacketHandler
{
	ShellPacketHandlerFn	fn;
};
// GLOBAL: LITHTECH 0x004e5dd0
ShellPacketHandler g_ShellHandlers[256];


// Handlers not decompiled yet.
LTRESULT OnUpdatePacket(CClientShell *pShell, CPacket *pPacket);					// 0x0048ada0
LTRESULT OnUnguaranteedUpdatePacket(CClientShell *pShell, CPacket *pPacket);		// 0x0048ce50
LTRESULT OnChangeObjectFilenamesPacket(CClientShell *pShell, CPacket *pPacket);	// 0x0048d150
LTRESULT OnPreloadListPacket(CClientShell *pShell, CPacket *pPacket);			// 0x0048dbb0
LTRESULT OnLightAnimInfoPacket(CClientShell *pShell, CPacket *pPacket);			// 0x0048e3b0


// ------------------------------------------------------------------------- //
// Packet handlers.
// ------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0048a930
LTRESULT OnYourIDPacket(CClientShell *pShell, CPacket *pPacket)
{
	pShell->m_ClientID = pPacket->ReadType((uint16*)0);
	pShell->m_bLocal = pPacket->ReadType((uint8*)0);

	con_Printf(CONRGB(250,100,100), 1, "Got ID packet (%d)", pShell->m_ClientID);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048aa50
LTRESULT OnPeerAuthPacket(CClientShell *pShell, CPacket *pPacket)
{
	pShell->m_pClientMgr->OnPeerAuthPacket(pPacket);
	return LT_OK;
}


// STUB: LITHTECH 0x0048aa70
// The original calls CMoArray<uint8>::Insert2 out of line in the inlined WriteType.
LTRESULT OnLoadWorldPacket(CClientShell *pShell, CPacket *pPacket)
{
	LTRESULT dResult;
	CPacket *pResponse;

	pShell->m_ClientObjectID = 0xFFFF;

	dResult = pShell->DoLoadWorld(pPacket, LTFALSE);
	if(dResult == LT_OK)
	{
		// Tell the server we're ready.
		pResponse = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));
		pResponse->m_Data[0] = CMSG_CONNECTSTAGE;
		pResponse->WriteType((uint8)0);
		pShell->m_pClientMgr->m_NetMgr.SendPacket(pResponse, pShell->m_HostID, MESSAGE_GUARANTEED);
		pResponse->Release();
	}

	return dResult;
}


// FUNCTION: LITHTECH 0x0048aba0
LTRESULT OnUnloadWorldPacket(CClientShell *pShell, CPacket *pPacket)
{
	pShell->m_ClientObjectID = 0xFFFF;
	cs_UnloadWorld(pShell);
	return LT_OK;
}



// Our build inlines more than the original here (592 vs 480 bytes); see README "How VC6 decides what to inline".
// STUB: LITHTECH 0x0048abc0
LTRESULT OnPacketGroupPacket(CClientShell *pShell, CPacket *pPacket)
{
	CPacket *pSubPacket;
	uint16 nLength;
	uint8 packetID;
	LTRESULT dResult;

	pSubPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	while((int)(pPacket->m_DataLen - pPacket->m_Pos) > 0)
	{
		nLength = pPacket->ReadType((uint8*)0);
		if((int)nLength > (int)(pPacket->m_DataLen - pPacket->m_Pos))
		{
			pShell->m_pClientMgr->SetupError(LT_INVALIDSERVERPACKET);
			dsi_OnReturnError(LT_INVALIDSERVERPACKET);
			if(g_DebugLevel >= 1)
				dsi_ConsolePrint(g_ReturnErrString, "OnMessageGroupPacket", "LT_INVALIDSERVERPACKET", "invalid packet");
			if(pSubPacket)
				pSubPacket->Release();
			return LT_INVALIDSERVERPACKET;
		}
		else if(nLength == 0)
		{
			// (This signals the end of the grouped packets).
			break;
		}

		// Set up a sub-packet.
		pSubPacket->Init(nLength, MAX_PACKET_LEN);
		pPacket->ReadRaw(pSubPacket->m_Data.GetArray(), nLength);
		pSubPacket->m_DataLen = nLength;
		pSubPacket->m_Pos = 1;

		packetID = pSubPacket->GetPacketID() & 0x3F;
		if(g_ShellHandlers[packetID].fn)
		{
			dResult = g_ShellHandlers[packetID].fn(pShell, pSubPacket);
			if(dResult != LT_OK)
			{
				pSubPacket->Release();
				return dResult;
			}
		}
	}

	if(pSubPacket)
		pSubPacket->Release();

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048bb00
void PrintPacketDebugInfo(LTObject *pObject, uint32 flags)
{
	if(g_bDebugPackets == 2)
	{
		con_WhitePrintf(g_EmptyString);
		con_WhitePrintf("ObjectID: %d, type %d, flags %d",
			pObject->m_ObjectID, (char)pObject->m_ObjectType, flags);

		if(flags & CF_NEWOBJECT)		con_WhitePrintf("CF_NEWOBJECT");
		if(flags & CF_POSITION)			con_WhitePrintf("CF_POSITION");
		if(flags & CF_ROTATION)			con_WhitePrintf("CF_ROTATION");
		if(flags & CF_FLAGS)			con_WhitePrintf("CF_FLAGS");
		if(flags & CF_SCALE)			con_WhitePrintf("CF_SCALE");
		if(flags & CF_MODELINFO)		con_WhitePrintf("CF_MODELINFO");
		if(flags & CF_COLORINFO)		con_WhitePrintf("CF_COLORINFO");
		if(flags & CF_ATTACHMENTS)		con_WhitePrintf("CF_ATTACHMENTS");
	}
	else if(g_bDebugPackets == 1)
	{
		if(flags & CF_NEWOBJECT)
		{
			con_WhitePrintf(g_EmptyString);
			con_WhitePrintf("ObjectID: %d, type: %d, flags %d",
				pObject->m_ObjectID, (char)pObject->m_ObjectType, flags);
			con_WhitePrintf("CF_NEWOBJECT");
		}
	}
}


// FUNCTION: LITHTECH 0x0048d040
LTRESULT OnServerGameTime(CClientShell *pShell, CPacket *pPacket)
{
	float newTime, delta;

	delta = 0.0f;
	newTime = pPacket->ReadType((float*)0);
	if(newTime > pShell->m_GameTime)
	{
		delta = newTime - pShell->m_GameTime;
		pShell->m_GameTime = newTime;
	}

	pShell->m_ServerPeriod = delta;
	return LT_OK;
}


// STUB: LITHTECH 0x0048d0f0
// The original computes &pPacket->m_Message before loading messageID for the call.
LTRESULT OnMessagePacket(CClientShell *pShell, CPacket *pPacket)
{
	uint8 messageID;

	// The message ID is the last byte.
	if((uint32)(pPacket->m_DataLen - pPacket->m_Pos) >= sizeof(uint8))
	{
		messageID = pPacket->m_Data[pPacket->m_DataLen - 1];
		--pPacket->m_DataLen;
	}
	else
	{
		messageID = 0;
	}

	pShell->m_pClientMgr->SetupPacketMessage(pPacket);
	pShell->m_pClientMgr->m_pClientShell->OnMessage(messageID, &pPacket->m_Message);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048d430
LTRESULT OnConsoleVar(CClientShell *pShell, CPacket *pPacket)
{
	char *pVarName, *pVarValue;

	pVarName = pPacket->ReadString();
	pVarValue = pPacket->ReadString();
	cc_SetConsoleVariable(&pShell->m_pClientMgr->m_ServerConsoleMirror, pVarName, pVarValue);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048d470
LTRESULT OnSkyDef(CClientShell *pShell, CPacket *pPacket)
{
	uint16 i, nSkyObjects;

	pPacket->ReadRaw(&pShell->m_pClientMgr->m_SkyDef, sizeof(SkyDef));

	nSkyObjects = pPacket->ReadType((uint16*)0);
	if(nSkyObjects > MAX_SKYOBJECTS)
	{
		pShell->m_pClientMgr->SetupError(LT_INVALIDSERVERPACKET);
		RETURN_ERROR_PARAM(1, OnSkyDef, LT_ERROR, "invalid packet");
	}

	memset(pShell->m_pClientMgr->m_SkyObjects, 0xFF, sizeof(pShell->m_pClientMgr->m_SkyObjects));
	for(i=0; i < nSkyObjects; i++)
	{
		pShell->m_pClientMgr->m_SkyObjects[i] = pPacket->ReadType((uint16*)0);
	}

	return LT_OK;
}


// STUB: LITHTECH 0x0048d610
// The original calls CPacket::ReadType<float> (0x0048e900) out of line for the first two reads only.
LTRESULT OnGlobalLight(CClientShell *pShell, CPacket *pPacket)
{
	ILTClient *pClientDE;
	LTVector vec;
	float fScale;

	pClientDE = pShell->m_pClientMgr->m_pClientDE;

	vec.x = pPacket->ReadType((float*)0);
	vec.y = pPacket->ReadType((float*)0);
	vec.z = pPacket->ReadType((float*)0);
	pClientDE->SetGlobalLightDir(vec);

	vec.x = pPacket->ReadType((float*)0);
	vec.y = pPacket->ReadType((float*)0);
	vec.z = pPacket->ReadType((float*)0);
	pClientDE->SetGlobalLightColor(vec);

	fScale = pPacket->ReadType((float*)0);
	vec.Init(fScale, fScale, fScale);
	pClientDE->SetGlobalLightScale(&vec);

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048d8e0
LTRESULT OnInstantSpecialEffect(CClientShell *pShell, CPacket *pPacket)
{
	IClientShell *pClientShell;

	pClientShell = pShell->m_pClientMgr->m_pClientShell;
	if(pClientShell)
	{
		pShell->m_pClientMgr->SetupPacketMessage(pPacket);
		pClientShell->SpecialEffectNotify(LTNULL, &pPacket->m_Message);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048d910
LTRESULT OnClientObjectID(CClientShell *pShell, CPacket *pPacket)
{
	pShell->m_ClientObjectID = pPacket->ReadType((uint16*)0);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048d9b0
LTRESULT OnPortalFlagsPacket(CClientShell *pShell, CPacket *pPacket)
{
	MainWorld *pWorld;
	WorldData *pWorldData;
	uint32 iWorldModel, iPortal;

	pWorld = pShell->GetWorld();
	iWorldModel = pPacket->ReadType((uint16*)0);
	iPortal = pPacket->ReadType((uint16*)0);

	if(iWorldModel < pWorld->m_WorldModels.GetSize())
	{
		pWorldData = pWorld->m_WorldModels[iWorldModel];
		if(iPortal < pWorldData->m_pOriginalBsp->m_nPortals)
		{
			pWorldData->m_pOriginalBsp->m_Portals[iPortal].m_Flags = pPacket->ReadType((uint8*)0);
			return LT_OK;
		}
	}

	pShell->m_pClientMgr->SetupError(LT_INVALIDSERVERPACKET);
	RETURN_ERROR(1, OnPortalFlagsPacket, LT_INVALIDSERVERPACKET);
}


// FUNCTION: LITHTECH 0x0048df60
LTRESULT OnNetProtocolVersionPacket(CClientShell *pShell, CPacket *pPacket)
{
	uint32 version;

	version = pPacket->ReadType((uint32*)0);
	if(version == LT_NETVERSION)
		return LT_OK;

	pShell->m_pClientMgr->SetupError(LT_INVALIDNETVERSION, LT_NETVERSION, version);
	RETURN_ERROR(1, OnNetProtocolVersionPacket, LT_INVALIDNETVERSION);
}



// FUNCTION: LITHTECH 0x0048e030
LTRESULT OnThreadLoadPacket(CClientShell *pShell, CPacket *pPacket)
{
	FileRef ref;
	uint8 fileType;
	FileIdentifier *pIdent;
	Model *pModel;

	fileType = pPacket->ReadType((uint8*)0);
	ref.m_FileType = FILE_SERVERFILE;
	ref.m_FileID = pPacket->ReadType((uint16*)0);

	if(fileType == FT_MODEL)
	{
		cm_LoadModel2(pShell->m_pClientMgr, &ref, &pModel, &pIdent, LTTRUE, LTFALSE);
		return LT_OK;
	}
	else if(fileType == FT_TEXTURE)
	{
		cm_AddSharedTexture(pShell->m_pClientMgr, &ref);
		return LT_OK;
	}

	RETURN_ERROR(1, OnThreadLoadPacket, LT_INVALIDSERVERPACKET);
}


// FUNCTION: LITHTECH 0x0048e1d0
LTRESULT OnUnloadPacket(CClientShell *pShell, CPacket *pPacket)
{
	FileRef ref;
	uint8 fileType;
	FileIdentifier *pIdent;
	Model *pModel;

	fileType = pPacket->ReadType((uint8*)0);
	ref.m_FileType = FILE_SERVERFILE;
	ref.m_FileID = pPacket->ReadType((uint16*)0);

	if(fileType == FT_MODEL)
	{
		pIdent = cf_GetFileIdentifier(pShell->m_pClientMgr->m_hFileMgr, &ref, TYPECODE_MODEL);
		if(pIdent && pIdent->m_pData)
		{
			pModel = (Model*)pIdent->m_pData;
			cm_RemoveModelObjects(pShell->m_pClientMgr, pModel, pIdent);
			delete pModel;
		}

		return LT_OK;
	}
	else if(fileType == FT_TEXTURE)
	{
		pIdent = cf_GetFileIdentifier(pShell->m_pClientMgr->m_hFileMgr, &ref, TYPECODE_MODEL);
		if(pIdent && pIdent->m_pData)
		{
			r_UnbindTexture((SharedTexture*)pIdent->m_pData);
		}

		return LT_OK;
	}

	RETURN_ERROR(1, OnThreadLoadPacket, LT_INVALIDSERVERPACKET);
}


// ------------------------------------------------------------------------- //
// Main routines.
// ------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0048a7c0
LTBOOL CClientShell::NewConnectionNotify(CBaseConn *id, LTBOOL bIsLocal)
{
	if(m_HostID)
	{
		return LTFALSE;
	}
	else
	{
		m_HostID = id;
		m_bOnServer = LTTRUE;
		return LTTRUE;
	}
}


// FUNCTION: LITHTECH 0x0048a7e0
void CClientShell::DisconnectNotify(CBaseConn *id)
{
	con_Printf(CONRGB(250,100,100), 1, "Disconnected from server");

	m_HostID = LTNULL;

	if(m_pClientMgr->m_pClientShell)
	{
		m_pClientMgr->m_pClientShell->OnEvent(LTEVENT_DISCONNECT, 0);
	}
}


// FUNCTION: LITHTECH 0x0048a820
void CClientShell::HandleUnknownPacket(CPacket *pPacket, uint8 senderAddr[4], uint16 senderPort)
{
}


// FUNCTION: LITHTECH 0x0048a830
void CClientShell::SetDisconnectCode(uint32 nCode, char *pMsg)
{
	if(m_pClientMgr->m_pClientShell)
		m_pClientMgr->m_pClientShell->SetDisconnectCode(nCode, pMsg);
}


// FUNCTION: LITHTECH 0x0048a850
void CClientShell::InitHandlers()
{
	memset(g_ShellHandlers, 0, sizeof(g_ShellHandlers));

	g_ShellHandlers[SMSG_YOURID].fn = OnYourIDPacket;
	g_ShellHandlers[SMSG_LOADWORLD].fn = OnLoadWorldPacket;
	g_ShellHandlers[SMSG_UNLOADWORLD].fn = OnUnloadWorldPacket;
	g_ShellHandlers[SMSG_PEERAUTH].fn = OnPeerAuthPacket;

	g_ShellHandlers[SMSG_UPDATE].fn = OnUpdatePacket;
	g_ShellHandlers[SMSG_UNGUARANTEEDUPDATE].fn = OnUnguaranteedUpdatePacket;

	g_ShellHandlers[SMSG_MESSAGE].fn = OnMessagePacket;
	g_ShellHandlers[SMSG_PACKETGROUP].fn = OnPacketGroupPacket;
	g_ShellHandlers[SMSG_CHANGEOBJECTFILENAMES].fn = OnChangeObjectFilenamesPacket;
	g_ShellHandlers[SMSG_CONSOLEVAR].fn = OnConsoleVar;
	g_ShellHandlers[SMSG_SKYDEF].fn = OnSkyDef;
	g_ShellHandlers[SMSG_GLOBALLIGHT].fn = OnGlobalLight;
	g_ShellHandlers[SMSG_INSTANTSPECIALEFFECT].fn = OnInstantSpecialEffect;
	g_ShellHandlers[SMSG_CLIENTOBJECTID].fn = OnClientObjectID;
	g_ShellHandlers[SMSG_PORTALFLAGS].fn = OnPortalFlagsPacket;
	g_ShellHandlers[SMSG_PRELOADLIST].fn = OnPreloadListPacket;
	g_ShellHandlers[SMSG_NETPROTOCOLVERSION].fn = OnNetProtocolVersionPacket;

	g_ShellHandlers[SMSG_THREADLOAD].fn = OnThreadLoadPacket;
	g_ShellHandlers[SMSG_UNLOAD].fn = OnUnloadPacket;
	g_ShellHandlers[SMSG_LIGHTANIMINFO].fn = OnLightAnimInfoPacket;
}


// FUNCTION: LITHTECH 0x0048e770
LTRESULT CClientShell::ProcessPackets()
{
	CPacket *pPacket;
	uint8 packetID;
	LTRESULT dResult;

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	// Process all the packets.
	m_pClientMgr->m_NetMgr.StartGettingPackets();
	while(m_pClientMgr->m_NetMgr.GetPacket(pPacket, (CBaseConn**)LTTRUE))
	{
		packetID = pPacket->GetPacketID() & 0x3F;

		// Call the appropriate packet handler.
		if(g_ShellHandlers[packetID].fn)
		{
			dResult = g_ShellHandlers[packetID].fn(this, pPacket);
			if(dResult != LT_OK)
			{
				m_pClientMgr->m_NetMgr.EndGettingPackets();
				pPacket->Release();
				return dResult;
			}
		}
		else
		{
			cf_ProcessPacket(m_pClientMgr->m_hFileMgr, *(CPacket_Read*)pPacket);
		}
	}

	m_pClientMgr->m_NetMgr.EndGettingPackets();
	if(pPacket)
		pPacket->Release();

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048e860
void CClientShell::SendGoodbye()
{
	CPacket *pPacket;

	pPacket = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	if(m_HostID)
	{
		pPacket->m_Data[0] = CMSG_GOODBYE;
		m_pClientMgr->m_NetMgr.SendPacket(pPacket, m_HostID, MESSAGE_GUARANTEED);
	}

	if(pPacket)
		pPacket->Release();
}


// Template and inline code emitted into this object (the linker kept these copies). The
// original's handlers call them; this function only makes VC6 emit them.
// FUNCTION: LITHTECH 0x0048e8d0 ??4CPacketRef@@QAEPAVCPacket@@ABV0@@Z
// FUNCTION: LITHTECH 0x0048e900 ?ReadTypeImpl@CPacket@@QAEMPAM@Z
// STANDIN: emits CPacketRef::operator= and ReadTypeImpl<float> (not in lithtech.exe)
void shellnet_EmitInlines(CPacket *pPacket, CPacketRef *pRef)
{
	float (CPacket::*pReadFloat)(float*) = &CPacket::ReadTypeImpl;
	CPacket* (CPacketRef::*pAssign)(const CPacketRef&) = &CPacketRef::operator=;

	(pPacket->*pReadFloat)(0);
	(pRef->*pAssign)(*pRef);
}
