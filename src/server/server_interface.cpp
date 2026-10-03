// FLAGS: /O2 /D__STL_NO_EXCEPTION_HEADER /D__STL_NO_NEW_NEW_HEADER /D__STL_NO_BAD_ALLOC /IE:/AVP2Source/build/proj/LT2/lithshared/stl /IE:/MSVC6/VC98/MFC
// Talon's server-side ILT interface implementations (the classes CLTServer embeds: SPhysicsLT,
// ServerCommonLT, ServerModelLT and ServerLightAnimLT; Jupiter split them into
// server_iltphysics.cpp, server_iltcommon.cpp and server_iltmodel.cpp), plus si_GetPointShade and
// CreateLTServer.
#include <string.h>
#include "bdefs.h"
#include "servermgr.h"
#include "s_object.h"
#define SERVERDE_STL
#include "serverde_impl.h"
#include "shared_iltcommon.h"
#include "iltmodel.h"
#include "iltphysics.h"
#include "iltlightanim.h"
#include "smoveabstract.h"
#include "moveobject.h"
#include "motion.h"
#include "impl_common.h"
#include "animtracker.h"
#include "packet.h"
#include "s_client.h"
#include "server_extradata.h"
#include "model.h"

// Console variable "DebugMaxDims" (3000).
// GLOBAL: LITHTECH 0x004d2208
extern float g_CV_DebugMaxDims;

void sm_SetLightAnimChanged(CServerMgr *pServerMgr, uint32 iLightAnim, uint32 flags);	// s_client, 0x00473550


// A world light (the MainWorld::m_StaticLights list). Talon layout, only what's used here.
struct StaticLight
{
	uint8		m_Pad00[0x68];		// WorldTreeObj, list link
	LTVector	m_Pos;				// 0x68
	float		m_Radius;			// 0x74
	LTVector	m_Color;			// 0x78 0-255
	LTVector	m_Dir;				// 0x84 Normalized direction vector for directional lights
	float		m_FOV;				// 0x90 cos(fov/2), -1 for omnidirectional lights
	LTVector	m_InnerColor;		// 0x94 color at the center of a directional light's cone
};

// Light table lookup result (clientde_impl.cpp has the same).
struct LTRGBColor
{
	uint8	b, g, r, a;
};

void w_GetLightVal(CLightTable *pTable, LTVector *pPos, LTRGBColor *pRGB);	// 0x00405980


// FUNCTION: LITHTECH 0x004795f0
LTBOOL si_GetPointShade(LTVector *pPoint, LTVector *pColor)
{
	LTRGBColor rgb;
	LTLink *pCur, *pListHead;
	StaticLight *pLight;
	LTVector vDelta;
	float fDist, fDot, t;

	if (!pColor || !pPoint)
		return LTFALSE;

	w_GetLightVal(&g_pServerMgr->m_World.m_LightTable, pPoint, &rgb);
	pColor->x = rgb.r;
	pColor->y = rgb.g;
	pColor->z = rgb.b;

	// Add the static lights.
	pListHead = &g_pServerMgr->m_World.m_StaticLights;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		pLight = (StaticLight*)pCur->m_pData;

		vDelta = *pPoint - pLight->m_Pos;
		fDist = vDelta.Mag();
		if (fDist < pLight->m_Radius)
		{
			if (pLight->m_FOV > -1.0f && pLight->m_FOV < 1.0f && fDist != 0.0f)
			{
				// Directional light: fade from the inner color to the color at the edge of the cone.
				fDot = VEC_DOT(vDelta, pLight->m_Dir) * (1.0f / fDist);
				if (fDot >= pLight->m_FOV)
				{
					t = (1.0f - (fDot + 1.0f) * 0.5f) / (1.0f - (pLight->m_FOV + 1.0f) * 0.5f);
					*pColor += pLight->m_InnerColor * t + pLight->m_Color * (1.0f - t);
				}
			}
			else
			{
				*pColor += pLight->m_Color;
			}
		}
	}

	if (pColor->x > 255.0f)
		pColor->x = 255.0f;

	if (pColor->y > 255.0f)
		pColor->y = 255.0f;

	if (pColor->z > 255.0f)
		pColor->z = 255.0f;

	return LTTRUE;
}


