// Jupiter runtime/kernel/src/sys/win/timemgr.cpp
// Talon's TimeInit only records the time base (no timeBeginPeriod).
#include <windows.h>
#include <mmsystem.h>
#include "bdefs.h"


// GLOBAL: LITHTECH 0x004e6228
static uint32 g_TimeBase;


// It does this so the timer won't start out at really large numbers (otherwise, the numbers
// will be less accurate and the timer will recycle faster).
// At the windows timer rate, it would take 49.71 days for the timer to recycle itself.
class TimeInit
{
	public:

		TimeInit()
		{
			g_TimeBase = timeGetTime();
		}
};

// FUNCTION: LITHTECH 0x0049c110 _$E2
// FUNCTION: LITHTECH 0x0049c120 _$E1
static TimeInit _g_TimeInit;


// FUNCTION: LITHTECH 0x0049c130
float time_GetTime()
{
	return (float)(timeGetTime() - g_TimeBase) * (1.0f / 1000.0f);
}
