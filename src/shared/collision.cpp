// Jupiter runtime/shared/src/collision.cpp
// Talon's collision keeps the box being moved and its bounding spheres in globals (SetupBox fills
// them), keeps the CMovingCylinder of Jupiter (stair stepping and cylinder physics) and reuses the
// box planes for FindPoliesTouchingBox (g_BoxFindPlanes).
#include <math.h>
#include <float.h>
#include "bdefs.h"
#include "de_world.h"
#include "de_objects.h"
#include "collision.h"
#include "moveobject.h"
#include "counter.h"
#include "impl_common.h"

// fullintersectline.cpp (0x00444750)
Node* IntersectLine(Node *pRoot, LTVector *pPoint1, LTVector *pPoint2, LTVector *pIPos, LTPlane *pIPlane);
// 0x00416f10 (clippoly.cpp; the parameters are the clipped box's min and max)
LTBOOL PolyTouchesBox(WorldPoly *pPoly, void *pUnknown1, void *pUnknown2);
// impl_common.cpp (0x0043d820): 1 = inside, 0 = outside, else intersecting.
int ci_IsSphereInsideBSP(Node **ppNode, LTVector *pCenter, float radius);

#define NUM_BOX_POINTS	8

// Talon spheres keep the radius first.
struct PhysicsSphere
{
	float		m_Radius;
	LTVector	m_Center;
};



// ------------------------------------------------------------------ //
// Globals.
// ------------------------------------------------------------------ //

// Static initializers of the globals with (empty) constructors, in order.
// FUNCTION: LITHTECH 0x004182b0 _$E2
// FUNCTION: LITHTECH 0x004182c0 _$E1
// FUNCTION: LITHTECH 0x004182d0 _$E5
// FUNCTION: LITHTECH 0x004182e0 _$E4
// FUNCTION: LITHTECH 0x004182f0 _$E8
// FUNCTION: LITHTECH 0x00418300 _$E7
// FUNCTION: LITHTECH 0x00418310 _$E11
// FUNCTION: LITHTECH 0x00418320 _$E10
// FUNCTION: LITHTECH 0x00418330 _$E14
// FUNCTION: LITHTECH 0x00418340 _$E13
// FUNCTION: LITHTECH 0x00418350 _$E17
// FUNCTION: LITHTECH 0x00418360 _$E16
// FUNCTION: LITHTECH 0x00418370 _$E20
// FUNCTION: LITHTECH 0x00418380 _$E19

// The box being moved (SetupBox).
// GLOBAL: LITHTECH 0x004e246c
LTVector g_BoxOffset;
// GLOBAL: LITHTECH 0x004e2478
LTVector g_BoxMin;
// GLOBAL: LITHTECH 0x004e249c
LTVector g_BoxMax;
// GLOBAL: LITHTECH 0x004e2484
LTVector g_P0;
// GLOBAL: LITHTECH 0x004e2490
LTVector g_P1;
// The box's bounding spheres at the start and end of the movement.
// GLOBAL: LITHTECH 0x004e0ca0
extern PhysicsSphere g_StartSphere;
// GLOBAL: LITHTECH 0x004e24b0
extern PhysicsSphere g_EndSphere;
// The box's corners at the start and end of the movement.
// GLOBAL: LITHTECH 0x004e2cc0
extern LTVector g_MovePts[2][NUM_BOX_POINTS];

// The sphere enclosing the whole movement, which FindPoliesTouchingBox (serverde_impl) reuses.
// GLOBAL: LITHTECH 0x004e245c
float g_BoxFindRadius;
// GLOBAL: LITHTECH 0x004e2460
LTVector g_BoxFindCenter;

// The planes of the box (their distances are set by SetupBox and si_FindPoliesTouchingBox).
// GLOBAL: LITHTECH 0x004e2f10
LTPlane g_BoxFindPlanes[6] =
{
	LTPlane(1.0f, 0.0f, 0.0f, 0.0f),
	LTPlane(-1.0f, 0.0f, 0.0f, 0.0f),
	LTPlane(0.0f, 1.0f, 0.0f, 0.0f),
	LTPlane(0.0f, -1.0f, 0.0f, 0.0f),
	LTPlane(0.0f, 0.0f, 1.0f, 0.0f),
	LTPlane(0.0f, 0.0f, -1.0f, 0.0f)
};

// The current collision request (CollideWithWorld) and its output.
// GLOBAL: LITHTECH 0x004e2f7c
CollideRequest *g_pCurRequest;
// GLOBAL: LITHTECH 0x004e2f78
CollideInfo *g_pCurInfo;
// The radius of the moving box (from its dims).
// GLOBAL: LITHTECH 0x004e0494
extern float g_BoxRadius;

// Profiling.
// GLOBAL: LITHTECH 0x004e24ac
uint32 g_Ticks_SetupBox;
// GLOBAL: LITHTECH 0x004e2f74
uint32 g_nClassifyGenericCalls;


// ------------------------------------------------------------------ //
// Point classification.
// ------------------------------------------------------------------ //

struct ClassifyPoints
{
	LTPlane			*m_pPlane;		// 0x00
	LTVector		*m_pPoints;		// 0x04
	PhysicsSphere	*m_pSphere;		// 0x08
	float			min, max;		// 0x0c
	int				*m_bCalcMinMax;	// 0x14
	LTVector		m_MinPos, m_MaxPos;	// 0x18
	int				m_iMinPoint, m_iMaxPoint;	// 0x30 which points gave min and max
};

