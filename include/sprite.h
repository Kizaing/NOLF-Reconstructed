// Client sprites (Talon layout recovered from lithtech.exe; Jupiter client/src/sprite.h, spritecontrol).
#ifndef __SPRITE_H__
#define __SPRITE_H__

#include "ltbasedefs.h"
#include "iltstream.h"

class CClientMgr;
struct SharedTexture;

// Jupiter client_filemgr.h (kept here; client_filemgr.h includes this header).
#define FILE_ANYFILE	0
#define FILE_CLIENTFILE	1

class FileRef
{
public:
	FileRef()
	{
		m_FileType = FILE_ANYFILE;
		m_pFilename = NULL;
		m_FileID = 0;		// Jupiter: -1
	}

	int			m_FileType;		// 0x00
	const char	*m_pFilename;	// 0x04
	uint16		m_FileID;		// 0x08
};

// Talon: free function taking the client manager (0x00426050).
SharedTexture* cm_AddSharedTexture(CClientMgr *pClientMgr, FileRef *pRef);

// Sprite control flags (SDK iltspritecontrol.h).
#ifndef SC_PLAY
#define SC_PLAY		(1<<0)
#define SC_LOOP		(1<<1)
#endif

struct SpriteEntry
{
	SharedTexture	*m_pTex;
};

// 0x3c bytes.
struct SpriteAnim
{
	char			m_sName[32];		// 0x00
	SpriteEntry		*m_Frames;			// 0x20
	uint32			m_nFrames;			// 0x24
	uint32			m_MsAnimLength;		// 0x28
	uint32			m_MsFrameRate;		// 0x2c
	LTBOOL			m_bKeyed;			// 0x30
	uint32			m_ColourKey;		// 0x34
	LTBOOL			m_bTranslucent;		// 0x38
};

// 0x14 bytes (Talon has no m_pFileIdent).
struct Sprite
{
	LTLink			m_Link;				// 0x00
	SpriteAnim		*m_Anims;			// 0x0c
	uint32			m_nAnims;			// 0x10
};

// 0x14 bytes.
struct SpriteTracker
{
	Sprite			*m_pSprite;			// 0x00
	SpriteAnim		*m_pCurAnim;		// 0x04
	SpriteEntry		*m_pCurFrame;		// 0x08
	uint32			m_MsCurTime;		// 0x0c
	uint32			m_Flags;			// 0x10
};

Sprite* spr_Create(CClientMgr *pClientMgr, ILTStream *pStream);
void spr_Destroy(Sprite *pSprite);

void spr_InitTracker(SpriteTracker *pTracker, Sprite *pSprite);
void spr_UpdateTracker(SpriteTracker *pTracker, uint32 msDelta);

#endif
