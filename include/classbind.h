// Class bind module (Talon layout recovered from lithtech.exe; Jupiter shared/src/classbind.h).
#ifndef __CLASSBIND_H__
#define __CLASSBIND_H__

#include "ltbasedefs.h"

class CBindModuleType;
struct ClassDef;
struct PropDef;

#define CB_NOERROR			-1
#define CB_CANTFINDMODULE	0
#define CB_NOTCLASSMODULE	1
#define CB_VERSIONMISMATCH	2

// Talon: heap allocated by cb_LoadModule (Jupiter embeds it by value in CClassMgr).
class ClassBindModule
{
public:
	uint8			m_Pad0[8];		// 0x00 unused by matched code
	CBindModuleType	*m_hModule;		// 0x08
	ClassDef		**m_pClassDefs;	// 0x0c
	int				m_nClassDefs;	// 0x10
};

// Talon bindmgr (lives in another unit).
int		bm_BindModule(const char *pModuleName, CBindModuleType **pModule);
void	bm_UnbindModule(CBindModuleType *hModule);
void*	bm_GetFunctionPointer(CBindModuleType *hModule, const char *pFunctionName);

// Returns a CB_ status. version is set if it returns CB_VERSIONMISMATCH.
int			cb_LoadModule(const char *pModuleName, void *pServer, ClassBindModule **ppModule, int *version);
void		cb_UnloadModule(ClassBindModule *pModule);

int			cb_GetNumClassDefs(ClassBindModule *hModule);
ClassDef**	cb_GetClassDefs(ClassBindModule *hModule);

ClassDef*	cb_FindClass(ClassBindModule *hModule, const char *pClassName);
ClassDef*	cb_IsClassFlagSet(ClassBindModule *hModule, ClassDef *pClass, const uint32 dwClassFlag);

#endif  // __CLASSBIND_H__
