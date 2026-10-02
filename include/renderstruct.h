// Talon renderer interface structure (Jupiter runtime/shared/src/sys/win/renderstruct.h).
// Only recovered members. The engine fills the function hooks in r_InitRenderStruct.
#ifndef __RENDERSTRUCT_H__
#define __RENDERSTRUCT_H__

#include "ltbasedefs.h"

struct SharedTexture;
class TextureData;
class Attachment;
struct RenderStructInit;

typedef void* HLTPARAM;

// Talon RenderStruct: only the members the engine side touches are named.
struct RenderStruct
{
	RenderStruct() {}

	LTObject*		(*ProcessAttachment)(LTObject *pParent, Attachment *pAttachment);	// 0x00
	SharedTexture*	(*GetSharedTexture)(const char *pFilename);					// 0x04
	TextureData*	(*GetTexture)(SharedTexture *pTexture);						// 0x08
	void			(*FreeTexture)(SharedTexture *pTexture);					// 0x0c
	void			(*RunConsoleString)(char *pString);							// 0x10
	void			(*ConsolePrint)(char *pMsg, ...);							// 0x14
	HLTPARAM		(*GetParameter)(char *pName);								// 0x18
	float			(*GetParameterValueFloat)(HLTPARAM hParam);					// 0x1c
	char*			(*GetParameterValueString)(HLTPARAM hParam);				// 0x20
	void			(*Unknown24)();												// 0x24 (engine passes an empty function)
	uint32			(*IncObjectFrameCode)();									// 0x28
	uint32			(*GetObjectFrameCode)();									// 0x2c
	uint16			(*IncCurTextureFrameCode)();								// 0x30
	void*			(*Alloc)(uint32 size);										// 0x34
	void			(*Free)(void *ptr);											// 0x38

	uint32			m_Width;		// 0x3c
	uint32			m_Height;		// 0x40
	int				m_bInitted;		// 0x44

	uint8			m_Pad48[0x70 - 0x48];

	int				(*Init)(RenderStructInit *pInit);	// 0x70 Returns RENDER_OK for success, or an error code.
	void			(*Term)();							// 0x74
	void			(*BindTexture)(SharedTexture *pTexture, LTBOOL bTextureChanged);	// 0x78
	void			(*UnbindTexture)(SharedTexture *pTexture);						// 0x7c

	uint8			m_Pad80[0x8c - 0x80];
	void			(*Clear)(LTRect *pRect, uint32 flags, LTVector *pColor);	// 0x8c
	LTBOOL			(*Start3D)();						// 0x90
	LTBOOL			(*End3D)();							// 0x94
	LTBOOL			(*IsIn3D)();						// 0x98
	LTBOOL			(*StartOptimized2D)();				// 0x9c
	void			(*EndOptimized2D)();				// 0xa0
	uint8			m_PadA4[0xa8 - 0xa4];
	LTBOOL			(*SetOptimized2DBlend)(LTSurfaceBlend blend);	// 0xa8
	uint8			m_PadAC[0xb0 - 0xac];
	LTBOOL			(*SetOptimized2DColor)(HLTCOLOR hColor);		// 0xb0
	uint8			m_PadB4[0xc0 - 0xb4];
	void*			(*GetHook)(char *pName);			// 0xc0 renderer objects by name ("LPDIRECTDRAW", "BACKBUFFER")
	uint8			m_PadC4[0x10c - 0xc4];
	SharedTexture	*m_pTexture10C;	// 0x10c textures the renderer holds (tagged by cm_TagUsedTextures; names unknown)
	struct RSTextureRef
	{
		SharedTexture	*m_pTexture;
		uint8			m_Pad04[0x14 - 0x4];
	}				m_TextureRefs[2];	// 0x110
	LTVector		m_GlobalLightDir;	// 0x138 (0,-2,-1) normalized by r_InitRenderStruct
	uint8			m_Pad144[0x150 - 0x144];
	uint32			m_Unknown150;		// 0x150
	uint32			m_Unknown154;		// 0x154
};

#define LTRENDER_VERSION	3421
#define RENDER_OK			0
#define RENDER_ERROR		1

struct RenderStructInit
{
	int		m_RendererVersion;	// 0x000 The renderer MUST set this to LTRENDER_VERSION.
	RMode	m_Mode;				// 0x004 What mode we want to use.
	void	*m_hWnd;			// 0x218 The main window.
};

#endif  // __RENDERSTRUCT_H__
