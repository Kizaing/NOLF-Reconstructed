// Talon file transfer server (Jupiter runtime/shared/src/ftserv.h). You create a file transfer
// server for each client you want to send files to.
#ifndef __FTSERV_H__
#define __FTSERV_H__

#include "ltbasedefs.h"

class CNetMgr;
class CBaseConn;
class CPacket;
class ILTStream;


// File flags.
#define FFLAG_NEEDED		(1<<0)	// The client must have this file.
#define FFLAG_SENDWAIT		(1<<1)	// Just an optimization for when a client gets on,
									// doesn't send a separate packet for each file.
#define FFLAG_CLIENTWANTS	(1<<1)	// The client requested this file to be sent.
									// (intentionally the same as FFLAG_SENDWAIT)
#define FFLAG_TRANSFERRING	(1<<2)	// This file is currently being transferred.

// Todo numbers.
#define TODO_RETURN			0		// Just return and forget about the error.
#define TODO_REMOVEFILE		1		// Forget about the error file.

#define FTSFLAG_ONLYSENDNEEDED		(1<<0)	// Only send files marked as needed.
#define FTSFLAG_DONTSENDANYTHING	(1<<1)	// Don't try to send any files right now.
#define FTSFLAG_LOCAL				(1<<2)	// This is local (so use the clienthack_ stuff).


struct FTServ;


// Initialization structure for the file transfer server.
struct FTSInitStruct
{
	// Open and close functions.
	ILTStream* (*m_OpenFn)(FTServ *hServ, char *pFilename);		// 0x00
	void (*m_CloseFn)(FTServ *hServ, ILTStream *pStream);		// 0x04

	// Called when a file can't be opened.  Return a TODO number (defined above).
	int (*m_CantOpenFileFn)(FTServ *hServ, char *pFilename);	// 0x08

	CNetMgr		*m_pNetMgr;		// 0x0c
	CBaseConn	*m_ConnID;		// 0x10 Who we're talking to.
};


// Create and delete the file transfer server.
FTServ*	fts_Init(FTSInitStruct *pStruct, uint32 flags);
void	fts_Term(FTServ *hServ);

// User data..
void*	fts_GetUserData1(FTServ *hServ);
void	fts_SetUserData1(FTServ *hServ, void *pData);

// Ask how many of each type of file are waiting to be verified.
uint32	fts_GetNumNeededFiles(FTServ *hServ);
uint32	fts_GetNumTotalFiles(FTServ *hServ);

// Add a file that the client needs to verify.  Every file you have must have
// a unique ID for it.  **NOTE** it assumes pFilename is allocated so it just
// stores the pointer instead of using up more memory.
int		fts_AddFile(FTServ *hServ, char *pFilename, uint32 fileSize, uint32 fileID, uint16 flags);

// Send out all the info for files with FFLAG_SENDWAIT.
void	fts_FlushAddedFiles(FTServ *hServ);

// Clear the file list.  Does NOT stop the current file transfer.
void	fts_ClearFiles(FTServ *hServ);

// Stops the current file transfer if one is happening.
void	fts_StopTransfer(FTServ *hServ);

// Call this when a packet comes in from the client.
LTBOOL	fts_ProcessPacket(FTServ *hServ, CPacket *pPacket);

// Call as often as possible.
void	fts_Update(FTServ *hServ, float timeDelta);

#endif  // __FTSERV_H__
