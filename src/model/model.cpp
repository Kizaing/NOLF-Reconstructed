// Jupiter runtime/model/src/model.cpp, Talon version.
// Talon's model classes have vtables (CMoArray-based), Model keeps its child models in a
// fixed array at 0x194 and its node transforms in a raw array allocated from m_pAlloc.
#include <string.h>
#include "bdefs.h"
#include "model.h"
#include "ltanimtracker.h"
#include "../../build/proj/LT2/lithshared/stdlith/l_allocator.h"

// GLOBAL: LITHTECH 0x004d483c
extern char *g_pNoModelFilename;


// FUNCTION: LITHTECH 0x0044db70
LTRESULT DefaultLoadChildFn(ModelLoadRequest *pRequest, Model **ppModel)
{
	return LT_NOCHANGE;
}


// FUNCTION: LITHTECH 0x0044db80
LTBOOL VerifyChildModel_R(ModelNode *pParentNode, ModelNode *pChildNode, ModelNode* &pErrNode)
{
	uint32 i;

	if(pParentNode->NumChildren() != pChildNode->NumChildren())
	{
		pErrNode = pParentNode;
		return LTFALSE;
	}

	for(i=0; i < pParentNode->NumChildren(); i++)
	{
		if(!VerifyChildModel_R(pParentNode->GetChild(i), pChildNode->GetChild(i), pErrNode))
			return LTFALSE;
	}

	return LTTRUE;
}


// ------------------------------------------------------------------------ //
// ModelStringList.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044dbe0
ModelStringList::ModelStringList(LAlloc *pAlloc)
{
	m_pAlloc = pAlloc;
	m_StringList = LTNULL;
}


// FUNCTION: LITHTECH 0x0044dc00
ModelStringList::~ModelStringList()
{
	Term();
}


// FUNCTION: LITHTECH 0x0044dc10
void ModelStringList::Term()
{
	ModelString *pCurString, *pNextString;

	pCurString = m_StringList;
	while(pCurString)
	{
		pNextString = pCurString->m_pNext;
		GetAlloc()->Free(pCurString);
		pCurString = pNextString;
	}
	m_StringList = LTNULL;
}


// FUNCTION: LITHTECH 0x0044dc40
const char* ModelStringList::AddString(const char *pString)
{
	ModelString *pCur, *pRet;
	uint32 dwSize;

	// Quick exit..
	if(!pString || pString[0] == 0)
		return g_EmptyString;

	pCur = m_StringList;
	while(pCur)
	{
		if(strcmp(pCur->m_String, pString) == 0)
			return pCur->m_String;

		pCur = pCur->m_pNext;
	}

	// Ok, add a new string.
	dwSize = sizeof(ModelString) - 1 + strlen(pString) + 1;
	pRet = (ModelString*)GetAlloc()->Alloc(dwSize);
	if(!pRet)
		return g_EmptyString;

	pRet->m_AllocSize = dwSize;
	pRet->m_pNext = m_StringList;
	m_StringList = pRet;

	strcpy(pRet->m_String, pString);
	return pRet->m_String;
}


// FUNCTION: LITHTECH 0x0044dd00
LTBOOL ModelStringList::SetAlloc(LAlloc *pAlloc)
{
	if(m_StringList)
		return LTFALSE;

	m_pAlloc = pAlloc;
	return LTTRUE;
}


// ------------------------------------------------------------------------ //
// AnimTimeRef / AnimKeyFrame.
// ------------------------------------------------------------------------ //

// STUB: LITHTECH 0x0044dd20
// esi/edi swapped between the model pointer and its anim count.
LTBOOL AnimTimeRef::IsValid()
{
	Model *pModel;

	pModel = m_pModel;
	if(pModel &&
		m_Prev.m_iAnim < pModel->NumAnims() && m_Cur.m_iAnim < pModel->NumAnims() &&
		m_Prev.m_iFrame < pModel->GetAnim(m_Prev.m_iAnim)->m_nKeyFrames &&
		m_Cur.m_iFrame < pModel->GetAnim(m_Cur.m_iAnim)->m_nKeyFrames &&
		m_Percent >= 0.0f && m_Percent <= 1.0f)
	{
		return LTTRUE;
	}

	return LTFALSE;
}


// ------------------------------------------------------------------------ //
// NewVertexWeight / ModelVert.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044dda0
NewVertexWeight::NewVertexWeight()
{
	m_Vec[0] = m_Vec[1] = m_Vec[2] = m_Vec[3] = 0.0f;
	m_iNode = 0;
}


