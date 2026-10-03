// The BSP half of a visibility query (Talon, lithtech.exe 0x0049e330-0x0049ec40). Names are ours.
#ifndef __VISQUERY_H__
#define __VISQUERY_H__

#include "ltbasedefs.h"
#include "world_tree.h"

struct Leaf;
class WorldBsp;

// The state of one visibility query through a BSP; world_tree's VisQueryRequest is copied into it.
struct VisQueryInfo
{
	Leaf			*m_pLeaf;		// 0x00 the leaf the viewpoint is in (LTNULL if none)
	WorldBsp		*m_pBsp;		// 0x04
	VQAddObjectFn	m_AddObject;	// 0x08 VisQueryRequest::m_AddObject
	void			*m_Unknown0C;	// 0x0c VisQueryRequest::m_Unknown20
	void			*m_Unknown10;	// 0x10 VisQueryRequest::m_Unknown18 (default: vq_DefaultGetObjects)
	void			*m_Unknown14;	// 0x14 VisQueryRequest::m_Unknown24
	void			*m_pUserData;	// 0x18 VisQueryRequest::m_pUserData
	uint32			m_FrameCode;	// 0x1c the world tree's frame code
};

// GLOBAL: LITHTECH 0x004e6270
extern VisQueryInfo *g_pCurVisQuery;

#endif  // __VISQUERY_H__
