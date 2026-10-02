// lithshared stdlith/goodlinklist.cpp (Talon). Jupiter libs/stdlith/goodlinklist.cpp with uint32 -> DWORD
// (the Talon .cpp is not on disk).
// FLAGS: /O2 /GX /Gy /IE:/AVP2Source/build/proj/LT2/lithshared/stdlith

#include "goodlinklist.h"


// FUNCTION: LITHTECH 0x004b3450
GPOS GLinkedList_FindElementMemcmp(
	CGLLNode *pHead,
	const void *pToFind,
	DWORD elementSize)
{
	CGLLNode *pCur;

	if(pHead)
	{
		pCur = pHead;
		do
		{
			if(memcmp(pCur, pToFind, elementSize) == 0)
				return pCur;

			pCur = pCur->m_pGNext;
		} while(pCur != pHead);
	}

	return NULL;
}
