// Jupiter runtime/world/src/world_tree.cpp, Talon version.
// Talon differences: WorldTree and WorldTreeObj have vtables (the queries are virtual),
// nodes are allocated one by one with new and keep 4 child pointers, a center vector and a
// radius, there is a third node object list for vis containers, and the tree runs the
// visibility query (DoVisQuery) for the renderer.
#include "bdefs.h"
#include "world_tree.h"
#include "objectmgr.h"
#include <math.h>


// Used by some of the recursive routines.
typedef void (*FilterFn_R)(WorldTreeNode *pNode, void *pData);


class FilterObjInfo
{
public:
	WorldTreeObj		*m_pObj;
	uint32				m_iObjArray;
	LTVector			m_Min;
	LTVector			m_Max;
	float				m_MaxSize;
	uint32				m_iCurLink;
};


class ISInfo
{
public:
	WorldTree		*m_pTree;
	uint32			m_iObjArray;
	LTVector		m_Pts[2];
	ISCallback		m_CB;
	void			*m_pCBUser;
};


// -------------------------------------------------------------------------------- //
// WorldTree internal helpers.
// -------------------------------------------------------------------------------- //

#define OBJ_NODE_LINK_ALWAYSVIS	4

inline void FilterBox(const LTVector *pMin, const LTVector *pMax,
	WorldTreeNode *pNode, FilterFn_R fn, void *pFnData)
{
	if(pMin->x < pNode->m_Center.x)
	{
		if(pMin->z < pNode->m_Center.z)
		{
			fn(pNode->GetChild(0, 0), pFnData);
		}

		if(pMax->z > pNode->m_Center.z)
		{
			fn(pNode->GetChild(0, 1), pFnData);
		}
	}

	if(pMax->x > pNode->m_Center.x)
	{
		if(pMin->z < pNode->m_Center.z)
		{
			fn(pNode->GetChild(1, 0), pFnData);
		}

		if(pMax->z > pNode->m_Center.z)
		{
			fn(pNode->GetChild(1, 1), pFnData);
		}
	}
}


// Returns true if the boxes intersect or touch.
inline LTBOOL DoBoxesTouch(const LTVector& vMin1, const LTVector& vMax1,
	const LTVector& vMin2, const LTVector& vMax2)
{
	return !(vMin1.x > vMax2.x || vMin1.y > vMax2.y || vMin1.z > vMax2.z ||
			vMax1.x < vMin2.x || vMax1.y < vMin2.y || vMax1.z < vMin2.z);
}


// Filters the vis query box down the tree.  Nodes with vis containers hand the
// query to the vis containers instead of recursing.
// FUNCTION: LITHTECH 0x0049efd0
void DoVisQuery_R(WorldTreeNode *pNode, VisQueryRequest *pInfo, uint32 depth)
{
	LTLink *pCur, *pListHead;
	WorldTreeObj *pObj;
	LTVector vMin, vMax;

	if(!pInfo->m_NodeFilterFn(pNode))
		return;

	// Let the vis containers do the work if there are any.
	pListHead = pNode->m_Objects[NOA_VisContainers].AsLTLink();
	if(pListHead->m_pNext != pListHead)
	{
		for(pCur=pListHead->m_pNext; pCur != pListHead;)
		{
			pObj = (WorldTreeObj*)pCur->m_pData;
			pCur = pCur->m_pNext;

			if(pObj->m_WTFrameCode != pInfo->m_pTree->m_nTempFrameCode)
			{
				pObj->m_WTFrameCode = pInfo->m_pTree->m_nTempFrameCode;
				pObj->WTSlot7(pInfo);
			}
		}

		return;
	}

	pListHead = pNode->m_Objects[pInfo->m_iObjArray].AsLTLink();
	for(pCur=pListHead->m_pNext; pCur != pListHead;)
	{
		pObj = (WorldTreeObj*)pCur->m_pData;
		pCur = pCur->m_pNext;

		if(pObj->m_WTFrameCode == pInfo->m_pTree->m_nTempFrameCode)
			continue;

		pObj->m_WTFrameCode = pInfo->m_pTree->m_nTempFrameCode;

		pObj->GetBBox(vMin, vMax);
		if(DoBoxesTouch(vMin, vMax, pInfo->m_BoxMin, pInfo->m_BoxMax))
		{
			pInfo->m_AddObject(pObj, pInfo->m_pUserData);
		}
	}

	if(pNode->HasChildren())
	{
		if(pInfo->m_BoxMin.x < pNode->m_Center.x)
		{
			if(pInfo->m_BoxMin.z < pNode->m_Center.z)
				DoVisQuery_R(pNode->GetChild(0, 0), pInfo, depth+1);

			if(pInfo->m_BoxMax.z > pNode->m_Center.z)
				DoVisQuery_R(pNode->GetChild(0, 1), pInfo, depth+1);
		}

		if(pInfo->m_BoxMax.x > pNode->m_Center.x)
		{
			if(pInfo->m_BoxMin.z < pNode->m_Center.z)
				DoVisQuery_R(pNode->GetChild(1, 0), pInfo, depth+1);

			if(pInfo->m_BoxMax.z > pNode->m_Center.z)
				DoVisQuery_R(pNode->GetChild(1, 1), pInfo, depth+1);
		}
	}
}


