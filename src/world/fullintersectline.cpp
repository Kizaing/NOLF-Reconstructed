// Jupiter runtime/world/src/fullintersectline.cpp (Talon version: no model OBBs, no filter-actual
// callback, no "from point inside object" test).
// FLAGS: /O2 /GX-

// For some reason we can't use certain optimizations in this module or
// else the raycasting is all screwed up.  This module also can't use
// precompiled headers or else these pragmas don't take effect.
#pragma optimize("", off)
#pragma optimize("as", on)

#include "bdefs.h"
#include "de_world.h"
#include "de_objects.h"
#include "world_tree.h"
#include "fullintersectline.h"


// intersect_line.cpp
class IntersectRequest
{
public:
	// FUNCTION: LITHTECH 0x00437f7a ??0IntersectRequest@@QAE@XZ
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
    LTVector        *m_pPoints[2];
    LTVector        *m_pIPos;   // Intersection position.
    IntersectQuery  *m_pQuery;
    WorldBsp		*m_pWorldBsp;

// Output (if it returns LTTRUE).
public:
    Node			*m_pNodeHit;
};

Node* IntersectLine(Node *pRoot, LTVector *pPoint1, LTVector *pPoint2,
    LTVector *pIPos, LTPlane *pIPlane);
LTBOOL IntersectLineNode(Node *pRoot, IntersectRequest *pRequest);


uint32 g_IntersectTicks, g_nIntersectCalls;	// Jupiter: g_Ticks_Intersect (this name gives the exe's .bss order)
float g_IntersectLineLen = 0.0f;


static void (*g_FindIntersectionsFn)(WorldBsp *pWorldBsp, Node **pNodeIntersectionPtr,
    LTVector *pIntersectionPosPtr, float *pDistSqrPtr, HPOLY *hWorldPoly,
    LTVector *pPoint1, LTVector *pPoint2, uint8 bWorldModel);


// The current query.
static IntersectQuery *g_pCurQuery;
static uint8 g_bProcessNonSolid;
static uint8 g_bProcessObjects;


// The current best intersection (LTNULL if none).
static Node *g_pWorldIntersection; // The node we intersected if we hit a BSP.
static LTObject *g_pIntersection;
static float g_IntersectionBestDistSqr = 0.0f; // Distance to intersection point squared.
static LTPlane g_IntersectionPlane;
static LTVector g_IntersectionPos;
static HPOLY g_hWorldPoly = 0;  // The WorldModel poly we're touching.

static LTVector g_V, g_VTimesInvVV;
static float g_VPTimesInvVV, g_LineLen;

// Static initializers for the LTPlane/LTVector globals above (empty constructors).
// FUNCTION: LITHTECH 0x00437c10 _$E2
// FUNCTION: LITHTECH 0x00437c1a _$E1
// FUNCTION: LITHTECH 0x00437c1f _$E5
// FUNCTION: LITHTECH 0x00437c29 _$E4
// FUNCTION: LITHTECH 0x00437c2e _$E8
// FUNCTION: LITHTECH 0x00437c38 _$E7

// Out-of-line copies of the SDK inlines (this module is built without optimization).
// FUNCTION: LITHTECH 0x00438ccf ?MatVMul_H@@YAMPAV?$_CVector@M@@PAVLTMatrix@@0@Z
// FUNCTION: LITHTECH 0x00438dae ?MatVMul_InPlace_H@@YAMPAVLTMatrix@@PAV?$_CVector@M@@@Z
// FUNCTION: LITHTECH 0x00438e94 ?MatVMul_3x3@@YAXPAV?$_CVector@M@@PAVLTMatrix@@0@Z
// FUNCTION: LITHTECH 0x00438f1e ?DistSqr@?$_CVector@M@@QBEMABV1@@Z
// FUNCTION: LITHTECH 0x00438f72 ?MagSqr@?$_CVector@M@@QBEMXZ




