// String manager (Jupiter runtime/kernel/src/sys/win/stringmgr.h).
#ifndef __STRINGMGR_H__
#define __STRINGMGR_H__

#include <stdarg.h>
#include "ltbasedefs.h"

// Talon lithshared/stdlith/glink.h (not on the include path).
#ifndef __GLINK_H__
#define __GLINK_H__
typedef struct GLink_t
{
	struct GLink_t *m_pNext, *m_pPrev;
	void *m_pData;
} GLink;

inline void gn_TieOff(GLink *pLink)
{
	pLink->m_pNext = pLink->m_pPrev = pLink;
}

inline void gn_Insert(GLink *pAfter, GLink *pLink)
{
	pLink->m_pPrev = pAfter;
	pLink->m_pNext = pAfter->m_pNext;
	pLink->m_pPrev->m_pNext = pLink->m_pNext->m_pPrev = pLink;
}

inline void gn_Remove(GLink *pLink)
{
	pLink->m_pPrev->m_pNext = pLink->m_pNext;
	pLink->m_pNext->m_pPrev = pLink->m_pPrev;
}
#endif

class CBindModuleType;

typedef void (*StringShowFn)(const char *pData, void *pUser);

void str_Init();
void str_Term();
void str_ShowAllStringsAllocated(StringShowFn fn, void *pUser);

uint8* str_FormatString(CBindModuleType *hModule, int stringCode, va_list *marker, int *bufferLen);
void str_FreeStringBuffer(uint8 *pBuffer);

HSTRING str_CreateString(uint8 *pBuffer);
HSTRING str_CreateStringAnsi(const char *pString);
HSTRING str_CopyString(HSTRING hString);
void str_FreeString(HSTRING hString);
LTBOOL str_CompareStrings(HSTRING hString1, HSTRING hString2);
LTBOOL str_CompareStringsUpper(HSTRING hString1, HSTRING hString2);
char* str_GetStringData(HSTRING hString);
int str_GetNumStringCharacters(HSTRING hString);
uint8* str_GetStringBytes(HSTRING hString, int *pNumBytes);

#endif
