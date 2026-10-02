// Jupiter runtime/kernel/src/sys/win/bindmgr.cpp
// The unit ends at 00401890: BinkVideoMgr's constructor starts binkvideomgrimpl.
#include <windows.h>
#include "bdefs.h"
#include "bindmgr.h"
#include "de_memory.h"

#define BINDTYPE_SERVER	0
#define BINDTYPE_DLL	1

typedef struct
{
	HINSTANCE	m_hInstance;	// 0x00
	int			m_Type;			// 0x04
	int			m_Unused;		// 0x08
} WinBind;

typedef void (*SetInstanceHandleFn)(void *handle);

// --------------------------------------------------------- //
// Main interface functions.
// --------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00401760
int bm_BindModule(const char *pModuleName, CBindModuleType **pModule)
{
	HINSTANCE hInstance;
	WinBind *pBind;

	hInstance = LoadLibrary(pModuleName);
	if(!hInstance)
		return BIND_CANTFINDMODULE;

	pBind = (WinBind*)dalloc(sizeof(WinBind));
	pBind->m_hInstance = hInstance;
	pBind->m_Type = BINDTYPE_DLL;

	*pModule = (CBindModuleType*)pBind;
	return BIND_NOERROR;
}


// FUNCTION: LITHTECH 0x004017a0
void bm_UnbindModule(CBindModuleType *hModule)
{
	WinBind *pBind = (WinBind*)hModule;

	if(pBind->m_Type == BINDTYPE_DLL)
	{
		FreeLibrary(pBind->m_hInstance);
	}

	dfree(pBind);
}


// FUNCTION: LITHTECH 0x004017c0
LTRESULT bm_SetInstanceHandle(CBindModuleType *hModule)
{
	SetInstanceHandleFn fn;
	WinBind *pBind;


	pBind = (WinBind*)hModule;
	if(!pBind)
	{
		RETURN_ERROR(1, bm_SetInstanceHandle, LT_INVALIDPARAMS);
	}

	fn = (SetInstanceHandleFn)GetProcAddress(pBind->m_hInstance, "SetInstanceHandle");
	if(fn)
	{
		fn((void*)pBind->m_hInstance);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00401820
LTRESULT bm_GetInstanceHandle(CBindModuleType *hModule, void **pHandle)
{
	WinBind *pBind;

	pBind = (WinBind*)hModule;
	if(!pBind)
	{
		RETURN_ERROR(1, bm_SetInstanceHandle, LT_INVALIDPARAMS);
	}

	*pHandle = (void*)pBind->m_hInstance;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00401870
void* bm_GetFunctionPointer(CBindModuleType *hModule, const char *pFunctionName)
{
	WinBind *pBind = (WinBind*)hModule;

	return (void*)GetProcAddress(pBind->m_hInstance, pFunctionName);
}
