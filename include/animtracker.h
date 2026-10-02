// Talon animation tracker (SDK ltanimtracker.h holds the structures; Jupiter model/src/animtracker.h).
#ifndef __ANIMTRACKER_H__
#define __ANIMTRACKER_H__

#include "model.h"
#include "ltanimtracker.h"

#define NOT_INTERPOLATING   0xFFFF

#define AT_PLAYING      (1<<0)  // Currently playing.
#define AT_LOOPING      (1<<1)
#define AT_DOCALLBACKS  (1<<2)  // Should this tracker do callbacks?
#define AT_ALLOWINVALID (1<<3)  // Allow invalid animations (IsValid returns FALSE).

void	trk_Init(LTAnimTracker *pTracker, Model *pModel, uint32 iAnim);
void	trk_Update(LTAnimTracker *pTracker, uint32 msDelta);
LTBOOL	trk_IsStopped(LTAnimTracker *pTracker);
void	trk_SetCurTime(LTAnimTracker *pTracker, uint32 msTime, LTBOOL bTransition);
LTBOOL	trk_SetCurAnim(LTAnimTracker *pTracker, uint32 iAnim, LTBOOL bTransition);
void	trk_Reset(LTAnimTracker *pTracker);
void	trk_SetAtKeyFrame(LTAnimTracker *pTracker, uint32 msTime);
void	trk_ScanToKeyFrame(LTAnimTracker *pTracker, uint32 msDelta, LTBOOL bProcessKeys);

// Written out (not via m_TimeRef.IsValid()) so the model and anim index loads are shared
// with GetCurAnim() in the callers.
inline LTBOOL LTAnimTracker::IsValid()
{
	return GetModel() &&
		m_TimeRef.m_Cur.m_iAnim < GetModel()->NumAnims() &&
		m_TimeRef.m_Prev.m_iAnim < GetModel()->NumAnims();
}

inline ModelAnim* LTAnimTracker::GetCurAnim()
{
	return GetModel()->GetAnim(m_TimeRef.m_Cur.m_iAnim);
}

inline ModelAnim* LTAnimTracker::GetPrevAnim()
{
	return GetModel()->GetAnim(m_TimeRef.m_Prev.m_iAnim);
}

inline AnimKeyFrame* LTAnimTracker::GetCurFrame()
{
	return &GetCurAnim()->m_KeyFrames[m_TimeRef.m_Cur.m_iFrame];
}

inline AnimKeyFrame* LTAnimTracker::GetPrevFrame()
{
	return &GetPrevAnim()->m_KeyFrames[m_TimeRef.m_Prev.m_iFrame];
}

inline LTBOOL LTAnimTracker::AllowInvalid()
{
	return !!(m_Flags & AT_ALLOWINVALID);
}

#endif
