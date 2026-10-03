// Talon's swept sphere vs polygon tests (not in Jupiter; Jupiter's intersectsweptsphere.cpp is a later rewrite).
// 0x00424970 sweeps the sphere against one polygon: the face (the centre reaches the plane inside the polygon's
// edges), then each edge (SweptSphereToEdge, 0x00425000) and each vertex (SweptSphereToPoint, 0x004253b0).
// 0x00425630 pushes the sphere out of the solid polygons (10 passes), 0x004258b0 is the polygon list walker and
// 0x004259a0 turns an object to stand on a surface (prints "SweptSphereOrient Rotation Invalid!!" on NaN).
// Names are ours except SweptSphereOrient.
// The geometry functions are written from the disassembly with the SDK vector operators the original used.
#include <math.h>
#include <float.h>
#include "bdefs.h"
#include "de_objects.h"
#include "de_world.h"
#include "csweptsphere.h"
#include "iltmath.h"

// Which test of SweptSphereToPoly found the hit: 0 the face, 1 an edge, 2 a vertex (-1 before it runs).
// GLOBAL: LITHTECH 0x004d1808
int g_SweptSphereHitType = -1;

// The polygon's vertex positions.
inline LTVector* PolyVert(WorldPoly *pPoly, uint32 i) { return ((SPolyVertex*)(pPoly + 1))[i].m_Vec; }

// Sweeps the sphere against one polygon: the face first (the sphere reaches the plane with its centre inside the
// polygon's edges), then each edge and vertex.  *pNormal is the direction the sphere is pushed away.
// Written with the SDK operators the original calls (it calls the vector constructor 0x00412960 out of line 13 times
// and Mag 0x0041f6a0 once, from inside an inlined Norm): direct-initialised locals (`LTVector vNormal = -(...)`,
// `LTVector vMove = ...`, `LTVector vContact = *pStart + vMove * t - vNormal * fRadius`, `LTVector vEdge`) construct
// in place where an assignment builds a temporary and copies it, and the vertex accessor PolyVert supplies pending
// inline sites. Aligned mismatches 462 -> 184 (wave 6 phase 2). Remaining: the loop's vEdge constructor and
// Dot are out of line in ours (inline in the original: we still have less budget left at the end), and the registers
// (pPoly in esi and the vertex array in edi in the original). Writing the second v0 of the Cross argument without
// PolyVert gives 164 (and the right call pattern up to the loop), but mixing the two forms in one statement looks
// arbitrary; ending with free pending sites, `if(0)` budget ballast and statement variants of the loop didn't help.
// Defined before SweptSphereToEdge/Point, in the exe's order (that costs 9 aligned mismatches against defining it
// after them, but the link order needs it).
// STUB: LITHTECH 0x00424970
LTBOOL SweptSphereToPoly(LTVector *pStart, LTVector *pEnd, float fRadius, WorldPoly *pPoly, float *pT,
	LTVector *pNormal)
{
	LTVector *pPrev, *pCur;
	float fStartDist, fEndDist, t;
	LTBOOL bHit;
	uint32 i;

	g_SweptSphereHitType = -1;

	LTVector vNormal = -((*PolyVert(pPoly, 1) - *PolyVert(pPoly, 0)).Cross(*PolyVert(pPoly, 2) - *PolyVert(pPoly, 0)));
	vNormal.Norm();

	fStartDist = vNormal.Dot(*pStart - *PolyVert(pPoly, 0));
	fEndDist = vNormal.Dot(*pEnd - *PolyVert(pPoly, 0));

	// The sphere's centre goes from at least a radius in front of the plane to within a radius of it.
	if(fStartDist >= fRadius && !(fEndDist > fRadius))
	{
		if(fStartDist - fEndDist != 0.0f)
		{
			t = (fStartDist - fRadius) / (fStartDist - fEndDist);
			LTVector vMove = *pEnd - *pStart;
			LTVector vContact = *pStart + vMove * t - vNormal * fRadius;

			pPrev = PolyVert(pPoly, pPoly->m_nVertices - 1);
			for(i=0; i < pPoly->m_nVertices; i++)
			{
				pCur = PolyVert(pPoly, i);
				LTVector vEdge = *pCur - *pPrev;
				if(vEdge.Cross(vContact - *pPrev).Dot(vNormal) > 0.0f)
					goto Edges;

				pPrev = pCur;
			}

			*pT = t;
			*pNormal = vNormal;
			g_SweptSphereHitType = 0;
			return LTTRUE;
		}
	}

Edges:
	if(fStartDist < 0.0f)
		return LTFALSE;

	*pT = 1.0f;
	bHit = LTFALSE;
	pPrev = PolyVert(pPoly, pPoly->m_nVertices - 1);
	for(i=0; i < pPoly->m_nVertices; i++)
	{
		pCur = PolyVert(pPoly, i);

		if(SweptSphereToEdge(pStart, pEnd, fRadius, pPrev, pCur, &t, &vNormal) && t < *pT)
		{
			*pT = t;
			*pNormal = vNormal;
			bHit = LTTRUE;
			g_SweptSphereHitType = 1;
		}

		if(SweptSphereToPoint(pStart, pEnd, fRadius, pPrev, &t, &vNormal) && t < *pT)
		{
			*pT = t;
			*pNormal = vNormal;
			bHit = LTTRUE;
			g_SweptSphereHitType = 2;
		}

		pPrev = pCur;
	}

	return bHit;
}

