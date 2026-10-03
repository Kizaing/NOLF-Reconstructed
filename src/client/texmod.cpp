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


// FUNCTION: LITHTECH 0x0049adc0
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
					hTexture->SetRefCount(hTexture->GetRefCount() + 1);
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


static LTBOOL texmod_IsValidTexture(SharedTexture *pTexture);


// STUB: LITHTECH 0x0049b0c0
// GetTextureHandle matches with SetRefCount(GetRefCount() + 1) (xor-on-memory with two reads); the same idiom here
// gives the right size, but the original reads m_RefCount once, merges (old & 0x7fff) - 1 in registers, stores the
// word and tests the merged value (15 aligned). Tried: the ST_REFS bitfield view (folds the mask into a lea, 11
// aligned but 16 bytes short), single-expression merges, a uint16 inline setter, a count local, `!(m_RefCount & mask)`.
LTRESULT CLTTexMod::ReleaseTextureHandle(const HTEXTURE hTexture)
{
	FN_NAME(LTTexMod::ReleaseTextureHandle);

	CHECK_PARAMS2(texmod_IsValidTexture(hTexture));

	hTexture->SetRefCount(hTexture->GetRefCount() - 1);
	if(hTexture->GetRefCount() == 0)
	{
		if(hTexture->m_pEngineData)
			dtx_Destroy((TextureData*)hTexture->m_pEngineData);
	}

	return LT_OK;
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
// The rectangle/lock type test is a CHECK_PARAMS2 (wave 7: the original prints "LT_INVALIDPARAMS" there, ERR printed
// "60"; same score). The format test is an early `!= BPP_32` error whose ERR(1, LT_NOTINITIALIZED) VC merges with the
// final one: that gives the original's layout and leaves the two LT_INVALIDPARAMS blocks merged only from the call on.
// Remaining diff (8 aligned): in the alpha-mask branch the original loads &pData, stores, then loads &lPitch after
// `pop edi`; we load both references first. Tried `!= LTNULL`, a TextureMipData local, swapping the two stores, a
// plain `if` instead of `else if`, `lockType == TLOCK_BUMPMAP &&`. Phase 2: a nested `if(!m_AlphaMask) ERR` (36), a
// TextureMipData pointer local, casts on the two stores, an empty `else if(lockType != TLOCK_BUMPMAP)`, the two stores
// swapped (496 bytes): still 8.
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

	CHECK_PARAMS2(pRect->right > pRect->left && pRect->bottom > pRect->top &&
		(lockType == TLOCK_TEXTURE || lockType == TLOCK_BUMPMAP));

	pTextureData->SetupPFormat(&format);
	if(format.m_eType != BPP_32)
	{
		ERR(1, LT_NOTINITIALIZED);
	}

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

	ERR(1, LT_NOTINITIALIZED);
}


// FUNCTION: LITHTECH 0x0049b400
LTRESULT CLTTexMod::UnlockTexture(const HTEXTURE hTexture)
{
	FN_NAME(LTTexMod::UnlockTexture);

	CHECK_PARAMS2(texmod_IsValidTexture(hTexture));

	r_BindTexture(hTexture, LTTRUE);
	return LT_OK;
}
