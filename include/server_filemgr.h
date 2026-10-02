// Server file manager (Talon layout recovered from lithtech.exe; Jupiter server/src/server_filemgr.h).
// Talon's is a plain struct embedded in CServerMgr (at 0xe3c) and driven by sf_ functions.
#ifndef __SERVER_FILEMGR_H__
#define __SERVER_FILEMGR_H__

#include "ltbasedefs.h"
#include "de_file.h"
#include "../../build/proj/LT2/lithshared/stdlith/object_bank.h"

#include "dhashtable.h"

class CServerMgr;

#define MAX_FILETREES_TO_SEARCH		30

// One opened resource tree (dalloc'd, 0x10 bytes).
struct ResTree
{
	LTLink			m_Link;			// 0x00 in ServerFileMgr::m_ResTrees (m_pData = this)
	HLTFileTree		*m_hFileTree;	// 0x0c
};

// A file the server has used (0x14 bytes).
struct UsedFile
{
	char*			GetFilename()	{ return (char*)hs_GetElementKey(m_hElement, LTNULL); }

	HHashElement	*m_hElement;	// 0x00 holds the file-table entry for filename
	uint32			m_FileSize;		// 0x04
	uint32			m_FileID;		// 0x08
	short			m_Flags;		// 0x0c
	ResTree			*m_pTree;		// 0x10
};

// 0x3c bytes.
struct ServerFileMgr
{
	LTLink			m_ResTrees;			// 0x00 ResTrees, in AddResources order
	uint32			m_CurrentFileID;	// 0x0c
	HHashTable		*m_hFileTable;		// 0x10 used files by name
	CServerMgr		*m_pServerMgr;		// 0x14
	ObjectBank<UsedFile>	m_UsedFileBank;	// 0x18
};

void		sf_Init(ServerFileMgr *pMgr, CServerMgr *pServerMgr);
void		sf_Term(ServerFileMgr *pMgr);
void		sf_AddResources(ServerFileMgr *pMgr, const char **pTrees, int nTrees,
	TreeType *pTreeTypes, int *pnTreesLoaded);
ILTStream*	sf_OpenFile(ServerFileMgr *pMgr, const char *pFilename);
ILTStream*	sf_OpenFile2(ServerFileMgr *pMgr, const char *pFilename, int bAddUsedFile, short flags);
ILTStream*	sf_OpenFile3(ServerFileMgr *pMgr, UsedFile *pUsedFile);
LTRESULT	sf_CopyFile(ServerFileMgr *pMgr, const char *pSrc, const char *pDest);
LTBOOL		sf_DoesFileExist(ServerFileMgr *pMgr, const char *pFilename, ResTree **ppTree, uint32 *pFileSize);
FileEntry*	sf_GetFileList(ServerFileMgr *pMgr, const char *pDirName);
int			sf_AddUsedFile(ServerFileMgr *pMgr, const char *pFilename, short flags, UsedFile **ppFile);
void		sf_ClearUsedFiles(ServerFileMgr *pMgr);
char*		sf_GetUsedFilename(ServerFileMgr *pMgr, UsedFile *pFile);

#endif  // __SERVER_FILEMGR_H__