// FUNCTION: LITHTECH 0x0044ddc0
ModelVert::ModelVert()
{
	m_Weights = LTNULL;
	m_nWeights = 0;
	m_Vec.Init();
	m_Normal.Init();
}


// FUNCTION: LITHTECH 0x0044dea0
AnimKeyFrame::AnimKeyFrame()
{
	m_Time = 0;
	m_pString = g_EmptyString;
	m_KeyType = KEYTYPE_POSITION;
	m_Callback = LTNULL;
	m_pUser = LTNULL;
}


// ------------------------------------------------------------------------ //
// AnimNode.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044df20 ?GetAnim@AnimNode@@UAEPAVModelAnim@@XZ

// STUB: LITHTECH 0x0044dfc0
// Talon's member CMoArrays are padding here; their destructors aren't generated yet.
AnimNode::~AnimNode()
{
	Term();
}

// STUB: LITHTECH 0x0044e150
// The original frees the arrays through out-of-line BaseDelete<T> calls (CMoArray::Term inlined one level deeper).
void AnimNode::Term()
{
	Model *pModel;
	LAlloc *pAlloc;
	uint32 i;

	pModel = GetModel();
	if(m_KeyFrames)
	{
		LDelete_Array(pModel->m_pAlloc, m_KeyFrames, m_nKeyFrames);
		m_KeyFrames = LTNULL;
	}
	m_nKeyFrames = 0;

	pAlloc = GetModel()->m_pAlloc;
	for(i=0; i < NumChildren(); i++)
	{
		LDelete(pAlloc, m_Children[i]);
	}

	if(m_Children)
	{
		LDelete_Array(pAlloc, m_Children, m_nChildren);
		m_Children = LTNULL;
	}
	m_nChildren = 0;
}

// FUNCTION: LITHTECH 0x0044e1e0 ?SetAnim@AnimNode@@UAEXPAVModelAnim@@@Z

// FUNCTION: LITHTECH 0x0044e1f0
AnimNode* AnimNode::Create(ModelAnim *pAnim, AnimNode *pParent)
{
	return new AnimNode(pAnim, pParent);
}