// Get the intersection point and see if it's inside the other dimensions.
#define DO_PLANE_TEST_X(planeCoord, coord0, coord1, coord2, normalDirection) \
    t = (planeCoord - Point1.coord0) / (Point2.coord0 - Point1.coord0);\
    testCoords[0] = Point1.coord1 + ((Point2.coord1 - Point1.coord1) * t);\
    if (testCoords[0] > pServerObj->m_MinBox.coord1 && testCoords[0] < pServerObj->m_MaxBox.coord1)\
    {\
        testCoords[1] = Point1.coord2 + ((Point2.coord2 - Point1.coord2) * t);\
        if (testCoords[1] > pServerObj->m_MinBox.coord2 && testCoords[1] < pServerObj->m_MaxBox.coord2)\
        {\
            pIntersectPt->coord0 = planeCoord;\
            pIntersectPt->coord1 = testCoords[0];\
            pIntersectPt->coord2 = testCoords[1];\
            pIntersectPlane->m_Normal.x = normalDirection;\
            pIntersectPlane->m_Normal.y = 0.0f;\
            pIntersectPlane->m_Normal.z = 0.0f;\
            pIntersectPlane->m_Dist = pServerObj->m_MinBox.x * normalDirection;\
            return true;\
        }\
    }

#define DO_PLANE_TEST_Y(planeCoord, coord0, coord1, coord2, normalDirection) \
    t = (planeCoord - Point1.coord0) / (Point2.coord0 - Point1.coord0);\
    testCoords[0] = Point1.coord1 + ((Point2.coord1 - Point1.coord1) * t);\
    if (testCoords[0] > pServerObj->m_MinBox.coord1 && testCoords[0] < pServerObj->m_MaxBox.coord1)\
    {\
        testCoords[1] = Point1.coord2 + ((Point2.coord2 - Point1.coord2) * t);\
        if (testCoords[1] > pServerObj->m_MinBox.coord2 && testCoords[1] < pServerObj->m_MaxBox.coord2)\
        {\
            pIntersectPt->coord0 = planeCoord;\
            pIntersectPt->coord1 = testCoords[0];\
            pIntersectPt->coord2 = testCoords[1];\
            pIntersectPlane->m_Normal.x = 0.0f;\
            pIntersectPlane->m_Normal.y = normalDirection;\
            pIntersectPlane->m_Normal.z = 0.0f;\
            pIntersectPlane->m_Dist = pServerObj->m_MinBox.y * normalDirection;\
            return true;\
        }\
    }

#define DO_PLANE_TEST_Z(planeCoord, coord0, coord1, coord2, normalDirection) \
    t = (planeCoord - Point1.coord0) / (Point2.coord0 - Point1.coord0);\
    testCoords[0] = Point1.coord1 + ((Point2.coord1 - Point1.coord1) * t);\
    if (testCoords[0] > pServerObj->m_MinBox.coord1 && testCoords[0] < pServerObj->m_MaxBox.coord1)\
    {\
        testCoords[1] = Point1.coord2 + ((Point2.coord2 - Point1.coord2) * t);\
        if (testCoords[1] > pServerObj->m_MinBox.coord2 && testCoords[1] < pServerObj->m_MaxBox.coord2)\
        {\
            pIntersectPt->coord0 = planeCoord;\
            pIntersectPt->coord1 = testCoords[0];\
            pIntersectPt->coord2 = testCoords[1];\
            pIntersectPlane->m_Normal.x = 0.0f;\
            pIntersectPlane->m_Normal.y = 0.0f;\
            pIntersectPlane->m_Normal.z = normalDirection;\
            pIntersectPlane->m_Dist = pServerObj->m_MinBox.z * normalDirection;\
            return true;\
        }\
    }



// Talon's object type is a signed char.
inline LTBOOL HasWorldModel(LTObject *pObj)
{
	return (char)pObj->m_ObjectType == OT_WORLDMODEL || (char)pObj->m_ObjectType == OT_CONTAINER;
}


// Just sets up the current 'closest object'.
#define USE_THIS_OBJECT(pServerObj, distSqr, plane, intersectionPt, hPoly) \
    g_IntersectionBestDistSqr = distSqr;\
    g_pIntersection = pServerObj;\
    g_IntersectionPlane = plane;\
    g_IntersectionPos = intersectionPt;\
    g_hWorldPoly = hPoly;


