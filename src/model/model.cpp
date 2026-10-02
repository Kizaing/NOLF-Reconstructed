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
			total += pLOD->m_nTris;
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
		total += GetPiece(i)->m_nVerts;
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
