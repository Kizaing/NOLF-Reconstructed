// Jupiter runtime/shared/src/modellt_impl.cpp, Talon version.
// Talon's ILTModel works on LTAnimTracker pointers (no tracker IDs), has hook functions,
// time scales and model lighting, and several functions reuse another's FN_NAME string.
#include <string.h>
#include "bdefs.h"
#include "iltmodel.h"
#include "iltmessage.h"
#include "de_objects.h"
#include "model.h"
#include "animtracker.h"

void quat_ConvertToMatrix(const float *pQuat, float mat[4][4]);
void quat_ConvertFromMatrix(float *pQuat, const float mat[4][4]);


// FUNCTION: LITHTECH 0x004593e0
LTRESULT ILTModel::GetSocket(HOBJECT hObj, char *pSocketName, HMODELSOCKET &hSocket)
{
	ModelInstance *pInst;
	Model *pModel;

	hSocket = INVALID_MODEL_SOCKET;

	CHECK_PARAMS(hObj && pSocketName && hObj->m_ObjectType == OT_MODEL, ILTModel::GetSocket);

	pInst = (ModelInstance*)hObj;
	pModel = pInst->GetModelDB();
	if (!pModel)
	{
		RETURN_ERROR(1, ILTModel::GetSocket, LT_NOTINITIALIZED);
	}

	if (pModel->FindSocket(pSocketName, &hSocket))
	{
		return LT_OK;
	}
	else
	{
		RETURN_ERROR(1, ILTModel::GetSocket, LT_NOTFOUND);
	}
}


// FUNCTION: LITHTECH 0x004594e0
LTRESULT ILTModel::GetNode(HOBJECT hObj, char *pNodeName, HMODELNODE &hNode)
{
	ModelInstance *pInst;
	Model *pModel;

	hNode = INVALID_MODEL_NODE;

	CHECK_PARAMS(hObj && pNodeName && hObj->m_ObjectType == OT_MODEL, ILTModel::GetNode);

	pInst = (ModelInstance*)hObj;
	pModel = pInst->GetModelDB();
	if (!pModel)
	{
		RETURN_ERROR(1, ILTModel::GetNode, LT_NOTINITIALIZED);
	}

	if (pModel->FindNode(pNodeName, &hNode))
	{
		return LT_OK;
	}
	else
	{
		RETURN_ERROR(1, ILTModel::GetNode, LT_NOTFOUND);
	}
}


// FUNCTION: LITHTECH 0x004595e0
LTRESULT ILTModel::GetPiece(HOBJECT hObj, char *pName, HMODELPIECE &hPiece)
{
	FN_NAME(ILTModel::GetPiece);
	ModelInstance *pInst;
	Model *pModel;

	hPiece = INVALID_MODEL_PIECE;

	CHECK_PARAMS2(hObj && pName && hObj->m_ObjectType == OT_MODEL);

	pInst = (ModelInstance*)hObj;
	pModel = pInst->GetModelDB();
	if (!pModel)
	{
		ERR(1, LT_NOTINITIALIZED);
	}

	if (pModel->FindPiece(pName, &hPiece))
	{
		return LT_OK;
	}
	else
	{
		ERR(1, LT_NOTFOUND);
	}
}


// FUNCTION: LITHTECH 0x004596e0
LTRESULT ILTModel::FindWeightSet(HOBJECT hObj, char *pName, HMODELWEIGHTSET &hSet)
{
	FN_NAME(ILTModel::FindWeightSet);
	ModelInstance *pInst;
	Model *pModel;

	hSet = INVALID_MODEL_WEIGHTSET;

	CHECK_PARAMS2(hObj && pName && hObj->m_ObjectType == OT_MODEL);

	pInst = (ModelInstance*)hObj;
	pModel = pInst->GetModelDB();
	if (!pModel)
	{
		ERR(1, LT_NOTINITIALIZED);
	}

	if (pModel->FindWeightSet(pName, &hSet))
	{
		return LT_OK;
	}
	else
	{
		ERR(1, LT_NOTFOUND);
	}
}


