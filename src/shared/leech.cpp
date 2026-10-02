// Jupiter runtime/shared/src/leech.cpp
// FLAGS: /O2 /GX-
#include <windows.h>
#include "bdefs.h"
#include "nexus.h"
#include "lthread.h"
#include "../../build/proj/LT2/lithshared/stdlith/object_bank.h"


// FUNCTION: LITHTECH 0x004447d0 _$E4
// FUNCTION: LITHTECH 0x004447e0 _$E1
// FUNCTION: LITHTECH 0x00444820 _$E3
// FUNCTION: LITHTECH 0x00444830 _$E2
// g_LeechBank is at 0x004e42c4.
ObjectBank<Leech, LCriticalSection> g_LeechBank(32, 32);
// FUNCTION: LITHTECH 0x00444860 _$E9
// FUNCTION: LITHTECH 0x00444870 _$E6
// FUNCTION: LITHTECH 0x00444880 _$E8
// FUNCTION: LITHTECH 0x00444890 _$E7
// GLOBAL: LITHTECH 0x004e42fc
LCriticalSection g_NexusCS;



// The base Leech stuff.
// FUNCTION: LITHTECH 0x004448a0
LTRESULT BaseLeechFn(Nexus *pNexus, Leech *pLeech, int msg, void *pUserData)
{
	LTBOOL bFree;

	if(msg == NEXUS_NEXUSDESTROY)
	{
		// Remove it from the Nexus' list.
		pNexus->RemoveLeech(pLeech);

		if(pUserData)
		{
			bFree = *((LTBOOL*)pUserData);
			if(bFree)
			{
				g_LeechBank.Free(pLeech);
			}
		}
	}

	return LT_OK;
}


// GLOBAL: LITHTECH 0x004d3bc0
LeechDef g_BaseLeech =
{
	BaseLeechFn,
	LTNULL
};


// FUNCTION: LITHTECH 0x004448f0
Leech* nexus_CreateLeech(LeechDef *pDef, void *pUserData)
{
	Leech *pRet;

	pRet = g_LeechBank.Allocate();
	if(pRet)
	{
		pRet->m_Def = pDef;
		pRet->m_pUserData = pUserData;
	}

	return pRet;
}


// FUNCTION: LITHTECH 0x00444970
LTRESULT nexus_AddLeech(Nexus *pNexus, Leech *pLeech)
{
	if(pLeech)
	{
		CSAccess cs(&g_NexusCS);

		pLeech->m_pNext = pNexus->m_LeechHead;
		pNexus->m_LeechHead = pLeech;
		return LT_OK;
	}
	else
	{
		return LT_ERROR;
	}
}


// ObjectBank<Leech, LCriticalSection> code emitted into this object (its vtable is 0x004c75b8).
// FUNCTION: LITHTECH 0x004449b0 ?AllocVoid@?$ObjectBank@VLeech@@VLCriticalSection@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x00444a20 ?FreeVoid@?$ObjectBank@VLeech@@VLCriticalSection@@@@UAEXPAX@Z
// FUNCTION: LITHTECH 0x00444a60 ?Term@?$ObjectBank@VLeech@@VLCriticalSection@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00444a90 ??_G?$ObjectBank@VLeech@@VLCriticalSection@@@@UAEPAXI@Z