typedef int (*ClassifyFn)(ClassifyPoints *pClassify);


// FUNCTION: LITHTECH 0x00418480
static int ReallyClassifyPointsGeneric(ClassifyPoints *pClassify)
{
	LTVector *pCur;
	int i;
	float dot;

	pCur = pClassify->m_pPoints;
	pClassify->min = (float)MAX_CREAL;
	pClassify->max = (float)-MAX_CREAL;

	for (i=0; i < NUM_BOX_POINTS; i++)
	{
		dot = pClassify->m_pPlane->DistTo(*pCur);

		if (dot < pClassify->min)
		{
			pClassify->min = dot;
			pClassify->m_iMinPoint = i;
		}
		else if (dot > pClassify->max)
		{
			pClassify->max = dot;
			pClassify->m_iMaxPoint = i;
		}

		++pCur;
	}

	*pClassify->m_bCalcMinMax = TRUE;

	// Changed this so that if the min is zero, the points are flush, and it is
	// considered to be on front side rather than backside...
	return (pClassify->min < -0.001f && pClassify->max > -0.001f) ? Intersect : (int)(pClassify->min > -0.001f);
}

// FUNCTION: LITHTECH 0x00418560
static int ClassifyPointsGeneric(ClassifyPoints *pClassify)
{
	float dot;

	g_nClassifyGenericCalls++;

	dot = pClassify->m_pPlane->DistTo(pClassify->m_pSphere->m_Center);
	if (dot > pClassify->m_pSphere->m_Radius)
	{
		*pClassify->m_bCalcMinMax = FALSE;
		return FrontSide;
	}
	else if (dot < -pClassify->m_pSphere->m_Radius)
	{
		*pClassify->m_bCalcMinMax = FALSE;
		return BackSide;
	}
	else
	{
		return ReallyClassifyPointsGeneric(pClassify);
	}
}

// FUNCTION: LITHTECH 0x00418600
static int ClassifyHighX(ClassifyPoints *pClassify)
{
	pClassify->min = (pClassify->m_MinPos.x) - pClassify->m_pPlane->m_Dist;
	pClassify->max = (pClassify->m_MaxPos.x) - pClassify->m_pPlane->m_Dist;
	return (int)(pClassify->min <= -0.001f && pClassify->max > -0.001f) ? Intersect : (int)(pClassify->min > -0.001f);
}

// FUNCTION: LITHTECH 0x00418660
static int ClassifyLowX(ClassifyPoints *pClassify)
{
	pClassify->min = -(pClassify->m_MaxPos.x) - pClassify->m_pPlane->m_Dist;
	pClassify->max = -(pClassify->m_MinPos.x) - pClassify->m_pPlane->m_Dist;
	return (int)(pClassify->min <= -0.001f && pClassify->max > -0.001f) ? Intersect : (int)(pClassify->min > -0.001f);
}

// FUNCTION: LITHTECH 0x004186c0
static int ClassifyHighY(ClassifyPoints *pClassify)
{
	pClassify->min = (pClassify->m_MinPos.y) - pClassify->m_pPlane->m_Dist;
	pClassify->max = (pClassify->m_MaxPos.y) - pClassify->m_pPlane->m_Dist;
	return (int)(pClassify->min <= -0.001f && pClassify->max > -0.001f) ? Intersect : (int)(pClassify->min > -0.001f);
}

// FUNCTION: LITHTECH 0x00418720
static int ClassifyLowY(ClassifyPoints *pClassify)
{
	pClassify->min = -(pClassify->m_MaxPos.y) - pClassify->m_pPlane->m_Dist;
	pClassify->max = -(pClassify->m_MinPos.y) - pClassify->m_pPlane->m_Dist;
	return (int)(pClassify->min <= -0.001f && pClassify->max > -0.001f) ? Intersect : (int)(pClassify->min > -0.001f);
}

// FUNCTION: LITHTECH 0x00418780
static int ClassifyHighZ(ClassifyPoints *pClassify)
{
	pClassify->min = (pClassify->m_MinPos.z) - pClassify->m_pPlane->m_Dist;
	pClassify->max = (pClassify->m_MaxPos.z) - pClassify->m_pPlane->m_Dist;
	return (int)(pClassify->min <= -0.001f && pClassify->max > -0.001f) ? Intersect : (int)(pClassify->min > -0.001f);
}

// FUNCTION: LITHTECH 0x004187e0
static int ClassifyLowZ(ClassifyPoints *pClassify)
{
	pClassify->min = -(pClassify->m_MaxPos.z) - pClassify->m_pPlane->m_Dist;
	pClassify->max = -(pClassify->m_MinPos.z) - pClassify->m_pPlane->m_Dist;
	return (int)(pClassify->min <= -0.001f && pClassify->max > -0.001f) ? Intersect : (int)(pClassify->min > -0.001f);
}

// GLOBAL: LITHTECH 0x004d07e8
ClassifyFn g_ClassifyFns[] =
{
	ClassifyHighX, ClassifyLowX,
	ClassifyHighY, ClassifyLowY,
	ClassifyHighZ, ClassifyLowZ,
	ClassifyPointsGeneric
};


