// Talon's ILTTexMod implementation (the error strings call it LTTexMod; clientde_impl.cpp
// embeds one in the client interface). Jupiter dropped it. The file name is a guess: the
// object sits between tcpdriver (udpdriver.cpp) and text_mgr.cpp.
// FLAGS: /O2 /GX-
#include <windows.h>		// before the StdLith headers (clientmgr.h brings in lthread.h)
#include "bdefs.h"
#include "de_world.h"
#include "clientmgr.h"
#include "ilttexmod.h"
#include "dtxmgr.h"
#include "render.h"
#include "sprite.h"


// Also declared in clientde_impl.cpp.
class CLTTexMod : public ILTTexMod
{
public:
	CLTTexMod(CClientMgr *pClientMgr);

	virtual LTRESULT GetTextureHandle(char *pFilename, HTEXTURE &hTexture, const uint32 flags);
	virtual LTRESULT ReleaseTextureHandle(const HTEXTURE hTexture);
	virtual LTRESULT GetTextureInfo(const HTEXTURE hTexture, TextureInfo &info);
	virtual LTRESULT WasTextureDrawnLastFrame(const HTEXTURE hTexture);
	virtual LTRESULT LockTexture(const HTEXTURE hTexture, const LTRect *pRect,
		const uint32 lockType, uint8* &pData, long &lPitch);
	virtual LTRESULT UnlockTexture(const HTEXTURE hTexture);

	CClientMgr	*m_pClientMgr;
};


// The 15-bit reference count in SharedTexture::m_RefCount.
struct STRefCount
{
	uint16	m_Count : 15;
	uint16	m_Flag : 1;
};
#define ST_REFS(pTexture)	(((STRefCount*)&(pTexture)->m_RefCount)->m_Count)


// FUNCTION: LITHTECH 0x0049ada0
CLTTexMod::CLTTexMod(CClientMgr *pClientMgr)
{
	m_pClientMgr = pClientMgr;
}


// STUB: LITHTECH 0x0049adc0
// The original masks the old reference count before adding (bitfield code differs). See ReleaseTextureHandle.
LTRESULT CLTTexMod::GetTextureHandle(char *pFilename, HTEXTURE &hTexture, const uint32 flags)
{
	FN_NAME(LTTexMod::GetTextureHandle);
	FileRef ref;
	FMConvertRequest request;
	TextureData *pTextureData, *pNewData;
	LTRESULT dResult;
	uint32 bBumpmap;

	hTexture = LTNULL;
	CHECK_PARAMS2(pFilename);

	ref.m_FileType = FILE_ANYFILE;
	ref.m_pFilename = pFilename;
	hTexture = cm_AddSharedTexture(m_pClientMgr, &ref);
	if(hTexture)
	{
		dResult = r_LoadSystemTexture(hTexture, &pTextureData, LTFALSE);
		if(dResult != LT_OK)
			return dResult;

		bBumpmap = flags & THANDLE_BUMPMAP;
		pNewData = dtx_Alloc(BPP_32, pTextureData->m_Header.m_BaseWidth, pTextureData->m_Header.m_BaseHeight,
			1, LTNULL, LTNULL, bBumpmap);
		if(pNewData)
		{
			// Convert the texture data.
			pTextureData->SetupPFormat(request.m_pSrcFormat);
			request.m_pSrc = pTextureData->m_Mips[0].m_Data;
			request.m_SrcPitch = pTextureData->m_Mips[0].m_Pitch;
			pNewData->SetupPFormat(request.m_pDestFormat);
			request.m_pDest = pNewData->m_Mips[0].m_Data;
			request.m_DestPitch = pNewData->m_Mips[0].m_Pitch;
			request.m_Width = pTextureData->m_Mips[0].m_Width;
			request.m_Height = pTextureData->m_Mips[0].m_Height;
			dResult = m_pClientMgr->m_FormatMgr.ConvertPixels(&request);
			if(dResult == LT_OK)
			{
				// Make the bumpmap.
				if(bBumpmap)
				{
					pTextureData->SetupPFormat(request.m_pSrcFormat);
					request.m_pSrc = pTextureData->m_Mips[0].m_Data;
					request.m_SrcPitch = pTextureData->m_Mips[0].m_Pitch;
					request.m_pDestFormat->Init(BPP_8, 0xFF, 0, 0, 0);
					request.m_pDest = pNewData->m_Mips[0].m_AlphaMask;
					request.m_DestPitch = pNewData->m_Mips[0].m_AlphaPitch;
					request.m_Width = pTextureData->m_Mips[0].m_Width;
					request.m_Height = pTextureData->m_Mips[0].m_Height;
					dResult = m_pClientMgr->m_FormatMgr.ConvertPixels(&request);
				}
				else
				{
					dResult = LT_OK;
				}

				r_UnloadSystemTexture(pTextureData);
				if(dResult == LT_OK)
				{
					pNewData->m_Flags |= DTX_NOSYSCACHE;
					hTexture->m_pEngineData = pNewData;
					ST_REFS(hTexture) = hTexture->GetRefCount() + 1;
					return LT_OK;
				}

				return dResult;
			}

			r_UnloadSystemTexture(pTextureData);
			return dResult;
		}

		r_UnloadSystemTexture(pTextureData);
		ERR(2, LT_OUTOFMEMORY);
	}

	ERR(2, LT_NOTFOUND);
}


