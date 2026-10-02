// Jupiter runtime/shared/src/ftserv.cpp
// Talon's file transfer server works on reference-counted CPackets, and a local server tells
// the local client about files directly through the clienthack path in ftclient.cpp.
#include <string.h>
#include "bdefs.h"
#include "ftserv.h"
#include "iltstream.h"
#include "packet.h"
#include "netmgr.h"
#include "../../build/proj/LT2/lithshared/stdlith/stringholder.h"
#include "../../build/proj/LT2/lithshared/stdlith/object_bank.h"


// ----------------------------------------------------------------------- //
// Defines (Jupiter ftbase.h).
// ----------------------------------------------------------------------- //

// How many file transfer blocks get sent before we wait for an ack packet?
#define NUM_UNVERIFIED_BLOCKS	6

#define PACKETID_FTBASE		50

#define STC_FILEDESC			(PACKETID_FTBASE+0)
#define STC_STARTTRANSFER		(PACKETID_FTBASE+1)
#define STC_CANCELFILETRANSFER	(PACKETID_FTBASE+2)
#define STC_FILEBLOCK			(PACKETID_FTBASE+3)
#define CTS_FILESTATUS			(PACKETID_FTBASE+4)
#define CTS_DATARECEIVED		(PACKETID_FTBASE+6)

// Transfer server states.
#define FTSTATE_NONE			0
#define FTSTATE_TRANSFERRING	1


// ftclient.cpp: tells the local client about a file (used with FTSFLAG_LOCAL).
void clienthack_NewFile(uint16 fileID, uint32 fileSize, char *pFilename);


// ----------------------------------------------------------------------- //
// Structures.
// ----------------------------------------------------------------------- //

// File info structure.
struct FTFile
{
	LTLink	m_Link;			// 0x00
	uint32	m_FileID;		// 0x0c
	uint32	m_FileSize;		// 0x10
	char	*m_Filename;	// 0x14
	uint16	m_Flags;		// 0x18
};


// The file transfer server.
struct FTServ
{
	CStringHolder		m_Strings;		// 0x00
	ObjectBank<FTFile>	m_FTFileBank;	// 0x18

	// All the files.  This list will be removed from as the
	// files are successfully transfered.
	LTLink		m_Files;				// 0x3c

	// How many files are there that the guy needs to have?
	int			m_nNeededFiles;			// 0x48

	// How many total files are there?
	int			m_nTotalFiles;			// 0x4c

	// Current state (states defined in ftserv.cpp).
	int			m_State;				// 0x50

	// Flags for how we're operating.
	uint32		m_ServerFlags;			// 0x54

	// The maximum transfer rate.
	float		m_BytesPerSecond;		// 0x58

	// Time delta since the last packet was sent.
	float		m_TimeDelta;			// 0x5c

	// The init structure is just copied over into here.
	FTSInitStruct	m_InitStruct;		// 0x60

	void		*m_UserData1;			// 0x74


	// Info about the current file transfer.
	ILTStream	*m_pCurFileStream;		// 0x78
	FTFile		*m_pCurFile;			// 0x7c
	uint32		m_nUnverifiedBlocks;	// 0x80
	uint32		m_nBytesLeft;			// 0x84
};


// Takes a reference on a packet from packet_Get.
inline CPacket* fts_AddRef(const CPacketRef &cPacketRef)
{
	CPacket *pPacket = cPacketRef.m_pPacket;

	if(pPacket)
		pPacket->AddRef();

	return pPacket;
}


