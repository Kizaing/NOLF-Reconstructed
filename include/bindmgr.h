// The bind manager manages all bindings to the engine (Jupiter runtime/kernel/src/sys/win/bindmgr.h).
// Talon binds with a CBindModuleType** and has no temp-file tracking.
#ifndef __BINDMGR_H__
#define __BINDMGR_H__

#include "ltbasedefs.h"

class CBindModuleType;

#define BIND_NOERROR			-1
#define BIND_CANTFINDMODULE		0

// Bind and unbind to modules...
int		bm_BindModule(const char *pModuleName, CBindModuleType **pModule);	// 0x00401760
void	bm_UnbindModule(CBindModuleType *hModule);							// 0x004017a0

// Calls the DLL's SetInstanceHandle function.
LTRESULT bm_SetInstanceHandle(CBindModuleType *hModule);					// 0x004017c0
LTRESULT bm_GetInstanceHandle(CBindModuleType *hModule, void **pHandle);	// 0x00401820

// Returns NULL if this module doesn't contain the function.
void*	bm_GetFunctionPointer(CBindModuleType *hModule, const char *pFunctionName);	// 0x00401870

#endif  // __BINDMGR_H__
