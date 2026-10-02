// Talon TransformMaker (Jupiter runtime/model/src/transformmaker.h).  Talon blends layered
// animations with a prev/cur weight set pair per animation, normalized layers, and child model
// node relations.
#ifndef __TRANSFORMMAKER_H__
#define __TRANSFORMMAKER_H__

#include "model.h"
#include "ltanimtracker.h"

// 0x2a8 bytes.
class TransformMaker
{
public:

	LTBOOL			IsValid();
	LTBOOL			SetupTransforms();

	LTBOOL			SetupCall();

	void			InitTransform(uint32 iAnim, uint32 iNode, LTRotation &outQuat, LTVector &outVec);
	void			InitTransformAdditive(uint32 iAnim, uint32 iNode, LTRotation &outQuat, LTVector &outVec);
	float			BlendTransform(uint32 iAnim, uint32 iNode, float fTotalWeight, LTBOOL bNormalize);

	void			Recurse(uint32 iNode, LTMatrix *pParentT);


	AnimTimeRef		m_Anims[MAX_GVP_ANIMS];		// 0x000
	uint32			m_nAnims;					// 0x120

	LTMatrix		*m_pStartMat;				// 0x124 If null, then identity is used.
	LTMatrix		*m_pOutput;					// 0x128 If null, then Model::m_Transforms is used.

	NodeControlFn	m_NodeControlFn;			// 0x12c
	void			*m_pNodeControlUserData;	// 0x130
	HOBJECT			m_hObject;					// 0x134

	uint32			*m_pRecursePath;			// 0x138
	uint32			m_iCurPath;					// 0x13c

	NodeRelation	*m_pRelation;				// 0x140
	LTMatrix		m_mRelation;				// 0x144
	ChildInfo		*m_pChildInfo;				// 0x184

	LTMatrix		m_mTemp;					// 0x188
	LTRotation		m_Quat;						// 0x1c8
	LTVector		m_vTrans;					// 0x1d8

	Model			*m_pModel;					// 0x1e4
	LTMatrix		m_mIdentity;				// 0x1e8 m_pStartMat if none was given

	// Local caches set up by SetupCall.
	WeightSet		*m_PrevWeightSets[MAX_GVP_ANIMS];	// 0x228
	WeightSet		*m_CurWeightSets[MAX_GVP_ANIMS];	// 0x248
	ModelAnim		*m_pAnimPrev[MAX_GVP_ANIMS];		// 0x268
	ModelAnim		*m_pAnimCur[MAX_GVP_ANIMS];			// 0x288
};

#endif
