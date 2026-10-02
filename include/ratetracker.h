// Jupiter runtime/shared/src/ratetracker.h (layout unchanged in Talon).
// Tracks a quantity's increment per second (framerate, bytes per second, ...).
#ifndef __RATETRACKER_H__
#define __RATETRACKER_H__

class RateTracker
{
public:

				RateTracker();

	// Set the cycle time.  0.0f means to never cycle automatically.
	void		Init(float cycleTime);

	float		GetRate()			{return m_Total / m_Seconds;}

	// Add to the total amount (like increment the frame count).
	void		Add(float amount)	{m_Total += amount;}

	void		Update(float timeDelta);

	// Resets the seconds counter (called automatically by Update).
	void		Cycle();


protected:

	float		m_Total;		// 0x00
	float		m_Seconds;		// 0x04
	float		m_CycleTime;	// 0x08  Controls how often it cycles.
};

#endif
