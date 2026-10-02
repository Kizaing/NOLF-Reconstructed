// Jupiter runtime/kernel/src/sys/win/text_mgr.cpp
// The unit starts at 0049b460 with g_Fonts' static initializers and ends at 0049c110, where
// timemgr starts. Talon fills ILTClient's font functions in tmgr_Init(ILTClient*).
#include <windows.h>
#include <string.h>
#include "bdefs.h"
#include "de_memory.h"
#include "stringmgr.h"
#include "pixelformat.h"
#include "iltclient.h"
#include "../../build/proj/LT2/lithshared/stdlith/goodlinklist.h"


struct LTFont : public CGLLNode
{
	HFONT		m_hFont;		// 0x08
};

// Client surface (winclientde_impl). Only recovered members.
struct CisSurface
{
	uint8		m_Pad00[0x10];
	uint32		m_Width;		// 0x10
	uint32		m_Height;		// 0x14
};

// winclientde_impl.
CisSurface* cis_InternalCreateSurface(uint32 width, uint32 height);	// 0x0040c520


// ----------------------------------------------------------------- //
// Globals.
// ----------------------------------------------------------------- //

// All the fonts.
// FUNCTION: LITHTECH 0x0049b460 _$E4
// FUNCTION: LITHTECH 0x0049b470 _$E1
// FUNCTION: LITHTECH 0x0049b490 _$E3
// FUNCTION: LITHTECH 0x0049b4a0 _$E2
// GLOBAL: LITHTECH 0x004e61f8
static CGLinkedList<LTFont*> g_Fonts;

// GLOBAL: LITHTECH 0x004e6204
static HDC g_hTextDC;
// GLOBAL: LITHTECH 0x004e6208
static HBITMAP g_hTextBitmap;
// GLOBAL: LITHTECH 0x004e620c
static HBITMAP g_hOldTextBitmap;
// GLOBAL: LITHTECH 0x004e6210
static HBRUSH g_hOldBrush;
// GLOBAL: LITHTECH 0x004e6214
static uint32 g_TextBitmapWidth;
// GLOBAL: LITHTECH 0x004e6218
static uint32 g_TextBitmapHeight;
// GLOBAL: LITHTECH 0x004e621c
static uint32 g_TextBitmapPitch;
// GLOBAL: LITHTECH 0x004e6220
static uint8 *g_pTextBitmapBits;


static HLTFONT tmgr_CreateFont(char *pFontName, int width, int height, LTBOOL bItalic,
	LTBOOL bUnderline, LTBOOL bBold);
static void tmgr_DeleteFont(HLTFONT hFont);
static void tmgr_DeleteFont(LTFont *pFont);
static LTRESULT tmgr_SetFontExtraSpace(HLTFONT hFont, int pixels);
static LTRESULT tmgr_GetFontExtraSpace(HLTFONT hFont, int &pixels);
static void tmgr_GetStringDimensions(HLTFONT hFont, HSTRING hString, int *sizeX, int *sizeY);
static void tmgr_DrawStringToSurface(HSURFACE hDest, HLTFONT hFont, HSTRING hString,
	LTRect *pRect, HLTCOLOR hForeColor, HLTCOLOR hBackColor);
static HSURFACE tmgr_CreateSurfaceFromString(HLTFONT hFont, HSTRING hString,
	HLTCOLOR hForeColor, HLTCOLOR hBackColor, int extraPixelsX, int extraPixelsY);
static void tmgr_InternalDrawStringToSurface(HSURFACE hDest, HLTFONT hFont, HSTRING hString,
	LTRect *pRect, HLTCOLOR hForeColor, HLTCOLOR hBackColor, SIZE *pSize);
static void tmgr_SizeTextSurface(SIZE *pSize);
static void tmgr_DrawTextToSurface(CisSurface *pDest, LTRect *pSrcRect, LTRect *pDestRect,
	HLTCOLOR hForeColor, HLTCOLOR hBackColor);
static void tmgr_DeleteAllFonts();


