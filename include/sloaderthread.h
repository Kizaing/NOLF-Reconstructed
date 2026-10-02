// Server loader thread (Talon, recovered from lithtech.exe; Jupiter server/src/sloaderthread.h).
#ifndef __SLOADERTHREAD_H__
#define __SLOADERTHREAD_H__

#include <stdarg.h>
#include "lthread.h"
#include "lmessage.h"

class CServerMgr;
class ILTMessage;
class MainWorld;

// Loads files for the server in the background (0x64 bytes, embedded in CServerMgr at 0xed4).
class CServerLoaderThread : public LThread
{
public:
					CServerLoaderThread();			// 0x0048eb60

	LTBOOL			Init(CServerMgr *pServerMgr);	// 0x0048eb80

	virtual void	ProcessMessage(LThreadMessage &msg);	// 0x0048eb90

	CServerMgr		*m_pServerMgr;		// 0x60
};

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

#endif  // __SLOADERTHREAD_H__
