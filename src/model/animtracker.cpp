// Jupiter runtime/model/src/animtracker.cpp (Talon version: time scale, weight sets, key types).
#include "animtracker.h"
#include "transformmaker.h"

static void trk_ProcessKey(LTAnimTracker *pTracker, ModelAnim *pAnim, uint32 iFrame);
static AnimKeyFrame* trk_NextPositionFrame(LTAnimTracker *pTracker, uint32 iStart, uint32 &iNextFrame);

// 0x00401000 is the linker's one `vector constructor iterator': this is the first object linked, so the original
// animtracker.obj had a ??_H COMDAT. VC6 emits ??_H (unreferenced) for an in-class inline constructor of a class with
// an array member of a class with a constructor (TransformMaker::m_Anims, netmgr.h's CPacketRef m_Fragments[]), so
// the original included some such header here; transformmaker.h (same module) is the likeliest.
// FUNCTION: LITHTECH 0x00401000 ??_H@YGXPAXIHP6EX0@Z@Z


// FUNCTION: LITHTECH 0x00401030
void LTAnimTracker::SetupTimeRef(AnimTimeRef *pRef)
{
	*pRef = m_TimeRef;
}


//  ----------------------------------------------------------------
//  Initialize the tracker with animation from model
//  ----------------------------------------------------------------
// Remaining diff: register allocation in the pModel block (orig: pModel esi, iAnim edx, flags bx).
// Also tried: Jupiter's chained iAnim/iFrame stores, local flags copy, nested AllowInvalid ifs (no change).
// STUB: LITHTECH 0x00401050
void trk_Init(LTAnimTracker *pTracker, Model *pModel, uint32 iAnim)
{
	pTracker->m_StringKeyCallback = LTNULL;
	pTracker->m_pUser1 = LTNULL;
	pTracker->SetModel(pModel);
	pTracker->m_CurKey = 0;
	pTracker->m_Flags = 0;
	pTracker->m_InterpolationMS = 0;
	pTracker->m_TimeScaleNum = 1;
	pTracker->m_TimeScaleDenom = 1;
	pTracker->m_bAllowInterpolation = LTTRUE;
	pTracker->m_TimeRef.m_Prev.Clear();
	pTracker->m_TimeRef.m_Cur.Clear();
	pTracker->m_TimeRef.m_Percent = 0.0f;

	if(pModel)
	{
		if(!pTracker->AllowInvalid() && (iAnim >= pModel->NumAnims()))
			iAnim = 0;

		pTracker->m_TimeRef.m_Prev.m_iAnim = pTracker->m_TimeRef.m_Cur.m_iAnim = (uint16)iAnim;
		pTracker->m_Flags |= AT_PLAYING;
		pTracker->m_TimeRef.m_Prev.m_iFrame = pTracker->m_TimeRef.m_Cur.m_iFrame = 0;
		pTracker->m_InterpolationMS = NOT_INTERPOLATING;
	}

	pTracker->m_Flags |= AT_LOOPING;
}


//  ----------------------------------------------------------------
//  Increment the tracker by dt
//  ----------------------------------------------------------------
// FUNCTION: LITHTECH 0x004010f0
void trk_Update(LTAnimTracker *pTracker, uint32 msDelta)
{
	if(pTracker->m_Flags & AT_PLAYING)
	{
		// Scale the time by the tracker's rate.
		msDelta *= pTracker->m_TimeScaleNum;
		if(pTracker->m_TimeScaleDenom == 0)
			pTracker->m_TimeScaleDenom = 1;

		trk_ScanToKeyFrame(pTracker, msDelta / pTracker->m_TimeScaleDenom, LTTRUE);
	}
}


// FUNCTION: LITHTECH 0x00401130
LTBOOL trk_IsStopped(LTAnimTracker *pTracker)
{
	if(pTracker->IsValid())
	{
		ModelAnim *pAnim = pTracker->GetCurAnim();

		if(!(pTracker->m_Flags & AT_LOOPING))
		{
			if(pAnim->m_KeyFrames.GetSize() <= 1)
				return LTTRUE;

			if(pTracker->m_TimeRef.m_Cur.m_Time == pAnim->m_KeyFrames[pAnim->m_KeyFrames.GetSize() - 1].m_Time + 1)
				return LTTRUE;
		}
	}

	return LTFALSE;
}


