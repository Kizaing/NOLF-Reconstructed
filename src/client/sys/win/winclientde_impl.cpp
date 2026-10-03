// Jupiter runtime/client/src/sys/win/winclientde_impl.cpp (Talon returns LTBOOL where Jupiter has bool)
// The client surface interface (cis_): CisSurface lives in interface_helpers.h.
// (The globals shared with interface_helpers.cpp carry their GLOBAL annotations there.)
// Talon differences: no format_mgr holder (GetFormatMgr returns the client manager's FormatMgr;
// FillRect calls it directly), RenderStruct::LockSurface takes only the buffer (CisSurface::m_Pitch
// holds the pitch), the blit shortcuts go through g_Render, cis_Init fills the ILTClient it is
// given, masked drawing is 16-bit only, the warp line tables hold 1600 entries, and GetEngineHook
// only knows hwnd/cres_hinstance/cresl_hinstance (through g_ClientGlob.m_pClientMgr).
// IsVert/IsHorzSpanSolidColor must stay static: as extern functions in the same file, VC6 knows
// they don't write memory and GetBorderSize stops reloading pRect after the calls.
#include <windows.h>
#include <string.h>
#include <math.h>
#include "bdefs.h"
#include "de_memory.h"
#include "clientmgr.h"
#include "iltclient.h"
#include "interface_helpers.h"
#include "sprite.h"
#include "ltdynarray.h"
#include "../../build/proj/LT2/lithshared/stdlith/struct_bank.h"
#include "render.h"
#include "dsys_interface.h"
#include "bindmgr.h"

#define OPTIMIZE_NO_TRANSPARENCY	0xFFFFFFFF


// load_pcx.h mirrors an older CMoArray and can't be combined with ltdynarray.h; this unit
// inlines LoadedBitmap's destructor (the real CMoArray<uint8>::~CMoArray), so it declares the
// class over the real CMoArray here. Same layout (size 0x458).
class LoadedBitmap
{
public:
					LoadedBitmap();		// 0x00446170

	PFormat			m_Format;			// 0x000
	RPaletteColor	m_Palette[256];		// 0x038
	unsigned long	m_Width;			// 0x438
	unsigned long	m_Height;			// 0x43C
	unsigned long	m_Pitch;			// 0x440
	CMoArray<uint8>	m_Data;				// 0x444
};

LTBOOL pcx_Create2(ILTStream *pStream, LoadedBitmap *pBitmap);	// 0x004461d0
void tmgr_Init(ILTClient *pClientDE);	// 0x0049b4c0
void tmgr_Term();						// 0x0049c060
FormatMgr* GetFormatMgr();


// ----------------------------------------------------------------- //
// Globals.
// ----------------------------------------------------------------- //

// The screen surface.. treated specially.
CisSurface g_ScreenSurface;

// The render struct we're using..
RenderStruct *g_pCisRenderStruct;

// Used for transparent drawing.
GenericColor g_TransparentColor;
GenericColor g_SolidColor;

// The surface allocator..
// GLOBAL: LITHTECH 0x004dedc4
static StructBank g_SurfaceBank;

// All the surfaces..
// FUNCTION: LITHTECH 0x0040c470 _$E4
// FUNCTION: LITHTECH 0x0040c480 _$E1
// FUNCTION: LITHTECH 0x0040c4a0 _$E3
// FUNCTION: LITHTECH 0x0040c4b0 _$E2
// GLOBAL: LITHTECH 0x004def58
static CGLinkedList<CisSurface*> g_Surfaces;

// Used for masked drawing.
// GLOBAL: LITHTECH 0x004dedc0
static CisSurface *g_pMask;

// Helper for masked drawing.
// GLOBAL: LITHTECH 0x004dee10
static uint8 g_MaskLookup[257];

// FUNCTION: LITHTECH 0x0040c4d0 _$E7
// FUNCTION: LITHTECH 0x0040c4e0 _$E6
PFormat g_ScreenFormat;
uint32 g_nScreenPixelBytes;

// Used when restoring surfaces.
// FUNCTION: LITHTECH 0x0040c4f0 _$E10
// FUNCTION: LITHTECH 0x0040c500 _$E9
// GLOBAL: LITHTECH 0x004ded88
static PFormat g_PrevScreenFormat;
// GLOBAL: LITHTECH 0x004def50
static uint32 g_nPrevScreenPixelBytes;


static void cis_InternalBitmapToSurface(CisSurface *pDest, LoadedBitmap *pSrc,
	LTRect *pSrcRect, int destX, int destY);
static void cis_DeleteSurfaceBuffer(CisSurface *pSurface);
static void cis_DeleteSurfaceBackupBuffer(CisSurface *pSurface);
static LTBOOL IsVertSpanSolidColor(uint8 *pBuf, GenericColor color, uint32 height, long pitch);
static LTBOOL IsHorzSpanSolidColor(uint8 *pBuf, GenericColor color, uint32 width);
static LTRESULT cis_WarpSurfaceToSurface(HSURFACE hDest, HSURFACE hSrc, 
	LTWarpPt *pCoords, int nCoords);
static LTRESULT cis_WarpSurfaceToSurfaceTransparent(HSURFACE hDest, HSURFACE hSrc, 
	LTWarpPt *pCoords, int nCoords, HLTCOLOR hColor);
static LTRESULT cis_WarpSurfaceToSurfaceSolidColor(HSURFACE hDest, HSURFACE hSrc, 
	LTWarpPt *pCoords, int nCoords, HLTCOLOR hTransColor, HLTCOLOR hFillColor);


// FUNCTION: LITHTECH 0x0040c510
FormatMgr* GetFormatMgr()
{
	return &g_pClientMgr->m_FormatMgr;
}

// FUNCTION: LITHTECH 0x0040c520
CisSurface* cis_InternalCreateSurface(uint32 width, uint32 height)
{
	CisSurface *pSurface;
	HLTBUFFER hBuffer;
	uint32 surfWidth, surfHeight;
	long surfPitch;

	if(width == 0 || height == 0 || width > 5000 || height > 5000)
		return LTNULL;

	if(!g_pCisRenderStruct)
		return LTNULL;

	hBuffer = g_pCisRenderStruct->CreateSurface(width, height);
	if(!hBuffer)
		return LTNULL;

	g_pCisRenderStruct->GetSurfaceInfo(hBuffer, &surfWidth, &surfHeight, &surfPitch);

	pSurface = (CisSurface*)sb_Allocate(&g_SurfaceBank);
	pSurface->m_hBuffer = hBuffer;
	pSurface->m_pBackupBuffer = LTNULL;
	pSurface->m_Width = surfWidth;
	pSurface->m_Height = surfHeight;
	pSurface->m_Pitch = surfPitch;
	pSurface->m_Flags = 0;
	pSurface->m_pUserData = LTNULL;
	pSurface->m_Alpha = 1.0f;

	g_Surfaces.AddHead(pSurface);
	return pSurface;
}

// FUNCTION: LITHTECH 0x0040c650
HSURFACE cis_CreateSurfaceFromPcx(LoadedBitmap *pLoadedBitmap)
{
	CisSurface *pSurface;

	// Create a surface for it.
	pSurface = cis_InternalCreateSurface(pLoadedBitmap->m_Width, pLoadedBitmap->m_Height);
	if(!pSurface)
	{
		return LTNULL;
	}

	cis_InternalBitmapToSurface(pSurface, pLoadedBitmap, LTNULL, 0, 0);
	return (HSURFACE)pSurface;
}

