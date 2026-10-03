// Talon kernel/src/sys/win/smackvideomgrimpl.cpp (Jupiter replaced it with dshowvideomgrimpl).
// Smacker videos decode into a 565 DirectDraw surface; when the screen isn't 565 the frame is
// converted into a second surface in the screen's format. Release, OnSurfaceDestroyed and
// OnTextureDestroyed are shared (identical-code folded) with BinkVideoInst.
#include <windows.h>
#include "../../jupiter/dx9inc/ddraw.h"
#include "bdefs.h"
#include "smackvideomgrimpl.h"
#include "render.h"
#include "dtxmgr.h"
#include "dsys_interface.h"
#include "soundmgr.h"


// winclientde_impl.
FormatMgr* GetFormatMgr();	// 0x0040c510

// cutil: fills a PFormat from a DirectDraw pixel format.
void DDPFToPFormat(DDPIXELFORMAT *pDDPF, PFormat *pFormat);	// 0x00426ac0


// Smacker open and buffer flags.
#define SMACKTRACKS			0x000FE000
#define SMACKYINTERLACE		0x00100000
#define SMACKAUTOEXTRA		0xFFFFFFFF
#define SMACKBUFFER565		0xC0000000


// -------------------------------------------------------------------------------- //
// SmackVideoMgr
// -------------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0049d310
SmackVideoMgr::SmackVideoMgr(CClientMgr *pClientMgr)
	: VideoMgr(pClientMgr)
{
	m_SmackSoundUseMSS	= LTNULL;
	m_SmackOpen			= LTNULL;
	m_SmackClose		= LTNULL;
	m_SmackWait			= LTNULL;
	m_SmackToBuffer		= LTNULL;
	m_SmackDoFrame		= LTNULL;
	m_SmackNextFrame	= LTNULL;
	m_hSmackDLL			= LTNULL;
}

// FUNCTION: LITHTECH 0x0049d350
SmackVideoMgr::~SmackVideoMgr()
{
	if(m_hSmackDLL)
	{
		FreeLibrary(m_hSmackDLL);
		m_hSmackDLL = LTNULL;
	}
}


