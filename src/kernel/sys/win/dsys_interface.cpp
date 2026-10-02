// Jupiter runtime/kernel/src/sys/win/dsys_interface.cpp (client build).
// Talon passes the server/client managers explicitly, binds the shells through the shell
// binder (sb_*), and finds render DLLs with _findfirst("*.ren").
#include <windows.h>
#include <io.h>
#include <stdio.h>
#include <string.h>
#include "bdefs.h"
#include "dsys_interface.h"
#include "de_memory.h"
#include "bindmgr.h"
#include "classbind.h"
#include "servermgr.h"
#include "clientmgr.h"
#include "console.h"
#include "render.h"
#include "sprite.h"
#include "version_resource.h"
#include "de_file.h"
#include "server_filemgr.h"
#include "client_filemgr.h"

// Other modules' init/term functions.
void str_Init();
void str_Term();
void obj_Init();	// 0x004300b0 (world/de_objects?)
void obj_Term();	// 0x004300e0
void packet_Init();
void packet_Term();

#define TYPECODE_DLL	5

// Shell binder: object.lto and cshell.dll export Get<shell>Functions and Get<shell>Version.
#define SB_NOERROR			-1
#define SB_CANTFINDMODULE	0
#define SB_NOTSHELLMODULE	1
#define SB_VERSIONMISMATCH	2
#define SHELL_VERSION		2
typedef void* (*CreateShellFn)(void *pInterface);
typedef void (*DeleteShellFn)(void *pShell);
int sb_LoadShellModule(const char *pModuleName, const char *pShellName, ShellBindModule **ppModule,
	int shellVersion, int *pVersion);	// 0x0048a640
void sb_GetShellFunctions(ShellBindModule *pModule, CreateShellFn *pCreate, DeleteShellFn *pDelete);	// 0x0048a790

void sm_SetupError(CServerMgr *pServerMgr, LTRESULT err, ...);
void cm_InitClientShellVars(CClientMgr *pClientMgr);	// 0x004360a0

// Render DLL exports.
typedef RMode* (*GetSupportedModesFn)();
typedef void (*FreeModeListFn)(RMode *pModes);


// GLOBAL: LITHTECH 0x004e3690
LTBOOL g_bComInitialized;
// GLOBAL: LITHTECH 0x004e3694
HINSTANCE g_hResourceModule;

struct LTSysResultString
{
	unsigned long dResult;
	unsigned long string_id;
};

// GLOBAL: LITHTECH 0x004d1df8
LTSysResultString g_StringMap[] =
{
	LT_SERVERERROR,					15,
	LT_ERRORLOADINGRENDERDLL,		16,
	LT_MISSINGWORLDMODEL,			2,
	LT_CANTLOADGAMERESOURCES,		3,
	LT_CANTINITIALIZEINPUT,			4,
	LT_MISSINGSHELLDLL,				78,
	LT_INVALIDSHELLDLL,				76,
	LT_INVALIDSHELLDLLVERSION,		77,
	LT_CANTCREATECLIENTSHELL,		82,
	LT_UNABLETOINITSOUND,			89,
	LT_MISSINGWORLDFILE,			13,
	LT_INVALIDWORLDFILE,			7,
	LT_INVALIDSERVERPACKET,			92,
	LT_MISSINGSPRITEFILE,			10,
	LT_INVALIDSPRITEFILE,			11,
	LT_MISSINGMODELFILE,			8,
	LT_INVALIDMODELFILE,			9,
	LT_UNABLETORESTOREVIDEO,		5,
	LT_MISSINGCLASS,				93,
	LT_CANTCREATESERVERSHELL,		42,
	LT_INVALIDOBJECTDLLVERSION,		66,
	LT_ERRORINITTINGNETDRIVER,		94,
	LT_USERCANCELED,				6,
	LT_CANTRESTOREOBJECT,			14,
	LT_NOGAMERESOURCES,				12,
	LT_ERRORCOPYINGFILE,			17,
	LT_INVALIDNETVERSION,			95
};

#define STRINGMAP_SIZE (sizeof(g_StringMap) / sizeof(g_StringMap[0]))

#define IDS_GENERIC_ERROR	1


// FUNCTION: LITHTECH 0x00434290
void dsi_OnReturnError(LTRESULT err)
{
	if(g_ClientGlob.m_bBreakOnError)
	{
		DebugBreak();
	}
}