// FUNCTION: LITHTECH 0x004381ab
inline bool i_BoundingBoxTest(const LTVector& Point1, const LTVector& Point2, const LTObject *pServerObj,
    LTVector *pIntersectPt, LTPlane *pIntersectPlane)
{
    float t;
    float testCoords[2];

    // Left/Right.
    if (Point1.x < pServerObj->m_MinBox.x)
	{
        if (Point2.x < pServerObj->m_MinBox.x)
		{
            return false;
        }

        DO_PLANE_TEST_X(pServerObj->m_MinBox.x, x, y, z, -1.0f);
    }
    else if (Point1.x > pServerObj->m_MaxBox.x)
	{
        if (Point2.x > pServerObj->m_MaxBox.x)
		{
            return false;
        }

        DO_PLANE_TEST_X(pServerObj->m_MaxBox.x, x, y, z, 1.0f);
    }

    // Top/Bottom.
    if (Point1.y < pServerObj->m_MinBox.y)
	{
        if (Point2.y < pServerObj->m_MinBox.y)
		{
            return false;
        }

        DO_PLANE_TEST_Y(pServerObj->m_MinBox.y, y, x, z, -1.0f);
    }
    else if (Point1.y > pServerObj->m_MaxBox.y)
	{
        if (Point2.y > pServerObj->m_MaxBox.y)
		{
            return false;
        }

        DO_PLANE_TEST_Y(pServerObj->m_MaxBox.y, y, x, z, 1.0f);
    }

    // Front/Back.
    if (Point1.z < pServerObj->m_MinBox.z)
	{
        if (Point2.z < pServerObj->m_MinBox.z)
		{
            return false;
        }

        DO_PLANE_TEST_Z(pServerObj->m_MinBox.z, z, x, y, -1.0f);
    }
    else if (Point1.z > pServerObj->m_MaxBox.z)
	{
        if (Point2.z > pServerObj->m_MaxBox.z)
		{
            return false;
        }

        DO_PLANE_TEST_Z(pServerObj->m_MaxBox.z, z, x, y, 1.0f);
    }

    return false;
}


inline float i_GetRadius(const LTObject *pObj) {return pObj->m_Radius;}
inline float i_GetRadiusSquared(const LTObject *pObj) {return pObj->m_Radius * pObj->m_Radius;}

// FUNCTION: LITHTECH 0x004388cb
inline bool i_QuickSphereTest(const LTVector& Point1, const LTVector& Point2, const LTObject *pServerObj)
{
    // Find the closest point to the line.
    // Here's the equation for t:
    // P = point1
    // V = point2 - point1 (ie: direction vector)
    // S = point you're testing
    // t = parametric (P + Vt)
    // t = -(-VS + VP) / VV

    //dirVec = *pPoint2 - *pPoint1;
    //t = pServerObj->m_Pos.Dot(dirVec) - dirVec.Dot(*pPoint1);
    //t /= dirVec.Dot(dirVec);

    float t = g_VTimesInvVV.Dot(pServerObj->GetPos()) - g_VPTimesInvVV;

    if (t < -i_GetRadius(pServerObj) || t > (g_LineLen + i_GetRadius(pServerObj)))
	{
        return false;
    }

    // Now see if it's within range.
    LTVector vecTo;
    vecTo = g_pCurQuery->m_From + g_V * t - pServerObj->GetPos();
    return vecTo.MagSqr() < i_GetRadiusSquared(pServerObj) ? true : false;
}