// ----------------------------------------------------------------------- //
// Internal helpers.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x004378f0
static FTFile* fts_FindFileToSend(FTServ *pServ)
{
	LTLink *pCur;
	FTFile *pFile;

	for(pCur=pServ->m_Files.m_pNext; pCur != &pServ->m_Files; pCur=pCur->m_pNext)
	{
		pFile = (FTFile*)pCur->m_pData;

		if( (pFile->m_Flags & FFLAG_CLIENTWANTS) )
		{
			return pFile;
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x004377e0
static FTFile* fts_FindFileByID(FTServ *pServ, uint16 fileID)
{
	LTLink *pCur;
	FTFile *pFile;

	for(pCur=pServ->m_Files.m_pNext; pCur != &pServ->m_Files; pCur=pCur->m_pNext)
	{
		pFile = (FTFile*)pCur->m_pData;
		if(pFile->m_FileID == fileID)
			return pFile;
	}

	return LTNULL;
}


// Remove the given file (and decrement the appropriate counts).
// FUNCTION: LITHTECH 0x00437600
static void fts_RemoveFile(FTServ *pServ, FTFile *pFile)
{
	--pServ->m_nTotalFiles;
	if(pFile->m_Flags & FFLAG_NEEDED)
		--pServ->m_nNeededFiles;

	dl_Remove(&pFile->m_Link);
	pServ->m_FTFileBank.Free(pFile);
}


// FUNCTION: LITHTECH 0x00437920
static void fts_MaybeSendDataBlock(FTServ *pServ)
{
	uint32 sendSize = MAX_PACKET_LEN - 40;

	if(pServ->m_nUnverifiedBlocks == NUM_UNVERIFIED_BLOCKS)
	{
		// Ok, wait for an ack packet before sending more.
		return;
	}

	CPacketRef cPacket;
	cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	// Are we ready to send off another data block?
	float bytesPerSecond = (float)sendSize / pServ->m_TimeDelta;
	if(bytesPerSecond <= pServ->m_BytesPerSecond)
	{
		uint8 tempData[MAX_PACKET_LEN];
		LTBOOL bDone = FALSE;

		// Ok, send out a packet!
		cPacket->m_Data[0] = STC_FILEBLOCK;

		if(sendSize >= pServ->m_nBytesLeft)
		{
			bDone = TRUE;
			sendSize = pServ->m_nBytesLeft;
		}

		// Send the block.
		pServ->m_pCurFileStream->Read(tempData, sendSize);
		cPacket->WriteRaw(tempData, (uint16)sendSize);
		pServ->m_InitStruct.m_pNetMgr->SendPacket(cPacket, pServ->m_InitStruct.m_ConnID, MESSAGE_GUARANTEED);

		// If this file transfer is done, then cleanup.
		if(bDone)
		{
			fts_RemoveFile(pServ, pServ->m_pCurFile);
			pServ->m_pCurFile = LTNULL;

			pServ->m_InitStruct.m_CloseFn(pServ, pServ->m_pCurFileStream);
			pServ->m_pCurFileStream = LTNULL;
		}

		++pServ->m_nUnverifiedBlocks;
		pServ->m_TimeDelta = 0;
	}
}


inline int fts_FileDescLen(FTFile *pFile)
{
	return sizeof(uint16) + sizeof(uint32) + strlen(pFile->m_Filename);
}


inline void fts_WriteFileDesc(FTFile *pFile, CPacket *pPacket)
{
	pPacket->WriteType((uint16)pFile->m_FileID);
	pPacket->WriteType(pFile->m_FileSize);
	pPacket->WriteString(pFile->m_Filename);
}


inline void fts_WriteFileDescPacket(FTFile *pFile, const CPacketRef &cPacket)
{
	cPacket->m_Data[0] = STC_FILEDESC;
	fts_WriteFileDesc(pFile, cPacket);
}


// ----------------------------------------------------------------------- //
// Interface functions.
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00437220
FTServ *fts_Init(FTSInitStruct *pStruct, uint32 flags)
{
	FTServ *pRet;

	pRet = new FTServ;

	pRet->m_nNeededFiles = 0;
	pRet->m_nTotalFiles = 0;
	pRet->m_State = 0;
	pRet->m_ServerFlags = flags;
	pRet->m_TimeDelta = 0.0f;
	pRet->m_UserData1 = LTNULL;
	pRet->m_pCurFileStream = LTNULL;
	pRet->m_pCurFile = LTNULL;
	pRet->m_nUnverifiedBlocks = 0;
	pRet->m_nBytesLeft = 0;

	pRet->m_Strings.SetAllocSize(4096);
	pRet->m_FTFileBank.Init(128, 128);

	memcpy(&pRet->m_InitStruct, pStruct, sizeof(FTSInitStruct));
	dl_TieOff(&pRet->m_Files);
	pRet->m_State = FTSTATE_NONE;
	pRet->m_BytesPerSecond = 100000.0f; // Basically, send as fast as possible.

	return pRet;
}


// FUNCTION: LITHTECH 0x004372e0
void fts_Term(FTServ *pServ)
{
	fts_ClearFiles(pServ);
	fts_StopTransfer(pServ);
	delete pServ;
}


// FUNCTION: LITHTECH 0x00437330
void* fts_GetUserData1(FTServ *pServ)
{
	if(pServ)
		return pServ->m_UserData1;
	else
		return LTNULL;
}


// FUNCTION: LITHTECH 0x00437340
void fts_SetUserData1(FTServ *pServ, void *pUser)
{
	if(pServ)
		pServ->m_UserData1 = pUser;
}


// FUNCTION: LITHTECH 0x00437350
uint32 fts_GetNumNeededFiles(FTServ *pServ)
{
	if(pServ)
		return pServ->m_nNeededFiles;
	else
		return 0;
}


// FUNCTION: LITHTECH 0x00437360
uint32 fts_GetNumTotalFiles(FTServ *pServ)
{
	if(pServ)
		return pServ->m_nTotalFiles;
	else
		return 0;
}


// FUNCTION: LITHTECH 0x00437370
int fts_AddFile(FTServ *pServ, char *pFilename, uint32 fileSize, uint32 fileID, uint16 flags)
{
	FTFile *pFile;
	CPacketRef cPacket;

	if(!pServ)
		return 0;

	pFile = pServ->m_FTFileBank.Allocate();
	pFile->m_Link.m_pData = pFile;
	pFile->m_Flags = flags;
	pFile->m_FileID = fileID;
	pFile->m_FileSize = fileSize;
	pFile->m_Filename = pFilename;

	if(flags & FFLAG_NEEDED)
	{
		// Needed files go first in the list.
		dl_Insert(&pServ->m_Files, &pFile->m_Link);
		++pServ->m_nNeededFiles;
	}
	else
	{
		dl_Insert(pServ->m_Files.m_pPrev, &pFile->m_Link);
	}

	// Tell the client about this file.
	if(!(flags & FFLAG_SENDWAIT))
	{
		cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);
		fts_WriteFileDescPacket(pFile, cPacket);
		pServ->m_InitStruct.m_pNetMgr->SendPacket(cPacket, pServ->m_InitStruct.m_ConnID, MESSAGE_GUARANTEED);
	}

	if(pServ->m_ServerFlags & FTSFLAG_LOCAL)
	{
		clienthack_NewFile((uint16)fileID, fileSize, pFilename);
	}

	++pServ->m_nTotalFiles;

	return 1;
}


// FUNCTION: LITHTECH 0x004374a0
void fts_FlushAddedFiles(FTServ *pServ)
{
	CPacketRef cPacket;
	LTLink *pCur;
	FTFile *pFile;
	int spaceLeft;

	if(!pServ)
		return;

	cPacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	for(pCur = pServ->m_Files.m_pNext; pCur != &pServ->m_Files; pCur = pCur->m_pNext)
	{
		pFile = (FTFile*)pCur->m_pData;
		if(pFile->m_Flags & FFLAG_SENDWAIT)
		{
			// Send what we have if this one won't fit.
			spaceLeft = cPacket->m_MaxSize - cPacket->m_Pos - 7;
			if(spaceLeft < 0)
				spaceLeft = 0;

			if(fts_FileDescLen(pFile) >= spaceLeft)
			{
				pServ->m_InitStruct.m_pNetMgr->SendPacket(cPacket, pServ->m_InitStruct.m_ConnID, MESSAGE_GUARANTEED);
				cPacket->m_DataLen = cPacket->m_Pos = 1;
			}

			fts_WriteFileDescPacket(pFile, cPacket);
			pFile->m_Flags &= ~FFLAG_SENDWAIT;
		}
	}

	// Flush it out..
	if((cPacket->m_DataLen - 1) > 0)
	{
		pServ->m_InitStruct.m_pNetMgr->SendPacket(cPacket, pServ->m_InitStruct.m_ConnID, MESSAGE_GUARANTEED);
	}
}


// FUNCTION: LITHTECH 0x004375c0
void fts_ClearFiles(FTServ *pServ)
{
	LTLink *pCur, *pNext;
	FTFile *pFile;

	pCur = pServ->m_Files.m_pNext;
	while(pCur != &pServ->m_Files)
	{
		pNext = pCur->m_pNext;

		// Free it if we're not transferring it.
		pFile = (FTFile*)pCur->m_pData;
		if(!(pFile->m_Flags & FFLAG_TRANSFERRING))
		{
			fts_RemoveFile(pServ, pFile);
		}

		pCur = pNext;
	}
}


// FUNCTION: LITHTECH 0x00437640
void fts_StopTransfer(FTServ *pServ)
{
	CPacket *pPacket;

	if(!pServ || (pServ->m_State != FTSTATE_TRANSFERRING))
		return;

	pPacket = fts_AddRef(packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN));
	pPacket->m_Data[0] = STC_CANCELFILETRANSFER;
	pServ->m_InitStruct.m_pNetMgr->SendPacket(pPacket, pServ->m_InitStruct.m_ConnID, MESSAGE_GUARANTEED);
	pServ->m_InitStruct.m_CloseFn(pServ, pServ->m_pCurFileStream);
	pServ->m_pCurFile->m_Flags &= ~FFLAG_TRANSFERRING;
	pServ->m_pCurFile = LTNULL;
	pServ->m_pCurFileStream = LTNULL;
	pServ->m_State = FTSTATE_NONE;
	pPacket->Release();
}


// FUNCTION: LITHTECH 0x004376d0
LTBOOL fts_ProcessPacket(FTServ *pServ, CPacket *pPacket)
{
	uint8 packetID;
	uint16 fileID;
	FTFile *pFile;

	packetID = pPacket->m_Data[0] & 0x3F;
	if(packetID == CTS_FILESTATUS)
	{
		while((pPacket->m_DataLen - pPacket->m_Pos) > 0)
		{
			fileID = pPacket->ReadType((uint16*)0);

			pFile = fts_FindFileByID(pServ, fileID & 0x7FFF);
			if(pFile)
			{
				if(fileID & 0x8000)
					fts_RemoveFile(pServ, pFile);
				else
					pFile->m_Flags |= FFLAG_CLIENTWANTS;
			}
		}

		return TRUE;
	}
	else if(packetID == CTS_DATARECEIVED)
	{
		if(pServ->m_State == FTSTATE_TRANSFERRING)
			pServ->m_nUnverifiedBlocks = 0;

		return TRUE;
	}

	return FALSE;
}


// FUNCTION: LITHTECH 0x00437810
void fts_Update(FTServ *pServ, float timeDelta)
{
	FTFile *pFile;
	ILTStream *pStream;
	int todo;


	if(!pServ)
		return;

	// What's our state?
	if(pServ->m_State == FTSTATE_NONE)
	{
		// Do we have any files that need to be sent?
		pFile = fts_FindFileToSend(pServ);
		if(pFile)
		{
			pServ->m_TimeDelta = 0.0f;

			// If they don't want us to send files at all right now, don't,
			if(pServ->m_ServerFlags & FTSFLAG_DONTSENDANYTHING)
				return;

			// If it only wants us to send needed files, then don't do anything if the
			// file isn't needed.
			if(pServ->m_ServerFlags & FTSFLAG_ONLYSENDNEEDED)
			{
				if(!(pFile->m_Flags & FFLAG_NEEDED))
				{
					return;
				}
			}

			pStream = pServ->m_InitStruct.m_OpenFn(pServ, pFile->m_Filename);
			if(pStream)
			{
				// Cooooool, start the transfer.
				pFile->m_Flags |= FFLAG_TRANSFERRING;
				pServ->m_pCurFile = pFile;
				pServ->m_pCurFileStream = pStream;
				pServ->m_State = FTSTATE_TRANSFERRING;
				pServ->m_nUnverifiedBlocks = 0;
				pServ->m_nBytesLeft = pFile->m_FileSize;
			}
			else
			{
				todo = pServ->m_InitStruct.m_CantOpenFileFn(pServ, pFile->m_Filename);
				if(todo == TODO_REMOVEFILE)
				{
					dl_Remove(&pFile->m_Link);
					pServ->m_FTFileBank.Free(pFile);
				}
				else if(todo == TODO_RETURN)
				{
					return;
				}
			}
		}
	}
	else if(pServ->m_State == FTSTATE_TRANSFERRING)
	{
		pServ->m_TimeDelta += timeDelta;

		fts_MaybeSendDataBlock(pServ);
	}
}


// Template and ObjectBank code emitted into this object.
// (CPacket::WriteType<uint16> at 0x004370c0 is ftclient.obj's copy.)
// FUNCTION: LITHTECH 0x00437a50 ?WriteTypeImpl@CPacket@@QAEXK@Z
// FUNCTION: LITHTECH 0x00437a20 ?AllocVoid@?$ObjectBank@UFTFile@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00437bb0 ?Term@?$ObjectBank@UFTFile@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00437bd0 ??_G?$ObjectBank@UFTFile@@VNullCS@@@@UAEPAXI@Z