// Sets up the box points, spheres and planes for the movement from g_P0 to g_P1.
// Returns FALSE if there isn't enough movement to generate the box info.
// The original builds the offset positions in temporaries (one more vector on the stack) and
// walks g_MovePts with an absolute pointer.
// STUB: LITHTECH 0x00418840
LTBOOL SetupBox()
{
	CountAdder cntAdd(&g_Ticks_SetupBox);
	LTVector v, offsetPos[2], *pDims;
	int i;

	VEC_SUB(v, g_P1, g_P0);
	if (fabs(v.x) < 0.0001f && fabs(v.y) < 0.0001f && fabs(v.z) < 0.0001f)
		return FALSE;

	VEC_ADD(offsetPos[0], g_BoxOffset, g_P0);
	VEC_ADD(offsetPos[1], g_BoxOffset, g_P1);

	// Setup the box points.
	pDims = &g_pCurRequest->m_Dims;
	for (i=0; i < 2; i++)
	{
		LTVector *pBoxPts = g_MovePts[i];
		LTVector &p = offsetPos[i];

		pBoxPts[0].Init(p.x - pDims->x, p.y + pDims->y, p.z - pDims->z);
		pBoxPts[1].Init(p.x - pDims->x, p.y + pDims->y, p.z + pDims->z);
		pBoxPts[2].Init(p.x + pDims->x, p.y + pDims->y, p.z + pDims->z);
		pBoxPts[3].Init(p.x + pDims->x, p.y + pDims->y, p.z - pDims->z);

		pBoxPts[4].Init(p.x - pDims->x, p.y - pDims->y, p.z - pDims->z);
		pBoxPts[5].Init(p.x - pDims->x, p.y - pDims->y, p.z + pDims->z);
		pBoxPts[6].Init(p.x + pDims->x, p.y - pDims->y, p.z + pDims->z);
		pBoxPts[7].Init(p.x + pDims->x, p.y - pDims->y, p.z - pDims->z);
	}

	// Setup the spheres.
	g_StartSphere.m_Center = offsetPos[0];
	g_EndSphere.m_Center = offsetPos[1];
	g_StartSphere.m_Radius = g_EndSphere.m_Radius = g_BoxRadius;

	// The whole sphere encloses the other two (as small as possible tho!  the size of this sphere
	// directly relates to how fast the physics are).
	g_BoxFindCenter = offsetPos[0] + v * 0.5f;
	g_BoxFindRadius = v.Mag() + g_BoxRadius;

	VEC_MIN(g_BoxMin, offsetPos[0], offsetPos[1]);
	VEC_MAX(g_BoxMax, offsetPos[0], offsetPos[1]);

	g_BoxMin -= *pDims;
	g_BoxMax += *pDims;

	g_BoxFindPlanes[0].m_Dist = g_BoxMin.x;
	g_BoxFindPlanes[1].m_Dist = -g_BoxMax.x;
	g_BoxFindPlanes[2].m_Dist = g_BoxMin.y;
	g_BoxFindPlanes[3].m_Dist = -g_BoxMax.y;
	g_BoxFindPlanes[4].m_Dist = g_BoxMin.z;
	g_BoxFindPlanes[5].m_Dist = -g_BoxMax.z;

	return TRUE;
}


// ------------------------------------------------------------------ //
// CMovingCylinder
// ------------------------------------------------------------------ //

class CMovingCylinder
{
public:
	// Re-calculates the non-provided information
	void Recalc();
	// Get which side of the plane the cylinder is on
	PolySide GetPlaneSide(const LTPlane *pPlane, LTVector &vCenter, LTBOOL bEnd, LTBOOL bOptimize);
	// Takes the appropriate action based on a collision
	void HandleCollision(const Node *pNode, CollideInfo *pInfo);
	// Gets the height section of the Y value in relation to the end location
	enum EHeightSection {
		HEIGHT_MIDDLE = 0,
		HEIGHT_ABOVE = 1,
		HEIGHT_BELOW = 2
	};
	EHeightSection GetHeightSection(float fYValue);

public:
	// ** Provided Members

	// Starting and ending positions of the cylinder
	LTVector m_vStart, m_vEnd;			// 0x00
	LTVector m_vRealEnd;				// 0x18 The final non-segmented ending position of the cylinder
	// Size of the cylinder  (m_fHeight is 1/2 total cylinder height)
	float m_fRadius, m_fHeight;			// 0x24
	// Perform stair-stepping
	LTBOOL m_bStairStep;				// 0x2c

	// ** Calculated Members

	// Radius of a sphere around the cylinder
	float m_fSphere;					// 0x30

	// Movement vector
	LTVector m_vMovement, m_vMovementDir;	// 0x34
	// Velocity
	float m_fVelocity;					// 0x4c

	// Center of the movement sphere
	LTVector m_vMoveMid;				// 0x50
	// Radius of the movement sphere
	float m_fMoveSphere;				// 0x5c

	// Squared member functions for optimization
	float m_fRadiusSqr, m_fSphereSqr, m_fMoveSphereSqr;	// 0x60
	float m_fVelocitySqr;				// 0x6c

	// Top/bottom of the cylinder in terms of movement
	float m_fMoveTop, m_fMoveBottom;	// 0x70

