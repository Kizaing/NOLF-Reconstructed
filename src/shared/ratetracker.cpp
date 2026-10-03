// Jupiter runtime/shared/src/ratetracker.cpp
// FLAGS: /O2 /GX-
#include "ratetracker.h"


// FUNCTION: LITHTECH 0x0046eab0
RateTracker::RateTracker()
{
	m_Seconds = 0.0001f;
	m_CycleTime = 2.0f;
	m_Total = 0.0f;
}


void RateTracker::Init(float cycleTime)
{
	m_CycleTime = cycleTime;
}


// FUNCTION: LITHTECH 0x0046ead0
void RateTracker::Update(float timeDelta)
{
	m_Seconds += timeDelta;

	if(m_CycleTime != 0.0f && m_Seconds > m_CycleTime)
	{
		Cycle();
	}
}


// FUNCTION: LITHTECH 0x0046eb10
void RateTracker::Cycle()
{
	float curRate;

	curRate = GetRate();
	m_Seconds = 0.1f;
	m_Total = curRate * m_Seconds;
}
