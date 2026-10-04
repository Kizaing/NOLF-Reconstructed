// d3d.ren SceneDesc: what the engine's cm_Render (src/client/clientmgr.cpp, a local definition there) hands to
// RenderStruct::RenderScene.  Same layout as the engine's, renderer-private copy (the engine keeps its definition in the
// .cpp; the renderer unit sys/d3d/common_stuff, package W5, owns g_pSceneDesc, 0x100566b4).
//
// NAME: SceneDesc and the members: Jupiter renderstruct.h SceneDesc where the use matches (engine decomp clientmgr.cpp,
// which is the evidence for the Talon offsets); names marked "name unknown" in the engine header stay m_Unk.
#ifndef __D3DREN_SCENEDESC_H__
#define __D3DREN_SCENEDESC_H__

#include "ltbasedefs.h"

// Jupiter renderstruct.h DRAWMODE_ values.
#define DRAWMODE_NORMAL		1	// Draw the world and objects.
#define DRAWMODE_OBJECTLIST	2	// Only render the objects in m_pObjectList.

struct SceneRect
{
	int left, top, right, bottom;
};

struct SceneDesc
{
	int			m_DrawMode;						// 0x00 DRAWMODE_
	uint32		*m_pTicks_Render_Objects;		// 0x04
	uint32		*m_pTicks_Render_Models;		// 0x08
	uint32		*m_pTicks_Render_Sprites;		// 0x0c
	uint32		*m_pTicks_Render_WorldModels;	// 0x10
	uint32		*m_pTicks_Render_ParticleSystems;	// 0x14
	uint32		*m_pTicks_Render_PolyGrids;		// 0x18 (name unknown)
	LTVector	m_GlobalModelLightAdd;			// 0x1c console "modeladd"
	LTVector	m_GlobalModelDirAdd;			// 0x28 (name unknown)
	uint32		m_hRenderContext;				// 0x34 the world's render data (MainWorld+0x1c8)
	LTVector	m_Unknown38;					// 0x38
	LTVector	m_Unknown44;					// 0x44
	LTVector	m_GlobalLightScale;				// 0x50
	LTVector	m_GlobalVertexTint;				// 0x5c
	LTVector	m_GlobalModelDirAdd2;			// 0x68 (name unknown)
	LTVector	m_GlobalLightAdd;				// 0x74 the camera's light add
	float		m_FrameTime;					// 0x80
	SkyDef		m_SkyDef;						// 0x84
	LTObject	**m_SkyObjects;					// 0xb4
	int			m_nSkyObjects;					// 0xb8
	SceneRect	m_Rect;							// 0xbc
	float		m_xFov, m_yFov;					// 0xcc
	float		m_FarZ;							// 0xd4
	LTVector	m_Pos;							// 0xd8
	LTRotation	m_Rotation;						// 0xe4
	LTObject	**m_pObjectList;				// 0xf4
	int			m_ObjectListSize;				// 0xf8
	ModelHookFn	m_ModelHookFn;					// 0xfc
	void		*m_ModelHookUser;				// 0x100
};

// The scene being rendered (set by d3d_InitFrame).
// GLOBAL: D3DREN 0x100566b4
extern SceneDesc *g_pSceneDesc;

#endif
