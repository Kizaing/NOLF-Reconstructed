// Talon shell binder (sb_*): loads object.lto / cshell.dll and gets their shell create/delete
// functions (Get<shell>Functions) after checking Get<shell>Version. Jupiter dropped it
// (its shells come through the interface manager); modelled on shared/classbind.cpp.
#include <stdio.h>
#include <stdlib.h>
#include "bdefs.h"
#include "bindmgr.h"

#define SB_NOERROR			-1
#define SB_CANTFINDMODULE	0
#define SB_NOTSHELLMODULE	1
#define SB_VERSIONMISMATCH	2

typedef void* (*CreateShellFn)(void *pInterface);
typedef void (*DeleteShellFn)(void *pShell);

typedef void (*GetShellFunctionsFn)(CreateShellFn *pCreate, DeleteShellFn *pDelete);
typedef int (*GetShellVersionFn)();

struct ShellBindModule
{
	CBindModuleType	*m_hModule;		// 0x00
	CreateShellFn	m_CreateFn;		// 0x04
	DeleteShellFn	m_DeleteFn;		// 0x08
};


// FUNCTION: LITHTECH 0x0048a640
int sb_LoadShellModule(const char *pModuleName, const char *pShellName, ShellBindModule **ppModule,
	int shellVersion, int *pVersion)
{
	CBindModuleType *hModule;
	GetShellFunctionsFn getFunctions;
	GetShellVersionFn getVersion;
	CreateShellFn createFn;
	DeleteShellFn deleteFn;
	ShellBindModule *pModule;
	char funcName[100];

	if (!bm_BindModule(pModuleName, &hModule))
		return SB_CANTFINDMODULE;

	bm_SetInstanceHandle(hModule);

	sprintf(funcName, "Get%sFunctions", pShellName);
	getFunctions = (GetShellFunctionsFn)bm_GetFunctionPointer(hModule, funcName);
	if (!getFunctions)
	{
		bm_UnbindModule(hModule);
		return SB_NOTSHELLMODULE;
	}

	sprintf(funcName, "Get%sVersion", pShellName);
	getVersion = (GetShellVersionFn)bm_GetFunctionPointer(hModule, funcName);
	if (!getVersion)
	{
		bm_UnbindModule(hModule);
		return SB_NOTSHELLMODULE;
	}

	*pVersion = getVersion();
	if (*pVersion != shellVersion)
	{
		bm_UnbindModule(hModule);
		return SB_VERSIONMISMATCH;
	}

	getFunctions(&createFn, &deleteFn);

	pModule = (ShellBindModule*)malloc(sizeof(ShellBindModule));
	pModule->m_hModule = hModule;
	pModule->m_CreateFn = createFn;
	pModule->m_DeleteFn = deleteFn;

	*ppModule = pModule;
	return SB_NOERROR;
}


// FUNCTION: LITHTECH 0x0048a770
void sb_UnloadShellModule(ShellBindModule *pModule)
{
	bm_UnbindModule(pModule->m_hModule);
	free(pModule);
}


// FUNCTION: LITHTECH 0x0048a790
void sb_GetShellFunctions(ShellBindModule *pModule, CreateShellFn *pCreate, DeleteShellFn *pDelete)
{
	*pCreate = pModule->m_CreateFn;
	*pDelete = pModule->m_DeleteFn;
}


// FUNCTION: LITHTECH 0x0048a7b0
CBindModuleType* sb_GetModule(ShellBindModule *pModule)
{
	return pModule->m_hModule;
}
