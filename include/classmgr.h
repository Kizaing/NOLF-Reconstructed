// Server class manager (Talon layout recovered from lithtech.exe; Jupiter server/src/classmgr.h).
#ifndef __CLASSMGR_H__
#define __CLASSMGR_H__

#include "ltbasedefs.h"
#include "ltserverobj.h"
#include "classbind.h"
#include "concommand.h"
#include "bdefs.h"
#include "de_objects.h"

class CServerMgr;
#include "iservershell.h"
struct HHashTable;

// Talon stdlith struct_bank.h
#include "../../build/proj/LT2/lithshared/stdlith/struct_bank.h"


// Per-class data (0x4c bytes).
class CClassData
{
public:
	CClassData()
	{
		m_nTicksThisUpdate	= 0;
		m_nUpdated			= 0;
		m_nTotal			= 0;
		m_nTickAveCnt		= 0;
		m_nTotalTicks		= 0;
		m_nMaxTicks			= 0;
		m_bDisplayedTicks	= LTFALSE;
	}

	uint8		m_Pad0[8];			// 0x00 (CGLLNode in Jupiter?)
	StructBank	m_ObjectBank;		// 0x08
	ClassDef	*m_pClass;			// 0x24
	uint16		m_ClassID;			// 0x28
	uint32		m_nTicksThisUpdate;	// 0x2c
	uint32		m_nUpdated;			// 0x30
	uint32		m_nTotal;			// 0x34
	uint32		m_nTickAveCnt;		// 0x38
	uint32		m_nTotalTicks;		// 0x3c
	uint32		m_nMaxTicks;		// 0x40
	LTBOOL		m_bDisplayedTicks;	// 0x44
	LTObject	*m_pStaticObject;	// 0x48
};


// Server shell module (loaded by dsi_LoadServerObjects, freed at 0x0048a770).
struct ShellBindModule;
void sb_UnloadShellModule(ShellBindModule *pModule);



// 0x30 bytes, embedded in CServerMgr at 0xc1c.
class CClassMgr
{
public:
	CClassMgr();
	~CClassMgr();

	LTBOOL			Init(CServerMgr *pServerMgr);
	void			Term();

	CClassData*		FindClassData(const char *pName);

public:
	ClassBindModule		*m_ClassModule;				// 0x00
	CBindModuleType		*m_hServerResourceModule;	// 0x04
	ShellBindModule		*m_hShellModule;			// 0x08
	CreateServerShellFn	m_CreateServerShellFn;		// 0x0c
	DeleteServerShellFn	m_DeleteServerShellFn;		// 0x10
	IServerShell		*m_pServerShell;			// 0x14
	ClassDef			*m_pBaseClass;				// 0x18
	CClassData			*m_ClassDatas;				// 0x1c
	int					m_nClassDatas;				// 0x20
	HHashTable			*m_hClassNameHash;			// 0x24
	int					m_ClassIndex;				// 0x28
	CServerMgr			*m_pServerMgr;				// 0x2c
};


LTRESULT LoadServerBinaries(CClassMgr *pClassMgr);


#endif  // __CLASSMGR_H__
