// Profiling counters (Jupiter runtime/kernel/src/sys/win/counter.h, non-final build).
// Talon keeps the counter functions out of line in the 00423880 gap.
#ifndef __COUNTER_H__
#define __COUNTER_H__

#include "ltbasetypes.h"

// You can start a counter in its constructor with these.
#define CSTART_NONE			0
#define CSTART_MILLI		1
#define CSTART_MICRO		2

class Counter
{
public:
	// 0x004238f0
	Counter(unsigned long startMode=CSTART_NONE);

	void			StartMS();
	unsigned long	EndMS();
	unsigned long	CountMS();

	void			StartMicro();
	unsigned long	EndMicro();
	unsigned long	CountMicro();

	unsigned long m_Data[2];
};

// How many ticks per second are there?
unsigned long cnt_NumTicksPerSecond();

// Start a counter. 0x004239e0 (Ghidra: LTSNPrintF)
void cnt_StartCounter(Counter &cCounter);

// Returns the number of ticks since you called StartCounter. 0x004239f0
unsigned long cnt_EndCounter(Counter &cCounter);


// C++ helpers..
class CountAdder
{
	public:

		CountAdder(uint32 *pNum)
		{
			m_pNum = pNum;
			cnt_StartCounter(m_Counter);
		}

		~CountAdder()
		{
			*m_pNum += cnt_EndCounter(m_Counter);
		}

		Counter m_Counter;
		uint32 *m_pNum;
};


// Measures the percentage of time spent inside a profiled section.
class CountPercent
{
public:
	CountPercent() : m_iDelayCount(0) { Clear(); }

	float	CalcPercent();
	// Clears the totals. 0x00423a00
	void	Clear();

	// Call to enter / exit the profiled section.
	uint32	In();
	uint32	Out();

	unsigned long m_Finger[2];		// 0x00
	unsigned long m_TotalIn[2];		// 0x08
	unsigned long m_TotalOut[2];	// 0x10
	uint32	m_iIn;					// 0x18
	int		m_iDelayCount;			// 0x1c
};

#endif  // __COUNTER_H__
