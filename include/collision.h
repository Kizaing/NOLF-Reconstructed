// Talon world collision (Jupiter runtime/shared/src/collision.h). Layouts from
// CollideAgainstWorld (moveobject).
#ifndef __COLLISION_H__
#define __COLLISION_H__

#include "ltbasedefs.h"

class MoveAbstract;
class WorldBsp;
class LTObject;
struct Node;

// 0x4c bytes.
struct CollideRequest
{
	// Constructor
	CollideRequest() : m_Unknown44(0), m_pRestart(LTNULL) {};

	// Abstraction layer.
	MoveAbstract	*m_pAbstract;		// 0x00
	CollisionInfo	*m_pCollisionInfo;	// 0x04
	uint32			m_bServer;			// 0x08 (MoveState::m_bServer)

	// The world to collide against.
	WorldBsp		*m_pWorld;			// 0x0c
	LTObject		*m_pWorldObj;		// 0x10

	// The movement.
	LTVector		m_OriginalPos;		// 0x14
	LTVector		m_NewPos;			// 0x20

	// Dimensions of the object trying to move.
	LTVector		m_Dims;				// 0x2c

	// FLAG_STAIRSTEP is set on the object.
	LTBOOL			m_bStairStep;		// 0x38

	// The LTObject doing the movement.  This MUST be set.  It will calculate
	// collision response, modify its velocity, and notify the object.
	LTObject		*m_pObject;			// 0x3c

	// Should the object slide along polygons
	LTBOOL			m_bSlide;			// 0x40

	uint32			m_Unknown44;		// 0x44 (MoveState::m_Unknown64)
	int				*m_pRestart;		// 0x48 (&MoveState::m_nRestart)
};

// 0x2c bytes.
struct CollideInfo
{
	// The final (unclipped) position the object wound up at.
	LTVector	m_FinalPos;			// 0x00

	// CollideWithWorld() will set this, telling you how many times it pushed the object off of something.
	uint32		m_nHits;			// 0x0c

	// If FLAG_STAIRSTEP is specified, this will set m_pStandingOn to
	// NULL or the object that it is standing on.
	Node		*m_pStandingOn;		// 0x10

	// Force of collisions
	LTVector	m_vForce;			// 0x14

	// This is the velocity offset.  You should apply this after calling CollideWithWorld.
	// This is here so you can do a touch notify on the object before changing the velocity.
	LTVector	m_VelOffset;		// 0x20
};


// Does this box intersect this BSP tree? (0x0041aed0)
LTBOOL DoesBoxIntersectBSP(Node *pRoot, LTVector &vMin, LTVector &vMax);

// Collides the axis-aligned box with the world (0x0041bdd0).
void CollideWithWorld(CollideRequest &request, CollideInfo *pInfo);

// Sets up the collision info and stopping velocities for two objects hitting each other (0x0041f3d0).
void DoInterObjectCollisionResponse(MoveAbstract *pAbstract, LTObject *pObj1, LTObject *pObj2,
	LTVector *pNormal, float fPlaneDist);

#endif  // __COLLISION_H__
