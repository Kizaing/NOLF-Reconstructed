// Jupiter runtime/world/src/de_nodes.cpp: the global NODE_IN/NODE_OUT nodes.
// Talon also keeps the code that files objects into the BSP here: an object is linked to the node its
// sphere straddles, and a PolyGrid is linked into every leaf its box touches (LeafLinks).
#include "bdefs.h"
#include "de_world.h"
#include "de_objects.h"
#include "../../build/proj/LT2/lithshared/stdlith/struct_bank.h"


// Links a PolyGrid to one leaf (0x20 bytes).
struct LeafLink
{
	LTLink		m_LeafLink;		// 0x00 in Leaf::m_LeafLinks
	LTLink		m_ObjLink;		// 0x0c in LTPolyGrid::m_LeafLinks
	LTObject	*m_pObject;		// 0x18
	Leaf		*m_pLeaf;		// 0x1c
};


// 0x004e3488 (static: no symbol for the checker)
static Node _g_InNode(NF_IN);
// 0x004e34a0
static Node _g_OutNode(NF_OUT);

Node *NODE_IN = &_g_InNode;
Node *NODE_OUT = &_g_OutNode;

// FUNCTION: LITHTECH 0x00430010 _$E2
// FUNCTION: LITHTECH 0x00430020 _$E1
// FUNCTION: LITHTECH 0x00430060 _$E5
// FUNCTION: LITHTECH 0x00430070 _$E4


// Globals so the recursion uses less stack.
// GLOBAL: LITHTECH 0x004e34b8
static int g_FilterSide;
// GLOBAL: LITHTECH 0x004e34bc
static int g_FilterPoint;
// GLOBAL: LITHTECH 0x004e34c0
static LeafLink *g_pFilterLink;

// GLOBAL: LITHTECH 0x004e34c4
static StructBank g_LeafLinkBank;
// GLOBAL: LITHTECH 0x004e34e0
static int g_LeafLinkBankRef;


// FUNCTION: LITHTECH 0x004300b0
void obj_Init()
{
	if(g_LeafLinkBankRef == 0)
	{
		sb_Init(&g_LeafLinkBank, sizeof(LeafLink), 32);
	}

	++g_LeafLinkBankRef;
}

// FUNCTION: LITHTECH 0x004300e0
void obj_Term()
{
	--g_LeafLinkBankRef;
	if(g_LeafLinkBankRef == 0)
	{
		sb_Term(&g_LeafLinkBank);
	}
}

// Finds the node whose plane the sphere straddles.
// FUNCTION: LITHTECH 0x00430100
Node* w_FindObjectNode(Node *pRoot, LTVector *pPos, float radius)
{
	float dist;
	LTVector pos = *pPos;

	for(;;)
	{
		dist = pRoot->GetPlane()->DistTo(pos);

		if(dist > radius)
		{
			if(pRoot->m_Sides[1] == NODE_IN)
				return pRoot;

			pRoot = pRoot->m_Sides[1];
		}
		else if(dist < -radius)
		{
			pRoot = pRoot->m_Sides[0];
			if(pRoot == NODE_OUT)
				return LTNULL;
		}
		else
		{
			return pRoot;
		}
	}
}

// Links pObj into every leaf the convex hull of pPoints touches.
// FUNCTION: LITHTECH 0x004301a0
void w_FilterPointsIntoLeaves(WorldBsp *pBsp, Node *pRoot, LTVector *pPoints, int nPoints,
	LTObject *pObj, LTLink *pList)
{
	int side;
	Leaf *pLeaf;

	while(!(pRoot->m_Flags & (NF_IN|NF_OUT)))
	{
		if(pRoot->m_iLeaf != 0xFFFF)
		{
			if(pRoot->m_iLeaf < pBsp->m_nLeafs)
				pLeaf = &pBsp->m_Leafs[pRoot->m_iLeaf];
			else
				pLeaf = LTNULL;

			g_pFilterLink = (LeafLink*)sb_Allocate(&g_LeafLinkBank);
			if(g_pFilterLink)
			{
				g_pFilterLink->m_ObjLink.m_pData = g_pFilterLink;
				g_pFilterLink->m_LeafLink.m_pData = g_pFilterLink;
				g_pFilterLink->m_pObject = pObj;
				g_pFilterLink->m_pLeaf = pLeaf;
				dl_Insert(&pLeaf->m_LeafLinks, &g_pFilterLink->m_LeafLink);
				dl_Insert(pList, &g_pFilterLink->m_ObjLink);
			}
			return;
		}

		side = pRoot->GetPlane()->DistTo(pPoints[0]) > 0.0f;
		for(g_FilterPoint=1; g_FilterPoint < nPoints; g_FilterPoint++)
		{
			g_FilterSide = pRoot->GetPlane()->DistTo(pPoints[g_FilterPoint]) > 0.0f;
			if(g_FilterSide != side)
			{
				side = 2;
				break;
			}
		}

		if(side == 2)
		{
			w_FilterPointsIntoLeaves(pBsp, pRoot->m_Sides[0], pPoints, nPoints, pObj, pList);
			pRoot = pRoot->m_Sides[1];
		}
		else
		{
			pRoot = pRoot->m_Sides[side];
		}
	}
}

