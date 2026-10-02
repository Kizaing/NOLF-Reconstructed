// Jupiter runtime/client/src/sys/win/winclientde_impl.cpp (Talon returns LTBOOL where Jupiter has bool)
#include "ltbasedefs.h"
#include "pixelformat.h"

// GLOBAL: LITHTECH 0x004def18
extern PFormat g_ScreenFormat;

// FUNCTION: LITHTECH 0x0040d050
LTBOOL IsVertSpanSolidColor(uint8 *pBuf, GenericColor color, uint32 height, long pitch)
{
	if(g_ScreenFormat.m_eType == BPP_16)
	{
		while(height)
		{
			if(*((uint16*)pBuf) != color.wVal)
				return LTFALSE;
			
			pBuf += pitch;
			--height;
		}
	}
	else if(g_ScreenFormat.m_eType == BPP_32)
	{
		while(height)
		{
			if(*((uint32*)pBuf) != color.dwVal)
				return LTFALSE;
			
			pBuf += pitch;
			--height;
		}
	}

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x0040d0b0
LTBOOL IsHorzSpanSolidColor(uint8 *pBuf, GenericColor color, uint32 width)
{
	if(g_ScreenFormat.m_eType == BPP_16)
	{
		while(width)
		{
			if(*((uint16*)pBuf) != color.wVal)
				return LTFALSE;
			
			--width;
			pBuf += sizeof(uint16);
		}
	}
	else if(g_ScreenFormat.m_eType == BPP_32)
	{
		while(width)
		{
			if(*((uint32*)pBuf) != color.dwVal)
				return LTFALSE;
			
			--width;
			pBuf += sizeof(uint32);
		}
	}

	return LTTRUE;
}
