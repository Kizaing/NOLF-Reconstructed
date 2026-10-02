// Jupiter runtime/shared/src/ftclient.cpp
// Talon reads and writes reference-counted CPackets, and the local server tells the client
// about files directly through clienthack_NewFile.
// FLAGS: /O2 /GX-
#define PACKET_INLINE_PAD	m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0; m_ErrorFlags|=0;
#include <string.h>
#include "bdefs.h"
#include "ftclient.h"
#include "packet.h"
#include "netmgr.h"
#include "de_memory.h"



// ----------------------------------------------------------------------- //
// Defines (Jupiter ftbase.h).
// ----------------------------------------------------------------------- //

#define PACKETID_FTBASE		50

#define STC_FILEDESC			(PACKETID_FTBASE+0)
#define CTS_FILESTATUS			(PACKETID_FTBASE+4)

// The most files the client will look at in one STC_FILEDESC packet.
#define MAX_FILEDESCS			5000


typedef void (*FTCFileFn)(FTClient *hClient, void *pFile, uint32 fileID);


// ----------------------------------------------------------------------- //
// Structures.
// ----------------------------------------------------------------------- //

struct FTClient
{
	// The current file we're transferring.
	void			*m_pCurFile;	// 0x00
	uint16			m_CurFileID;	// 0x04

	// All the function pointers.
	FTCInitStruct	m_Init;			// 0x08

	void			*m_pUserData1;	// 0x24
};

// Only used by the clienthack stuff.
// GLOBAL: LITHTECH 0x004e37b4
static FTClient *g_pFTClient;


// ----------------------------------------------------------------------- //
// Interface functions.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00436db0
FTClient* ftc_Init(FTCInitStruct *pStruct)
{
	FTClient *pClient;

	pClient = (FTClient*)dalloc(sizeof(FTClient));
	if(pClient)
	{
		memset(pClient, 0, sizeof(FTClient));
		memcpy(&pClient->m_Init, pStruct, sizeof(FTCInitStruct));
		g_pFTClient = pClient;
	}

	return pClient;
}


// FUNCTION: LITHTECH 0x00436df0
void ftc_Term(FTClient *pClient)
{
	if(!pClient)
		return;

	// Cancel the file being transferred (FTCInitStruct::m_Fn10 takes these in Talon).
	if(pClient->m_pCurFile)
		((FTCFileFn)pClient->m_Init.m_Fn10)(pClient, pClient->m_pCurFile, pClient->m_CurFileID);

	dfree(pClient);
	g_pFTClient = LTNULL;
}


// FUNCTION: LITHTECH 0x00436e30
void* ftc_GetUserData1(FTClient *pClient)
{
	if(!pClient)
		return LTNULL;

	return pClient->m_pUserData1;
}


// FUNCTION: LITHTECH 0x00436e40
void ftc_SetUserData1(FTClient *pClient, void *pUser)
{
	if(pClient)
		pClient->m_pUserData1 = pUser;
}


// STUB: LITHTECH 0x00436e50
// The original calls CPacket::ReadType<uint16>/<uint32> and WriteType<uint16> out of line
// (copies below); VC6 inlines them here, even with the inlines padded.
void ftc_ProcessPacket(FTClient *pClient, const CPacket_Read &cPacket_Read)
{
	CPacket *pPacket, *pResponse;
	uint32 i, fileID, fileSize;
	char *pFilename;
	int spaceLeft;

	if(!pClient)
		return;

	pPacket = (CPacket*)&cPacket_Read;
	if((pPacket->GetPacketID() & 0x3F) != STC_FILEDESC)
		return;

	pResponse = packet_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));

	for(i=0; i < MAX_FILEDESCS; i++)
	{
		if(!(pPacket->m_DataLen - pPacket->m_Pos))
			break;

		fileID = pPacket->ReadType((uint16*)0);
		fileSize = pPacket->ReadType((uint32*)0);
		pFilename = pPacket->ReadString();

		if(pClient->m_Init.m_NewFile(pClient, pFilename, fileSize, fileID) == NF_HAVEFILE)
		{
			fileID |= 0x8000;
		}

		// Send what we have if this one won't fit.
		spaceLeft = pResponse->m_MaxSize - pResponse->m_Pos - 7;
		if(spaceLeft < 0)
			spaceLeft = 0;

		if((uint32)spaceLeft < sizeof(uint32))
		{
			pResponse->m_Data[0] = CTS_FILESTATUS;
			pClient->m_Init.m_pNetMgr->SendPacket(pResponse, pClient->m_Init.m_ConnID, MESSAGE_GUARANTEED);
			pResponse->m_DataLen = pResponse->m_Pos = 1;
		}

		pResponse->WriteType((uint16)fileID);
	}

	if((pResponse->m_DataLen - 1) > 0)
	{
		pResponse->m_Data[0] = CTS_FILESTATUS;
		pClient->m_Init.m_pNetMgr->SendPacket(pResponse, pClient->m_Init.m_ConnID, MESSAGE_GUARANTEED);
	}

	pResponse->Release();
}


// FUNCTION: LITHTECH 0x00436f90
void clienthack_NewFile(uint16 fileID, uint32 fileSize, char *pFilename)
{
	if(g_pFTClient)
	{
		g_pFTClient->m_Init.m_NewFile(g_pFTClient, pFilename, fileSize, fileID);
	}
}


// Template code emitted into this object (the linker kept these copies). The original's
// ftc_ProcessPacket calls them; here they're referenced through this table so that VC6 emits them.
// FUNCTION: LITHTECH 0x00436fc0 ?ReadType@CPacket@@QAEGPAG@Z
// FUNCTION: LITHTECH 0x00437040 ?ReadType@CPacket@@QAEKPAK@Z
// FUNCTION: LITHTECH 0x004370c0 ?WriteType@CPacket@@QAEXG@Z
void ftc_EmitPacketTemplates(CPacket *pPacket)
{
	uint16 (CPacket::*pReadWord)(uint16*) = &CPacket::ReadType;
	uint32 (CPacket::*pReadDWord)(uint32*) = &CPacket::ReadType;
	void (CPacket::*pWriteWord)(uint16) = &CPacket::WriteType;

	(pPacket->*pReadWord)(0);
	(pPacket->*pReadDWord)(0);
	(pPacket->*pWriteWord)(0);
}
