// Jupiter runtime/shared/src/engine_vars.cpp
// The engine variable table (static in the original, 96 entries starting with "BindIP") is
// not reconstructed yet; it is referenced here by address.
#include "concommand.h"

// GLOBAL: LITHTECH 0x004d2230
extern LTEngineVar g_LTEngineVars[96];

// The engine variables themselves (0x4d2128-0x4d222c, initialised ints and floats that the table points
// at) are not reconstructed yet either, except these, which other modules use.
// soundinstance: filtered samples are muted and left looping on their tail instead of being stopped
// when it is cleared.
// GLOBAL: LITHTECH 0x004d212c ?g_bStopFilteredSamples@@3IA
LTBOOL g_bStopFilteredSamples = LTTRUE;
// soundmgr: when a buffer has more instances than this, the one closest to finishing is removed.
// GLOBAL: LITHTECH 0x004d2130 ?g_dwMaxInstancesPerBuffer@@3KA
uint32 g_dwMaxInstancesPerBuffer = 32;

// servermgr: ticks spent in class updates this frame.
// GLOBAL: LITHTECH 0x004e36c0
uint32 g_Ticks_ClassUpdate;

// The console's global model light add / directional add / world model ambient colors
// (set by the ModelAdd, ModelDirAdd and WMAmbient commands), zero constructed here.
// FUNCTION: LITHTECH 0x00436100 _$E2
// FUNCTION: LITHTECH 0x00436110 _$E1
// GLOBAL: LITHTECH 0x004e36b4
LTVector g_ConsoleModelAdd(0.0f, 0.0f, 0.0f);
// FUNCTION: LITHTECH 0x00436130 _$E5
// FUNCTION: LITHTECH 0x00436140 _$E4
// GLOBAL: LITHTECH 0x004e369c
LTVector g_ConsoleModelDirAdd(0.0f, 0.0f, 0.0f);
// FUNCTION: LITHTECH 0x00436160 _$E8
// FUNCTION: LITHTECH 0x00436170 _$E7
// GLOBAL: LITHTECH 0x004e36a8
LTVector g_ConsoleModelDirAdd2(0.0f, 0.0f, 0.0f);

// Functions that other modules call.
// FUNCTION: LITHTECH 0x00436190
LTEngineVar* GetEngineVars() {return g_LTEngineVars;}
// FUNCTION: LITHTECH 0x004361a0
int GetNumEngineVars() {return sizeof(g_LTEngineVars) / sizeof(LTEngineVar);}