// Sphere moving from pStart to pEnd against the line segment pV0 - pV1: the fraction of the move (*pT) and the
// direction from the segment to the sphere's centre at the hit (pNormal).
// The quadratic is |vP + vMove t|^2 - (vDir . (vP + vMove t))^2 = r^2 (a cylinder around the edge's line), then the
// hit must lie within the segment (0 <= s <= length).
// Wave 6 phase 2: direct-initialised operator locals (`LTVector vMove = *pEnd - *pStart;` ...) and the edge length
// computed twice (`fLen = vEdge.Mag(); vDir = vEdge * (1.0f / vEdge.Mag());`, which VC6 merges) give the
// original's first 0xc5 bytes and constructor calls: 226 -> 76 aligned mismatches. Remaining: the original computes
// 1.0f / fLen from the stored fLen (ours keeps it on the FPU), keeps t on the FPU stack through the range tests
// (`fld st(0); fstp [pT]`) where we go through memory, and calls Dot/operator* out of line at the end (we inline them:
// we still have more budget there). Tried: the Dot/MagSqr forms of a, b, A, B, C (32 combinations), Dist, a named
// 1/fLen, `vDir *= ...`.
// STUB: LITHTECH 0x00425000
LTBOOL SweptSphereToEdge(LTVector *pStart, LTVector *pEnd, float fRadius, LTVector *pV0, LTVector *pV1,
	float *pT, LTVector *pNormal)
{
	float a, b, A, B, C, fDisc, fInv, t, t1, t2, s;

	LTVector vMove = *pEnd - *pStart;
	LTVector vEdge = *pV1 - *pV0;
	float fLen = vEdge.Mag();
	LTVector vDir = vEdge * (1.0f / vEdge.Mag());
	LTVector vP = *pStart - *pV0;

	a = vDir.Dot(vMove);
	b = vDir.Dot(vP);
	A = vMove.Dot(vMove) - a * a;
	B = (vP.Dot(vMove) - b * a) * 2.0f;
	if(A == 0.0f)
		return LTFALSE;

	C = vP.Dot(vP) - b * b - fRadius * fRadius;
	fDisc = B * B - A * C * 4.0f;
	fInv = 1.0f / (A * 2.0f);
	if(fDisc == 0.0f)
	{
		t1 = -(B * fInv);
		t2 = t1;
	}
	else if(fDisc > 0.0f)
	{
		fDisc = (float)sqrt(fDisc);
		t1 = (fDisc - B) * fInv;
		t2 = (-B - fDisc) * fInv;
	}
	else
		return LTFALSE;

	if(t1 == t2)
		return LTFALSE;

	t = LTMIN(t1, t2);
	*pT = t;
	if(0.0f <= t && t <= 1.0f)
	{
		s = a * t + b;
		if(0.0f <= s && s <= fLen)
		{
			LTVector vRel = vMove * t + vP;
			*pNormal = vRel - vDir * vRel.Dot(vDir);
			pNormal->Norm(1.0f);
			return LTTRUE;
		}
	}

	return LTFALSE;
}

