// Jupiter runtime/shared/src/nexus.cpp
// FLAGS: /O2 /GX-
#include "ltbasedefs.h"
#include "nexus.h"


// FUNCTION: LITHTECH 0x00466530
Nexus::Nexus()
{
	Init(LTNULL);
}


// FUNCTION: LITHTECH 0x00466540
Nexus::~Nexus()
{
	Term();
}


// FUNCTION: LITHTECH 0x00466550
LTBOOL Nexus::Init(void *pData)
{
	m_LeechHead = LTNULL;
	m_pData = pData;
	return LTTRUE;
}


// FUNCTION: LITHTECH 0x00466570
void Nexus::Term()
{
	LTBOOL bFree;

	bFree = LTTRUE;
	SendMessage(NEXUS_NEXUSDESTROY, &bFree);
}


// FUNCTION: LITHTECH 0x00466590
LTRESULT Nexus::SendMessage(int msg, void *pUserData)
{
	Leech *pCur, *pNext;
	LeechDef *pCurDef;

	for(pCur=m_LeechHead; pCur; pCur=pNext)
	{
		pNext = pCur->m_pNext;

		pCurDef = pCur->m_Def;
		while(pCurDef)
		{
			pCurDef->m_Fn(this, pCur, msg, pUserData);
			pCurDef = pCurDef->m_pParent;
		}
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x004665e0
void Nexus::RemoveLeech(Leech *pLeech)
{
	Leech **ppPrev, *pCur;

	ppPrev = &m_LeechHead;
	for(pCur=m_LeechHead; pCur; pCur=pCur->m_pNext)
	{
		if(pCur == pLeech)
		{
			*ppPrev = pCur->m_pNext;
			break;
		}

		ppPrev = &pCur->m_pNext;
	}
}


// FUNCTION: LITHTECH 0x00466610
Leech* Nexus::FindLeech(LeechDef *pDef)
{
	Leech *pLeech;

	for(pLeech=m_LeechHead; pLeech; pLeech=pLeech->m_pNext)
	{
		if(pLeech->m_Def == pDef)
			return pLeech;
	}

	return LTNULL;
}