// --------------------------------------------------------------- //
// Internal functions.
// --------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x004343e0
static LTBOOL dsi_LoadResourceModule()
{
	g_hResourceModule = LoadLibrary("ltmsg.dll");
	return !!g_hResourceModule;
}

// FUNCTION: LITHTECH 0x00434440
static void dsi_UnloadResourceModule()
{
	if(g_hResourceModule)
	{
		FreeLibrary(g_hResourceModule);
		g_hResourceModule = LTNULL;
	}
}


// FUNCTION: LITHTECH 0x004342a0
LTRESULT dsi_SetupMessage(char *pMsg, int maxMsgLen, LTRESULT dResult, va_list marker)
{
	int i;
	unsigned long resultCode, stringID;
	LTBOOL bFound;
	uint32 args[4], nBytes;
	char tempBuffer[500];

	pMsg[0] = 0;

	if(!g_hResourceModule)
	{
		sprintf(pMsg, "<missing resource DLL>");
		return LT_ERROR;
	}

	// Try to find the error code.
	bFound = LTFALSE;
	resultCode = dResult & 0xFFFF;
	for(i=0; i < STRINGMAP_SIZE; i++)
	{
		if(g_StringMap[i].dResult == resultCode)
		{
			bFound = LTTRUE;
			stringID = g_StringMap[i].string_id;
			break;
		}
	}

	if(bFound)
	{
		nBytes = LoadString(g_hResourceModule, stringID, tempBuffer, sizeof(tempBuffer)-1);
		if(nBytes > 0)
		{
			// Format it.
			nBytes = FormatMessage(FORMAT_MESSAGE_FROM_STRING, tempBuffer, 0, 0, pMsg, maxMsgLen, &marker);

			if(nBytes > 0)
				return LT_OK;
		}
	}

	// Ok, format the default message.
	args[0] = resultCode;
	nBytes = FormatMessage(FORMAT_MESSAGE_FROM_HMODULE|FORMAT_MESSAGE_ARGUMENT_ARRAY,
		g_hResourceModule, IDS_GENERIC_ERROR, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		pMsg, maxMsgLen, (va_list*)args);

	if(nBytes > 0)
	{
		return LT_OK;
	}
	else
	{
		sprintf(pMsg, "<invalid resource DLL>");
		return LT_ERROR;
	}
}


// --------------------------------------------------------------- //
// External functions.
// --------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00434390
int dsi_Init()
{
	HRESULT hResult;

	hResult = CoInitialize(LTNULL);
	if(SUCCEEDED(hResult))
	{
		g_bComInitialized = LTTRUE;
	}
	else
	{
		return 1;
	}

	dm_Init();		// Memory manager.
	str_Init();		// String manager.
	df_Init();		// File manager.
	obj_Init();
	packet_Init();

	if(dsi_LoadResourceModule())
	{
		return 0;
	}
	else
	{
		dsi_Term();
		return 1;
	}
}


// FUNCTION: LITHTECH 0x00434400
void dsi_Term()
{
	packet_Term();
	obj_Term();
	df_Term();
	str_Term();
	dm_Term();

	dsi_UnloadResourceModule();

	if(g_bComInitialized)
	{
		CoUninitialize();
		g_bComInitialized = LTFALSE;
	}
}


// FUNCTION: LITHTECH 0x00434460
LTRESULT GetOrCopyFile(CServerMgr *pServerMgr, const char *pTempPath, const char *pFilename,
	char *pOutName, int outNameLen)
{
	ResTree *pTree;
	char tempFilename[MAX_PATH];
	LTRESULT dResult, dResult2;
	ServerFileMgr *pFileMgr;

	pFileMgr = &pServerMgr->m_FileMgr;
	if(sf_DoesFileExist(pFileMgr, pFilename, &pTree, LTNULL) &&
		(df_GetTreeType(pTree->m_hFileTree) == DosTree))
	{
		df_GetFullFilename(pTree->m_hFileTree, pFilename, pOutName, outNameLen);
		return LT_OK;
	}

	// Copy it into the temp directory.
	sprintf(pOutName, "%s%s", pTempPath, pFilename);
	dResult = sf_CopyFile(pFileMgr, pFilename, pOutName);
	if(dResult == LT_OK)
		return LT_OK;

	// Try a temp filename.
	if(GetTempFileName(pTempPath, pFilename, 0, tempFilename))
	{
		dResult2 = sf_CopyFile(pFileMgr, pFilename, tempFilename);
		if(dResult2 != LT_OK)
			return dResult2;

		strncpy(pOutName, tempFilename, outNameLen-1);
		return LT_OK;
	}

	return dResult;
}