// ----------------------------------------------------------------------- //
// The interface classes.
// ----------------------------------------------------------------------- //

// Creates the server's ILT interface (the CLTServer constructor, in serverde_impl.h, is inline).
// si_SetupFunctionPointers is called here, inside a null test that merges with operator new's, not at the end of
// the constructor: that is what restores ebx/ebp/edi before the call, as in the original. The constructor's pointer
// assignments are in the original's order (model, transform, physics).
// Remaining difference: the original builds the child model link map (member at 0x280) with _M_empty_initialize
// (0x00487d10) out of line. That needs 5-8 code-free ballast statements (if(0) x = 0;) anywhere before the map
// (e.g. in the ServerLightAnimLT or ServerModelLT constructor): with them this is a MATCH. 1-4 are too few, 16 too
// many; pending free calls in CreateLTServer don't change anything, and after the map they un-inline the whole map
// constructor. The real source of that cost is unknown.
// STUB: LITHTECH 0x004798b0
ILTServer* CreateLTServer(CServerMgr *pServerMgr)
{
	CLTServer *pServer = new CLTServer(pServerMgr);
	if (pServer)
		si_SetupFunctionPointers(pServer);
	return pServer;
}

// FUNCTION: LITHTECH 0x004799f0 ??_GILTServer@@MAEPAXI@Z

