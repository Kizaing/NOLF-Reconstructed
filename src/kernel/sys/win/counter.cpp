// Jupiter runtime/kernel/src/sys/win/counter.cpp (Talon: Counter is the CounterFinal class).
// The linker folded StartMicro into StartMS (identical code) and dropped the unreferenced
// CountMicro and CountPercent members other than Clear.
// FLAGS: /O2 /GX-
#include <windows.h>
#include "counter.h"


// GLOBAL: LITHTECH 0x004d1230
static unsigned __int64 g_CountDiv = 1;


// Win2K has a VERY high counter frequency but it's more useful if it's in
// microseconds.
class CountDivSetter
{
public:
	CountDivSetter()
	{
		LARGE_INTEGER perSec;

		QueryPerformanceFrequency(&perSec);
		g_CountDiv = 1;
	}
};
// FUNCTION: LITHTECH 0x00423880 _$E2
// FUNCTION: LITHTECH 0x00423890 _$E1
static CountDivSetter __g_CountDivSetter;



// FUNCTION: LITHTECH 0x004238c0
unsigned long cnt_NumTicksPerSecond()
{
	LARGE_INTEGER perSec;

	QueryPerformanceFrequency(&perSec);
	return (unsigned long)(perSec.QuadPart / g_CountDiv);
}



// FUNCTION: LITHTECH 0x004238f0
Counter::Counter(unsigned long startMode)
{
	if(startMode == CSTART_MICRO)
		StartMicro();
	else if(startMode == CSTART_MILLI)
		StartMS();
}


// FUNCTION: LITHTECH 0x00423920
void Counter::StartMS()
{
	LARGE_INTEGER *pInt;

	pInt = (LARGE_INTEGER*)m_Data;
	QueryPerformanceCounter(pInt);
}

// FUNCTION: LITHTECH 0x00423930
unsigned long Counter::EndMS()
{
	LARGE_INTEGER curCount, *pInCount, perSec;
	LONGLONG timeElapsed;

	pInCount = (LARGE_INTEGER*)m_Data;
	QueryPerformanceCounter(&curCount);

	QueryPerformanceFrequency(&perSec);
	timeElapsed = curCount.QuadPart - pInCount->QuadPart;
	return (unsigned long)((timeElapsed*(LONGLONG)1000) / perSec.QuadPart);
}

// FUNCTION: LITHTECH 0x00423990
unsigned long Counter::CountMS()
{
	return EndMS();
}



// Folded into StartMS (0x00423920).
void Counter::StartMicro()
{
	LARGE_INTEGER *pInt;

	pInt = (LARGE_INTEGER*)m_Data;
	QueryPerformanceCounter(pInt);
}

// FUNCTION: LITHTECH 0x004239a0
unsigned long Counter::EndMicro()
{
	LARGE_INTEGER curCount, *pInCount;

	pInCount = (LARGE_INTEGER*)m_Data;
	QueryPerformanceCounter(&curCount);

	return (unsigned long)((curCount.QuadPart - pInCount->QuadPart) / g_CountDiv);
}

unsigned long Counter::CountMicro()
{
	return EndMicro();
}



// FUNCTION: LITHTECH 0x004239e0
void cnt_StartCounter(Counter &cCounter)
{
	cCounter.StartMicro();
}


// FUNCTION: LITHTECH 0x004239f0
unsigned long cnt_EndCounter(Counter &cCounter)
{
	return cCounter.EndMicro();
}



// FUNCTION: LITHTECH 0x00423a00
void CountPercent::Clear()
{
	m_Finger[0] = 0;
	m_Finger[1] = 0;
	m_TotalIn[0] = 0;
	m_TotalIn[1] = 0;
	m_TotalOut[0] = 0;
	m_TotalOut[1] = 0;

	m_iIn = 0;
}