// Sphere moving from pStart to pEnd against the point pVertex: the fraction of the move (*pT) and the direction
// from the point to the sphere's centre at the hit (pNormal).
// Close (180/624 bytes differ, same code through the quadratic): the original builds vMove * t in a stack temporary
// (its frame is 12 bytes larger, y and z go through [esp+0x28]/[esp+0x2c] while x stays on the FPU stack) before
// adding vP; the root selection is `t1 = (disc - b) * inv; t2 = (-b - disc) * inv; t = LTMIN(t1, t2)` with t1 on the
// FPU stack and t2 in memory, and the positive form `if(0.0f <= t && t <= 1.0f) {...}`; all of that matches.
// STUB: LITHTECH 0x004253b0
LTBOOL SweptSphereToPoint(LTVector *pStart, LTVector *pEnd, float fRadius, LTVector *pVertex, float *pT,
	LTVector *pNormal)
{
	LTVector vMove, vP;
	float a, b, c, fDisc, fInv, t, t1, t2, fMag;

	vMove = *pEnd - *pStart;
	vP = *pStart - *pVertex;

	a = vMove.Dot(vMove);
	b = vP.Dot(vMove) * 2.0f;
	if(a == 0.0f)
		return LTFALSE;

	c = vP.Dot(vP) - fRadius * fRadius;
	fDisc = b * b - a * c * 4.0f;
	fInv = 1.0f / (a * 2.0f);
	if(fDisc == 0.0f)
	{
		t1 = -(b * fInv);
		t2 = t1;
	}
	else if(fDisc > 0.0f)
	{
		fDisc = (float)sqrt(fDisc);
		t1 = (fDisc - b) * fInv;
		t2 = (-b - fDisc) * fInv;
	}
	else
		return LTFALSE;

	if(t1 == t2)
		return LTFALSE;

	t = LTMIN(t1, t2);
	*pT = t;
	if(0.0f <= t && t <= 1.0f)
	{
		*pNormal = vMove * t + vP;
		fMag = pNormal->Mag();
		if(fMag != 0.0f)
		{
			fMag = 1.0f / fMag;
			pNormal->x *= fMag;
			pNormal->y *= fMag;
			pNormal->z *= fMag;
		}
		return LTTRUE;
	}
	return LTFALSE;
}

// Pushes the sphere at pPos out of the solid polygons it overlaps (a plane and the point inside the polygon).
// Each push uses up one of 10 passes; returns the passes left (0 if it could not get free).
// Not matching. The edge test is (vEdge x vNormal) . vTo with the normal's by-value copy hoisted out of the loop (the
// earlier (vEdge x vTo) . vNormal had the opposite sign). The loop head is `do { if(!(nPasses > 0)) break; ... goto
// Again; ... } while(1)`. Remaining (107 aligned mismatches, was 100 with the wrong-signed test): the original's
// polygon loop is the inverted for (count tested before and at the bottom) where ours tests at the top, its inner
// loop's break goes straight to the next polygon (ours tests j < nVerts first), and it keeps pPos in ebx and the
// polygon in esi. A `while` head, `goto NextPoly` out of the inner loop and `if(j != nVerts)` were worse.
// STUB: LITHTECH 0x00425630
uint32 SpherePosTestPolys(LTVector *pPos, float fRadius, WorldPoly **pPolies, int nPolies)
{
	uint16 nPasses;
	int i;
	uint32 j, nVerts;
	WorldPoly *pPoly;
	LTPlane *pPlane;
	LTVector *pPrev, *pCur;
	LTVector vNormal, vEdge, vTo, vCross;
	float fDist;

	nPasses = 10;
	do
	{
		if(!(nPasses > 0))
			break;
		for(i=0; i < nPolies; i++)
		{
			pPoly = pPolies[i];
			if(!(((Surface*)pPoly->m_pSurface)->m_Flags & SURF_SOLID))
				continue;

			pPlane = pPoly->GetPlane();
			fDist = pPlane->DistTo(*pPos);
			if(!(fDist < fRadius + 0.1f))
				continue;

			// Is the point on the plane inside the polygon's edges?
			LTVector vProj = *pPos - pPlane->m_Normal * fDist;
			nVerts = pPoly->m_nVertices;
			vNormal = pPlane->m_Normal;
			for(j=0; j < nVerts; j++)
			{
				if(j)
					pPrev = PolyVert(pPoly, j - 1);
				else
					pPrev = PolyVert(pPoly, nVerts - 1);
				pCur = PolyVert(pPoly, j);

				vEdge = *pCur - *pPrev;
				vTo = vProj - *pPrev;
				vCross = vEdge.Cross(vNormal);
				if(vCross.Dot(vTo) < -0.001f)
					break;
			}

			if(j < nVerts)
				continue;

			// Push it out of the plane and start over.
			*pPos += pPoly->GetPlane()->m_Normal * (fRadius - fDist + 0.2f);
			nPasses--;
			goto Again;
		}
		break;
Again:;
	} while(1);

	return nPasses;
}

