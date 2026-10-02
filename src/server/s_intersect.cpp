// Jupiter runtime/server/src/s_intersect.cpp
// Talon reaches the server's world tree through g_pServerMgr and tells the shared intersect
// code it's the server.
#include "bdefs.h"
#include "servermgr.h"

class IntersectQuery;
struct IntersectInfo;

LTBOOL i_IntersectSegment(IntersectQuery *pQuery, IntersectInfo *pInfo, WorldTree *pWorldTree, LTBOOL bServer);


// FUNCTION: LITHTECH 0x00473eb0
LTBOOL ServerIntersectSegment(IntersectQuery *pQuery, IntersectInfo *pInfo)
{
	if (!g_pServerMgr)
		return LTFALSE;

	return i_IntersectSegment(pQuery, pInfo, &g_pServerMgr->m_World.m_WorldTree, LTTRUE);
}