// FUNCTION: LITHTECH 0x004011a0
void trk_SetCurTime(LTAnimTracker *pTracker, uint32 msTime, LTBOOL bTransition)
{
	// If we're not transitioning, then prev should move to the same keyframe as cur.
	if(!pTracker->IsValid() || !bTransition)
	{
		pTracker->m_TimeRef.m_Cur.m_Time = 0;
		pTracker->m_TimeRef.m_Prev.m_Time = 0;
		pTracker->m_TimeRef.m_Cur.m_iFrame = 0;
		pTracker->m_TimeRef.m_Prev.m_iFrame = 0;
		pTracker->m_CurKey = 0;
	}

	pTracker->m_TimeRef.m_Cur.m_Time = 0;
	pTracker->m_TimeRef.m_Cur.m_iFrame = 0;
	pTracker->m_CurKey = 0;

	trk_ScanToKeyFrame(pTracker, msTime, LTFALSE);
}


// FUNCTION: LITHTECH 0x00401200
LTBOOL trk_SetCurAnim(LTAnimTracker *pTracker, uint32 iAnim, LTBOOL bTransition)
{
	if(!pTracker->AllowInvalid())
	{
		if(iAnim >= pTracker->GetModel()->NumAnims())
			return LTFALSE;
	}

	// The previous frame keeps its weight set.
	uint32 iWeightSet = pTracker->m_TimeRef.m_Prev.m_iWeightSet;
	pTracker->m_TimeRef.m_Prev = pTracker->m_TimeRef.m_Cur;
	pTracker->m_TimeRef.m_Prev.m_iWeightSet = iWeightSet;

	pTracker->m_TimeRef.m_Percent = 0.0f;
	pTracker->m_TimeRef.m_Cur.m_iAnim = (uint16)iAnim;
	pTracker->m_CurKey = 0;
	pTracker->m_TimeRef.m_Cur.m_iFrame = 0;
	pTracker->m_TimeRef.m_Cur.m_Time = 0;

	if(bTransition)
	{
		ModelAnim *pAnim = pTracker->GetModel()->GetAnim(iAnim);
		if(pAnim)
			bTransition = pAnim->m_InterpolationMS > 0;
	}

	if(bTransition && pTracker->IsValid() && pTracker->m_bAllowInterpolation)
	{
		// Start the interpolation.
		pTracker->m_InterpolationMS = 0;
	}
	else
	{
		pTracker->m_InterpolationMS = NOT_INTERPOLATING;
		pTracker->m_TimeRef.m_Prev = pTracker->m_TimeRef.m_Cur;
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x004012e0
void trk_Reset(LTAnimTracker *pTracker)
{
	pTracker->m_TimeRef.m_Prev.m_iAnim		= pTracker->m_TimeRef.m_Cur.m_iAnim;
	pTracker->m_TimeRef.m_Prev.m_iWeightSet	= pTracker->m_TimeRef.m_Cur.m_iWeightSet;
	pTracker->m_CurKey = 0;
	pTracker->m_TimeRef.m_Cur.m_iFrame	= 0;
	pTracker->m_TimeRef.m_Prev.m_iFrame	= 0;
	pTracker->m_TimeRef.m_Cur.m_Time	= 0;
	pTracker->m_TimeRef.m_Prev.m_Time	= 0;
	pTracker->m_TimeRef.m_Percent = 0.0f;
}


// ----------------------------------------------------------------
// UpdatePositionInterpolant( tracker )
// Finds the position between key frames.
// ----------------------------------------------------------------
inline void trk_UpdatePositionInterpolant(LTAnimTracker *pTracker)
{
	AnimKeyFrame *pPrevFrame, *pCurFrame;

	if(pTracker->m_TimeRef.m_Prev.m_iFrame != pTracker->m_TimeRef.m_Cur.m_iFrame && pTracker->IsValid())
	{
		pPrevFrame = pTracker->GetPrevFrame();
		pCurFrame = pTracker->GetCurFrame();

		pTracker->m_TimeRef.m_Percent =
			(float)(pTracker->m_TimeRef.m_Cur.m_Time - pPrevFrame->m_Time) /
			(pCurFrame->m_Time - pPrevFrame->m_Time);
	}
	else
	{
		pTracker->m_TimeRef.m_Percent = 0.0f;
	}
}


// ----------------------------------------------------------------
// SetAtKeyFrame( tracker, time );
// sets the animation to the keyframe closest to msTime
// ----------------------------------------------------------------
// FUNCTION: LITHTECH 0x00401310
void trk_SetAtKeyFrame(LTAnimTracker *pTracker, uint32 msTime)
{
	trk_Reset(pTracker);

	ModelAnim *pCurAnim = pTracker->GetCurAnim();

	if(pCurAnim->m_KeyFrames.GetSize() <= 1)
		return;

	uint32 iEndKey = pCurAnim->m_KeyFrames.GetSize() - 1;

	uint32 endTime = pCurAnim->m_KeyFrames[iEndKey].m_Time;
	if(msTime > endTime)
	{
		msTime = endTime;
	}

	if(!(pTracker->m_Flags & AT_LOOPING))
	{
		pTracker->m_TimeRef.m_Cur.m_Time = LTMIN(pTracker->m_TimeRef.m_Cur.m_Time, (endTime+1));
	}

	uint32 lastkeyTime = 0;
	uint32 keyTime;
	while(pTracker->m_CurKey <= iEndKey)
	{
		keyTime = pCurAnim->m_KeyFrames[pTracker->m_CurKey].m_Time;
		if(msTime <= keyTime)
		{
			pTracker->m_TimeRef.m_Prev.m_iFrame = (uint16)pTracker->m_CurKey > 0 ? (uint16)pTracker->m_CurKey-1 : 0;
			pTracker->m_TimeRef.m_Cur.m_iFrame = (uint16)pTracker->m_CurKey;
			if((keyTime - lastkeyTime) == 0)
			{
				pTracker->m_TimeRef.m_Percent = 0.0f;
			}
			else
			{
				pTracker->m_TimeRef.m_Prev.m_Time = lastkeyTime;
				pTracker->m_TimeRef.m_Cur.m_Time = msTime;
				pTracker->m_TimeRef.m_Percent = (LTFLOAT)(msTime - lastkeyTime)/(LTFLOAT)(keyTime - lastkeyTime);
			}

			break;
		}

		lastkeyTime = keyTime;
		++pTracker->m_CurKey;
	}
}


//  ----------------------------------------------------------------
//  ScanToKeyFrame
//  ----------------------------------------------------------------
// FUNCTION: LITHTECH 0x00401400
void trk_ScanToKeyFrame(LTAnimTracker *pTracker, uint32 msDelta, LTBOOL bProcessKeys)
{
	if(!pTracker->IsValid())
	{
		pTracker->m_TimeRef.m_Cur.m_Time += msDelta;
		return;
	}

	ModelAnim *pCurAnim = pTracker->GetCurAnim();

	// Are we interpolating between two animations?
	if(pTracker->m_InterpolationMS != NOT_INTERPOLATING)
	{
		pTracker->m_InterpolationMS += msDelta;
		if(pTracker->m_InterpolationMS > pCurAnim->m_InterpolationMS)
		{
			trk_Reset(pTracker);
			msDelta = pTracker->m_InterpolationMS - pCurAnim->m_InterpolationMS;
			pTracker->m_InterpolationMS = NOT_INTERPOLATING;
		}
		else
		{
			if(pCurAnim->m_InterpolationMS)
				pTracker->m_TimeRef.m_Percent = (float)pTracker->m_InterpolationMS / pCurAnim->m_InterpolationMS;
			else
				pTracker->m_TimeRef.m_Percent = 0.0f;

			return;
		}
	}

	if(pCurAnim->m_KeyFrames.GetSize() <= 1)
	{
		pTracker->m_CurKey = 0;
		pTracker->m_TimeRef.m_Cur.m_Time = 0;
		pTracker->m_TimeRef.m_Percent = 0.0f;
		return;
	}

	uint32 iEndKey = pCurAnim->m_KeyFrames.GetSize() - 1;
	uint32 endTime = pCurAnim->m_KeyFrames[iEndKey].m_Time;

	pTracker->m_TimeRef.m_Prev.m_Time = pTracker->m_TimeRef.m_Cur.m_Time;
	pTracker->m_TimeRef.m_Cur.m_Time += msDelta;

	if(!(pTracker->m_Flags & AT_LOOPING))
	{
		pTracker->m_TimeRef.m_Cur.m_Time = LTMIN(pTracker->m_TimeRef.m_Cur.m_Time, (endTime+1));
	}

	while(pTracker->m_CurKey <= iEndKey)
	{
		if(pTracker->m_TimeRef.m_Cur.m_Time <= pCurAnim->m_KeyFrames[pTracker->m_CurKey].m_Time)
			break;

		if(bProcessKeys)
			trk_ProcessKey(pTracker, pCurAnim, pTracker->m_CurKey);

		++pTracker->m_CurKey;
	}

	if(pTracker->m_CurKey == (iEndKey+1) && (pTracker->m_Flags & AT_LOOPING))
	{
		pTracker->m_TimeRef.m_Cur.m_Time %= endTime;
		pTracker->m_CurKey = 0;
	}

	if(!bProcessKeys)
	{
		pTracker->m_TimeRef.m_Prev.m_iFrame = pTracker->m_TimeRef.m_Cur.m_iFrame = (uint16)pTracker->m_CurKey;
	}

	trk_UpdatePositionInterpolant(pTracker);
}


// ----------------------------------------------------------------
// ProcessKey
// Process keyframes that have callbacks associated with them.
// ----------------------------------------------------------------
// FUNCTION: LITHTECH 0x00401600
static void trk_ProcessKey(LTAnimTracker *pTracker, ModelAnim *pAnim, uint32 iFrame)
{
	AnimKeyFrame *pNextPosition;
	uint32 iNextPosition;
	AnimKeyFrame *pFrame;

	pFrame = &pAnim->m_KeyFrames[iFrame];

	if(pFrame->m_KeyType == KEYTYPE_CALLBACK)
	{
		if((pTracker->m_Flags & AT_DOCALLBACKS) && pFrame->m_Callback)
			pFrame->m_Callback(pTracker, pFrame);
	}
	else if(pFrame->m_KeyType == KEYTYPE_POSITION)
	{
		// Is there a string for this key?
		if(pFrame->m_pString[0] != 0 && pTracker->m_StringKeyCallback)
			pTracker->m_StringKeyCallback(pTracker, pFrame, LTNULL, LTFALSE);

		// Notify the end of the animation.
		if(iFrame == pAnim->m_KeyFrames.GetSize() - 1 && pTracker->m_StringKeyCallback && pFrame->m_Time > 1)
			pTracker->m_StringKeyCallback(pTracker, pFrame, "LTAnim_End", LTTRUE);

		pNextPosition = trk_NextPositionFrame(pTracker, iFrame+1, iNextPosition);
		if(pNextPosition)
		{
			if(pTracker->m_TimeRef.m_Cur.m_Time <= pNextPosition->m_Time)
			{
				pTracker->m_TimeRef.m_Prev.m_iFrame = (uint16)iFrame;
				pTracker->m_TimeRef.m_Cur.m_iFrame = pNextPosition - pAnim->m_KeyFrames.GetArray();
			}
			else
			{
				pTracker->m_TimeRef.m_Prev.m_iFrame = pTracker->m_TimeRef.m_Cur.m_iFrame =
					pNextPosition - pAnim->m_KeyFrames.GetArray();
			}
		}
		else
		{
			pTracker->m_TimeRef.m_Prev.m_iFrame = pTracker->m_TimeRef.m_Cur.m_iFrame = (uint16)iFrame;
		}
	}
}


// FUNCTION: LITHTECH 0x00401700
static AnimKeyFrame* trk_NextPositionFrame(LTAnimTracker *pTracker, uint32 iStart, uint32 &iNextFrame)
{
	uint32 i;

	ModelAnim *pAnim = pTracker->GetCurAnim();
	if(pAnim)
	{
		for(i=iStart; i < pAnim->m_KeyFrames.GetSize(); i++)
		{
			if(pAnim->m_KeyFrames[i].m_KeyType == KEYTYPE_POSITION)
			{
				iNextFrame = i;
				return &pAnim->m_KeyFrames[i];
			}
		}
	}

	return LTNULL;
}
