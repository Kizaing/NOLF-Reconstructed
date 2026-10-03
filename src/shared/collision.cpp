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
#include "csweptsphere.h"
#include "moveobject.h"
#include "counter.h"
#include "impl_common.h"
#include "iltmath.h"
#include "iltphysics.h"

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
// The offset positions are built in temporaries (the vector constructor) like the original, but the
// original reads the box dimensions through both pDims (x) and g_pCurRequest (y, z) and adds the
// point and the dimension the other way round.
// STUB: LITHTECH 0x00418840
LTBOOL SetupBox()
{
	CountAdder cntAdd(&g_Ticks_SetupBox);
	LTVector v, offsetPos[2], *pDims;
	int i;

	VEC_SUB(v, g_P1, g_P0);
	if (fabs(v.x) < 0.0001f && fabs(v.y) < 0.0001f && fabs(v.z) < 0.0001f)
		return FALSE;

	offsetPos[0] = LTVector(g_BoxOffset.x + g_P0.x, g_BoxOffset.y + g_P0.y, g_BoxOffset.z + g_P0.z);
	offsetPos[1] = LTVector(g_BoxOffset.x + g_P1.x, g_BoxOffset.y + g_P1.y, g_BoxOffset.z + g_P1.z);

	// Setup the box points.
	pDims = &g_pCurRequest->m_Dims;
	for (i=0; i < 2; i++)
	{
		LTVector *pBoxPts = g_MovePts[i];
		LTVector &p = offsetPos[i];

		pBoxPts[0].Init(p.x - pDims->x, p.y + g_pCurRequest->m_Dims.y, p.z - g_pCurRequest->m_Dims.z);
		pBoxPts[1].Init(p.x - pDims->x, p.y + g_pCurRequest->m_Dims.y, p.z + g_pCurRequest->m_Dims.z);
		pBoxPts[2].Init(p.x + pDims->x, p.y + g_pCurRequest->m_Dims.y, p.z + g_pCurRequest->m_Dims.z);
		pBoxPts[3].Init(p.x + pDims->x, p.y + g_pCurRequest->m_Dims.y, p.z - g_pCurRequest->m_Dims.z);

		pBoxPts[4].Init(p.x - pDims->x, p.y - g_pCurRequest->m_Dims.y, p.z - g_pCurRequest->m_Dims.z);
		pBoxPts[5].Init(p.x - pDims->x, p.y - g_pCurRequest->m_Dims.y, p.z + g_pCurRequest->m_Dims.z);
		pBoxPts[6].Init(p.x + pDims->x, p.y - g_pCurRequest->m_Dims.y, p.z + g_pCurRequest->m_Dims.z);
		pBoxPts[7].Init(p.x + pDims->x, p.y - g_pCurRequest->m_Dims.y, p.z - g_pCurRequest->m_Dims.z);
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
	// Collides the cylinder with a world polygon
	LTBOOL CollideWith(WorldPoly *pPoly, Node *pNode);
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


// Collides the cylinder with a world polygon: finds the point of the polygon (or its plane)
// that pushes furthest into the cylinder at m_vEnd.
// Not matching: transcribed from the disassembly, the float expressions and locals are untuned (the
// original also keeps its frame 0x28 bytes larger and calls GetHeightSection out of line).
// STUB: LITHTECH 0x004190f0
LTBOOL CMovingCylinder::CollideWith(WorldPoly *pPoly, Node *pNode)
{
	LTPlane *pPlane;
	LTVector vProj, vPrev, vCur, vEdge, vBest, vEdgeNormal, vSlope;
	LTVector vDiff;
	SPolyVertex *pVert;
	float fRadius, fDist, fAbsY, fHeightDiff, fBest, fLen, fInv, t, fD, fSide;
	int iPrevSection, iCurSection, i, nVerts;
	LTBOOL bInside, bHoriz, bStep;

	pPlane = pPoly->m_pPlane;
	if (m_vMovementDir.Dot(pPlane->m_Normal) > 0.01)
		return LTFALSE;

	fRadius = m_fMoveSphere + pPoly->m_Radius;
	vDiff = pPoly->m_Center - m_vMoveMid;
	if (vDiff.MagSqr() > fRadius * fRadius)
		return LTFALSE;

	// The point on the plane nearest the cylinder's end.
	fDist = -m_fDistToPlane;
	vProj.x = fDist * pPlane->m_Normal.x + m_vEnd.x;
	vProj.y = fDist * pPlane->m_Normal.y + m_vEnd.y;
	vProj.z = fDist * pPlane->m_Normal.z + m_vEnd.z;

	fAbsY = (float)fabs(pPlane->m_Normal.y);
	if (fAbsY < 0.01f)
	{
		// A vertical plane: keep the point within the cylinder's height.
		if (pPoly->m_Center.y < m_fMoveBottom)
			vProj.y = m_fMoveBottom;
		else if (pPoly->m_Center.y <= m_fMoveTop)
			vProj.y = pPoly->m_Center.y;
		else
			vProj.y = m_fMoveTop;
	}

	bInside = LTTRUE;
	bHoriz = LTFALSE;
	bStep = LTFALSE;
	if (fAbsY >= 0.9f)
	{
		fHeightDiff = m_vEnd.y - pPoly->m_Center.y;
		bHoriz = LTTRUE;
		if (m_bStairStep && pPlane->m_Normal.y > 0.0f && fHeightDiff >= 0.0f &&
			m_fHeight * 0.5f > m_fHeight - fHeightDiff)
		{
			bStep = LTTRUE;
		}
	}

	nVerts = pPoly->m_nVertices;
	pVert = (SPolyVertex*)(pPoly + 1);
	vPrev = *pVert[nVerts - 1].m_Vec;
	vBest = vPrev;
	fBest = m_fSphere;
	for (i = 0; i < nVerts; i++)
	{
		LTVector *pCur = pVert[i].m_Vec;

		vCur = *pCur;
		vEdge = vCur - vPrev;

		iPrevSection = GetHeightSection(vPrev.y);
		iCurSection = GetHeightSection(vCur.y);
		if (iPrevSection != iCurSection || iPrevSection == 0)
		{
			fLen = (float)sqrt(vEdge.x * vEdge.x + vEdge.z * vEdge.z);
			if (fLen > 0.01f)
			{
				fInv = 1.0f / fLen;
				t = (vEdge.z * fInv * (m_vEnd.z - vPrev.z) + vEdge.x * fInv * (m_vEnd.x - vPrev.x)) * fInv;
				if (t >= 0.0f && t <= 1.0f)
				{
					vCur.x = t * vEdge.x + vPrev.x;
					vCur.y = vEdge.y * t + vPrev.y;
					vCur.z = vEdge.z * t + vPrev.z;
				}
			}

			if (vEdge.y != 0.0f && (iPrevSection != 0 || iCurSection != 0))
			{
				fLen = (float)sqrt(vEdge.z * vEdge.z + vEdge.x * vEdge.x + vEdge.y * vEdge.y);
				if (fLen != 0.0f)
				{
					fInv = 1.0f / fLen;
					vEdge.x *= fInv;
					vEdge.y *= fInv;
					vEdge.z *= fInv;
				}

				fInv = 1.0f / vEdge.y;
				vSlope.x = vEdge.x * fInv;
				vSlope.z = fInv * vEdge.z;
				if (vCur.y < m_fMoveBottom)
				{
					fD = m_fMoveBottom - vCur.y;
					vCur.y = m_fMoveBottom;
					vCur.x = vSlope.x * fD + vCur.x;
					vCur.z = vSlope.z * fD + vCur.z;
				}
				if (m_fHeight + m_vEnd.y < vCur.y)
				{
					fD = m_fMoveTop - vCur.y;
					vCur.y = m_fMoveTop;
					vCur.x = vSlope.x * fD + vCur.x;
					vCur.z = vSlope.z * fD + vCur.z;
				}
			}

			fDist = vCur.x - m_vEnd.x;
			fD = vCur.z - m_vEnd.z;
			fDist = (float)sqrt(fD * fD + fDist * fDist);
			if (fDist < fBest)
			{
				vBest = vCur;
				fBest = fDist;
			}
		}

		if (bInside)
		{
			vEdgeNormal.x = pPlane->m_Normal.y * vEdge.z - pPlane->m_Normal.z * vEdge.y;
			vEdgeNormal.y = pPlane->m_Normal.z * vEdge.x - pPlane->m_Normal.x * vEdge.z;
			vEdgeNormal.z = pPlane->m_Normal.x * vEdge.y - pPlane->m_Normal.y * vEdge.x;
			fLen = (float)sqrt(vEdgeNormal.x * vEdgeNormal.x + vEdgeNormal.y * vEdgeNormal.y +
				vEdgeNormal.z * vEdgeNormal.z);
			if (fLen != 0.0f)
			{
				fInv = 1.0f / fLen;
				vEdgeNormal.x *= fInv;
				vEdgeNormal.y *= fInv;
				vEdgeNormal.z *= fInv;
			}

			fSide = (vEdgeNormal.y * vProj.y + vEdgeNormal.x * vProj.x + vEdgeNormal.z * vProj.z) -
				(vEdgeNormal.z * pCur->z + pCur->x * vEdgeNormal.x + vEdgeNormal.y * pCur->y);
			if (fSide < -0.01f)
				bInside = LTFALSE;
		}

		vPrev = *pCur;
	}

	fDist = vProj.x - m_vEnd.x;
	fD = vProj.z - m_vEnd.z;
	if (!(fBest < m_fRadius) || (bInside && !bHoriz && (float)sqrt(fD * fD + fDist * fDist) < fBest) || bStep)
	{
		if (!bInside)
			return LTFALSE;

		// The plane itself is the closest thing.
		if (m_fClosestDist < m_fPlaneIntrusion)
		{
			m_vClosestPt = vProj;
			m_fClosestDist = m_fPlaneIntrusion;
			m_vClosestDir.x = -pPlane->m_Normal.x;
			m_vClosestDir.y = -pPlane->m_Normal.y;
			m_vClosestDir.z = -pPlane->m_Normal.z;
			m_pClosestNode = pNode;
		}
	}
	else if (m_fClosestDist < m_fRadius - fBest)
	{
		// An edge or vertex of the polygon.
		m_vClosestPt = vBest;
		if (fBest != 0.0f)
		{
			m_vClosestDir.x = vBest.x - m_vEnd.x;
			m_vClosestDir.y = 0.0f;
			m_vClosestDir.z = vBest.z - m_vEnd.z;
			fLen = (float)sqrt(m_vClosestDir.z * m_vClosestDir.z + m_vClosestDir.y * m_vClosestDir.y +
				m_vClosestDir.x * m_vClosestDir.x);
			if (fLen != 0.0f)
			{
				fInv = 1.0f / fLen;
				m_vClosestDir.x *= fInv;
				m_vClosestDir.y *= fInv;
				m_vClosestDir.z *= fInv;
			}
		}
		else
		{
			m_vClosestDir.x = 0.0f;
			m_vClosestDir.y = 0.0f;
			m_vClosestDir.z = 0.0f;
		}
		m_pClosestNode = pNode;
		m_fClosestDist = m_fRadius - fBest;
		return LTTRUE;
	}

	return LTTRUE;
}


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


// ------------------------------------------------------------------ //
// Collision response.
// ------------------------------------------------------------------ //

#define IMPULSE_TIME_CONSTANT	0.01f
#define EXTRA_PENETRATION_ADD	0.05f

inline LTBOOL CalculateCollisionResponse(LTObject *pObj, LTVector *pObjectVel, LTVector *pStopPlane,
	LTVector *pVelAdd, LTVector *pForce)
{
	float vDotN;
	LTVector accel;
	LTVector frictionZone;

	// *pVelAdd = -N * (V * N)
	vDotN = pStopPlane->Dot(*pObjectVel);

	if (vDotN < 0.0f)
	{
		*pVelAdd = *pStopPlane * -vDotN;

		// acceleration = *pVelAdd * IMPULSE_TIME_CONSTANT
		// force = acceleration * pObj->m_Mass
		accel = *pVelAdd * IMPULSE_TIME_CONSTANT;
		*pForce = accel * (float)pObj->m_Mass;

		// This makes it so the velocity doesn't actually switch all the way
		// to zero in the direction of the normal, so friction will still
		// be applied and there is a tiny amount of force causing the guy
		// to stick to the floor.
		if (pStopPlane->y > 0.7071f)
		{
			frictionZone = *pStopPlane * -EXTRA_PENETRATION_ADD;
			*pVelAdd += frictionZone;
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
// FUNCTION: LITHTECH 0x00419b20
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


// ------------------------------------------------------------------ //
// Sphere physics (FLAG2_SPHEREPHYSICS), Talon only.
// ------------------------------------------------------------------ //

// One node of a PBlock's mini BSP (6 bytes).
struct PBlockNode
{
	uint16		m_iNode;		// 0x00 index in WorldBsp::m_Nodes (0xFFFF = no poly)
	uint16		m_Sides[2];		// 0x02 child index per PolySide (0xFFFE/0xFFFF end a branch)
};

#define PBLOCK_STACK_SIZE	1024

// The world polygons a sphere move found and the world models they belong to (8 bytes).
struct SphereHitPoly
{
	LTObject	*m_pObject;		// 0x00
	Node		*m_pNode;		// 0x04
};

// GLOBAL: LITHTECH 0x004e0cb0
static uint16 g_PBlockStack[PBLOCK_STACK_SIZE];
// GLOBAL: LITHTECH 0x004e24c0
static WorldPoly *g_SphereTestPolys[0x200];


void GetSphereCollideTestPolys(SphereMoveInfo *pInfo, WorldPoly **pPolies, int *pnPolies, int nMaxPolies,
	LTVector *pStart, LTVector *pEnd, SphereHitPoly *pHits);
void GetSpherePosTestPolys(SphereMoveInfo *pInfo, WorldPoly **pPolies, int *pnPolies, int nMaxPolies,
	LTVector vPos, SphereHitPoly *pHits);
void OrientMovement(SphereMoveInfo *pInfo);

// Moves a sphere physics object (FLAG2_SPHEREPHYSICS), sliding it along the polygons it hits.
// The x87 operand order of the crease test's x term (`fld v.x; fmul this.x`) came out right once the remaining
// fraction is inline in `vDelta * (1.0f - fTime)` instead of an fRemain local: an earlier statement decides it
// (README, wave 6). Also needed: `do {...} while(1)` with the sphere-test failure as a `break` to the
// orient code after the loop (a `for(;;)` with the return inside the loop duplicates the loop head), the two "gave
// up" exits (a normal that is too short, 10 iterations) each restoring pDest and sharing one block, positive
// forms (`!(x > c)`, `fTime <= m_fHitTime`), a `bool` bFound (byte register), `0 < m_nIterations` for the return
// (`cmp al,dl; sbb; neg`), the crease test as `vNormal.Dot(m_Normals[i])` (the stack copy is of the normal list's
// entry), and `m_vDestPos = m_vStartPos` for the exits (member to member loads all three words first).
// FUNCTION: LITHTECH 0x00419d40
LTBOOL MoveSphere(SphereMoveInfo *pInfo)
{
	LTVector vNormal(0.0f, 0.0f, 0.0f);
	float fTime, fDot;
	int bWall, nPolies, i;
	bool bFound;
	LTVector vSlide, vDelta, vRemain, vHitNormal, vUp, vRight, vForward;
	LTVector *pDest, *pStart;
	WorldPoly *polys[0x200];

	pInfo->m_vHitNormal = vNormal;
	pInfo->m_fHitTime = 1.0f;
	pInfo->m_nIterations = 0;
	pInfo->m_nNormals = 0;
	pInfo->m_nGroundNormals = 0;
	pInfo->m_bGround = 0;

	ILTMath math;
	LTRotation rot = pInfo->m_pObj->m_Rotation;
	math.GetRotationVectors(rot, vRight, vUp, vForward);

	nPolies = 0;
	pStart = &pInfo->m_vStartPos;
	GetSpherePosTestPolys(pInfo, polys, &nPolies, 0x200, *pStart, 0);
	if (!SpherePosTestPolys(pStart, pInfo->m_fRadius, polys, nPolies))
	{
		pInfo->m_vDestPos = pInfo->m_vStartPos;
		return LTFALSE;
	}

	pDest = &pInfo->m_vDestPos;

	do
	{
		GetSphereCollideTestPolys(pInfo, polys, &nPolies, 0x200, LTNULL, LTNULL, 0);
		if (!SweptSphereToPolys(pStart, pDest, pInfo->m_fRadius, polys, nPolies, &vNormal, &vHitNormal,
			&fTime, &bWall))
			break;

		if (!(vNormal.MagSqr() > 0.001f))
		{
			pInfo->m_vDestPos = pInfo->m_vStartPos;
			return LTFALSE;
		}

		vDelta = *pDest - *pStart;
		vRemain = vDelta * (1.0f - fTime);
		fDot = vNormal.Dot(vRemain);
		vSlide = vNormal * fDot;

		if (bWall && fTime <= pInfo->m_fHitTime)
		{
			pInfo->m_fHitTime = fTime;
			pInfo->m_vHitNormal = vHitNormal;
		}

		// Is it a crease (a plane facing away from one we already slid along)?
		bFound = LTFALSE;
		for (i = 0; i < pInfo->m_nNormals && !bFound; i++)
			bFound = vNormal.Dot(pInfo->m_Normals[i]) < 0.0f;

		if (pInfo->m_nNormals < 10)
		{
			pInfo->m_Normals[pInfo->m_nNormals] = vNormal;
			pInfo->m_nNormals++;
		}

		if (pInfo->m_nIterations >= 10)
		{
			pInfo->m_vDestPos = pInfo->m_vStartPos;
			return LTFALSE;
		}

		if (!bFound)
			*pDest -= vSlide * 1.02f;
		else
			*pDest -= vRemain * 1.02f;

		if (bWall && vUp.Dot(vNormal) < 0.99f)
		{
			pInfo->m_GroundNormals[pInfo->m_nGroundNormals] = vNormal;
			pInfo->m_bGround = 1;
			pInfo->m_nGroundNormals++;
		}

		pInfo->m_nIterations++;
	} while (1);

	if (pInfo->m_pObj->m_Flags2 & FLAG2_ORIENTMOVEMENT)
		OrientMovement(pInfo);
	return 0 < pInfo->m_nIterations;
}

// Finds the world polygons the sphere moving from pStart to pEnd could touch.
// FUNCTION: LITHTECH 0x0041a1b0
void GetSphereCollideTestPolys(SphereMoveInfo *pInfo, WorldPoly **pPolies, int *pnPolies, int nMaxPolies,
	LTVector *pStart, LTVector *pEnd, SphereHitPoly *pHits)
{
	LTVector vStart, vEnd, vMid, vPos, vDelta;
	float fRadius, fDist1, fDist2;
	int i, nStackBytes;
	uint16 iNode;
	uint16 *pStackPos;

	if (pPolies)
	{
		*pnPolies = 0;

		if (!pStart)
			pStart = (LTVector*)pInfo->m_pState->m_pStartPos;
		vStart = *pStart;

		if (!pEnd)
			pEnd = &pInfo->m_pState->m_vDestPos;
		vEnd = *pEnd;

		vDelta = vEnd - *pStart;
		vMid = vStart + vDelta * 0.5f;
		fRadius = vDelta.Mag() + pInfo->m_fRadius + 5.0f;

		for (i = pInfo->m_pObjects->m_nObjects - 1; i >= 0; i--)
		{
			LTObject *pObj = pInfo->m_pObjects->m_Objects[i].m_pObject;

			if (pObj->m_ObjectType == OT_WORLDMODEL || pObj->m_ObjectType == OT_CONTAINER)
			{
				WorldBsp *pBsp = ((WorldModelInstance*)pObj)->m_pValidBsp;
				PBlock *pBlock = (PBlock*)((WorldModelInstance*)pObj)->IsPointInside(&vStart);
				if (pBlock)
				{
					Node *pNodes;

					vPos = vStart;
					iNode = pBlock->m_iRoot;
					pStackPos = g_PBlockStack;
					pNodes = pBsp->GetNodes();
					nStackBytes = 0;
					do
					{
						PBlockNode *pBN;
						Node *pNode;

						if ((nStackBytes & ~1) >= PBLOCK_STACK_SIZE * 2)
						{
							dsi_ConsolePrint("GetSphereCollideTestPolys!!  World caused stack overflow!");
							break;
						}

						if (iNode == 0xfffe || iNode == 0xffff)
						{
							if (pStackPos == g_PBlockStack)
								break;
							iNode = *--pStackPos;
							nStackBytes -= 2;
						}

						pBN = (PBlockNode*)pBlock->m_pNodes + iNode;
						if (pBN->m_iNode == 0xffff)
						{
							iNode = pBN->m_Sides[FrontSide];
						}
						else
						{
							pNode = pNodes + pBN->m_iNode;

							fDist1 = pNode->GetPlane()->DistTo(vPos);
							fDist2 = pNode->GetPlane()->DistTo(vEnd);
							if (fDist1 > pInfo->m_fRadius && fDist2 > pInfo->m_fRadius)
							{
								iNode = pBN->m_Sides[FrontSide];
							}
							else if (fDist1 < -pInfo->m_fRadius && fDist2 < -pInfo->m_fRadius)
							{
								iNode = pBN->m_Sides[BackSide];
							}
							else
							{
								if (((Surface*)pNode->m_pPoly->m_pSurface)->m_Flags & SURF_SOLID)
								{
									LTVector vDiff = pNode->m_pPoly->m_Center - vMid;
									float fR = fRadius + pNode->m_pPoly->m_Radius;
									if (vDiff.MagSqr() <= fR * fR)
									{
										if (*pnPolies >= nMaxPolies)
										{
											dsi_ConsolePrint("GetSphereCollideTestPolys!!  World caused max collision polys!");
											break;
										}

										pPolies[*pnPolies] = pNode->m_pPoly;
										if (pHits)
										{
											pHits[*pnPolies].m_pObject = pObj;
											pHits[*pnPolies].m_pNode = pNode;
										}
										(*pnPolies)++;
									}
								}

								{
									int bFront = fDist1 > -0.001f;
									uint16 iFar = pBN->m_Sides[!bFront];
									if (iFar != 0xfffe && iFar != 0xffff)
									{
										*pStackPos++ = iFar;
										nStackBytes += 2;
									}
									iNode = pBN->m_Sides[bFront];
								}
							}
						}
					} while (1);
				}
			}
		}
	}
}


// Finds the world polygons within the sphere's radius of a position.
// FUNCTION: LITHTECH 0x0041a570
void GetSpherePosTestPolys(SphereMoveInfo *pInfo, WorldPoly **pPolies, int *pnPolies, int nMaxPolies,
	LTVector vPos, SphereHitPoly *pHits)
{
	float fRadius, fDist;
	int i, nStackBytes;
	uint16 iNode;
	uint16 *pStackPos;

	if (pPolies)
	{
		*pnPolies = 0;

		fRadius = pInfo->m_fRadius + 0.1f;
		for (i = pInfo->m_pObjects->m_nObjects - 1; i >= 0; i--)
		{
			LTObject *pObj = pInfo->m_pObjects->m_Objects[i].m_pObject;

			if (pObj->m_ObjectType == OT_WORLDMODEL || pObj->m_ObjectType == OT_CONTAINER)
			{
				WorldBsp *pBsp = ((WorldModelInstance*)pObj)->m_pValidBsp;
				PBlock *pBlock = (PBlock*)((WorldModelInstance*)pObj)->IsPointInside(&vPos);
				if (pBlock)
				{
					Node *pNodes;

					iNode = pBlock->m_iRoot;
					pStackPos = g_PBlockStack;
					pNodes = pBsp->GetNodes();
					nStackBytes = 0;
					do
					{
						PBlockNode *pBN;
						Node *pNode;

						if ((nStackBytes & ~1) >= PBLOCK_STACK_SIZE * 2)
						{
							dsi_ConsolePrint("GetSpherePosTestPolys!!  World caused stack overflow!");
							break;
						}

						if (iNode == 0xfffe || iNode == 0xffff)
						{
							if (pStackPos == g_PBlockStack)
								break;
							iNode = *--pStackPos;
							nStackBytes -= 2;
						}

						pBN = (PBlockNode*)pBlock->m_pNodes + iNode;
						if (pBN->m_iNode == 0xffff)
						{
							iNode = pBN->m_Sides[FrontSide];
						}
						else
						{
							pNode = pNodes + pBN->m_iNode;

							fDist = pNode->GetPlane()->DistTo(vPos);
							if (fDist > fRadius)
							{
								iNode = pBN->m_Sides[FrontSide];
							}
							else if (fDist < -0.001f)
							{
								iNode = pBN->m_Sides[BackSide];
							}
							else
							{
								if (((Surface*)pNode->m_pPoly->m_pSurface)->m_Flags & SURF_SOLID)
								{
									LTVector vDiff = pNode->m_pPoly->m_Center - vPos;
									float fR = fRadius + pNode->m_pPoly->m_Radius;
									if (vDiff.MagSqr() <= fR * fR)
									{
										if (*pnPolies >= nMaxPolies)
										{
											dsi_ConsolePrint("GetSpherePosTestPolys!!  World caused max collision polys!");
											break;
										}

										pPolies[*pnPolies] = pNode->m_pPoly;
										if (pHits)
										{
											pHits[*pnPolies].m_pObject = pObj;
											pHits[*pnPolies].m_pNode = pNode;
										}
										(*pnPolies)++;
									}
								}

								{
									int bFront = fDist >= -0.001f;
									uint16 iFar = pBN->m_Sides[!bFront];
									if (iFar != 0xfffe && iFar != 0xffff)
									{
										*pStackPos++ = iFar;
										nStackBytes += 2;
									}
									iNode = pBN->m_Sides[bFront];
								}
							}
						}
					} while (1);
				}
			}
		}
	}
}

// FLAG2_ORIENTMOVEMENT: follows the edge of the BSP, rotating the object to lie on it.
// Not matching: transcribed from the disassembly, the vector expressions are untuned (the original
// builds most of them with out-of-line constructor and operator calls).
// STUB: LITHTECH 0x0041a7f0
void OrientMovement(SphereMoveInfo *pInfo)
{
	ILTMath math;
	LTRotation rot;
	LTVector vRight, vUp, vForward;
	LTVector vDelta, vStep, vPos, vP0, vOffset, vNormal, vHitNormal, vAvg;
	LTVector *pDest, *pStart;
	float fTime, fStep, fLen;
	int nPolies, bWall, bHit, i, nSteps;

	rot = pInfo->m_pObj->m_Rotation;
	math.GetRotationVectors(rot, vRight, vUp, vForward);

	pStart = &pInfo->m_vStartPos;
	pDest = &pInfo->m_vDestPos;
	bHit = 0;
	nPolies = 0;

	vDelta = *pDest - *pStart;
	nSteps = (int)(vDelta.Mag() / (pInfo->m_fRadius * 0.99)) + 2;
	fStep = 1.0f / (float)(nSteps - 1);
	vStep = vDelta * fStep;

	for (i = 0; i < nSteps; i++)
	{
		vPos = *pStart + vStep * (float)i;
		vP0 = vPos - vUp * (pInfo->m_fRadius + pInfo->m_fRadius);

		GetSphereCollideTestPolys(pInfo, g_SphereTestPolys, &nPolies, 0x200, &vPos, &vP0, 0);
		if (SweptSphereToPolys(&vPos, &vP0, pInfo->m_fRadius, g_SphereTestPolys, nPolies, &vNormal,
			&vHitNormal, &fTime, &bWall) && bWall && vNormal.Dot(vUp) > 0.99)
		{
			SweptSphereOrient(&vNormal, pInfo->m_pObj);

			rot = pInfo->m_pObj->m_Rotation;
			math.GetRotationVectors(rot, vRight, vUp, vForward);

			fTime -= 0.1f;
			if (fTime < 0.0f)
				fTime = 0.0f;

			*pDest = vPos + (vP0 - vPos) * fTime;
			bHit = 1;
			break;
		}
	}

	if (pInfo->m_nIterations && !pInfo->m_bGround && !bHit)
	{
		if (pInfo->m_fHitTime < 1.0f)
			SweptSphereOrient(&pInfo->m_vHitNormal, pInfo->m_pObj);
		return;
	}

	if (!pInfo->m_bGround)
	{
		fStep = pInfo->m_fRadius * 0.2f;
		vOffset = vUp * fStep;
		vPos = *pDest + vOffset;
		vP0 = *pDest - vOffset;

		GetSphereCollideTestPolys(pInfo, g_SphereTestPolys, &nPolies, 0x200, &vPos, &vP0, 0);
		if (!SweptSphereToPolys(&vPos, &vP0, pInfo->m_fRadius, g_SphereTestPolys, nPolies, &vNormal,
			&vHitNormal, &fTime, &bWall))
		{
			*pDest = vP0;
			return;
		}

		if (fTime < 1.0f && vNormal.Dot(vUp) > 0.99)
			SweptSphereOrient(&vNormal, pInfo->m_pObj);
		return;
	}

	// Stand on the average of the ground normals.
	vAvg.Init(0.0f, 0.0f, 0.0f);
	for (i = 0; i < pInfo->m_nGroundNormals; i++)
		vAvg += pInfo->m_GroundNormals[i];
	vAvg *= 1.0f / (float)pInfo->m_nGroundNormals;
	fLen = vAvg.Mag();
	if (fLen != 0.0f)
		vAvg *= 1.0f / fLen;
	SweptSphereOrient(&vAvg, pInfo->m_pObj);
	*pDest += vAvg * 2.0f;
}


LTBOOL SimpleBoxBSPIntersect(Node *pRoot, PhysicsSphere *pSphere);

// Does this box intersect this BSP tree?
// Close: the original runs out of inline budget earlier (the last two box points use the
// out-of-line Init).  It matches byte for byte with about 50 units of
// code-free inline ballast (inline functions of 20, 20 and 3 `if(0) x = 0;` statements) after
// g_BoxOffset.Init, so the original has an inline call (or several) there that isn't known.
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


// Does a point collision (FLAG_POINTCOLLIDE): finds what the movement hits and stands on it.
// FUNCTION: LITHTECH 0x0041b380
void DoPointCollision(CollideRequest *pRequest, CollideInfo *pInfo)
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


static void SetupMoveBox(LTVector *pMin, LTVector *pMax);

// ------------------------------------------------------------------ //
// Setting up the swept box.
// ------------------------------------------------------------------ //

// The world model's PBlocks the box touches, and the one SetupBox works in.
// GLOBAL: LITHTECH 0x004e0498
static PBlock *g_PBlocks[0x200];
// GLOBAL: LITHTECH 0x004e0c98
static PBlock *g_pCurBlock;

// Finds the PBlocks of the world model the swept box touches.
// FUNCTION: LITHTECH 0x0041b4d0
static void GetBoxPBlocks(PBlock **ppBlocks, int *pnBlocks, int nMaxBlocks)
{
	LTVector vMin, vMax, vBlockSize, vOrigin, vA, vB, vLocalMin, vLocalMax, vStart, vCur;
	PBlockTable *pTable;
	WorldModelInstance *pWM;
	PBlock *pBlock;
	LTBOOL bDone;

	SetupMoveBox(&vMin, &vMax);

	pTable = g_pCurRequest->m_pWorld->GetPBlockTable();
	vBlockSize = pTable->m_BlockSize;
	vOrigin = *g_pCurRequest->m_pWorld->GetPBlockOrigin();

	// Into the world model's space.
	pWM = (WorldModelInstance*)g_pCurRequest->m_pWorldObj;
	MatVMul_H(&vA, &pWM->m_BackTransform, &vMin);
	MatVMul_H(&vB, &pWM->m_BackTransform, &vMax);
	VEC_MIN(vLocalMin, vA, vB);
	VEC_MAX(vLocalMax, vA, vB);

	vStart.x = (float)(int)((vLocalMin.x - vOrigin.x) / vBlockSize.x) * vBlockSize.x + vOrigin.x;
	vStart.y = (float)(int)((vLocalMin.y - vOrigin.y) / vBlockSize.y) * vBlockSize.y + vOrigin.y;
	vStart.z = (float)(int)((vLocalMin.z - vOrigin.z) / vBlockSize.z) * vBlockSize.z + vOrigin.z;

	*pnBlocks = 0;
	bDone = LTFALSE;
	for (vCur.y = vStart.y; vCur.y <= vLocalMax.y && !bDone; vCur.y += vBlockSize.y)
	{
		for (vCur.x = vStart.x; vCur.x <= vLocalMax.x && !bDone; vCur.x += vBlockSize.x)
		{
			for (vCur.z = vStart.z; vCur.z <= vLocalMax.z && !bDone; vCur.z += vBlockSize.z)
			{
				pBlock = (PBlock*)g_pCurRequest->m_pWorld->VSlot4(vCur);
				*ppBlocks = pBlock;
				if (pBlock)
				{
					int nBlocks = ++(*pnBlocks);
					ppBlocks++;
					if (nBlocks >= nMaxBlocks)
						bDone = LTTRUE;
				}
			}
		}
	}

	if (*pnBlocks == 0)
	{
		pBlock = (PBlock*)g_pCurRequest->m_pWorld->VSlot4(vOrigin);
		*ppBlocks = pBlock;
		if (pBlock)
			(*pnBlocks)++;
	}
}


// Sets up the box around the whole movement: its min and max, the sphere around it, the points
// of the top and bottom faces and the box planes' distances.
// Not matching: the original releases its frame in the middle and finishes below esp (the function is
// noncontiguous in Ghidra) and keeps the sums on the FPU stack.
// STUB: LITHTECH 0x0041b930
static void SetupMoveBox(LTVector *pMin, LTVector *pMax)
{
	VEC_MIN(*pMin, g_P0, g_P1);
	VEC_MAX(*pMax, g_P0, g_P1);

	*pMax += g_BoxOffset + g_pCurRequest->m_Dims;
	*pMin += g_BoxOffset - g_pCurRequest->m_Dims;

	g_BoxFindCenter = *pMax - *pMin;
	g_BoxFindCenter *= 0.5f;
	g_BoxFindRadius = g_BoxFindCenter.Mag() + 1.0f;
	g_BoxFindCenter += *pMin;

	g_MovePts[0][0].Init(pMin->x, pMax->y, pMin->z);
	g_MovePts[0][1].Init(pMin->x, pMax->y, pMax->z);
	g_MovePts[0][2].Init(pMax->x, pMax->y, pMax->z);
	g_MovePts[0][3].Init(pMax->x, pMax->y, pMin->z);
	g_MovePts[0][4] = g_MovePts[0][0];
	g_MovePts[0][5] = g_MovePts[0][1];
	g_MovePts[0][6] = g_MovePts[0][2];
	g_MovePts[0][7] = g_MovePts[0][3];

	g_MovePts[1][0].Init(pMin->x, pMin->y, pMin->z);
	g_MovePts[1][1].Init(pMin->x, pMin->y, pMax->z);
	g_MovePts[1][2].Init(pMax->x, pMin->y, pMax->z);
	g_MovePts[1][3].Init(pMax->x, pMin->y, pMin->z);
	g_MovePts[1][4] = g_MovePts[1][0];
	g_MovePts[1][5] = g_MovePts[1][1];
	g_MovePts[1][6] = g_MovePts[1][2];
	g_MovePts[1][7] = g_MovePts[1][3];

	g_BoxFindPlanes[0].m_Dist = pMin->x;
	g_BoxFindPlanes[1].m_Dist = -pMax->x;
	g_BoxFindPlanes[2].m_Dist = pMin->y;
	g_BoxFindPlanes[3].m_Dist = -pMax->y;
	g_BoxFindPlanes[4].m_Dist = pMin->z;
	g_BoxFindPlanes[5].m_Dist = -pMax->z;
}

// ------------------------------------------------------------------ //
// CollideWithWorld.
// ------------------------------------------------------------------ //

#define MAX_PHYSICS_ITERATIONS	40

// Reset by CollideWithWorld (the profiling counters of the functions below).
extern uint32 g_Ticks_PolyTouchesBox;
// GLOBAL: LITHTECH 0x004e24a8
extern uint32 g_nPolyTouchesBoxCalls;
// GLOBAL: LITHTECH 0x004e2f70
uint32 g_Ticks_CollideBoxWithTree;
// GLOBAL: LITHTECH 0x004e2458
uint32 g_nCollideBoxNodes;

LTBOOL CollideBox(uint16 iRoot, CollideInfo *pInfo);
LTBOOL CollideCylinderWithTree(uint16 iRoot, CollideInfo *pInfo, LTBOOL bUnused);
LTBOOL StairStepOld(uint16 iRoot, CollideInfo *pInfo);
void StairStep(uint16 iRoot, CollideInfo *pInfo, LTBOOL *pbHitNonStep);

// Collides the axis-aligned box with the world.  If you specify pObj, it'll calculate collision
// responses and send them to the object.
// Not matching: the saved g_pCurRequest/g_pCurInfo stay in eax/edx in the original (no spill before
// the request copy), each return has its own epilogue and the frame is 4 bytes smaller.
// STUB: LITHTECH 0x0041bdd0
void CollideWithWorld(CollideRequest &request, CollideInfo *pInfo)
{
	CollideRequest curRequest;
	CollideRequest *pOldRequest;
	CollideInfo *pOldInfo;
	LTBOOL bHitStairStep, bHitNonStep;
	ILTPhysics *pPhysics;
	LTVector vSavedP0;
	int nBlocks, iBlock, i;

	pOldRequest = g_pCurRequest;
	pOldInfo = g_pCurInfo;

	g_nPolyTouchesBoxCalls = 0;
	g_Ticks_SetupBox = 0;
	g_Ticks_PolyTouchesBox = 0;
	g_Ticks_AddPushPlane = 0;
	g_Ticks_CollideBoxWithTree = 0;
	g_nClassifyGenericCalls = 0;
	g_nCollideBoxNodes = 0;

	curRequest = request;
	g_pCurRequest = &curRequest;
	g_pCurInfo = pInfo;

	pInfo->m_pStandingOn = LTNULL;
	pInfo->m_vForce.Init(0.0f, 0.0f, 0.0f);
	pInfo->m_VelOffset.Init(0.0f, 0.0f, 0.0f);
	pInfo->m_nHits = 0;

	bHitStairStep = LTFALSE;
	pInfo->m_FinalPos = request.m_OriginalPos;

	// Do point collisions?
	if (request.m_pObject->m_Flags & FLAG_POINTCOLLIDE)
	{
		DoPointCollision(&request, pInfo);
		g_pCurRequest = pOldRequest;
		g_pCurInfo = pOldInfo;
		return;
	}

	g_nPushPlanes = 0;
	g_P0 = request.m_OriginalPos;
	g_P1 = request.m_NewPos;
	g_BoxOffset.Init(0.0f, 0.0f, 0.0f);

	// Stairstep
	if (request.m_bStairStep)
	{
		g_pCurBlock = (PBlock*)((WorldModelInstance*)g_pCurRequest->m_pWorldObj)->IsPointInside(&g_P0);

		if (g_CV_NewStairStep)
		{
			float fStairHeight, fHalfStair;

			pPhysics = request.m_pAbstract->GetPhysics();
			pPhysics->GetStairHeight(fStairHeight);
			fStairHeight = LTMIN(request.m_Dims.y, LTMAX(fStairHeight, request.m_Dims.y * 0.5f));
			fHalfStair = fStairHeight * 0.5f;

			g_BoxOffset.y = -(request.m_Dims.y - fHalfStair);
			g_pCurRequest->m_Dims = request.m_Dims;
			g_pCurRequest->m_Dims.y = fHalfStair;

			g_BoxOffset.y += *(float*)request.m_pRestart * 0.5f;
			g_pCurRequest->m_Dims.y -= *(float*)request.m_pRestart * 0.5f;

			bHitNonStep = LTFALSE;
			vSavedP0 = g_P0;
			if (g_pCurRequest->m_Dims.y > 0.0f)
			{
				SetupMoveBox(&g_BoxMin, &g_BoxMax);
				StairStep(g_pCurBlock->m_iRoot, pInfo, &bHitNonStep);
			}

			*(float*)request.m_pRestart += g_P0.y - vSavedP0.y;
			g_P0 = vSavedP0;
			g_BoxOffset.y = *(float*)request.m_pRestart * 0.5f;
			g_pCurRequest->m_Dims = request.m_Dims;

			if (bHitNonStep)
			{
				g_pCurRequest->m_Dims.y -= g_BoxOffset.y;
			}
			else
			{
				g_BoxOffset.y = fHalfStair;
				g_pCurRequest->m_Dims = request.m_Dims;
				g_pCurRequest->m_Dims.y -= fHalfStair;
			}
		}
		else
		{
			float fStairHeight;
			uint32 nPreStepHits;

			// Get the stair height
			pPhysics = request.m_pAbstract->GetPhysics();
			pPhysics->GetStairHeight(fStairHeight);
			if (fStairHeight < 0.0f)
				fStairHeight = request.m_Dims.y * 0.5f;

			// Adjust by 1/2 unit so it's an inclusive height
			fStairHeight += 0.5f;

			// Use half the stair height since that's the amount the adjustments are made by
			fStairHeight *= 0.5f;

			// Adjust the bounding box..
			g_BoxOffset.y = -(request.m_Dims.y - fStairHeight);
			g_pCurRequest->m_Dims = request.m_Dims;
			g_pCurRequest->m_Dims.y = fStairHeight;

			nPreStepHits = pInfo->m_nHits;

			// Check for stair step
			SetupMoveBox(&g_BoxMin, &g_BoxMax);
			bHitNonStep = StairStepOld(g_pCurBlock->m_iRoot, pInfo);

			g_pCurRequest->m_Dims = request.m_Dims;

			if (bHitNonStep)
			{
				// If we hit a poly w/ SURF_NOTASTEP set, then do a full height collision later
				g_BoxOffset.y = 0.0f;
			}
			else
			{
				g_BoxOffset.y = fStairHeight;
				g_pCurRequest->m_Dims.y -= fStairHeight;
			}

			// Remember that we hit the stairs, but don't count it as a collision
			if (pInfo->m_nHits != nPreStepHits && !g_pCurRequest->m_Unknown44)
				bHitStairStep = LTTRUE;
			else
				bHitStairStep = LTFALSE;
			pInfo->m_nHits = nPreStepHits;
		}
	}

	GetBoxPBlocks(g_PBlocks, &nBlocks, 0x200);
	if (nBlocks == 0)
	{
		if (g_DebugLevel > 0)
			dsi_ConsolePrint("Did not intersect with WorldModel '%s'", g_pCurRequest->m_pWorld->m_WorldName);
		g_pCurRequest = pOldRequest;
		g_pCurInfo = pOldInfo;
		return;
	}

	g_BoxRadius = g_pCurRequest->m_Dims.Mag() + 0.01f;

	// Loop around, testing for collisions with anything.
	for (iBlock = 0; iBlock < nBlocks; iBlock++)
	{
		g_pCurBlock = g_PBlocks[iBlock];

		for (i = 0; i < MAX_PHYSICS_ITERATIONS; i++)
		{
			uint32 nHits = pInfo->m_nHits;

			if (SetupBox())
			{
				if (!g_CV_NewCollision && (g_pCurRequest->m_pObject->m_Flags2 & FLAG2_CYLINDERPHYSICS))
				{
					CollideCylinderWithTree(g_pCurBlock->m_iRoot, pInfo, i > 0);
					nHits = pInfo->m_nHits;
				}
				else
				{
					CollideBox(g_pCurBlock->m_iRoot, pInfo);
				}
			}

			if (nHits == pInfo->m_nHits)
				break;
		}

		if (i == MAX_PHYSICS_ITERATIONS)
		{
			// Got into a potentially recursive situation and kept hitting stuff.
			// Just put them back at their original position.
			if (g_DebugLevel > 0)
			{
				dsi_ConsolePrint("Error: physics recursed infinitely on a %s",
					g_pCurRequest->m_pAbstract->GetObjectClassName(request.m_pObject));
			}

			g_P1 = g_P0;
		}
	}

	pInfo->m_FinalPos = g_P1;

	g_pCurRequest = pOldRequest;
	g_pCurInfo = pOldInfo;

	// If there was a stair collision, and nothing else, increment the hit count
	if (!g_CV_NewStairStep && !pInfo->m_nHits && bHitStairStep)
		pInfo->m_nHits = 1;
}

// Collides the box with the tree.  Called twice when the first pass hit something: the second
// pass runs on the box SetupBox rebuilt from the adjusted movement (0x0041c460).
LTBOOL ClipBoxIntoTree(uint16 iRoot, CollideInfo *pInfo, LTBOOL bSecondPass, LTBOOL *pbHit);

// FUNCTION: LITHTECH 0x0041c400
LTBOOL CollideBox(uint16 iRoot, CollideInfo *pInfo)
{
	LTBOOL bHit;

	bHit = LTFALSE;
	if (!ClipBoxIntoTree(iRoot, pInfo, LTFALSE, &bHit))
		return LTFALSE;

	if (!bHit)
		return LTTRUE;

	if (!SetupBox())
		return LTFALSE;

	return ClipBoxIntoTree(iRoot, pInfo, LTTRUE, &bHit);
}


static void PushBoxOutOfPlanes();


// Adds the movement to the position, making sure the movement doesn't get lost to
// floating point precision.
// This is an inline function in the original (ClipBoxIntoTree's first call and both of StairStep's
// are expanded; this out-of-line copy sits after ClipBoxIntoTree).  Not inline here because that
// leaves no out-of-line copy until the callers match.  `*pOut == vPos` (LTVector::operator==, which
// takes a const reference) and vPos.Mag() give the original's x87 operand order (README, wave 6).
// FUNCTION: LITHTECH 0x0041d640
void AddMovement(LTVector *pOut, LTVector vPos, LTVector vMove)
{
	float fMoveMag, fScale;

	*pOut = vPos;

	fMoveMag = vMove.Mag();
	if (!(fMoveMag < FLT_EPSILON))
	{
		*pOut += vMove;

		// If the movement didn't change the position, make it big enough to.
		if (*pOut == vPos)
		{
			fScale = (vPos.Mag() * FLT_EPSILON) / fMoveMag;
			vMove *= fScale;
			*pOut = vPos + vMove;
		}
	}
}


// ------------------------------------------------------------------ //
// Clipping the box into the world BSP.
// ------------------------------------------------------------------ //

// Collides the box with the tree.  The first pass (bSecondPass FALSE) only flags a hit (*pbHit);
// the second runs on the box SetupBox rebuilt and pushes the box out of what it hits.
// This is Talon's older ClipBoxIntoTree2 (Jupiter): the BSP is a PBlock's mini tree of node indices.
// Not matching: it inlines more of the LTVector operators than the original, which calls the
// out-of-line copies at the end of this unit (its inline budget is spent earlier) and expands
// AddMovement only in the intersect case.
// STUB: LITHTECH 0x0041c460
LTBOOL ClipBoxIntoTree(uint16 iRoot, CollideInfo *pInfo, LTBOOL bSecondPass, LTBOOL *pbHit)
{
	CountAdder cntAdd(&g_Ticks_CollideBoxWithTree);
	ClassifyPoints cp[2];
	int bCalcMinMax;
	LTVector vBasePt, vClipMin, vClipMax, vDir, vMove, vBack, vPushDir, vNormal;
	Node *pNodes, *pRoot;
	LTPlane *pPlane;
	PBlockNode *pBN;
	uint16 *pStackPos;
	float fDot, fPush, fT, fMinDist, fD, fDenom;
	int state1, state2, iAxis, iBest;
	LTBOOL bSlide;

	cp[0].m_pPoints = g_MovePts[0];
	cp[0].m_pSphere = &g_StartSphere;
	cp[0].m_bCalcMinMax = &bCalcMinMax;
	cp[1].m_pPoints = g_MovePts[1];
	cp[1].m_pSphere = &g_EndSphere;
	cp[1].m_bCalcMinMax = &bCalcMinMax;

	VEC_ADD(vBasePt, g_BoxOffset, g_P0);
	cp[0].m_MinPos = vBasePt - g_pCurRequest->m_Dims;
	cp[0].m_MaxPos = vBasePt + g_pCurRequest->m_Dims;
	VEC_ADD(vBasePt, g_BoxOffset, g_P1);
	cp[1].m_MinPos = vBasePt - g_pCurRequest->m_Dims;
	cp[1].m_MaxPos = vBasePt + g_pCurRequest->m_Dims;

	pNodes = g_pCurRequest->m_pWorld->GetNodes();
	pStackPos = g_PBlockStack;
	for (;;)
	{
		if (iRoot == 0xfffe || iRoot == 0xffff)
		{
			if (pStackPos == g_PBlockStack)
				return LTTRUE;

			iRoot = *--pStackPos;
		}

		g_nCollideBoxNodes++;
		pBN = (PBlockNode*)g_pCurBlock->m_pNodes + iRoot;
		pRoot = pNodes + pBN->m_iNode;
		pPlane = pRoot->GetPlane();

		// Do the fast test on the whole movement area.
		fDot = pPlane->DistTo(g_BoxFindCenter);
		if (fDot > g_BoxFindRadius)
		{
			iRoot = pBN->m_Sides[FrontSide];
			continue;
		}
		else if (fDot < -g_BoxFindRadius)
		{
			iRoot = pBN->m_Sides[BackSide];
			continue;
		}

		// Classify where the starting and ending areas are.
		bCalcMinMax = LTTRUE;
		cp[0].m_pPlane = cp[1].m_pPlane = pPlane;
		state1 = g_ClassifyFns[pRoot->m_PlaneType](&cp[0]);
		state2 = g_ClassifyFns[pRoot->m_PlaneType](&cp[1]);

		if (state1 == state2)
		{
			if (state1 == Intersect)
			{
				// Have we run into it from the side?
				vDir = g_P1 - g_P0;
				vDir.Norm();
				fDot = vDir.Dot(pPlane->m_Normal);
				if (fDot < 0.0001f && PolyTouchesBox(pRoot->m_pPoly, &vClipMin, &vClipMax))
				{
					*pbHit = LTTRUE;
					if (bSecondPass)
					{
						// Find the axis that pushes the box out of the polygon the least.
						iBest = -1;
						fMinDist = FLT_MAX;
						for (iAxis = 0; iAxis < 3; iAxis++)
						{
							if (vDir[iAxis] <= 0.0001f)
							{
								if (vDir[iAxis] < -0.0001f)
								{
									fD = vClipMax[iAxis] - (&g_BoxMin.x)[iAxis];
									if (fD < fMinDist)
									{
										fPush = fD;
										iBest = iAxis;
										fMinDist = fD;
									}
								}
							}
							else
							{
								fD = (&g_BoxMax.x)[iAxis] - vClipMin[iAxis];
								if (fD < fMinDist)
								{
									fPush = -fD;
									iBest = iAxis;
									fMinDist = fD;
								}
							}
						}

						if (iBest == -1)
							return LTFALSE;

						vPushDir.Init(0.0f, 0.0f, 0.0f);
						vPushDir[iBest] = 1.0f;

						if (vPushDir.y > 0.01f)
						{
							if (!g_pCurInfo->m_pStandingOn ||
								g_pCurInfo->m_pStandingOn->GetPlane()->m_Normal.y < pPlane->m_Normal.y)
							{
								g_pCurInfo->m_pStandingOn = pRoot;
							}
						}

						DoObjectCollisionResponse(g_pCurRequest->m_pCollisionInfo, pInfo, g_pCurRequest->m_pObject,
							g_pCurRequest->m_pWorldObj, g_pCurRequest->m_pWorld, pRoot, &vPushDir);
						pInfo->m_nHits++;

						if (g_pCurRequest->m_bSlide)
						{
							vBack = vPushDir * fPush;
						}
						else
						{
							vMove = g_P1 - g_P0;
							fDot = vMove.x * vPushDir.x + vPushDir.z * vMove.z + vMove.y * vPushDir.y;
							if (fDot >= FLT_EPSILON || fDot <= -FLT_EPSILON)
								fT = -(fPush / fDot);
							else
								fT = 0.0f;
							fT = -fT;
							vBack = vMove * fT;
						}

						AddMovement(&g_P1, g_P1, vBack);

						if (!SetupBox())
							return LTFALSE;

						VEC_ADD(vBasePt, g_BoxOffset, g_P0);
						cp[0].m_MinPos = vBasePt - g_pCurRequest->m_Dims;
						cp[0].m_MaxPos = vBasePt + g_pCurRequest->m_Dims;
						VEC_ADD(vBasePt, g_BoxOffset, g_P1);
						cp[1].m_MinPos = vBasePt - g_pCurRequest->m_Dims;
						cp[1].m_MaxPos = vBasePt + g_pCurRequest->m_Dims;
					}
				}

				pPlane = pRoot->GetPlane();
				fDot = pPlane->DistTo(g_P0);
			}
			else
			{
				iRoot = pBN->m_Sides[state1];
				continue;
			}
		}
		else
		{
			if (state1 == FrontSide && PolyTouchesBox(pRoot->m_pPoly, LTNULL, LTNULL))
			{
				pPlane = pRoot->GetPlane();
				bSlide = g_pCurRequest->m_bSlide;
				vNormal = pPlane->m_Normal;
				if (bSlide && vNormal.y > 0.0f && vNormal.y < 0.7071f && g_pCurRequest->m_bStairStep)
				{
					vNormal.y = 0.0f;
					vNormal.Norm(1.0f);
				}

				if (vNormal.y > 0.01f && (!g_pCurInfo->m_pStandingOn ||
					g_pCurInfo->m_pStandingOn->GetPlane()->m_Normal.y < pPlane->m_Normal.y))
				{
					g_pCurInfo->m_pStandingOn = pRoot;
				}

				DoObjectCollisionResponse(g_pCurRequest->m_pCollisionInfo, pInfo, g_pCurRequest->m_pObject,
					g_pCurRequest->m_pWorldObj, g_pCurRequest->m_pWorld, pRoot, &vNormal);
				pInfo->m_nHits++;

				if (!bCalcMinMax)
					ReallyClassifyPointsGeneric(&cp[1]);

				if (!bSlide)
				{
					vMove = g_P1 - g_P0;
					fDenom = (cp[1].min - pPlane->m_Normal.Dot(vMove)) - cp[1].min;
					if (!(fDenom < FLT_EPSILON) || fDenom <= -FLT_EPSILON)
						vBack = vMove * (cp[1].min / fDenom);
					else
						vBack.Init(0.0f, 0.0f, 0.0f);
				}
				else
				{
					vBack = -pPlane->m_Normal * cp[1].min;
				}

				AddMovement(&g_P1, g_P1, vBack);
				AddPushPlane(pPlane, pPlane);
				if (SetupBox())
				{
					VEC_ADD(vBasePt, g_BoxOffset, g_P0);
					cp[0].m_MinPos = vBasePt - g_pCurRequest->m_Dims;
					cp[0].m_MaxPos = vBasePt + g_pCurRequest->m_Dims;
					VEC_ADD(vBasePt, g_BoxOffset, g_P1);
					cp[1].m_MinPos = vBasePt - g_pCurRequest->m_Dims;
					cp[1].m_MaxPos = vBasePt + g_pCurRequest->m_Dims;
					iRoot = pBN->m_Sides[FrontSide];
					continue;
				}
			}

			pPlane = pRoot->GetPlane();
			fDot = pPlane->m_Normal.Dot(g_P0) - pPlane->m_Dist;
		}
		// Go into the side the start is on, and remember the other.
		{
			int bFront = fDot > -0.001f;
			uint16 iFar = pBN->m_Sides[!bFront];
			if (iFar != 0xfffe && iFar != 0xffff)
				*pStackPos++ = iFar;
			iRoot = pBN->m_Sides[bFront];
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



// Pushes the box at the end of the movement out of all the planes it touches.
// 31 bytes differ: the loop's first x87 operand order (z, x, y in the original) and where the two
// counters are cleared.
// STUB: LITHTECH 0x0041d820
static void PushBoxOutOfPlanes()
{
	LTVector pos, *pDims, pts[NUM_BOX_POINTS], move;
	int i, j, nIterations;
	float minDist, dist, pushDist;
	LTPlane *pPlane;

	i = 0;
	nIterations = 0;
	VEC_ADD(pos, g_P1, g_BoxOffset);
	pDims = &g_pCurRequest->m_Dims;

	pts[0].Init(pos.x + pDims->x, pos.y + pDims->y, pos.z + pDims->z);
	pts[1].Init(pos.x - pDims->x, pos.y + pDims->y, pos.z + pDims->z);
	pts[2].Init(pos.x - pDims->x, pos.y + pDims->y, pos.z - pDims->z);
	pts[3].Init(pos.x + pDims->x, pos.y + pDims->y, pos.z - pDims->z);
	pts[4].Init(pos.x + pDims->x, pos.y - pDims->y, pos.z + pDims->z);
	pts[5].Init(pos.x - pDims->x, pos.y - pDims->y, pos.z + pDims->z);
	pts[6].Init(pos.x - pDims->x, pos.y - pDims->y, pos.z - pDims->z);
	pts[7].Init(pos.x + pDims->x, pos.y - pDims->y, pos.z - pDims->z);

	for (; i < g_nPushPlanes && nIterations < MAX_PUSH_ITERATIONS; i++)
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

// The step normal limits of the two stair steps (not folded into the code: VC6 keeps const floats).
// GLOBAL: LITHTECH 0x004d0804
static const float g_fStairMinNormalY = (float)0.1;
// GLOBAL: LITHTECH 0x004d0808
static const float g_fStairFlatNormalY = (float)0.7071;
// GLOBAL: LITHTECH 0x004d080c
static const float g_fStairOldMinNormalY = (float)0.1;
// GLOBAL: LITHTECH 0x004d0810
static const float g_fStairOldFlatNormalY = (float)0.7071;

// ------------------------------------------------------------------ //
// Cylinder and stair step collision.
// ------------------------------------------------------------------ //

// Collides the cylinder (the object's dims) moving from g_P0 to g_P1 with the world, a step at a
// time, backing it off what it hits.
// Not matching: the same size as the original (2112) but the frame is 4 bytes larger and the cylinder fields sit in
// other slots. The CountAdder is declared after the three leading assignments (nOldHitCount, nRetryHitCount,
// iRetryCollision), as the original constructs it after them. The original forms vFullStart/vFullEnd without by-value
// argument copies and stores each component through a stack temporary (neither the operator+ nor the VEC_ADD macro
// form reproduces that).
// STUB: LITHTECH 0x0041da50
LTBOOL CollideCylinderWithTree(uint16 iRoot, CollideInfo *pInfo, LTBOOL bUnused)
{
	CMovingCylinder theCylinder;
	LTVector vFullStart, vFullEnd, vDirection;
	Node *pNodes, *pRoot;
	PBlockNode *pBN;
	uint16 *pStackPos, iCur;
	uint32 nOldHitCount, nRetryHitCount;
	int iRetryCollision, state1, state2;
	float fVelocityLeft, fVelocityStep, fVelocitySegment;
	float fRadiusVelocity, fHeightVelocity, fRadiusRatio, fHeightRatio, fStepRadius, fStepHeight, fFullStep;
	float fDot;

	nOldHitCount = pInfo->m_nHits;
	nRetryHitCount = pInfo->m_nHits;
	iRetryCollision = 3;

	CountAdder cntAdd(&g_Ticks_CollideBoxWithTree);

	pNodes = g_pCurRequest->m_pWorld->GetNodes();

	// Disable cylinder stair stepping..  Maybe that code will come in handy some day...
	theCylinder.m_bStairStep = LTFALSE;
	// Set up the cylinder size
	theCylinder.m_fRadius = LTMIN(g_pCurRequest->m_Dims.x, g_pCurRequest->m_Dims.z);
	theCylinder.m_fHeight = g_pCurRequest->m_Dims.y;

	// No invalid cylinders!
	if (theCylinder.m_fRadius < 0.01f || theCylinder.m_fHeight < 0.01f)
		return LTTRUE;

	// Set up the iterative test parameters
	vFullStart = g_BoxOffset + g_P0;
	vFullEnd = g_BoxOffset + g_P1;
	vDirection = vFullEnd - vFullStart;
	fVelocityLeft = vDirection.Mag();
	if (fVelocityLeft == 0.0f)
		return LTTRUE;

	// Get the radius & height velocities
	fRadiusVelocity = (float)sqrt(vDirection.x * vDirection.x + vDirection.z * vDirection.z);
	fHeightVelocity = (float)fabs(vDirection.y);
	// Normalize the direction vector
	vDirection *= 1.0f / fVelocityLeft;
	// Decide whether or not we need to restrict the velocity
	if (fHeightVelocity > theCylinder.m_fHeight * 1.8f || fRadiusVelocity > theCylinder.m_fRadius * 0.9f)
	{
		// OK, which one needs the biggest restriction?
		fRadiusRatio = fRadiusVelocity / theCylinder.m_fRadius;
		fHeightRatio = fHeightVelocity / (theCylinder.m_fHeight * 2);

		// Moving forward/sideways faster?
		if (fRadiusRatio > fHeightRatio)
		{
			fStepRadius = theCylinder.m_fRadius;
			fStepHeight = (fStepRadius / fRadiusVelocity) * fHeightVelocity;
		}
		// Aah, moving vertically faster..
		else
		{
			fStepHeight = theCylinder.m_fHeight * 2;
			fStepRadius = (fStepHeight / fHeightVelocity) * fRadiusVelocity;
		}

		// Ok, figure out how long the steps are going to be..
		fFullStep = (float)sqrt(fStepRadius * fStepRadius + fStepHeight * fStepHeight);
		fVelocityStep = fFullStep * 0.75f;
		fVelocitySegment = fFullStep * 0.9f;
	}
	// Otherwise just use the whole velocity
	else
	{
		fVelocityStep = fVelocityLeft;
		fVelocitySegment = fVelocityLeft;
	}

	if (fVelocityStep > 0.001f)
	{
		while (fVelocityLeft > 0.0f)
		{
			if (nOldHitCount == pInfo->m_nHits)
			{
				// Get the next segment of the movement search
				if (fVelocityLeft < fVelocityStep)
					vFullEnd = vFullStart + (vDirection * fVelocityLeft);
				else
					vFullEnd = vFullStart + (vDirection * fVelocitySegment);

				// Set up the cylinder for this segment
				theCylinder.m_vStart = vFullStart;
				theCylinder.m_vEnd = vFullEnd;
				theCylinder.Recalc();
			}

			// Collide with the BSP for this segment
			pStackPos = g_PBlockStack;
			iCur = iRoot;
			for (;;)
			{
				if (iCur == 0xfffe || iCur == 0xffff)
				{
					if (pStackPos == g_PBlockStack)
						break;

					iCur = *--pStackPos;
				}

				g_nCollideBoxNodes++;
				pBN = (PBlockNode*)g_pCurBlock->m_pNodes + iCur;
				pRoot = pNodes + pBN->m_iNode;

				// Do the fast test on the whole movement area.
				fDot = pRoot->GetPlane()->DistTo(theCylinder.m_vMoveMid);
				if (fDot > theCylinder.m_fMoveSphere)
				{
					iCur = pBN->m_Sides[FrontSide];
					continue;
				}
				else if (fDot < -theCylinder.m_fMoveSphere)
				{
					iCur = pBN->m_Sides[BackSide];
					continue;
				}

				// Decide which side of the plane we're on
				state1 = theCylinder.GetPlaneSide(pRoot->GetPlane(), theCylinder.m_vStart, LTFALSE, LTTRUE);
				state2 = theCylinder.GetPlaneSide(pRoot->GetPlane(), theCylinder.m_vEnd, LTTRUE, state1 != FrontSide);

				// Do something based on the values.
				if (state1 == state2)
				{
					if (state1 == Intersect)
					{
						if (theCylinder.CollideWith(pRoot->m_pPoly, pRoot))
							pInfo->m_nHits++;

						state1 = pRoot->GetPlane()->DistTo(theCylinder.m_vStart) > -0.001f;
						if (pBN->m_Sides[!state1] != 0xfffe && pBN->m_Sides[!state1] != 0xffff)
							*pStackPos++ = pBN->m_Sides[!state1];
						iCur = pBN->m_Sides[state1];
					}
					else
					{
						iCur = pBN->m_Sides[state1];
					}
				}
				else
				{
					if (state1 == FrontSide && theCylinder.CollideWith(pRoot->m_pPoly, pRoot))
						pInfo->m_nHits++;

					state1 = pRoot->GetPlane()->DistTo(theCylinder.m_vStart) > -0.001f;
					if (pBN->m_Sides[!state1] != 0xfffe && pBN->m_Sides[!state1] != 0xffff)
						*pStackPos++ = pBN->m_Sides[!state1];
					iCur = pBN->m_Sides[state1];
				}
			}

			// Break out of the iterative search if there was a collision
			if (nOldHitCount != pInfo->m_nHits)
			{
				// Re-try the collision if necessary
				if (iRetryCollision && nRetryHitCount != pInfo->m_nHits)
				{
					iRetryCollision--;

					// Try to move to a non-collision position
					theCylinder.HandleCollision(theCylinder.m_pClosestNode, pInfo);

					// Remember how many hits there were..
					nRetryHitCount = pInfo->m_nHits;
					continue;
				}
				else
				{
					if (nRetryHitCount != pInfo->m_nHits)
						theCylinder.m_vEnd = g_BoxOffset + g_P0;
					break;
				}
			}

			// Move to the next segment of the search
			vFullStart = vFullStart + (vDirection * fVelocityStep);
			fVelocityLeft -= fVelocityStep;
		}

		g_P1 = theCylinder.m_vEnd - g_BoxOffset;
	}

	return LTTRUE;
}


// Stair stepping (g_CV_NewStairStep): finds a polygon that is a step (it isn't SURF_NOTASTEP and
// faces up), and raises the box on top of it.  *pbHitNonStep is set when it ran into a polygon that
// can't be stepped on.
// Not matching: AddMovement is an inline function in the original and is expanded here (the size is
// close once it is inline), the push limiting follows the Ghidra output only roughly.
// STUB: LITHTECH 0x0041e290
void StairStep(uint16 iRoot, CollideInfo *pInfo, LTBOOL *pbHitNonStep)
{
	Node *pNodes, *pRoot;
	PBlockNode *pBN;
	LTPlane *pPlane;
	uint16 *pStackPos;
	LTVector vDir, vN, vPush, vClipMin, vClipMax, vSavedVelOffset, vMoveBack, vFinal, vFinalDir, vNewP0;
	CollisionInfo *pCollisionInfo;
	float fDot, fLen, fMinDist, fDist, fInv, fDotMove;
	int iPt, iBest, i;

	pNodes = g_pCurRequest->m_pWorld->GetNodes();
	pStackPos = g_PBlockStack;
	for (;;)
	{
		for (;;)
		{
			while (iRoot == 0xfffe || iRoot == 0xffff)
			{
				if (pStackPos == g_PBlockStack)
					return;

				iRoot = *--pStackPos;
			}

			pBN = (PBlockNode*)g_pCurBlock->m_pNodes + iRoot;
			pRoot = pNodes + pBN->m_iNode;
			pPlane = pRoot->GetPlane();

			// Do the fast test on the whole movement area.
			fDot = pPlane->DistTo(g_BoxFindCenter);
			if (fDot > g_BoxFindRadius)
			{
				iRoot = pBN->m_Sides[FrontSide];
				continue;
			}
			if (fDot < -g_BoxFindRadius)
			{
				iRoot = pBN->m_Sides[BackSide];
				continue;
			}

			// Polygons that can't be stepped on.
			if (((Surface*)pRoot->m_pPoly->m_pSurface)->m_Flags & SURF_NOTASTEP)
			{
				if (pBN->m_Sides[BackSide] != 0xfffe && pBN->m_Sides[BackSide] != 0xffff)
					*pStackPos++ = pBN->m_Sides[BackSide];
				iRoot = pBN->m_Sides[FrontSide];
				*pbHitNonStep = LTTRUE;
				continue;
			}

			break;
		}

		if (pRoot->m_pPoly && g_fStairMinNormalY < pPlane->m_Normal.y)
		{
			vDir = g_P1 - g_P0;
			vDir.Norm();
			vN = pPlane->m_Normal;
			fDot = vN.x * vDir.x + vDir.y * vN.y + vN.z * vDir.z;
			if (fDot < 0.5f && PolyTouchesBox(pRoot->m_pPoly, &vClipMin, &vClipMax))
			{
				// How far is the lowest of the box's bottom points from the plane?
				vPush = pPlane->m_Normal;
				fMinDist = 5000.0f;
				iBest = 0;
				if (pPlane->m_Normal.y >= g_fStairFlatNormalY)
				{
					vPush.Init(0.0f, 1.0f, 0.0f);
					for (iPt = 0; iPt < 4; iPt++)
					{
						LTVector vPt = g_MovePts[1][iPt];
						fDist = (pPlane->m_Dist - (vPt.y * pPlane->m_Normal.y + vPt.x * pPlane->m_Normal.x +
							vPt.z * pPlane->m_Normal.z)) * (-1.0f / pPlane->m_Normal.y);
						if (fDist < fMinDist)
						{
							iBest = iPt;
							fMinDist = fDist;
						}
					}
				}
				else
				{
					vPush.y = 0.0f;
					fLen = (float)sqrt(vPush.z * vPush.z + vPush.x * vPush.x);
					if (fLen != 0.0f)
					{
						fInv = 1.0f / fLen;
						vPush.x *= fInv;
						vPush.y = 0.0f * fInv;
						vPush.z *= fInv;
					}
					for (iPt = 0; iPt < 4; iPt++)
					{
						LTVector vPt = g_MovePts[1][iPt];
						fDist = (pPlane->m_Dist - (vPt.y * pPlane->m_Normal.y + vPt.x * pPlane->m_Normal.x +
							vPt.z * pPlane->m_Normal.z)) *
							(-1.0f / (pPlane->m_Normal.y * vPush.y + pPlane->m_Normal.x * vPush.x +
							vPush.z * pPlane->m_Normal.z));
						if (fDist < fMinDist)
						{
							iBest = iPt;
							fMinDist = fDist;
						}
					}
				}

				if (fMinDist < -0.0001f)
				{
					pCollisionInfo = g_pCurRequest->m_pCollisionInfo;
					pCollisionInfo->m_Plane.m_Normal = vPush;
					pCollisionInfo->m_Plane.m_Dist = pPlane->m_Dist;
					pCollisionInfo->m_hObject = (HOBJECT)g_pCurRequest->m_pWorldObj;
					pCollisionInfo->m_hPoly = g_pCurRequest->m_pWorld->MakeHPoly(pRoot);

					vSavedVelOffset = pInfo->m_VelOffset;
					pInfo->m_nHits++;
					DoObjectCollisionResponse(pCollisionInfo, pInfo, g_pCurRequest->m_pObject,
						g_pCurRequest->m_pWorldObj, g_pCurRequest->m_pWorld, pRoot, &vPush);
					pInfo->m_VelOffset = vSavedVelOffset;

					fMinDist = -fMinDist;
					vMoveBack = vPush * fMinDist;

					// Don't push the box above the clip box.
					vFinal.y = vMoveBack.y;
					if (vClipMax.y < vMoveBack.y + g_MovePts[1][iBest].y)
						vFinal.y = vMoveBack.y - ((vMoveBack.y + g_MovePts[1][iBest].y) - vClipMax.y);

					vFinal.x = vMoveBack.x;
					if (!(vMoveBack.x < 0.0f))
					{
						if (vMoveBack.x > 0.0f && g_MovePts[1][0].x < vClipMin.x)
							vFinal.x = 0.0f;
					}
					else if (vClipMax.x < g_MovePts[1][2].x)
					{
						vFinal.x = 0.0f;
					}

					vFinal.z = vMoveBack.z;
					if (!(vMoveBack.z < 0.0f))
					{
						if (vMoveBack.z > 0.0f && g_MovePts[1][0].z < vClipMin.z)
							vFinal.z = 0.0f;
					}
					else if (vClipMax.z < g_MovePts[1][2].z)
					{
						vFinal.z = 0.0f;
					}

					fLen = vFinal.Mag();
					vFinalDir = vFinal;
					if (fLen != 0.0f)
						vFinalDir.Norm();

					// Take the part of the push along the movement out.
					fDotMove = vFinalDir.y * (g_P1.y - g_P0.y) + vFinalDir.z * (g_P1.z - g_P0.z) +
						(g_P1.x - g_P0.x) * vFinalDir.x;
					if (fDotMove > 0.0f)
					{
						vFinal.x -= vFinalDir.x * fDotMove;
						vFinal.y -= vFinalDir.y * fDotMove;
						vFinal.z -= vFinalDir.z * fDotMove;
					}

					AddMovement(&g_P1, g_P1, vFinal);

					vMoveBack = vFinalDir * fDotMove + vFinal;
					if (vMoveBack.y > 0.0f)
					{
						// The start only follows the box up.
						AddMovement(&vNewP0, g_P0, vMoveBack);
						g_P0.y = vNewP0.y;
						g_BoxOffset.y += vMoveBack.y * 0.5f;
						g_pCurRequest->m_Dims.y -= vMoveBack.y * 0.5f;
					}

					if (g_pCurRequest->m_Dims.y < 0.0f)
						return;

					SetupMoveBox(&g_BoxMin, &g_BoxMax);
					g_pCurInfo->m_pStandingOn = pRoot;
					iRoot = pBN->m_Sides[FrontSide];
					continue;
				}
			}
		}

		if (pBN->m_Sides[BackSide] != 0xfffe && pBN->m_Sides[BackSide] != 0xffff)
			*pStackPos++ = pBN->m_Sides[BackSide];
		iRoot = pBN->m_Sides[FrontSide];
	}
}


// The original stair stepping (the "NewStairStep" console variable is 0): raises the box onto the
// first stair polygon the velocity runs into.  Returns TRUE when it ran into a SURF_NOTASTEP polygon
// that's too high to step on.
// Not matching: written from the Ghidra output; the original is 288 bytes larger (the position
// adds are expanded with the epsilon fix-up of AddMovement).
// STUB: LITHTECH 0x0041edb0
LTBOOL StairStepOld(uint16 iRoot, CollideInfo *pInfo)
{
	Node *pNodes, *pRoot;
	PBlockNode *pBN;
	LTPlane *pPlane;
	uint16 *pStackPos;
	CollisionInfo *pCollisionInfo;
	LTVector vSavedVelOffset;
	LTBOOL bHitNonStep;
	float fMaxRise, fMinDist, fDist, fDot, fRise, fX, fZ;
	int iPt;

	bHitNonStep = LTFALSE;
	if (g_P0.y <= g_P1.y)
		fMaxRise = 0.0f;
	else
		fMaxRise = g_P0.y - g_P1.y;

	pNodes = g_pCurRequest->m_pWorld->GetNodes();
	pStackPos = g_PBlockStack;
	for (;;)
	{
		for (;;)
		{
			for (;;)
			{
				if (iRoot == 0xfffe || iRoot == 0xffff)
				{
					if (pStackPos == g_PBlockStack)
						return bHitNonStep;

					iRoot = *--pStackPos;
				}

				pBN = (PBlockNode*)g_pCurBlock->m_pNodes + iRoot;
				pRoot = pNodes + pBN->m_iNode;
				pPlane = pRoot->GetPlane();

				// Do the fast test on the whole movement area.
				fDot = g_BoxFindCenter.x * pPlane->m_Normal.x + g_BoxFindCenter.y * pPlane->m_Normal.y +
					g_BoxFindCenter.z * pPlane->m_Normal.z - pPlane->m_Dist;
				if (fDot > g_BoxFindRadius)
				{
					iRoot = pBN->m_Sides[FrontSide];
					continue;
				}
				if (fDot < -g_BoxFindRadius)
				{
					iRoot = pBN->m_Sides[BackSide];
					continue;
				}
				break;
			}

			if (pRoot->m_pPoly)
				break;

			if (pBN->m_Sides[BackSide] != 0xfffe && pBN->m_Sides[BackSide] != 0xffff)
				*pStackPos++ = pBN->m_Sides[BackSide];
			iRoot = pBN->m_Sides[FrontSide];
		}

		if (g_fStairOldMinNormalY < pRoot->GetPlane()->m_Normal.y)
		{
			LTObject *pObj = g_pCurRequest->m_pObject;

			pPlane = pRoot->GetPlane();
			fDot = pPlane->m_Normal.x * (g_pCurInfo->m_VelOffset.x + pObj->m_Velocity.x) +
				(g_pCurInfo->m_VelOffset.y + pObj->m_Velocity.y) * pPlane->m_Normal.y +
				pPlane->m_Normal.z * (g_pCurInfo->m_VelOffset.z + pObj->m_Velocity.z);
			if (fDot <= 0.0f && PolyTouchesBox(pRoot->m_pPoly, LTNULL, LTNULL))
			{
				pPlane = pRoot->GetPlane();
				fMinDist = 5000.0f;
				pCollisionInfo = g_pCurRequest->m_pCollisionInfo;
				pCollisionInfo->m_Plane = *pPlane;
				if (pPlane->m_Normal.y >= g_fStairOldFlatNormalY)
				{
					pCollisionInfo->m_Plane.m_Normal.x = 0.0f;
					pCollisionInfo->m_Plane.m_Normal.y = 1.0f;
					pCollisionInfo->m_Plane.m_Normal.z = 0.0f;
					for (iPt = 0; iPt < 4; iPt++)
					{
						fDist = (pCollisionInfo->m_Plane.m_Dist - (g_MovePts[1][iPt].x * pPlane->m_Normal.x +
							g_MovePts[1][iPt].y * pPlane->m_Normal.y + g_MovePts[1][iPt].z * pPlane->m_Normal.z)) *
							(-1.0f / pPlane->m_Normal.y);
						if (fDist < fMinDist)
							fMinDist = fDist;
					}
				}
				else
				{
					pCollisionInfo->m_Plane.m_Normal.y = 0.0f;
					fDist = (float)sqrt(pCollisionInfo->m_Plane.m_Normal.z * pCollisionInfo->m_Plane.m_Normal.z +
						pCollisionInfo->m_Plane.m_Normal.x * pCollisionInfo->m_Plane.m_Normal.x);
					if (fDist != 0.0f)
					{
						fDist = 1.0f / fDist;
						pCollisionInfo->m_Plane.m_Normal.x *= fDist;
						pCollisionInfo->m_Plane.m_Normal.y = 0.0f * fDist;
						pCollisionInfo->m_Plane.m_Normal.z *= fDist;
					}
					for (iPt = 0; iPt < 4; iPt++)
					{
						fDist = (pCollisionInfo->m_Plane.m_Dist - (g_MovePts[1][iPt].x * pPlane->m_Normal.x +
							g_MovePts[1][iPt].y * pPlane->m_Normal.y + g_MovePts[1][iPt].z * pPlane->m_Normal.z)) *
							(-1.0f / (pPlane->m_Normal.x * pCollisionInfo->m_Plane.m_Normal.x +
							pPlane->m_Normal.z * pCollisionInfo->m_Plane.m_Normal.z +
							pPlane->m_Normal.y * pCollisionInfo->m_Plane.m_Normal.y));
						if (fDist < fMinDist)
							fMinDist = fDist;
					}
				}

				pCollisionInfo->m_hObject = (HOBJECT)g_pCurRequest->m_pWorldObj;
				pCollisionInfo->m_hPoly = g_pCurRequest->m_pWorld->MakeHPoly(pRoot);

				if (fMinDist < 0.0f)
				{
					fX = pCollisionInfo->m_Plane.m_Normal.x;
					fMinDist = -fMinDist;
					fZ = pCollisionInfo->m_Plane.m_Normal.z;
					fRise = pCollisionInfo->m_Plane.m_Normal.y * fMinDist;
					if (!(((Surface*)pRoot->m_pPoly->m_pSurface)->m_Flags & SURF_NOTASTEP) || fRise <= fMaxRise)
					{
						pInfo->m_nHits++;
						vSavedVelOffset = pInfo->m_VelOffset;
						DoObjectCollisionResponse(pCollisionInfo, pInfo, g_pCurRequest->m_pObject,
							g_pCurRequest->m_pWorldObj, g_pCurRequest->m_pWorld, pRoot, &pPlane->m_Normal);
						pInfo->m_VelOffset = vSavedVelOffset;

						g_P0.x = fX * fMinDist + g_P0.x;
						g_P0.y = fRise + g_P0.y;
						g_P0.z = g_P0.z + fZ * fMinDist;
						g_P1.x = fX * fMinDist + g_P1.x;
						g_P1.y = fRise + g_P1.y;
						g_P1.z = fZ * fMinDist + g_P1.z;
						g_BoxOffset.y -= fRise * 0.5f;
						g_pCurRequest->m_Dims.y -= fRise * 0.5f;
						SetupMoveBox(&g_BoxMin, &g_BoxMax);
						g_pCurInfo->m_pStandingOn = pRoot;
						fMaxRise -= fRise;
						iRoot = pBN->m_Sides[FrontSide];
						continue;
					}

					iRoot = pBN->m_Sides[FrontSide];
					bHitNonStep = LTTRUE;
					continue;
				}
			}
		}

		if (pBN->m_Sides[BackSide] != 0xfffe && pBN->m_Sides[BackSide] != 0xffff)
			*pStackPos++ = pBN->m_Sides[BackSide];
		iRoot = pBN->m_Sides[FrontSide];
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
// Out-of-line copies of the LTVector inlines (the big collision functions call them).
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
// STANDIN: g_pfnNodeGetPlane..g_pfnVecNorm force the Node/LTVector out-of-line copies that the (still
// inexact) callers above inline instead of calling (not in lithtech.exe)
LTPlane* (Node::*g_pfnNodeGetPlane)() = &Node::GetPlane;
void (LTVector::*g_pfnVecInit)(float, float, float) = &LTVector::Init;
float (LTVector::*g_pfnVecMag)() const = &LTVector::Mag;
float (LTVector::*g_pfnVecDot)(LTVector) const = &LTVector::Dot;
LTVector (LTVector::*g_pfnVecNeg)() const = &LTVector::operator-;
LTVector (LTVector::*g_pfnVecAdd)(const LTVector) const = &LTVector::operator+;
LTVector (LTVector::*g_pfnVecSub)(const LTVector) const = &LTVector::operator-;
void (LTVector::*g_pfnVecScaleEq)(float) = &LTVector::operator*=;
LTBOOL (LTVector::*g_pfnVecNotEq)(const LTVector&) const = &LTVector::operator!=;
void (LTVector::*g_pfnVecNorm)(float) = &LTVector::Norm;