	// ** Collision information

	// Distance from m_vEnd to the plane
	float m_fDistToPlane;				// 0x78
	// Amount of the intrusion of the plane
	float m_fPlaneIntrusion;			// 0x7c

	// Furthest intruding point at m_vEnd
	LTVector m_vClosestPt;				// 0x80
	// Direction of intrusion
	LTVector m_vClosestDir;				// 0x8c
	// Distance of intrusion
	float m_fClosestDist;				// 0x98
	// Closest node
	const Node *m_pClosestNode;			// 0x9c
};


// FUNCTION: LITHTECH 0x00418cf0
void CMovingCylinder::Recalc()
{
	m_fRadiusSqr = m_fRadius * m_fRadius;
	m_fSphereSqr = m_fHeight * m_fHeight + m_fRadiusSqr;
	m_fSphere = (float)sqrt(m_fSphereSqr);

	m_vMovement = (m_vEnd - m_vStart);
	m_fVelocitySqr = m_vMovement.Dot(m_vMovement);
	m_fVelocity = (float)sqrt(m_fVelocitySqr);
	if (m_fVelocity != 0.0f)
		m_vMovementDir = m_vMovement * (1.0f / m_fVelocity);
	else
		m_vMovementDir.Init(0.0f, 0.0f, 0.0f);
	m_vMoveMid = (m_vStart + m_vEnd) * 0.5f;
	m_fMoveSphere = m_fSphere + (m_fVelocity * 0.5f);
	m_fMoveSphereSqr = m_fMoveSphere * m_fMoveSphere;

	m_fMoveTop = LTMAX(m_vStart.y, m_vEnd.y) + m_fHeight;
	m_fMoveBottom = LTMIN(m_vStart.y, m_vEnd.y) - m_fHeight;

	m_fClosestDist = 0;
	m_pClosestNode = LTNULL;
}


// Note : This is the general-case intersection method..  A specific case for horizontal
// planes would make this a lot faster
// FUNCTION: LITHTECH 0x00418ea0
PolySide CMovingCylinder::GetPlaneSide(const LTPlane *pPlane, LTVector &vCenter, LTBOOL bEnd, LTBOOL bOptimize)
{
	float fDistToPlane = pPlane->DistTo(vCenter);

	if (bEnd)
		m_fDistToPlane = fDistToPlane;

	if (bOptimize)
	{
		// Handle the obvious case
		if (fDistToPlane >= m_fSphere)
			return FrontSide;
		else if (fDistToPlane <= -m_fSphere)
			return BackSide;

		// Handle the vertical plane case
		if (pPlane->m_Normal.y == 0.0)
		{
			if (fDistToPlane >= m_fRadius)
				return FrontSide;
			else if (fDistToPlane <= -m_fRadius)
				return BackSide;
			else
			{
				if (bEnd)
					m_fPlaneIntrusion = m_fRadius - fDistToPlane;
				return Intersect;
			}
		}
		// Handle the horizontal plane case
		else if ((pPlane->m_Normal.x == 0.0) && (pPlane->m_Normal.z == 0.0))
		{
			if (fDistToPlane >= m_fHeight)
				return FrontSide;
			else if (fDistToPlane <= -m_fHeight)
				return BackSide;
			else
			{
				if (bEnd)
					m_fPlaneIntrusion = m_fHeight - fDistToPlane;
				return Intersect;
			}
		}
	}

	// Handle the other cases
	float fPlaneCircle = (float)sqrt(pPlane->m_Normal.x * pPlane->m_Normal.x + pPlane->m_Normal.z * pPlane->m_Normal.z);
	float fPlaneRadius = fPlaneCircle * m_fRadius;
	float fPlaneHeight = (float)sqrt(1.0f - fPlaneCircle * fPlaneCircle) * m_fHeight;
	float fProjMax = fPlaneRadius + fPlaneHeight;
	if (fDistToPlane >= fProjMax)
		return FrontSide;
	else if (fDistToPlane <= -fProjMax)
	{
		if (!bOptimize && bEnd)
			m_fPlaneIntrusion = (fProjMax * 2) - fDistToPlane;
		return BackSide;
	}
	else
	{
		if (bEnd)
			m_fPlaneIntrusion = fProjMax - fDistToPlane;
		return Intersect;
	}
}


// FUNCTION: LITHTECH 0x004190b0
CMovingCylinder::EHeightSection CMovingCylinder::GetHeightSection(float fYValue)
{
	if (fYValue < m_fMoveBottom)
		return HEIGHT_BELOW;
	else if (fYValue > m_fMoveTop)
		return HEIGHT_ABOVE;
	else
		return HEIGHT_MIDDLE;
}


void DoObjectCollisionResponse(CollisionInfo *pCollisionInfo, CollideInfo *pInfo,
	LTObject *pObject, LTObject *pWorldObj, WorldBsp *pWorld, Node *pNode, LTVector *pStopPlane);

