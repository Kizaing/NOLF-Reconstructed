// Talon world tree object (Jupiter runtime/world/src/world_tree.h). Only recovered members.
// Talon's WorldTreeObj keeps its bounding box in LTObject (0xf4/0x100) and makes it virtual,
// and has four more virtual slots than Jupiter (names unknown).
#ifndef __WORLD_TREE_H__
#define __WORLD_TREE_H__

#include "ltbasedefs.h"

#define MAX_OBJ_NODE_LINKS	5
#define FRAMECODE_NOTINTREE	0xFFFFFFFF

class WorldTree;
class WorldTreeNode;
class WorldTreeHelper;

typedef uint32 WTObjType;

class WTObjLink;

#define MAX_WTNODE_CHILDREN	4
#define NUM_NODEOBJ_ARRAYS	3

// Talon world tree node (0x5c bytes); WorldTree's root node is at WorldTree+0xc.
// Talon allocates each child node with new instead of using a node list.
class WorldTreeNode
{
public:
	WorldTreeNode();								// 0x0049f220
	~WorldTreeNode();								// 0x0049f230

	void			Clear();						// 0x0049f240
	void			Term();							// 0x0049f3c0
	void			TermChildren();					// 0x0049f400

	// Get/set the bounding box (SetBBox also sets the center, smallest dim and radius).
	void			GetBBox(LTVector *pMin, LTVector *pMax);	// 0x0049f290
	void			SetBBox(LTVector boxMin, LTVector boxMax);	// 0x0049f2d0

	// Load the tree layout (one bit per node: subdivided or not).
	LTBOOL			LoadLayout(ILTStream *pStream, uint8 &curByte, uint8 &curBit);	// 0x0049f440

	// Create the 4 child nodes.
	LTBOOL			Subdivide();					// 0x0049f4e0

	// Adds an object to the specified list (0x0049f730).
	void			AddObjectToList(WTObjLink *pLink, uint32 iArray);

	WorldTreeNode*	GetChild(uint32 nX, uint32 nZ)	{ return m_Children[nX*2 + nZ]; }
	LTBOOL			HasChildren()					{ return !!m_Children[0]; }

public:
	// All the objects sitting on this node (NOA_ index).
	CheapLTLink		m_Objects[NUM_NODEOBJ_ARRAYS];	// 0x00

	LTVector		m_BBoxMin;					// 0x18
	LTVector		m_BBoxMax;					// 0x24
	LTVector		m_Center;					// 0x30
	float			m_SmallestDim;				// 0x3c smallest of the X and Z dimensions
	float			m_Radius;					// 0x40 half the box diagonal

	// How many objects are on or below this node? Used to stop recursion early.
	uint32			m_nObjectsOnOrBelow;		// 0x44

	WorldTreeNode	*m_pParent;					// 0x48
	WorldTreeNode	*m_Children[MAX_WTNODE_CHILDREN];	// 0x4c (-x-z, -x+z, +x-z, +x+z)
};

// This links a WorldTreeObj to a node (0x10 bytes).
class WTObjLink
{
public:
	LTLink			m_Link;		// 0x00
	WorldTreeNode	*m_pNode;	// 0x0c
};

// 0x5c bytes. vtable 0x004c8b54.
class WorldTreeObj
{
public:
	// 0x0049f180
	WorldTreeObj(WTObjType objType);
	// 0x0049f1c0
	virtual ~WorldTreeObj();

	inline WTObjType GetObjType() {return m_ObjType;}

	// This is called before the object is added to the world tree.
	// If you return LTTRUE, then it assumes you added yourself.
	virtual LTBOOL InsertSpecial(WorldTree *pTree) {return LTFALSE;}

	// Unlink everything from the world tree (0x0049f1d0).
	virtual void RemoveFromWorldTree();

	// Get the bounding box of the object.
	virtual void GetBBox(LTVector &vMin, LTVector &vMax)=0;

	// Slots 4-7: names unknown (defaults return 0 / do nothing).
	virtual LTBOOL WTSlot4() {return LTFALSE;}
	virtual LTLink* WTSlot5(uint32 i) {return LTNULL;}
	virtual void WTSlot6(class LTObject *pObj) {}
	virtual void WTSlot7(void *p) {}

public:
	WTObjLink		m_Links[MAX_OBJ_NODE_LINKS];	// 0x04

	// Tells what kind of object this is.
	WTObjType		m_ObjType;						// 0x54

	// Set to FRAMECODE_NOTINTREE if the object is not in the WorldTree.
	uint32			m_WTFrameCode;					// 0x58
};

// WorldTreeObj::m_ObjType values.
#define WTObj_DObject	0

typedef void (*WTObjCallback)(WorldTreeObj *pObj, void *pUser);

// IntersectSegment callback.  Return LTTRUE if you detected an intersection to
// assist with early termination.
typedef LTBOOL (*ISCallback)(WorldTreeObj *pObj, void *pUser);

// Which object array of the tree nodes to search.
#define NOA_Objects		0
#define NOA_Lights		1
#define NOA_VisContainers	2	// vis container objects (physics/vis BSPs, terrain sections)

