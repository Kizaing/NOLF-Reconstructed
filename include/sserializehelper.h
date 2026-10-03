// The server's save-game message helper (Talon, recovered from lithtech.exe; no Jupiter equivalent). Its
// methods live in sloaderthread.cpp (0x0048e9a0-0x0048eb60); servermgr.cpp creates it. Kept out of
// sloaderthread.h, which servermgr.h includes everywhere: the extra declarations there flip a fragile register
// choice in CLTServer::UnloadFile.
#ifndef __SSERIALIZEHELPER_H__
#define __SSERIALIZEHELPER_H__

#include <stdarg.h>
#include "lmessage.h"

class CServerMgr;
class ILTMessage;
class MainWorld;

// Reads and writes object references in save games (8 bytes, vtable 0x004c8694).
class CServerSerializeHelper : public LMessageHelper
{
public:
					CServerSerializeHelper(CServerMgr *pServerMgr);	// 0x0048e9a0

	// The message's slot 0x90 (LMessageImpl::IsInvalid) is LTTRUE for save games, which refer
	// to objects by m_SerializeID instead of m_ObjectID.
	virtual LTRESULT	ReadObjectRef(ILTMessage *pMsg, HOBJECT *pObj);		// 0x0048e9c0
	virtual LTRESULT	WriteObjectRef(ILTMessage *pMsg, HOBJECT hObj);		// 0x0048ea70
	virtual LTRESULT	WriteHStringArgList(ILTMessage *pMsg, int messageCode, va_list *pList);	// 0x0048ead0
	virtual LTRESULT	GetWorld(MainWorld **ppWorld);						// 0x0048eb40

	CServerMgr		*m_pServerMgr;		// 0x04
};

#endif  // __SSERIALIZEHELPER_H__
