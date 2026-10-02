// Talon file transfer client (Jupiter runtime/shared/src/ftclient.h). Talon's init structure
// carries the client's callbacks.
#ifndef __FTCLIENT_H__
#define __FTCLIENT_H__

#include "ltbasedefs.h"

class CNetMgr;
class CBaseConn;
class CPacket_Read;
struct FTClient;

// Return values for the new-file callback.
#define NF_HAVEFILE		0

typedef int (*FTCNewFileFn)(FTClient *hClient, const char *pFilename, uint32 size, uint32 fileID);
typedef int (*FTCIntFn)(FTClient *hClient);
typedef void (*FTCVoidFn)();

struct FTCInitStruct
{
	FTCNewFileFn	m_NewFile;		// 0x00
	FTCIntFn		m_Fn04;			// 0x04 (callbacks 0x04-0x10: names unknown)
	FTCVoidFn		m_Fn08;			// 0x08
	FTCVoidFn		m_Fn0C;			// 0x0c
	FTCVoidFn		m_Fn10;			// 0x10
	CNetMgr			*m_pNetMgr;		// 0x14
	CBaseConn		*m_ConnID;		// 0x18 Who we're talking to.
};

FTClient*	ftc_Init(FTCInitStruct *pStruct);							// 0x00436db0
void		ftc_Term(FTClient *hClient);								// 0x00436df0
void*		ftc_GetUserData1(FTClient *hClient);						// 0x00436e30
void		ftc_SetUserData1(FTClient *hClient, void *pUser);			// 0x00436e40
void		ftc_ProcessPacket(FTClient *hClient, const CPacket_Read &cPacket);	// 0x00436e50

#endif  // __FTCLIENT_H__
