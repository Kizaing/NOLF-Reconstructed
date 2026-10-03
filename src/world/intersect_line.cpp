// Jupiter runtime/world/src/intersect_line.cpp: line segment intersects in the BSP.
// Talon's version recurses on the starting side with explicit endpoints, tests SURF_SOLID before the polygon
// and has no epsilon on the clipped segments.

#include "bdefs.h"
#include "de_world.h"
#include "de_objects.h"
#include "intersect_line.h"

#define INTERSECT_EPSILON	0.01f

#define FrontSide	1
#define BackSide	0


// Tells if pPt is inside the convex poly.
inline LTBOOL InsideConvex(WorldPoly *pPoly, LTVector *pPt)
{
	LTPlane edgePlane;
	float edgeDot;
	LTVector *pNormal;
	SPolyVertex *pCur, *pPrev, *pEnd;

	// Reject it if it's outside the radius of the poly
	if ((pPoly->m_Center - *pPt).MagSqr() > (pPoly->m_Radius * pPoly->m_Radius))
		return LTFALSE;

	pNormal = &pPoly->GetPlane()->m_Normal;
	pCur  = (SPolyVertex*)(pPoly + 1);
	pEnd  = pCur + pPoly->m_nVertices;
	pPrev = pEnd - 1;

	for(; pCur != pEnd; pPrev = pCur, ++pCur)
	{
		LTVector &currVec = *pCur->m_Vec;
		LTVector vTemp = *pPrev->m_Vec - currVec;
		edgePlane.m_Normal = pNormal->Cross(vTemp);
		edgePlane.m_Normal.Norm();
		edgePlane.m_Dist = edgePlane.m_Normal.Dot(currVec);

		edgeDot = edgePlane.DistTo(*pPt);
		if(edgeDot < -INTERSECT_EPSILON)
			return LTFALSE;
	}

	return LTTRUE;
}

// Close: the original keeps Cross's 3-float constructor out of line and inlines the rest (more inline cost
// before it); the frame is 0x50 bytes there (dot1 lives in the dead pPoint1 slot), 0x60 here.
// Wave 5: inline_scan finds that 8 units of ballast before any statement up to the VEC_LERP changes the diff
// (982 -> 905 bytes) but never matches; `*pNormal ^ vTemp` (an extra nesting level) makes it worse (1248 bytes).
// In the original the Cross result is built straight into edgePlane (ctor `this` = the plane), not in a temp.
// STUB: LITHTECH 0x004442a0
static LTBOOL InternalIntersectLineNode(
	Node *pRoot,
	IntersectRequest *pRequest,
	LTVector *pPoint1,
	LTVector *pPoint2)
{
	LTVector point1;
	LTVector iPoint;
	float intersection_t;
	float dot1, dot2;
	int side1;

	point1 = *pPoint1;

	while((pRoot->m_Flags & (NF_IN|NF_OUT)) == 0)
	{
		// Go into the correct side.
		dot1 = pRoot->GetPlane()->DistTo(point1);
		dot2 = pRoot->GetPlane()->DistTo(*pPoint2);

		// Handle the segment being entirely on one side of the plane
		if(dot1 > INTERSECT_EPSILON && dot2 > INTERSECT_EPSILON)
		{
			pRoot = pRoot->m_Sides[FrontSide];
		}
		else if(dot1 < -INTERSECT_EPSILON && dot2 < -INTERSECT_EPSILON)
		{
			pRoot = pRoot->m_Sides[BackSide];
		}
		else
		{
			// Ok, it crosses this plane.. go into the side that pFrom is on first.
			if((dot1 < -INTERSECT_EPSILON) || (dot1 > INTERSECT_EPSILON))
				side1 = (int)(dot1 > 0.0f);
			else
				side1 = (int)(dot2 < 0.0f);

			// Get the difference between the distances
			intersection_t = dot2 - dot1;
			if(intersection_t != 0.0f)
			{
				if((dot1 < -INTERSECT_EPSILON) || (dot1 > INTERSECT_EPSILON))
				{
					// Find the point of intersection
					intersection_t = -dot1 / intersection_t;
					VEC_LERP(iPoint, point1, *pPoint2, intersection_t);

					// Test the side the starting point is on.
					if(InternalIntersectLineNode(pRoot->m_Sides[side1], pRequest, &point1, &iPoint))
						return LTTRUE;

					// Check for a polygon intersection
					if((side1 == FrontSide) && pRoot->m_pPoly)
					{
						if(((Surface*)pRoot->m_pPoly->m_pSurface)->m_Flags & SURF_SOLID)
						{
							if(InsideConvex(pRoot->m_pPoly, &iPoint))
							{
								IntersectQuery *pQuery = pRequest->m_pQuery;
								if(!(pQuery && pQuery->m_PolyFilterFn && pRequest->m_pWorldBsp) ||
									pQuery->m_PolyFilterFn(pRequest->m_pWorldBsp->MakeHPoly(pRoot), pQuery->m_pUserData))
								{
									// Congratulations, we have a winner!
									pRequest->m_pNodeHit = pRoot;
									*pRequest->m_pIPos = iPoint;
									return LTTRUE;
								}
							}
						}
					}
					else if(intersection_t > (1.0f - INTERSECT_EPSILON))
					{
						// Jump out if the ray doesn't go to the "other" side
						return LTFALSE;
					}

					// Clip the segment to the plane
					point1 = iPoint;
				}
			}

			// Go into the other side.
			pRoot = pRoot->m_Sides[!side1];
		}
	}

	return LTFALSE;
}

// FUNCTION: LITHTECH 0x00444730
LTBOOL IntersectLineNode(
	Node *pRoot,
	IntersectRequest *pRequest)
{
	return InternalIntersectLineNode(
		pRoot,
		pRequest,
		pRequest->m_pPoints[0],
		pRequest->m_pPoints[1]
		);
}

// FUNCTION: LITHTECH 0x00444750
Node* IntersectLine(Node *pRoot, LTVector *pPoint1, LTVector *pPoint2, LTVector *pIPos, LTPlane *pIPlane)
{
	IntersectRequest req;

	req.m_pPoints[0] = pPoint1;
	req.m_pPoints[1] = pPoint2;
	req.m_pIPos = pIPos;

	if(IntersectLineNode(pRoot, &req))
	{
		*pIPlane = *req.m_pNodeHit->GetPlane();
		return req.m_pNodeHit;
	}

	return LTNULL;
}