// -------------------------------------------------------------------------------- //
// WorldTreeObj.
// -------------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0049f180
WorldTreeObj::WorldTreeObj(WTObjType objType)
{
	uint32 i;
	WTObjLink *pLink;

	for(i=0; i < MAX_OBJ_NODE_LINKS; i++)
	{
		pLink = &m_Links[i];

		pLink->m_Link.TieOff();
		pLink->m_Link.m_pData = this;
		pLink->m_pNode = LTNULL;
	}

	m_ObjType = objType;
	m_WTFrameCode = FRAMECODE_NOTINTREE;
}


// FUNCTION: LITHTECH 0x0049f1c0
WorldTreeObj::~WorldTreeObj()
{
	RemoveFromWorldTree();
}


// FUNCTION: LITHTECH 0x0049f1d0
void WorldTreeObj::RemoveFromWorldTree()
{
	uint32 i;
	WTObjLink *pLink;

	m_WTFrameCode = FRAMECODE_NOTINTREE;
	for(i=0; i < MAX_OBJ_NODE_LINKS; i++)
	{
		pLink = &m_Links[i];

		pLink->m_Link.Remove();

		// Remove references from this node all the way up the tree.
		while(pLink->m_pNode)
		{
			pLink->m_pNode->m_nObjectsOnOrBelow--;
			pLink->m_pNode = pLink->m_pNode->m_pParent;
		}
	}
}



// -------------------------------------------------------------------------------- //
// WorldTreeNode.
// -------------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0049f220
WorldTreeNode::WorldTreeNode()
{
	Clear();
}


// FUNCTION: LITHTECH 0x0049f230
WorldTreeNode::~WorldTreeNode()
{
	Term();
}


// FUNCTION: LITHTECH 0x0049f240
void WorldTreeNode::Clear()
{
	uint32 i;

	m_pParent = LTNULL;
	m_nObjectsOnOrBelow = 0;

	for(i=0; i < NUM_NODEOBJ_ARRAYS; i++)
	{
		m_Objects[i].TieOff();
	}

	for(i=0; i < MAX_WTNODE_CHILDREN; i++)
	{
		m_Children[i] = LTNULL;
	}

	m_Center.Init();
	m_BBoxMin.Init();
	m_BBoxMax.Init();
	m_SmallestDim = 0.0f;
	m_Radius = 0.0f;
}


// FUNCTION: LITHTECH 0x0049f290
void WorldTreeNode::GetBBox(LTVector *pMin, LTVector *pMax)
{
	*pMin = m_BBoxMin;
	*pMax = m_BBoxMax;
}


