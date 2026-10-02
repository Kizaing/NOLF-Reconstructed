// Server loader thread (Talon, recovered from lithtech.exe; Jupiter server/src/sloaderthread.h).
#ifndef __SLOADERTHREAD_H__
#define __SLOADERTHREAD_H__

#include "lthread.h"

class CServerMgr;

// Loads files for the server in the background (0x64 bytes, embedded in CServerMgr at 0xed4).
class CServerLoaderThread : public LThread
{
public:
	LTBOOL			Init(CServerMgr *pServerMgr);	// 0x0048eb80

	CServerMgr		*m_pServerMgr;		// 0x60
};

// Reads and writes object references in save games (8 bytes, vtable 0x004c8694).
class CServerSerializeHelper
{
public:
					CServerSerializeHelper(CServerMgr *pServerMgr);	// 0x0048e9a0

	virtual LTRESULT	ReadObjectRef(ILTStream *pStream, HOBJECT *pObj);	// 0x0048e9c0
	virtual LTRESULT	WriteObjectRef(ILTStream *pStream, HOBJECT hObj);	// 0x0048ea70

	CServerMgr		*m_pServerMgr;		// 0x04
};

#endif  // __SLOADERTHREAD_H__
