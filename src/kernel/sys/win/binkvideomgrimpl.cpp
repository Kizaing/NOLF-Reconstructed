// Jupiter runtime/kernel/src/sys/win/binkvideomgrimpl.cpp
// Talon (DirectDraw) version: Binkw32.dll is loaded at runtime, on-screen videos decode into an
// offscreen DirectDraw surface and texture videos into a TextureData. Release,
// OnSurfaceDestroyed and OnTextureDestroyed were identical-code folded with SmackVideoInst's.
// The unit starts at 00401890 (BinkVideoMgr's constructor).
#include <windows.h>
#include "../../jupiter/dx9inc/ddraw.h"
#include "bdefs.h"
#include "binkvideomgrimpl.h"
#include "render.h"
#include "dtxmgr.h"
#include "dsys_interface.h"
#include "soundmgr.h"



// Bink surface types and open flags.
#define BINKSURFACE32		3
#define BINKSURFACE32R		4
#define BINKSURFACE555		9
#define BINKSURFACE565		10

#define BINKSNDTRACK		0x00004000
#define BINKFRAMERATE		0x00080000
#define BINKNOSKIP			0x00080000
#define BINKIOSIZE			0x01000000
#define BINKFILEHANDLE		0x00800000
#define BINKNOTHREADEDIO	0x04000000
#define BINKFROMMEMORY		0x04000000
#define BINKALPHA			0x20000000


// -------------------------------------------------------------------------------- //
// BinkVideoMgr
// -------------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00401890
BinkVideoMgr::BinkVideoMgr(CClientMgr *pClientMgr)
	: VideoMgr(pClientMgr)
{
	m_BinkSoundFn		= LTNULL;
	m_BinkMilesFn		= LTNULL;
	m_BinkDSndFn		= LTNULL;
	m_BinkSetSoundTrack	= LTNULL;
	m_BinkOpen			= LTNULL;
	m_BinkClose			= LTNULL;
	m_BinkWait			= LTNULL;
	m_BinkToBuffer		= LTNULL;
	m_BinkDoFrame		= LTNULL;
	m_BinkNextFrame		= LTNULL;
	m_hBinkDLL			= LTNULL;
}

// FUNCTION: LITHTECH 0x004018d0
BinkVideoMgr::~BinkVideoMgr()
{
	if(m_hBinkDLL)
	{
		FreeLibrary(m_hBinkDLL);
		m_hBinkDLL = LTNULL;
	}
}


