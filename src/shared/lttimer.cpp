// Jupiter runtime/shared/src/lttimer.cpp
// FLAGS: /O2 /GX-
#include <math.h>
#include "bdefs.h"
#include "lttimer.h"



// FUNCTION: LITHTECH 0x0044d0c0
LTTimer::LTTimer()
{
	m_UpdateRate = 0.0f;
	m_Adjust = 0.0f;
}


// FUNCTION: LITHTECH 0x0044d0e0
void LTTimer::SetUpdateRate(float rate)
{
	m_UpdateRate = rate;
	m_Adjust = 0.0f;
	m_Counter.StartMS();
}


// FUNCTION: LITHTECH 0x0044d100
float LTTimer::GetUpdateRate()
{
	return m_UpdateRate;
}


// FUNCTION: LITHTECH 0x0044d110
LTBOOL LTTimer::Update()
{
	float secondsElapsed, invUpdateRate;

	if(m_UpdateRate == 0.0f)
		return LTFALSE;

	invUpdateRate = 1.0f / m_UpdateRate;

	secondsElapsed = (m_Counter.CountMS() / 1000.0f) + m_Adjust;
	if(secondsElapsed > invUpdateRate)
	{
		m_Adjust = (float)fmod(secondsElapsed, invUpdateRate);
		m_Counter.StartMS();
		return LTTRUE;
	}
	else
	{
		return LTFALSE;
	}
}
