// Server loader thread (Talon, recovered from lithtech.exe; Jupiter server/src/sloaderthread.h).
#ifndef __SLOADERTHREAD_H__
#define __SLOADERTHREAD_H__

#include <stdarg.h>
#include "lthread.h"

class CServerMgr;

// Loads files for the server in the background (0x64 bytes, embedded in CServerMgr at 0xed4).
class CServerLoaderThread : public LThread
{
public:
					CServerLoaderThread();			// 0x0048eb60

	LTBOOL			Init(CServerMgr *pServerMgr);	// 0x0048eb80

	virtual void	ProcessMessage(LThreadMessage &msg);	// 0x0048eb90

	CServerMgr		*m_pServerMgr;		// 0x60
};

#endif  // __SLOADERTHREAD_H__
