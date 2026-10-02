// The engine's CMoArray is the real Talon StdLith one (vtable from GenList, data pointer at +4,
// constructor = Clear() + out-of-line Init()). Include this rather than the path below.
// Exception: load_pcx.h (see there).
#ifndef __LTDYNARRAY_H__
#define __LTDYNARRAY_H__

#include "ltbasedefs.h"
#include "../../build/proj/LT2/lithshared/stdlith/dynarray.h"

#endif