// STUB: LITHTECH 0x00434570
// Remaining diff: VC merges the two identical LT_INVALIDOBJECTDLL error tails the other way round.
LTRESULT dsi_LoadServerObjects(CClassMgr *pClassMgr)
{
	int version;
	char fileName[256];
	char tempPath[200];
	int status;
	char *pDLLName;
	CServerMgr *pServerMgr;

	pServerMgr = pClassMgr->m_pServerMgr;

	if(!GetTempPath(sizeof(tempPath), tempPath))
	{
		strcpy(tempPath, ".\\");
	}

	pDLLName = "object.lto";

	// Copy the object.lto file out of the res so we can run it.
	if(GetOrCopyFile(pServerMgr, tempPath, pDLLName, fileName, sizeof(fileName)) == LT_OK)
	{
		// Load the object.lto DLL.
		status = cb_LoadModule(fileName, pServerMgr->m_pServerInterface, &pClassMgr->m_ClassModule, &version);

		// Check for errors.
		if(status == CB_NOERROR)
		{
			// Bind to the server shell.
			status = sb_LoadShellModule(fileName, "ServerShell", &pClassMgr->m_hShellModule, SHELL_VERSION, &version);
			if(status == SB_CANTFINDMODULE)
			{
				sm_SetupError(pServerMgr, LT_MISSINGSHELLDLL, fileName);
				RETURN_ERROR_PARAM(1, LoadServerObjects, LT_MISSINGSHELLDLL, pDLLName);
			}
			else if(status == SB_NOTSHELLMODULE)
			{
				sm_SetupError(pServerMgr, LT_INVALIDSHELLDLL, fileName);
				RETURN_ERROR_PARAM(1, LoadServerObjects, LT_INVALIDSHELLDLL, pDLLName);
			}
			else if(status == SB_VERSIONMISMATCH)
			{
				sm_SetupError(pServerMgr, LT_INVALIDSHELLDLLVERSION, fileName, version, SHELL_VERSION);
				RETURN_ERROR_PARAM(1, LoadServerObjects, LT_INVALIDSHELLDLLVERSION, pDLLName);
			}

			sb_GetShellFunctions(pClassMgr->m_hShellModule,
				(CreateShellFn*)&pClassMgr->m_CreateServerShellFn, (DeleteShellFn*)&pClassMgr->m_DeleteServerShellFn);

			// Get sres.dll.
			if(GetOrCopyFile(pServerMgr, tempPath, "sres.dll", fileName, sizeof(fileName)) == LT_OK &&
				bm_BindModule(fileName, &pClassMgr->m_hServerResourceModule) == BIND_NOERROR)
			{
				return LT_OK;
			}

			sb_UnloadShellModule(pClassMgr->m_hShellModule);
			pClassMgr->m_hShellModule = LTNULL;

			sm_SetupError(pServerMgr, LT_ERRORCOPYINGFILE, "sres.dll");
			RETURN_ERROR_PARAM(1, LoadServerObjects, LT_ERRORCOPYINGFILE, "sres.dll");
		}
		else if(status == CB_CANTFINDMODULE)
		{
			sm_SetupError(pServerMgr, LT_INVALIDOBJECTDLL, pDLLName);
			RETURN_ERROR_PARAM(1, LoadObjectsInDirectory, LT_INVALIDOBJECTDLL, pDLLName);
		}
		else if(status == CB_NOTCLASSMODULE)
		{
			sm_SetupError(pServerMgr, LT_INVALIDOBJECTDLL, pDLLName);
			RETURN_ERROR_PARAM(1, LoadObjectsInDirectory, LT_INVALIDOBJECTDLL, pDLLName);
		}
		else
		{
			sm_SetupError(pServerMgr, LT_INVALIDOBJECTDLLVERSION, pDLLName, version, SERVEROBJ_VERSION);
			RETURN_ERROR_PARAM(1, LoadObjectsInDirectory, LT_INVALIDOBJECTDLLVERSION, pDLLName);
		}
	}

	sm_SetupError(pServerMgr, LT_ERRORCOPYINGFILE, pDLLName);
	RETURN_ERROR_PARAM(1, LoadServerObjects, LT_ERRORCOPYINGFILE, pDLLName);
}


