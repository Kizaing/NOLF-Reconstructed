// Jupiter runtime/shared/src/engine_vars.cpp
// The engine variable table (static in the original, 96 entries starting with "BindIP") is
// not reconstructed yet; it is referenced here by address.
#include "concommand.h"

// GLOBAL: LITHTECH 0x004d2230
extern LTEngineVar g_LTEngineVars[96];

// Functions that other modules call.
// FUNCTION: LITHTECH 0x00436190
LTEngineVar* GetEngineVars() {return g_LTEngineVars;}
// FUNCTION: LITHTECH 0x004361a0
int GetNumEngineVars() {return sizeof(g_LTEngineVars) / sizeof(LTEngineVar);}
