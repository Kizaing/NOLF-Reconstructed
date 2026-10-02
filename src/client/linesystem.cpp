// Jupiter runtime/client/src/linesystem.cpp (the ILTClient line system functions).
// Talon keeps the extents in a different order than Jupiter (see de_objects.h LineSystem).
// FLAGS: /O2 /GX-
#include "bdefs.h"
#include "de_objects.h"
#include "iltclient.h"
#include "../../build/proj/LT2/lithshared/stdlith/struct_bank.h"

// Talon LineSystem extents (de_objects.h names them in Jupiter's order).
#define LS_MINPOS(pSystem)		(*(LTVector*)&(pSystem)->m_SystemCenter)		// 0x1fc
#define LS_MAXPOS(pSystem)		(*(LTVector*)&(pSystem)->m_SystemRadius)		// 0x208
#define LS_CENTER(pSystem)		(*(LTVector*)((uint8*)(pSystem) + 0x214))		// 0x214
#define LS_RADIUS(pSystem)		(*(float*)((uint8*)(pSystem) + 0x220))			// 0x220


// -------------------------------------------------------------------- //
// Internals.
// -------------------------------------------------------------------- //

inline void linesystem_CopyLTLineToLSLine(LTLine *pIn, LSLine *pFillIn)
{
	int i;

	for(i=0; i < 2; i++)
	{
		pFillIn->m_Points[i].m_Pos = pIn->m_Points[i].m_Pos;

		pFillIn->m_Points[i].r = pIn->m_Points[i].r;
		pFillIn->m_Points[i].g = pIn->m_Points[i].g;
		pFillIn->m_Points[i].b = pIn->m_Points[i].b;
		pFillIn->m_Points[i].a = pIn->m_Points[i].a;
	}
}


inline void linesystem_CopyLSLineToLTLine(LSLine *pIn, LTLine *pFillIn)
{
	int i;

	for(i=0; i < 2; i++)
	{
		pFillIn->m_Points[i].m_Pos = pIn->m_Points[i].m_Pos;

		pFillIn->m_Points[i].r = pIn->m_Points[i].r;
		pFillIn->m_Points[i].g = pIn->m_Points[i].g;
		pFillIn->m_Points[i].b = pIn->m_Points[i].b;
		pFillIn->m_Points[i].a = pIn->m_Points[i].a;
	}
}


inline void linesystem_ExtendBounds(LineSystem *pSystem, LTVector *pPt)
{
	VEC_MIN(LS_MINPOS(pSystem), LS_MINPOS(pSystem), *pPt);
	VEC_MAX(LS_MAXPOS(pSystem), LS_MAXPOS(pSystem), *pPt);
}


inline void linesystem_CalcExtents(LineSystem *pSystem)
{
	LTVector half;

	half = LS_MAXPOS(pSystem) - LS_MINPOS(pSystem);
	half *= 0.5f;
	LS_CENTER(pSystem) = LS_MINPOS(pSystem) + half;
	LS_RADIUS(pSystem) = half.Mag() + 1.0f;
}


// -------------------------------------------------------------------- //
// External functions.
// -------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x004450c0
HLTLINE linesystem_GetNextLine(HLOCALOBJ hObj, HLTLINE hPrev)
{
	LineSystem *pSystem;
	LSLine *pLine;

	pSystem = (LineSystem*)hObj;
	if(!pSystem || pSystem->m_ObjectType != OT_LINESYSTEM)
		return LTNULL;

	if(hPrev)
	{
		pLine = (LSLine*)hPrev;

		if(pLine->m_pNext == &pSystem->m_LineHead)
			return LTNULL;

		return (HLTLINE)pLine->m_pNext;
	}
	else
	{
		if(pSystem->m_LineHead.m_pNext == &pSystem->m_LineHead)
			return LTNULL;

		return (HLTLINE)pSystem->m_LineHead.m_pNext;
	}
}


// FUNCTION: LITHTECH 0x00445110
void linesystem_GetLineInfo(HLTLINE hLine, LTLine *pFillIn)
{
	LSLine *pLine;

	if(!hLine || !pFillIn)
		return;

	pLine = (LSLine*)hLine;
	linesystem_CopyLSLineToLTLine(pLine, pFillIn);
}


// FUNCTION: LITHTECH 0x00445170
void linesystem_SetLineInfo(HLTLINE hLine, LTLine *pInput)
{
	LSLine *pLine;

	if(!hLine || !pInput)
		return;

	pLine = (LSLine*)hLine;

	linesystem_CopyLTLineToLSLine(pInput, pLine);

	linesystem_ExtendBounds(pLine->m_pSystem, &pLine->m_Points[0].m_Pos);
	linesystem_ExtendBounds(pLine->m_pSystem, &pLine->m_Points[1].m_Pos);
	linesystem_CalcExtents(pLine->m_pSystem);

	pLine->m_pSystem->m_bChanged = LTTRUE;
}


// FUNCTION: LITHTECH 0x00445490
HLTLINE linesystem_AddLine(HLOCALOBJ hObj, LTLine *pInput)
{
	LSLine *pLine;
	LineSystem *pSystem;

	pSystem = (LineSystem*)hObj;
	if(!pSystem || !pInput || pSystem->m_ObjectType != OT_LINESYSTEM)
		return LTNULL;

	pLine = (LSLine*)sb_Allocate(pSystem->m_pLineBank);
	pLine->m_pNext = &pSystem->m_LineHead;
	pLine->m_pPrev = pSystem->m_LineHead.m_pPrev;
	pLine->m_pPrev->m_pNext = pLine->m_pNext->m_pPrev = pLine;
	pLine->m_pSystem = pSystem;

	linesystem_CopyLTLineToLSLine(pInput, pLine);
	linesystem_ExtendBounds(pSystem, &pLine->m_Points[0].m_Pos);
	linesystem_ExtendBounds(pSystem, &pLine->m_Points[1].m_Pos);
	linesystem_CalcExtents(pSystem);

	pSystem->m_bChanged = LTTRUE;
	return (HLTLINE)pLine;
}


// FUNCTION: LITHTECH 0x004457f0
void linesystem_RemoveLine(HLOCALOBJ hObj, HLTLINE hLine)
{
	LSLine *pLine;
	LineSystem *pSystem;

	if(!hObj || !hLine)
		return;

	pLine = (LSLine*)hLine;
	pSystem = (LineSystem*)hObj;
	if(pSystem->m_ObjectType != OT_LINESYSTEM)
		return;

	pLine->m_pNext->m_pPrev = pLine->m_pPrev;
	pLine->m_pPrev->m_pNext = pLine->m_pNext;
	sb_Free(pSystem->m_pLineBank, pLine);
}