// Is this a texture handle from GetTextureHandle?
// FUNCTION: LITHTECH 0x0049b150
static LTBOOL texmod_IsValidTexture(SharedTexture *pTexture)
{
	TextureData *pTextureData;

	if(pTexture && ST_REFS(pTexture) > 0)
	{
		pTextureData = (TextureData*)pTexture->m_pEngineData;
		if(pTextureData && pTextureData->m_Header.m_nMipmaps == 1 && (pTextureData->m_Flags & DTX_NOSYSCACHE))
			return LTTRUE;
	}

	return LTFALSE;
}


// STUB: LITHTECH 0x0049b0c0
// As GetTextureHandle: the reference count decrement. The original computes (old & 0x7fff) - 1 unsimplified, merges it
// into the old word (xor/and/xor) and tests the merged value; the ST_REFS bitfield view folds the mask into a lea, and
// SetRefCount/GetRefCount give an xor-on-memory read-modify-write. Locals, casts and `& 0x7fff` forms all compile the same.
LTRESULT CLTTexMod::ReleaseTextureHandle(const HTEXTURE hTexture)
{
	FN_NAME(LTTexMod::ReleaseTextureHandle);

	CHECK_PARAMS2(texmod_IsValidTexture(hTexture));

	ST_REFS(hTexture) = hTexture->GetRefCount() - 1;
	if(ST_REFS(hTexture) == 0)
	{
		if(hTexture->m_pEngineData)
			dtx_Destroy((TextureData*)hTexture->m_pEngineData);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0049b180
LTRESULT CLTTexMod::WasTextureDrawnLastFrame(const HTEXTURE hTexture)
{
	FN_NAME(LTTexMod::WasTextureDrawnLastFrame);

	CHECK_PARAMS2(hTexture);

	return (hTexture->m_Unknown30 == m_pClientMgr->m_CurTextureFrameCode) ? LT_YES : LT_NO;
}


// FUNCTION: LITHTECH 0x0049b1e0
LTRESULT CLTTexMod::GetTextureInfo(const HTEXTURE hTexture, TextureInfo &info)
{
	FN_NAME(LTTexMod::GetTextureInfo);
	TextureData *pTextureData;

	info.m_Width = 0;
	info.m_Height = 0;

	CHECK_PARAMS2(texmod_IsValidTexture(hTexture));

	pTextureData = (TextureData*)hTexture->m_pEngineData;
	info.m_Width = pTextureData->m_Mips[0].m_Width;
	info.m_Height = pTextureData->m_Mips[0].m_Height;
	return LT_OK;
}


// STUB: LITHTECH 0x0049b260
// Only the error blocks differ (same tail-merge family as dsi_LoadServerObjects): the original keeps `mov eax,[fn]; mov
// ecx,[err]` per block before the const pushes and merges only the call; we merge from the pushes on.
LTRESULT CLTTexMod::LockTexture(const HTEXTURE hTexture, const LTRect *pRect,
	const uint32 lockType, uint8* &pData, long &lPitch)
{
	FN_NAME(LTTexMod::LockTexture);
	PFormat format;
	LTRect rect;
	TextureData *pTextureData;

	CHECK_PARAMS2(texmod_IsValidTexture(hTexture));

	pTextureData = (TextureData*)hTexture->m_pEngineData;
	if(!pRect)
	{
		rect.left = rect.top = 0;
		rect.right = pTextureData->m_Mips[0].m_Width;
		rect.bottom = pTextureData->m_Mips[0].m_Height;
		pRect = &rect;
	}

	if(pRect->right > pRect->left && pRect->bottom > pRect->top &&
		(lockType == TLOCK_TEXTURE || lockType == TLOCK_BUMPMAP))
	{
		pTextureData->SetupPFormat(&format);
		if(format.m_eType == BPP_32)
		{
			if(lockType == TLOCK_TEXTURE)
			{
				pData = pTextureData->m_Mips[0].m_Data;
				lPitch = pTextureData->m_Mips[0].m_Pitch;
				return LT_OK;
			}
			else if(pTextureData->m_Mips[0].m_AlphaMask)
			{
				pData = pTextureData->m_Mips[0].m_AlphaMask;
				lPitch = pTextureData->m_Mips[0].m_AlphaPitch;
				return LT_OK;
			}
		}

		ERR(1, LT_NOTINITIALIZED);
	}

	ERR(2, LT_INVALIDPARAMS);
}


// FUNCTION: LITHTECH 0x0049b400
LTRESULT CLTTexMod::UnlockTexture(const HTEXTURE hTexture)
{
	FN_NAME(LTTexMod::UnlockTexture);

	CHECK_PARAMS2(texmod_IsValidTexture(hTexture));

	r_BindTexture(hTexture, LTTRUE);
	return LT_OK;
}
