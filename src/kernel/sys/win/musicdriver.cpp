// Jupiter runtime/kernel/src/sys/win/musicdriver.cpp
// The unit ends at 004627c0: the static initializer there belongs to netmgr.
#include <windows.h>
#include <stdio.h>
#include "bdefs.h"
#include "console.h"
#include "musicdriver.h"


// GLOBAL: LITHTECH 0x004e45a8
static HINSTANCE g_hMusicDLL = 0;
// GLOBAL: LITHTECH 0x004e45ac
static SMusicMgr *g_pMusicMgr = LTNULL;


// FUNCTION: LITHTECH 0x00462690
void music_ConsolePrint( char *pMsg, ... )
{
	va_list		marker;
	char		str[500];

	va_start(marker, pMsg);
	_vsnprintf(str, sizeof(str)-1, pMsg, marker);
	va_end(marker);

	con_PrintString(CONRGB(100,255,100), 0, str);
}

// FUNCTION: LITHTECH 0x004626d0
musicdriver_status music_InitDriver( char *pMusicDLLName, SMusicMgr *pMusicMgr )
{
	MusicDLLSetupFn pSetupFn;

	if( !pMusicMgr || !pMusicDLLName )
		return MUSICDRIVER_INVALIDOPTIONS;

	music_TermDriver( );
	pMusicMgr->m_bValid = 0;

	g_hMusicDLL = LoadLibrary( pMusicDLLName );
	if( !g_hMusicDLL )
	{
		DWORD error;
		error = GetLastError( );
		return MUSICDRIVER_CANTLOADLIBRARY;
	}

	// Have the driver setup all the function pointers.
	pSetupFn = (MusicDLLSetupFn)GetProcAddress(g_hMusicDLL, "MusicDLLSetup");
	if(!pSetupFn)
	{
		FreeLibrary(g_hMusicDLL);
		g_hMusicDLL = LTNULL;
		return MUSICDRIVER_INVALIDDLL;
	}

	pSetupFn( pMusicMgr );
	pMusicMgr->ConsolePrint = music_ConsolePrint;

	if( !pMusicMgr->Init( pMusicMgr ))
	{
		FreeLibrary(g_hMusicDLL);
		g_hMusicDLL = LTNULL;
		return MUSICDRIVER_INVALIDOPTIONS;
	}

	g_pMusicMgr = pMusicMgr;

// {BP 1/2/98}  Commented this out becuase this is supposed to be in DLL
//	pMusicMgr->m_bValid = 1;

	return MUSICDRIVER_OK;
}


// FUNCTION: LITHTECH 0x00462780
void music_TermDriver()
{
	if( g_pMusicMgr )
	{
		g_pMusicMgr->Term( );
		g_pMusicMgr->m_bValid = 0;
		g_pMusicMgr = LTNULL;
	}

	if( g_hMusicDLL )
	{
		FreeLibrary( g_hMusicDLL );
		g_hMusicDLL = LTNULL;
	}
}
