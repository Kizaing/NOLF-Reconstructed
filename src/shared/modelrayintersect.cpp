// Talon-only: ray intersection against a model's triangles (ILTModel::IntersectRays).
// Not in Jupiter; the file name is a guess (it sorts between modellt_impl and motion).
#include "bdefs.h"
#include "de_objects.h"
#include "model.h"
#include "transformmaker.h"
#include "modelrayintersect.h"


// Transformed vertices and triangles of the piece LOD being tested.
// FUNCTION: LITHTECH 0x0045add0 _$E7
// FUNCTION: LITHTECH 0x0045ade0 _$E3
// FUNCTION: LITHTECH 0x0045ae10 _$E6
// FUNCTION: LITHTECH 0x0045ae20 _$E4
// The static members' destructors share one guard byte (0x004e4550), one bit each.
// GLOBAL: LITHTECH 0x004e453c
CMoArray<LTVector> CModelRayIntersect::s_RayVerts;

// FUNCTION: LITHTECH 0x0045ae80 _$E12
// FUNCTION: LITHTECH 0x0045ae90 _$E9
// FUNCTION: LITHTECH 0x0045aec0 _$E11
// FUNCTION: LITHTECH 0x0045aed0 _$E10
// GLOBAL: LITHTECH 0x004e4528
CMoArray<RayTri> CModelRayIntersect::s_RayTris;


