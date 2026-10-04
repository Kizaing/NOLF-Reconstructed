// The renderer's view and clip state is the ViewParams object `g_ViewParams`, anchored at 0x10055cf8. Its layout is
// mapped from the renderer's linked data references; former standalone clip-plane and matrix aliases are members below.
#ifndef __D3DREN_VIEWPARAMS_H__
#define __D3DREN_VIEWPARAMS_H__

#include "ltbasedefs.h"
#include "ltmatrix.h"
#include "ltplane.h"
#include "ltrect.h"

// Current clip-plane mask (bit 0 near, 1 far, 2 left, 3 top, 4 right, 5 bottom; 0x3f = everything).
// GLOBAL: D3DREN 0x10056274
extern uint32 DAT_10056274;

// GLOBAL: D3DREN 0x1005626c
extern int DAT_1005626c;		// guess: triangles drawn by the model drawer this frame ("Model triangles drawn: %d")
// GLOBAL: D3DREN 0x1005668c
extern int DAT_1005668c;		// guess: number of plane clip tests done this frame ("Num Clip Tests: %d")

struct TLVertex;
// GLOBAL: D3DREN 0x1005627c
extern TLVertex *DAT_1005627c;	// guess: scratch output vertex buffer of the polygon clippers

// The view box passed to d3d_InitFrustum2 (Jupiter d3d_viewparams.h ViewBoxDef; Talon layout identical, 0x1c bytes).
struct ViewBoxDef
{
	LTVector	m_COP;				// 0x00 Center of projection.
	float		m_WindowSize[2];	// 0x0c Half-width and half-height of view window.
	float		m_NearZ, m_FarZ;	// 0x14 Near and far Z (in relation to m_COP.z).
};

// The renderer's view state `g_ViewParams` (0x10055cf8): its Talon layout is mapped from d3d_InitFrustum2
// (0x1000f72b) and linked data references. Names follow Jupiter only where the member has the same role; other fields
// use m_Unk<offset>. Owner: unit sys/d3d/common_stuff (package W5).
class ViewParams
{
public:
	ViewParams() {}		// 0x1000f3af out-of-line copy in unit sys/d3d/common_stuff (/O1); the /O2 units inline it (only the LTRect member needs construction)

	ViewBoxDef		m_ViewBox;				// 0x00 copy of the input of d3d_InitFrustum2
	LTRect			m_Rect;					// 0x1c rounded screen rectangle (the inline constructor zeroes it)
	float			m_Unk2c;				// 0x2c screenMinX
	float			m_Unk30;				// 0x30 screenMaxX - 1.0 (_DAT_100461cc)
	float			m_Unk34;				// 0x34 screenMinY
	float			m_Unk38;				// 0x38 screenMaxY - 1.0
	int				m_Unk3c;				// 0x3c m_Rect.left
	int				m_Unk40;				// 0x40 m_Rect.top
	int				m_Unk44;				// 0x44 m_Rect.right - 1
	int				m_Unk48;				// 0x48 m_Rect.bottom - 1
	float			m_Unk4c;				// 0x4c (derived from the view box, see d3d_InitFrustum2)
	float			m_Unk50;				// 0x50
	float			m_fScreenWidth;			// 0x54 screenMaxX - screenMinX (Jupiter m_fScreenWidth)
	float			m_fScreenHeight;		// 0x58 screenMaxY - screenMinY (Jupiter m_fScreenHeight)
	float			m_Unk5c;				// 0x5c
	float			m_Unk60;				// 0x60
	float			m_Unk64;				// 0x64
	float			m_Unk68;				// 0x68
	float			m_Unk6c;				// 0x6c
	float			m_Unk70;				// 0x70
	float			m_Unk74;				// 0x74
	float			m_Unk78;				// 0x78
	float			m_Unk7c;				// 0x7c
	float			m_Unk80;				// 0x80
	float			m_Unk84;				// 0x84
	float			m_NearZ;				// 0x88 Near clip Z
	float			m_FarZ;					// 0x8c clamped far Z
	float			m_Unk90;				// 0x90 copy of m_FarZ
	float			m_Unk94;				// 0x94 COP.z / WindowSize[0]
	float			m_Unk98;				// 0x98 COP.z / WindowSize[1]
	LTMatrix		m_mInvView;				// 0x9c the viewer matrix passed in (view to world transform)
	LTMatrix		m_mView;				// 0xdc world space to camera space
	LTMatrix		m_Unk11c;				// 0x11c
	LTMatrix		m_Unk15c;				// 0x15c
	LTMatrix		m_DeviceTimesProjection;	// 0x19c device * projection
	LTMatrix		m_FullTransform;		// 0x1dc all the above put together
	LTMatrix		m_mIdentity;			// 0x21c Identity
	LTMatrix		m_Unk25c;				// 0x25c shear (COP to the origin)
	LTMatrix		m_Unk29c;				// 0x29c
	LTMatrix		m_Unk2dc;				// 0x2dc
	float			m_Unk31c;				// 0x31c cached projection terms (d3d_InitFrustum2's last helper 0x1000f6de)
	float			m_Unk320;				// 0x320
	float			m_Unk324;				// 0x324
	float			m_Unk328;				// 0x328
	float			m_Unk32c;				// 0x32c
	float			m_Unk330;				// 0x330
	LTVector		m_ViewPoints[5];		// 0x334 camera-space apex and the four far corners, moved to world space
	LTPlane			m_CSClipPlanes[6];		// 0x370 camera-space clip planes
	LTPlane			m_ClipPlanes[6];		// 0x3d0 world-space clip planes
	LTPlane			m_Unk430[6];			// 0x430 copy of m_CSClipPlanes with plane 0 distance = ReallyCloseNearZ
	LTVector		m_Up;					// 0x490
	LTVector		m_Right;				// 0x49c
	LTVector		m_Forward;				// 0x4a8
	LTVector		m_Pos;					// 0x4b4 viewer position
	LTVector		m_SkyViewPos;			// 0x4c0 (guess: written by d3d_SetupSkyStuff)
	uint32			m_Unk4cc;				// 0x4cc 0 (d3d_InitFrustum2)
	float			m_Unk4d0;				// 0x4d0 1.0f (d3d_InitFrustum2)
	int				m_Unk4d4;				// 0x4d4: 0 at the start of d3d_InitFrustum2, tested by the object queues
	uint8			m_Pad4d8[0x4e4 - 0x4d8];
	LTVector		m_Unk4e4;				// 0x4e4 (DAT_100561dc) the position the vertical fog values below were computed for (W2 FUN_1000f1a0)
	float			m_Unk4f0;				// 0x4f0 (DAT_100561e8) vertical fog value at that height (VFogMinYVal..VFogMaxYVal)
	int				m_Unk4f4;				// 0x4f4 (DAT_100561ec) vertical fog zone: 0 below VFogMinY, 1 above VFogMaxY, 2 in between

	// guess: stores the viewer position and derives the vertical fog values from its height (unit unk/1000f160, 3d_ops).
	void FUN_1000f1a0(LTVector vPos);				// 0x1000f1a0 (the position is passed by value: callers copy the LTVector with movsd, callee pops 0xc)
};
// GLOBAL: D3DREN 0x10055cf8
extern ViewParams g_ViewParams;

#endif
