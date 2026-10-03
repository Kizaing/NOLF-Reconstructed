// Jupiter runtime/client/src/cmoveabstract.cpp
// Talon's CMoveAbstract keeps its CClientMgr, notifies the client shell of touches through the
// manager, moves physical attachments and reports the global force (GetGlobalForce is Talon only).
#include "bdefs.h"
#include "cmoveabstract.h"
#include "iclientshell.h"

// 0x004265e0
LTObject* cm_FindObject(CClientMgr *pClientMgr, uint16 objectID);


// Identical-code folded: DoCrush, PutObjectInContainer and CheckMaxPos share this body.
// FUNCTION: LITHTECH 0x00417580
void CMoveAbstract::SetObjectChangeFlags(LTObject *pObj, uint32 flags)
{
}

// FUNCTION: LITHTECH 0x00417590
CollisionInfo *& CMoveAbstract::GetCollisionInfo()
{
	return m_pClientMgr->m_pCollisionInfo;
}

// FUNCTION: LITHTECH 0x004175a0
void CMoveAbstract::DoTouchNotify(LTObject *pMain, LTObject *pTouching, LTVector &stopVel, float forceMag)
{
	if (m_pClientMgr->m_pCollisionInfo)
	{
		m_pClientMgr->m_pCollisionInfo->m_vStopVel = stopVel;
		m_pClientMgr->m_pCollisionInfo->m_hObject = (HOBJECT)pTouching;

		// Only the world has polies.
		if (pTouching && !pTouching->IsMainWorldModel() && !pTouching->HasWorldModel())
		{
			m_pClientMgr->m_pCollisionInfo->m_hPoly = INVALID_HPOLY;
		}

		if (m_pClientMgr->m_pClientShell)
		{
			m_pClientMgr->m_pClientShell->OnTouchNotify((HOBJECT)pMain, m_pClientMgr->m_pCollisionInfo, forceMag);
		}
	}
}

void CMoveAbstract::DoCrush(LTObject *pObject, LTObject *pCrusher)
{
}

void CMoveAbstract::PutObjectInContainer(LTObject *pObj, LTObject *pContainer)
{
}

// FUNCTION: LITHTECH 0x00417630
void CMoveAbstract::BreakContainerLinks(LTObject *pObj)
{
}

// FUNCTION: LITHTECH 0x00417640
void CMoveAbstract::MoveAttachments(MoveState *pState)
{
	Attachment *pAttachment;
	LTObject *pAttachedObj;
	LTVector attachPos, vOffset;
	LTMatrix mat;
	MoveState moveState;

	// Move the attachments.
	pAttachment = pState->m_pObj->m_Attachments;
	while (pAttachment)
	{
		pAttachedObj = cm_FindObject(m_pClientMgr, pAttachment->m_nChildID);

		// Only physical attachments move on the client.
		if (pAttachedObj && (pAttachedObj->m_Flags & (FLAG_SOLID|FLAG_TOUCH_NOTIFY|FLAG_CONTAINER)))
		{
			vOffset = pAttachment->m_Offset.m_Pos;
			quat_ConvertToMatrix(pState->m_pObj->m_Rotation.m_Quat, mat.m);
			mat.Apply3x3(vOffset);
			attachPos = pState->m_pObj->GetPos() + vOffset;

			moveState.Setup(pState->m_pWorldTree, pState->m_pAbstract, pAttachedObj, pState->m_BPriority);
			MoveObject(&moveState, attachPos, MO_DETACHSTANDING | MO_MOVESTANDINGONS);

			// Update its rotation..
			LTRotation newRot = pState->m_pObj->m_Rotation * pAttachment->m_Offset.m_Rot;
			if (!newRot.Equals(pAttachedObj->m_Rotation, 0.00001f))
			{
				if (pAttachedObj->HasWorldModel())
				{
					moveState.Setup(pState->m_pWorldTree, pState->m_pAbstract, pAttachedObj, pAttachedObj->m_BPriority);
					RotateWorldModel(&moveState, &newRot, LTTRUE);
				}
				else
				{
					pAttachedObj->m_Rotation = newRot;
				}
			}
		}

		pAttachment = pAttachment->m_pNext;
	}
}

// FUNCTION: LITHTECH 0x004179a0
LTBOOL CMoveAbstract::ShouldPushObject(MoveState *pState, LTObject *pPusher, LTObject *pPushee)
{
	return pState->m_pObj == pPusher && pState->m_BPriority > pPushee->m_BPriority && pPushee->IsMoveable();
}

void CMoveAbstract::CheckMaxPos(MoveState *pState, LTVector *pPos)
{
}

// Identical-code folded with ic_EndCounter (0x0043dac0).
uint32 CMoveAbstract::IsServer()
{
	return LTFALSE;
}

// FUNCTION: LITHTECH 0x004179e0
LTBOOL CMoveAbstract::CanOptimizeObject(LTObject *pObj)
{
	return LTFALSE;
}

// FUNCTION: LITHTECH 0x004179f0
char* CMoveAbstract::GetObjectClassName(LTObject *pObject)
{
	return "CLIENT-OBJECT";
}

// FUNCTION: LITHTECH 0x00417a00
LTRESULT CMoveAbstract::GetGlobalForce(LTObject *pObj, LTVector *pForce)
{
	if (!pObj)
	{
		if (pForce)
			*pForce = m_pClientMgr->m_MotionState.m_Info.m_Force;

		return LT_OK;
	}

	if (pForce)
		*pForce = m_pClientMgr->m_MotionState.m_Info.m_Force;

	return LT_OK;
}

// Out-of-line copies of SDK inlines.
// FUNCTION: LITHTECH 0x004178b0 ?quat_Mul@@YAXPAMPBM1@Z
// FUNCTION: LITHTECH 0x00417940 ?Apply3x3@LTMatrix@@QAEXABV?$_CVector@M@@AAV2@@Z