// FUNCTION: LITHTECH 0x00434970
static void dsi_GetDLLModes(char *pDLLName, RMode **pMyList)
{
	HINSTANCE hModule;
	GetSupportedModesFn getModes;
	FreeModeListFn freeModes;
	RMode *pMyMode;
	RMode *pListHead, *pCur;

	hModule = LoadLibrary(pDLLName);
	if(!hModule)
		return;

	getModes = (GetSupportedModesFn)GetProcAddress(hModule, "GetSupportedModes");
	freeModes = (FreeModeListFn)GetProcAddress(hModule, "FreeModeList");
	if(getModes && freeModes)
	{
		pListHead = getModes();

		// Copy the mode list.
		pCur = pListHead;
		while(pCur)
		{
			pMyMode = (RMode*)dalloc(sizeof(RMode));
			memcpy(pMyMode, pCur, sizeof(RMode));
			strncpy(pMyMode->m_RenderDLL, pDLLName, sizeof(pMyMode->m_RenderDLL)-1);

			pMyMode->m_pNext = *pMyList;
			*pMyList = pMyMode;

			pCur = pCur->m_pNext;
		}

		freeModes(pListHead);
	}

	FreeLibrary(hModule);
}


// FUNCTION: LITHTECH 0x00434910
RMode* dsi_GetRenderModes()
{
	RMode *pList;
	long handle;
	_finddata_t data;

	pList = LTNULL;

	handle = _findfirst("*.ren", &data);
	if(handle != -1)
	{
		do
		{
			if(!(data.attrib & _A_SUBDIR))
			{
				dsi_GetDLLModes(data.name, &pList);
			}
		}
		while(_findnext(handle, &data) != -1);
	}

	return pList;
}


// FUNCTION: LITHTECH 0x00434a30
void dsi_RelinquishRenderModes(RMode *pMode)
{
	RMode *pCur, *pNext;

	pCur = pMode;
	while(pCur)
	{
		pNext = pCur->m_pNext;
		dfree(pCur);
		pCur = pNext;
	}
}