// FUNCTION: LITHTECH 0x0040c690
static void cis_InternalBitmapToSurface(CisSurface *pDest, LoadedBitmap *pSrc,
	LTRect *pSrcRect, int destX, int destY)
{
	LTRect srcRect, destRect;
	FMConvertRequest request;
	LTRESULT dResult;


	if(pSrcRect)
	{
		cis_ClipRectsNonScaled(
			pSrc->m_Width, pSrc->m_Height, pSrcRect->left, pSrcRect->top, pSrcRect->right, pSrcRect->bottom,
			pDest->m_Width, pDest->m_Height, destX, destY, &srcRect, &destRect);
	}
	else
	{
		cis_ClipRectsNonScaled(
			pSrc->m_Width, pSrc->m_Height, 0, 0, pSrc->m_Width, pSrc->m_Height,
			pDest->m_Width, pDest->m_Height, destX, destY, &srcRect, &destRect);
	}

	// Read the stuff in.
	request.m_pDest = (uint8*)cis_LockSurface(pDest, request.m_DestPitch, LTTRUE);
	if(!request.m_pDest)
		return;

	request.m_pDestFormat = &g_ScreenFormat;
	request.m_pDest += destRect.top*request.m_DestPitch + destRect.left*g_nScreenPixelBytes;

	request.m_pSrcFormat = &pSrc->m_Format;
	request.m_pSrc = pSrc->m_Data.GetArray();
	request.m_pSrc += srcRect.top*pSrc->m_Pitch + srcRect.left;
	request.m_pSrcPalette = pSrc->m_Palette;
	request.m_SrcPitch = pSrc->m_Pitch;

	request.m_Width = (uint32)(srcRect.right - srcRect.left);
	request.m_Height = (uint32)(srcRect.bottom - srcRect.top);
	request.m_Flags = 0;

	dResult = GetFormatMgr()->ConvertPixels(&request);
	cis_UnlockSurface(pDest);
}

