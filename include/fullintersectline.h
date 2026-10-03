// Jupiter runtime/world/src/fullintersectline.h: line/BSP intersection.
#ifndef __FULLINTERSECTLINE_H__
#define __FULLINTERSECTLINE_H__

#include "ltbasetypes.h"

// Profiling counters (the server manager clears them every update).
// GLOBAL: LITHTECH 0x004e3804
extern uint32 g_IntersectTicks;
// GLOBAL: LITHTECH 0x004e3808
extern uint32 g_nIntersectCalls;
// Total length of all the segments tested (Talon).
// GLOBAL: LITHTECH 0x004e3814
extern float g_IntersectLineLen;

#endif  // __FULLINTERSECTLINE_H__