// FUNCTION: LITHTECH 0x0049f2d0
void WorldTreeNode::SetBBox(LTVector boxMin, LTVector boxMax)
{
	LTVector vHalfDims;

	m_BBoxMin = boxMin;
	m_BBoxMax = boxMax;

	vHalfDims = (boxMax - boxMin) * 0.5f;
	m_Center = boxMin + vHalfDims;
	m_SmallestDim = LTMIN(boxMax.x - boxMin.x, boxMax.z - boxMin.z);
	m_Radius = vHalfDims.Mag();
}


// FUNCTION: LITHTECH 0x0049f3c0
void WorldTreeNode::Term()
{
	uint32 i;
	LTLink *pCur, *pNext;
	WorldTreeObj *pObj;

	// Free child nodes.
	TermChildren();

	// Remove all the objects, so they don't have bad pointers into us.
	for(i=0; i < NUM_NODEOBJ_ARRAYS; i++)
	{
		for(pCur=m_Objects[i].m_pNext; pCur != (LTLink*)&m_Objects[i]; pCur=pNext)
		{
			pNext = pCur->m_pNext;
			pObj = ((WorldTreeObj*)pCur->m_pData);
			pObj->RemoveFromWorldTree();
		}
	}

	Clear();
}


// FUNCTION: LITHTECH 0x0049f400
void WorldTreeNode::TermChildren()
{
	uint32 i;

	for(i=0; i < MAX_WTNODE_CHILDREN; i++)
	{
		if(m_Children[i])
		{
			delete m_Children[i];
			m_Children[i] = LTNULL;
		}
	}
}


