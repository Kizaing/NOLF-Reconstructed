// Music bindings module. Does the system-dependent initialization of a music driver
// (Jupiter runtime/kernel/src/sys/win/musicdriver.h).
#ifndef __MUSICDRIVER_H__
#define __MUSICDRIVER_H__

#include "musicmgr.h"

// Music status codes...
enum musicdriver_status
{
	MUSICDRIVER_OK = 0,
	MUSICDRIVER_CANTLOADLIBRARY,
	MUSICDRIVER_INVALIDDLL,
	MUSICDRIVER_INVALIDOPTIONS
};

musicdriver_status music_InitDriver( char *pMusicDLLName, SMusicMgr *pMusicMgr );
void music_TermDriver();

#endif  // __MUSICDRIVER_H__