// FUNCTION: LITHTECH 0x0049d380
LTRESULT SmackVideoMgr::Init()
{
	m_hSmackDLL = LoadLibrary("smackw32.dll");

	if(!m_hSmackDLL)
	{
		dsi_ConsolePrint("Unable to find smackw32.dll, video playback disabled.");
		RETURN_ERROR(1, SmackVideoMgr::Init, LT_MISSINGFILE);
	}

	m_SmackSoundUseMSS	= (SmackSoundUseMSSFn)GetProcAddress(m_hSmackDLL,	"_SmackSoundUseMSS@4");
	m_SmackOpen			= (SmackOpenFn)GetProcAddress(m_hSmackDLL,			"_SmackOpen@12");
	m_SmackClose		= (SmackCloseFn)GetProcAddress(m_hSmackDLL,			"_SmackClose@4");
	m_SmackWait			= (SmackWaitFn)GetProcAddress(m_hSmackDLL,			"_SmackWait@4");
	m_SmackToBuffer		= (SmackToBufferFn)GetProcAddress(m_hSmackDLL,		"_SmackToBuffer@28");
	m_SmackDoFrame		= (SmackDoFrameFn)GetProcAddress(m_hSmackDLL,		"_SmackDoFrame@4");
	m_SmackNextFrame	= (SmackNextFrameFn)GetProcAddress(m_hSmackDLL,		"_SmackNextFrame@4");

	if( !m_SmackSoundUseMSS || !m_SmackOpen || !m_SmackClose || !m_SmackWait ||
		!m_SmackToBuffer || !m_SmackDoFrame || !m_SmackNextFrame )
	{
		dsi_ConsolePrint("Invalid smackw32.dll, video playback disabled.");
		FreeLibrary(m_hSmackDLL);
		m_hSmackDLL = LTNULL;
		RETURN_ERROR(1, SmackVideoMgr::Init, LT_NOTINITIALIZED);
	}

	// Play the sound through Miles if the engine uses it.
	m_SmackSoundUseMSS(GetClientILTSoundMgrImpl()->m_bValid ? GetClientILTSoundMgrImpl()->m_hDigDriver : 0);

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0049d4f0
LTRESULT SmackVideoMgr::CreateVideo(const char *pFilename, uint32 flags, VideoInst* &pVideo, LTBOOL bTexture)
{
	SmackVideoInst *pInst;
	LTRESULT dResult;

	pVideo = LTNULL;

	pInst = new SmackVideoInst(this);
	if(!pInst)
	{
		RETURN_ERROR(1, SmackVideoMgr, LT_OUTOFMEMORY);
	}

	dResult = pInst->Init(pFilename, flags, bTexture);
	if(dResult != LT_OK)
	{
		pInst->Release();
		return dResult;
	}

	if(!bTexture)
		dResult = pInst->InitScreen();
	else
		dResult = pInst->InitTexture();

	if(dResult != LT_OK)
	{
		pInst->Release();
		return dResult;
	}

	m_Videos.AddTail(pInst, &pInst->m_Link);
	pVideo = pInst;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0049d600
LTRESULT SmackVideoMgr::CreateScreenVideo(const char *pFilename, uint32 flags, VideoInst* &pVideo)
{
	return CreateVideo(pFilename, flags, pVideo, LTFALSE);
}

// FUNCTION: LITHTECH 0x0049d620
LTRESULT SmackVideoMgr::CreateTextureVideo(const char *pFilename, uint32 flags, VideoInst* &pVideo)
{
	return CreateVideo(pFilename, flags, pVideo, LTTRUE);
}


// -------------------------------------------------------------------------------- //
// SmackVideoInst
// -------------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x0049d640
SmackVideoInst::SmackVideoInst(SmackVideoMgr *pMgr)
{
	m_smk = LTNULL;
	m_pMgr = pMgr;
	m_pSurface = LTNULL;
	m_bConvert = LTFALSE;
	m_pTextureData = LTNULL;
	m_pConvertSurface = LTNULL;
}

// FUNCTION: LITHTECH 0x0049d6b0
SmackVideoInst::~SmackVideoInst()
{
	Term();
}


// FUNCTION: LITHTECH 0x0049d6e0
void SmackVideoInst::Term()
{
	MPOS pos;
	VideoSurfaceLink *pLink;

	OnRenderTerm();

	if(m_smk)
	{
		m_pMgr->m_SmackClose(m_smk);
		m_smk = LTNULL;
	}

	if(m_pTextureData)
	{
		dtx_Destroy(m_pTextureData);
		m_pTextureData = LTNULL;
	}

	// Give the surfaces their textures back.
	for(pos=m_SurfaceLinks; pos; )
	{
		pLink = m_SurfaceLinks.GetNext(pos);
		if(pLink->m_pSurface)
			pLink->m_pSurface->m_pTexture = pLink->m_pTexture;
	}

	for(pos=m_SurfaceLinks; pos; )
	{
		pLink = m_SurfaceLinks.GetNext(pos);
		delete pLink;
	}

	m_SurfaceLinks.RemoveAll();
}


// FUNCTION: LITHTECH 0x0049d780
LTRESULT SmackVideoInst::Init(const char *pFilename, uint32 flags, LTBOOL bTexture)
{
	uint32 openFlags;

	// Texture videos don't play sound.
	openFlags = bTexture ? 0 : SMACKTRACKS;

	if(flags & PLAYBACK_YINTERLACE)
		openFlags |= SMACKYINTERLACE;

	m_smk = m_pMgr->m_SmackOpen(pFilename, openFlags, SMACKAUTOEXTRA);
	if(!m_smk)
	{
		RETURN_ERROR_PARAM(1, SmackVideoInst::Init, LT_MISSINGFILE, "SmackOpen failed");
	}

	m_bTexture = bTexture;
	m_Flags = flags;
	return LT_OK;
}


// STUB: LITHTECH 0x0049d810
// Remaining diff (36 aligned): the error tails are cross-jumped differently. The original's GetDisplayMode block jumps
// into the first CreateSurface block's print code and every `jl` goes to that block's epilogue; ours merges the first
// CreateSurface block into the GetDisplayMode one and sends the `jl`s to the last block's epilogue. Wave 6 tried (no
// change or worse): the NOT_INITIALIZED error as an else branch, an early `if(!m_smk || ...)` return, the
// GetDisplayMode test nested as `== DD_OK`, the 565 test inverted, a goto past the convert path, a do{}while(0)
// RETURN_ERROR_PARAM, and /O1 /Ox /Os /Oy- /G6 /Gr on a copy (README, wave 6).
LTRESULT SmackVideoInst::InitScreen()
{
	LPDIRECTDRAW7 pDD;
	DDSURFACEDESC2 ddsd;
	DDBLTFX fx;

	OnRenderTerm();

	if(m_smk && g_Render.m_bInitted && (pDD = (LPDIRECTDRAW7)g_Render.GetHook("LPDIRECTDRAW")) != LTNULL)
	{
		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);
		if(pDD->GetDisplayMode(&ddsd) != DD_OK)
		{
			RETURN_ERROR_PARAM(1, SmackVideoInst::InitScreen, LT_ERROR, "IDirectDraw::GetDisplayMode failed");
		}

		ddsd.dwWidth = m_smk->Width;
		ddsd.dwHeight = m_smk->Height;
		ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
		ddsd.ddsCaps.dwCaps = DDSCAPS_SYSTEMMEMORY;

		DDPFToPFormat(&ddsd.ddpfPixelFormat, &m_ScreenFormat);

		if(m_ScreenFormat.GetType() == BPP_16 && m_ScreenFormat.m_Masks[CP_RED] == 0xF800 &&
			m_ScreenFormat.m_Masks[CP_GREEN] == 0x7E0 && m_ScreenFormat.m_Masks[CP_BLUE] == 0x1F)
		{
			// The screen is 565 so Smacker can decode straight into the surface.
			if(pDD->CreateSurface(&ddsd, &m_pSurface, LTNULL) != DD_OK)
			{
				m_pSurface = LTNULL;
				RETURN_ERROR_PARAM(1, SmackVideoInst::InitScreen, LT_ERROR, "IDirectDraw::CreateSurface failed");
			}
		}
		else
		{
			// Decode into a 565 surface and convert into one in the screen's format.
			m_bConvert = LTTRUE;
			if(pDD->CreateSurface(&ddsd, &m_pConvertSurface, LTNULL) != DD_OK)
			{
				m_pSurface = LTNULL;
				RETURN_ERROR_PARAM(1, SmackVideoInst::InitScreen, LT_ERROR, "IDirectDraw::CreateSurface failed");
			}

			ddsd.ddpfPixelFormat.dwRGBBitCount = 16;
			ddsd.ddpfPixelFormat.dwRBitMask = 0xF800;
			ddsd.ddpfPixelFormat.dwGBitMask = 0x7E0;
			ddsd.ddpfPixelFormat.dwBBitMask = 0x1F;
			if(pDD->CreateSurface(&ddsd, &m_pSurface, LTNULL) != DD_OK)
			{
				m_pSurface = LTNULL;
				RETURN_ERROR_PARAM(1, SmackVideoInst::InitScreen, LT_ERROR, "IDirectDraw::CreateSurface failed");
			}
		}

		// Clear it.
		memset(&fx, 0, sizeof(fx));
		fx.dwSize = sizeof(fx);
		m_pSurface->Blt(LTNULL, LTNULL, LTNULL, DDBLT_COLORFILL | DDBLT_DDFX, &fx);
		return LT_OK;
	}

	RETURN_ERROR(1, SmackVideoInst::InitScreen, LT_NOTINITIALIZED);
}


// FUNCTION: LITHTECH 0x0049da70
LTRESULT SmackVideoInst::InitTexture()
{
	RPaletteColor palette[256];
	uint32 i;

	OnRenderTerm();

	if(!m_smk)
	{
		RETURN_ERROR(1, SmackVideoInst::InitTexture, LT_NOTINITIALIZED);
	}

	m_pTextureData = dtx_Alloc(BPP_32, m_smk->Width, m_smk->Height, 1, LTNULL, LTNULL, 0);
	if(!m_pTextureData)
	{
		RETURN_ERROR(1, SmackVideoInst::InitTexture, LT_OUTOFMEMORY);
	}

	for(i=0; i < 256; i++)
	{
		palette[i].rgb.r = m_smk->Palette[i*3+0];
		palette[i].rgb.g = m_smk->Palette[i*3+1];
		palette[i].rgb.b = m_smk->Palette[i*3+2];
	}

	m_pTextureData->m_Flags2 |= 2;
	m_pTextureData->m_Flags |= 0x40;
	m_Texture.m_pEngineData = m_pTextureData;
	r_BindTexture(&m_Texture, LTTRUE);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0049db90
void SmackVideoInst::OnRenderInit()
{
	LTRESULT dResult;

	if(!m_bTexture)
		dResult = InitScreen();
	else
		dResult = InitTexture();

	if(dResult != LT_OK)
		Term();
}

// FUNCTION: LITHTECH 0x0049dbc0
void SmackVideoInst::OnRenderTerm()
{
	if(m_pSurface)
	{
		m_pSurface->Release();
		m_pSurface = LTNULL;
	}

	if(m_pConvertSurface)
	{
		m_pConvertSurface->Release();
		m_pSurface = LTNULL;
	}

	if(m_pTextureData)
	{
		r_UnbindTexture(&m_Texture);
		m_Texture.m_pEngineData = LTNULL;
		dtx_Destroy(m_pTextureData);
		m_pTextureData = LTNULL;
	}
}


// FUNCTION: LITHTECH 0x0049dc10
void SmackVideoInst::OnSurfaceDestroyed(Surface *pSurface)
{
	MPOS pos;
	VideoSurfaceLink *pLink;

	for(pos=m_SurfaceLinks; pos; )
	{
		pLink = m_SurfaceLinks.GetNext(pos);
		if(pLink->m_pSurface == pSurface)
		{
			m_SurfaceLinks.RemoveAt(&pLink->m_Link);
			delete pLink;
		}
	}
}

// FUNCTION: LITHTECH 0x0049dc90
void SmackVideoInst::OnTextureDestroyed(SharedTexture *pTexture)
{
	MPOS pos;
	VideoSurfaceLink *pLink;

	for(pos=m_SurfaceLinks; pos; )
	{
		pLink = m_SurfaceLinks.GetNext(pos);
		if(pLink->m_pTexture == pTexture)
			pLink->SetTexture(LTNULL);
	}
}

// FUNCTION: LITHTECH 0x0049dcd0
LTRESULT SmackVideoInst::Update()
{
	if(m_smk && m_bTexture == LTTRUE)
		return UpdateTextureVideo();

	return LT_FINISHED;
}

// FUNCTION: LITHTECH 0x0049dcf0
LTRESULT SmackVideoInst::DrawVideo()
{
	if(m_smk && m_pSurface)
		return UpdateOnScreen();

	return LT_FINISHED;
}

// FUNCTION: LITHTECH 0x0049dd10
LTRESULT SmackVideoInst::GetVideoStatus()
{
	if(m_smk)
		return IsAtLastFrame() ? LT_FINISHED : LT_OK;

	return LT_FINISHED;
}

// FUNCTION: LITHTECH 0x0049dd30
LTRESULT SmackVideoInst::BindToSurface(Surface *pSurface)
{
	VideoSurfaceLink *pLink;

	pLink = new VideoSurfaceLink(this);
	if(pLink)
	{
		pLink->SetSurface(pSurface);
		pLink->SetTexture((SharedTexture*)pSurface->m_pTexture);
		pSurface->m_pTexture = &m_Texture;

		m_SurfaceLinks.AddTail(pLink, &pLink->m_Link);
		return LT_OK;
	}

	RETURN_ERROR(1, SmackVideoInst::BindToSurface, LT_OUTOFMEMORY);
}

// FUNCTION: LITHTECH 0x0049ddf0
void SmackVideoInst::Release()
{
	if(m_pMgr->m_Videos.FindElement(this) != BAD_INDEX)
		m_pMgr->m_Videos.RemoveAt(&m_Link);

	delete this;
}

// FUNCTION: LITHTECH 0x0049de60
LTBOOL SmackVideoInst::IsAtLastFrame()
{
	if(m_smk)
		return m_smk->FrameNum == (m_smk->Frames - 1);

	return LTTRUE;
}


// STUB: LITHTECH 0x0049de90
// Remaining diff (116 bytes): the original's srcDesc and destDesc stack slots are swapped relative to ours (the first
// Lock reuses ddsd's slot at +0x40); no permutation of the four block locals (all 23 tried) changes it, and making
// ddsd a function-scope variable that also serves as srcDesc is much worse (704 bytes).
LTRESULT SmackVideoInst::UpdateOnScreen()
{
	LPDIRECTDRAWSURFACE7 pBackBuffer, pDisplaySurface;
	RECT srcRect, destRect;
	uint32 waitResult;

	pBackBuffer = (LPDIRECTDRAWSURFACE7)g_Render.GetHook("BACKBUFFER");
	if(!pBackBuffer)
	{
		RETURN_ERROR_PARAM(2, SmackVideoInst::UpdateOnScreen, LT_NOTINITIALIZED, "GetHook(BACKBUFFER) failed");
	}

	if(IsAtLastFrame())
		return LT_OK;

	waitResult = m_pMgr->m_SmackWait(m_smk);
	if(!waitResult)
	{
		DDSURFACEDESC2 ddsd;

		ddsd.dwSize = sizeof(ddsd);
		if(m_pSurface->Lock(LTNULL, &ddsd, DDLOCK_WRITEONLY, LTNULL) == DD_OK)
		{
			m_pMgr->m_SmackToBuffer(m_smk, 0, 0, ddsd.lPitch, m_smk->Height, ddsd.lpSurface, SMACKBUFFER565);
			m_pMgr->m_SmackDoFrame(m_smk);
			m_pSurface->Unlock(LTNULL);
		}
	}

	if(m_bConvert)
	{
		DDSURFACEDESC2 destDesc;
		PFormat destFormat;
		FMConvertRequest request;
		DDSURFACEDESC2 srcDesc;

		if(!m_pConvertSurface)
		{
			RETURN_ERROR_PARAM(2, SmackVideoInst::UpdateOnScreen, LT_NOTINITIALIZED, "No conversion surface for Smacker available!");
		}

		srcDesc.dwSize = sizeof(srcDesc);
		if(m_pSurface->Lock(LTNULL, &srcDesc, DDLOCK_WRITEONLY, LTNULL) != DD_OK)
		{
			RETURN_ERROR_PARAM(2, SmackVideoInst::UpdateOnScreen, LT_NOTINITIALIZED, "Unable to lock source surface.");
		}

		destDesc.dwSize = sizeof(destDesc);
		if(m_pConvertSurface->Lock(LTNULL, &destDesc, DDLOCK_WRITEONLY, LTNULL) != DD_OK)
		{
			RETURN_ERROR_PARAM(2, SmackVideoInst::UpdateOnScreen, LT_NOTINITIALIZED, "Unable to lock conversion surface.");
		}

		DDPFToPFormat(&destDesc.ddpfPixelFormat, &destFormat);

		request.m_pSrc = (uint8*)srcDesc.lpSurface;
		request.m_SrcPitch = srcDesc.lPitch;
		request.m_pSrcFormat = &m_ScreenFormat;
		request.m_pDest = (uint8*)destDesc.lpSurface;
		request.m_DestPitch = destDesc.lPitch;
		request.m_pDestFormat = &destFormat;
		request.m_Width = m_smk->Width;
		request.m_Height = m_smk->Height;

		if(GetFormatMgr()->ConvertPixels(&request) != LT_OK)
			pDisplaySurface = LTNULL;
		else
			pDisplaySurface = m_pConvertSurface;
	}
	else
	{
		pDisplaySurface = m_pSurface;
	}

	if(!pDisplaySurface)
	{
		RETURN_ERROR_PARAM(2, SmackVideoImpl::UpdateOnScreen, LT_NOTINITIALIZED, "Smacker display buffer is null!");
	}

	if(m_Flags & PLAYBACK_FULLSCREEN)
	{
		srcRect.left = 0;
		srcRect.top = 0;
		srcRect.right = m_smk->Width;
		srcRect.bottom = m_smk->Height;

		destRect.top = 0;
		destRect.left = 0;
		destRect.right = g_Render.m_Width;
		destRect.bottom = g_Render.m_Height;

		pBackBuffer->Blt(&destRect, pDisplaySurface, &srcRect, 0, LTNULL);
	}
	else
	{
		// Center it, clipping to the screen.
		srcRect.left = 0;
		srcRect.top = 0;
		srcRect.right = m_smk->Width;
		srcRect.bottom = m_smk->Height;

		destRect.left = (g_Render.m_Width - m_smk->Width) / 2;
		destRect.top = (g_Render.m_Height - m_smk->Height) / 2;

		if(destRect.left < 0)
		{
			srcRect.left = -destRect.left;
			srcRect.right -= destRect.left;
			destRect.left = 0;
		}

		if(destRect.top < 0)
		{
			srcRect.top = -destRect.top;
			srcRect.bottom -= destRect.top;
			destRect.top = 0;
		}

		destRect.right = destRect.left - srcRect.left + srcRect.right;
		if(destRect.right > (long)g_Render.m_Width)
		{
			srcRect.right += g_Render.m_Width - destRect.right;
			destRect.right = g_Render.m_Width;
		}

		destRect.bottom = destRect.top - srcRect.top + srcRect.bottom;
		if(destRect.bottom > (long)g_Render.m_Height)
		{
			srcRect.bottom += g_Render.m_Height - destRect.bottom;
			destRect.bottom = g_Render.m_Height;
		}

		pBackBuffer->BltFast(destRect.left, destRect.top, pDisplaySurface, &srcRect, 0);
	}

	if(!waitResult && !IsAtLastFrame())
		m_pMgr->m_SmackNextFrame(m_smk);

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0049e270
LTRESULT SmackVideoInst::UpdateTextureVideo()
{
	if(!m_smk || !m_pTextureData)
	{
		RETURN_ERROR(1, SmackVideoInst::UpdateTextureVideo, LT_FINISHED);
	}

	if(IsAtLastFrame())
		return LT_OK;

	if(!m_pMgr->m_SmackWait(m_smk))
	{
		m_pMgr->m_SmackToBuffer(m_smk, 0, 0, m_pTextureData->m_Mips[0].m_Pitch, m_smk->Height,
			m_pTextureData->m_Mips[0].m_Data, 0);
		m_pMgr->m_SmackDoFrame(m_smk);
		r_BindTexture(&m_Texture, LTTRUE);

		if(!IsAtLastFrame())
			m_pMgr->m_SmackNextFrame(m_smk);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0049d690 ??_GSmackVideoInst@@UAEPAXI@Z