// ----------------------------------------------------------------- //
// Main exposed functions.
// ----------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0049b4c0
void tmgr_Init(ILTClient *pClientDE)
{
	// Setup interface pointers.
	pClientDE->CreateFont = tmgr_CreateFont;
	pClientDE->DeleteFont = tmgr_DeleteFont;
	pClientDE->SetFontExtraSpace = tmgr_SetFontExtraSpace;
	pClientDE->GetFontExtraSpace = tmgr_GetFontExtraSpace;
	pClientDE->GetStringDimensions = tmgr_GetStringDimensions;
	pClientDE->DrawStringToSurface = tmgr_DrawStringToSurface;
	pClientDE->CreateSurfaceFromString = tmgr_CreateSurfaceFromString;


	// Setup the HDC we will draw all the text into.
	g_hTextDC = CreateCompatibleDC(LTNULL);
	g_hOldBrush = (HBRUSH)SelectObject(g_hTextDC, GetStockObject(BLACK_BRUSH));
	SetTextColor(g_hTextDC, RGB(255,255,255));

//	SetBkMode(g_hTextDC, TRANSPARENT);
	SetBkMode(g_hTextDC, OPAQUE);
	SetTextColor(g_hTextDC, RGB(255,255,255));
	SetBkColor(g_hTextDC, RGB(0,0,0));

	g_hTextBitmap = 0;
	g_hOldTextBitmap = 0;
	g_TextBitmapWidth = g_TextBitmapHeight = 0;
	g_pTextBitmapBits = LTNULL;
}


// ----------------------------------------------------------------- //
// Interface implementation functions.
// ----------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0049b5a0
static HLTFONT tmgr_CreateFont(char *pFontName, int width, int height, LTBOOL bItalic,
	LTBOOL bUnderline, LTBOOL bBold)
{
	HFONT hFont;
	LTFont *pFont;

	hFont = CreateFont(height, width, 0, 0, bBold?FW_BOLD:FW_NORMAL,
		bItalic, bUnderline, LTFALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH|FF_DONTCARE, pFontName);

	if(!hFont)
		return LTNULL;

	pFont = (LTFont*)dalloc(sizeof(LTFont));
	pFont->m_hFont = hFont;
	g_Fonts.AddHead(pFont);

	return (HLTFONT)pFont;
}

// FUNCTION: LITHTECH 0x0049b650 ?tmgr_DeleteFont@@YAXPAUHLTFONT_t@@@Z
static void tmgr_DeleteFont(HLTFONT hFont)
{
	LTFont *pFont;

	pFont = (LTFont*)hFont;
	if(pFont)
	{
		tmgr_DeleteFont(pFont);
	}
}

// FUNCTION: LITHTECH 0x0049b670 ?tmgr_DeleteFont@@YAXPAULTFont@@@Z
static void tmgr_DeleteFont(LTFont *pFont)
{
	DeleteObject(pFont->m_hFont);
	g_Fonts.RemoveAt(pFont);
	dfree(pFont);
}

