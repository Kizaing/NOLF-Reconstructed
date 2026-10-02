// Jupiter runtime/world/src/de_objects.cpp
// Talon also defines DebugOut here (Jupiter: kernel/src/debugging.cpp).
#include <stdio.h>
#include <stdarg.h>
#include <windows.h>
#include "bdefs.h"
#include "de_world.h"
#include "de_objects.h"
#include "geomroutines.h"


// FUNCTION: LITHTECH 0x004306c0
void obj_SetupWorldModelTransform(WorldModelInstance *pWorldModel)
{
	WorldBsp *pWorldBsp;

	if(!pWorldModel->m_pOriginalBsp->IsUntransformed())
	{
		pWorldBsp = pWorldModel->m_pOriginalBsp;
		gr_SetupWMTransform(
			&pWorldBsp->m_WorldTranslation,
			&pWorldModel->GetPos(),
			&pWorldModel->m_Rotation,
			&pWorldModel->m_Transform,
			&pWorldModel->m_BackTransform);
	}
}


// FUNCTION: LITHTECH 0x00430710
void DebugOut(const char *pMsg, ...)
{
	char msg[10000];
	va_list marker;

	va_start(marker, pMsg);
	_vsnprintf(msg, 9999, pMsg, marker);
	va_end(marker);

	OutputDebugString(msg);
}