// FUNCTION: LITHTECH 0x00434a50
LTRESULT dsi_GetRenderMode(RMode *pMode)
{
	memcpy(pMode, &g_RMode, sizeof(RMode));
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00434a70
LTRESULT dsi_SetRenderMode(RMode *pMode)
{
	RMode currentMode;
	char message[256];

	if(r_TermRender(g_pClientMgr, 1) != LT_OK)
	{
		dsi_SetupMessage(message, sizeof(message)-1, LT_UNABLETORESTOREVIDEO, LTNULL);
		dsi_OnClientShutdown(message);
		RETURN_ERROR(0, SetRenderMode, LT_UNABLETORESTOREVIDEO);
	}

	memcpy(&currentMode, &g_RMode, sizeof(RMode));

	// Try to set the new mode.
	if(r_InitRender(g_pClientMgr, pMode) != LT_OK)
	{
		// Ok, try to restore the old mode.
		if(r_InitRender(g_pClientMgr, &currentMode) != LT_OK)
		{
			RETURN_ERROR(0, SetRenderMode, LT_UNABLETORESTOREVIDEO);
		}

		RETURN_ERROR(1, SetRenderMode, LT_KEPTSAMEMODE);
	}

	g_ClientGlob.m_bRendererShutdown = LTFALSE;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00434bd0
LTRESULT dsi_ShutdownRender(uint32 flags)
{
	r_TermRender(g_pClientMgr, 1);

	if(flags & RSHUTDOWN_MINIMIZEWINDOW)
	{
		ShowWindow(g_ClientGlob.m_hMainWnd, SW_MINIMIZE);
	}

	if(flags & RSHUTDOWN_HIDEWINDOW)
	{
		ShowWindow(g_ClientGlob.m_hMainWnd, SW_HIDE);
	}

	g_ClientGlob.m_bRendererShutdown = LTTRUE;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00434c20
LTRESULT GetOrCopyClientFile(CClientMgr *pClientMgr, const char *pTempPath, const char *pFilename,
	char *pOutName, int outNameLen)
{
	FileIdentifier *pIdent;
	FileRef ref;
	char tempFilename[MAX_PATH];
	LTRESULT dResult, dResult2;

	ref.m_FileType = FILE_ANYFILE;
	ref.m_pFilename = pFilename;
	pIdent = cf_GetFileIdentifier(pClientMgr->m_hFileMgr, &ref, TYPECODE_DLL);
	if(pIdent && (df_GetTreeType(pIdent->m_hFileTree) == DosTree))
	{
		df_GetFullFilename(pIdent->m_hFileTree, pFilename, pOutName, outNameLen);
		return LT_OK;
	}

	// Copy it into the temp directory.
	sprintf(pOutName, "%s%s", pTempPath, pFilename);
	dResult = cf_CopyFile(pClientMgr->m_hFileMgr, pFilename, pOutName);
	if(dResult == LT_OK)
		return LT_OK;

	// Try a temp filename.
	if(GetTempFileName(pTempPath, pFilename, 0, tempFilename))
	{
		dResult2 = cf_CopyFile(pClientMgr->m_hFileMgr, pFilename, tempFilename);
		if(dResult2 != LT_OK)
			return dResult2;

		strncpy(pOutName, tempFilename, outNameLen-1);
		return LT_OK;
	}

	return dResult;
}


// FUNCTION: LITHTECH 0x00434d40
LTRESULT dsi_InitClientShellDE(CClientMgr *pClientMgr)
{
	int version;
	CreateShellFn createFn;
	DeleteShellFn deleteFn;
	char tempPath[200];
	char fileName[256];
	int status;

	pClientMgr->m_hClientResourceModule = LTNULL;
	pClientMgr->m_hLocalizedClientResourceModule = LTNULL;
	pClientMgr->m_hShellModule = LTNULL;

	if(!GetTempPath(sizeof(tempPath), tempPath))
	{
		strcpy(tempPath, ".\\");
	}

	// Setup the cshell.dll file.
	if(GetOrCopyClientFile(pClientMgr, tempPath, "cshell.dll", fileName, sizeof(fileName)) != LT_OK)
	{
		pClientMgr->SetupError(LT_ERRORCOPYINGFILE, "cshell.dll");
		RETURN_ERROR_PARAM(1, InitClientShellDE, LT_ERRORCOPYINGFILE, "cshell.dll");
	}

	status = sb_LoadShellModule(fileName, "ClientShell", &pClientMgr->m_hShellModule, SHELL_VERSION, &version);
	if(status == SB_NOERROR)
	{
		// Try to setup cres.dll.
		if(GetOrCopyClientFile(pClientMgr, tempPath, "cres.dll", fileName, sizeof(fileName)) == LT_OK &&
			bm_BindModule(fileName, &pClientMgr->m_hClientResourceModule) == BIND_NOERROR)
		{
			// The localized resources are optional.
			GetOrCopyClientFile(pClientMgr, tempPath, "cresl.dll", fileName, sizeof(fileName));
			bm_BindModule(fileName, &pClientMgr->m_hLocalizedClientResourceModule);

			sb_GetShellFunctions(pClientMgr->m_hShellModule, &createFn, &deleteFn);
			pClientMgr->m_pClientShell = (IClientShell*)createFn(pClientMgr->m_pClientDE);
			if(!pClientMgr->m_pClientShell)
			{
				RETURN_ERROR(1, InitClientShellDE, LT_CANTCREATECLIENTSHELL);
			}

			cm_InitClientShellVars(pClientMgr);
			return LT_OK;
		}

		// Unload cshell.dll.
		sb_UnloadShellModule(pClientMgr->m_hShellModule);
		pClientMgr->m_hShellModule = LTNULL;

		pClientMgr->SetupError(LT_ERRORCOPYINGFILE, "cres.dll");
		RETURN_ERROR_PARAM(1, InitClientShellDE, LT_ERRORCOPYINGFILE, "cres.dll");
	}
	else if(status == SB_CANTFINDMODULE)
	{
		pClientMgr->SetupError(LT_MISSINGSHELLDLL, "cshell.dll");
		RETURN_ERROR(1, InitClientShellDE, LT_MISSINGSHELLDLL);
	}
	else if(status == SB_NOTSHELLMODULE)
	{
		pClientMgr->SetupError(LT_INVALIDSHELLDLL, "cshell.dll");
		RETURN_ERROR(1, InitClientShellDE, LT_INVALIDSHELLDLL);
	}
	else
	{
		pClientMgr->SetupError(LT_INVALIDSHELLDLLVERSION, "cshell.dll", version, SHELL_VERSION);
		RETURN_ERROR(1, InitClientShellDE, LT_INVALIDSHELLDLLVERSION);
	}
}


// FUNCTION: LITHTECH 0x00435060
void dsi_OnMemoryFailure()
{
	longjmp(g_ClientGlob.m_MemoryJmp, 1);
}


// Client-only functions.
// FUNCTION: LITHTECH 0x00435070
void dsi_ClientSleep(uint32 ms)
{
	if(ms > 0)
	{
		Sleep(ms);
	}
}


// FUNCTION: LITHTECH 0x00435080
LTBOOL dsi_IsInputEnabled()
{
	return g_ClientGlob.m_bInputEnabled;
}


// FUNCTION: LITHTECH 0x00435090
uint16 dsi_NumKeyDowns()
{
	return g_ClientGlob.m_nKeyDowns;
}


// FUNCTION: LITHTECH 0x004350a0
uint16 dsi_NumKeyUps()
{
	return g_ClientGlob.m_nKeyUps;
}


// FUNCTION: LITHTECH 0x004350b0
uint32 dsi_GetKeyDown(uint32 i)
{
	return g_ClientGlob.m_KeyDowns[i];
}


// FUNCTION: LITHTECH 0x004350c0
uint32 dsi_GetKeyDownRep(uint32 i)
{
	return g_ClientGlob.m_KeyDownReps[i];
}


// FUNCTION: LITHTECH 0x004350d0
uint32 dsi_GetKeyUp(uint32 i)
{
	return g_ClientGlob.m_KeyUps[i];
}

// FUNCTION: LITHTECH 0x004350e0
void dsi_ClearKeyDowns()
{
	g_ClientGlob.m_nKeyDowns=0;
}

// FUNCTION: LITHTECH 0x004350f0
void dsi_ClearKeyUps()
{
	g_ClientGlob.m_nKeyUps=0;
}

// FUNCTION: LITHTECH 0x00435100
void dsi_ClearKeyMessages()
{
	MSG msg;
	int i;

	for(i=0; i < 500; i++)
	{
		if(!PeekMessage(&msg, g_ClientGlob.m_hMainWnd, WM_KEYDOWN, WM_KEYDOWN, PM_REMOVE))
		{
			break;
		}
	}

	for(i=0; i < 500; i++)
	{
		if(!PeekMessage(&msg, g_ClientGlob.m_hMainWnd, WM_KEYUP, WM_KEYUP, PM_REMOVE))
		{
			break;
		}
	}
}

// FUNCTION: LITHTECH 0x00435170
LTBOOL dsi_IsConsoleUp()
{
	return g_ClientGlob.m_bIsConsoleUp;
}

// FUNCTION: LITHTECH 0x00435180
void dsi_SetConsoleUp(LTBOOL bUp)
{
	g_ClientGlob.m_bIsConsoleUp = bUp;
}

// FUNCTION: LITHTECH 0x00435190
LTBOOL dsi_IsClientActive()
{
	return g_ClientGlob.m_bClientActive;
}

// FUNCTION: LITHTECH 0x004351a0
void dsi_OnClientShutdown(char *pMsg)
{
	if(pMsg && pMsg[0])
	{
		LTStrCpy(g_ClientGlob.m_ExitMessage, pMsg, sizeof(g_ClientGlob.m_ExitMessage));
	}
	else
	{
		g_ClientGlob.m_ExitMessage[0] = '\0';
	}

	if(g_ClientGlob.m_bProcessWindowMessages)
	{
		PostQuitMessage(0);
	}
}


// FUNCTION: LITHTECH 0x004351f0
void dsi_ConsolePrint(const char *pMsg, ...)
{
	char msg[500];
	va_list marker;

	va_start(marker, pMsg);
	vsprintf(msg, pMsg, marker);
	va_end(marker);

	con_PrintString(CONRGB(0,255,255), 0, msg);
}

// FUNCTION: LITHTECH 0x00435230
void* dsi_GetInstanceHandle()
{
	return (void*)g_ClientGlob.m_hInstance;
}

// FUNCTION: LITHTECH 0x00435240
void* dsi_GetMainWindow()
{
	return (void*)g_ClientGlob.m_hMainWnd;
}

// FUNCTION: LITHTECH 0x00435250
LTRESULT dsi_DoErrorMessage(const char *pMessage)
{
	con_PrintString(CONRGB(255,255,255), 0, pMessage);

	if(!g_Render.m_bInitted)
	{
		MessageBox(g_ClientGlob.m_hMainWnd, pMessage, g_ClientGlob.m_WndCaption, MB_OK);
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00435290
void dsi_MessageBox(const char *pMessage, const char *pTitle)
{
	int i;
	i = MessageBox(g_ClientGlob.m_hMainWnd, pMessage, pTitle, MB_OK);
}

// FUNCTION: LITHTECH 0x004352b0
LTRESULT dsi_GetVersionInfo(LTVersionInfo &info)
{
	return GetLTExeVersion(g_ClientGlob.m_hInstance, info);
}