// FUNCTION: LITHTECH 0x00401900
LTRESULT BinkVideoMgr::Init()
{
	CSoundMgr *pSoundMgr;

	// load bink dll //

	m_hBinkDLL = LoadLibrary("Binkw32.dll");

	if(!m_hBinkDLL)
	{
		dsi_ConsolePrint("Unable to find Binkw32.dll, video playback disabled.");
		RETURN_ERROR(1, BinkVideoMgr::Init, LT_MISSINGFILE);
	}

	// connect bink dll functions //

	m_BinkSoundFn         = (BinkSoundFn)GetProcAddress(m_hBinkDLL,         "_BinkSetSoundSystem@8");
	m_BinkMilesFn         = (BinkMilesFn)GetProcAddress(m_hBinkDLL,         "_BinkOpenMiles@4");
	m_BinkDSndFn          = (BinkDSndFn)GetProcAddress(m_hBinkDLL,          "_BinkOpenDirectSound@4");
	m_BinkSetSoundTrack   = (BinkSetSoundTrackFn)GetProcAddress(m_hBinkDLL, "_BinkSetSoundTrack@4");
	m_BinkOpen            = (BinkOpenFn)GetProcAddress(m_hBinkDLL,          "_BinkOpen@8");
	m_BinkClose           = (BinkCloseFn)GetProcAddress(m_hBinkDLL,         "_BinkClose@4");
	m_BinkWait            = (BinkWaitFn)GetProcAddress(m_hBinkDLL,          "_BinkWait@4");
	m_BinkToBuffer        = (BinkToBufferFn)GetProcAddress(m_hBinkDLL,      "_BinkCopyToBuffer@28");
	m_BinkDoFrame         = (BinkDoFrameFn)GetProcAddress(m_hBinkDLL,       "_BinkDoFrame@4");
	m_BinkNextFrame       = (BinkNextFrameFn)GetProcAddress(m_hBinkDLL,     "_BinkNextFrame@4");
	m_BinkIsSoftCursorFn  = (BinkIsSoftCursorFn)GetProcAddress(m_hBinkDLL,  "_BinkIsSoftwareCursor@8");
	m_BinkCheckCursorFn   = (BinkCheckCursorFn)GetProcAddress(m_hBinkDLL,   "_BinkCheckCursor@20");
	m_BinkRestoreCursorFn = (BinkRestoreCursorFn)GetProcAddress(m_hBinkDLL, "_BinkRestoreCursor@4");

	// make sure functions connected //

	if( !m_BinkSoundFn || !m_BinkOpen || !m_BinkClose || !m_BinkWait ||
		!m_BinkToBuffer || !m_BinkDoFrame || !m_BinkNextFrame || !m_BinkMilesFn ||
		!m_BinkDSndFn || !m_BinkSetSoundTrack || !m_BinkIsSoftCursorFn || !m_BinkCheckCursorFn )
	{
		dsi_ConsolePrint("Invalid Binkw32.dll, video playback disabled.");
		FreeLibrary(m_hBinkDLL);
		m_hBinkDLL = LTNULL;
		RETURN_ERROR(1, BinkVideoMgr::Init, LT_NOTINITIALIZED);
	}

	// Use the sound system the engine uses.
	if(GetClientILTSoundMgrImpl()->m_bValid)
	{
		pSoundMgr = GetClientILTSoundMgrImpl();
		m_BinkSoundFn((void*)m_BinkMilesFn, (uint32)pSoundMgr->m_hDigDriver);
	}
	else
	{
		m_BinkSoundFn((void*)m_BinkDSndFn, 0);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00401b00
LTRESULT BinkVideoMgr::CreateVideo(const char *pFilename, uint32 flags, VideoInst* &pVideo, LTBOOL bTexture)
{
	BinkVideoInst *pInst;
	LTRESULT dResult;

	pVideo = LTNULL;

	pInst = new BinkVideoInst(this);
	if(!pInst)
	{
		RETURN_ERROR(1, BinkVideoMgr, LT_OUTOFMEMORY);
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

// FUNCTION: LITHTECH 0x00401c00
LTRESULT BinkVideoMgr::CreateScreenVideo(const char *pFilename, uint32 flags, VideoInst* &pVideo)
{
	return CreateVideo(pFilename, flags, pVideo, LTFALSE);
}

// FUNCTION: LITHTECH 0x00401c20
LTRESULT BinkVideoMgr::CreateTextureVideo(const char *pFilename, uint32 flags, VideoInst* &pVideo)
{
	return CreateVideo(pFilename, flags, pVideo, LTTRUE);
}


// -------------------------------------------------------------------------------- //
// BinkVideoInst
// -------------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00401c40
BinkVideoInst::BinkVideoInst(BinkVideoMgr *pMgr)
{
	m_bnk = LTNULL;
	m_pMgr = pMgr;
	m_pSurface = LTNULL;
	m_pTextureData = LTNULL;
	m_BufferFormat = 0;
}

// FUNCTION: LITHTECH 0x00401cc0
BinkVideoInst::~BinkVideoInst()
{
	Term();
}


// FUNCTION: LITHTECH 0x00401cf0
void BinkVideoInst::Term()
{
	MPOS pos;
	VideoSurfaceLink *pLink;

	if(m_bnk)
	{
		while(m_pMgr->m_BinkWait(m_bnk))
		{
		}

		m_pMgr->m_BinkClose(m_bnk);
		m_bnk = LTNULL;
	}

	OnRenderTerm();

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


// FUNCTION: LITHTECH 0x00401da0
LTRESULT BinkVideoInst::Init(const char *pFilename, uint32 flags, LTBOOL bTexture)
{
	uint32 openFlags;

	openFlags = 0;
	if(bTexture == LTTRUE)
	{
		// Texture videos don't play sound.
		m_pMgr->m_BinkSetSoundTrack(0xFFFFFFFF);
		openFlags = BINKSNDTRACK;
	}

	if(flags & PLAYBACK_YINTERLACE)
		openFlags |= BINKALPHA;

	if(flags & PLAYBACK_FROMHANDLE)
		openFlags |= BINKFILEHANDLE;

	if(flags & PLAYBACK_FROMMEMORY)
		openFlags |= BINKFROMMEMORY;

	m_bnk = m_pMgr->m_BinkOpen(pFilename, openFlags);
	if(!m_bnk)
	{
		RETURN_ERROR_PARAM(1, BinkVideoInst::Init, LT_MISSINGFILE, "BinkOpen failed");
	}

	m_bTexture = bTexture;
	m_Flags = flags;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00401e40
LTRESULT BinkVideoInst::InitScreen()
{
	LPDIRECTDRAW7 pDD;
	DDSURFACEDESC2 ddsd;
	DDBLTFX fx;
	HWND hWnd;
	HCURSOR hCursor;

	OnRenderTerm();

	if(m_bnk && g_Render.m_bInitted && (pDD = (LPDIRECTDRAW7)g_Render.GetHook("LPDIRECTDRAW")) != LTNULL)
	{
		m_pMgr->m_bSoftwareCursor = LTFALSE;
		hWnd = (HWND)dsi_GetMainWindow();
		hCursor = (HCURSOR)GetClassLong(hWnd, GCL_HCURSOR);
		if(m_pMgr->m_BinkIsSoftCursorFn(g_Render.GetHook("BACKBUFFER"), hCursor))
		{
			m_pMgr->m_bSoftwareCursor = LTTRUE;
		}

		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);
		if(pDD->GetDisplayMode(&ddsd) != DD_OK)
		{
			RETURN_ERROR_PARAM(1, BinkVideoInst::InitScreen, LT_ERROR, "IDirectDraw::GetDisplayMode failed");
		}

		ddsd.dwWidth = m_bnk->Width;
		ddsd.dwHeight = m_bnk->Height;
		ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
		ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;

		// Find the bink format that matches the screen.
		if(ddsd.ddpfPixelFormat.dwRGBBitCount == 16)
		{
			m_BufferFormat = BINKSURFACE565;
			if(ddsd.ddpfPixelFormat.dwRBitMask == 0xF800 && ddsd.ddpfPixelFormat.dwGBitMask == 0x7E0 &&
				ddsd.ddpfPixelFormat.dwBBitMask == 0x1F)
			{
				m_BufferFormat = BINKSURFACE565;
			}
			else if(ddsd.ddpfPixelFormat.dwRBitMask == 0x7C00 && ddsd.ddpfPixelFormat.dwGBitMask == 0x3E0 &&
				ddsd.ddpfPixelFormat.dwBBitMask == 0x1F)
			{
				m_BufferFormat = BINKSURFACE555;
			}
			else
			{
				m_BufferFormat = BINKSURFACE565;
			}
		}
		else if(ddsd.ddpfPixelFormat.dwRGBBitCount == 32)
		{
			if(ddsd.ddpfPixelFormat.dwRBitMask == 0xFF0000 && ddsd.ddpfPixelFormat.dwGBitMask == 0xFF00 &&
				ddsd.ddpfPixelFormat.dwBBitMask == 0xFF)
			{
				m_BufferFormat = BINKSURFACE32;
			}
			else if(ddsd.ddpfPixelFormat.dwRBitMask == 0xFF && ddsd.ddpfPixelFormat.dwGBitMask == 0xFF00 &&
				ddsd.ddpfPixelFormat.dwBBitMask == 0xFF0000)
			{
				m_BufferFormat = BINKSURFACE32R;
			}
			else
			{
				m_BufferFormat = BINKSURFACE32;
			}
		}
		else
		{
			m_BufferFormat = BINKSURFACE565;
		}

		if(pDD->CreateSurface(&ddsd, &m_pSurface, LTNULL) != DD_OK)
		{
			m_pSurface = LTNULL;
			RETURN_ERROR_PARAM(1, BinkVideoInst::InitScreen, LT_ERROR, "IDirectDraw::CreateSurface failed");
		}

		// Clear it.
		memset(&fx, 0, sizeof(fx));
		fx.dwSize = sizeof(fx);
		m_pSurface->Blt(LTNULL, LTNULL, LTNULL, DDBLT_COLORFILL | DDBLT_DDFX | DDBLT_WAIT, &fx);
		return LT_OK;
	}

	RETURN_ERROR(1, BinkVideoInst::InitScreen, LT_NOTINITIALIZED);
}


// FUNCTION: LITHTECH 0x004020b0
LTRESULT BinkVideoInst::InitTexture()
{
	OnRenderTerm();

	if(!m_bnk)
	{
		RETURN_ERROR(1, BinkVideoInst::InitTexture, LT_NOTINITIALIZED);
	}

	m_pTextureData = dtx_Alloc(BPP_32, m_bnk->Width, m_bnk->Height, 1, LTNULL, LTNULL, 0);
	if(!m_pTextureData)
	{
		RETURN_ERROR(1, BinkVideoInst::InitTexture, LT_OUTOFMEMORY);
	}

	m_pTextureData->m_Flags2 |= 4;
	m_pTextureData->m_Flags |= 0x40;
	m_Texture.m_pEngineData = m_pTextureData;
	r_BindTexture(&m_Texture, LTTRUE);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00402190
void BinkVideoInst::OnRenderInit()
{
	LTRESULT dResult;

	if(!m_bTexture)
		dResult = InitScreen();
	else
		dResult = InitTexture();

	if(dResult != LT_OK)
		Term();
}

// FUNCTION: LITHTECH 0x004021c0
void BinkVideoInst::OnRenderTerm()
{
	if(m_pSurface)
	{
		m_pSurface->Release();
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

// FUNCTION: LITHTECH 0x00402210
LTRESULT BinkVideoInst::Update()
{
	if(m_bnk && m_bTexture == LTTRUE)
		return UpdateTextureVideo();

	return LT_FINISHED;
}

// FUNCTION: LITHTECH 0x00402230
LTRESULT BinkVideoInst::DrawVideo()
{
	if(m_bnk && m_pSurface)
		return UpdateOnScreen();

	return LT_FINISHED;
}

// FUNCTION: LITHTECH 0x00402250
LTRESULT BinkVideoInst::GetVideoStatus()
{
	if(m_bnk)
		return IsAtLastFrame() ? LT_FINISHED : LT_OK;

	return LT_FINISHED;
}

void BinkVideoInst::Release()
{
	if(m_pMgr->m_Videos.FindElement(this) != BAD_INDEX)
		m_pMgr->m_Videos.RemoveAt(&m_Link);

	delete this;
}

// FUNCTION: LITHTECH 0x00402270
LTRESULT BinkVideoInst::BindToSurface(Surface *pSurface)
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

	RETURN_ERROR(1, BinkVideoInst::BindToSurface, LT_OUTOFMEMORY);
}

void BinkVideoInst::OnSurfaceDestroyed(Surface *pSurface)
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

void BinkVideoInst::OnTextureDestroyed(SharedTexture *pTexture)
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

// FUNCTION: LITHTECH 0x00402330
LTBOOL BinkVideoInst::IsAtLastFrame()
{
	if(m_bnk)
		return m_bnk->FrameNum == (m_bnk->Frames - 1);

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x00402350
LTRESULT BinkVideoInst::UpdateOnScreen()
{
	LPDIRECTDRAWSURFACE7 pBackBuffer;
	DDSURFACEDESC2 ddsd;
	RECT srcRect, destRect;
	uint32 waitResult;

	pBackBuffer = (LPDIRECTDRAWSURFACE7)g_Render.GetHook("BACKBUFFER");
	if(!pBackBuffer)
	{
		RETURN_ERROR_PARAM(2, BinkVideoInst::UpdateOnScreen, LT_NOTINITIALIZED, "GetHook(BACKBUFFER) failed");
	}

	if(IsAtLastFrame())
		return LT_OK;

	waitResult = m_pMgr->m_BinkWait(m_bnk);
	if(!waitResult)
	{
		ddsd.dwSize = sizeof(ddsd);

		if(m_pMgr->m_bSoftwareCursor)
		{
			m_pMgr->m_SoftCursorCount = m_pMgr->m_BinkCheckCursorFn(dsi_GetMainWindow(), 0, 0,
				m_bnk->Width, m_bnk->Height);
		}

		if(m_pSurface->Lock(LTNULL, &ddsd, DDLOCK_WAIT | DDLOCK_WRITEONLY, LTNULL) == DD_OK)
		{
			m_pMgr->m_BinkToBuffer(m_bnk, ddsd.lpSurface, ddsd.lPitch, m_bnk->Height, 0, 0, m_BufferFormat);
			m_pMgr->m_BinkDoFrame(m_bnk);
			m_pSurface->Unlock(LTNULL);
		}
	}

	if(m_Flags & PLAYBACK_FULLSCREEN)
	{
		srcRect.left = 0;
		srcRect.top = 0;
		srcRect.right = m_bnk->Width;
		srcRect.bottom = m_bnk->Height;

		destRect.top = 0;
		destRect.left = 0;
		destRect.right = g_Render.m_Width;
		destRect.bottom = g_Render.m_Height;

		pBackBuffer->Blt(&destRect, m_pSurface, &srcRect, DDBLT_WAIT, LTNULL);
	}
	else
	{
		// Center it, clipping to the screen.
		srcRect.left = 0;
		srcRect.top = 0;
		srcRect.right = m_bnk->Width;
		srcRect.bottom = m_bnk->Height;

		destRect.left = (g_Render.m_Width - m_bnk->Width) / 2;
		destRect.top = (g_Render.m_Height - m_bnk->Height) / 2;

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

		pBackBuffer->BltFast(destRect.left, destRect.top, m_pSurface, &srcRect, DDBLTFAST_WAIT);
	}

	if(!waitResult && !IsAtLastFrame())
		m_pMgr->m_BinkNextFrame(m_bnk);

	return LT_OK;
}


// FUNCTION: LITHTECH 0x004025a0
LTRESULT BinkVideoInst::UpdateTextureVideo()
{
	if(!m_bnk || !m_pTextureData)
	{
		RETURN_ERROR(1, BinkVideoInst::UpdateTextureVideo, LT_FINISHED);
	}

	if(IsAtLastFrame())
		return LT_OK;

	if(!m_pMgr->m_BinkWait(m_bnk))
	{
		m_pMgr->m_BinkToBuffer(m_bnk, m_pTextureData->m_Mips[0].m_Data, m_pTextureData->m_Mips[0].m_Pitch,
			m_bnk->Height, 0, 0, BINKSURFACE565);
		m_pMgr->m_BinkDoFrame(m_bnk);
		r_BindTexture(&m_Texture, LTTRUE);

		if(IsAtLastFrame())
		{
			if(m_pMgr->m_bSoftwareCursor)
				m_pMgr->m_BinkRestoreCursorFn(m_pMgr->m_SoftCursorCount);
		}
		else
		{
			m_pMgr->m_BinkNextFrame(m_bnk);
		}
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00401c80 ??_GVideoInst@@MAEPAXI@Z
// FUNCTION: LITHTECH 0x00401ca0 ??_GBinkVideoInst@@UAEPAXI@Z
