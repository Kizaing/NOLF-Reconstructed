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

typedef uint32 WTObjType;

class WTObjLink;

// Talon world tree node; WorldTree's root node is at WorldTree+0xc.
class WorldTreeNode
{
public:
	// Adds an object to the specified list (0x0049f730).
	void			AddObjectToList(WTObjLink *pLink, uint32 iArray);
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

// Which object array of the tree nodes to search.
#define NOA_Objects		0

// Talon's WorldTree has a vtable and its queries are virtual (embedded in CServerMgr at 0x210).
class WorldTree
{
public:
	virtual WorldTreeNode*	FindNode(void *pNodePath);	// 0x00 (name unknown)
	virtual void	FindObjectsInBox(const LTVector *pMin, const LTVector *pMax,
		WTObjCallback cb, void *pCBUser, uint32 iObjArray);		// 0x04
	virtual void	WTreeSlot2();		// 0x08 (name unknown)
	virtual void	FindObjectsOnPoint(const LTVector *pPoint,
		WTObjCallback cb, void *pCBUser, uint32 iObjArray);		// 0x0c

	// Sets up an empty tree (0x0049f7c0).
	void			InitWorldTree(class WorldTreeHelper *pHelper);

	// Insert an object into the tree (0x0049f870).
	void			InsertObject(WorldTreeObj *pObj, uint32 iObjArray);

	// Objects that are always visible (FLAG_REALLYCLOSE) (0x004a04d0).
	void			InsertAlwaysVisObject(WorldTreeObj *pObj);
};

#endif  // __WORLD_TREE_H__
