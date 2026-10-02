// Talon client file manager (Jupiter runtime/client/src/client_filemgr.h).
// Talon has no IClientFileMgr interface: the cf_ functions take the ClientFileMgr explicitly
// (CClientMgr::m_hFileMgr).
#ifndef __CLIENT_FILEMGR_H__
#define __CLIENT_FILEMGR_H__

#include "ltbasedefs.h"
#include "de_file.h"

class CClientMgr;
struct FTClient;
class CPacket_Read;
class CBaseConn;

// FileRef, FILE_ANYFILE and FILE_CLIENTFILE live in sprite.h (wave 1).
#include "sprite.h"

// FileRef::m_FileType.
#define FILE_SERVERFILE	2

#define TYPECODE_UNKNOWN	0xFF

// 0x20 bytes.
struct FileIdentifier
{
	void		*m_pData;		// 0x00
	LTLink		m_Link;			// 0x04
	HLTFileTree	*m_hFileTree;	// 0x10
	uint16		m_FileID;		// 0x14 Server file ID (if this file comes from the server).
	uint16		m_NameLen;		// 0x16 strlen(m_Filename)
	uint8		m_TypeCode;		// 0x18
	uint8		m_Flags;		// 0x19
	char		*m_Filename;	// 0x1c Uppercase filename.
};

struct ClientFileMgr;

ClientFileMgr*	cf_Init(CClientMgr *pClientMgr);												// 0x00403f50
void			cf_Term(ClientFileMgr *hFileMgr);												// 0x00404060
void			cf_ProcessPacket(ClientFileMgr *hFileMgr, const CPacket_Read &cPacket);			// 0x00404140
void			cf_OnConnect(ClientFileMgr *hFileMgr, CBaseConn *serverID);							// 0x00404160
void			cf_OnDisconnect(ClientFileMgr *hFileMgr);										// 0x004043e0
void			cf_AddResourceTrees(ClientFileMgr *hFileMgr, const char **pTreeNames, int nTrees,
					TreeType *pTreeTypes, int *nTreesLoaded);									// 0x004044b0
const char*		cf_GetFilename(ClientFileMgr *hFileMgr, FileRef *pFileRef);						// 0x00404540
FileEntry*		cf_GetFileList(ClientFileMgr *hFileMgr, const char *pDirName);					// 0x00404580
FileIdentifier*	cf_GetFileIdentifier(ClientFileMgr *hFileMgr, FileRef *pRef, uint8 typeCode);		// 0x004045e0
ILTStream*		cf_OpenFileIdent(ClientFileMgr *hFileMgr, FileIdentifier *pIdent);				// 0x004047f0
ILTStream*		cf_OpenFile(ClientFileMgr *hFileMgr, FileRef *pRef);							// 0x00404820
LTRESULT		cf_CopyFile(ClientFileMgr *hFileMgr, const char *pSrcFilename, const char *pDestFilename);	// 0x004048b0

#endif  // __CLIENT_FILEMGR_H__
