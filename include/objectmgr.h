// Talon object manager (Jupiter runtime/shared/src/objectmgr.h). 0x2d0 bytes.
// CServerMgr embeds one at 0x394 (its m_ObjectLists are at CServerMgr+0x5b4).
#ifndef __OBJECTMGR_H__
#define __OBJECTMGR_H__

#include "de_objects.h"
#include "../../build/proj/LT2/lithshared/stdlith/object_bank.h"

#define OBJECT_PREALLOCATIONS   32
#define MODEL_PREALLOCATIONS    256
#define SPRITE_PREALLOCATIONS   128

// vtable 0x004c7dec (both slots pure).
class WorldTreeHelper
{
public:
	virtual uint32 IncFrameCode()=0;
	virtual uint32 GetFrameCode()=0;
};

// vtable 0x004c7de4.
class ObjectMgr : public WorldTreeHelper
{
public:
	ObjectMgr();							// 0x00466650

	virtual uint32 IncFrameCode();			// 0x00466800
	virtual uint32 GetFrameCode();

public:
	uint32		m_CurFrameCode;				// 0x04
	LTLink		m_InternalLink;				// 0x08 Used by the ObjectMgr module.

	StructBank	m_ParticleBank;				// 0x14
	StructBank	m_LineBank;					// 0x30
	StructBank	m_AttachmentBank;			// 0x4c

	ObjectBank<LTObject>			m_ObjectBankNormal;			// 0x68
	ObjectBank<ModelInstance>		m_ObjectBankModel;			// 0x8c
	ObjectBank<WorldModelInstance>	m_ObjectBankWorldModel;		// 0xb0
	ObjectBank<SpriteInstance>		m_ObjectBankSprite;			// 0xd4
	ObjectBank<DynamicLight>		m_ObjectBankLight;			// 0xf8
	ObjectBank<CameraInstance>		m_ObjectBankCamera;			// 0x11c
	ObjectBank<LTParticleSystem>	m_ObjectBankParticleSystem;	// 0x140
	ObjectBank<LTPolyGrid>			m_ObjectBankPolyGrid;		// 0x164
	ObjectBank<LineSystem>			m_ObjectBankLineSystem;		// 0x188
	ObjectBank<ContainerInstance>	m_ObjectBankContainer;		// 0x1ac
	ObjectBank<Canvas>				m_ObjectBankCanvas;			// 0x1d0

	BaseObjectBank	*m_ObjectBankPointers[NUM_OBJECTTYPES];		// 0x1f4

	LTList		m_ObjectLists[NUM_OBJECTTYPES];					// 0x220
};

LTRESULT om_Init(ObjectMgr *pMgr, LTBOOL bClient);
LTRESULT om_Term(ObjectMgr *pMgr);
LTRESULT om_CreateObject(ObjectMgr *pMgr, ObjectCreateStruct *pStruct, LTObject **ppObject);
LTRESULT om_DestroyObject(ObjectMgr *pMgr, LTObject *pObject);
LTRESULT om_CreateAttachment(ObjectMgr *pMgr, LTObject *pParent, uint16 nChildID, int iSocket,
	LTVector *pOffset, LTRotation *pRotationOffset, Attachment **ppAttachment);
LTRESULT om_RemoveAttachment(ObjectMgr *pMgr, LTObject *pParent, Attachment *pAttachment);
void om_ClearSerializeIDs(ObjectMgr *pMgr);

inline void om_RemoveAttachments(ObjectMgr *pMgr, LTObject *pObj)
{
	Attachment *pCur, *pNext;

	pCur = pObj->m_Attachments;
	while (pCur)
	{
		pNext = pCur->m_pNext;
		sb_Free(&pMgr->m_AttachmentBank, pCur);
		pCur = pNext;
	}
	pObj->m_Attachments = LTNULL;
}

#endif  // __OBJECTMGR_H__
