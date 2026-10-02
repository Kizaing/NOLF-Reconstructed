// Object movement (Talon layout recovered from lithtech.exe; Jupiter shared/src/moveobject.h).
#ifndef __MOVEOBJECT_H__
#define __MOVEOBJECT_H__

#include "ltbasedefs.h"
#include "de_objects.h"

class MoveState;
class WorldTree;
class ILTPhysics;

// MoveObject flags.
#define MO_DETACHSTANDING	(1<<0)
#define MO_SETCHANGEFLAG	(1<<1)
#define MO_MOVESTANDINGONS	(1<<2)
#define MO_TELEPORT			(1<<3)
#define MO_GOTHRUWORLD		(1<<4)	// Don't do world collision.
#define MO_NOSLIDING		(1<<5)	// Don't slide (client MoveObject, MOVEOBJECT_NCTELEPORT).

// The abstraction MoveObject uses to talk to the client or the server.
class MoveAbstract
{
public:
	virtual void			SetObjectChangeFlags(LTObject *pObj, uint32 flags)=0;
	virtual CollisionInfo *&GetCollisionInfo()=0;
	virtual void			DoTouchNotify(LTObject *pMain, LTObject *pTouching, LTVector &stopVel, float force)=0;
	virtual void			PutObjectInContainer(LTObject *pObj, LTObject *pContainer)=0;
	virtual void			BreakContainerLinks(LTObject *pObj)=0;
	virtual void			MoveAttachments(MoveState *pState)=0;
	virtual LTBOOL			ShouldPushObject(MoveState *pState, LTObject *pPusher, LTObject *pPushee)=0;
	virtual void			DoCrush(LTObject *pObject, LTObject *pCrusher)=0;
	virtual void			CheckMaxPos(MoveState *pState, LTVector *pPos)=0;
	virtual uint32			IsServer()=0;
	virtual LTBOOL			CanOptimizeObject(LTObject *pObject)=0;
	virtual char*			GetObjectClassName(LTObject *pObject)=0;
	virtual ILTPhysics*		GetPhysics()=0;
	virtual LTRESULT		GetGlobalForce(LTObject *pObj, LTVector *pForce)=0;	// Talon only
};


// 0x6c bytes.
class MoveState
{
public:
	MoveState()
	{
		m_pWorldTree = LTNULL;
		m_pAbstract = LTNULL;
		m_pObj = LTNULL;
		m_Unknown64 = 0;
		m_nRestart = 0;
	}

	void Setup(WorldTree *pWorldTree,
		MoveAbstract *pAbstract, LTObject *pObj, uint32 bPriority)
	{
		m_pWorldTree = pWorldTree;
		m_pAbstract = pAbstract;
		m_pObj = pObj;
		m_BPriority = bPriority;
		m_CustomTestObjects = LTNULL;
		m_nCustomTestObjects = 0;
	}

// Set these before calling MoveObject.
	WorldTree		*m_pWorldTree;			// 0x00
	MoveAbstract	*m_pAbstract;			// 0x04
	LTObject		*m_pObj;				// 0x08
	uint32			m_BPriority;			// 0x0c What blocking priority are we using?
	LTObject		**m_CustomTestObjects;	// 0x10 Tells it to only test these objects for collision.
	uint32			m_nCustomTestObjects;	// 0x14

// Used internally, don't set.
	uint8			m_Pad18[0x64 - 0x18];
	uint32			m_Unknown64;			// 0x64
	int				m_nRestart;				// 0x68
};

// THE function to move an object (0x0045d5b0).
void MoveObject(MoveState *pState, LTVector moveTo, uint32 flags);

// 0x00461490. Returns TRUE if the object could take the new dimensions.
LTBOOL ChangeObjectDimensions(MoveState *pState, LTVector *pNewDims, uint32 bPushObjects, LTBOOL bUnknown);

// Rotates the world model and moves objects out of the way (0x004619f0).
void RotateWorldModel(MoveState *pState, LTRotation *pNewRot, LTBOOL bForce);

#endif  // __MOVEOBJECT_H__
