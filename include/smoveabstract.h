// Server-side MoveAbstract (Talon layout recovered from lithtech.exe; Jupiter server/src/smoveabstract.h).
#ifndef __SMOVEABSTRACT_H__
#define __SMOVEABSTRACT_H__

#include "moveobject.h"
#include "servermgr.h"

// 8 bytes, vtable 0x004c8658. Talon keeps a pointer to its server manager.
class SMoveAbstract : public MoveAbstract
{
public:
	SMoveAbstract(CServerMgr *pServerMgr)
	{
		m_pServerMgr = pServerMgr;
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
	virtual ILTPhysics*		GetPhysics()	{ return m_pServerMgr->m_pServerInterface->Physics(); }
	virtual LTRESULT		GetGlobalForce(LTObject *pObj, LTVector *pForce);

	CServerMgr		*m_pServerMgr;	// 0x04
};

void FullMoveObject(CServerMgr *pServerMgr, LTObject *pObj, const LTVector *pP1, uint32 flags);

#endif  // __SMOVEABSTRACT_H__