// Moves the cylinder away from the closest intrusion and stops the object's velocity.
// FUNCTION: LITHTECH 0x00419930
void CMovingCylinder::HandleCollision(const Node *pNode, CollideInfo *pInfo)
{
	// Move away from the "closest" point
	LTVector vMoveAway = m_vClosestDir * -(m_fClosestDist * 1.01f);
	// Apply a sideways reflection vector to avoid some "sticky" situations caused by low framerate
	if (m_vClosestDir.y == 0.0f)
	{
		LTVector vRight = m_vMovementDir.Cross(LTVector(0.0f, 1.0f, 0.0f));
		vMoveAway += vRight * (vRight.Dot(m_vClosestDir) * -2.0f);
	}
	m_vEnd += vMoveAway;

	// Attach to horizontal planes
	if (m_bStairStep && (m_vClosestDir.y < 0.707f))
	{
		g_pCurInfo->m_pStandingOn = (Node*)m_pClosestNode;
	}

	if (pNode)
	{
		LTVector vNormal = vMoveAway;
		vNormal.Norm();
		DoObjectCollisionResponse(g_pCurRequest->m_pCollisionInfo, pInfo, g_pCurRequest->m_pObject,
			g_pCurRequest->m_pWorldObj, g_pCurRequest->m_pWorld, (Node*)pNode, &vNormal);
	}

	// Recalculate the pre-calculated data
	Recalc();
}


// Collides the box with the tree.  Called twice when the first pass hit something: the second
// pass runs on the box SetupBox rebuilt from the adjusted movement (0x0041c460).
LTBOOL CollideBoxWithTree(Node *pRoot, CollideInfo *pInfo, LTBOOL bSecondPass, LTBOOL *pbHit);

// FUNCTION: LITHTECH 0x0041c400
LTBOOL CollideBox(Node *pRoot, CollideInfo *pInfo)
{
	LTBOOL bHit;

	bHit = LTFALSE;
	if (!CollideBoxWithTree(pRoot, pInfo, LTFALSE, &bHit))
		return LTFALSE;

	if (!bHit)
		return LTTRUE;

	if (!SetupBox())
		return LTFALSE;

	return CollideBoxWithTree(pRoot, pInfo, LTTRUE, &bHit);
}


// ------------------------------------------------------------------ //
// Pushing the box out of planes.
// ------------------------------------------------------------------ //

#define MAX_PUSH_PLANES		20
#define MAX_PUSH_ITERATIONS	10

// A plane the box got pushed out of (0x14 bytes).
struct PushPlane
{
	LTPlane		m_Plane;	// 0x00
	void		*m_pID;		// 0x10 what it came from (to skip duplicates)
};

// GLOBAL: LITHTECH 0x004e2d80
PushPlane g_PushPlanes[MAX_PUSH_PLANES];
// GLOBAL: LITHTECH 0x004e14b0
static int g_nPushPlanes;
// GLOBAL: LITHTECH 0x004e0c9c
static uint32 g_Ticks_AddPushPlane;


// Adds the movement to the position, making sure the movement doesn't get lost to
// floating point precision.
// Close: the original squares vPos's components in a different order and keeps the scaled
// movement on the FPU stack for the final add.
// STUB: LITHTECH 0x0041d640
void AddMovement(LTVector *pOut, LTVector vPos, LTVector vMove)
{
	float fMoveMag, fScale;

	*pOut = vPos;

	fMoveMag = vMove.Mag();
	if (!(fMoveMag < FLT_EPSILON))
	{
		*pOut += vMove;

		// If the movement didn't change the position, make it big enough to.
		if (pOut->x == vPos.x && pOut->y == vPos.y && pOut->z == vPos.z)
		{
			fScale = (vPos.Mag() * FLT_EPSILON) / fMoveMag;
			vMove *= fScale;
			*pOut = vPos + vMove;
		}
	}
}


// Pushes the box at the end of the movement out of all the planes it touches.
// The point setup is scheduled differently (the original shares the sums on the FPU stack).
// STUB: LITHTECH 0x0041d820
static void PushBoxOutOfPlanes()
{
	LTVector pos, *pDims, pts[NUM_BOX_POINTS], move;
	int i, j, nIterations;
	float minDist, dist, pushDist;
	LTPlane *pPlane;

	pos = g_BoxOffset + g_P1;
	pDims = &g_pCurRequest->m_Dims;

	pts[0].Init(pos.x + pDims->x, pos.y + pDims->y, pos.z + pDims->z);
	pts[1].Init(pos.x - pDims->x, pos.y + pDims->y, pos.z + pDims->z);
	pts[2].Init(pos.x - pDims->x, pos.y + pDims->y, pos.z - pDims->z);
	pts[3].Init(pos.x + pDims->x, pos.y + pDims->y, pos.z - pDims->z);
	pts[4].Init(pos.x + pDims->x, pos.y - pDims->y, pos.z + pDims->z);
	pts[5].Init(pos.x - pDims->x, pos.y - pDims->y, pos.z + pDims->z);
	pts[6].Init(pos.x - pDims->x, pos.y - pDims->y, pos.z - pDims->z);
	pts[7].Init(pos.x + pDims->x, pos.y - pDims->y, pos.z - pDims->z);

	nIterations = 0;
	for (i=0; i < g_nPushPlanes && nIterations < MAX_PUSH_ITERATIONS; i++)
	{
		pPlane = &g_PushPlanes[i].m_Plane;

		minDist = (float)MAX_CREAL;
		for (j=0; j < NUM_BOX_POINTS; j++)
		{
			dist = pPlane->DistTo(pts[j]);
			if (dist < minDist)
				minDist = dist;
		}

		if (minDist < 0.09f)
		{
			// Push it out and start over.
			pushDist = 0.1f - minDist;
			move = pPlane->m_Normal * pushDist;
			g_P1 += move;
			for (j=0; j < NUM_BOX_POINTS; j++)
				pts[j] += move;

			i = -1;
			nIterations++;
		}
	}
}