// FUNCTION: LITHTECH 0x004597e0
LTRESULT ILTModel::GetModelLighting(HOBJECT hObj, LTVector &vModelLighting)
{
	FN_NAME(ILTModel::GetModelLighting);

	CHECK_PARAMS2(hObj && hObj->m_ObjectType == OT_MODEL);

	vModelLighting = ((ModelInstance*)hObj)->m_ModelLighting;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00459850
LTRESULT ILTModel::GetPieceHideStatus(HOBJECT hObj, HMODELPIECE hPiece, LTBOOL &bHidden)
{
	FN_NAME(ILTModel::GetPieceHideStatus);
	ModelInstance *pInst;

	CHECK_PARAMS2(hObj && hObj->m_ObjectType == OT_MODEL);

	pInst = (ModelInstance*)hObj;
	bHidden = !!(pInst->m_HiddenPieces & (1 << hPiece));

	return LT_OK;
}


// FUNCTION: LITHTECH 0x004598c0
LTRESULT ILTModel::SetPieceHideStatus(HOBJECT hObj, HMODELPIECE hPiece, LTBOOL bHidden)
{
	FN_NAME(ServerModelLT::SetPieceHideStatus);
	ModelInstance *pInst;

	CHECK_PARAMS2(hObj && hObj->m_ObjectType == OT_MODEL);
	pInst = (ModelInstance*)hObj;

	bHidden = !!bHidden;
	if ((uint32)bHidden == !!(pInst->m_HiddenPieces & (1 << hPiece)))
	{
		return LT_NOCHANGE;
	}
	else
	{
		if (bHidden)
		{
			pInst->m_HiddenPieces |= (1 << hPiece);
		}
		else
		{
			pInst->m_HiddenPieces &= ~(1 << hPiece);
		}

		return LT_OK;
	}
}


// FUNCTION: LITHTECH 0x00459970
LTRESULT ILTModel::GetNodeTransform(HOBJECT hObj, HMODELNODE hNode,
	LTransform &transform, LTBOOL bWorldSpace)
{
	FN_NAME(ILTModel::GetNodeTransform);
	ModelInstance *pInst;
	Model *pModel;
	LTMatrix mat, objMat;

	CHECK_PARAMS2(hObj && hObj->m_ObjectType == OT_MODEL);

	pInst = (ModelInstance*)hObj;
	pModel = pInst->GetModelDB();
	if (!pModel || hNode >= pModel->NumNodes())
	{
		ERR(1, LT_NOTINITIALIZED);
	}

	mat = *pInst->GetNodeTransform(hNode);
	if (bWorldSpace)
	{
		pInst->SetupTransform(objMat);
		mat = objMat * mat;
	}

	// Remove the scale.
	if (pInst->m_Scale.x != 1.0f || pInst->m_Scale.y != 1.0f || pInst->m_Scale.z != 1.0f)
	{
		mat.Normalize();
	}

	mat.GetTranslation(transform.m_Pos);
	quat_ConvertFromMatrix((float*)&transform.m_Rot, mat.m);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00459bf0
LTRESULT ILTModel::GetSocketTransform(HOBJECT hObj, HMODELSOCKET hSocket,
	LTransform &transform, LTBOOL bWorldSpace)
{
	FN_NAME(ILTModel::GetSocketTransform);
	ModelInstance *pInst;
	Model *pModel;
	ModelSocket *pSocket;
	LTMatrix *pNodeMat;
	LTMatrix mat, socketMat;

	CHECK_PARAMS2(hObj && hObj->m_ObjectType == OT_MODEL);

	pInst = (ModelInstance*)hObj;
	pModel = pInst->GetModelDB();
	if (!pModel)
	{
		ERR(1, LT_NOTINITIALIZED);
	}

	// Indices past the sockets are nodes.
	if (hSocket >= pModel->NumSockets())
	{
		return GetNodeTransform(hObj, hSocket - pModel->NumSockets(), transform, bWorldSpace);
	}

	pSocket = pModel->GetSocket(hSocket);
	if (pSocket->m_iNode > pModel->NumNodes())
	{
		ERR(1, LT_ERROR);
	}

	pNodeMat = pInst->GetNodeTransform(pSocket->m_iNode);
	quat_ConvertToMatrix((float*)&pSocket->m_Rot, socketMat.m);
	socketMat.SetTranslation(pSocket->m_Pos);
	mat = *pNodeMat * socketMat;

	if (bWorldSpace)
	{
		LTMatrix objMat;
		pInst->SetupTransform(objMat);
		mat = objMat * mat;
	}

	// Remove the scale.
	if (pInst->m_Scale.x != 1.0f || pInst->m_Scale.y != 1.0f || pInst->m_Scale.z != 1.0f)
	{
		mat.Normalize();
	}

	mat.GetTranslation(transform.m_Pos);
	quat_ConvertFromMatrix((float*)&transform.m_Rot, mat.m);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00459f20 ?SetBasisVectors2@LTMatrix@@QAEXPAV?$_CVector@M@@00@Z


// FUNCTION: LITHTECH 0x00459f70
LTRESULT ILTModel::GetInstanceHookFn(HOBJECT hObj, ModelInstanceHookFn &fn)
{
	FN_NAME(ILTModel::GetModelInstanceHookFn);

	CHECK_PARAMS2(hObj && hObj->m_ObjectType == OT_MODEL);

	fn = ((ModelInstance*)hObj)->m_HookFn;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00459fd0
LTRESULT ILTModel::SetInstanceHookFn(HOBJECT hObj, ModelInstanceHookFn fn)
{
	FN_NAME(ILTModel::SetModelInstanceHookFn);

	CHECK_PARAMS2(hObj && hObj->m_ObjectType == OT_MODEL);

	((ModelInstance*)hObj)->m_HookFn = fn;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045a030
LTRESULT ILTModel::GetWeightSet(LTAnimTracker *pTracker, HMODELWEIGHTSET &hSet)
{
	FN_NAME(ILTModel::GetWeightSet);
	CHECK_PARAMS2(pTracker);

	hSet = pTracker->m_TimeRef.m_Cur.m_iWeightSet;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045a090
LTRESULT ILTModel::SetWeightSet(LTAnimTracker *pTracker, HMODELWEIGHTSET hSet)
{
	FN_NAME(ILTModel::SetWeightSet);
	CHECK_PARAMS2(pTracker);

	if (pTracker->m_bAllowInterpolation)
	{
		pTracker->m_TimeRef.m_Prev.m_iWeightSet = pTracker->m_TimeRef.m_Cur.m_iWeightSet;
		pTracker->m_TimeRef.m_Cur.m_iWeightSet = hSet;
	}
	else
	{
		pTracker->m_TimeRef.m_Prev.m_iWeightSet = hSet;
		pTracker->m_TimeRef.m_Cur.m_iWeightSet = hSet;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045a100
LTRESULT ILTModel::GetAllowTransition(LTAnimTracker *pTracker, LTBOOL &bAllowTransition)
{
	FN_NAME(ILTModel::GetAllowTransition);
	CHECK_PARAMS2(pTracker);

	bAllowTransition = pTracker->m_bAllowInterpolation;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045a160
LTRESULT ILTModel::SetAllowTransition(LTAnimTracker *pTracker, LTBOOL bAllowTransition)
{
	FN_NAME(ILTModel::SetAllowTransition);
	CHECK_PARAMS2(pTracker);

	pTracker->m_bAllowInterpolation = bAllowTransition;
	if (!bAllowTransition)
	{
		pTracker->m_TimeRef.m_Prev.m_iWeightSet = pTracker->m_TimeRef.m_Cur.m_iWeightSet;
		pTracker->m_TimeRef.m_Prev.m_iAnim = pTracker->m_TimeRef.m_Cur.m_iAnim;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045a1d0
LTRESULT ILTModel::GetTimeScale(LTAnimTracker *pTracker, LTFLOAT &fTimeScale)
{
	FN_NAME(ILTModel::GetAllowTransition);
	CHECK_PARAMS2(pTracker);

	fTimeScale = (float)pTracker->m_TimeScaleNum / (float)pTracker->m_TimeScaleDenom;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045a250
LTRESULT ILTModel::SetTimeScale(LTAnimTracker *pTracker, LTFLOAT fTimeScale)
{
	FN_NAME(ILTModel::SetAllowTransition);
	CHECK_PARAMS2(pTracker);

	if (fTimeScale < 1.0f)
	{
		pTracker->m_TimeScaleNum = (uint32)(fTimeScale * 100000.0f);
		pTracker->m_TimeScaleDenom = 100000;
	}
	else if (fTimeScale > 1.0f)
	{
		pTracker->m_TimeScaleNum = 100000;
		pTracker->m_TimeScaleDenom = (uint32)(100000.0f / fTimeScale);
	}
	else
	{
		pTracker->m_TimeScaleDenom = 1;
		pTracker->m_TimeScaleNum = 1;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045a310
LTRESULT ILTModel::GetCurAnimLength(LTAnimTracker *pTracker, uint32 &length, LTBOOL bIncorporateScale)
{
	FN_NAME(ILTModel::GetCurAnimLength);
	ModelAnim *pAnim;

	length = 0;
	CHECK_PARAMS2(pTracker);

	pAnim = pTracker->GetCurAnim();
	if (!pAnim)
	{
		ERR(1, LT_NOTINITIALIZED);
	}

	length = pAnim->GetAnimTime();
	if (bIncorporateScale)
	{
		length *= pTracker->m_TimeScaleNum;
		length /= pTracker->m_TimeScaleDenom;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045a3e0
LTRESULT ILTModel::GetCurAnimTime(LTAnimTracker *pTracker, uint32 &curTime, LTBOOL bIncorporateScale)
{
	FN_NAME(ILTModel::GetCurAnimTime);

	curTime = 0;
	CHECK_PARAMS2(pTracker);

	curTime = pTracker->m_TimeRef.m_Cur.m_Time;
	if (bIncorporateScale)
	{
		curTime *= pTracker->m_TimeScaleDenom;
		curTime /= pTracker->m_TimeScaleNum;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045a460
LTRESULT ILTModel::SetCurAnimTime(LTAnimTracker *pTracker, uint32 curTime, LTBOOL bIncorporateScale)
{
	FN_NAME(ILTModel::SetCurAnimTime);
	CHECK_PARAMS2(pTracker);

	if (bIncorporateScale)
	{
		curTime = curTime * pTracker->m_TimeScaleNum / pTracker->m_TimeScaleDenom;
	}

	trk_SetCurTime(pTracker, curTime, pTracker->m_Flags & AT_PLAYING);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045a4e0
LTRESULT ILTModel::AddTracker(HOBJECT hObj, LTAnimTracker *pTracker)
{
	FN_NAME(ILTModel::AddTracker);
	ModelInstance *pInst;
	LTAnimTracker **ppPrev, **ppLast, *pTest;

	CHECK_PARAMS2(hObj && hObj->m_ObjectType == OT_MODEL && pTracker);
	pInst = (ModelInstance*)hObj;

	if (pInst->FindTracker(pTracker, ppPrev))
	{
		ERR(3, LT_ALREADYEXISTS);
	}

	// Find the end of the tracker list.
	ppLast = &pInst->m_AnimTrackers;
	for (pTest=pInst->m_AnimTrackers; pTest; pTest=pTest->GetNext())
	{
		ppLast = (LTAnimTracker**)&pTest->m_Link.m_pNext;
	}

	// Init.
	trk_Init(pTracker, pInst->GetModelDB(), 0);
	pTracker->SetModelInstance(pInst);

	// Add to the list.
	pTracker->SetNext(LTNULL);
	*ppLast = pTracker;

	return LT_OK;
}


// STUB: LITHTECH 0x0045a5e0
// Register allocation differs (vtable load in edx, ppPrev store) throughout.
LTRESULT ILTModel::RemoveTracker(HOBJECT hObj, LTAnimTracker *pTracker)
{
	FN_NAME(ILTModel::RemoveTracker);
	ModelInstance *pInst;
	LTAnimTracker **ppPrev;

	CHECK_PARAMS2(hObj && hObj->m_ObjectType == OT_MODEL && pTracker &&
		pTracker != &((ModelInstance*)hObj)->m_AnimTracker);
	pInst = (ModelInstance*)hObj;

	if (pInst->FindTracker(pTracker, ppPrev))
	{
		// unlink
		*ppPrev = pTracker->GetNext();
	}
	else
	{
		ERR(3, LT_NOTFOUND);
	}

	return LT_OK;
}


// STUB: LITHTECH 0x0045a6a0
// Frame is 0x14 vs 0x10: the original keeps weightSet/bAllowTransition/flags in dead parameter slots.
LTRESULT ILTModel::ReadTracker(LTAnimTracker *pTracker, HMESSAGEREAD hRead)
{
	FN_NAME(ILTModel::ReadTracker);
	uint16 curAnim;
	uint32 curTime, weightSet, timeScaleNum, timeScaleDenom;
	uint8 bAllowTransition;

	CHECK_PARAMS2(hRead && pTracker);

	hRead->ReadWordFL(curAnim);
	hRead->ReadDWordFL(curTime);
	hRead->ReadDWordFL(weightSet);
	hRead->ReadDWordFL(timeScaleNum);
	hRead->ReadDWordFL(timeScaleDenom);
	hRead->ReadByteFL(bAllowTransition);

	pTracker->m_TimeRef.m_Cur.m_iWeightSet = weightSet;
	trk_SetCurAnim(pTracker, curAnim, LTFALSE);
	trk_SetCurTime(pTracker, curTime, LTFALSE);
	pTracker->m_TimeScaleNum = timeScaleNum ? timeScaleNum : 1;
	pTracker->m_TimeScaleDenom = timeScaleDenom ? timeScaleDenom : 1;
	pTracker->m_bAllowInterpolation = bAllowTransition;

	{
		uint16 flags;
		hRead->ReadWordFL(flags);
		pTracker->m_Flags = flags;
	}
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045a7c0
LTRESULT ILTModel::WriteTracker(LTAnimTracker *pTracker, HMESSAGEWRITE hWrite)
{
	FN_NAME(ILTModel::WriteTracker);
	CHECK_PARAMS2(hWrite && pTracker);

	hWrite->WriteWord(pTracker->m_TimeRef.m_Cur.m_iAnim);
	hWrite->WriteDWord(pTracker->m_TimeRef.m_Cur.m_Time);
	hWrite->WriteDWord(pTracker->m_TimeRef.m_Cur.m_iWeightSet);
	hWrite->WriteDWord(pTracker->m_TimeScaleNum);
	hWrite->WriteDWord(pTracker->m_TimeScaleDenom);
	hWrite->WriteByte((uint8)pTracker->m_bAllowInterpolation);
	hWrite->WriteWord(pTracker->m_Flags);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045a870
LTRESULT ILTModel::GetLooping(LTAnimTracker *pTracker)
{
	FN_NAME(ILTModel::GetLooping);
	CHECK_PARAMS2(pTracker);

	return (pTracker->m_Flags & AT_LOOPING) ? LT_YES : LT_NO;
}


// FUNCTION: LITHTECH 0x0045a8d0
LTRESULT ILTModel::SetLooping(LTAnimTracker *pTracker, LTBOOL bLooping)
{
	FN_NAME(ILTModel::SetLooping);
	CHECK_PARAMS2(pTracker);

	if (bLooping)
		pTracker->m_Flags |= AT_LOOPING;
	else
		pTracker->m_Flags &= ~AT_LOOPING;

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045a930
LTRESULT ILTModel::GetPlaying(LTAnimTracker *pTracker)
{
	FN_NAME(ILTModel::GetPlaying);
	CHECK_PARAMS2(pTracker);

	return (pTracker->m_Flags & AT_PLAYING) ? LT_YES : LT_NO;
}


// FUNCTION: LITHTECH 0x0045a980
LTRESULT ILTModel::SetPlaying(LTAnimTracker *pTracker, LTBOOL bPlaying)
{
	FN_NAME(ILTModel::SetPlaying);
	CHECK_PARAMS2(pTracker);

	if (bPlaying)
		pTracker->m_Flags |= AT_PLAYING;
	else
		pTracker->m_Flags &= ~AT_PLAYING;

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045a9e0
LTRESULT ILTModel::GetCurAnim(LTAnimTracker *pTracker, HMODELANIM &hAnim)
{
	FN_NAME(ILTModel::GetCurAnim);
	CHECK_PARAMS2(pTracker);

	hAnim = pTracker->m_TimeRef.m_Cur.m_iAnim;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045aa40
LTRESULT ILTModel::SetCurAnim(LTAnimTracker *pTracker, HMODELANIM hAnim)
{
	FN_NAME(ILTModel::SetCurAnim);
	ModelInstance *pInst;

	pInst = pTracker->GetModelInstance();
	if (pInst)
	{
		trk_SetCurAnim(pTracker, hAnim, !!(pInst->m_Flags & FLAG_ANIMTRANSITION));
		return LT_OK;
	}
	else
	{
		RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
	}
}


// FUNCTION: LITHTECH 0x0045aab0
LTRESULT ILTModel::ResetAnim(LTAnimTracker *pTracker)
{
	FN_NAME(ILTModel::ResetAnim);
	CHECK_PARAMS2(pTracker);

	trk_Reset(pTracker);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045ab10
LTRESULT ILTModel::GetPlaybackState(LTAnimTracker *pTracker, uint32 &flags)
{
	FN_NAME(ILTModel::GetPlaybackState);

	flags = 0;
	CHECK_PARAMS2(pTracker);

	flags = 0;
	if (trk_IsStopped(pTracker))
		flags |= MS_PLAYDONE;

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045ab80
LTRESULT ILTModel::GetMainTracker(HOBJECT hObj, LTAnimTracker* &pTracker)
{
	FN_NAME(ILTModel::GetMainTracker);

	pTracker = LTNULL;
	CHECK_PARAMS2(hObj && hObj->m_ObjectType == OT_MODEL);

	pTracker = &((ModelInstance*)hObj)->m_AnimTracker;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045abf0
LTRESULT ILTModel::UpdateMainTracker(HOBJECT hObj, LTFLOAT fUpdateDelta)
{
	LTAnimTracker *pTracker;
	LTRESULT dResult;

	dResult = GetMainTracker(hObj, pTracker);
	if (dResult != LT_OK)
		return dResult;

	trk_Update(pTracker, (uint32)(fUpdateDelta * 1000.0f));
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0045ac30
LTRESULT ILTModel::GetNormalize(LTAnimTracker *pTracker)
{
	FN_NAME(ILTModel::GetLooping);
	CHECK_PARAMS2(pTracker);

	return pTracker->m_TimeRef.m_bNormalize;
}


// FUNCTION: LITHTECH 0x0045ac80
LTRESULT ILTModel::SetNormalize(LTAnimTracker *pTracker, LTBOOL bNormalize)
{
	FN_NAME(ILTModel::SetLooping);
	CHECK_PARAMS2(pTracker);

	pTracker->m_TimeRef.m_bNormalize = bNormalize;
	return LT_OK;
}


// Model ray intersector (code at 0x0045af30, outside this unit).
class CModelRayIntersect
{
public:
	LTBOOL	Init(HOBJECT hModel, const LTVector &vCamPos, int32 nLODOffset);	// 0x0045af30
	LTBOOL	Setup();																// 0x0045b0a0
	LTBOOL	Intersect(HMODELPIECE *aPieces, uint32 nPieceCount, ILTModel::LTRayResult *aRays, uint32 nRayCount);	// 0x0045b170

	uint8	m_Data[0x14];
};

// FUNCTION: LITHTECH 0x0045acd0
LTRESULT ILTModel::IntersectRays(HOBJECT hModel, const LTVector &vCamPos, int32 nLODOffset,
	HMODELPIECE *aPieces, uint32 nPieceCount, LTRayResult *aRays, uint32 nRayCount)
{
	FN_NAME(ILTModel::IntersectRays);
	CModelRayIntersect cIntersect;

	CHECK_PARAMS2(hModel && hModel->m_ObjectType == OT_MODEL);
	CHECK_PARAMS2(aRays && nRayCount);

	if (!cIntersect.Init(hModel, vCamPos, nLODOffset))
		return LT_ERROR;

	if (!cIntersect.Setup())
		return LT_ERROR;

	cIntersect.Intersect(aPieces, nPieceCount, aRays, nRayCount);
	return LT_OK;
}
