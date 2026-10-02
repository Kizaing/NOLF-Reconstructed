// Video playback managers (Jupiter runtime/kernel/src/sys/win/videomgr.h).
// Talon: the managers are created for a client manager, play to the screen or onto a
// world surface, and VideoMgr has a non-virtual destructor.
#ifndef __VIDEOMGR_H__
#define __VIDEOMGR_H__

#include "ltbasedefs.h"
#include "nexus.h"
#include "iltvideomgr.h"
#include "../../build/proj/LT2/lithshared/stdlith/stdlithdefs.h"
#include "../../build/proj/LT2/lithshared/stdlith/multilinklist.h"

class CClientMgr;
struct SharedTexture;
struct Surface;

// 0x00444970 (leech.cpp).
LTRESULT nexus_AddLeech(Nexus *pNexus, Leech *pLeech);

LTRESULT VMSurfaceLeechFn(Nexus *pNexus, Leech *pLeech, int msg, void *pUserData);
LTRESULT VMTextureLeechFn(Nexus *pNexus, Leech *pLeech, int msg, void *pUserData);

// GLOBAL: LITHTECH 0x004d7d88
extern LeechDef g_VMSurfaceLeechDef;
// GLOBAL: LITHTECH 0x004d7d90
extern LeechDef g_VMTextureLeechDef;

class VideoInst;

// Ties a texture video to a surface it draws onto: the video swaps its own SharedTexture into
// the surface and watches the surface and its original texture through leeches. 0x2c bytes.
class VideoSurfaceLink
{
public:
				VideoSurfaceLink(VideoInst *pVideo);	// 0x0049d090
				~VideoSurfaceLink();					// 0x0049d0c0

	void		SetSurface(Surface *pSurface);			// 0x0049d0e0
	void		SetTexture(SharedTexture *pTexture);	// 0x0049d120

	Surface			*m_pSurface;		// 0x00 (its Nexus is at +0x24)
	Leech			m_SurfaceLeech;		// 0x04
	SharedTexture	*m_pTexture;		// 0x10 the surface's original texture (Nexus at +0)
	Leech			m_TextureLeech;		// 0x14
	CMLLNode		m_Link;				// 0x20 in the video's m_SurfaceLinks
};

class VideoInst
{
protected:
	virtual			~VideoInst() {}

public:
	virtual void	Release() {}
	virtual void	OnRenderInit()=0;
	virtual void	OnRenderTerm()=0;

	// Update the video playback.
	// Returns LT_OK *if a new frame is ready*.
	// Returns LT_INPROGRESS if the old frame's contents still hold.
	// Returns LT_FINISHED if the video is done playing.
	virtual LTRESULT Update()=0;

	// If this is an on-screen video, this draws the video to the screen.
	virtual LTRESULT DrawVideo()=0;

	// LT_OK = still playing
	// LT_FINISHED = done playing
	virtual LTRESULT GetVideoStatus()=0;

	// Texture videos: draw onto this surface.
	virtual LTRESULT BindToSurface(Surface *pSurface)=0;

	// Called by the leeches when a bound surface or its original texture goes away.
	virtual void	OnSurfaceDestroyed(Surface *pSurface)=0;
	virtual void	OnTextureDestroyed(SharedTexture *pTexture)=0;

public:
	CMLLNode		m_Link;			// 0x04 in VideoMgr::m_Videos
};

class VideoMgr
{
public:
	VideoMgr(CClientMgr *pClientMgr) : m_pClientMgr(pClientMgr) {}

	// Create a specific type of video and adds it to m_Videos.
	virtual LTRESULT CreateScreenVideo(const char *pFilename, uint32 flags, VideoInst* &pVideo)=0;
	virtual LTRESULT CreateTextureVideo(const char *pFilename, uint32 flags, VideoInst* &pVideo)=0;

	// Updates all the videos.. updates textures using texture videos.
	virtual void	UpdateVideos();

	// Calls through to all the videos.
	virtual void	OnRenderInit();
	virtual void	OnRenderTerm();

public:
	// Currently playing videos.
	CMultiLinkList<VideoInst*>	m_Videos;	// 0x04
	CClientMgr					*m_pClientMgr;	// 0x0c
};

// Create and initialize a video manager ("BINK" or "SMACKER").  Returns NULL if it can't be
// initialized (ie: if the video DLLs are missing).
VideoMgr* CreateVideoMgr(CClientMgr *pClientMgr, const char *pName);

#endif  // __VIDEOMGR_H__