// Used for box queries (0x34 bytes).
class FindObjInfo
{
public:
	FindObjInfo()
	{
		m_pTree = LTNULL;
		m_iObjArray = NOA_Objects;
		m_Min.Init();
		m_Max.Init();
		m_Unknown20.Init();
		m_CB = LTNULL;
		m_pCBUser = LTNULL;
	}

	// These are automatically filled in.
	WorldTree		*m_pTree;			// 0x00

	// Fill these in when making calls.
	uint32			m_iObjArray;		// 0x04 Which array to index.
	LTVector		m_Min;				// 0x08
	LTVector		m_Max;				// 0x14
	LTVector		m_Unknown20;		// 0x20 (never read by world_tree)
	WTObjCallback	m_CB;				// 0x2c
	void			*m_pCBUser;			// 0x30
};

// Visibility query (WorldTree slot 0x14, 0x48 bytes). Names are ours.
class VisQueryRequest;
typedef LTBOOL (*VQNodeFilterFn)(WorldTreeNode *pNode);
typedef void (*VQAddObjectFn)(WorldTreeObj *pObj, void *pUser);

class VisQueryRequest
{
public:
	uint32			m_iObjArray;		// 0x00
	LTVector		m_Viewpoint;		// 0x04
	float			m_ViewRadius;		// 0x10
	VQAddObjectFn	m_AddObject;		// 0x14 called for visible objects
	void			*m_Unknown18;		// 0x18
	void			*m_pUserData;		// 0x1c passed to m_AddObject
	void			*m_Unknown20;		// 0x20
	void			*m_Unknown24;		// 0x24
	VQNodeFilterFn	m_NodeFilterFn;		// 0x28 return FALSE to skip a node (default: accept all)

	// Filled in by WorldTree::DoVisQuery.
	LTVector		m_BoxMin;			// 0x2c m_Viewpoint - m_ViewRadius
	LTVector		m_BoxMax;			// 0x38 m_Viewpoint + m_ViewRadius
	WorldTree		*m_pTree;			// 0x44
};

// Talon's WorldTree has a vtable and its queries are virtual (0x7c bytes, vtable 0x004c8b74,
// embedded in MainWorld at 0x6c, so CServerMgr+0x210).
class WorldTree
{
public:
	WorldTree();						// 0x0049f770

	// Find the node with the given path (2 bits per level, 4 bytes of path, then the depth).
	virtual WorldTreeNode*	FindNode(void *pNodePath);	// 0x00 (name ours)
	virtual void	FindObjectsInBox(const LTVector *pMin, const LTVector *pMax,
		WTObjCallback cb, void *pCBUser, uint32 iObjArray);		// 0x04
	virtual void	FindObjectsInBox2(FindObjInfo *pInfo);		// 0x08
	virtual void	FindObjectsOnPoint(const LTVector *pPoint,
		WTObjCallback cb, void *pCBUser, uint32 iObjArray);		// 0x0c
	virtual void	IntersectSegment(LTVector *pPt1, LTVector *pPt2,
		ISCallback cb, void *pCBUser, uint32 iObjArray);		// 0x10 (fullintersectline)
	virtual void	DoVisQuery(VisQueryRequest *pInfo);			// 0x14 (name ours)
	virtual void	WTreeSlot6(void *p1, void *p2);				// 0x18 empty (name unknown)

	// Sets up an empty tree (0x0049f7c0).
	void			InitWorldTree(class WorldTreeHelper *pHelper);
	void			Term();								// 0x0049f7e0

	uint32			GetFrameCode();						// 0x0049f7b0 m_pHelper->GetFrameCode()

	// Insert an object into the tree (0x0049f870).
	void			InsertObject(WorldTreeObj *pObj, uint32 iObjArray);
	void			InsertObject2(WorldTreeObj *pObj, const LTVector &vMin, const LTVector &vMax,
		uint32 iObjArray);								// 0x0049f8b0

	// Objects that are always visible (FLAG_REALLYCLOSE) (0x004a04d0).
	void			InsertAlwaysVisObject(WorldTreeObj *pObj);

	// Copy the node layout from the other tree.
	LTBOOL			Inherit(WorldTree *pOther);			// 0x004a0300

	// Load the node layout.
	LTBOOL			LoadLayout(ILTStream *pStream);		// 0x004a0370

	WorldTreeNode*	GetRootNode()	{ return &m_RootNode; }

private:
	LTBOOL			CopyNodeLayout_R(WorldTreeNode *pDest, WorldTreeNode *pSrc);	// 0x004a0470

public:
	WorldTreeHelper	*m_pHelper;			// 0x04

	// Gotten from m_pHelper and used during queries.
	uint32			m_nTempFrameCode;	// 0x08

	// Root of tree (depth value 0).
	WorldTreeNode	m_RootNode;			// 0x0c

	uint32			m_Unknown68;		// 0x68 (Term clears it, Inherit copies it)
	uint32			m_Unknown6c;		// 0x6c (Term clears it, Inherit copies it)
	uint32			m_TerrainDepth;		// 0x70 (read by LoadLayout)

	// The list of always-visible objects.
	CheapLTLink		m_AlwaysVisObjects;	// 0x74
};

#endif  // __WORLD_TREE_H__
