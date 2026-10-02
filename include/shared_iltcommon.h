// Talon shared ILTCommon implementation (Jupiter runtime/shared/src/shared_iltcommon.h, CLTCommonShared).
// The client (ClientCommonLT) and server derive from it. Methods live at 0x0043c000-0x0043d110.
#ifndef __SHARED_ILTCOMMON_H__
#define __SHARED_ILTCOMMON_H__

#include "ltbasedefs.h"
#include "iltcommon.h"

class ILTMath;

// vtable 0x004c74d8.
class CommonLT : public ILTCommon
{
public:
	CommonLT();								// 0x0043c000 (out of line)

	void		SetMathLT(ILTMath *pMathLT);	// 0x0043c010

	virtual LTRESULT GetObjectFlags(const HOBJECT hObj, const ObjFlagType flagType, uint32 &dwFlags);
	virtual LTRESULT SetObjectFlags(HOBJECT hObj, const ObjFlagType flagType, uint32 dwFlags);
	virtual LTRESULT GetAttachments(HLOCALOBJ hObj, HLOCALOBJ *inList, uint32 inListSize,
		uint32 &outListSize, uint32 &outNumAttachments);
	virtual LTRESULT GetAttachmentTransform(HATTACHMENT hAttachment, LTransform &transform,
		LTBOOL bWorldSpace);
	virtual LTRESULT GetAttachedModelNodeTransform(HATTACHMENT hAttachment,
		HMODELNODE hNode, LTransform &transform);
	virtual LTRESULT GetAttachedModelSocketTransform(HATTACHMENT hAttachment,
		HMODELSOCKET hSocket, LTransform &transform);
	virtual LTRESULT Parse(ConParse *pParse);
	virtual LTRESULT GetObjectType(HOBJECT hObj, uint32 *type);
	virtual LTRESULT GetModelAnimUserDims(HOBJECT hObject, LTVector *pDims, HMODELANIM hAnim);
	virtual LTRESULT GetRotationVectors(LTRotation &rot, LTVector &up, LTVector &right, LTVector &forward);
	virtual LTRESULT SetupEuler(LTRotation &rot, float pitch, float yaw, float roll);
	virtual LTRESULT GetCRC(ILTStream *pStream, uint32 &dwResult);

public:
	ILTMath		*m_pMathLT;		// 0x0c
};

#endif
