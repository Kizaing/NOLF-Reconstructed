// Smacker video manager (Talon; Jupiter replaced it with dshowvideomgrimpl).
// Talon loads smackw32.dll at runtime.
#ifndef __SMACKVIDEOMGRIMPL_H__
#define __SMACKVIDEOMGRIMPL_H__

#include "videomgr.h"
#include "de_world.h"
#include "pixelformat.h"

// Smacker's movie handle (only the members the engine reads).
struct Smack
{
	uint32		Version;		// 0x000
	uint32		Width;			// 0x004
	uint32		Height;			// 0x008
	uint32		Frames;			// 0x00c
	uint8		m_Pad10[0x6c - 0x10];
	uint8		Palette[772];	// 0x06c 256 RGB triples (+ padding)
	uint8		m_Pad370[0x374 - 0x370];
	uint32		FrameNum;		// 0x374
};

typedef void (__stdcall *SmackSoundUseMSSFn)(void *dd);
typedef Smack* (__stdcall *SmackOpenFn)(const char *name, uint32 flags, uint32 extrabuf);
typedef void (__stdcall *SmackCloseFn)(Smack *smk);
typedef uint32 (__stdcall *SmackWaitFn)(Smack *smk);
typedef void (__stdcall *SmackToBufferFn)(Smack *smk, uint32 left, uint32 top, uint32 Pitch, uint32 destheight, void *buf, uint32 Flags);
typedef uint32 (__stdcall *SmackDoFrameFn)(Smack *smk);
typedef void (__stdcall *SmackNextFrameFn)(Smack *smk);

// -------------------------------------------------------------------------------- //
// SmackVideoMgr. 0x34 bytes.
// -------------------------------------------------------------------------------- //
class SmackVideoMgr : public VideoMgr
{
public:
						SmackVideoMgr(CClientMgr *pClientMgr);	// 0x0049d310
						~SmackVideoMgr();						// 0x0049d350

	LTRESULT			Init();									// 0x0049d380

	virtual LTRESULT	CreateScreenVideo(const char *pFilename, uint32 flags, VideoInst* &pVideo);
	virtual LTRESULT	CreateTextureVideo(const char *pFilename, uint32 flags, VideoInst* &pVideo);

	LTRESULT			CreateVideo(const char *pFilename, uint32 flags, VideoInst* &pVideo, LTBOOL bTexture);

public:
	uint32				m_Unknown10;		// 0x10
	SmackSoundUseMSSFn	m_SmackSoundUseMSS;	// 0x14
	SmackOpenFn			m_SmackOpen;		// 0x18
	SmackCloseFn		m_SmackClose;		// 0x1c
	SmackWaitFn			m_SmackWait;		// 0x20
	SmackToBufferFn		m_SmackToBuffer;	// 0x24
	SmackDoFrameFn		m_SmackDoFrame;		// 0x28
	SmackNextFrameFn	m_SmackNextFrame;	// 0x2c
	HINSTANCE			m_hSmackDLL;		// 0x30
};


struct IDirectDrawSurface7;

// -------------------------------------------------------------------------------- //
// SmackVideoInst. 0xb0 bytes.
// -------------------------------------------------------------------------------- //
class SmackVideoInst : public VideoInst
{
public:

						SmackVideoInst(SmackVideoMgr *pMgr);
	virtual				~SmackVideoInst();

	LTRESULT			Init(const char *pFilename, uint32 flags, LTBOOL bTexture);
	LTRESULT			InitScreen();
	LTRESULT			InitTexture();
	void				Term();

	virtual void		Release();
	virtual void		OnRenderInit();
	virtual void		OnRenderTerm();

	virtual LTRESULT	Update();
	virtual LTRESULT	DrawVideo();
	virtual LTRESULT	GetVideoStatus();

	virtual LTRESULT	BindToSurface(Surface *pSurface);
	virtual void		OnSurfaceDestroyed(Surface *pSurface);
	virtual void		OnTextureDestroyed(SharedTexture *pTexture);

	LTBOOL				IsAtLastFrame();
	LTRESULT			UpdateOnScreen();
	LTRESULT			UpdateTextureVideo();

public:
	CMultiLinkList<VideoSurfaceLink*>	m_SurfaceLinks;	// 0x10 surfaces we draw onto
	Smack				*m_smk;				// 0x18
	SmackVideoMgr		*m_pMgr;			// 0x1c the manager that created us
	IDirectDrawSurface7	*m_pSurface;		// 0x20 on-screen videos: 565 decode surface
	LTBOOL				m_bConvert;			// 0x24 the screen isn't 565: convert through m_pConvertSurface
	IDirectDrawSurface7	*m_pConvertSurface;	// 0x28 surface in the screen format
	PFormat				m_ScreenFormat;		// 0x2c
	class TextureData	*m_pTextureData;	// 0x64 texture videos: the frame
	SharedTexture		m_Texture;			// 0x68 texture videos: swapped into bound surfaces
	uint32				m_Flags;			// 0xa8 PLAYBACK_ flags
	LTBOOL				m_bTexture;			// 0xac
};

#endif  // __SMACKVIDEOMGRIMPL_H__
