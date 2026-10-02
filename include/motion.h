// Talon physics motion (Jupiter runtime/shared/src/motion.h).  Talon's CalcMotion takes
// all of its inputs and its displacement output in one structure.
#ifndef __MOTION_H__
#define __MOTION_H__

#include "ltbasedefs.h"
#include "de_objects.h"


// Gravity info (Jupiter shared/src/motion.h), 0x20 bytes.
struct MotionInfo
{
	LTVector	m_Force, m_UnitForce;	// 0x00, 0x0c
	float		m_ForceMag;				// 0x18
	float		m_SlideRatio;			// 0x1c

	void SetForce(const LTVector *pForce)
	{
		m_Force = *pForce;
		m_ForceMag = m_Force.Mag();
		if(m_ForceMag > 0.00001f)
		{
			m_UnitForce = m_Force;
			m_UnitForce /= m_ForceMag;
		}
		else
		{
			m_UnitForce.Init();
		}
	}
};

// CalcMotion input/output (0x40 bytes).
struct MotionState
{
	LTObject	*m_pObj;			// 0x00
	float		m_dt;				// 0x04 time step
	uint32		m_Flags;			// 0x08 object flags (FLAG_GRAVITY)
	LTVector	*m_pVelocity;		// 0x0c
	LTVector	*m_pAcceleration;	// 0x10
	MotionInfo	m_Info;				// 0x14
	LTVector	m_Offset;			// 0x34 displacement (output)
};

// Returns FALSE if the object stopped and its physics were disabled.
LTBOOL CalcMotion(MotionState *pState);

#endif