// FUNCTION: LITHTECH 0x0041d770
void AddPushPlane(LTPlane *pPlane, void *pID)
{
	CountAdder cntAdd(&g_Ticks_AddPushPlane);
	int i;

	if (g_nPushPlanes < MAX_PUSH_PLANES)
	{
		// Don't add the same plane twice.
		if (pID)
		{
			for (i=0; i < g_nPushPlanes; i++)
			{
				if (g_PushPlanes[i].m_pID == pID)
					return;
			}
		}

		g_PushPlanes[g_nPushPlanes].m_Plane = *pPlane;
		g_PushPlanes[g_nPushPlanes].m_pID = pID;
		g_nPushPlanes++;

		if (g_nPushPlanes > 1)
			PushBoxOutOfPlanes();
	}
}


// ------------------------------------------------------------------ //
// Collision response.
// ------------------------------------------------------------------ //

#define IMPULSE_TIME_CONSTANT	0.01f
#define EXTRA_PENETRATION_ADD	0.05f

inline LTBOOL CalculateCollisionResponse(LTObject *pObj, LTVector *pObjectVel, LTVector *pStopPlane,
	LTVector *pVelAdd, LTVector *pForce)
{
	float vDotN;

	// *pVelAdd = -N * (V * N)
	vDotN = pStopPlane->Dot(*pObjectVel);

	if (vDotN < 0.0f)
	{
		*pVelAdd = *pStopPlane * -vDotN;

		// acceleration = *pVelAdd * IMPULSE_TIME_CONSTANT
		// force = acceleration * pObj->m_Mass
		*pForce = (*pVelAdd * IMPULSE_TIME_CONSTANT) * pObj->m_Mass;

		// This makes it so the velocity doesn't actually switch all the way
		// to zero in the direction of the normal, so friction will still
		// be applied and there is a tiny amount of force causing the guy
		// to stick to the floor.
		if (pStopPlane->y > 0.7071f)
		{
			*pVelAdd += *pStopPlane * -EXTRA_PENETRATION_ADD;
		}

		return LTTRUE;
	}
	else
	{
		// If they weren't moving in the direction of the normal, don't do anything.
		return LTFALSE;
	}
}

// Sets up the collision info and stops the object's velocity on the plane it hit.
// Close: the original computes GetPlane after saving the registers and keeps the operator+
// argument copy 0xc lower on the stack.
// STUB: LITHTECH 0x00419b20
void DoObjectCollisionResponse(CollisionInfo *pCollisionInfo, CollideInfo *pInfo,
	LTObject *pObject, LTObject *pWorldObj, WorldBsp *pWorld, Node *pNode, LTVector *pStopPlane)
{
	LTVector velAdd, objectVel, force;

	// Setup the collision info...
	// Because we could hit more than one poly, the collision info will contain the last poly hit...
	pCollisionInfo->m_Plane = *pNode->GetPlane();
	pCollisionInfo->m_hObject = (HOBJECT)pWorldObj;

	if (pWorld)
	{
		pCollisionInfo->m_hPoly = pWorld->MakeHPoly(pNode);
	}
	else
	{
		pCollisionInfo->m_hPoly = INVALID_HPOLY;
	}

	objectVel = pObject->m_Velocity + pInfo->m_VelOffset;

	// Do the collision response.
	if (CalculateCollisionResponse(pObject, &objectVel, pStopPlane, &velAdd, &force))
	{
		// Notify them if they want to be notified.
		if (pObject->m_Flags & FLAG_TOUCH_NOTIFY)
		{
			pInfo->m_vForce += force;
		}

		pObject->m_InternalFlags |= IFLAG_APPLYPHYSICS;

		// Stop their velocity on this plane!
		pInfo->m_VelOffset += velAdd;
	}
}


// Goes thru the BSP and tests if the box intersects the BSP.
// It uses the sphere as a quick approximation to the box first.
// GLOBAL: LITHTECH 0x004e14b8
static Node *g_SimpleBoxStack[1000];

// FUNCTION: LITHTECH 0x0041b2a0
LTBOOL SimpleBoxBSPIntersect(Node *pRoot, PhysicsSphere *pSphere)
{
	Node **stackPos = g_SimpleBoxStack;
	float dot;

	for (;;)
	{
		// Done?
		if (pRoot->m_Flags & (NF_IN | NF_OUT))
		{
			if (stackPos == g_SimpleBoxStack)
				return FALSE;

			--stackPos;
			pRoot = *stackPos;
		}

		dot = pRoot->GetPlane()->DistTo(pSphere->m_Center);

		if (dot > pSphere->m_Radius)
		{
			pRoot = pRoot->m_Sides[FrontSide];
		}
		else if (dot < -pSphere->m_Radius)
		{
			pRoot = pRoot->m_Sides[BackSide];
		}
		else
		{
			// Does it really intersect?
			if (pRoot->m_pPoly && PolyTouchesBox(pRoot->m_pPoly, LTNULL, LTNULL))
				return TRUE;

			// Go into both sides.
			if ((pRoot->m_Sides[BackSide]->m_Flags & (NF_IN | NF_OUT)) == 0)
				*stackPos++ = pRoot->m_Sides[BackSide];
			pRoot = pRoot->m_Sides[FrontSide];
		}
	}

	return FALSE;
}


