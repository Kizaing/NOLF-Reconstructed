// The main system-dependent engine functions (Jupiter runtime/kernel/src/sys/win/dsys_interface.h).
#ifndef __DSYS_INTERFACE_H__
#define __DSYS_INTERFACE_H__

#include <windows.h>
#include <setjmp.h>
#include "ltbasedefs.h"
#include "version_info.h"

#define MAX_KEYBUFFER		100

class CClientMgr;
class CClassMgr;
class CServerMgr;

// Client globals. Talon has m_pClientMgr at 0x54 (Jupiter: g_pClientMgr only).
class ClientGlob
{
public:
	BOOL			m_bProcessWindowMessages;	// 0x00
	jmp_buf			m_MemoryJmp;				// 0x04
	HWND			m_hMainWnd;					// 0x44

	HINSTANCE		m_hInstance;				// 0x48

	char			*m_WndClassName;			// 0x4c
	const char		*m_WndCaption;				// 0x50

	CClientMgr		*m_pClientMgr;				// 0x54
	BOOL			m_bInitializingRenderer;	// 0x58
	BOOL			m_bBreakOnError;			// 0x5c Break in dsi_OnReturnError?
	BOOL			m_bClientActive;			// 0x60
	BOOL			m_bLostFocus;				// 0x64
	BOOL			m_bAppClosing;				// 0x68
	BOOL			m_bDialogUp;				// 0x6c
	BOOL			m_bRendererShutdown;		// 0x70 They called ShutdownRender so we shouldn't
												// reinitialize the renderer.

	BOOL			m_bHost;					// 0x74
	char			*m_pGameResources;			// 0x78

	const char		*m_pWorldName;				// 0x7c
	char			m_CachePath[500];			// 0x80

	DWORD			m_KeyDowns[MAX_KEYBUFFER];		// 0x274
	DWORD			m_KeyUps[MAX_KEYBUFFER];		// 0x404
	BOOL			m_KeyDownReps[MAX_KEYBUFFER];	// 0x594

	WORD			m_nKeyDowns;				// 0x724
	WORD			m_nKeyUps;					// 0x726

	BOOL			m_bIsConsoleUp;				// 0x728

	BOOL			m_bInputEnabled;			// 0x72c

	char			m_ExitMessage[500];			// 0x730
};

// GLOBAL: LITHTECH 0x004de2b8
extern ClientGlob g_ClientGlob;


// These are called in the startup code. They initialize the system-dependent modules.
// 0 = success
// 1 = couldn't load resource module (ltmsg.dll).
int dsi_Init();
void dsi_Term();

// Called when any function uses RETURN_ERROR.
void dsi_OnReturnError(LTRESULT err);

// ClientDE implementations.
RMode* dsi_GetRenderModes();
void dsi_RelinquishRenderModes(RMode *pMode);
LTRESULT dsi_GetRenderMode(RMode *pMode);
LTRESULT dsi_SetRenderMode(RMode *pMode);
LTRESULT dsi_ShutdownRender(uint32 flags);

// Initializes the cshell and cres DLLs (copies them into a temp directory).
LTRESULT dsi_InitClientShellDE(CClientMgr *pClientMgr);
LTRESULT dsi_LoadServerObjects(CClassMgr *pClassMgr);

// Called when we run out of memory.
void dsi_OnMemoryFailure();

// Client-only things.
void dsi_ClientSleep(uint32 ms);

LTBOOL dsi_IsInputEnabled();

uint16 dsi_NumKeyDowns();
uint16 dsi_NumKeyUps();
uint32 dsi_GetKeyDown(uint32 i);
uint32 dsi_GetKeyDownRep(uint32 i);
uint32 dsi_GetKeyUp(uint32 i);
void dsi_ClearKeyDowns();
void dsi_ClearKeyUps();
void dsi_ClearKeyMessages();

LTBOOL dsi_IsConsoleUp();
void dsi_SetConsoleUp(LTBOOL bUp);
LTBOOL dsi_IsClientActive();
void dsi_OnClientShutdown(char *pMsg);

// Sets up a message for a LTRESULT.
LTRESULT dsi_SetupMessage(char *pMsg, int maxMsgLen, LTRESULT dResult, va_list marker);

// Puts an error message in the console, and in a message box if the renderer isn't up.
LTRESULT dsi_DoErrorMessage(const char *pMessage);

// Talon copies files out of rez files into pTempPath.
LTRESULT GetOrCopyFile(CServerMgr *pServerMgr, const char *pTempPath, const char *pFilename,
	char *pOutName, int outNameLen);
LTRESULT GetOrCopyClientFile(CClientMgr *pClientMgr, const char *pTempPath, const char *pFilename,
	char *pOutName, int outNameLen);

void dsi_ConsolePrint(const char *pMsg, ...);	// Print to console.

void* dsi_GetInstanceHandle();	// Returns an HINSTANCE.
void* dsi_GetMainWindow();		// Returns an HWND.

// Message box.
void dsi_MessageBox(const char *pMsg, const char *pTitle);

// Get the version info of the executable.
LTRESULT dsi_GetVersionInfo(LTVersionInfo &info);

#endif  // __DSYS_INTERFACE_H__
