// LT timers (Jupiter runtime/shared/src/lttimer.h). Methods at 0x0044d0c0-0x0044d190.
// Client::m_Timer (servermgr.h) is one.
#ifndef __LTTIMER_H__
#define __LTTIMER_H__

#include "counter.h"

class LTTimer
{
public:

	LTTimer();							// 0x0044d0c0

	void	SetUpdateRate(float rate);	// 0x0044d0e0
	float	GetUpdateRate();			// 0x0044d100

	// Returns TRUE if the timer has 'fired'.
	LTBOOL	Update();					// 0x0044d110

protected:

	Counter	m_Counter;		// 0x00
	float	m_UpdateRate;	// 0x08
	float	m_Adjust;		// 0x0c
};

#endif  // __LTTIMER_H__
