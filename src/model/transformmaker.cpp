// Jupiter runtime/model/src/transformmaker.cpp (Talon version: weight set pairs, normalized
// layers, child model node relations, node control callback).
#include "transformmaker.h"


// FUNCTION: LITHTECH 0x0049c590
LTBOOL TransformMaker::IsValid()
{
	uint32 i;
	Model *pModel;
	AnimTimeRef *pAnim;

	if(m_nAnims == 0 || m_nAnims > MAX_GVP_ANIMS)
		return LTFALSE;

	pModel = m_Anims[0].m_pModel;
	for(i=0; i < m_nAnims; i++)
	{
		pAnim = &m_Anims[i];

		if(!pAnim->IsValid() || pAnim->m_pModel != pModel)
			return LTFALSE;

		// Make sure the weight set is valid.
		if(i != 0)
		{
			if(pAnim->m_Cur.m_iWeightSet >= pModel->NumWeightSets())
				return LTFALSE;
		}
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0049c5f0
LTBOOL TransformMaker::SetupTransforms()
{
	if(!SetupCall())
		return LTFALSE;

	Recurse(m_pModel->GetRootNode()->GetNodeIndex(), m_pStartMat);
	return LTTRUE;
}


// Remaining diff: the two inlined GetWeightSet() lookups swap edx/edi (register allocation only).
// STUB: LITHTECH 0x0049c630
LTBOOL TransformMaker::SetupCall()
{
	uint32 i;

	if(!IsValid())
		return LTFALSE;

	m_pModel = m_Anims[0].m_pModel;
	m_pRecursePath = LTNULL;

	// Cache the information that's going to be used while recursing
	for(i=0; i < m_nAnims; i++)
	{
		m_PrevWeightSets[i]	= m_pModel->GetWeightSet(m_Anims[i].m_Prev.m_iWeightSet);
		m_CurWeightSets[i]	= m_pModel->GetWeightSet(m_Anims[i].m_Cur.m_iWeightSet);
		m_pAnimPrev[i]		= m_pModel->GetAnim(m_Anims[i].m_Prev.m_iAnim);
		m_pAnimCur[i]		= m_pModel->GetAnim(m_Anims[i].m_Cur.m_iAnim);
	}

	if(!m_pStartMat)
	{
		m_mIdentity.Identity();
		m_pStartMat = &m_mIdentity;
	}

	if(!m_pOutput)
		m_pOutput = m_pModel->m_Transforms;

	m_pChildInfo = m_pModel->GetAnimInfo(m_Anims[0].m_Prev.m_iAnim)->m_pChildInfo;
	return LTTRUE;
}


// Remaining diff: prologue scheduling (orig loads both anim/node chains before the key indices).
// STUB: LITHTECH 0x0049c770
void TransformMaker::InitTransform(uint32 iAnim, uint32 iNode, LTRotation &outQuat, LTVector &outVec)
{
	AnimTimeRef *pTimeRef;
	NodeKeyFrame *pKey1, *pKey2;
	LTVector *pTrans1, *pTrans2;

	pTimeRef = &m_Anims[iAnim];
	pKey1 = &m_pAnimPrev[iAnim]->GetAnimNode(iNode)->m_KeyFrames[pTimeRef->m_Prev.m_iFrame];
	pKey2 = &m_pAnimCur[iAnim]->GetAnimNode(iNode)->m_KeyFrames[pTimeRef->m_Cur.m_iFrame];

	outQuat.Slerp(pKey1->m_Quaternion, pKey2->m_Quaternion, pTimeRef->m_Percent);
	outVec = pKey1->m_vTranslation + (pKey2->m_vTranslation - pKey1->m_vTranslation) * pTimeRef->m_Percent;

	// Apply the per-animation translation to the root node.
	if(iNode == 0)
	{
		pTrans1 = &m_pModel->GetAnimInfo(pTimeRef->m_Prev.m_iAnim)->m_vTranslation;
		pTrans2 = &m_pModel->GetAnimInfo(pTimeRef->m_Cur.m_iAnim)->m_vTranslation;
		outVec += *pTrans1 + (*pTrans2 - *pTrans1) * pTimeRef->m_Percent;
	}
}


// Remaining diff: register allocation and stack slot assignment of the by-value vector temporaries.
// STUB: LITHTECH 0x0049c920
void TransformMaker::InitTransformAdditive(uint32 iAnim, uint32 iNode, LTRotation &outQuat, LTVector &outVec)
{
	AnimTimeRef *pTimeRef;
	NodeKeyFrame *pBase, *pKey1, *pKey2;

	pTimeRef = &m_Anims[iAnim];
	pBase = m_pAnimPrev[iAnim]->GetAnimNode(iNode)->m_KeyFrames;
	pKey1 = &pBase[pTimeRef->m_Prev.m_iFrame];
	pKey2 = &m_pAnimCur[iAnim]->GetAnimNode(iNode)->m_KeyFrames[pTimeRef->m_Cur.m_iFrame];

	outQuat.Slerp(pKey1->m_Quaternion, pKey2->m_Quaternion, pTimeRef->m_Percent);
	outVec = pKey1->m_vTranslation + (pKey2->m_vTranslation - pKey1->m_vTranslation) * pTimeRef->m_Percent;

	// frame = base + offset
	outQuat = ~pBase->m_Quaternion * outQuat;
	outVec = outVec - pBase->m_vTranslation;
}


// FUNCTION: LITHTECH 0x0049cac0
float TransformMaker::BlendTransform(uint32 iAnim, uint32 iNode, float fTotalWeight, LTBOOL bNormalize)
{
	float fPercent, fPrevWeight, fCurWeight, fWeight;
	LTRotation qTransform, qTemp;
	LTVector vTransform;

	fWeight = (fTotalWeight > 1.0f) ? 1.0f : fTotalWeight;

	if(bNormalize)
	{
		// Fill up what the earlier animations left.
		fPercent = 1.0f - fWeight;
	}
	else
	{
		fPrevWeight = m_PrevWeightSets[iAnim] ? m_PrevWeightSets[iAnim]->m_Weights[iNode] : 0.0f;
		fCurWeight = m_CurWeightSets[iAnim] ? m_CurWeightSets[iAnim]->m_Weights[iNode] : 0.0f;

		if(fPrevWeight == 2.0f)
			fPercent = fCurWeight;
		else if(fCurWeight == 2.0f)
			fPercent = 2.0f;
		else
			fPercent = (fCurWeight - fPrevWeight) * m_Anims[iAnim].m_Percent + fPrevWeight;
	}

	// skip this blend if the weight for this anim is zero.
	if(fPercent == 0.0f)
		return 0.0f;

	if(fPercent == 2.0f)
	{
		// Add the animation.
		InitTransformAdditive(iAnim, iNode, qTransform, vTransform);

		m_Quat = m_Quat * qTransform;
		m_vTrans = m_vTrans + vTransform;
		return 0.0f;
	}
	else
	{
		InitTransform(iAnim, iNode, qTransform, vTransform);
		qTemp = m_Quat;
		m_Quat.Slerp(qTemp, qTransform, fPercent);
		m_vTrans = m_vTrans + (vTransform - m_vTrans) * fPercent;
		return fPercent;
	}
}


// Remaining diff: one lea scheduled before vs after pushing &m_mRelation (13 bytes).
// STUB: LITHTECH 0x0049cd00
void TransformMaker::Recurse(uint32 iNode, LTMatrix *pParentT)
{
	uint32 i;
	LTMatrix *pMyGlobal;
	ModelNode *pNode;
	float fWeight, fPrevWeight;
	NodeRelation *pRelation;
	static LTMatrix mScratchMat;

	for(;;)
	{
		pMyGlobal = &m_pOutput[iNode];

		// Apply animation data (first one inits, the rest are blended in).
		InitTransform(0, iNode, m_Quat, m_vTrans);

		if(m_PrevWeightSets[0] && m_CurWeightSets[0] && m_Anims[0].m_Percent != 2.0f)
		{
			fPrevWeight = m_PrevWeightSets[0]->m_Weights[iNode];
			fWeight = (m_CurWeightSets[0]->m_Weights[iNode] - fPrevWeight) * m_Anims[0].m_Percent + fPrevWeight;
		}
		else
		{
			fWeight = 0.0f;
		}

		for(i=1; i < m_nAnims; i++)
		{
			fWeight += BlendTransform(i, iNode, fWeight, m_Anims[i].m_bNormalize);
		}

		pNode = m_pModel->GetNode(iNode);

		// Update the global matrix.
		m_Quat.ConvertToMatrix(m_mTemp);

		// Use the offset from the parent if this node only uses rotation data
		// from the animation.
		if(pNode->m_Flags & MNODE_ROTATIONONLY)
		{
			m_mTemp.SetTranslation(pNode->m_vOffsetFromParent);
		}
		else
		{
			m_mTemp.SetTranslation(m_vTrans);
		}

		MatMul(&mScratchMat, pParentT, &m_mTemp);

		// Go into the space of the model the animation came from.
		pRelation = m_pRelation = &m_pChildInfo->m_Relation[iNode];
		pRelation->m_Rot.ConvertToMatrix(m_mRelation);
		m_mRelation.SetTranslation(pRelation->m_Pos);
		MatMul(pMyGlobal, &mScratchMat, &m_mRelation);

		if(m_NodeControlFn)
			m_NodeControlFn(m_hObject, iNode, pMyGlobal, m_pNodeControlUserData);

		// Do the children..
		if(m_pRecursePath)
		{
			if(m_iCurPath > 0)
			{
				m_iCurPath--;
				// Start over the loop at the next point in the path
				iNode = m_pRecursePath[m_iCurPath];
				pParentT = pMyGlobal;
			}
			else
				break;
		}
		else
		{
			uint32 nNumChildren = pNode->NumChildren();
			if(nNumChildren)
			{
				// Recurse for the beginning of the child list
				--nNumChildren;
				for(i=0; i < nNumChildren; i++)
				{
					Recurse(pNode->m_Children[i]->GetNodeIndex(), pMyGlobal);
				}
				// Iterate for the final child
				m_iCurPath = 1;
				iNode = pNode->m_Children[nNumChildren]->GetNodeIndex();
				pParentT = pMyGlobal;
			}
			else
				// Jump out of the list..  No children.
				break;
		}
	}
}
