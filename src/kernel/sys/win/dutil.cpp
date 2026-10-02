// Jupiter runtime/kernel/src/sys/win/dutil.cpp (Talon has no du_strupr).
// FLAGS: /O2 /GX-
#include "bdefs.h"
#include "dutil.h"

inline char du_Toupper(char theChar)
{
	if(theChar >= 'a' && theChar <= 'z')
		return theChar - ('a' - 'A');
	else
		return theChar;
}


// FUNCTION: LITHTECH 0x00435960
int du_UpperStrcmp(const char *pStr1, const char *pStr2)
{
	for(;;)
	{
		if(du_Toupper(*pStr1) != du_Toupper(*pStr2))
			return 0;

		if(*pStr1 == 0)
			return 1;

		++pStr1;
		++pStr2;
	}

	return 0;
}