// FUNCTION: LITHTECH 0x0040c880
LTRESULT cis_DeleteSurface(HSURFACE hSurface)
{
	CisSurface *pSurface = (CisSurface*)hSurface;

	if(!pSurface)
	{
		RETURN_ERROR_PARAM(1, ILTClient::DeleteSurface, LT_ERROR, "(null surface)");
	}

	if(!g_pCisRenderStruct)
	{
		RETURN_ERROR_PARAM(1, ILTClient::DeleteSurface, LT_ERROR, "(renderer not initialized)");
	}

	if(cis_IsScreenSurface(pSurface))
	{
	}
	else
	{
		g_Surfaces.RemoveAt(pSurface);
		cis_DeleteSurfaceBackupBuffer(pSurface);
		cis_DeleteSurfaceBuffer(pSurface);
		sb_Free(&g_SurfaceBank, pSurface);
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040c970
static void cis_DeleteSurfaceBuffer(CisSurface *pSurface)
{
	if(pSurface->m_hBuffer)
	{
		g_pCisRenderStruct->DeleteSurface(pSurface->m_hBuffer);
		pSurface->m_hBuffer = LTNULL;
	}
}

// FUNCTION: LITHTECH 0x0040c9a0
static void cis_DeleteSurfaceBackupBuffer(CisSurface *pSurface)
{
	if(pSurface->m_pBackupBuffer)
	{
		dfree(pSurface->m_pBackupBuffer);
		pSurface->m_pBackupBuffer = LTNULL;
	}
}

// FUNCTION: LITHTECH 0x0040c9c0
LTRESULT cis_GetPixel(HSURFACE hSurface, uint32 x, uint32 y, HLTCOLOR *color)
{
	CisSurface *pSurface;
	uint8 *pBuffer;

	pSurface = (CisSurface*)hSurface;
	if(!pSurface || x >= pSurface->m_Width || y >= pSurface->m_Height)
		RETURN_ERROR(1, ILTClient::GetPixel, LT_INVALIDPARAMS);

	pBuffer = (uint8*)g_pCisRenderStruct->LockSurface(pSurface->m_hBuffer);
	if(!pBuffer)
		RETURN_ERROR(1, ILTClient::GetPixel, LT_ERROR);

	pBuffer += y*pSurface->m_Pitch + x*g_nScreenPixelBytes;
	GetFormatMgr()->PValueFromFormatColor(&g_ScreenFormat, *((GenericColor*)pBuffer), *color);

	g_pCisRenderStruct->UnlockSurface(pSurface->m_hBuffer);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0040cac0
LTRESULT cis_SetPixel(HSURFACE hSurface, uint32 x, uint32 y, HLTCOLOR color)
{
	CisSurface *pSurface;
	uint8 *pBuffer;

	pSurface = (CisSurface*)hSurface;
	if(!pSurface || x >= pSurface->m_Width || y >= pSurface->m_Height)
		RETURN_ERROR(1, ILTClient::SetPixel, LT_INVALIDPARAMS);

	pBuffer = (uint8*)g_pCisRenderStruct->LockSurface(pSurface->m_hBuffer);
	if(!pBuffer)
		RETURN_ERROR(1, ILTClient::SetPixel, LT_ERROR);

	pBuffer += y*pSurface->m_Pitch + x*g_nScreenPixelBytes;
	GetFormatMgr()->PValueToFormatColor(&g_ScreenFormat, color, *((GenericColor*)pBuffer));

	g_pCisRenderStruct->UnlockSurface(pSurface->m_hBuffer);
	pSurface->m_Flags |= SURFFLAG_OPTIMIZEDIRTY;
	return LT_OK;
}

// ----------------------------------------------------------------- //
// Drawing helpers.
// ----------------------------------------------------------------- //

typedef LTRESULT (*DrawPixelsFn)(LTBOOL bSameSurface, uint8 *pSrcData, uint8 *pDestData,
	long srcPitch, long destPitch, LTRect *pSrcRect, LTRect *pDestRect);


inline void cis_SetTransparentColor(HLTCOLOR inColor)
{
	GetFormatMgr()->PValueToFormatColor(&g_ScreenFormat, inColor, g_TransparentColor);
}

inline void cis_SetSolidColor(HLTCOLOR inColor)
{
	GetFormatMgr()->PValueToFormatColor(&g_ScreenFormat, inColor, g_SolidColor);
}


// FUNCTION: LITHTECH 0x0040d9f0
static LTRESULT cis_OpaqueDraw(LTBOOL bSameSurface,
	uint8 *pSrcLine, uint8 *pDestLine,
	long srcPitch, long destPitch, LTRect *pSrcRect, LTRect *pDestRect)
{
	uint32 y, rectWidth, rectHeight, bytesPerLine;

	rectWidth = (uint32)(pSrcRect->right - pSrcRect->left);
	rectHeight = (uint32)(pSrcRect->bottom - pSrcRect->top);
	bytesPerLine = rectWidth * g_nScreenPixelBytes;

	pSrcLine += pSrcRect->top*srcPitch + pSrcRect->left*g_nScreenPixelBytes;
	pDestLine += pDestRect->top*destPitch + pDestRect->left*g_nScreenPixelBytes;

	for(y=0; y < rectHeight; y++)
	{
		if(bSameSurface)
		{
			memmove(pDestLine, pSrcLine, bytesPerLine);
		}
		else
		{
			memcpy(pDestLine, pSrcLine, bytesPerLine);
		}

		pSrcLine += srcPitch;
		pDestLine += destPitch;
	}

	return LT_OK;
}


template<class P>
inline void cis_SolidColorDrawLine(uint8 *pSrcLine, uint8 *pDestLine, uint32 width, P *pixelType)
{
	P src, dest;

	src = pSrcLine;
	dest = pDestLine;
	while(width)
	{
		width--;

		if(src != g_TransparentColor)
			dest = g_SolidColor;

		++src;
		++dest;
	}
}

// FUNCTION: LITHTECH 0x0040d560
static LTRESULT cis_SolidColorDraw(LTBOOL bSameSurface,
	uint8 *pSrcLine, uint8 *pDestLine,
	long srcPitch, long destPitch, LTRect *pSrcRect, LTRect *pDestRect)
{
	uint32 y, rectWidth, rectHeight;

	rectWidth = (uint32)(pSrcRect->right - pSrcRect->left);
	rectHeight = (uint32)(pSrcRect->bottom - pSrcRect->top);

	pSrcLine += pSrcRect->top*srcPitch + pSrcRect->left*g_nScreenPixelBytes;
	pDestLine += pDestRect->top*destPitch + pDestRect->left*g_nScreenPixelBytes;

	for(y=0; y < rectHeight; y++)
	{
		if(g_ScreenFormat.m_eType == BPP_16)
		{
			cis_SolidColorDrawLine(pSrcLine, pDestLine, rectWidth, (Pixel16*)LTNULL);
		}
		else if(g_ScreenFormat.m_eType == BPP_32)
		{
			cis_SolidColorDrawLine(pSrcLine, pDestLine, rectWidth, (Pixel32*)LTNULL);
		}

		pSrcLine += srcPitch;
		pDestLine += destPitch;
	}

	return LT_OK;
}


template<class P>
inline void cis_TransparentDrawLine(uint8 *pSrcLine, uint8 *pDestLine, uint32 width, P *pixelType)
{
	P src, dest;

	src = pSrcLine;
	dest = pDestLine;
	while(width)
	{
		width--;

		if(src != g_TransparentColor)
			dest = src;

		++src;
		++dest;
	}
}

// FUNCTION: LITHTECH 0x0040daa0
static LTRESULT cis_TransparentDraw(LTBOOL bSameSurface,
	uint8 *pSrcData, uint8 *pDestData,
	long srcPitch, long destPitch, LTRect *pSrcRect, LTRect *pDestRect)
{
	uint32 y, rectWidth, rectHeight;
	uint8 *pSrcLine, *pDestLine;

	rectWidth = (uint32)(pSrcRect->right - pSrcRect->left);
	rectHeight = (uint32)(pSrcRect->bottom - pSrcRect->top);

	pSrcLine = pSrcData + pSrcRect->top*srcPitch + pSrcRect->left*g_nScreenPixelBytes;
	pDestLine = pDestData + pDestRect->top*destPitch + pDestRect->left*g_nScreenPixelBytes;

	for(y=0; y < rectHeight; y++)
	{
		if(g_ScreenFormat.m_eType == BPP_16)
		{
			cis_TransparentDrawLine(pSrcLine, pDestLine, rectWidth, (Pixel16*)LTNULL);
		}
		else if(g_ScreenFormat.m_eType == BPP_32)
		{
			cis_TransparentDrawLine(pSrcLine, pDestLine, rectWidth, (Pixel32*)LTNULL);
		}

		pSrcLine += srcPitch;
		pDestLine += destPitch;
	}

	return LT_OK;
}


template<class P>
inline void cis_MaskedDrawLine(uint8 *pSrcLine, uint8 *pDestLine, uint32 width,
							   uint8 *pMaskLine, uint32 maskMaskX, uint32 maskX, P *pixelType)
{
	P src, dest, mask;

	src = pSrcLine;
	dest = pDestLine;
	mask = pMaskLine;

	while(width)
	{
		width--;

		if(src != g_TransparentColor)
		{
			dest = mask[maskX & maskMaskX];
		}

		++maskX;
		++src;
		++dest;
	}
}


// If the surface's contents are dirty, reoptimize the surface.
// FUNCTION: LITHTECH 0x0040db60
static void cis_OptimizeDirty(CisSurface *pSurface)
{
	if(!pSurface)
		return;

	if((pSurface->m_Flags & SURFFLAG_OPTIMIZED) && (pSurface->m_Flags & SURFFLAG_OPTIMIZEDIRTY))
	{
		g_pCisRenderStruct->OptimizeSurface(pSurface->m_hBuffer, pSurface->m_OptimizedTransparentColor);
		pSurface->m_Flags &= ~SURFFLAG_OPTIMIZEDIRTY;
	}
}


// FUNCTION: LITHTECH 0x0040dc30
static LTRESULT cis_MaskedDraw(LTBOOL bSameSurface,
	uint8 *pSrcLine, uint8 *pDestLine,
	long srcPitch, long destPitch, LTRect *pSrcRect, LTRect *pDestRect)
{
	FN_NAME(cis_MaskedDraw);

	uint8 *pMaskData;
	uint32 y, maskMaskX, maskMaskY;
	long maskPitch;
	uint32 rectWidth, rectHeight;

	CHECK_PARAMS2(g_pMask && g_pMask->m_Width <= 256 && g_pMask->m_Height <= 256);

	maskMaskX = g_MaskLookup[g_pMask->m_Width];
	maskMaskY = g_MaskLookup[g_pMask->m_Height];

	CHECK_PARAMS2(maskMaskX && maskMaskY);

	pMaskData = (uint8*)cis_LockSurface(g_pMask, maskPitch);
	if(!pMaskData)
	{
		ERR(1, LT_ERROR);
	}

	rectWidth = (uint32)(pSrcRect->right - pSrcRect->left);
	rectHeight = (uint32)(pSrcRect->bottom - pSrcRect->top);
	pSrcLine += pSrcRect->top*srcPitch + pSrcRect->left*g_nScreenPixelBytes;
	pDestLine += pDestRect->top*destPitch + pDestRect->left*g_nScreenPixelBytes;

	for(y=0; y < rectHeight; y++)
	{
		// (Talon only draws masks onto 16-bit screens.)
		if(g_ScreenFormat.m_eType == BPP_16)
		{
			cis_MaskedDrawLine(pSrcLine, pDestLine, rectWidth,
				&pMaskData[((pSrcRect->top+y)&maskMaskY)*maskPitch], maskMaskX,
				pSrcRect->left, (Pixel16*)LTNULL);
		}

		pSrcLine += srcPitch;
		pDestLine += destPitch;
	}

	cis_UnlockSurface(g_pMask);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0040d630
static LTRESULT cis_DoDrawSurfaceToSurface(HSURFACE hDest, HSURFACE hSrc,
	LTRect *pSrcRect, int destX, int destY, DrawPixelsFn fn)
{
	FN_NAME(cis_DoDrawSurfaceToSurface);

	CisSurface *pSrc = (CisSurface*)hSrc;
	CisSurface *pDest = (CisSurface*)hDest;
	uint8 *pSrcData, *pDestData;
	LTRect srcRect, destRect;
	LTBOOL bIsInside;
	long srcPitch, destPitch;
	BlitRequest blitRequest;
	LTBOOL bOk;

	CHECK_PARAMS2(pSrc && pDest);

	if(pSrcRect)
	{
		bIsInside = cis_ClipRectsNonScaled(
			pSrc->m_Width, pSrc->m_Height, pSrcRect->left, pSrcRect->top, pSrcRect->right, pSrcRect->bottom,
			pDest->m_Width, pDest->m_Height, destX, destY, &srcRect, &destRect);
	}
	else
	{
		bIsInside = cis_ClipRectsNonScaled(
			pSrc->m_Width, pSrc->m_Height, 0, 0, pSrc->m_Width, pSrc->m_Height,
			pDest->m_Width, pDest->m_Height, destX, destY, &srcRect, &destRect);
	}

	if(!bIsInside)
		return LT_OK;

	// Try to translate the command to let the RenderStruct do it.
	if(fn == cis_OpaqueDraw ||
		fn == cis_TransparentDraw)
	{
		if(pDest == &g_ScreenSurface)
		{
			if(g_Render.BlitToScreen)
			{
				cis_OptimizeDirty(pSrc);

				blitRequest.m_hBuffer = pSrc->m_hBuffer;
				blitRequest.m_TransparentColor = g_TransparentColor;
				blitRequest.m_pSrcRect = &srcRect;
				blitRequest.m_pDestRect = &destRect;
				blitRequest.m_BlitOptions = (fn == cis_TransparentDraw) ? BLIT_TRANSPARENT : 0;
				blitRequest.m_Alpha = pSrc->m_Alpha;

				g_Render.BlitToScreen(&blitRequest);

				pDest->m_Flags |= SURFFLAG_OPTIMIZEDIRTY;
				return LT_OK;
			}
		}
		else if (pSrc==&g_ScreenSurface)
		{
			// do a screen read...
			if ( g_Render.BlitFromScreen )
			{
				blitRequest.m_hBuffer = pDest->m_hBuffer;
				blitRequest.m_TransparentColor = g_TransparentColor;
				blitRequest.m_pSrcRect = &srcRect;
				blitRequest.m_pDestRect = &destRect;
				blitRequest.m_BlitOptions = (fn == cis_TransparentDraw) ? BLIT_TRANSPARENT : 0;

				g_Render.BlitFromScreen(&blitRequest);
				return LT_OK;
			}
		}
	}

	bOk = LTFALSE;
	if(pSrcData = (uint8*)cis_LockSurface(pSrc, srcPitch))
	{
		if(pSrc == pDest)
		{
			if(fn(LTTRUE, pSrcData, pSrcData, srcPitch, srcPitch, &srcRect, &destRect) == LT_OK)
				bOk = LTTRUE;
		}
		else
		{
			if(pDestData = (uint8*)cis_LockSurface(pDest, destPitch, LTTRUE))
			{
				if(fn(LTFALSE, pSrcData, pDestData, srcPitch, destPitch, &srcRect, &destRect) == LT_OK)
					bOk = LTTRUE;

				cis_UnlockSurface(pDest);
			}
		}

		cis_UnlockSurface(pSrc);
	}

	if(bOk)
	{
		return LT_OK;
	}
	else
	{
		ERR(1, LT_ERROR);
	}
}


// ----------------------------------------------------------------- //
// Interface implementation functions.
// ----------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0040cd80
static HLTCOLOR cis_CreateColor(float r, float g, float b, LTBOOL bTransparent)
{
	if(bTransparent)
	{
		return SETRGB_FT(r,g,b);
	}
	else
	{
		return SETRGB_F(r,g,b);
	}
}

static void cis_DeleteColor(HLTCOLOR hColor)
{
}

// FUNCTION: LITHTECH 0x0040cde0
static HLTCOLOR cis_SetupColor1(float r, float g, float b, LTBOOL bTransparent)
{
	return cis_CreateColor(r, g, b, bTransparent);
}

// FUNCTION: LITHTECH 0x0040ce00
static HLTCOLOR cis_SetupColor2(float r, float g, float b, LTBOOL bTransparent)
{
	return cis_CreateColor(r, g, b, bTransparent);
}


// FUNCTION: LITHTECH 0x0040ce20
static LTRESULT cis_GetBorderSize(HSURFACE hSurface, HLTCOLOR hColor, LTRect *pRect)
{
	CisSurface *pSurface;
	uint8 *pBuf;
	GenericColor theColor;

	pSurface = (CisSurface*)hSurface;
	if (!pSurface || !pRect || !pSurface->m_hBuffer) {
		RETURN_ERROR(1, GetBorderSize, LT_INVALIDPARAMS); }
	else if (!g_pCisRenderStruct) {
		RETURN_ERROR(1, GetBorderSize, LT_NOTINITIALIZED); }

	pBuf = (uint8*)g_pCisRenderStruct->LockSurface(pSurface->m_hBuffer);
	if (!pBuf) { RETURN_ERROR(1, GetBorderSize, LT_ERROR); }

	pRect->left = pRect->top = pRect->right = pRect->bottom = 0;
	GetFormatMgr()->PValueToFormatColor(&g_ScreenFormat, hColor, theColor);

	// Test each side.
	while (pRect->left < (int)pSurface->m_Width && IsVertSpanSolidColor(&pBuf[pRect->left], theColor, pSurface->m_Height, pSurface->m_Pitch)) {
		++pRect->left; }

	while (pRect->right < (int)pSurface->m_Width && IsVertSpanSolidColor(&pBuf[pSurface->m_Width-pRect->right-1], theColor, pSurface->m_Height, pSurface->m_Pitch)) {
		++pRect->right; }

	while(pRect->top < (int)pSurface->m_Height && IsHorzSpanSolidColor(&pBuf[pRect->top*pSurface->m_Pitch], theColor, pSurface->m_Width)) {
		++pRect->top; }

	while(pRect->bottom < (int)pSurface->m_Height && IsHorzSpanSolidColor(&pBuf[(pSurface->m_Height-pRect->bottom-1)*pSurface->m_Pitch], theColor, pSurface->m_Width)) {
		++pRect->bottom; }

	g_pCisRenderStruct->UnlockSurface(pSurface->m_hBuffer);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0040d050
static LTBOOL IsVertSpanSolidColor(uint8 *pBuf, GenericColor color, uint32 height, long pitch)
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
static LTBOOL IsHorzSpanSolidColor(uint8 *pBuf, GenericColor color, uint32 width)
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



// FUNCTION: LITHTECH 0x0040d110
static LTRESULT cis_OptimizeSurface(HSURFACE hSurface, HLTCOLOR hTransparentColor)
{
	CisSurface *pSurface;

	if(!hSurface)
		RETURN_ERROR(1, OptimizeSurface, LT_INVALIDPARAMS);

	pSurface = (CisSurface*)hSurface;
	pSurface->m_OptimizedTransparentColor = (uint32)hTransparentColor;

	// The render driver wants a uint32 color so either get rid of the
	// COLOR_TRANSPARENCY_MASK or set it to OPTIMIZE_NO_TRANSPARENCY.
	if(pSurface->m_OptimizedTransparentColor & COLOR_TRANSPARENCY_MASK)
		pSurface->m_OptimizedTransparentColor &= ~COLOR_TRANSPARENCY_MASK;
	else
		pSurface->m_OptimizedTransparentColor = OPTIMIZE_NO_TRANSPARENCY;

	pSurface->m_Flags |= SURFFLAG_OPTIMIZED;

	if(g_pCisRenderStruct && g_pCisRenderStruct->m_bInitted)
	{
		if(g_pCisRenderStruct->OptimizeSurface(pSurface->m_hBuffer, pSurface->m_OptimizedTransparentColor))
		{
			pSurface->m_Flags &= ~SURFFLAG_OPTIMIZEDIRTY;
			return LT_OK;
		}
		else
		{
			return LT_ERROR;
		}
	}
	else
	{
		// Just return out.. we'll optimize it when we recreate the surface.
		return LT_OK;
	}
}


// FUNCTION: LITHTECH 0x0040d1b0
static LTRESULT cis_UnoptimizeSurface(HSURFACE hSurface)
{
	CisSurface *pSurface;

	if(!hSurface)
		RETURN_ERROR(1, OptimizeSurface, LT_INVALIDPARAMS);

	pSurface = (CisSurface*)hSurface;
	if(!(pSurface->m_Flags & SURFFLAG_OPTIMIZED))
		return LT_OK;

	pSurface->m_Flags &= ~SURFFLAG_OPTIMIZED;
	if(g_pCisRenderStruct->m_bInitted)
	{
		g_pCisRenderStruct->UnoptimizeSurface(pSurface->m_hBuffer);
		return LT_OK;
	}
	else
	{
		return LT_OK;
	}
}


// FUNCTION: LITHTECH 0x0040d220
static HSURFACE cis_GetScreenSurface()
{
	if(!g_pCisRenderStruct)
		return LTNULL;

	return (HSURFACE)&g_ScreenSurface;
}


// FUNCTION: LITHTECH 0x0040d2e0
static LTBOOL cis_LoadPcx(char *pBitmapName, LoadedBitmap *pBitmap)
{
	ILTStream *pStream;
	FileRef ref;
	LTBOOL bRet;

	ref.m_FileType = FILE_CLIENTFILE;
	ref.m_pFilename = pBitmapName;

	pStream = cf_OpenFile(g_pClientMgr->m_hFileMgr, &ref);
	if(!pStream)
		return LTNULL;

	bRet = pcx_Create2(pStream, pBitmap);
	pStream->Release();
	return bRet;
}


// FUNCTION: LITHTECH 0x0040d230
static HSURFACE cis_CreateSurfaceFromBitmap(char *pBitmapName)
{
	LoadedBitmap bitmap;
	HSURFACE hRet;


	if(!cis_LoadPcx(pBitmapName, &bitmap))
		return LTNULL;

	hRet = cis_CreateSurfaceFromPcx(&bitmap);
	return hRet;
}


// FUNCTION: LITHTECH 0x0040d340
static HSURFACE cis_CreateSurface(uint32 width, uint32 height)
{
	return (HSURFACE)cis_InternalCreateSurface(width, height);
}


// FUNCTION: LITHTECH 0x0040d350
static void* cis_GetSurfaceUserData(HSURFACE hSurf)
{
	if(hSurf)
	{
		return ((CisSurface*)hSurf)->m_pUserData;
	}
	else
	{
		return LTNULL;
	}
}


// FUNCTION: LITHTECH 0x0040d360
static void cis_SetSurfaceUserData(HSURFACE hSurf, void *pUserData)
{
	if(hSurf)
	{
		((CisSurface*)hSurf)->m_pUserData = pUserData;
	}
}


// FUNCTION: LITHTECH 0x0040d370
static void cis_GetSurfaceDims(HSURFACE hSurf, uint32 *pWidth, uint32 *pHeight)
{
	CisSurface *pSurface = (CisSurface*)hSurf;

	if(pSurface)
	{
		if(pWidth)
			*pWidth = pSurface->m_Width;

		if(pHeight)
			*pHeight = pSurface->m_Height;
	}
	else
	{
		*pWidth = *pHeight = 0;
	}
}

// FUNCTION: LITHTECH 0x0040d3b0
static LTBOOL cis_DrawBitmapToSurface(HSURFACE hDest, char *pSourceBitmapName,
	LTRect *pSrcRect, int destX, int destY)
{
	LoadedBitmap bitmap;
	CisSurface *pSurface;


	pSurface = (CisSurface*)hDest;
	if(!pSurface)
		return LTFALSE;

	if(!cis_LoadPcx(pSourceBitmapName, &bitmap))
		return LTFALSE;

	cis_InternalBitmapToSurface(pSurface, &bitmap, pSrcRect, destX, destY);
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x0040d4c0
static LTRESULT cis_DrawSurfaceSolidColor(HSURFACE hDest, HSURFACE hSrc,
	LTRect *pSrcRect, int destX, int destY, HLTCOLOR hTransColor, HLTCOLOR hFillColor)
{
	if(!g_pCisRenderStruct)
		RETURN_ERROR(1, DrawSurfaceSolidColor, LT_NOTINITIALIZED);

	cis_SetTransparentColor(hTransColor);
	cis_SetSolidColor(hFillColor);
	return cis_DoDrawSurfaceToSurface(hDest, hSrc, pSrcRect, destX, destY, cis_SolidColorDraw);
}

// FUNCTION: LITHTECH 0x0040dba0
static LTRESULT cis_DrawSurfaceMasked(HSURFACE hDest, HSURFACE hSrc, HSURFACE hMask,
	LTRect *pSrcRect, int destX, int destY, HLTCOLOR hColor)
{
	if(!g_pCisRenderStruct)
		RETURN_ERROR(1, DrawSurfaceMasked, LT_NOTINITIALIZED);

	g_pMask = (CisSurface*)hMask;
	cis_SetTransparentColor(hColor);
	return cis_DoDrawSurfaceToSurface(hDest, hSrc, pSrcRect, destX, destY, cis_MaskedDraw);
}

// FUNCTION: LITHTECH 0x0040dee0
static LTRESULT cis_DrawSurfaceToSurface(HSURFACE hDest, HSURFACE hSrc,
	LTRect *pSrcRect, int destX, int destY)
{
	if(!g_pCisRenderStruct)
		RETURN_ERROR(1, DrawSurfaceToSurface, LT_NOTINITIALIZED);

	return cis_DoDrawSurfaceToSurface(hDest, hSrc, pSrcRect, destX, destY, cis_OpaqueDraw);
}


// FUNCTION: LITHTECH 0x0040df50
static LTRESULT cis_DrawSurfaceToSurfaceTransparent(HSURFACE hDest, HSURFACE hSrc,
	LTRect *pSrcRect, int destX, int destY, HLTCOLOR hColor)
{
	if(!g_pCisRenderStruct)
		RETURN_ERROR(1, DrawSurfaceToSurfaceTransparent, LT_NOTINITIALIZED);

	cis_SetTransparentColor(hColor);
	return cis_DoDrawSurfaceToSurface(hDest, hSrc, pSrcRect, destX, destY, cis_TransparentDraw);
}


// tType 0 = transparent
// tType 1 = solid
// tType 2 = solid color fill (for nontransparent pixels)

// FUNCTION: LITHTECH 0x0040e030
static LTRESULT cis_InternalScaleSurfaceToSurface(HSURFACE hDest, HSURFACE hSrc, LTRect *pDestRect, LTRect *pSrcRect, int tType, HLTCOLOR tColor, HLTCOLOR fillColor)
{
	LTRect destRect, srcRect;
	CisSurface *pSrc, *pDest;
	LTWarpPt warpPoints[4];
	LTBOOL bIsInside;
	BlitRequest blitRequest;


	pSrc = (CisSurface*)hSrc;
	pDest = (CisSurface*)hDest;
	if(!pSrc || !pDest)
		RETURN_ERROR(4, InternalScaleSurfaceToSurface, LT_INVALIDPARAMS);

	if(pDestRect)
	{
		destRect = *pDestRect;
	}
	else
	{
		destRect.left = destRect.top = 0;
		destRect.right = pDest->m_Width;
		destRect.bottom = pDest->m_Height;
	}

	if(pSrcRect)
	{
		srcRect = *pSrcRect;
	}
	else
	{
		srcRect.left = srcRect.top = 0;
		srcRect.right = pSrc->m_Width;
		srcRect.bottom = pSrc->m_Height;
	}

	// Try to get the RenderStruct to do it.
	if(tType == 0 || tType == 1)
	{
		if(pDest == &g_ScreenSurface)
		{
			bIsInside = cis_ClipRectsScaled(
				pSrc->m_Width, pSrc->m_Height, srcRect.left, srcRect.top, srcRect.right, srcRect.bottom,
				pDest->m_Width, pDest->m_Height, destRect.left, destRect.top, destRect.right, destRect.bottom,
				&srcRect, &destRect);

			if(bIsInside)
			{
				if(g_Render.BlitToScreen)
				{
					cis_OptimizeDirty(pSrc);

					blitRequest.m_hBuffer = pSrc->m_hBuffer;
					blitRequest.m_TransparentColor = g_TransparentColor;
					blitRequest.m_pSrcRect = &srcRect;
					blitRequest.m_pDestRect = &destRect;
					blitRequest.m_BlitOptions = (tType == 0) ? BLIT_TRANSPARENT : 0;
					blitRequest.m_Alpha = pSrc->m_Alpha;

					g_Render.BlitToScreen(&blitRequest);

					pDest->m_Flags |= SURFFLAG_OPTIMIZEDIRTY;
					return LT_OK;
				}
			}
		}
	}


	warpPoints[0].dest_x = (float)destRect.left;
	warpPoints[0].dest_y = (float)destRect.top;
	warpPoints[0].source_x = (float)srcRect.left;
	warpPoints[0].source_y = (float)srcRect.top;

	warpPoints[1].dest_x = (float)destRect.right;
	warpPoints[1].dest_y = (float)destRect.top;
	warpPoints[1].source_x = (float)srcRect.right;
	warpPoints[1].source_y = (float)srcRect.top;

	warpPoints[2].dest_x = (float)destRect.right;
	warpPoints[2].dest_y = (float)destRect.bottom;
	warpPoints[2].source_x = (float)srcRect.right;
	warpPoints[2].source_y = (float)srcRect.bottom;

	warpPoints[3].dest_x = (float)destRect.left;
	warpPoints[3].dest_y = (float)destRect.bottom;
	warpPoints[3].source_x = (float)srcRect.left;
	warpPoints[3].source_y = (float)srcRect.bottom;

	if(tType == 0)
		return cis_WarpSurfaceToSurfaceTransparent(hDest, hSrc, warpPoints, 4, tColor);
	else if(tType == 1)
		return cis_WarpSurfaceToSurface(hDest, hSrc, warpPoints, 4);
	else// if(tType == 2)
		return cis_WarpSurfaceToSurfaceSolidColor(hDest, hSrc, warpPoints, 4, tColor, fillColor);
}


// FUNCTION: LITHTECH 0x0040dfc0
static LTRESULT cis_ScaleSurfaceToSurface(HSURFACE hDest, HSURFACE hSrc,
	LTRect *pDestRect, LTRect *pSrcRect)
{
	if(!g_pCisRenderStruct)
		RETURN_ERROR(4, ScaleSurfaceToSurface, LT_NOTINITIALIZED);

	return cis_InternalScaleSurfaceToSurface(hDest, hSrc, pDestRect, pSrcRect, 1, LTNULL, LTNULL);
}


// FUNCTION: LITHTECH 0x0040e320
static LTRESULT cis_ScaleSurfaceToSurfaceTransparent(HSURFACE hDest, HSURFACE hSrc,
	LTRect *pDestRect, LTRect *pSrcRect, HLTCOLOR hColor)
{
	if(!g_pCisRenderStruct)
		RETURN_ERROR(1, ScaleSurfaceToSurfaceTransparent, LT_NOTINITIALIZED);

	cis_SetTransparentColor(hColor);
	return cis_InternalScaleSurfaceToSurface(hDest, hSrc, pDestRect, pSrcRect, 0, hColor, LTNULL);
}


// FUNCTION: LITHTECH 0x0040e3a0
static LTRESULT cis_ScaleSurfaceToSurfaceSolidColor(HSURFACE hDest, HSURFACE hSrc,
	LTRect *pDestRect, LTRect *pSrcRect, HLTCOLOR hTransColor, HLTCOLOR hFillColor)
{
	if(!g_pCisRenderStruct)
		RETURN_ERROR(1, ScaleSurfaceToSurfaceSolidColor, LT_NOTINITIALIZED);

	return cis_InternalScaleSurfaceToSurface(hDest, hSrc, pDestRect, pSrcRect, 2, hTransColor, hFillColor);
}


typedef LTRESULT (*DrawWarpFn)(CisSurface *pDest, CisSurface *pSrc,
	WarpCoords *pLeftCoords, WarpCoords *pRightCoords, uint32 minY, uint32 maxY);

// FUNCTION: LITHTECH 0x0040e480
static LTRESULT cis_InternalWarpSurfaceToSurface(HSURFACE hDest, HSURFACE hSrc,
	LTWarpPt *pCoords, int nCoords, DrawWarpFn fn)
{
	CisSurface *pSrc, *pDest;
	int i;
	LTBOOL bIsVisible;
	WarpCoords leftCoords[1600], rightCoords[1600];	// (Talon: 1600 lines; Jupiter 1024)
	uint32 minY, maxY;


	pSrc = (CisSurface*)hSrc;
	pDest = (CisSurface*)hDest;
	if(!pSrc || !pDest)
		RETURN_ERROR(1, InternalWarpSurfaceToSurface, LT_INVALIDPARAMS);

	// Make sure they specified the coordinates correctly.
	if(nCoords > MAX_WARP_POINTS)
		nCoords = MAX_WARP_POINTS;

	if(nCoords < 3)
		RETURN_ERROR_PARAM(1, InternalWarpSurfaceToSurface, LT_INVALIDPARAMS, "nCoords < 3");

	// Clamp the source coordinates.
	for(i=0; i < nCoords; i++)
	{
		pCoords[i].source_x = LTCLAMP(pCoords[i].source_x, 0.0f, (float)(pSrc->m_Width-1));
		pCoords[i].source_y = LTCLAMP(pCoords[i].source_y, 0.0f, (float)(pSrc->m_Height-1));
	}

	// Clip the dest coordinates..
	bIsVisible = cis_Clip2dPoly(pCoords, nCoords, 0.0f, 0.0f,
		(float)(pDest->m_Width-1), (float)(pDest->m_Height-1));
	if(!bIsVisible)
		return LT_OK;

	// Get the warp coordinates into the lookup tables.
	cis_GetWarpCoordinates(leftCoords, rightCoords, pCoords, nCoords, minY, maxY);

	// Draw it.
	return fn(pDest, pSrc, leftCoords, rightCoords, minY, maxY);
}


// FUNCTION: LITHTECH 0x0040e410
static LTRESULT cis_WarpSurfaceToSurface(HSURFACE hDest, HSURFACE hSrc,
	LTWarpPt *pCoords, int nCoords)
{
	if(!g_pCisRenderStruct)
		RETURN_ERROR(1, WarpSurfaceToSurface, LT_NOTINITIALIZED);

	return cis_InternalWarpSurfaceToSurface(hDest, hSrc, pCoords, nCoords, cis_DrawWarp);
}


// FUNCTION: LITHTECH 0x0040e690
static LTRESULT cis_WarpSurfaceToSurfaceTransparent(HSURFACE hDest, HSURFACE hSrc,
	LTWarpPt *pCoords, int nCoords, HLTCOLOR hColor)
{
	if(!g_pCisRenderStruct)
		RETURN_ERROR(1, WarpSurfaceToSurfaceTransparent, LT_NOTINITIALIZED);

	cis_SetTransparentColor(hColor);
	return cis_InternalWarpSurfaceToSurface(hDest, hSrc, pCoords, nCoords, cis_DrawWarpTransparent);
}


// FUNCTION: LITHTECH 0x0040e700
static LTRESULT cis_WarpSurfaceToSurfaceSolidColor(HSURFACE hDest, HSURFACE hSrc,
	LTWarpPt *pCoords, int nCoords, HLTCOLOR hTransColor, HLTCOLOR hFillColor)
{
	if(!g_pCisRenderStruct)
		RETURN_ERROR(1, WarpSurfaceToSurfaceSolidColor, LT_NOTINITIALIZED);

	cis_SetTransparentColor(hTransColor);
	cis_SetSolidColor(hFillColor);
	return cis_InternalWarpSurfaceToSurface(hDest, hSrc, pCoords, nCoords, cis_DrawWarpSolidColor);
}


// FUNCTION: LITHTECH 0x0040e820
static LTRESULT cis_InternalTransformSurfaceToSurface(HSURFACE hDest, HSURFACE hSrc,
	LTFloatPt *pRotOrigin, int destX, int destY, float angle, float scaleX, float scaleY,
	LTBOOL bTransparent, HLTCOLOR tColor)
{
	CisSurface *pSrc, *pDest;
	LTFloatPt rotOrigin;
	float ca, sa;
	LTWarpPt warpPoints[4];
	LTFloatPt points[4], translate, shiftBack;
	BlitRequest blitRequest;
	int i;

	pSrc = (CisSurface*)hSrc;
	pDest = (CisSurface*)hDest;
	if(!pSrc || !pDest)
		RETURN_ERROR(1, InternalTransformSurfaceToSurface, LT_INVALIDPARAMS);

	if(pRotOrigin)
	{
		rotOrigin = *pRotOrigin;
	}
	else
	{
		rotOrigin.x = (float)destX + (float)pSrc->m_Width * 0.5f;
		rotOrigin.y = (float)destY + (float)pSrc->m_Height * 0.5f;
	}


	// Setup the points at the rotation origin.
	translate.x = (float)destX - rotOrigin.x;
	translate.y = (float)destY - rotOrigin.y;
	points[0].x = translate.x;
	points[0].y = translate.y;
	points[1].x = translate.x + (float)pSrc->m_Width;
	points[1].y = translate.y;
	points[2].x = translate.x + (float)pSrc->m_Width;
	points[2].y = translate.y + (float)pSrc->m_Height;
	points[3].x = translate.x;
	points[3].y = translate.y + (float)pSrc->m_Height;

	// Scale, rotate, and shift them back to the destX, destY offsets.
	ca = (float)cos(angle);
	sa = (float)sin(angle);

	shiftBack.x = -translate.x + (float)destX;
	shiftBack.y = -translate.y + (float)destY;

	for(i=0; i < 4; i++)
	{
		points[i].x *= scaleX;
		points[i].y *= scaleY;

		warpPoints[i].dest_x = (ca * points[i].x) - (sa * points[i].y);
		warpPoints[i].dest_y = (sa * points[i].x) + (ca * points[i].y);
		warpPoints[i].dest_x += shiftBack.x;
		warpPoints[i].dest_y += shiftBack.y;
	}

	warpPoints[0].source_x = warpPoints[0].source_y = 0.0f;
	warpPoints[1].source_x = (float)(pSrc->m_Width-1);
	warpPoints[1].source_y = 0.0f;
	warpPoints[2].source_x = (float)(pSrc->m_Width-1);
	warpPoints[2].source_y = (float)(pSrc->m_Height-1);
	warpPoints[3].source_x = 0.0f;
	warpPoints[3].source_y = (float)(pSrc->m_Height - 1);

	// Try to translate the command to let the RenderStruct do it
	if(pDest == &g_ScreenSurface)
	{
		if(g_Render.WarpToScreen)
		{
			// Clamp the source coordinates.
			warpPoints[0].source_x = LTCLAMP(warpPoints[0].source_x, 0.0f, (float)(pSrc->m_Width - 1));
			warpPoints[0].source_y = LTCLAMP(warpPoints[0].source_y, 0.0f, (float)(pSrc->m_Height - 1));
			warpPoints[1].source_x = LTCLAMP(warpPoints[1].source_x, 0.0f, (float)(pSrc->m_Width - 1));
			warpPoints[1].source_y = LTCLAMP(warpPoints[1].source_y, 0.0f, (float)(pSrc->m_Height - 1));
			warpPoints[2].source_x = LTCLAMP(warpPoints[2].source_x, 0.0f, (float)(pSrc->m_Width - 1));
			warpPoints[2].source_y = LTCLAMP(warpPoints[2].source_y, 0.0f, (float)(pSrc->m_Height - 1));
			warpPoints[3].source_x = LTCLAMP(warpPoints[3].source_x, 0.0f, (float)(pSrc->m_Width - 1));
			warpPoints[3].source_y = LTCLAMP(warpPoints[3].source_y, 0.0f, (float)(pSrc->m_Height - 1));

			// Setup source and destination rectangles for reference in the warp render function
			LTRect rSrcRect;
			LTRect rDestRect;
			rSrcRect.Init(0, 0, pSrc->m_Width - 1, pSrc->m_Height - 1);
			rDestRect.Init(0, 0, pDest->m_Width - 1, pDest->m_Height - 1);

			cis_OptimizeDirty(pSrc);

			blitRequest.m_hBuffer = pSrc->m_hBuffer;
			blitRequest.m_TransparentColor = g_TransparentColor;
			blitRequest.m_pSrcRect = &rSrcRect;
			blitRequest.m_pDestRect = &rDestRect;
			blitRequest.m_BlitOptions = bTransparent ? BLIT_TRANSPARENT : 0;
			blitRequest.m_Alpha = pSrc->m_Alpha;

			blitRequest.m_pWarpPts = warpPoints;
			blitRequest.m_nWarpPts = 4;

			// Do the blit unless there's something wrong...
			if(g_Render.WarpToScreen(&blitRequest))
			{
				pDest->m_Flags |= SURFFLAG_OPTIMIZEDIRTY;
				return LT_OK;
			}
		}
	}

	// Run this case if any of the above fails
	if(bTransparent)
		return cis_WarpSurfaceToSurfaceTransparent(hDest, hSrc, warpPoints, 4, tColor);
	else
		return cis_WarpSurfaceToSurface(hDest, hSrc, warpPoints, 4);
}


// FUNCTION: LITHTECH 0x0040e7a0
static LTRESULT cis_TransformSurfaceToSurface(HSURFACE hDest, HSURFACE hSrc,
	LTFloatPt *pRotOrigin, int destX, int destY, float angle, float scaleX, float scaleY)
{
	if(!g_pCisRenderStruct)
		RETURN_ERROR(1, TransformSurfaceToSurface, LT_NOTINITIALIZED);

	return cis_InternalTransformSurfaceToSurface(hDest, hSrc, pRotOrigin, destX, destY,
		angle, scaleX, scaleY, LTFALSE, 0);
}


// FUNCTION: LITHTECH 0x0040ec90
static LTRESULT cis_TransformSurfaceToSurfaceTransparent(HSURFACE hDest, HSURFACE hSrc,
	LTFloatPt *pRotOrigin, int destX, int destY, float angle, float scaleX, float scaleY,
	HLTCOLOR hColor)
{
	if(!g_pCisRenderStruct)
		RETURN_ERROR(1, TransformSurfaceToSurfaceTransparent, LT_NOTINITIALIZED);

	cis_SetTransparentColor(hColor);
	return cis_InternalTransformSurfaceToSurface(hDest, hSrc, pRotOrigin, destX, destY,
		angle, scaleX, scaleY, LTTRUE, hColor);
}


// FUNCTION: LITHTECH 0x0040ed30
static LTRESULT cis_FillRect(HSURFACE hDest, LTRect *pRect, HLTCOLOR hColor)
{
	LTRect theRect, tempRect;
	LTBOOL bIsVisible;
	CisSurface *pDest;
	FMRectRequest request;
	LTRESULT dResult;


	if(!g_pCisRenderStruct)
		RETURN_ERROR(1, FillRect, LT_NOTINITIALIZED);

	pDest = (CisSurface*)hDest;
	if(!pDest)
		RETURN_ERROR(1, FillRect, LT_INVALIDPARAMS);

	if(pRect)
	{
		tempRect.left = tempRect.top = 0;
		tempRect.right = pDest->m_Width;
		tempRect.bottom = pDest->m_Height;
		bIsVisible = cis_RectIntersection(&theRect, &tempRect, pRect);
		if(!bIsVisible)
			return LT_OK;
	}
	else
	{
		theRect.left = theRect.top = 0;
		theRect.right = pDest->m_Width;
		theRect.bottom = pDest->m_Height;
	}

	request.m_pDest = (uint8*)cis_LockSurface(pDest, request.m_DestPitch, LTTRUE);
	if(request.m_pDest)
	{
		request.m_pDestFormat = &g_ScreenFormat;
		request.m_Rect = theRect;
		request.m_Color = hColor;

		dResult = g_pClientMgr->m_FormatMgr.FillRect(&request);
		cis_UnlockSurface(pDest);
		return dResult;
	}
	else
	{
		RETURN_ERROR(1, FillRect, LT_ERROR);
	}
}


// FUNCTION: LITHTECH 0x0040ef70
static LTRESULT cis_GetSurfaceAlpha(HSURFACE hSurface, float &alpha)
{
	FN_NAME(ILTClient::GetSurfaceAlpha);
	CisSurface *pSurface;

	CHECK_PARAMS2(hSurface);

	pSurface = (CisSurface*)hSurface;
	alpha = pSurface->m_Alpha;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0040efc0
static LTRESULT cis_SetSurfaceAlpha(HSURFACE hSurface, float alpha)
{
	FN_NAME(ILTClient::GetSurfaceAlpha);
	CisSurface *pSurface;

	CHECK_PARAMS2(hSurface);

	pSurface = (CisSurface*)hSurface;
	pSurface->m_Alpha = LTCLAMP(alpha, 0.0f, 1.0f);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0040f050
static LTRESULT cis_GetEngineHook(char *pName, void **pData)
{
	if(stricmp(pName, "hwnd") == 0)
	{
		*pData = g_ClientGlob.m_hMainWnd;
		return LT_OK;
	}
	else if(stricmp(pName, "cres_hinstance")==0)
	{
		return bm_GetInstanceHandle(g_ClientGlob.m_pClientMgr->m_hClientResourceModule, pData);
	}
	else if(stricmp(pName, "cresl_hinstance")==0)
	{
		return bm_GetInstanceHandle(g_ClientGlob.m_pClientMgr->m_hLocalizedClientResourceModule, pData);
	}

	return LT_ERROR;
}


// ----------------------------------------------------------------- //
// Surface backup (when the renderer goes away).
// ----------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0040f420
static LTBOOL cis_BackupSurface(CisSurface *pSurface)
{
	uint32 y;
	uint8 *pSrcPos, *pDestPos, *pSrcBuffer;

	// Setup the backup buffer.
	pSurface->m_pBackupBuffer = (uint8*)dalloc(g_nScreenPixelBytes * pSurface->m_Width * pSurface->m_Height);

	// Copy the pixels over.
	pSrcBuffer = (uint8*)g_pCisRenderStruct->LockSurface(pSurface->m_hBuffer);
	if(!pSrcBuffer)
		return LTFALSE;

	for(y=0; y < pSurface->m_Height; y++)
	{
		pSrcPos = &pSrcBuffer[y*pSurface->m_Pitch];
		pDestPos = &pSurface->m_pBackupBuffer[y * pSurface->m_Width * g_nScreenPixelBytes];
		memcpy(pDestPos, pSrcPos, pSurface->m_Width * g_nScreenPixelBytes);
	}

	g_pCisRenderStruct->UnlockSurface(pSurface->m_hBuffer);

	// Get rid of the buffer.
	cis_DeleteSurfaceBuffer(pSurface);

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0040f1f0
static LTBOOL cis_RestoreSurface(CisSurface *pSurface)
{
	uint32 surfWidth, surfHeight;
	FMConvertRequest request;
	LTRESULT dResult;


	// Recreate the surface.
	pSurface->m_hBuffer = g_pCisRenderStruct->CreateSurface(pSurface->m_Width, pSurface->m_Height);
	if(!pSurface->m_hBuffer)
		return LTFALSE;

	g_pCisRenderStruct->GetSurfaceInfo(pSurface->m_hBuffer, &surfWidth, &surfHeight, &pSurface->m_Pitch);

	// Restore the pixels.
	request.m_pDestFormat = &g_ScreenFormat;
	request.m_pDest = (uint8*)g_pCisRenderStruct->LockSurface(pSurface->m_hBuffer);
	if(!request.m_pDest)
		return LTFALSE;

	request.m_DestPitch = pSurface->m_Pitch;

	request.m_pSrcFormat = &g_PrevScreenFormat;
	request.m_pSrc = pSurface->m_pBackupBuffer;
	request.m_SrcPitch = pSurface->m_Width * g_nPrevScreenPixelBytes;

	request.m_Width = pSurface->m_Width;
	request.m_Height = pSurface->m_Height;
	request.m_Flags = 0;

	dResult = GetFormatMgr()->ConvertPixels(&request);
	g_pCisRenderStruct->UnlockSurface(pSurface->m_hBuffer);

	if(dResult != LT_OK)
	{
		return LTFALSE;
	}

	// Get rid of the backup buffer.
	cis_DeleteSurfaceBackupBuffer(pSurface);

	// Reoptimize it...
	if(pSurface->m_Flags & SURFFLAG_OPTIMIZED)
	{
		g_pCisRenderStruct->OptimizeSurface(pSurface->m_hBuffer, pSurface->m_OptimizedTransparentColor);
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0040f3d0
static LTBOOL cis_BackupSurfaces()
{
	GPOS pos;
	CisSurface *pSurface;

	for(pos=g_Surfaces; pos; )
	{
		pSurface = g_Surfaces.GetNext(pos);

		if(!cis_BackupSurface(pSurface))
			return LTFALSE;
	}

	return LTTRUE;
}


// Ghidra merged this into cis_RendererIsHere's extent (0x0040f160-0x0040f1f0), which tail-jumps here.
// FUNCTION: LITHTECH 0x0040f1a0
static LTBOOL cis_RestoreSurfaces()
{
	GPOS pos;
	CisSurface *pSurface;

	for(pos=g_Surfaces; pos; )
	{
		pSurface = g_Surfaces.GetNext(pos);

		if(!cis_RestoreSurface(pSurface))
			return LTFALSE;
	}

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0040f100
static void cis_DeleteSurfaces()
{
	GPOS pos;
	CisSurface *pSurface;

	for(pos=g_Surfaces; pos; )
	{
		pSurface = g_Surfaces.GetNext(pos);

		cis_DeleteSurfaceBackupBuffer(pSurface);
		cis_DeleteSurfaceBuffer(pSurface);

		sb_Free(&g_SurfaceBank, pSurface);
	}

	g_Surfaces.RemoveAll();
}


// ----------------------------------------------------------------- //
// Interface functions.
// ----------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0040cbd0
void cis_Init(ILTClient *pClientDE)
{
	uint32 i;

	// Init the interface pointers.
	pClientDE->CreateColor = cis_CreateColor;
	pClientDE->DeleteColor = cis_DeleteColor;
	pClientDE->SetupColor1 = cis_SetupColor1;
	pClientDE->SetupColor2 = cis_SetupColor2;

	pClientDE->GetBorderSize = cis_GetBorderSize;

	pClientDE->OptimizeSurface = cis_OptimizeSurface;
	pClientDE->UnoptimizeSurface = cis_UnoptimizeSurface;

	pClientDE->GetScreenSurface = cis_GetScreenSurface;

	pClientDE->CreateSurfaceFromBitmap = cis_CreateSurfaceFromBitmap;
	pClientDE->CreateSurface = cis_CreateSurface;
	pClientDE->DeleteSurface = cis_DeleteSurface;

	pClientDE->GetSurfaceUserData = cis_GetSurfaceUserData;
	pClientDE->SetSurfaceUserData = cis_SetSurfaceUserData;

	pClientDE->GetPixel = cis_GetPixel;
	pClientDE->SetPixel = cis_SetPixel;

	pClientDE->GetSurfaceDims = cis_GetSurfaceDims;
	pClientDE->DrawBitmapToSurface = cis_DrawBitmapToSurface;
	pClientDE->DrawSurfaceSolidColor = cis_DrawSurfaceSolidColor;
	pClientDE->DrawSurfaceMasked = cis_DrawSurfaceMasked;
	pClientDE->DrawSurfaceToSurface = cis_DrawSurfaceToSurface;
	pClientDE->DrawSurfaceToSurfaceTransparent = cis_DrawSurfaceToSurfaceTransparent;

	pClientDE->ScaleSurfaceToSurface = cis_ScaleSurfaceToSurface;
	pClientDE->ScaleSurfaceToSurfaceTransparent = cis_ScaleSurfaceToSurfaceTransparent;
	pClientDE->ScaleSurfaceToSurfaceSolidColor = cis_ScaleSurfaceToSurfaceSolidColor;

	pClientDE->WarpSurfaceToSurface = cis_WarpSurfaceToSurface;
	pClientDE->WarpSurfaceToSurfaceTransparent = cis_WarpSurfaceToSurfaceTransparent;
	pClientDE->WarpSurfaceToSurfaceSolidColor = cis_WarpSurfaceToSurfaceSolidColor;

	pClientDE->TransformSurfaceToSurface = cis_TransformSurfaceToSurface;
	pClientDE->TransformSurfaceToSurfaceTransparent = cis_TransformSurfaceToSurfaceTransparent;

	pClientDE->FillRect = cis_FillRect;
	pClientDE->GetEngineHook = cis_GetEngineHook;

	pClientDE->GetSurfaceAlpha = cis_GetSurfaceAlpha;
	pClientDE->SetSurfaceAlpha = cis_SetSurfaceAlpha;

	// Init the screen surface.
	g_ScreenSurface.m_Flags = SURFFLAG_SCREEN;

	// Init the allocators..
	sb_Init(&g_SurfaceBank, sizeof(CisSurface), 10);

	// Init the mask lookup.
	memset(g_MaskLookup, 0, sizeof(g_MaskLookup));
	for(i=0; i < 8; i++)
		g_MaskLookup[1 << i] = (1 << i) - 1;

	// Init the text manager..
	tmgr_Init(pClientDE);
}

// FUNCTION: LITHTECH 0x0040f0d0
void cis_Term()
{
	cis_DeleteSurfaces();

	// Shutdown the allocators.
	sb_Term(&g_SurfaceBank);

	tmgr_Term();

	g_pCisRenderStruct = LTNULL;
}


// The bytes match; SIZE only because Ghidra's extent includes cis_RestoreSurfaces (0x0040f1a0).
// FUNCTION: LITHTECH 0x0040f160
LTBOOL cis_RendererIsHere(RenderStruct *pStruct)
{
	g_pCisRenderStruct = pStruct;

	g_ScreenSurface.m_Width = pStruct->m_Width;
	g_ScreenSurface.m_Height = pStruct->m_Height;
	pStruct->GetScreenFormat(&g_ScreenFormat);
	g_nScreenPixelBytes = g_ScreenFormat.GetBytesPerPixel();

	return cis_RestoreSurfaces();
}


// FUNCTION: LITHTECH 0x0040f320
LTBOOL cis_RendererGoingAway()
{
	LTBOOL bRet;

	g_PrevScreenFormat = g_ScreenFormat;
	g_nPrevScreenPixelBytes = g_nScreenPixelBytes;

	bRet = cis_BackupSurfaces();
	g_pCisRenderStruct = LTNULL;

	return bRet;
}


// The template code this unit instantiates (COMDATs placed after the unit's .text):
// LoadedBitmap's inline destructor pulls in CMoArray<uint8>'s vtable, g_Surfaces CGLinkedList's.
// FUNCTION: LITHTECH 0x0040f4c0 ?GenGetNext@?$CMoArray@EVDefaultCache@@@@UBEEAAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0040f4e0 ?GenGetAt@?$CMoArray@EVDefaultCache@@@@UBEEAAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0040f4f0 ?GenAppend@?$CMoArray@EVDefaultCache@@@@UAEHAAE@Z
// FUNCTION: LITHTECH 0x0040f5e0 ?GenRemoveAt@?$CMoArray@EVDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0040f6c0 ?GenCopyList@?$CMoArray@EVDefaultCache@@@@UAEHABV?$GenList@E@@@Z
// FUNCTION: LITHTECH 0x0040f7f0 ?GenAppendList@?$CMoArray@EVDefaultCache@@@@UAEHABV?$GenList@E@@@Z
// FUNCTION: LITHTECH 0x0040f8d0 ?GenFindElement@?$CMoArray@EVDefaultCache@@@@UBEHABEAAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0040f900 ?GenRemoveAt@?$CGLinkedList@PAVCisSurface@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0040f940 ?GenFindElement@?$CGLinkedList@PAVCisSurface@@@@UBEHABQAVCisSurface@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0040f970 ?InternalNiceSetSize@?$CMoArray@EVDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
