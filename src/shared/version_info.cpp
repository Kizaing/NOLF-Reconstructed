// Jupiter runtime/shared/src/version_info.cpp
// FLAGS: /O2 /GX-
// Talon uses plain sprintf where Jupiter has LTSNPrintF.
#include <stdio.h>
#include "ltbasedefs.h"
#include "version_info.h"


// FUNCTION: LITHTECH 0x0049cf50
void LTVersionInfo::GetString(char *pStr, uint32 nStrBytes)
{
	char tempStr[512];

	sprintf(tempStr, "%lu.%lu", m_MajorVersion, m_MinorVersion);
	LTStrCpy(pStr, tempStr, nStrBytes);
}
