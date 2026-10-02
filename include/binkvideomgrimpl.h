// Bink video manager (Jupiter runtime/kernel/src/sys/win/binkvideomgrimpl.h).
// Talon loads Binkw32.dll at runtime and can also play Smacker files through it.
#ifndef __BINKVIDEOMGRIMPL_H__
#define __BINKVIDEOMGRIMPL_H__

#include "videomgr.h"
#include "de_world.h"

// Bink's movie handle (only the members the engine reads).
struct BINK
{
	uint32		Width;		// 0x00
	uint32		Height;		// 0x04
	uint32		Frames;		// 0x08
	uint32		FrameNum;	// 0x0c
};

typedef BINK* HBINK;

typedef int (__stdcall *BinkSoundFn)(void *open, uint32 param);
typedef void* (__stdcall *BinkMilesFn)(uint32 param);
typedef void* (__stdcall *BinkDSndFn)(uint32 param);
typedef void (__stdcall *BinkSetSoundTrackFn)(uint32 track);
typedef HBINK (__stdcall *BinkOpenFn)(const char *name, uint32 flags);
typedef void (__stdcall *BinkCloseFn)(HBINK bnk);
typedef uint32 (__stdcall *BinkWaitFn)(HBINK bnk);
typedef void (__stdcall *BinkToBufferFn)(HBINK bnk, void *buf, uint32 left, uint32 top, uint32 Pitch, uint32 destheight, uint32 Flags);
typedef uint32 (__stdcall *BinkDoFrameFn)(HBINK bnk);
typedef void (__stdcall *BinkNextFrameFn)(HBINK bnk);
typedef int (__stdcall *BinkIsSoftCursorFn)(void *lpSurface, void *hCur);
typedef int (__stdcall *BinkCheckCursorFn)(void *hWnd, int x, int y, int w, int h);
typedef int (__stdcall *BinkRestoreCursorFn)(int checkcount);

// -------------------------------------------------------------------------------- //
// BinkVideoMgr. 0x54 bytes.
// -------------------------------------------------------------------------------- //
class BinkVideoMgr : public VideoMgr
{
public:
						BinkVideoMgr(CClientMgr *pClientMgr);	// 0x00401890
						~BinkVideoMgr();						// 0x004018d0

	LTRESULT			Init();									// 0x00401900

	virtual LTRESULT	CreateScreenVideo(const char *pFilename, uint32 flags, VideoInst* &pVideo);
	virtual LTRESULT	CreateTextureVideo(const char *pFilename, uint32 flags, VideoInst* &pVideo);

	LTRESULT			CreateVideo(const char *pFilename, uint32 flags, VideoInst* &pVideo, LTBOOL bTexture);

public:
	uint32						m_VideoFlags;			// 0x10

	//Bink interface
	BinkSoundFn					m_BinkSoundFn;			// 0x14
	BinkMilesFn					m_BinkMilesFn;			// 0x18
	BinkDSndFn					m_BinkDSndFn;			// 0x1c
	BinkSetSoundTrackFn			m_BinkSetSoundTrack;	// 0x20
	BinkOpenFn					m_BinkOpen;				// 0x24
	BinkCloseFn					m_BinkClose;			// 0x28
	BinkWaitFn					m_BinkWait;				// 0x2c
	BinkToBufferFn				m_BinkToBuffer;			// 0x30
	BinkDoFrameFn				m_BinkDoFrame;			// 0x34
	BinkNextFrameFn				m_BinkNextFrame;		// 0x38
	BinkIsSoftCursorFn			m_BinkIsSoftCursorFn;	// 0x3c
	BinkCheckCursorFn			m_BinkCheckCursorFn;	// 0x40
	BinkRestoreCursorFn			m_BinkRestoreCursorFn;	// 0x44
	HINSTANCE					m_hBinkDLL;				// 0x48
	int							m_SoftCursorCount;		// 0x4c BinkCheckCursor result
	LTBOOL						m_bSoftwareCursor;		// 0x50
};


struct IDirectDrawSurface7;

// -------------------------------------------------------------------------------- //
// BinkVideoInst. 0x74 bytes.
// -------------------------------------------------------------------------------- //
class BinkVideoInst : public VideoInst
{
public:

						BinkVideoInst(BinkVideoMgr *pMgr);
	virtual				~BinkVideoInst();

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
	BINK				*m_bnk;				// 0x18 handle to the actual bink video
	BinkVideoMgr		*m_pMgr;			// 0x1c the manager that created us
	IDirectDrawSurface7	*m_pSurface;		// 0x20 on-screen videos: offscreen surface
	class TextureData	*m_pTextureData;	// 0x24 texture videos: the frame
	SharedTexture		m_Texture;			// 0x28 texture videos: swapped into bound surfaces
	uint32				m_Flags;			// 0x68 PLAYBACK_ flags
	LTBOOL				m_bTexture;			// 0x6c
	uint32				m_BufferFormat;		// 0x70 the bink format that corresponds to our surface type
};

#endif  // __BINKVIDEOMGRIMPL_H__