// Returns the nearest hit of the sphere against the solid polygons of the list.
// FUNCTION: LITHTECH 0x004258b0
LTBOOL SweptSphereToPolys(LTVector *pStart, LTVector *pEnd, float fRadius, WorldPoly **ppPolys, int nPolys,
	LTVector *pHitPos, LTVector *pHitNormal, float *pFraction, int *pbClear)
{
	LTBOOL bHit;
	int i;
	WorldPoly *pPoly;
	LTVector vHit;
	float t;

	bHit = LTFALSE;
	*pFraction = 1.0f;
	*pbClear = LTTRUE;

	for(i=0; i < nPolys; i++)
	{
		pPoly = ppPolys[i];
		if(((Surface*)pPoly->m_pSurface)->m_Flags & SURF_SOLID)
		{
			if(SweptSphereToPoly(pStart, pEnd, fRadius, pPoly, &t, &vHit) && t < *pFraction)
			{
				*pFraction = t;
				*pHitPos = vHit;
				bHit = LTTRUE;
				*pHitNormal = pPoly->GetPlane()->m_Normal;
				if(*pbClear && (((Surface*)pPoly->m_pSurface)->m_Flags & 0x800000))
					*pbClear = LTFALSE;
			}
		}
	}

	return bHit;
}


// Turns the object to stand on the surface with this normal: rotates its orientation, velocity and acceleration
// around the axis from its up vector to the normal.  Returns 0 when the angle is NaN.
// The vectors are built with the SDK operators (vUp.Cross(-*pNormal), Norm, Mag, vF * fMag) and the locals are all
// declared at the top: declaring the LTRotations in the middle adds inline constructor sites that push the Norm's Mag
// out of line.
// FUNCTION: LITHTECH 0x004259a0
LTBOOL SweptSphereOrient(LTVector *pNormal, LTObject *pObj)
{
	ILTMath math;
	LTVector vRight, vUp, vForward, vR, vU, vF;
	LTRotation rVel, rAccel;
	float fMag;

	math.GetRotationVectors(pObj->m_Rotation, vRight, vUp, vForward);

	// Turn around the axis from the up vector to the normal.
	LTVector vAxis = vUp.Cross(-*pNormal);
	vAxis.Norm();

	float fAngle = (float)acos(vUp.Dot(*pNormal));
	if(_isnan(fAngle))
		return LTFALSE;

	math.AlignRotation(rVel, pObj->m_Velocity, vUp);
	math.RotateAroundAxis(rVel, vAxis, fAngle);
	fMag = pObj->m_Velocity.Mag();
	math.GetRotationVectors(rVel, vR, vU, vF);
	pObj->m_Velocity = vF * fMag;

	math.AlignRotation(rAccel, pObj->m_Acceleration, vUp);
	math.RotateAroundAxis(rAccel, vAxis, fAngle);
	fMag = pObj->m_Acceleration.Mag();
	math.GetRotationVectors(rAccel, vR, vU, vF);
	pObj->m_Acceleration = vF * fMag;

	math.RotateAroundAxis(pObj->m_Rotation, vAxis, fAngle);
	math.GetRotationVectors(pObj->m_Rotation, vR, vU, vF);
	if(_isnan(vU.x) || _isnan(vU.y) || _isnan(vU.z))
		dsi_ConsolePrint("SweptSphereOrient Rotation Invalid!!  Axis = %f %f %f, Theta", vAxis.x, vAxis.y, vAxis.z, fAngle);

	return LTTRUE;
}
