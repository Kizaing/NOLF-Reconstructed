// Talon's built-in surface effects (effects.cpp; Jupiter dropped them, so the name is a guess).
#ifndef __EFFECTS_H__
#define __EFFECTS_H__

class CClientMgr;

// Registers Pan, Rotate, Warble, Portal and Mirror with cm_AddSurfaceEffect.
void se_AddSurfaceEffects(CClientMgr *pClientMgr);		// 0x004360a0
void se_RemoveSurfaceEffects(CClientMgr *pClientMgr);	// 0x004360d0

#endif  // __EFFECTS_H__
