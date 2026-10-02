// Jupiter runtime/shared/src/transformlt_impl.cpp
// FLAGS: /O2 /GX-
// Talon has no CLTTransform implementation class: the SDK's ILTTransform declares
// non-pure virtuals and the engine defines them directly. LTransform has no scale.
#include "ltbasedefs.h"
#include "ilttransform.h"


// ---------------------------------------------------------------------- //
// Helpers.
// ---------------------------------------------------------------------- //
inline void _TransformToMatrix(LTransform &transform, LTMatrix &mat)
{
	transform.m_Rot.ConvertToMatrix(mat);
	mat.SetTranslation(transform.m_Pos);
}

inline void _TransformFromMatrix(LTransform &transform, LTMatrix &mat)
{
	Mat_GetTranslation(mat, transform.m_Pos);
	transform.m_Rot.ConvertFromMatrix(mat);
}


// FUNCTION: LITHTECH 0x0049c160
LTRESULT ILTTransform::Get(LTransform &transform, LTVector &pos, LTRotation &rot)
{
	pos = transform.m_Pos;
	rot = transform.m_Rot;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0049c1a0
LTRESULT ILTTransform::Set(LTransform &transform, LTVector &pos, LTRotation &rot)
{
	transform.m_Pos = pos;
	transform.m_Rot = rot;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0049c1e0
LTRESULT ILTTransform::GetPos(LTransform &transform, LTVector &pos)
{
	pos = transform.m_Pos;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0049c200
LTRESULT ILTTransform::GetRot(LTransform &transform, LTRotation &rot)
{
	rot = transform.m_Rot;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0049c230
LTRESULT ILTTransform::ToMatrix(LTransform &transform, LTMatrix &mat)
{
	_TransformToMatrix(transform, mat);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0049c280
LTRESULT ILTTransform::FromMatrix(LTransform &transform, LTMatrix &mat)
{
	_TransformFromMatrix(transform, mat);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0049c2b0
LTRESULT ILTTransform::Inverse(LTransform &inverse, LTransform &transform)
{
	LTMatrix mTransform;

	// Talon never inverts the matrix: this just copies transform into inverse.
	_TransformToMatrix(transform, mTransform);
	_TransformFromMatrix(inverse, mTransform);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0049c320
LTRESULT ILTTransform::Multiply(LTransform &out, LTransform &t1, LTransform &t2)
{
	LTMatrix m1, m2, mOut;

	_TransformToMatrix(t1, m1);
	_TransformToMatrix(t2, m2);
	MatMul(&mOut, &m1, &m2);
	_TransformFromMatrix(out, mOut);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0049c3f0
LTRESULT ILTTransform::Difference(LTransform &diff, LTransform &t1, LTransform &t2)
{
	LTMatrix m1, m2, mOut, mInverse;

	// t2 * diff = t1
	// diff = ~t2 * t1
	_TransformToMatrix(t1, m1);
	_TransformToMatrix(t2, m2);
	Mat_InverseTransformation(&m2, &mInverse);
	MatMul(&mOut, &mInverse, &m1);
	_TransformFromMatrix(diff, mOut);
	return LT_OK;
}
