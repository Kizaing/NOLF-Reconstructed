// DirectEngine's interface to the renderer and the global RenderStruct
// (Jupiter runtime/kernel/src/sys/win/render.h, shared/src/sys/win/renderstruct.h).
#ifndef __RENDER_H__
#define __RENDER_H__

#include "ltbasedefs.h"

class CClientMgr;
struct SharedTexture;
class TextureData;
class Attachment;

#include "renderstruct.h"

// GLOBAL: LITHTECH 0x004e4888
extern RenderStruct g_Render;
// The current (or last successful) config for the renderer.
// GLOBAL: LITHTECH 0x004e4670
extern RMode g_RMode;

// Render initialization status codes.
#define R_OK					0
#define R_CANTLOADLIBRARY		1
#define R_INVALIDRENDERDLL		2
#define R_INVALIDRENDEROPTIONS	3

inline LTBOOL r_IsRenderInitted() {return g_Render.m_bInitted;}

// Called right at the beginning by the client.. initializes the RenderStruct data members.
void r_InitRenderStruct(LTBOOL bFullClear);

// Initializes the renderer.
LTRESULT r_InitRender(CClientMgr *pClientMgr, RMode *pMode);

// surfaceHandling
//    0 = leave surfaces alone
//    1 = backup surfaces
//    2 = delete surfaces
LTRESULT r_TermRender(CClientMgr *pClientMgr, int surfaceHandling);

void r_BindTexture(SharedTexture *pSharedTexture, LTBOOL bTextureChanged);

// Unbinds the texture from the device and frees its engine data.
void r_UnbindTexture(SharedTexture *pSharedTexture);

// Called by the renderer and ILTClient::ProcessAttachments.
LTObject* r_ProcessAttachment(LTObject *pParent, Attachment *pAttachment);

// Loads the texture's data (TextureData) and hooks it to the SharedTexture.
LTRESULT r_LoadSystemTexture(SharedTexture *pSharedTexture, TextureData **ppTextureData, LTBOOL bBind);

// Frees the associated texture data and cleans up references to it.
void r_UnloadSystemTexture(TextureData *pTexture);

// All the loaded TextureDatas, in an MRU (most recently used are at the start of the list).
struct SysCache
{
	uint32	m_MaxMem;	// 0x00
	uint32	m_CurMem;	// 0x04 How much memory currently used?
	LTList	m_List;		// 0x08
};

// GLOBAL: LITHTECH 0x004e4658
extern SysCache g_SysCache;

#endif  // __RENDER_H__
