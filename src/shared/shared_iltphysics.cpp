// Jupiter runtime/shared/src/shared_iltphysics.cpp, Talon version.
// Talon: Get* take references, every function checks its params, the friction functions are
// no longer obsolete, and IsWorldObject also accepts the physics BSP.
#include "bdefs.h"
#include "iltphysics.h"
#include "de_objects.h"
#include "de_world.h"

// Talon world info flags (Jupiter de_world.h).
#define WIF_MAINWORLD		(1<<2)
#define WIF_PHYSICSBSP		(1<<3)

#define HObjToLTObj(hObj)	((LTObject*)(hObj))


// FUNCTION: LITHTECH 0x0043d110
LTRESULT ILTPhysics::GetFrictionCoefficient(HOBJECT hObj, float &u)
{
	CHECK_PARAMS(hObj, ILTPhysics::GetFrictionCoefficient);
	u = HObjToLTObj(hObj)->m_FrictionCoefficient;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0043d170
LTRESULT ILTPhysics::SetFrictionCoefficient(HOBJECT hObj, float u)
{
	CHECK_PARAMS(hObj, ILTPhysics::SetFrictionCoefficient);
	HObjToLTObj(hObj)->m_FrictionCoefficient = u;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0043d1c0
LTRESULT ILTPhysics::GetForceIgnoreLimit(HOBJECT hObj, float &limit)
{
	CHECK_PARAMS(hObj, ILTPhysics::GetForceIgnoreLimit);
	limit = (float)sqrt(HObjToLTObj(hObj)->m_ForceIgnoreLimitSqr);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0043d220
LTRESULT ILTPhysics::SetForceIgnoreLimit(HOBJECT hObj, float limit)
{
	CHECK_PARAMS(hObj, ILTPhysics::SetForceIgnoreLimit);
	HObjToLTObj(hObj)->m_ForceIgnoreLimitSqr = limit*limit;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0043d280
LTRESULT ILTPhysics::GetVelocity(HOBJECT hObj, LTVector *pVel)
{
	LTObject *pObj;

	if(!hObj || !pVel)
	{
		RETURN_ERROR(1, CommonLT::GetVelocity, LT_INVALIDPARAMS);
	}

	pObj = HObjToLTObj(hObj);
	*pVel = pObj->m_Velocity;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0043d2f0
LTRESULT ILTPhysics::GetAcceleration(HOBJECT hObj, LTVector *pAccel)
{
	CHECK_PARAMS(hObj && pAccel, CommonLT::GetAcceleration);
	*pAccel = HObjToLTObj(hObj)->m_Acceleration;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0043d360
LTRESULT ILTPhysics::GetObjectMass(HOBJECT hObj, float &m)
{
	CHECK_PARAMS(hObj, CommonLT::GetObjectMass);
	m = HObjToLTObj(hObj)->m_Mass;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0043d3c0
LTRESULT ILTPhysics::SetObjectMass(HOBJECT hObj, float m)
{
	CHECK_PARAMS(hObj, CommonLT::GetObjectMass);
	HObjToLTObj(hObj)->m_Mass = m;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0043d410
LTRESULT ILTPhysics::GetStandingOn(HOBJECT hObj, CollisionInfo *pInfo)
{
	LTObject *pStandingOn;
	LTObject *pObj;
	LTPlane *pPlane;

	CHECK_PARAMS(hObj && pInfo, ILTPhysics::GetStandingOn);

	pObj = (LTObject*)hObj;

	// Stopping velocity only applies when MID_TOUCHNOTIFY is sent.
	pInfo->m_vStopVel.Init();
	pInfo->m_hObject = LTNULL;
	pInfo->m_hPoly = INVALID_HPOLY;

	// Check if we are standing on something...
	// pObj->m_pStandingOn can either be a world node or an object...
	if((pStandingOn = pObj->m_pStandingOn) != LTNULL)
	{
		// Check if it is a world node...
		if(pObj->m_pNodeStandingOn)
		{
			pPlane = pObj->m_pNodeStandingOn->GetPlane();
			if(pPlane)
			{
				pInfo->m_Plane = *pPlane;
				pInfo->m_hObject = (HOBJECT)pStandingOn;

				// Should be standing on an HPOLY..
				if(pStandingOn->HasWorldModel())
				{
					pInfo->m_hPoly = ((WorldModelInstance*)pStandingOn)->MakeHPoly(pObj->m_pNodeStandingOn);
				}
			}
		}
		// It's an object...
		else
		{
			pInfo->m_Plane.m_Normal.Init(0.0f, 1.0f, 0.0f);
			pInfo->m_Plane.m_Dist = pStandingOn->GetPos().y + pStandingOn->m_Dims.y;
			pInfo->m_hObject = (HOBJECT)pStandingOn;
		}
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0043d510
LTRESULT ILTPhysics::IsWorldObject(HOBJECT hObj)
{
	LTObject *pObj = HObjToLTObj(hObj);

	if(pObj && pObj->m_ObjectType == OT_WORLDMODEL &&
		((((WorldModelInstance*)pObj)->m_pOriginalBsp->GetWorldInfoFlags() & WIF_MAINWORLD) ||
		(((WorldModelInstance*)pObj)->m_pOriginalBsp->GetWorldInfoFlags() & WIF_PHYSICSBSP)))
	{
		return LT_YES;
	}
	else
	{
		return LT_NO;
	}
}

// FUNCTION: LITHTECH 0x0043d560
LTRESULT ILTPhysics::GetObjectDims(HOBJECT hObj, LTVector *pDims)
{
	LTObject *pObj;

	CHECK_PARAMS(hObj && pDims, ILTPhysics::GetObjectDims);

	pObj = (LTObject*)hObj;
	*pDims = pObj->m_Dims;
	return LT_OK;
}
