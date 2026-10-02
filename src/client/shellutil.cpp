// Jupiter runtime/client/src/shellutil.cpp
// FLAGS: /O2 /GX-
#include "bdefs.h"
#include "clientshell.h"

LTObject* cm_FindObject(CClientMgr *pClientMgr, uint16 objectID);		// 0x004265e0


// FUNCTION: LITHTECH 0x0048e980
LTObject* CClientShell::GetClientObject()
{
	if(m_ClientObjectID == (uint16)-1)
		return LTNULL;
	else
		return cm_FindObject(m_pClientMgr, m_ClientObjectID);
}