// FUNCTION: LITHTECH 0x0045af30
LTBOOL CModelRayIntersect::Init(HOBJECT hModel, const LTVector &vCamPos, int32 nLODOffset)
{
	m_hModel = hModel;
	m_pModel = ((ModelInstance*)hModel)->GetModelDB();
	m_iLOD = CalcLOD(vCamPos, nLODOffset);
	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0045af60
// Dist() inlines operator- but, with the m_LODDists.GetSize() sites pending after it, leaves the
// LTVector(x,y,z) constructor (0x00412960) and Mag() (0x0041f6a0) out of line.
uint32 CModelRayIntersect::CalcLOD(const LTVector &vCamPos, int32 nLODOffset)
{
	float fDist;
	uint32 i, iLOD;

	fDist = vCamPos.Dist(m_hModel->GetPos());

	if(nLODOffset > 0)
	{
		if(nLODOffset > (int32)m_pModel->m_LODDists.GetSize())
			nLODOffset = m_pModel->m_LODDists.GetSize();

		fDist += *m_pModel->GetLODDist(nLODOffset);
	}
	else if(nLODOffset < 0)
	{
		nLODOffset = -nLODOffset;
		if(nLODOffset < 0)
			nLODOffset = 0;
		else if(nLODOffset > (int32)m_pModel->m_LODDists.GetSize())
			nLODOffset = m_pModel->m_LODDists.GetSize();

		fDist -= *m_pModel->GetLODDist(nLODOffset);
	}

	iLOD = 0;
	for(i=0; i < m_pModel->m_LODDists.GetSize()+1; i++)
	{
		if(fDist > *m_pModel->GetLODDist(i))
			iLOD = i;
	}

	return iLOD;
}


// STUB: LITHTECH 0x0045b0a0
// Two TransformMaker member stores are scheduled differently.
LTBOOL CModelRayIntersect::Setup()
{
	LTMatrix mTransform;

	m_hModel->SetupTransform(mTransform);

	TransformMaker tMaker;
	tMaker.m_pStartMat = &mTransform;
	tMaker.m_hObject = m_hModel;
	((ModelInstance*)m_hModel)->SetupTransformMaker(&tMaker);

	if(!tMaker.SetupTransforms())
	{
		dsi_ConsolePrint("Model::SetupTransforms failed for %s.", m_pModel->GetFilename());
		return LTFALSE;
	}

	return LTTRUE;
}


// STUB: LITHTECH 0x0045b170
// The original keeps aPieces in its argument slot and doesn't duplicate the piece loop test.
LTBOOL CModelRayIntersect::Intersect(HMODELPIECE *aPieces, uint32 nPieceCount, ILTModel::LTRayResult *aRays, uint32 nRayCount)
{
	ModelPiece *pPiece;
	PieceLOD *pLOD;
	uint32 i, iRay, iPiece;

	if(!aPieces || !nPieceCount)
	{
		aPieces = LTNULL;
		nPieceCount = m_pModel->NumPieces();
	}

	for(iRay=0; iRay < nRayCount; iRay++)
	{
		aRays[iRay].m_bIntersect = LTFALSE;
		aRays[iRay].m_fDistance = aRays[iRay].m_fMaxDist;
	}

	for(i=0; i < nPieceCount; i++)
	{
		iPiece = aPieces ? aPieces[i] : i;
		m_iPiece = iPiece;

		pPiece = m_pModel->GetPiece(iPiece);
		if(((ModelInstance*)m_hModel)->m_HiddenPieces & (1 << iPiece))
			continue;

		pLOD = pPiece->GetLOD(m_iLOD);
		if(!SetupArrays(pLOD))
			return LTFALSE;

		TransformVerts(pLOD);
		SetupTris(pLOD);

		for(iRay=0; iRay < nRayCount; iRay++)
			IntersectRay(&aRays[iRay]);
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0045b260
LTBOOL CModelRayIntersect::SetupArrays(PieceLOD *pLOD)
{
	if(pLOD->m_Verts.GetSize() > s_RayVerts.GetSize())
	{
		if(!s_RayVerts.SetSize(pLOD->m_Verts.GetSize() + 32))
			return LTFALSE;
	}

	if(pLOD->m_Tris.GetSize() > s_RayTris.GetSize())
	{
		if(!s_RayTris.SetSize(pLOD->m_Tris.GetSize() + 32))
			return LTFALSE;
	}

	return LTTRUE;
}


// Skins the LOD's vertices with the model's node transforms.
// STUB: LITHTECH 0x0045b3b0
// Structure now matches the original except for: frame size (orig sub esp,0x14: three more dead dwords than our
// push ecx), pLOD/loop registers (orig pLOD ebx, vertex stride edi), where `pWeight = pVert->m_Weights` is loaded
// (orig after the weight-count test), and the order of the four terms per row (orig x: m02v2+m03v3+m01v1+v0*m00,
// y/z/w: m_2v2+m_0v0+m_3v3+m_1v1; VC6 re-sorts the terms of an addition chain regardless of source order, always
// emitting [3],[0],[2],[1] for y/z/w here, so the original's tree must have a different shape).
// What fixed the rest: `for(iWeight=0; iWeight < n; iWeight++)` (countdown with jbe), `pOut[i].x = ...` instead of
// pOut++, and `fInvW = 0.0f; ... w = fInvW;` (the original's 4th accumulator is loaded from the fInvW stack slot).
void CModelRayIntersect::TransformVerts(PieceLOD *pLOD)
{
	LTMatrix *pTransforms;
	ModelVert *pVert;
	NewVertexWeight *pWeight;
	LTMatrix *pMat;
	LTVector *pOut;
	uint32 i, iWeight;
	float x, y, z, w, fInvW;

	pTransforms = m_pModel->m_Transforms.GetArray();
	pOut = s_RayVerts.GetArray();

	for(i=0; i < pLOD->m_Verts.GetSize(); i++)
	{
		pVert = &pLOD->m_Verts[i];

		fInvW = 0.0f;
		x = y = z = 0.0f;
		w = fInvW;
		for(iWeight=0, pWeight = pVert->m_Weights; iWeight < pVert->m_nWeights; iWeight++)
		{
			pMat = &pTransforms[pWeight->m_iNode];
			x += pMat->m[0][2]*pWeight->m_Vec[2] + pMat->m[0][3]*pWeight->m_Vec[3] + pMat->m[0][1]*pWeight->m_Vec[1] + pWeight->m_Vec[0]*pMat->m[0][0];
			y += pMat->m[1][2]*pWeight->m_Vec[2] + pMat->m[1][0]*pWeight->m_Vec[0] + pMat->m[1][3]*pWeight->m_Vec[3] + pMat->m[1][1]*pWeight->m_Vec[1];
			z += pMat->m[2][2]*pWeight->m_Vec[2] + pMat->m[2][0]*pWeight->m_Vec[0] + pMat->m[2][3]*pWeight->m_Vec[3] + pMat->m[2][1]*pWeight->m_Vec[1];
			w += pMat->m[3][2]*pWeight->m_Vec[2] + pMat->m[3][0]*pWeight->m_Vec[0] + pMat->m[3][3]*pWeight->m_Vec[3] + pMat->m[3][1]*pWeight->m_Vec[1];
			pWeight++;
		}

		fInvW = 1.0f / w;
		pOut[i].x = x * fInvW;
		pOut[i].y = y * fInvW;
		pOut[i].z = z * fInvW;
	}
}


// FUNCTION: LITHTECH 0x0045b4f0
void CModelRayIntersect::SetupTris(PieceLOD *pLOD)
{
	LTVector *pVerts;
	ModelTri *pTri;
	RayTri *pRayTri;
	uint32 i;

	pVerts = s_RayVerts.GetArray();
	m_nTris = pLOD->m_Tris.GetSize();

	for(i=0; i < m_nTris; i++)
	{
		pRayTri = &s_RayTris[i];
		pTri = &pLOD->m_Tris[i];

		pRayTri->m_vPt = pVerts[pTri->m_Indices[0]];
		pRayTri->m_vEdge1 = pVerts[pTri->m_Indices[1]] - pRayTri->m_vPt;
		pRayTri->m_vEdge2 = pVerts[pTri->m_Indices[2]] - pRayTri->m_vPt;
	}
}


// Moller-Trumbore ray/triangle test against every prepared triangle.
// STUB: LITHTECH 0x0045b640
// Call structure recovered from the disassembly: vP = e2.Cross(dir) and vQ = e1.Cross(vT) (the by-value
// argument is the vector that gets copied), vNormal = e2.Cross(e1).  The original calls the
// LTVector(x,y,z) constructor out of line in the first Cross and the operator-, Cross out of line for vQ and
// Dot out of line for t (0x00412960, 0x0043eb30, 0x0041f6d0) but inlines the normal's Cross, Mag and *=;
// our inline budget gives all-or-nothing, so the call pattern isn't reproduced yet.
void CModelRayIntersect::IntersectRay(ILTModel::LTRayResult *pRay)
{
	RayTri *pTri;
	LTVector vP, vT, vQ, vNormal;
	float fInvDet, u, v, t, fMag;
	uint32 i;

	for(i=0; i < m_nTris; i++)
	{
		pTri = &s_RayTris[i];

		vP = pTri->m_vEdge2.Cross(pRay->m_vDir);
		fInvDet = 1.0f / pTri->m_vEdge1.Dot(vP);

		vT = pRay->m_vOrigin - pTri->m_vPt;
		u = vT.Dot(vP) * fInvDet;
		if(u < 0.0f || u > 1.0f)
			continue;

		vQ = pTri->m_vEdge1.Cross(vT);
		v = pRay->m_vDir.Dot(vQ) * fInvDet;
		if(v < 0.0f || u + v > 1.0f)
			continue;

		t = pTri->m_vEdge2.Dot(vQ) * fInvDet;
		if(t < pRay->m_fDistance && t > pRay->m_fMinDist)
		{
			pRay->m_fDistance = t;
			pRay->m_bIntersect = LTTRUE;

			vNormal = pTri->m_vEdge2.Cross(pTri->m_vEdge1);
			fMag = vNormal.Mag();
			if(fMag != 0.0f)
			{
				fMag = 1.0f / fMag;
				vNormal *= fMag;
			}

			pRay->m_vNormal = vNormal;
			pRay->m_nPiece = m_iPiece;
		}
	}
}


// ------------------------------------------------------------------------ //
// CMoArray instances (0x0045b930-0x0045c600).
// ------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0045b930 ?GenGetNext@?$CMoArray@URayTri@@VDefaultCache@@@@UBE?AURayTri@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0045b960 ?GenGetAt@?$CMoArray@URayTri@@VDefaultCache@@@@UBE?AURayTri@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0045b990 ?GenAppend@?$CMoArray@URayTri@@VDefaultCache@@@@UAEHAAURayTri@@@Z
// FUNCTION: LITHTECH 0x0045bad0 ?GenRemoveAt@?$CMoArray@URayTri@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0045bbf0 ?GenCopyList@?$CMoArray@URayTri@@VDefaultCache@@@@UAEHABV?$GenList@URayTri@@@@@Z
// FUNCTION: LITHTECH 0x0045bd30 ?GenAppendList@?$CMoArray@URayTri@@VDefaultCache@@@@UAEHABV?$GenList@URayTri@@@@@Z
// FUNCTION: LITHTECH 0x0045be40 ?GenFindElement@?$CMoArray@URayTri@@VDefaultCache@@@@UBEHABURayTri@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0045be70 ?GenAppend@?$CMoArray@V?$_CVector@M@@VDefaultCache@@@@UAEHAAV?$_CVector@M@@@Z
// FUNCTION: LITHTECH 0x0045bfe0 ?GenRemoveAt@?$CMoArray@V?$_CVector@M@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0045c120 ?GenCopyList@?$CMoArray@V?$_CVector@M@@VDefaultCache@@@@UAEHABV?$GenList@V?$_CVector@M@@@@@Z
// FUNCTION: LITHTECH 0x0045c260 ?GenAppendList@?$CMoArray@V?$_CVector@M@@VDefaultCache@@@@UAEHABV?$GenList@V?$_CVector@M@@@@@Z
// FUNCTION: LITHTECH 0x0045c360 ?GenFindElement@?$CMoArray@V?$_CVector@M@@VDefaultCache@@@@UBEHABV?$_CVector@M@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0045c390 ?InternalNiceSetSize@?$CMoArray@URayTri@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x0045c4a0 ?InternalNiceSetSize@?$CMoArray@V?$_CVector@M@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x0045c5b0 ?BaseNew@@YAPAURayTri@@PAVLAlloc@@PAU1@K@Z
// FUNCTION: LITHTECH 0x0045c5d0 ?BaseNew@@YAPAV?$_CVector@M@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x0045c5f0 ??0RayTri@@QAE@XZ
