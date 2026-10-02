// GenericProp setup helpers (Jupiter runtime/shared/src/genericprop_setup.h). Talon: 0x0043aef0-0x0043b0d0.
#ifndef __GENERICPROP_SETUP_H__
#define __GENERICPROP_SETUP_H__

#include "ltbasedefs.h"

// Initialize a GenericProp to default values.
void gp_Init(GenericProp *pGeneric);								// 0x0043aef0

// Init the GenericProp from the different data types.
void gp_InitString(GenericProp *pGeneric, const char *pString);	// 0x0043af30
void gp_InitVector(GenericProp *pGeneric, LTVector *pVec);		// 0x0043b000
void gp_InitFloat(GenericProp *pGeneric, float val);				// 0x0043b070
void gp_InitRotation(GenericProp *pGeneric, LTVector *pAngles);	// 0x0043b090

#endif