// Does this box intersect this BSP tree?
// Close: the original runs out of inline budget earlier (the last two box points use the
// out-of-line Init) and squares the sphere's z first.
// STUB: LITHTECH 0x0041aed0
LTBOOL DoesBoxIntersectBSP(Node *pRoot, LTVector &vMin, LTVector &vMax)
{
	int status;

	// Setup the sphere approximation.
	g_BoxFindCenter = vMax - vMin;
	g_BoxFindCenter *= 0.5f;
	g_BoxFindRadius = g_BoxFindCenter.Mag() + 0.1f;
	g_BoxFindCenter += vMin;

	// If any of the points are outside, it intersects.
	if (!ci_IsPointInsideBSP(pRoot, g_BoxFindCenter) ||
		!ci_IsPointInsideBSP(pRoot, vMin) ||
		!ci_IsPointInsideBSP(pRoot, vMax))
	{
		return TRUE;
	}

	status = ci_IsSphereInsideBSP(&pRoot, &g_BoxFindCenter, g_BoxFindRadius);
	if (status == 1)
		return FALSE;
	else if (status == 0)
		return TRUE;

	// Setup the box points.
	g_BoxOffset.Init(0.0f, 0.0f, 0.0f);

	g_MovePts[0][0].Init(vMin.x, vMax.y, vMin.z);
	g_MovePts[0][1].Init(vMin.x, vMax.y, vMax.z);
	g_MovePts[0][2].Init(vMax.x, vMax.y, vMax.z);
	g_MovePts[0][3].Init(vMax.x, vMax.y, vMin.z);
	g_MovePts[0][4] = g_MovePts[0][0];
	g_MovePts[0][5] = g_MovePts[0][1];
	g_MovePts[0][6] = g_MovePts[0][2];
	g_MovePts[0][7] = g_MovePts[0][3];

	g_MovePts[1][0].Init(vMin.x, vMin.y, vMin.z);
	g_MovePts[1][1].Init(vMin.x, vMin.y, vMax.z);
	g_MovePts[1][2].Init(vMax.x, vMin.y, vMax.z);
	g_MovePts[1][3].Init(vMax.x, vMin.y, vMin.z);
	g_MovePts[1][4] = g_MovePts[1][0];
	g_MovePts[1][5] = g_MovePts[1][1];
	g_MovePts[1][6] = g_MovePts[1][2];
	g_MovePts[1][7] = g_MovePts[1][3];

	g_BoxFindPlanes[0].m_Dist = vMin.x;
	g_BoxFindPlanes[1].m_Dist = -vMax.x;
	g_BoxFindPlanes[2].m_Dist = vMin.y;
	g_BoxFindPlanes[3].m_Dist = -vMax.y;
	g_BoxFindPlanes[4].m_Dist = vMin.z;
	g_BoxFindPlanes[5].m_Dist = -vMax.z;

	// g_BoxFindRadius and g_BoxFindCenter make up the whole-movement sphere.
	return SimpleBoxBSPIntersect(pRoot, (PhysicsSphere*)&g_BoxFindRadius);
}


// Moves the object up stairs (FLAG_STAIRSTEP): finds what the movement hits and stands on it.
// FUNCTION: LITHTECH 0x0041b380
void StairStep(CollideRequest *pRequest, CollideInfo *pInfo)
{
	LTVector intersectPt;
	LTPlane intersectPlane;
	Node *pNode;

	pInfo->m_FinalPos = pRequest->m_NewPos;

	pNode = IntersectLine(pRequest->m_pWorld->GetRootNode(), &pRequest->m_OriginalPos, &pInfo->m_FinalPos,
		&intersectPt, &intersectPlane);
	if (pNode)
	{
		if ((pInfo->m_FinalPos - intersectPt).MagSqr() > 0.001f)
		{
			DoObjectCollisionResponse(g_pCurRequest->m_pCollisionInfo, pInfo, pRequest->m_pObject,
				g_pCurRequest->m_pWorldObj, g_pCurRequest->m_pWorld, pNode, &pNode->GetPlane()->m_Normal);

			pInfo->m_FinalPos = intersectPt;
			pInfo->m_nHits++;

			// Can they stand on it?
			if (pNode->GetPlane()->m_Normal.y > 0.01f)
			{
				if (!pInfo->m_pStandingOn ||
					pInfo->m_pStandingOn->GetPlane()->m_Normal.y < pNode->GetPlane()->m_Normal.y)
				{
					g_pCurInfo->m_pStandingOn = pNode;
				}
			}
		}
	}
}


