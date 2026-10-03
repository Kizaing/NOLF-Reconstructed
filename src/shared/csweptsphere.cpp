// Talon's swept sphere vs polygon tests (not in Jupiter; Jupiter's intersectsweptsphere.cpp is a later rewrite).
// 0x00424970 sweeps the sphere against one polygon: the face (the centre reaches the plane inside the polygon's
// edges), then each edge (SweptSphereToEdge, 0x00425000) and each vertex (SweptSphereToPoint, 0x004253b0).
// 0x00425630 pushes the sphere out of the solid polygons (10 passes), 0x004258b0 is the polygon list walker and
// 0x004259a0 turns an object to stand on a surface (prints "SweptSphereOrient Rotation Invalid!!" on NaN).
// Names are ours except SweptSphereOrient.
// The geometry functions are written from the disassembly and are all still STUBs.
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

// Sphere moving from pStart to pEnd against the line segment pV0 - pV1: the fraction of the move (*pT) and the
// direction from the segment to the sphere's centre at the hit (pNormal).
// The quadratic is |vP + vMove t|^2 - (vDir . (vP + vMove t))^2 = r^2 (a cylinder around the edge's line), then the
// hit must lie within the segment (0 <= s <= length).
// Not matching: the original calls the vector constructor (0x00412960) out of line for vDir and vP and the
// Dot/operator*/operator-/Norm copies (0x0041f6d0/0x0041f770/0x0041f740/0x0041f820) for the normal, while our
// budget inlines all of them. With `inline void __ballast()` of 12 `if(0) x = 0;` statements called before the first
// statement the call pattern, the prologue and the first 0xc5 bytes match exactly and 73 of 308 instruction lines
// still differ (so the original has about 12 more units of inline cost at the top, or less budget). What remains:
// the original keeps t on the FPU stack through the range tests (`fld st(0); fstp [pT]`, `fld 0; fcomp st(1)`) where
// we reload it from memory, the stack slot of the second by-value vector temporary (esp+0x38 against esp+0x20),
// and the x87 order of vDir.Dot(vP) (y, z, x).
// STUB: LITHTECH 0x00425000
LTBOOL SweptSphereToEdge(LTVector *pStart, LTVector *pEnd, float fRadius, LTVector *pV0, LTVector *pV1,
	float *pT, LTVector *pNormal)
{
	LTVector vMove, vEdge, vDir, vP, vRel;
	float fLen, a, b, A, B, C, fDisc, fInv, t, t1, t2, s;

	vMove = *pEnd - *pStart;
	vEdge = *pV1 - *pV0;
	fLen = vEdge.Mag();
	vDir = vEdge * (1.0f / fLen);
	vP = *pStart - *pV0;

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
			vRel = vMove * t + vP;
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

// Sweeps the sphere against one polygon: the face first (the sphere reaches the plane with its centre inside the
// polygon's edges), then each edge and vertex.  *pNormal is the direction the sphere is pushed away.
// Not matching: the original calls the vector constructor (0x00412960), Mag (0x0041f6a0) and the operators out of line
// from the first statement on (its plane normal is (v1 - v0).Cross(v2 - v0): the second operand is the by-value
// argument and goes through the constructor, the first stays on the FPU stack; then it is negated through the
// constructor again and normalised inline after an out-of-line Mag) but inlines the Dot with the normal and the Cross
// of the edge test at the end, while we inline the early ones and call the late ones. That is the greedy inline budget
// seen from the other side: the original's early expansions get small shares, so it has many more inline call
// sites pending after them than our source. Free pending calls after the last statement (an empty inline
// function called 16 times, not shipped) take the aligned-instruction mismatches from 465 to 302 of about 425.
// The structure after the face test (edge and vertex loop, the hit-type global) matches.
// STUB: LITHTECH 0x00424970
LTBOOL SweptSphereToPoly(LTVector *pStart, LTVector *pEnd, float fRadius, WorldPoly *pPoly, float *pT,
	LTVector *pNormal)
{
	SPolyVertex *pVert;
	LTVector *pPrev, *pCur;
	LTVector vNormal, vContact, vTo, vEdge, vCross;
	float fStartDist, fEndDist, t, fMag;
	LTBOOL bHit;
	uint32 i, nVerts;

	g_SweptSphereHitType = -1;

	pVert = (SPolyVertex*)(pPoly + 1);
	nVerts = pPoly->m_nVertices;

	vNormal = -((*pVert[1].m_Vec - *pVert[0].m_Vec).Cross(*pVert[2].m_Vec - *pVert[0].m_Vec));
	fMag = vNormal.Mag();
	if(fMag != 0.0f)
	{
		fMag = 1.0f / fMag;
		vNormal.x *= fMag;
		vNormal.y *= fMag;
		vNormal.z *= fMag;
	}

	fStartDist = (*pStart - *pVert[0].m_Vec).Dot(vNormal);
	fEndDist = (*pEnd - *pVert[0].m_Vec).Dot(vNormal);

	// The sphere's centre goes from at least a radius in front of the plane to within a radius of it.
	if(fStartDist >= fRadius && !(fEndDist > fRadius))
	{
		if(fStartDist - fEndDist != 0.0f)
		{
			t = (fStartDist - fRadius) / (fStartDist - fEndDist);
			vContact = (*pStart + (*pEnd - *pStart) * t) - vNormal * fRadius;

			pPrev = pVert[nVerts - 1].m_Vec;
			for(i=0; i < nVerts; i++)
			{
				pCur = pVert[i].m_Vec;
				vEdge = *pCur - *pPrev;
				vTo = vContact - *pPrev;
				vCross = vEdge.Cross(vTo);
				if(vNormal.Dot(vCross) > 0.0f)
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
	pPrev = pVert[nVerts - 1].m_Vec;
	for(i=0; i < nVerts; i++)
	{
		pCur = pVert[i].m_Vec;

		if(SweptSphereToEdge(pStart, pEnd, fRadius, pPrev, pCur, &t, &vContact) && t < *pT)
		{
			*pT = t;
			*pNormal = vContact;
			bHit = LTTRUE;
			g_SweptSphereHitType = 1;
		}

		if(SweptSphereToPoint(pStart, pEnd, fRadius, pPrev, &t, &vContact) && t < *pT)
		{
			*pT = t;
			*pNormal = vContact;
			bHit = LTTRUE;
			g_SweptSphereHitType = 2;
		}

		pPrev = pCur;
	}

	return bHit;
}

// Pushes the sphere at pPos out of the solid polygons it overlaps (a plane and the point inside the polygon).
// Each push uses up one of 10 passes; returns the passes left (0 if it could not get free).
// Not matching, but close in shape: the loop head is `do { if(!(nPasses > 0)) break; ... goto Again; ... } while(1)`
// (a `while` is inverted and loses the first test), the plane test is pPlane->DistTo(*pPos) (the by-value copy is of
// *pPos, the plane normal is read in place). The original keeps pPos in ebx and the plane in edx; ours uses edi/ecx.
// The return type is uint32 (the callers test eax, not ax) with a uint16 counter (`and eax,0xffff` at the end).
// STUB: LITHTECH 0x00425630
uint32 SpherePosTestPolys(LTVector *pPos, float fRadius, WorldPoly **pPolies, int nPolies)
{
	uint16 nPasses;
	int i;
	uint32 j, nVerts;
	WorldPoly *pPoly;
	LTPlane *pPlane;
	SPolyVertex *pVert;
	LTVector *pPrev, *pCur;
	LTVector vProj, vNormal, vEdge, vTo, vCross, vPush;
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

			vProj = *pPos - pPlane->m_Normal * fDist;
			pVert = (SPolyVertex*)(pPoly + 1);
			nVerts = pPoly->m_nVertices;
			vNormal = pPlane->m_Normal;
			for(j=0; j < nVerts; j++)
			{
				if(j)
					pPrev = pVert[j - 1].m_Vec;
				else
					pPrev = pVert[nVerts - 1].m_Vec;
				pCur = pVert[j].m_Vec;

				vEdge = *pCur - *pPrev;
				vTo = vProj - *pPrev;
				vCross = vEdge.Cross(vTo);
				if(vCross.Dot(vNormal) < -0.001f)
					break;
			}

			if(j < nVerts)
				continue;

			vPush = pPoly->GetPlane()->m_Normal * (fRadius - fDist + 0.2f);
			*pPos += vPush;
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
// STUB: LITHTECH 0x004259a0
// Remaining difference: 579/816 bytes. The original's frame is 0x8c: math, fMag, vAxis, a by-value copy of the normal,
// fAngle, vUp, three vectors+rotation for the GetRotationVectors/AlignRotation/RotateAroundAxis helpers, and 16 bytes
// (a LTRotation?) at F+0x54 that nothing here uses; the first GetRotationVectors writes vRight/vForward to the
// two outermost slots (F+0x74, F+0x80), later ones to F+0x30/0x3c/0x48. The axis (up x -normal) is computed with
// the three negated normal components loaded first (fchs) and nz multiplied in place; (-*pNormal).Cross(vUp),
// VEC_CROSS with a VEC_NEGATE local and vUp.Cross(-n) all schedule differently.
LTBOOL SweptSphereOrient(LTVector *pNormal, LTObject *pObj)
{
	LTVector vForward, vRight;
	LTRotation rot, rotUnused;
	LTVector vR2, vU2, vF2, vUp;
	float fAngle;
	LTVector vAxis;
	float fMag;
	ILTMath math;

	math.GetRotationVectors(pObj->m_Rotation, vRight, vUp, vForward);

	vAxis = (-*pNormal).Cross(vUp);
	{
		float fLen = VEC_MAG(vAxis);
		if(fLen != 0.0f)
		{
			fLen = 1.0f / fLen;
			vAxis.x *= fLen;
			vAxis.y *= fLen;
			vAxis.z *= fLen;
		}
	}

	fAngle = (float)acos(vUp.Dot(*pNormal));
	if(_isnan(fAngle))
		return LTFALSE;

	math.AlignRotation(rot, pObj->m_Velocity, vUp);
	math.RotateAroundAxis(rot, vAxis, fAngle);
	fMag = VEC_MAG(pObj->m_Velocity);
	math.GetRotationVectors(rot, vR2, vU2, vF2);
	pObj->m_Velocity.x = vF2.x * fMag;
	pObj->m_Velocity.y = vF2.y * fMag;
	pObj->m_Velocity.z = vF2.z * fMag;

	math.AlignRotation(rot, pObj->m_Acceleration, vUp);
	math.RotateAroundAxis(rot, vAxis, fAngle);
	fMag = VEC_MAG(pObj->m_Acceleration);
	math.GetRotationVectors(rot, vR2, vU2, vF2);
	pObj->m_Acceleration.x = vF2.x * fMag;
	pObj->m_Acceleration.y = vF2.y * fMag;
	pObj->m_Acceleration.z = vF2.z * fMag;

	math.RotateAroundAxis(pObj->m_Rotation, vAxis, fAngle);
	math.GetRotationVectors(pObj->m_Rotation, vR2, vU2, vF2);
	if(_isnan(vU2.x) || _isnan(vU2.y) || _isnan(vU2.z))
		dsi_ConsolePrint("SweptSphereOrient Rotation Invalid!!  Axis = %f %f %f, Theta", vAxis.x, vAxis.y, vAxis.z, fAngle);

	return LTTRUE;
}