// Returns true if the segment hits the world model.
// FUNCTION: LITHTECH 0x00438a53
static bool i_TestWorldModel(WorldModelInstance *pObj)
{
    Node *pNodeIntersection;
    LTVector intersectionPt;
    LTFLOAT distToIntersectionSqr;
    HPOLY hWorldPoly;
    LTVector points[2], planePt;
    LTPlane tempPlane;


    // Pre-rotate the endpoints for the worldmodel.
    MatVMul_H(&points[0], &pObj->m_BackTransform, (LTVector*)&g_pCurQuery->m_From);
    MatVMul_H(&points[1], &pObj->m_BackTransform, (LTVector*)&g_pCurQuery->m_To);

    g_FindIntersectionsFn(pObj->m_pOriginalBsp,
        &pNodeIntersection, &intersectionPt, &distToIntersectionSqr, &hWorldPoly,
        &points[0], &points[1], true);

    if (!pNodeIntersection)
	{
        return false;
    }

    MatVMul_InPlace_H(&pObj->m_Transform, &intersectionPt);
    distToIntersectionSqr = g_pCurQuery->m_From.DistSqr(intersectionPt);

    if (distToIntersectionSqr < g_IntersectionBestDistSqr)
	{
        planePt = pNodeIntersection->GetPlane()->m_Normal * pNodeIntersection->GetPlane()->m_Dist;
        MatVMul_3x3(&tempPlane.m_Normal, &pObj->m_Transform, &pNodeIntersection->GetPlane()->m_Normal);
        tempPlane.m_Dist = tempPlane.m_Normal.Dot(planePt);

        g_pWorldIntersection = pNodeIntersection;
        USE_THIS_OBJECT(pObj, distToIntersectionSqr, tempPlane, intersectionPt, hWorldPoly);
        return true;
    }

    return false;
}


// Tries everything it can think of to reject this object intersection.
// If it does intersect and is closer than the current best world intersection
// then it replaces the current one.
// FUNCTION: LITHTECH 0x00438063
inline bool i_HandlePossibleIntersection(const LTVector& Point1, const LTVector& Point2, LTObject *pServerObj)
{
    // Quick sphere test.
    if (i_QuickSphereTest(Point1, Point2, pServerObj))
	{
        // Ok, filter if necessary.
        if (g_pCurQuery->m_FilterFn &&
            !g_pCurQuery->m_FilterFn((HOBJECT)pServerObj, g_pCurQuery->m_pUserData))
        {
            // They said to ignore it..
        }
        else
		{
            // If it's a WorldModel, add it to the list to be tested later.
            // You can't treat it like a solid box here because a ray could go
            // right through all its geometry.
            if (HasWorldModel(pServerObj))
			{
				return i_TestWorldModel((WorldModelInstance*)pServerObj);
            }
            else
			{
                // Bounding box test...
				LTVector testPt;
				float distToIntersectionSqr;
				LTPlane testPlane;
                if (i_BoundingBoxTest(Point1, Point2, pServerObj, &testPt, &testPlane))
				{
                    // Is this intersection closer than the current best?
                    distToIntersectionSqr = testPt.DistSqr(g_pCurQuery->m_From);

                    if (g_pIntersection)
					{
                        if (distToIntersectionSqr < g_IntersectionBestDistSqr)
						{
                            USE_THIS_OBJECT(pServerObj, distToIntersectionSqr, testPlane, testPt, INVALID_HPOLY);
                            return true;
                        }
                    }
                    else
					{
                        USE_THIS_OBJECT(pServerObj, distToIntersectionSqr, testPlane, testPt, INVALID_HPOLY);
                        return true;
                    }
                }
            }
        }
    }

    return false;
}


// Finds intersections a slower way, but fills in hWorldPoly.
// FUNCTION: LITHTECH 0x00437ef3
static void i_FindIntersectionsHPoly(WorldBsp *pWorldBsp, Node **pNodeIntersectionPtr,
    LTVector *pIntersectionPosPtr, float *pDistSqrPtr, HPOLY *hWorldPoly,
    LTVector *pPoint1, LTVector *pPoint2, uint8 bWorldModel)
{
    IntersectRequest req;

    req.m_pPoints[0] = pPoint1;
    req.m_pPoints[1] = pPoint2;
    req.m_pIPos      = pIntersectionPosPtr;
    req.m_pQuery     = g_pCurQuery;
    req.m_pWorldBsp  = pWorldBsp;

    if (IntersectLineNode(pWorldBsp->GetRootNode(), &req))
	{
        *hWorldPoly = pWorldBsp->MakeHPoly(req.m_pNodeHit);
        *pNodeIntersectionPtr = req.m_pNodeHit;
        *pDistSqrPtr = g_pCurQuery->m_From.DistSqr(*req.m_pIPos);
    }
    else
	{
        *hWorldPoly = INVALID_HPOLY;
        *pNodeIntersectionPtr = LTNULL;
    }
}