// FUNCTION: LITHTECH 0x0041f3d0
void DoInterObjectCollisionResponse(MoveAbstract *pAbstract,
	LTObject *pObj1, LTObject *pObj2, LTVector *pPlane, float fPlaneDist)
{
	float vDotN[2], forceScale, forceMagSqr, forceMag;
	LTVector oppositeStopPlane, velAdd[2], combinedVelAdd;
	LTVector combinedForce;
	CollisionInfo *pCollisionInfo;

	pCollisionInfo = pAbstract->GetCollisionInfo();

	// Get stopping velocities (if any).
	vDotN[0] = VEC_DOT(*pPlane, pObj1->m_Velocity);
	velAdd[0] = *pPlane * -vDotN[0];

	oppositeStopPlane = -*pPlane;
	vDotN[1] = oppositeStopPlane.Dot(pObj2->m_Velocity);
	velAdd[1] = oppositeStopPlane * -vDotN[1];

	pCollisionInfo->m_Plane.m_Normal = *pPlane;
	pCollisionInfo->m_Plane.m_Dist = fPlaneDist;
	pCollisionInfo->m_hPoly = 0;

	// Get combined force (velAdd[0] - velAdd[1] .. ie: if velAdd[1] is in opposite direction,
	// force is greater).
	combinedVelAdd = velAdd[0] - velAdd[1];
	forceScale = IMPULSE_TIME_CONSTANT * (pObj1->m_Mass + pObj2->m_Mass);
	combinedForce = combinedVelAdd * forceScale;

	forceMagSqr = combinedForce.MagSqr();
	forceMag = (float)sqrt(forceMagSqr);

	// Notify the next of kin...
	if (pObj1->m_Flags & FLAG_TOUCH_NOTIFY)
	{
		if (forceMagSqr >= pObj1->m_ForceIgnoreLimitSqr)
		{
			pAbstract->DoTouchNotify(pObj1, pObj2, velAdd[0], forceMag);
		}
	}

	if (pObj2->m_Flags & FLAG_TOUCH_NOTIFY)
	{
		if (forceMagSqr >= pObj2->m_ForceIgnoreLimitSqr)
		{
			pAbstract->DoTouchNotify(pObj2, pObj1, velAdd[1], -forceMag);
		}
	}


	// Stop their velocities and apply the collision to their acceleration.
	if (vDotN[0] < 0.0f && (pObj1->m_BPriority <= pObj2->m_BPriority))
	{
		pObj1->m_InternalFlags |= IFLAG_APPLYPHYSICS;
		pObj1->m_Velocity += velAdd[0];
	}

	if (vDotN[1] < 0.0f && (pObj2->m_BPriority <= pObj1->m_BPriority))
	{
		pObj2->m_InternalFlags |= IFLAG_APPLYPHYSICS;
		pObj2->m_Velocity += velAdd[1];
	}
}


// FUNCTION: LITHTECH 0x0041d620 ?GetPlane@Node@@QAEPAVLTPlane@@XZ
// Out-of-line copies of the LTVector inlines (the big collision functions call them; the table
// keeps them in the object until those functions are decompiled).
// FUNCTION: LITHTECH 0x0041f680 ?Init@?$_CVector@M@@QAEXMMM@Z
// FUNCTION: LITHTECH 0x0041f6a0 ?Mag@?$_CVector@M@@QBEMXZ
// FUNCTION: LITHTECH 0x0041f6d0 ?Dot@?$_CVector@M@@QBEMV1@@Z
// FUNCTION: LITHTECH 0x0041f6f0 ??G?$_CVector@M@@QBE?AV0@XZ
// FUNCTION: LITHTECH 0x0041f710 ??H?$_CVector@M@@QBE?AV0@V0@@Z
// FUNCTION: LITHTECH 0x0041f740 ??G?$_CVector@M@@QBE?AV0@V0@@Z
// FUNCTION: LITHTECH 0x0041f770 ??D?$_CVector@M@@QBE?AV0@M@Z
// FUNCTION: LITHTECH 0x0041f7a0 ??Y?$_CVector@M@@QAEXV0@@Z
// FUNCTION: LITHTECH 0x0041f7c0 ??X?$_CVector@M@@QAEXM@Z
// FUNCTION: LITHTECH 0x0041f7e0 ??9?$_CVector@M@@QBEIABV0@@Z
// FUNCTION: LITHTECH 0x0041f820 ?Norm@?$_CVector@M@@QAEXM@Z
LTPlane* (Node::*g_pfnNodeGetPlane)() = &Node::GetPlane;
void (LTVector::*g_pfnVecInit)(float, float, float) = &LTVector::Init;
float (LTVector::*g_pfnVecMag)() const = &LTVector::Mag;
float (LTVector::*g_pfnVecDot)(LTVector) const = &LTVector::Dot;
LTVector (LTVector::*g_pfnVecNeg)() const = &LTVector::operator-;
LTVector (LTVector::*g_pfnVecAdd)(const LTVector) const = &LTVector::operator+;
LTVector (LTVector::*g_pfnVecSub)(const LTVector) const = &LTVector::operator-;
LTVector (LTVector::*g_pfnVecScale)(float) const = &LTVector::operator*;
void (LTVector::*g_pfnVecAddEq)(const LTVector) = &LTVector::operator+=;
void (LTVector::*g_pfnVecScaleEq)(float) = &LTVector::operator*=;
LTBOOL (LTVector::*g_pfnVecNotEq)(const LTVector&) const = &LTVector::operator!=;
void (LTVector::*g_pfnVecNorm)(float) = &LTVector::Norm;
