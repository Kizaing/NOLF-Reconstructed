// Jupiter runtime/client/src/memorywatch.cpp
#include "bdefs.h"

// GLOBAL: LITHTECH 0x004e4520
unsigned long g_dwSoundMemory;


// FUNCTION: LITHTECH 0x0044db60
void mw_ResetWatches()
{
	g_dwSoundMemory = 0;
}
