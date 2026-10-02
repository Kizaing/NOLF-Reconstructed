// Jupiter runtime/server/src/game_serialize.h: save/load LT objects.
// Talon passes the server manager explicitly.
#ifndef __GAME_SERIALIZE_H__
#define __GAME_SERIALIZE_H__

#include "servermgr.h"

class ILTStream;

void		sm_SaveObjects(CServerMgr *pServerMgr, ILTStream *pStream, ObjectList *pList, uint32 dwParam,
				uint32 flags);											// 0x00438fb0
LTRESULT	sm_RestoreObjects(CServerMgr *pServerMgr, ILTStream *pStream, uint32 dwParam, uint32 flags);	// 0x00439a50

#endif  // __GAME_SERIALIZE_H__
