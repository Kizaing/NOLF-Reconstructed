// Jupiter runtime/shared/src/genericprop_setup.cpp (Talon: sprintf/strncpy, and a property type
// table set up by a static object).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bdefs.h"
#include "genericprop_setup.h"
#include "geomroutines.h"
#include "ltserverobj.h"


// A static table indexed by property type (PT_), filled in by a static object's constructor.
// Nothing in the engine reads it.
class CPropTypeTable
{
public:
	CPropTypeTable();
	~CPropTypeTable();
};

// GLOBAL: LITHTECH 0x004e382c
uint8 g_PropTypeOrder[8];

// FUNCTION: LITHTECH 0x0043ae70 _$E4
// FUNCTION: LITHTECH 0x0043ae80 _$E1
// FUNCTION: LITHTECH 0x0043ae90 _$E3
// FUNCTION: LITHTECH 0x0043aea0 _$E2
// GLOBAL: LITHTECH 0x004e3828
static CPropTypeTable g_PropTypeTable;

// FUNCTION: LITHTECH 0x0043aeb0
CPropTypeTable::CPropTypeTable()
{
	g_PropTypeOrder[PT_ROTATION] = 0;
	g_PropTypeOrder[PT_LONGINT] = 1;
	g_PropTypeOrder[PT_COLOR] = 2;
	g_PropTypeOrder[PT_REAL] = 3;
	g_PropTypeOrder[PT_VECTOR] = 4;
	g_PropTypeOrder[PT_STRING] = 5;
	g_PropTypeOrder[PT_FLAGS] = 6;
	g_PropTypeOrder[PT_BOOL] = 7;
}

// Empty (identical-code folded at 0x00473ac0).
CPropTypeTable::~CPropTypeTable()
{
}


// FUNCTION: LITHTECH 0x0043aef0
void gp_Init(GenericProp *pGeneric)
{
	pGeneric->m_Vec.Init();
	pGeneric->m_Rotation.Init();
	pGeneric->m_String[0] = 0;
	pGeneric->m_Long = 0;
	pGeneric->m_Float = 0.0f;
	pGeneric->m_Bool = LTFALSE;
}


static void _SetNumProp(GenericProp *pProp, float val);

// FUNCTION: LITHTECH 0x0043af30
void gp_InitString(GenericProp *pGeneric, const char *pString)
{
	gp_Init(pGeneric);

	sscanf(pString, "%f %f %f", &pGeneric->m_Vec.x, &pGeneric->m_Vec.y, &pGeneric->m_Vec.z);

	if (stricmp(pString, "true") == 0)
	{
		_SetNumProp(pGeneric, 1.0f);
	}
	else if (stricmp(pString, "false") == 0)
	{
		_SetNumProp(pGeneric, 0.0f);
	}
	else
	{
		_SetNumProp(pGeneric, (float)atof(pString));
	}

	strncpy(pGeneric->m_String, pString, MAX_GP_STRING_LEN);
}


// FUNCTION: LITHTECH 0x0043afb0
static void _SetNumProp(GenericProp *pProp, float val)
{
	sprintf(pProp->m_String, "%f", val);

	pProp->m_Float = val;
	pProp->m_Bool = pProp->m_Long = (long)val;
}


// FUNCTION: LITHTECH 0x0043b000
void gp_InitVector(GenericProp *pGeneric, LTVector *pVec)
{
	gp_Init(pGeneric);
	sprintf(pGeneric->m_String, "%f %f %f", pVec->x, pVec->y, pVec->z);
	pGeneric->m_Vec = *pVec;
	pGeneric->m_Color = *pVec;
}


// FUNCTION: LITHTECH 0x0043b070
void gp_InitFloat(GenericProp *pGeneric, float val)
{
	gp_Init(pGeneric);
	_SetNumProp(pGeneric, val);
}


// FUNCTION: LITHTECH 0x0043b090
void gp_InitRotation(GenericProp *pGeneric, LTVector *pAngles)
{
	gp_Init(pGeneric);

	pGeneric->m_Vec = *pAngles;
	gr_EulerToRotation(pGeneric->m_Vec.x, pGeneric->m_Vec.y, pGeneric->m_Vec.z, &pGeneric->m_Rotation);
}