// ----------------------------------------------------------------------- //
// SPhysicsLT
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00479a10
LTRESULT SPhysicsLT::SetVelocity(HOBJECT hObj, LTVector *pVel)
{
	if (!hObj || !pVel)
	{
		RETURN_ERROR(1, ILTPhysics::SetVelocity, LT_INVALIDPARAMS);
	}

	if (hObj->m_Velocity.DistSqr(*pVel) < 0.001f)
		return LT_OK;

	hObj->m_InternalFlags |= IFLAG_APPLYPHYSICS;
	hObj->m_Velocity = *pVel;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00479b00
LTRESULT SPhysicsLT::SetAcceleration(HOBJECT hObj, LTVector *pAccel)
{
	if (!hObj)
	{
		RETURN_ERROR(1, ILTPhysics::SetAcceleration, LT_INVALIDPARAMS);
	}

	if (hObj->m_Acceleration.DistSqr(*pAccel) < 0.001f)
		return LT_OK;

	hObj->m_InternalFlags |= IFLAG_APPLYPHYSICS;
	hObj->m_Acceleration = *pAccel;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00479bd0
LTRESULT SPhysicsLT::MoveObject(HOBJECT hObj, LTVector *pPos, uint32 flags)
{
	uint32 moveFlags;

	if (!hObj)
	{
		RETURN_ERROR(1, ILTPhysics::MoveObject, LT_INVALIDPARAMS);
	}

	moveFlags = MO_DETACHSTANDING | MO_SETCHANGEFLAG | MO_MOVESTANDINGONS;
	if (flags & MOVEOBJECT_TELEPORT)
		moveFlags |= MO_TELEPORT;

	if (flags & MOVEOBJECT_NCTELEPORT)
		moveFlags |= MO_NOSLIDING;

	FullMoveObject(m_pServerMgr, hObj, pPos, moveFlags);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00479c50
LTRESULT SPhysicsLT::SetObjectDims(HOBJECT hObj, LTVector *pNewDims, uint32 flags)
{
	LTVector newDims;
	MoveState moveState;

	newDims = *pNewDims;

	if (hObj)
	{
		if (pNewDims->x > g_CV_DebugMaxDims || pNewDims->y > g_CV_DebugMaxDims || pNewDims->z > g_CV_DebugMaxDims)
		{
			dsi_ConsolePrint("ILTPhysics::SetObjectDims: dims larger than 'DebugMaxDims'");
			dsi_ConsolePrint("Object class: %s, dims (%.2f, %.2f, %.2f)", hObj->sd->m_pClass->m_ClassName,
				pNewDims->x, pNewDims->y, pNewDims->z);
		}

		// Not allowed to change a WorldModel's dimensions!
		if (hObj->m_ObjectType != OT_CONTAINER && hObj->m_ObjectType != OT_WORLDMODEL)
		{
			moveState.Setup(&m_pServerMgr->m_World.m_WorldTree, m_pServerMgr->m_MoveAbstract, hObj, hObj->m_BPriority);
			if (ChangeObjectDimensions(&moveState, &newDims, flags & SETDIMS_PUSHOBJECTS, LTTRUE))
			{
				return LT_OK;
			}
			else
			{
				*pNewDims = newDims;
				return LT_ERROR;
			}
		}
		else
		{
			return LT_INVALIDPARAMS;
		}
	}

	RETURN_ERROR(2, SPhysicsLT::SetObjectDims, LT_INVALIDPARAMS);
}


// FUNCTION: LITHTECH 0x00479df0
LTRESULT SPhysicsLT::GetGlobalForce(LTVector &vec)
{
	vec = m_pServerMgr->GetMotionState()->m_Info.m_Force;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00479e20
LTRESULT SPhysicsLT::SetGlobalForce(LTVector &vec)
{
	MotionState *pState;

	pState = m_pServerMgr->GetMotionState();
	pState->m_Info.m_Force = vec;
	pState->m_Info.m_ForceMag = pState->m_Info.m_Force.Mag();
	if (pState->m_Info.m_ForceMag > 0.00001f)
	{
		pState->m_Info.m_UnitForce = pState->m_Info.m_Force;
		pState->m_Info.m_UnitForce /= pState->m_Info.m_ForceMag;
	}
	else
	{
		pState->m_Info.m_UnitForce.Init();
	}

	return LT_OK;
}


// ----------------------------------------------------------------------- //
// ServerCommonLT
// ----------------------------------------------------------------------- //

// The poly an HPOLY refers to (world model index in the high word).
static inline WorldPoly* w_GetPolyFromHPoly(MainWorld *pWorld, HPOLY hPoly)
{
	uint32 iModel;
	WorldData *pWorldData;

	iModel = hPoly >> 16;
	if (iModel >= pWorld->m_WorldModels.GetSize())
		return LTNULL;

	pWorldData = pWorld->m_WorldModels[iModel];
	if (!pWorldData)
		return LTNULL;

	return pWorldData->m_pOriginalBsp->GetPolyFromHPoly(hPoly);
}


// STUB: LITHTECH 0x00479ec0
// The original pushes edi after the parameter check; this pushes it in the prologue.
LTRESULT ServerCommonLT::SetObjectFlags(HOBJECT hObj, const ObjFlagType flagType, uint32 dwFlags)
{
	FN_NAME(ServerCommonLT::SetObjectFlags);
	uint32 oldFlags;

	CHECK_PARAMS2(hObj);

	if (flagType == OFT_Flags)
	{
		if (hObj->m_Flags == dwFlags)
			return LT_OK;

		// They changed a FLAGS_.
		hObj->m_InternalFlags |= IFLAG_APPLYPHYSICS;

		// If we're going to nonsolid, get rid of anything standing on us.
		if ((hObj->m_Flags & FLAG_SOLID) && !(dwFlags & FLAG_SOLID))
		{
			DetachObjectStanding(hObj);
		}

		// Only tell clients if it changes a flag relevant to them.
		if ((hObj->m_Flags ^ dwFlags) & CLIENT_FLAGMASK)
		{
			SetObjectChangeFlags(m_pServerMgr, hObj, CF_FLAGS);
		}

		oldFlags = hObj->m_Flags;
		hObj->m_Flags = dwFlags;

		// If they turned on real world model physics, retransform the world model.
		if (hObj->HasWorldModel() && (oldFlags & FLAG_BOXPHYSICS) && !(dwFlags & FLAG_BOXPHYSICS))
		{
			RetransformWorldModel((WorldModelInstance*)hObj);
		}

		sm_UpdateInBspStatus(m_pServerMgr, hObj);
	}
	else
	{
		if (hObj->m_Flags2 == dwFlags)
			return LT_OK;

		hObj->m_Flags2 = dwFlags;
		SetObjectChangeFlags(m_pServerMgr, hObj, CF_FLAGS);
	}

	return LT_OK;
}


// Changes a model's or sprite's files and tells the clients that know about the object.
// FUNCTION: LITHTECH 0x0047a190
LTRESULT ServerCommonLT::SetObjectFilenames(HOBJECT pObj, ObjectCreateStruct *pStruct)
{
	FN_NAME(ServerCommonLT::SetObjectFilenames);
	LTRESULT dResult;
	Model *pOldModel, *pNewModel;
	Attachment *pAttachment;
	uint32 newSocketIndex;
	LTLink *pCur, *pListHead;
	ExtraDataBackup backup;

	CHECK_PARAMS2(pStruct && pObj &&
		(pObj->m_ObjectType == OT_MODEL || pObj->m_ObjectType == OT_SPRITE));

	pOldModel = LTNULL;
	if (pObj->m_ObjectType == OT_MODEL)
		pOldModel = ((ModelInstance*)pObj)->GetModelDB();

	CPacketRef cChangePacket;
	cChangePacket = packet_Get(MAX_PACKET_LEN, MAX_PACKET_LEN);

	// Setup the new files (this terminates the old ones).
	BackupExtraData(pObj, &backup);
	sm_TermExtraData(m_pServerMgr, pObj);
	dResult = sm_InitExtraData(m_pServerMgr, pObj, pStruct);
	if (dResult != LT_OK)
	{
		RestoreExtraData(pObj, &backup);
		return dResult;
	}

	// Tell all the clients about it.
	cChangePacket->ResetWrite();
	cChangePacket->WriteType(pObj->m_ObjectID);
	sm_WriteModelFiles(pObj, cChangePacket, LTNULL);

	pListHead = &m_pServerMgr->m_Clients.m_Head;
	for (pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
	{
		Client *pClient = (Client*)pCur->m_pData;

		if (pClient->m_ObjInfos[pObj->m_ObjectID].m_ChangeFlags & CF_SENTINFO)
		{
			SendToClient(m_pServerMgr, pClient, 15, cChangePacket, LTFALSE, MESSAGE_GUARANTEED);
		}
	}

	// If we've changed models, then reorder the attachment socket node bindings.
	if (pOldModel)
	{
		pNewModel = ((ModelInstance*)pObj)->GetModelDB();

		for (pAttachment = pObj->m_Attachments; pAttachment; pAttachment = pAttachment->m_pNext)
		{
			if (pAttachment->m_iSocket < pOldModel->NumSockets())
			{
				if (pNewModel->FindSocket(pOldModel->GetSocket(pAttachment->m_iSocket)->m_Name, &newSocketIndex))
				{
					pAttachment->m_iSocket = newSocketIndex;
				}
			}
			// Look for it in the node list if it's not in the socket list.
			else if (pAttachment->m_iSocket < (pOldModel->NumSockets() + pOldModel->NumNodes()))
			{
				if (pNewModel->FindNode(pOldModel->GetNode(pAttachment->m_iSocket - pOldModel->NumSockets())->GetName(),
					&newSocketIndex))
				{
					pAttachment->m_iSocket = newSocketIndex + pNewModel->NumSockets();
				}
			}
		}
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0047a3c0
LTRESULT ServerCommonLT::GetPointStatus(LTVector *pPoint)
{
	if (ic_IsPointInWorld(&m_pServerMgr->m_World.m_WorldTree, pPoint))
		return LT_INSIDE;
	else
		return LT_OUTSIDE;
}


// FUNCTION: LITHTECH 0x0047a3f0
LTRESULT ServerCommonLT::GetPointShade(LTVector *pPoint, LTVector *pColor)
{
	return si_GetPointShade(pPoint, pColor);
}


// FUNCTION: LITHTECH 0x0047a410
LTRESULT ServerCommonLT::GetPolyTextureFlags(HPOLY hPoly, uint32 *pFlags)
{
	WorldPoly *pPoly;

	*pFlags = 0;

	pPoly = w_GetPolyFromHPoly(&m_pServerMgr->m_World, hPoly);
	if (pPoly)
	{
		*pFlags = ((Surface*)pPoly->m_pSurface)->m_TextureFlags;
		return LT_OK;
	}

	RETURN_ERROR(2, CommonLT::GetPolyTextureFlags, LT_ERROR);
}


// FUNCTION: LITHTECH 0x0047a4a0
LTRESULT ServerCommonLT::GetPolyInfo(HPOLY hPoly, LTPlane **ppPlane, LTVector *pVertexList,
	uint32 nVertexListMaxSize, uint32 *pnNumVertices)
{
	WorldPoly *pPoly;
	uint32 nVertices, i;

	pPoly = w_GetPolyFromHPoly(&m_pServerMgr->m_World, hPoly);
	if (pPoly)
	{
		if (ppPlane)
			*ppPlane = pPoly->m_pPlane;

		nVertices = pPoly->m_nVertices;
		if (pVertexList)
		{
			for (i=0; i < nVertices && i < nVertexListMaxSize; i++)
			{
				*pVertexList = *((SPolyVertex*)(pPoly + 1))[i].m_Vec;
				pVertexList++;
			}
		}

		if (pnNumVertices)
			*pnNumVertices = nVertices;

		return LT_OK;
	}

	RETURN_ERROR(2, CommonLT::GetPolyInfo, LT_ERROR);
}


// FUNCTION: LITHTECH 0x0047a570
LTRESULT ServerCommonLT::GetPolySurfaceFlags(HPOLY hPoly, uint32 &dwSurfFlags)
{
	WorldPoly *pPoly;

	pPoly = w_GetPolyFromHPoly(&m_pServerMgr->m_World, hPoly);
	if (pPoly && pPoly->m_pSurface)
	{
		dwSurfFlags = ((Surface*)pPoly->m_pSurface)->m_Flags;
		return LT_OK;
	}

	RETURN_ERROR(2, CommonLT::GetPolySurfaceFlags, LT_ERROR);
}


// FUNCTION: LITHTECH 0x0047a600
LTRESULT ServerCommonLT::CreateMessage(ILTMessage* &pMsg)
{
	pMsg = &m_pServerMgr->AllocPacket()->m_Message;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0047a620
LTRESULT ServerCommonLT::GetAttachmentObjects(HATTACHMENT hAttachment, HOBJECT &hParent, HOBJECT &hChild)
{
	FN_NAME(ServerCommonLT::GetAttachmentObjects);
	Attachment *pAttachment;

	pAttachment = (Attachment*)hAttachment;
	if (!pAttachment)
		ERR(1, LT_INVALIDPARAMS);

	hParent = sm_FindObject(m_pServerMgr, pAttachment->m_nParentID);
	hChild = sm_FindObject(m_pServerMgr, pAttachment->m_nChildID);

	if (hParent && hChild)
		return LT_OK;
	else
		ERR(1, LT_NOTINITIALIZED);
}


// ----------------------------------------------------------------------- //
// ServerModelLT
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0047a6f0
LTRESULT ServerModelLT::AddTracker(HOBJECT hObj, LTAnimTracker *pTracker)
{
	LTRESULT dResult;

	dResult = ILTModel::AddTracker(hObj, pTracker);
	if (dResult == LT_OK)
	{
		SetObjectChangeFlags(m_pServerMgr, hObj, CF_MODELINFO);
	}

	return dResult;
}


// FUNCTION: LITHTECH 0x0047a800
LTRESULT ServerModelLT::RemoveTracker(HOBJECT hObj, LTAnimTracker *pTracker)
{
	LTRESULT dResult;

	dResult = ILTModel::RemoveTracker(hObj, pTracker);
	if (dResult == LT_OK)
	{
		SetObjectChangeFlags(m_pServerMgr, hObj, CF_MODELINFO);
	}

	return dResult;
}


// FUNCTION: LITHTECH 0x0047a910
LTRESULT ServerModelLT::SetLooping(LTAnimTracker *pTracker, LTBOOL bLooping)
{
	FN_NAME(ServerModelLT::SetLooping);
	LTObject *pObj;
	LTRESULT dResult;

	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		dResult = ILTModel::SetLooping(pTracker, bLooping);
		if (dResult == LT_OK)
		{
			SetObjectChangeFlags(m_pServerMgr, pObj, CF_MODELINFO);
		}

		return dResult;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}


// FUNCTION: LITHTECH 0x0047aa70
LTRESULT ServerModelLT::SetPlaying(LTAnimTracker *pTracker, LTBOOL bPlaying)
{
	FN_NAME(ServerModelLT::SetPlaying);
	LTObject *pObj;
	LTRESULT dResult;

	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		dResult = ILTModel::SetPlaying(pTracker, bPlaying);
		if (dResult == LT_OK)
		{
			SetObjectChangeFlags(m_pServerMgr, pObj, CF_MODELINFO);
		}

		return dResult;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}


// FUNCTION: LITHTECH 0x0047abd0
LTRESULT ServerModelLT::SetCurAnimTime(LTAnimTracker *pTracker, uint32 curTime)
{
	FN_NAME(ServerModelLT::SetCurAnimTime);
	LTObject *pObj;
	LTRESULT dResult;

	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		dResult = ILTModel::SetCurAnimTime(pTracker, curTime, LTTRUE);
		if (dResult == LT_OK)
		{
			trk_SetAtKeyFrame(pTracker, pTracker->m_TimeRef.m_Cur.m_Time);
			SetObjectChangeFlags(m_pServerMgr, pObj, CF_RESETANIM);
		}

		return dResult;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}


// FUNCTION: LITHTECH 0x0047ad40
LTRESULT ServerModelLT::SetCurAnim(LTAnimTracker *pTracker, HMODELANIM hAnim)
{
	FN_NAME(ServerModelLT::SetCurAnim);
	LTObject *pObj;
	LTRESULT dResult;

	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		dResult = ILTModel::SetCurAnim(pTracker, hAnim);
		if (dResult == LT_OK)
		{
			SetObjectChangeFlags(m_pServerMgr, pObj, CF_MODELINFO);
		}

		return dResult;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}


// FUNCTION: LITHTECH 0x0047aea0
LTRESULT ServerModelLT::ResetAnim(LTAnimTracker *pTracker)
{
	FN_NAME(ServerModelLT::ResetAnim);
	LTObject *pObj;
	LTRESULT dResult;

	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		dResult = ILTModel::ResetAnim(pTracker);
		if (dResult == LT_OK)
		{
			SetObjectChangeFlags(m_pServerMgr, pObj, CF_RESETANIM);
		}

		return dResult;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}


// FUNCTION: LITHTECH 0x0047b000
LTRESULT ServerModelLT::SetWeightSet(LTAnimTracker *pTracker, HMODELWEIGHTSET hSet)
{
	FN_NAME(ServerModelLT::SetWeightSet);
	LTObject *pObj;
	LTRESULT dResult;

	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		dResult = ILTModel::SetWeightSet(pTracker, hSet);
		if (dResult == LT_OK)
		{
			SetObjectChangeFlags(m_pServerMgr, pObj, CF_MODELINFO);
		}

		return dResult;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}


// FUNCTION: LITHTECH 0x0047b160
LTRESULT ServerModelLT::SetPieceHideStatus(HOBJECT hObj, HMODELPIECE hPiece, LTBOOL bHidden)
{
	LTRESULT dResult;

	dResult = ILTModel::SetPieceHideStatus(hObj, hPiece, bHidden);
	if (dResult == LT_OK)
	{
		SetObjectChangeFlags(m_pServerMgr, hObj, CF_ATTACHMENTS);
	}

	return dResult;
}


// FUNCTION: LITHTECH 0x0047b280
LTRESULT ServerModelLT::SetAllowTransition(LTAnimTracker *pTracker, LTBOOL bAllowTransition)
{
	FN_NAME(ServerModelLT::SetAllowTransition);
	LTObject *pObj;
	LTRESULT dResult;

	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		dResult = ILTModel::SetAllowTransition(pTracker, bAllowTransition);
		if (dResult == LT_OK)
		{
			SetObjectChangeFlags(m_pServerMgr, pObj, CF_MODELINFO);
		}

		return dResult;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}


// FUNCTION: LITHTECH 0x0047b3e0
LTRESULT ServerModelLT::SetTimeScale(LTAnimTracker *pTracker, LTFLOAT fTimeScale)
{
	FN_NAME(ServerModelLT::SetTimeScale);
	LTObject *pObj;
	LTRESULT dResult;

	pObj = (LTObject*)pTracker->GetModelInstance();
	if (pObj)
	{
		dResult = ILTModel::SetTimeScale(pTracker, fTimeScale);
		if (dResult == LT_OK)
		{
			SetObjectChangeFlags(m_pServerMgr, pObj, CF_MODELINFO);
		}

		return dResult;
	}

	RETURN_ERROR_NO_TOKEN_PASTE(2, ___bdefs__pFnName, LT_INVALIDPARAMS);
}


// ----------------------------------------------------------------------- //
// ServerLightAnimLT
// ----------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0047b540
LTRESULT ServerLightAnimLT::FindLightAnim(const char *pName, HLIGHTANIM &hLightAnim)
{
	if (m_pServerMgr->m_World.FindLightAnim(pName, (uint32*)&hLightAnim))
	{
		return LT_OK;
	}
	else
	{
		hLightAnim = INVALID_LIGHT_ANIM;
		return LT_NOTFOUND;
	}
}


// FUNCTION: LITHTECH 0x0047b580
LTRESULT ServerLightAnimLT::GetNumFrames(HLIGHTANIM hLightAnim, uint32 &nFrames)
{
	FN_NAME(ServerLightAnimLT::GetNumFrames);

	nFrames = 0;
	CHECK_PARAMS2((uint32)hLightAnim < m_pServerMgr->m_World.m_LightAnims.GetSize());

	nFrames = m_pServerMgr->m_World.m_LightAnims[(uint32)hLightAnim].m_nFrames;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0047b5f0
LTRESULT ServerLightAnimLT::GetLightAnimInfo(HLIGHTANIM hLightAnim, LAInfo &info)
{
	FN_NAME(ServerLightAnimLT::GetLightAnimInfo);

	CHECK_PARAMS2((uint32)hLightAnim < m_pServerMgr->m_World.m_LightAnims.GetSize());

	la_GetInfo(&m_pServerMgr->m_World.m_LightAnims[(uint32)hLightAnim], &info);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0047b660
LTRESULT ServerLightAnimLT::SetLightAnimInfo(HLIGHTANIM hLightAnim, LAInfo &info)
{
	FN_NAME(ServerLightAnimLT::SetLightAnimInfo);
	LightAnim *pAnim;
	uint32 changed;
	LTBOOL bChanged;

	CHECK_PARAMS2((uint32)hLightAnim < m_pServerMgr->m_World.m_LightAnims.GetSize());

	pAnim = &m_pServerMgr->m_World.m_LightAnims[(uint32)hLightAnim];
	bChanged = la_InfoChanged(pAnim, &info, &changed);
	la_SetInfo(pAnim, &info);

	if (bChanged)
	{
		sm_SetLightAnimChanged(m_pServerMgr, (uint32)hLightAnim, changed);
	}

	return LT_OK;
}
