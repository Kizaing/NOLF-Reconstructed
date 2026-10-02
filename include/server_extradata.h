// Jupiter runtime/server/src/server_extradata.h: per-type object data (models, sprites,
// world models) and Talon's server model cache.
#ifndef __SERVER_EXTRADATA_H__
#define __SERVER_EXTRADATA_H__

#include "ltbasedefs.h"

class CServerMgr;
class Model;
struct UsedFile;

LTRESULT	se_LoadModel(CServerMgr *pServerMgr, const char *pFilename, UsedFile *pFile, Model **ppModel);	// 0x004784c0
LTRESULT	se_LoadChildModels(CServerMgr *pServerMgr, Model *pModel, UsedFile *pFile, uint32 flags);	// 0x00478780
LTRESULT	se_GetModel(CServerMgr *pServerMgr, char *pFilename, Model **ppModel, UsedFile **ppFile,
				LTBOOL bAddRef, uint32 flags);			// 0x004788c0
LTRESULT	se_UncacheModel(CServerMgr *pServerMgr, const char *pFilename, UsedFile *pFile);	// 0x00478a20

#endif  // __SERVER_EXTRADATA_H__