// FUNCTION: LITHTECH 0x0049f440
LTBOOL WorldTreeNode::LoadLayout(ILTStream *pStream, uint8 &curByte, uint8 &curBit)
{
	uint32 i;
	LTBOOL bSubdivide;

	TermChildren();

	// Read the next bit.
	if(curBit == 8)
	{
		*pStream >> curByte;
		curBit = 0;
	}

	bSubdivide = !!(curByte & (1<<curBit));
	++curBit;

	if(bSubdivide)
	{
		if(!Subdivide())
			return LTFALSE;

		for(i=0; i < MAX_WTNODE_CHILDREN; i++)
		{
			if(!m_Children[i]->LoadLayout(pStream, curByte, curBit))
			{
				TermChildren();
				return LTFALSE;
			}
		}
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0049f4e0
LTBOOL WorldTreeNode::Subdivide()
{
	uint32 i;
	LTVector vHalfDims;

	for(i=0; i < MAX_WTNODE_CHILDREN; i++)
	{
		m_Children[i] = new WorldTreeNode;
		if(!m_Children[i])
		{
			Term();
			return LTFALSE;
		}

		m_Children[i]->m_pParent = this;
	}

	vHalfDims = (m_BBoxMax - m_BBoxMin) * 0.5f;

	// -x -z
	GetChild(0, 0)->SetBBox(
		LTVector(m_Center.x - vHalfDims.x, m_BBoxMin.y, m_Center.z - vHalfDims.z),
		LTVector(m_Center.x, m_BBoxMax.y, m_Center.z));

	// -x +z
	GetChild(0, 1)->SetBBox(
		LTVector(m_Center.x - vHalfDims.x, m_BBoxMin.y, m_Center.z),
		LTVector(m_Center.x, m_BBoxMax.y, m_Center.z + vHalfDims.z));

	// +x -z
	GetChild(1, 0)->SetBBox(
		LTVector(m_Center.x, m_BBoxMin.y, m_Center.z - vHalfDims.z),
		LTVector(m_Center.x + vHalfDims.x, m_BBoxMax.y, m_Center.z));

	// +x +z
	GetChild(1, 1)->SetBBox(
		LTVector(m_Center.x, m_BBoxMin.y, m_Center.z),
		LTVector(m_Center.x + vHalfDims.x, m_BBoxMax.y, m_Center.z + vHalfDims.z));

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0049f730
void WorldTreeNode::AddObjectToList(WTObjLink *pLink, uint32 iArray)
{
	WorldTreeNode *pTempNode;

	dl_Insert(&m_Objects[iArray], &pLink->m_Link);
	pLink->m_pNode = this;

	// Add a reference to all the nodes above here.
	pTempNode = this;
	while(pTempNode)
	{
		pTempNode->m_nObjectsOnOrBelow++;
		pTempNode = pTempNode->m_pParent;
	}
}



// -------------------------------------------------------------------------------- //
// WorldTree.
// -------------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0049f770
WorldTree::WorldTree()
{
	m_pHelper = LTNULL;
	m_Unknown68 = 0;
	m_Unknown6c = 0;
	m_TerrainDepth = 0;
	m_AlwaysVisObjects.TieOff();
}


// FUNCTION: LITHTECH 0x0049f7a0
void WorldTree::WTreeSlot6(void *p1, void *p2)
{
}


// FUNCTION: LITHTECH 0x0049f7b0
uint32 WorldTree::GetFrameCode()
{
	return m_pHelper->GetFrameCode();
}


// FUNCTION: LITHTECH 0x0049f7c0
void WorldTree::InitWorldTree(WorldTreeHelper *pHelper)
{
	m_pHelper = pHelper;
	m_AlwaysVisObjects.Init();
}


// FUNCTION: LITHTECH 0x0049f7e0
void WorldTree::Term()
{
	m_RootNode.Term();
	m_Unknown68 = 0;
	m_Unknown6c = 0;
	m_AlwaysVisObjects.Term();
}


// FUNCTION: LITHTECH 0x0049f810
WorldTreeNode* WorldTree::FindNode(void *pNodePath)
{
	uint8 *pPath;
	WorldTreeNode *pNode;
	uint32 i, iByte, iBit, iChild;

	pPath = (uint8*)pNodePath;
	pNode = &m_RootNode;
	i = 0;
	iBit = 0;
	iByte = 0;
	for(; i != *((uint32*)&pPath[4]); i++)
	{
		if(!pNode->HasChildren())
			return LTNULL;

		iChild = (pPath[iByte] >> iBit) & 3;
		iBit += 2;
		if(iBit == 8)
		{
			iByte++;
			iBit = 0;
			if(iByte >= 4)
				return LTNULL;
		}

		pNode = pNode->m_Children[iChild];
	}

	return pNode;
}


// FUNCTION: LITHTECH 0x0049f870
void WorldTree::InsertObject(WorldTreeObj *pObj, uint32 iArray)
{
	LTVector vMin, vMax;

	pObj->GetBBox(vMin, vMax);
	InsertObject2(pObj, vMin, vMax, iArray);
}


static void FilterObj_R(WorldTreeNode *pNode, FilterObjInfo *pInfo);

// FUNCTION: LITHTECH 0x0049f8b0
void WorldTree::InsertObject2(WorldTreeObj *pObj, const LTVector &vMin, const LTVector &vMax,
	uint32 iArray)
{
	FilterObjInfo foInfo;
	LTVector vDiff;

	pObj->RemoveFromWorldTree();

	vDiff = vMax - vMin;

	foInfo.m_pObj = pObj;
	foInfo.m_iObjArray = iArray;
	foInfo.m_Min = vMin;
	foInfo.m_Max = vMax;
	foInfo.m_MaxSize = LTMAX(vDiff.x, vDiff.z);
	foInfo.m_iCurLink = 0;

	if(!pObj->InsertSpecial(this))
	{
		FilterObj_R(&m_RootNode, &foInfo);
	}
}


// FUNCTION: LITHTECH 0x0049f970
static void FilterObj_R(WorldTreeNode *pNode, FilterObjInfo *pInfo)
{
	// Tell the vis container about the object.
	if(pNode->m_Objects[NOA_VisContainers].m_pNext != (LTLink*)&pNode->m_Objects[NOA_VisContainers])
	{
		((WorldTreeObj*)pNode->m_Objects[NOA_VisContainers].m_pNext->m_pData)->WTSlot6((LTObject*)pInfo->m_pObj);
	}

	if(pInfo->m_MaxSize >= (pNode->m_SmallestDim * 0.5f) || !pNode->HasChildren())
	{
		// This shouldn't ever happen.  If it does, the object won't be
		// located correctly.
		if(pInfo->m_iCurLink >= MAX_OBJ_NODE_LINKS)
			return;

		// Ok, it's likely to cover the space of all the nodes below us anyways, so add it
		// to this node and stop recursing.
		pNode->AddObjectToList(&pInfo->m_pObj->m_Links[pInfo->m_iCurLink], pInfo->m_iObjArray);

		if(pInfo->m_pObj->WTSlot4())
		{
			dl_Insert(&pNode->m_Objects[NOA_VisContainers], pInfo->m_pObj->WTSlot5(pInfo->m_iCurLink));
		}

		pInfo->m_iCurLink++;
	}
	else
	{
		FilterBox(&pInfo->m_Min, &pInfo->m_Max, pNode, (FilterFn_R)FilterObj_R, pInfo);
	}
}


// FUNCTION: LITHTECH 0x0049fa80
void WorldTree::FindObjectsInBox(const LTVector *pMin, const LTVector *pMax,
	WTObjCallback cb, void *pCBUser, uint32 iArray)
{
	FindObjInfo foInfo;

	foInfo.m_iObjArray = iArray;
	foInfo.m_Min = *pMin;
	foInfo.m_Max = *pMax;
	foInfo.m_CB = cb;
	foInfo.m_pCBUser = pCBUser;

	FindObjectsInBox2(&foInfo);
}


static void FindObjectsInBox_R(WorldTreeNode *pNode, FindObjInfo *pInfo);

// FUNCTION: LITHTECH 0x0049fb00
void WorldTree::FindObjectsInBox2(FindObjInfo *pInfo)
{
	m_nTempFrameCode = m_pHelper->IncFrameCode();

	pInfo->m_pTree = this;
	FindObjectsInBox_R(&m_RootNode, pInfo);
}


// Filters the box down the tree and calls the callback for any objects
// that the box touches.
// FUNCTION: LITHTECH 0x0049fb30
static void FindObjectsInBox_R(WorldTreeNode *pNode, FindObjInfo *pInfo)
{
	LTLink *pCur, *pListHead;
	WorldTreeObj *pObj;
	LTVector vMin, vMax;
	uint32 frameCode;

	frameCode = pInfo->m_pTree->m_nTempFrameCode;

	// Check objects sitting on this node.
	if(pNode->m_nObjectsOnOrBelow == 0)
		return;

	pListHead = pNode->m_Objects[pInfo->m_iObjArray].AsLTLink();
	for(pCur=pListHead->m_pNext; pCur != pListHead;)
	{
		pObj = (WorldTreeObj*)pCur->m_pData;

		// KEF - 04/03/00 - Increment the link pointer here in case this link goes away in the callback
		pCur = pCur->m_pNext;

		// Check the frame code.
		if(pObj->m_WTFrameCode == frameCode)
			continue;

		pObj->m_WTFrameCode = frameCode;

		// Do the boxes intersect?
		pObj->GetBBox(vMin, vMax);
		if(DoBoxesTouch(vMin, vMax, pInfo->m_Min, pInfo->m_Max))
		{
			pInfo->m_CB(pObj, pInfo->m_pCBUser);
		}
	}

	// Recurse into appropriate nodes.
	if(pNode->HasChildren())
	{
		FilterBox(&pInfo->m_Min, &pInfo->m_Max, pNode, (FilterFn_R)FindObjectsInBox_R, pInfo);
	}
}


// FUNCTION: LITHTECH 0x0049fca0
void WorldTree::FindObjectsOnPoint(const LTVector *pPoint,
	WTObjCallback cb, void *pCBUser, uint32 iArray)
{
	FindObjectsInBox(pPoint, pPoint, cb, pCBUser, iArray);
}


static LTBOOL IntersectSegment_R(WorldTreeNode *pNode, ISInfo *pInfo);

// FUNCTION: LITHTECH 0x0049fcc0
void WorldTree::IntersectSegment(LTVector *pPt1, LTVector *pPt2,
	ISCallback cb, void *pCBUser, uint32 iArray)
{
	ISInfo isInfo;

	m_nTempFrameCode = m_pHelper->IncFrameCode();

	isInfo.m_pTree = this;
	isInfo.m_iObjArray = iArray;
	isInfo.m_Pts[0] = *pPt1;
	isInfo.m_Pts[1] = *pPt2;
	isInfo.m_CB = cb;
	isInfo.m_pCBUser = pCBUser;

	IntersectSegment_R(&m_RootNode, &isInfo);
}


// Returns BackSide if segment is behind boxMin, FrontSide if the segment
// is in front of boxMax, and Intersect otherwise.
inline PolySide GetDimBoxStatus(float pt0, float pt1, float boxMin, float boxMax)
{
	if(pt0 < boxMin && pt1 < boxMin)
		return BackSide;
	else if(pt0 > boxMax && pt1 > boxMax)
		return FrontSide;
	else
		return Intersect;
}


// Intersects the line segment with the plane at fPlane in one dimension, then sees if the
// intersection point is inside the box in the other dimension.
inline LTBOOL TestBoxPlaneInline(float pt1, float pt2, float fPlane,
	float boxMin, float boxMax, float otherPt1, float otherPt2)
{
	float fDiff, fTest;

	// Does it cross the plane?
	if((pt1 < fPlane) == (pt2 <= fPlane))
		return LTFALSE;

	fDiff = pt2 - pt1;
	if(fabs(fDiff) < 0.001f)
		fTest = otherPt1;
	else
		fTest = otherPt1 + ((fPlane - pt1) * (otherPt2 - otherPt1)) / fDiff;

	return fTest >= boxMin && fTest <= boxMax;
}

// The same, out of line (the inline budget runs out in TestNode).
// FUNCTION: LITHTECH 0x004a0150
static LTBOOL TestBoxPlane(float pt1, float pt2, float fPlane,
	float boxMin, float boxMax, float otherPt1, float otherPt2)
{
	return TestBoxPlaneInline(pt1, pt2, fPlane, boxMin, boxMax, otherPt1, otherPt2);
}


// Does tests to see if the segment intersects the node.  If so, calls IntersectSegment_R
// on it and returns the value.
// FUNCTION: LITHTECH 0x0049fe50
static LTBOOL TestNode(WorldTreeNode *pNode, ISInfo *pInfo)
{
	PolySide outStatus;

	// Trivial accept.
	if(base_IsPtInBoxXZ(&pInfo->m_Pts[0], &pNode->m_BBoxMin, &pNode->m_BBoxMax) ||
		base_IsPtInBoxXZ(&pInfo->m_Pts[1], &pNode->m_BBoxMin, &pNode->m_BBoxMax))
	{
		return IntersectSegment_R(pNode, pInfo);
	}
	else
	{
		// If both points are outside on the same side, then the line is outside.
		outStatus = GetDimBoxStatus(pInfo->m_Pts[0].x, pInfo->m_Pts[1].x, pNode->m_BBoxMin.x, pNode->m_BBoxMax.x);
		if(outStatus != Intersect)
		{
			if(outStatus == GetDimBoxStatus(pInfo->m_Pts[0].z, pInfo->m_Pts[1].z, pNode->m_BBoxMin.z, pNode->m_BBoxMax.z))
			{
				// Trivial reject.
				return LTFALSE;
			}
		}

		// Allllllllllll-righty, we'll do the extensive test!
		if(TestBoxPlaneInline(pInfo->m_Pts[0].x, pInfo->m_Pts[1].x, pNode->m_BBoxMin.x, pNode->m_BBoxMin.z, pNode->m_BBoxMax.z, pInfo->m_Pts[0].z, pInfo->m_Pts[1].z) ||
			TestBoxPlaneInline(pInfo->m_Pts[0].x, pInfo->m_Pts[1].x, pNode->m_BBoxMax.x, pNode->m_BBoxMin.z, pNode->m_BBoxMax.z, pInfo->m_Pts[0].z, pInfo->m_Pts[1].z) ||
			TestBoxPlane(pInfo->m_Pts[0].z, pInfo->m_Pts[1].z, pNode->m_BBoxMin.z, pNode->m_BBoxMin.x, pNode->m_BBoxMax.x, pInfo->m_Pts[0].x, pInfo->m_Pts[1].x) ||
			TestBoxPlane(pInfo->m_Pts[0].z, pInfo->m_Pts[1].z, pNode->m_BBoxMax.z, pNode->m_BBoxMin.x, pNode->m_BBoxMax.x, pInfo->m_Pts[0].x, pInfo->m_Pts[1].x))
		{
			return IntersectSegment_R(pNode, pInfo);
		}
	}

	return LTFALSE;
}


// Filters the line segment down the tree and calls the callback for objects
// in nodes that the line segment intersects.

// Returns true if it tested all subnodes and an object was hit (ie: it should
// return true up the stack and terminate early thus avoid tons of tests).
// FUNCTION: LITHTECH 0x0049fd40
static LTBOOL IntersectSegment_R(WorldTreeNode *pNode, ISInfo *pInfo)
{
	LTLink *pCur, *pListHead;
	WorldTreeObj *pObj;
	LTBOOL bIntersected;
	int iX, iZ;

	bIntersected = LTFALSE;

	// Visit objects in this node.
	pListHead = pNode->m_Objects[pInfo->m_iObjArray].AsLTLink();
	for(pCur=pListHead->m_pNext; pCur != pListHead;)
	{
		pObj = (WorldTreeObj*)pCur->m_pData;

		// KEF - 04/03/00 - Increment the link pointer here in case this link goes away in the callback
		pCur = pCur->m_pNext;

		// Check the frame code.
		if(pObj->m_WTFrameCode == pInfo->m_pTree->m_nTempFrameCode)
			continue;

		pObj->m_WTFrameCode = pInfo->m_pTree->m_nTempFrameCode;
		bIntersected |= pInfo->m_CB(pObj, pInfo->m_pCBUser);
	}

	// Recurse into child nodes.
	if(pNode->HasChildren())
	{
		// Test in front to back order.
		iX = pInfo->m_Pts[0].x > pNode->m_Center.x;
		iZ = pInfo->m_Pts[0].z > pNode->m_Center.z;

		LTBOOL bResult = TestNode(pNode->m_Children[iX*2 + iZ], pInfo);
		bResult |= TestNode(pNode->m_Children[!iX*2 + iZ], pInfo);
		bResult |= TestNode(pNode->m_Children[iX*2 + !iZ], pInfo);
		bResult |= TestNode(pNode->m_Children[!iX*2 + !iZ], pInfo);

		if(bResult)
			return LTTRUE;
	}

	return bIntersected;
}

static LTBOOL DefaultNodeFilterFn(WorldTreeNode *pNode);
static void DoAlwaysVisObjects(CheapLTLink *pListHead, VisQueryRequest *pInfo);

// FUNCTION: LITHTECH 0x004a01e0
void WorldTree::DoVisQuery(VisQueryRequest *pInfo)
{
	m_nTempFrameCode = m_pHelper->IncFrameCode();

	pInfo->m_pTree = this;
	pInfo->m_BoxMin = pInfo->m_Viewpoint - LTVector(pInfo->m_ViewRadius, pInfo->m_ViewRadius, pInfo->m_ViewRadius);
	pInfo->m_BoxMax = pInfo->m_Viewpoint + LTVector(pInfo->m_ViewRadius, pInfo->m_ViewRadius, pInfo->m_ViewRadius);

	if(!pInfo->m_NodeFilterFn)
		pInfo->m_NodeFilterFn = DefaultNodeFilterFn;

	DoVisQuery_R(&m_RootNode, pInfo, 0);
	DoAlwaysVisObjects(&m_AlwaysVisObjects, pInfo);
}


// FUNCTION: LITHTECH 0x004a02b0
static LTBOOL DefaultNodeFilterFn(WorldTreeNode *pNode)
{
	return LTTRUE;
}


// FUNCTION: LITHTECH 0x004a02c0
static void DoAlwaysVisObjects(CheapLTLink *pListHead, VisQueryRequest *pInfo)
{
	LTLink *pCur;
	WorldTreeObj *pObj;

	for(pCur=pListHead->m_pNext; pCur != (LTLink*)pListHead;)
	{
		pObj = (WorldTreeObj*)pCur->m_pData;
		pCur = pCur->m_pNext;

		if(pObj->m_WTFrameCode != pInfo->m_pTree->m_nTempFrameCode)
		{
			pObj->m_WTFrameCode = pInfo->m_pTree->m_nTempFrameCode;
			pInfo->m_AddObject(pObj, pInfo->m_pUserData);
		}
	}
}


// FUNCTION: LITHTECH 0x004a0300
LTBOOL WorldTree::Inherit(WorldTree *pOther)
{
	// Clear out any old data.
	Term();

	m_TerrainDepth = pOther->m_TerrainDepth;
	m_Unknown6c = pOther->m_Unknown6c;
	m_Unknown68 = pOther->m_Unknown68;

	// Create an identical node layout.
	m_RootNode.SetBBox(pOther->m_RootNode.m_BBoxMin, pOther->m_RootNode.m_BBoxMax);
	return CopyNodeLayout_R(&m_RootNode, &pOther->m_RootNode);
}


// FUNCTION: LITHTECH 0x004a0370
LTBOOL WorldTree::LoadLayout(ILTStream *pStream)
{
	LTVector boxMin, boxMax;
	uint32 nDummyNumNodes;
	uint8 curByte, curBit;

	*pStream >> boxMin >> boxMax;
	*pStream >> nDummyNumNodes;
	*pStream >> m_TerrainDepth;

	curByte = 0;
	curBit = 8;
	m_RootNode.SetBBox(boxMin, boxMax);

	if(m_RootNode.LoadLayout(pStream, curByte, curBit) && pStream->ErrorStatus() == LT_OK)
		return LTTRUE;

	return LTFALSE;
}


// FUNCTION: LITHTECH 0x004a0470
LTBOOL WorldTree::CopyNodeLayout_R(WorldTreeNode *pDest, WorldTreeNode *pSrc)
{
	uint32 i;

	if(pSrc->HasChildren())
	{
		if(!pDest->Subdivide())
			return LTFALSE;

		for(i=0; i < MAX_WTNODE_CHILDREN; i++)
		{
			if(!CopyNodeLayout_R(pDest->m_Children[i], pSrc->m_Children[i]))
				return LTFALSE;
		}
	}

	return LTTRUE;
}


// Insert an object into the always-visible list.
// FUNCTION: LITHTECH 0x004a04d0
void WorldTree::InsertAlwaysVisObject(WorldTreeObj *pObj)
{
	WTObjLink *pLink = &pObj->m_Links[OBJ_NODE_LINK_ALWAYSVIS];
	dl_Insert(&m_AlwaysVisObjects, &pLink->m_Link);
	pLink->m_pNode = LTNULL;
}
