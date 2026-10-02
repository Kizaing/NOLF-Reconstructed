// Jupiter runtime/world/src/intersect_line.h: line segment intersects in the BSP.
#ifndef __INTERSECT_LINE_H__
#define __INTERSECT_LINE_H__

#include "ltbasedefs.h"

struct Node;
class WorldBsp;
class IntersectQuery;

class IntersectRequest
{
public:
	IntersectRequest()
	{
		m_pPoints[0] = NULL;
		m_pPoints[1] = NULL;
		m_pIPos      = NULL;

		m_pNodeHit   = NULL;
		m_pQuery     = NULL;
		m_pWorldBsp  = NULL;
	}

// Input to the routine.
public:
	LTVector		*m_pPoints[2];
	LTVector		*m_pIPos;		// Intersection position.
	IntersectQuery	*m_pQuery;
	WorldBsp		*m_pWorldBsp;

// Output (if it returns LTTRUE).
public:
	Node			*m_pNodeHit;
};

// Returns the node hit (LTNULL if none) and the plane of its polygon.
Node* IntersectLine(Node *pRoot, LTVector *pPoint1, LTVector *pPoint2, LTVector *pIPos, LTPlane *pIPlane);	// 0x00444750

// Fills in pRequest->m_pNodeHit and m_pIPos when it returns LTTRUE.
LTBOOL IntersectLineNode(Node *pRoot, IntersectRequest *pRequest);	// 0x00444730

#endif  // __INTERSECT_LINE_H__