// FUNCTION: LITHTECH 0x00437faf
static void i_FindIntersections(WorldBsp *pWorldBsp, Node **pNodeIntersectionPtr,
    LTVector *pIntersectionPosPtr, float *pDistSqrPtr, HPOLY *hWorldPoly,
    LTVector *pPoint1, LTVector *pPoint2, uint8 bWorldModel)
{
    LTPlane iPlane;


    *hWorldPoly = INVALID_HPOLY;
    *pNodeIntersectionPtr = IntersectLine(pWorldBsp->GetRootNode(), pPoint1, pPoint2, pIntersectionPosPtr, &iPlane);
    if (*pNodeIntersectionPtr)
	{
        *pDistSqrPtr = pPoint1->DistSqr(*pIntersectionPosPtr);
    }
}


// Called by the WorldTree in IntersectSegment.
// FUNCTION: LITHTECH 0x00437ffb
static LTBOOL i_ISCallback(WorldTreeObj *pObj, void *pCBUser)
{
    LTObject *pObject = (LTObject*)pObj;

    if (pObject->m_Flags & (FLAG_RAYHIT|FLAG_SOLID) || g_bProcessNonSolid)
	{
        // Honor the INTERSECT_OBJECTS flag (or lack thereof...)
        if (!g_bProcessObjects && !pObject->IsMainWorldModel())
		{
            return LTFALSE;
        }

        // Hit the object if it has the ray hit flag, is solid, or if non-solid and it
        // query doesn't have ignore non-solid...
        // This is basically everything except for objects with only touch-notify...
        // Do further tests..
        return i_HandlePossibleIntersection(g_pCurQuery->m_From, g_pCurQuery->m_To, pObject);
    }

    return LTFALSE;
}


// FUNCTION: LITHTECH 0x00437c3d
LTBOOL i_IntersectSegment(IntersectQuery *pQuery, IntersectInfo *pInfo, WorldTree *pWorldTree, LTBOOL bServer)
{
    float InvVV, VP, testMag;

    ++g_nIntersectCalls;

    // Init..
    g_pCurQuery = pQuery;
    g_pIntersection = LTNULL;
    g_pWorldIntersection = LTNULL;
    g_hWorldPoly = INVALID_HPOLY;
    g_bProcessNonSolid = !(pQuery->m_Flags & IGNORE_NONSOLID);
    g_bProcessObjects = !!(pQuery->m_Flags & INTERSECT_OBJECTS);
    g_IntersectionBestDistSqr = (pQuery->m_From - pQuery->m_To).MagSqr() + 1.0f;

    // Precalculate stuff to totally accelerate i_QuickSphereTest.
    g_V = pQuery->m_To - pQuery->m_From;
    g_LineLen = g_V.Mag();
    g_V /= g_LineLen;
    g_IntersectLineLen += g_LineLen;

    // Was it too short?
    testMag = g_V.MagSqr();
    if (testMag < 0.5f || testMag > 2.0f)
	{
        return LTFALSE;
    }

    VP = g_V.Dot(pQuery->m_From);
    InvVV = 1.0f / g_V.MagSqr();
    g_VTimesInvVV = g_V * InvVV;
    g_VPTimesInvVV = VP * InvVV;

    if (pQuery->m_Flags & INTERSECT_HPOLY)
	{
        g_FindIntersectionsFn = i_FindIntersectionsHPoly;
    }
    else
	{
        g_FindIntersectionsFn = i_FindIntersections;
    }

    // Start at the world tree.
    pWorldTree->IntersectSegment(&pQuery->m_From, &pQuery->m_To, i_ISCallback, LTNULL, NOA_Objects);

    // If an object was hit, use it!
    if (g_pIntersection)
	{
        pInfo->m_Point = g_IntersectionPos;
        pInfo->m_Plane = g_IntersectionPlane;
        pInfo->m_hObject = (HOBJECT)g_pIntersection;
        pInfo->m_hPoly = g_hWorldPoly;

        if (g_pWorldIntersection)
		{
            pInfo->m_SurfaceFlags = ((Surface*)g_pWorldIntersection->m_pPoly->m_pSurface)->m_TextureFlags;
        }
        else
		{
            pInfo->m_SurfaceFlags = 0;
        }

        return LTTRUE;
    }
    else
	{
        return LTFALSE;
    }
}