// FUNCTION: LITHTECH 0x0044e210
LTBOOL AnimNode::FillNodeList(uint32 &curNodeIndex)
{
	uint32 i;

	if(m_pAnim->m_AnimNodes && curNodeIndex < GetModel()->NumNodes())
	{
		m_pAnim->m_AnimNodes[curNodeIndex] = this;
		++curNodeIndex;

		for(i=0; i < NumChildren(); i++)
		{
			if(!GetChild(i)->FillNodeList(curNodeIndex))
				return LTFALSE;
		}

		return LTTRUE;
	}

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x0044e280
LTBOOL AnimNode::SetNode_R(ModelNode *pNode)
{
	uint32 i;

	m_pNode = pNode;
	if(NumChildren() != pNode->NumChildren())
		return LTFALSE;

	for(i=0; i < NumChildren(); i++)
	{
		GetChild(i)->SetNode_R(pNode->GetChild(i));
	}

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x0044e2d0
Model* AnimNode::GetModel()
{
	return m_pAnim->m_pModel;
}


// ------------------------------------------------------------------------ //
// ModelAnim.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044e410
void ModelAnim::SetModel(Model *pModel)
{
	m_pName = (char*)pModel->AddString(m_pName);
	m_pModel = pModel;
	m_pRootNode->SetNode_R(pModel->GetRootNode());
}

// FUNCTION: LITHTECH 0x0044e440
ModelAnim* ModelAnim::Create(Model *pModel)
{
	return new ModelAnim(pModel);
}

// FUNCTION: LITHTECH 0x0044e460
void ModelAnim::FreeRootNode()
{
	if(m_pRootNode)
	{
		if(m_pRootNode != &m_RootNode)
		{
			delete m_pRootNode;
		}
	}

	m_pRootNode = &m_RootNode;
}

// FUNCTION: LITHTECH 0x0044e4a0
LTBOOL ModelAnim::SetupNodeLists(LTBOOL bRebuild)
{
	return PrecalcNodeLists(bRebuild);
}


// FUNCTION: LITHTECH 0x0044e4b0
LTBOOL ModelAnim::PrecalcNodeLists(LTBOOL bRebuild)
{
	if(!bRebuild && m_AnimNodes)
		return LTTRUE;

	if(m_AnimNodes)
		m_pModel->m_pAlloc->Free(m_AnimNodes);

	m_AnimNodes = (AnimNode**)m_pModel->m_pAlloc->Alloc(m_pModel->m_nNodes * 4);
	if(!m_AnimNodes)
		return LTFALSE;

	{
		uint32 curNodeIndex = 0;
		AnimNode *pRoot = m_pRootNode;
		return pRoot->FillNodeList(curNodeIndex);
	}
}

// FUNCTION: LITHTECH 0x0044e520
uint32 ModelAnim::GetAnimTime()
{
	if(m_nKeyFrames > 0)
		return m_KeyFrames[m_nKeyFrames-1].m_Time;
	else
		return 0;
}


// ------------------------------------------------------------------------ //
// ModelNode.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044e5b0 ?MNSlot1@ModelNode@@UAEXKKK@Z

// STUB: LITHTECH 0x0044e610
// Talon's member CMoArray is padding here; its destructor isn't generated yet.
ModelNode::~ModelNode()
{
}

// FUNCTION: LITHTECH 0x0044e650
void ModelNode::Term()
{
	LAlloc *pAlloc;
	uint32 i;

	pAlloc = m_pModel->m_pAlloc;
	for(i=0; i < NumChildren(); i++)
	{
		LDelete(pAlloc, m_Children[i]);
	}

	if(m_Children)
	{
		pAlloc->Free(m_Children);
		m_Children = LTNULL;
	}
	m_nChildren = 0;

	Clear();
}

// FUNCTION: LITHTECH 0x0044e6c0
void ModelNode::Clear()
{
	m_pName = g_EmptyString;
	m_NodeIndex = 0;
	m_Flags = 0;
	m_vOffsetFromParent.Init();
	m_mGlobalTransform.Identity();
	m_mInvGlobalTransform.Identity();
}

// FUNCTION: LITHTECH 0x0044e770
ModelNode* ModelNode::Create(Model *pModel)
{
	return new ModelNode(pModel);
}

// FUNCTION: LITHTECH 0x0044e790
void ModelNode::SetModel(Model *pModel)
{
	uint32 i;

	m_pModel = pModel;
	m_pName = (char*)pModel->AddString(m_pName);

	for(i=0; i < NumChildren(); i++)
	{
		GetChild(i)->SetModel(pModel);
	}
}

// FUNCTION: LITHTECH 0x0044e7e0
uint32 ModelNode::CalcNumNodes()
{
	uint32 i, total;

	total = 1;
	for(i=0; i < NumChildren(); i++)
		total += GetChild(i)->CalcNumNodes();

	return total;
}


// FUNCTION: LITHTECH 0x0044e810
LTBOOL ModelNode::FillNodeList(uint32 &curNodeIndex)
{
	uint32 i;

	if(curNodeIndex >= m_pModel->m_nFlatNodes)
		return LTFALSE;

	m_NodeIndex = (uint16)curNodeIndex;
	m_pModel->m_FlatNodeList[curNodeIndex] = this;
	++curNodeIndex;

	for(i=0; i < NumChildren(); i++)
	{
		if(!GetChild(i)->FillNodeList(curNodeIndex))
			return LTFALSE;
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0044e870
void ModelNode::SetParent_R(uint32 iParent)
{
	uint32 i;

	m_iParentNode = iParent;
	for(i=0; i < NumChildren(); i++)
	{
		GetChild(i)->SetParent_R(m_NodeIndex);
	}
}


// FUNCTION: LITHTECH 0x0044ebb0
ModelSocket::ModelSocket()
{
	m_Name[0] = 0;
	m_iNode = 0;
	m_Pos.Init();
	m_Rot.Init();
	m_Unknown30 = 0;
}


// ------------------------------------------------------------------------ //
// Model.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044f590
ModelPiece* Model::FindPiece(const char *pName, uint32 *index)
{
	uint32 i;
	ModelPiece *pPiece;

	for(i=0; i < NumPieces(); i++)
	{
		pPiece = GetPiece(i);
		if(stricmp(pName, pPiece->m_Name) == 0)
		{
			if(index)
				*index = i;

			return pPiece;
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x0044f5e0
WeightSet* Model::FindWeightSet(const char *pName, uint32 *index)
{
	uint32 i;
	WeightSet *pSet;

	for(i=0; i < m_WeightSets.GetSize(); i++)
	{
		pSet = m_WeightSets[i];
		if(stricmp(pName, (const char*)pSet) == 0)
		{
			if(index)
				*index = i;

			return pSet;
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x0044f630
const char* Model::AddString(const char *pStr)
{
	return m_StringList.AddString(pStr);
}


// FUNCTION: LITHTECH 0x0044f640
ModelNode* Model::FindNode(const char *pName, uint32 *index)
{
	uint32 i;

	for(i=0; i < NumNodes(); i++)
	{
		if(stricmp(GetNode(i)->GetName(), pName) == 0)
		{
			if(index)
				*index = i;

			return GetNode(i);
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x0044f6a0
ModelAnim* Model::FindAnim(const char *pName, uint32 *index, AnimInfo **ppInfo)
{
	uint32 i;
	ModelAnim *pAnim;

	for(i=0; i < NumAnims(); i++)
	{
		pAnim = GetAnim(i);
		if(stricmp(pAnim->m_pName, pName) == 0)
		{
			if(index)
				*index = i;

			if(ppInfo)
				*ppInfo = GetAnimInfo(i);

			return pAnim;
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x0044f720
AnimInfo* Model::FindAnimInfo(const char *pAnimName, Model *pOwner, uint32 *index)
{
	uint32 i;
	AnimInfo *pInfo;

	for(i=0; i < NumAnims(); i++)
	{
		pInfo = GetAnimInfo(i);
		if(pInfo->m_pAnim->m_pModel == pOwner && stricmp(pInfo->m_pAnim->m_pName, pAnimName) == 0)
		{
			if(index)
				*index = i;

			return pInfo;
		}
	}

	return LTNULL;
}


// FUNCTION: LITHTECH 0x0044f920
uint32 Model::CalcNumTris(uint32 iLOD)
{
	uint32 i, total;
	ModelPiece *pPiece;
	PieceLOD *pLOD;

	total = 0;
	for(i=0; i < NumPieces(); i++)
	{
		pPiece = GetPiece(i);
		pLOD = pPiece->GetLOD(iLOD);
		if(pLOD)
			total += pLOD->m_Tris.GetSize();
	}

	return total;
}


// FUNCTION: LITHTECH 0x0044f970
uint32 Model::CalcNumVerts()
{
	uint32 i, total;

	total = 0;
	for(i=0; i < NumPieces(); i++)
	{
		total += GetPiece(i)->m_Verts.GetSize();
	}

	return total;
}


// FUNCTION: LITHTECH 0x0044f990
uint32 Model::CalcNumChildModelAnims(LTBOOL bIncludeSelf)
{
	uint32 i, total;
	ChildInfo *pChildModel;

	total = 0;
	for(i=0; i < NumChildModels(); i++)
	{
		pChildModel = GetChildModel(i);

		if(!pChildModel->m_pModel)
			continue;

		if(pChildModel == GetSelfChildModel() && !bIncludeSelf)
			continue;

		total += pChildModel->m_pModel->m_nAnims;
	}

	return total;
}


// FUNCTION: LITHTECH 0x0044f9e0
uint32 Model::CalcNumParentAnims()
{
	uint32 i, total;

	total = 0;
	for(i=0; i < NumAnims(); i++)
	{
		if(GetAnim(i)->m_pModel == this)
			++total;
	}

	return total;
}


// FUNCTION: LITHTECH 0x0044ff50
LTBOOL Model::SetFilename(const char *pInFilename)
{
	char *pFilename;

	FreeFilename();

	pFilename = new char[strlen(pInFilename) + 1];
	if(!pFilename)
		return LTFALSE;

	strcpy(pFilename, pInFilename);
	m_pFilename = pFilename;
	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0044ffa0
void Model::FreeFilename()
{
	if(m_pFilename != g_pNoModelFilename)
	{
		delete [] m_pFilename;
		m_pFilename = g_pNoModelFilename;
	}
	else
	{
		m_pFilename = g_pNoModelFilename;
	}
}


// FUNCTION: LITHTECH 0x0044ffd0
LTBOOL Model::VerifyChildModelTree(Model *pChild, ModelNode* &pErrNode)
{
	return VerifyChildModel_R(GetRootNode(), pChild->GetRootNode(), pErrNode);
}


// FUNCTION: LITHTECH 0x00450080
LTBOOL Model::InitChildInfo(uint32 index, ChildInfo *pChildModel, Model *pModel, const char *pFilename)
{
	if(index >= MAX_CHILD_MODELS)
		return LTFALSE;

	pChildModel->m_pFilename = pFilename;
	pChildModel->m_pParentModel = this;
	pChildModel->m_pModel = pModel;
	pChildModel->m_AnimOffset = 0;

	if(pModel)
	{
		if(pModel != this)
		{
			pModel->m_RefCount++;
		}
	}

	m_ChildModels[index] = pChildModel;
	return LTTRUE;
}


// FUNCTION: LITHTECH 0x004500e0
ModelSocket* Model::FindSocket(const char *pName, uint32 *index)
{
	uint32 i;
	ModelSocket *pSocket;

	for(i=0; i < NumSockets(); i++)
	{
		pSocket = GetSocket(i);
		if(stricmp(pSocket->m_Name, pName) == 0)
		{
			if(index)
				*index = i;

			return pSocket;
		}
	}

	return LTNULL;
}


// ------------------------------------------------------------------------ //
// CMoArray instances.
// The linker kept model.obj's copies of the arrays used by the model classes
// (0x00450140-0x004552c0, after this file's code).  The original instantiates
// them from the constructors and destructors above, which aren't matched yet,
// so this function references them instead.  It isn't in lithtech.exe.
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00450140 ?GenAppend@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@UAEHAAVNodeKeyFrame@@@Z
// FUNCTION: LITHTECH 0x004502a0 ??4LTRotation@@QAEAAV0@ABV0@@Z
// FUNCTION: LITHTECH 0x004502c0 ?GenRemoveAt@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00450440 ?GenCopyList@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@UAEHABV?$GenList@VNodeKeyFrame@@@@@Z
// FUNCTION: LITHTECH 0x00450590 ?GenAppendList@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@UAEHABV?$GenList@VNodeKeyFrame@@@@@Z
// FUNCTION: LITHTECH 0x004506c0 ?GenRemoveAt@?$CMoArray@PAVModelNode@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00450790 ?GenRemoveAll@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@UAEXXZ
// FUNCTION: LITHTECH 0x004507c0 ?GenAppend@?$CMoArray@VAnimKeyFrame@@VNoCache@@@@UAEHAAVAnimKeyFrame@@@Z
// FUNCTION: LITHTECH 0x004508b0 ?GenRemoveAt@?$CMoArray@VAnimKeyFrame@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x004509b0 ?GenCopyList@?$CMoArray@VAnimKeyFrame@@VNoCache@@@@UAEHABV?$GenList@VAnimKeyFrame@@@@@Z
// FUNCTION: LITHTECH 0x00450ae0 ?GenAppendList@?$CMoArray@VAnimKeyFrame@@VNoCache@@@@UAEHABV?$GenList@VAnimKeyFrame@@@@@Z
// FUNCTION: LITHTECH 0x00450be0 ?GenFindElement@?$CMoArray@VAnimKeyFrame@@VNoCache@@@@UBEHABVAnimKeyFrame@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00450c10 ?GenGetNext@?$CMoArray@VNodeRelation@@VDefaultCache@@@@UBE?AVNodeRelation@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00450c60 ?GenGetAt@?$CMoArray@VNodeRelation@@VDefaultCache@@@@UBE?AVNodeRelation@@AAVGenListPos@@@Z
// STUB: LITHTECH 0x00450cb0 ?GenAppend@?$CMoArray@VNodeRelation@@VDefaultCache@@@@UAEHAAVNodeRelation@@@Z
// The original runs out of inline budget after two copy loops and calls NodeRelation::operator= out of line.
// FUNCTION: LITHTECH 0x00450ea0 ?GenRemoveAt@?$CMoArray@VNodeRelation@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00451040 ?GenRemoveAll@?$CMoArray@VNodeRelation@@VDefaultCache@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00451070 ?GenCopyList@?$CMoArray@VNodeRelation@@VDefaultCache@@@@UAEHABV?$GenList@VNodeRelation@@@@@Z
// FUNCTION: LITHTECH 0x004511d0 ?GenAppendList@?$CMoArray@VNodeRelation@@VDefaultCache@@@@UAEHABV?$GenList@VNodeRelation@@@@@Z
// FUNCTION: LITHTECH 0x00451300 ?GenFindElement@?$CMoArray@VNodeRelation@@VDefaultCache@@@@UBEHABVNodeRelation@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00451330 ?GenAppend@?$CMoArray@VModelVert@@VNoCache@@@@UAEHAAVModelVert@@@Z
// FUNCTION: LITHTECH 0x00451420 ?GenRemoveAt@?$CMoArray@VModelVert@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00451520 ?GenCopyList@?$CMoArray@VModelVert@@VNoCache@@@@UAEHABV?$GenList@VModelVert@@@@@Z
// FUNCTION: LITHTECH 0x00451650 ?GenAppendList@?$CMoArray@VModelVert@@VNoCache@@@@UAEHABV?$GenList@VModelVert@@@@@Z
// FUNCTION: LITHTECH 0x00451750 ?GenGetNext@?$CMoArray@VModelVert@@VNoCache@@@@UBE?AVModelVert@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00451780 ?GenAppend@?$CMoArray@VModelTri@@VNoCache@@@@UAEHAAVModelTri@@@Z
// FUNCTION: LITHTECH 0x00451900 ?GenRemoveAt@?$CMoArray@VModelTri@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00451aa0 ?GenCopyList@?$CMoArray@VModelTri@@VNoCache@@@@UAEHABV?$GenList@VModelTri@@@@@Z
// FUNCTION: LITHTECH 0x00451bf0 ?GenAppendList@?$CMoArray@VModelTri@@VNoCache@@@@UAEHABV?$GenList@VModelTri@@@@@Z
// FUNCTION: LITHTECH 0x00451d10 ?GenFindElement@?$CMoArray@VModelVert@@VNoCache@@@@UBEHABVModelVert@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00451d40 ?GenGetNext@?$CMoArray@VPieceLOD@@VNoCache@@@@UBE?AVPieceLOD@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00451da0 ?GenGetAt@?$CMoArray@VPieceLOD@@VNoCache@@@@UBE?AVPieceLOD@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00451e00 ?GenAppend@?$CMoArray@VPieceLOD@@VNoCache@@@@UAEHAAVPieceLOD@@@Z
// FUNCTION: LITHTECH 0x00451f60 ?GenRemoveAt@?$CMoArray@VPieceLOD@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00452190 ??4ModelTri@@QAEXABV0@@Z
// FUNCTION: LITHTECH 0x004521d0 ?GenRemoveAll@?$CMoArray@VPieceLOD@@VNoCache@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00452220 ?GenCopyList@?$CMoArray@VPieceLOD@@VNoCache@@@@UAEHABV?$GenList@VPieceLOD@@@@@Z
// FUNCTION: LITHTECH 0x00452420 ?GenAppendList@?$CMoArray@VPieceLOD@@VNoCache@@@@UAEHABV?$GenList@VPieceLOD@@@@@Z
// FUNCTION: LITHTECH 0x00452610 ?GenGetNext@?$CMoArray@MVDefaultCache@@@@UBEMAAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00452630 ?GenGetAt@?$CMoArray@MVDefaultCache@@@@UBEMAAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00452640 ?GenCopyList@?$CMoArray@MVDefaultCache@@@@UAEHABV?$GenList@M@@@Z
// FUNCTION: LITHTECH 0x00452760 ?GenAppendList@?$CMoArray@MVDefaultCache@@@@UAEHABV?$GenList@M@@@Z
// FUNCTION: LITHTECH 0x00452830 ?GenAppend@?$CMoArray@PAVModelNode@@VNoCache@@@@UAEHAAPAVModelNode@@@Z
// FUNCTION: LITHTECH 0x004528d0 ?GenCopyList@?$CMoArray@PAVModelNode@@VNoCache@@@@UAEHABV?$GenList@PAVModelNode@@@@@Z
// FUNCTION: LITHTECH 0x004529d0 ?GenAppendList@?$CMoArray@PAVModelNode@@VNoCache@@@@UAEHABV?$GenList@PAVModelNode@@@@@Z
// FUNCTION: LITHTECH 0x00452aa0 ?GenGetNext@?$CMoArray@VAnimKeyFrame@@VNoCache@@@@UBE?AVAnimKeyFrame@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00452ad0 ?GenGetAt@?$CMoArray@VAnimKeyFrame@@VNoCache@@@@UBE?AVAnimKeyFrame@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00452b00 ?GenAppend@?$CMoArray@VNewVertexWeight@@VNoCache@@@@UAEHAAVNewVertexWeight@@@Z
// FUNCTION: LITHTECH 0x00452bf0 ?GenRemoveAt@?$CMoArray@VNewVertexWeight@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00452cf0 ?GenCopyList@?$CMoArray@VNewVertexWeight@@VNoCache@@@@UAEHABV?$GenList@VNewVertexWeight@@@@@Z
// FUNCTION: LITHTECH 0x00452e20 ?GenAppendList@?$CMoArray@VNewVertexWeight@@VNoCache@@@@UAEHABV?$GenList@VNewVertexWeight@@@@@Z
// FUNCTION: LITHTECH 0x00452f20 ?GenGetNext@?$CMoArray@VLTMatrix@@VNoCache@@@@UBE?AVLTMatrix@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00452f50 ?GenGetAt@?$CMoArray@VLTMatrix@@VNoCache@@@@UBE?AVLTMatrix@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00452f80 ?GenAppend@?$CMoArray@VLTMatrix@@VNoCache@@@@UAEHAAVLTMatrix@@@Z
// FUNCTION: LITHTECH 0x00453070 ?GenRemoveAt@?$CMoArray@VLTMatrix@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00453180 ?GenCopyList@?$CMoArray@VLTMatrix@@VNoCache@@@@UAEHABV?$GenList@VLTMatrix@@@@@Z
// FUNCTION: LITHTECH 0x004532b0 ?GenAppendList@?$CMoArray@VLTMatrix@@VNoCache@@@@UAEHABV?$GenList@VLTMatrix@@@@@Z
// FUNCTION: LITHTECH 0x004533b0 ?GenFindElement@?$CMoArray@VLTMatrix@@VNoCache@@@@UBEHABVLTMatrix@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x004533e0 ?GenAppend@?$CMoArray@VLODDistance@@VDefaultCache@@@@UAEHAAVLODDistance@@@Z
// FUNCTION: LITHTECH 0x004534d0 ?GenRemoveAt@?$CMoArray@VLODDistance@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x004535c0 ?GenCopyList@?$CMoArray@VLODDistance@@VDefaultCache@@@@UAEHABV?$GenList@VLODDistance@@@@@Z
// FUNCTION: LITHTECH 0x004536e0 ?GenAppendList@?$CMoArray@VLODDistance@@VDefaultCache@@@@UAEHABV?$GenList@VLODDistance@@@@@Z
// FUNCTION: LITHTECH 0x004537c0 ?GenGetAt@?$CMoArray@PAVModelNode@@VNoCache@@@@UBEPAVModelNode@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x004537d0 ?GenSetCacheSize@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@UAEXK@Z
// FUNCTION: LITHTECH 0x004537e0 ?GenGetAt@?$CMoArray@VModelVert@@VNoCache@@@@UBE?AVModelVert@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00453810 ?GenAppend@?$CMoArray@VAnimInfo@@VNoCache@@@@UAEHAAVAnimInfo@@@Z
// FUNCTION: LITHTECH 0x00453900 ?GenRemoveAt@?$CMoArray@VAnimInfo@@VNoCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00453a00 ?GenGetSize@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@UBEKXZ
// FUNCTION: LITHTECH 0x00453a10 ?GenCopyList@?$CMoArray@VAnimInfo@@VNoCache@@@@UAEHABV?$GenList@VAnimInfo@@@@@Z
// FUNCTION: LITHTECH 0x00453b40 ?GenAppendList@?$CMoArray@VAnimInfo@@VNoCache@@@@UAEHABV?$GenList@VAnimInfo@@@@@Z
// FUNCTION: LITHTECH 0x00453c40 ??4NodeRelation@@QAEAAV0@ABV0@@Z
// FUNCTION: LITHTECH 0x00453c80 ?SetSize2@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00453cf0 ?InternalNiceSetSize@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00453e10 ?InternalNiceSetSize@?$CMoArray@PAVModelNode@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00453ec0 ?InternalNiceSetSize@?$CMoArray@VAnimKeyFrame@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00453fb0 ?InternalNiceSetSize@?$CMoArray@VNodeRelation@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454100 ?SetSize2@?$CMoArray@PAVModelNode@@VNoCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454160 ?SetSize2@?$CMoArray@VModelVert@@VNoCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x004541e0 ?InternalNiceSetSize@?$CMoArray@VModelVert@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x004542c0 ?SetSize2@?$CMoArray@VModelTri@@VNoCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454320 ?InternalNiceSetSize@?$CMoArray@VModelTri@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454440 ?InternalNiceSetSize@?$CMoArray@VPieceLOD@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454590 ?Init@?$CMoArray@PAVModelNode@@VNoCache@@@@QAEHKK@Z
// FUNCTION: LITHTECH 0x00454600 ?Init@?$CMoArray@VNewVertexWeight@@VNoCache@@@@QAEHKK@Z
// FUNCTION: LITHTECH 0x00454660 ?SetSize2@?$CMoArray@VNewVertexWeight@@VNoCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x004546e0 ?InternalNiceSetSize@?$CMoArray@VNewVertexWeight@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x004547d0 ?Init@?$CMoArray@VLTMatrix@@VNoCache@@@@QAEHKK@Z
// FUNCTION: LITHTECH 0x00454830 ?SetSize2@?$CMoArray@VLTMatrix@@VNoCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454890 ?InternalNiceSetSize@?$CMoArray@VLTMatrix@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454960 ?Init@?$CMoArray@VLODDistance@@VDefaultCache@@@@QAEHKK@Z
// FUNCTION: LITHTECH 0x004549e0 ?SetSize2@?$CMoArray@VLODDistance@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454a70 ?InternalNiceSetSize@?$CMoArray@VLODDistance@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454b70 ?Init@?$CMoArray@VAnimInfo@@VNoCache@@@@QAEHKK@Z
// FUNCTION: LITHTECH 0x00454bd0 ?SetSize2@?$CMoArray@VAnimInfo@@VNoCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454c50 ?InternalNiceSetSize@?$CMoArray@VAnimInfo@@VNoCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454d40 ?_DeleteAndDestroyArray@?$CMoArray@VNodeKeyFrame@@VNoCache@@@@AAEXPAVLAlloc@@K@Z
// FUNCTION: LITHTECH 0x00454d60 ?_DeleteAndDestroyArray@?$CMoArray@VPieceLOD@@VNoCache@@@@AAEXPAVLAlloc@@K@Z
// FUNCTION: LITHTECH 0x00454da0 ?BaseDelete@@YAXPAVLAlloc@@PAVModelPiece@@K@Z
// FUNCTION: LITHTECH 0x00454dd0 ?BaseDelete@@YAXPAVLAlloc@@PAVWeightSet@@K@Z
// FUNCTION: LITHTECH 0x00454e00 ?BaseNew@@YAPAVNodeKeyFrame@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x00454e20 ?BaseNew@@YAPAVAnimKeyFrame@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x00454e60 ?CopyArray2@?$CMoArray@PAVModelNode@@VNoCache@@@@QAEHABV1@PAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454ef0 ?CopyArray2@?$CMoArray@VModelVert@@VNoCache@@@@QAEHABV1@PAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00454fa0 ?BaseNew@@YAPAVModelVert@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x00454fe0 ?CopyArray2@?$CMoArray@VModelTri@@VNoCache@@@@QAEHABV1@PAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x004550a0 ?BaseNew@@YAPAVModelTri@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x004550c0 ?BaseDelete@@YAXPAVLAlloc@@PAVPieceLOD@@K@Z
// FUNCTION: LITHTECH 0x004550f0 ?BaseNew@@YAPAVPieceLOD@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x00455130 ?BaseNew@@YAPAVNewVertexWeight@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x00455170 ?CopyArray2@?$CMoArray@VLTMatrix@@VNoCache@@@@QAEHABV1@PAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00455200 ?BaseNew@@YAPAVLTMatrix@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x00455220 ?BaseNew@@YAPAVLODDistance@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x00455260 ?BaseNew@@YAPAVAnimInfo@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x004552a0 ??_GPieceLOD@@QAEPAXI@Z

#define MODEL_ARRAY_INSTANCE(T, C) \
	{ \
		CMoArray<T, C> theArray; \
		theArray.SetSize2(0, &g_DefAlloc); \
		theArray.Init(0, 0); \
		theArray.CopyArray2(theArray, &g_DefAlloc); \
		theArray.NiceSetSize2(0, &g_DefAlloc); \
	}

void model_InstantiateArrays()
{
	MODEL_ARRAY_INSTANCE(NodeRelation, DefaultCache)
	MODEL_ARRAY_INSTANCE(NodeKeyFrame, NoCache)
	MODEL_ARRAY_INSTANCE(AnimNode*, NoCache)
	MODEL_ARRAY_INSTANCE(AnimKeyFrame, NoCache)
	MODEL_ARRAY_INSTANCE(ModelNode*, NoCache)
	MODEL_ARRAY_INSTANCE(ModelVert, NoCache)
	MODEL_ARRAY_INSTANCE(ModelTri, NoCache)
	MODEL_ARRAY_INSTANCE(PieceLOD, NoCache)
	MODEL_ARRAY_INSTANCE(float, DefaultCache)
	MODEL_ARRAY_INSTANCE(WeightSet*, NoCache)
	MODEL_ARRAY_INSTANCE(NewVertexWeight, NoCache)
	MODEL_ARRAY_INSTANCE(LTMatrix, NoCache)
	MODEL_ARRAY_INSTANCE(LODDistance, DefaultCache)
	MODEL_ARRAY_INSTANCE(ModelSocket*, NoCache)
	MODEL_ARRAY_INSTANCE(AnimInfo, NoCache)
	MODEL_ARRAY_INSTANCE(ModelPiece*, NoCache)

	LDelete(&g_DefAlloc, (ModelPiece*)LTNULL);
	LDelete(&g_DefAlloc, (WeightSet*)LTNULL);
}