// Only FPU scheduling differs: the original issues all eight y stores before the x/z stores and ends with fld st(0)/fstp instead of fst.
// STUB: LITHTECH 0x004303a0
void w_AddPolyGridToLeaves(WorldBsp *pBsp, LTPolyGrid *pGrid)
{
	LTVector pts[8];
	LTMatrix mat;
	float halfX, halfZ;
	int i;

	halfX = (float)pGrid->m_Width * 0.5f;
	halfZ = (float)pGrid->m_Height * 0.5f;

	pts[0].Init(-halfX, -127.0f, -halfZ);
	pts[1].Init(halfX, -127.0f, -halfZ);
	pts[2].Init(halfX, 127.0f, -halfZ);
	pts[3].Init(-halfX, 127.0f, -halfZ);
	pts[4].Init(-halfX, -127.0f, halfZ);
	pts[5].Init(halfX, -127.0f, halfZ);
	pts[6].Init(halfX, 127.0f, halfZ);
	pts[7].Init(-halfX, 127.0f, halfZ);

	pGrid->SetupTransform(mat);
	for(i=0; i < 8; i++)
	{
		MatVMul_InPlace_H(&mat, &pts[i]);
	}

	w_FilterPointsIntoLeaves(pBsp, pBsp->m_RootNode, pts, 8, pGrid, &pGrid->m_LeafLinks);
}

// FUNCTION: LITHTECH 0x00430590
void w_RemovePolyGridFromLeaves(LTPolyGrid *pGrid)
{
	LTLink *pCur, *pNext;
	LeafLink *pLink;

	pCur = pGrid->m_LeafLinks.m_pNext;
	while(pCur != &pGrid->m_LeafLinks)
	{
		pNext = pCur->m_pNext;

		pLink = (LeafLink*)pCur->m_pData;
		dl_Remove(&pLink->m_LeafLink);
		dl_Remove(&pLink->m_ObjLink);
		((StructLink*)pLink)->m_pSLNext = g_LeafLinkBank.m_FreeListHead;
		g_LeafLinkBank.m_FreeListHead = (StructLink*)pLink;

		pCur = pNext;
	}
}

// FUNCTION: LITHTECH 0x004305f0
Node* w_AddObjectToLeaf(WorldBsp *pBsp, LTObject *pObj)
{
	Node *pNode;

	w_RemoveObjectFromLeaf(pObj);

	pNode = w_FindObjectNode(pBsp->m_RootNode, &pObj->m_Pos, pObj->LTObject::GetRadius());
	if(pNode)
	{
		if(pObj->sd)
		{
			dl_Insert(&pNode->m_Objects, &pObj->m_Link6C);
		}
		else
		{
			dl_Insert(pNode->m_Objects.m_pPrev, &pObj->m_Link6C);
			if(pObj->m_ObjectType == OT_POLYGRID)
				w_AddPolyGridToLeaves(pBsp, (LTPolyGrid*)pObj);
		}

		pObj->m_Unknown78 = (uint32)pNode;
	}

	return pNode;
}

// FUNCTION: LITHTECH 0x00430680
void w_RemoveObjectFromLeaf(LTObject *pObj)
{
	if(pObj->m_Unknown78)
	{
		dl_Remove(&pObj->m_Link6C);
		pObj->m_Unknown78 = 0;
	}

	if(pObj->m_ObjectType == OT_POLYGRID)
		w_RemovePolyGridFromLeaves((LTPolyGrid*)pObj);
}