// FUNCTION: LITHTECH 0x0049b6d0
static LTRESULT tmgr_SetFontExtraSpace(HLTFONT hFont, int pixels)
{
	LTFont *pFont = (LTFont*)hFont;
	HFONT hOldFont;

	if(!pFont)
		return LT_ERROR;

	hOldFont = (HFONT)SelectObject(g_hTextDC, pFont->m_hFont);
	SetTextCharacterExtra(g_hTextDC, pixels);
	SelectObject(g_hTextDC, hOldFont);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0049b720
static LTRESULT tmgr_GetFontExtraSpace(HLTFONT hFont, int &pixels)
{
	LTFont *pFont = (LTFont*)hFont;
	HFONT hOldFont;

	if(!pFont)
		return LT_ERROR;

	hOldFont = (HFONT)SelectObject(g_hTextDC, pFont->m_hFont);
	pixels = GetTextCharacterExtra(g_hTextDC);
	SelectObject(g_hTextDC, hOldFont);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0049b770
static void tmgr_GetStringDimensions(HLTFONT hFont, HSTRING hString, int *sizeX, int *sizeY)
{
	LTFont *pFont;
	int nChars;
	SIZE size;
	HFONT hOldFont;

	// Make sure they have valid parameters.
	pFont = (LTFont*)hFont;
	if(!pFont || !hString)
	{
		*sizeX = *sizeY = 0;
		return;
	}

	hOldFont = (HFONT)SelectObject(g_hTextDC, pFont->m_hFont);
		nChars = str_GetNumStringCharacters(hString);
		GetTextExtentPoint32(g_hTextDC, (char*)str_GetStringBytes(hString, LTNULL), nChars, &size);
	SelectObject(g_hTextDC, hOldFont);

	*sizeX = size.cx;
	*sizeY = size.cy;
}


// FUNCTION: LITHTECH 0x0049b800
static void tmgr_DrawStringToSurface(HSURFACE hDest, HLTFONT hFont, HSTRING hString,
	LTRect *pRect, HLTCOLOR hForeColor, HLTCOLOR hBackColor)
{
	LTFont *pFont;
	SIZE size;
	int nChars;
	HFONT hOldFont;


	// Parameter validation.
	if(!hFont || !hString || !hDest || !g_hTextDC)
		return;

	pFont = (LTFont*)hFont;

	// Make sure the HDC is big enough..
	hOldFont = (HFONT)SelectObject(g_hTextDC, pFont->m_hFont);
		nChars = str_GetNumStringCharacters(hString);
		GetTextExtentPoint32(g_hTextDC, (char*)str_GetStringBytes(hString, LTNULL), nChars, &size);
	SelectObject(g_hTextDC, hOldFont);

	tmgr_InternalDrawStringToSurface(hDest, hFont, hString, pRect, hForeColor, hBackColor, &size);
}


// ----------------------------------------------------------------- //
// Internal helpers.
// ----------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0049b8a0
static void tmgr_InternalDrawStringToSurface(HSURFACE hDest, HLTFONT hFont, HSTRING hString,
	LTRect *pRect, HLTCOLOR hForeColor, HLTCOLOR hBackColor, SIZE *pSize)
{
	LTFont *pFont;
	CisSurface *pDest;
	HFONT hOldFont;
	int nChars;
	LTRect textRect, destRect;


	pFont = (LTFont*)hFont;
	pDest = (CisSurface*)hDest;

	// Make sure we even have a DC.
	if(!hString || !pFont || !pDest)
		return;

	tmgr_SizeTextSurface(pSize);

	// Did it size?
	if(!g_hTextBitmap || !g_pTextBitmapBits)
		return;


	// Clear the HDC and draw the text into it.
	hOldFont = (HFONT)SelectObject(g_hTextDC, pFont->m_hFont);
		nChars = str_GetNumStringCharacters(hString);
		Rectangle(g_hTextDC, 0, 0, pSize->cx, pSize->cy);
		TextOut(g_hTextDC, 0, 0, (char*)str_GetStringBytes(hString, LTNULL), nChars);
	SelectObject(g_hTextDC, hOldFont);

	// Convert from that into the surface.
	if(pRect)
	{
		destRect = *pRect;
	}
	else
	{
		destRect.left = destRect.top = 0;
		destRect.right = pDest->m_Width;
		destRect.bottom = pDest->m_Height;
	}

	textRect.left = textRect.top = 0;
	textRect.right = pSize->cx;
	textRect.bottom = pSize->cy;
	tmgr_DrawTextToSurface(pDest, &textRect, &destRect, hForeColor, hBackColor);
}


// FUNCTION: LITHTECH 0x0049b9f0
static void tmgr_SizeTextSurface(SIZE *pSize)
{
	struct {
		BITMAPINFOHEADER bmi;
	} bmi;
	int fullWidth;


	if(pSize->cx > (int)g_TextBitmapWidth || pSize->cy > (int)g_TextBitmapHeight)
	{
		// Get rid of the old stuff if it exists.
		if(g_hOldTextBitmap)
			SelectObject(g_hTextDC, g_hOldTextBitmap);

		if(g_hTextBitmap)
			DeleteObject(g_hTextBitmap);


		// Create the new bitmap.
		fullWidth = pSize->cx;
		if(fullWidth % 32 != 0)
			fullWidth += (32 - (fullWidth % 32));

		memset(&bmi, 0, sizeof(bmi));

		bmi.bmi.biSize         = sizeof(BITMAPINFOHEADER);
		bmi.bmi.biWidth        = fullWidth;
		bmi.bmi.biHeight       = - (int)pSize->cy;
		bmi.bmi.biBitCount     = 16;
		bmi.bmi.biPlanes       = 1;
		bmi.bmi.biCompression  = BI_RGB;

		g_hTextBitmap = CreateDIBSection(g_hTextDC,
			(BITMAPINFO*)&bmi, DIB_RGB_COLORS, (void**)&g_pTextBitmapBits, LTNULL, 0);

		g_hOldTextBitmap = (HBITMAP)SelectObject(g_hTextDC, g_hTextBitmap);
		g_TextBitmapWidth = fullWidth;
		g_TextBitmapHeight = pSize->cy;
		g_TextBitmapPitch = fullWidth * 2;
	}
}


// STUB: LITHTECH 0x0049bae0
// Not decompiled yet: Talon locks the surface through the renderer's function table and
// rasterizes 16/32-bit text inline.
static void tmgr_DrawTextToSurface(CisSurface *pDest, LTRect *pSrcRect, LTRect *pDestRect,
	HLTCOLOR hForeColor, HLTCOLOR hBackColor)
{
}


// FUNCTION: LITHTECH 0x0049bf90
static HSURFACE tmgr_CreateSurfaceFromString(HLTFONT hFont, HSTRING hString,
	HLTCOLOR hForeColor, HLTCOLOR hBackColor, int extraPixelsX, int extraPixelsY)
{
	CisSurface *pDest;
	int nChars;
	LTFont *pFont;
	HFONT hOldFont;
	SIZE size;

	// Parameter validation.
	if(!hFont || !g_hTextDC)
		return LTNULL;

	pFont = (LTFont*)hFont;

	// Find out how big the surface needs to be.
	if(hString)
	{
		hOldFont = (HFONT)SelectObject(g_hTextDC, pFont->m_hFont);
			nChars = str_GetNumStringCharacters(hString);
			GetTextExtentPoint32(g_hTextDC, (char*)str_GetStringBytes(hString, LTNULL), nChars, &size);
		SelectObject(g_hTextDC, hOldFont);
	}
	else
	{
		size.cx = size.cy = 1;
	}

	size.cx += extraPixelsX;
	size.cy += extraPixelsY;
	pDest = cis_InternalCreateSurface(size.cx, size.cy);
	if(!pDest)
		return LTNULL;

	tmgr_InternalDrawStringToSurface((HSURFACE)pDest, hFont, hString, LTNULL, hForeColor, hBackColor, &size);
	return (HSURFACE)pDest;
}


// FUNCTION: LITHTECH 0x0049c060
void tmgr_Term()
{
	tmgr_DeleteAllFonts();

	// Get rid of the text DC and bitmaps.
	if(g_hTextDC)
	{
		SelectObject(g_hTextDC, g_hOldBrush);
		SelectObject(g_hTextDC, g_hOldTextBitmap);
		DeleteDC(g_hTextDC);
	}

	if(g_hTextBitmap)
	{
		DeleteObject(g_hTextBitmap);
	}

	g_hOldTextBitmap = LTNULL;
	g_hTextBitmap = LTNULL;
	g_hTextDC = LTNULL;
}


// FUNCTION: LITHTECH 0x0049c0d0
static void tmgr_DeleteAllFonts()
{
	GPOS pos;
	LTFont *pFont;

	for(pos=g_Fonts; pos; )
	{
		pFont = g_Fonts.GetNext(pos);

		tmgr_DeleteFont(pFont);
	}
}
