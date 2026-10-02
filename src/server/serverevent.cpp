// Jupiter runtime/server/src/serverevent.cpp
#include "serverevent.h"

struct ClientStructNode;


// FUNCTION: LITHTECH 0x00482140
void CServerEvent::DecrementRefCount()
{
	LTLink *pCur, *pNext;
	ClientStructNode *pNode;

	--m_RefCount;
	if( m_RefCount == 0 )
	{
		pCur = m_ClientStructNodeList.m_Head.m_pNext;
		while( pCur != &m_ClientStructNodeList.m_Head )
		{
			pNext = pCur->m_pNext;
			pNode = ( ClientStructNode * )pCur->m_pData;
			sb_Free( &g_pServerMgr->m_ClientStructNodeBank, pNode );
			pCur = pNext;
		}

		dl_InitList( &m_ClientStructNodeList );
		sb_Free(&g_pServerMgr->m_ServerEventBank, this);
	}
}
