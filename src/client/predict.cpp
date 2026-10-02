// Jupiter runtime/client/src/predict.cpp
// Talon: the client shell holds its CClientMgr (no holders), objects are moved through the
// cm_ functions, interpolation keeps a per-object time left (cd.m_fMoveAccumulatedTime), and
// pd_OnObjectRotate times rotations from the previous rotation update.
#include "bdefs.h"
#include "de_objects.h"
#include "clientmgr.h"
#include "clientshell.h"
#include "iclientshell.h"

// GLOBAL: LITHTECH 0x004e374c
extern int32 g_bPrediction;

// Scale the server periods by this amount.. 1.1 looks good.
// GLOBAL: LITHTECH 0x004d55f8
static float g_fServerPeriodMultiplier
	= 1.1f;

// 0x00426940
void cm_RotateObject(CClientMgr *pClientMgr, LTObject *pObject, LTRotation *pNewRot);


// FUNCTION: LITHTECH 0x0046ddf0
void pd_InitialServerUpdate(CClientShell *pShell, float gameTime)
{
	pShell->m_ClientGameTime = gameTime;
	pShell->m_ClientGameTimerSync = pShell->m_pClientMgr->m_CurTime;

	dl_TieOff( &pShell->m_MovingObjects );
	dl_TieOff( &pShell->m_RotatingObjects );
}


// Stack slots differ: the original keeps pClientShell in a local and fVelMagSqr in pShell's
// argument slot (0x10-byte frame vs 0xc); code is otherwise identical.
// STUB: LITHTECH 0x0046de20
void pd_OnObjectMove(CClientShell *pShell, LTObject *pObject, LTVector *pNewPos, LTVector *pNewVel, LTBOOL bNew, LTBOOL bTeleport)
{
	ClientData *pData;
	IClientShell *pClientShell;
	float fVelMagSqr, fUpdateDelta;

	fVelMagSqr = pNewVel->MagSqr();
	pClientShell = pShell->m_pClientMgr->m_pClientShell;
	pData = &pObject->cd;

	// Teleport the object if the new update is the same as the previous update
	// and it's not moving, or if it jumped too far.
	if (pNewPos->Equals(pData->m_LastUpdatePosServer, 0.1f) && (fVelMagSqr < 0.01f))
		bTeleport = LTTRUE;
	else if (!bTeleport && (fVelMagSqr < 1000000.0f) && ((pObject->GetPos() - *pNewPos).MagSqr() > 1000000.0f))
		bTeleport = LTTRUE;

	// Remember the last update we got from the server
	fUpdateDelta = pShell->m_ClientGameTime - pData->m_fLastUpdatePosTime;

	pData->m_fLastUpdatePosTime = pShell->m_ClientGameTime;
	pData->m_LastUpdatePosServer = *pNewPos;
	pData->m_LastUpdateVelServer = *pNewVel;
	pData->m_LastUpdatePosClient = pObject->GetPos();

	if(g_bPrediction)
	{
		if(!bNew && !bTeleport && fUpdateDelta != 0.0f)
		{
			dl_Remove(&pData->m_MovingLink);
			dl_Insert(&pShell->m_MovingObjects, &pData->m_MovingLink);

			pData->m_fMoveAccumulatedTime = fUpdateDelta * g_fServerPeriodMultiplier;
		}
		else
		{
			// It's a new object.. just initialize it as nonmoving.
			dl_Remove(&pData->m_MovingLink);
			dl_TieOff(&pData->m_MovingLink);

			if (pNewVel->MagSqr() > 0.01f)
				dl_Insert(&pShell->m_MovingObjects, &pData->m_MovingLink);

			pData->m_fMoveAccumulatedTime = 0.0f;

			// Just move it since we won't be interpolating its position.
			pClientShell->OnObjectMove((HOBJECT)pObject, bNew||bTeleport, pNewPos);
			cm_MoveObject(pShell->m_pClientMgr, pObject, pNewPos, LTTRUE);
		}
	}
	else
	{
		// Just move the object like normal.
		pClientShell->OnObjectMove((HOBJECT)pObject, bNew||bTeleport, pNewPos);
		cm_MoveObject(pShell->m_pClientMgr, pObject, pNewPos, LTTRUE);
	}
}


// FUNCTION: LITHTECH 0x0046e120
void pd_OnObjectRotate(CClientShell *pShell, LTObject *pObject, LTRotation *pNewRot, LTBOOL bNew, LTBOOL bSnap)
{
	ClientData *pClientData;
	IClientShell *pClientShell;
	float fUpdateDelta;

	pClientShell = pShell->m_pClientMgr->m_pClientShell;
	pClientData = &pObject->cd;

	fUpdateDelta = pShell->m_ClientGameTime - pClientData->m_fLastUpdateRotTime;
	pClientData->m_fLastUpdateRotTime = pShell->m_ClientGameTime;
	pClientData->m_rLastUpdateRotServer = *pNewRot;

	if(g_bPrediction)
	{
		if(!bNew && !bSnap && fUpdateDelta != 0.0f)
		{
			// Put it in the rotating object list.
			dl_Remove(&pClientData->m_RotatingLink);
			dl_Insert(&pShell->m_RotatingObjects, &pClientData->m_RotatingLink);

			// Add in the time...
			pClientData->m_fRotAccumulatedTime = fUpdateDelta * 2.0f;
		}
		else
		{
			// It's a new object.. just initialize it as nonmoving.
			dl_Remove(&pClientData->m_RotatingLink);
			dl_TieOff(&pClientData->m_RotatingLink);

			pClientData->m_fRotAccumulatedTime = 0.0f;

			// Just snap it since we won't be interpolating its rotation.
			pClientShell->OnObjectRotate((HOBJECT)pObject, bNew||bSnap, pNewRot);
			cm_RotateObject(pShell->m_pClientMgr, pObject, pNewRot);
		}
	}
	else
	{
		// Just move the object like normal.
		pClientShell->OnObjectRotate((HOBJECT)pObject, bNew||bSnap, pNewRot);
		cm_RotateObject(pShell->m_pClientMgr, pObject, pNewRot);
	}
}


// Talon runs CalcMotion through a MotionState (motion.h) and also saves/restores m_Flags.
// STUB: LITHTECH 0x0046e720
LTVector predict_EvaluateCurve(LTObject *pObj, const ClientData *pClientData, const LTVector &vGravity,
	float fInterpolant, float fTotalTime, LTBOOL bFullCalc)
{
	return pClientData->m_LastUpdatePosServer;
}


// STUB: LITHTECH 0x0046e250
void pd_Update(CClientShell *pShell)
{
}
