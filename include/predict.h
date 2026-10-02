// Talon client-side prediction (Jupiter runtime/client/src/predict.h). Objects the server moves
// are interpolated towards its last update; predict.cpp keeps the moving and rotating lists on
// the client shell.
#ifndef __PREDICT_H__
#define __PREDICT_H__

#include "ltbasedefs.h"

class CClientShell;
class LTObject;

// Called on the first update from the server: synchronizes our game time with the server's.
void pd_InitialServerUpdate(CClientShell *pShell, float gameTime);	// 0x0046ddf0

// The server told us about a new position and velocity / rotation for an object.
void pd_OnObjectMove(CClientShell *pShell, LTObject *pObject, LTVector *pNewPos, LTVector *pNewVel,
	LTBOOL bNew, LTBOOL bTeleport);									// 0x0046de20
void pd_OnObjectRotate(CClientShell *pShell, LTObject *pObject, LTRotation *pNewRot,
	LTBOOL bNew, LTBOOL bSnap);										// 0x0046e120

// Interpolates the objects towards their last server update.
void pd_Update(CClientShell *pShell);								// 0x0046e250

#endif  // __PREDICT_H__
