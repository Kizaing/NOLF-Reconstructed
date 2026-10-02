// The engine's CMoArray is the real Talon StdLith one (vtable from GenList, data pointer at +4,
// constructor = Clear() + Init()). Include this rather than the path below.
// Whether Init/SetSize2/Insert2/Remove2 are inlined depends only on the caller's inline budget
// (see README "How VC6 decides what to inline"); the header itself needs no changes.
#ifndef __LTDYNARRAY_H__
#define __LTDYNARRAY_H__

#include "ltbasedefs.h"
#include "../../build/proj/LT2/lithshared/stdlith/dynarray.h"

#endif
