// Jupiter runtime/client/src/console.cpp
// Talon defines the global console object here (Jupiter: winconsole_impl.cpp), and the
// wrappers call it without Jupiter's dsi_IsConsoleEnabled() checks.
#include <stdarg.h>
#include "console.h"

// Its compiler-generated dynamic initializer is _$E4 (calls _$E1 = construct, then
// _$E3 = atexit(_$E2 = destruct)).
// FUNCTION: LITHTECH 0x00420690 _$E4
// FUNCTION: LITHTECH 0x004206a0 _$E1
// FUNCTION: LITHTECH 0x004206b0 _$E3
// FUNCTION: LITHTECH 0x004206c0 _$E2
// GLOBAL: LITHTECH 0x004e2f88
CConsole g_Console;


// ------------------------------------------------------------------ //
// Console interface functions.
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x004205c0
void con_Term(LTBOOL bDeleteTextLines)
{
	GETCONSOLE()->Term( bDeleteTextLines );
}

// FUNCTION: LITHTECH 0x004205d0
LTBOOL con_InitBare()
{
	return GETCONSOLE()->InitBare();
}

// FUNCTION: LITHTECH 0x004205e0
LTRESULT con_LoadBackground()
{
	return GETCONSOLE()->LoadBackground();
}

// FUNCTION: LITHTECH 0x004205f0
LTBOOL con_Init(LTRect *pRect, CommandHandler handler, RenderStruct *pStruct)
{
	return GETCONSOLE()->Init( pRect, handler, pStruct );
}

// FUNCTION: LITHTECH 0x00420610
void con_SetErrorLog(ErrorLogFn fn)
{
	GETCONSOLE()->SetErrorLogFn( fn );
}

// FUNCTION: LITHTECH 0x00420620
void con_PrintString(CONCOLOR theColor, int filterLevel, const char *pMsg)
{
	GETCONSOLE()->PrintString( theColor, filterLevel, pMsg );
}

// FUNCTION: LITHTECH 0x00420640
void con_Printf(CONCOLOR theColor, int filterLevel, const char *pMsg, ...)
{
	va_list	marker;
	va_start( marker, pMsg );
	GETCONSOLE()->vPrintf( theColor, filterLevel, pMsg, marker );
	va_end( marker );
}

// FUNCTION: LITHTECH 0x00420660
void con_WhitePrintf(const char *pMsg, ...)
{
	va_list		marker;
	va_start( marker, pMsg );
	GETCONSOLE()->vPrintf( CONRGB(255,255,255), 0, pMsg, marker );
	va_end( marker );
}

// FUNCTION: LITHTECH 0x00420680
void con_OnKeyPress(uint32 key)
{
	GETCONSOLE()->OnKeyPress( key );
}
