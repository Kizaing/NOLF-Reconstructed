// Talon-only model ray intersection (ILTModel::IntersectRays).
// Layout recovered from lithtech.exe 0x0045af30-0x0045b930.
#ifndef __MODELRAYINTERSECT_H__
#define __MODELRAYINTERSECT_H__

#include "iltmodel.h"
#include "ltdynarray.h"

class Model;
class PieceLOD;

// One triangle prepared for ray tests (0x24 bytes).
struct RayTri
{
	LTVector	m_vPt;			// 0x00 first corner
	LTVector	m_vEdge1;		// 0x0c second corner - first corner
	LTVector	m_vEdge2;		// 0x18 third corner - first corner
};

class CModelRayIntersect
{
public:
	LTBOOL	Init(HOBJECT hModel, const LTVector &vCamPos, int32 nLODOffset);	// 0x0045af30
	uint32	CalcLOD(const LTVector &vCamPos, int32 nLODOffset);				// 0x0045af60 (name unknown)
	LTBOOL	Setup();															// 0x0045b0a0
	LTBOOL	Intersect(HMODELPIECE *aPieces, uint32 nPieceCount, ILTModel::LTRayResult *aRays, uint32 nRayCount);	// 0x0045b170

	LTBOOL	SetupArrays(PieceLOD *pLOD);		// 0x0045b260 (name unknown)
	void	TransformVerts(PieceLOD *pLOD);		// 0x0045b3b0 (name unknown)
	void	SetupTris(PieceLOD *pLOD);			// 0x0045b4f0 (name unknown)
	void	IntersectRay(ILTModel::LTRayResult *pRay);	// 0x0045b640 (name unknown)

	// The transformed vertices and prepared triangles of the piece LOD being tested.
	static CMoArray<LTVector>	s_RayVerts;		// 0x004e453c
	static CMoArray<RayTri>		s_RayTris;		// 0x004e4528

	uint32		m_nTris;		// 0x00
	HOBJECT		m_hModel;		// 0x04
	Model		*m_pModel;		// 0x08
	uint32		m_iLOD;			// 0x0c
	uint32		m_iPiece;		// 0x10
};

#endif
