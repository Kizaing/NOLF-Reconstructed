// Client-side MoveAbstract (Jupiter runtime/client/src/cmoveabstract.h). Talon keeps a pointer to
// its client manager; CClientMgr constructs one in cm_Init (vtable 0x004c6ba4, emitted there).
#ifndef __CMOVEABSTRACT_H__
#define __CMOVEABSTRACT_H__

#include "moveobject.h"
#include "clientmgr.h"
#include "iltclient.h"

// 8 bytes.
class CMoveAbstract : public MoveAbstract
{
public:
	CMoveAbstract(CClientMgr *pClientMgr)
	{
		m_pClientMgr = pClientMgr;
	}

	virtual void			SetObjectChangeFlags(LTObject *pObj, uint32 flags);
	virtual CollisionInfo *&GetCollisionInfo();
	virtual void			DoTouchNotify(LTObject *pMain, LTObject *pTouching, LTVector &stopVel, float forceMag);
	virtual void			PutObjectInContainer(LTObject *pObj, LTObject *pContainer);
	virtual void			BreakContainerLinks(LTObject *pObj);
	virtual void			MoveAttachments(MoveState *pState);
	virtual LTBOOL			ShouldPushObject(MoveState *pState, LTObject *pPusher, LTObject *pPushee);
	virtual void			DoCrush(LTObject *pObject, LTObject *pCrusher);
	virtual void			CheckMaxPos(MoveState *pState, LTVector *pPos);
	virtual uint32			IsServer();
	virtual LTBOOL			CanOptimizeObject(LTObject *pObject);
	virtual char*			GetObjectClassName(LTObject *pObject);
	virtual ILTPhysics*		GetPhysics()	{ return m_pClientMgr->m_pClientDE->Physics(); }	// 0x00411710
	virtual LTRESULT		GetGlobalForce(LTObject *pObj, LTVector *pForce);

	CClientMgr		*m_pClientMgr;	// 0x04
};

#endif  // __CMOVEABSTRACT_H__
