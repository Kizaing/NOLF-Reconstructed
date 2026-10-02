// Talon world BSP (Jupiter runtime/world/src/de_world.h). Only recovered members.
// Talon's WorldBsp has a vtable; slot 0 is non-zero for a BSP that isn't transformed
// (obj_SetupWorldModelTransform and w_TransformWorldModel skip it).
#ifndef __DE_WORLD_H__
#define __DE_WORLD_H__

#include "ltbasedefs.h"

struct WorldPoly;
struct FileIdentifier;
struct StateChange;

// SharedTexture::m_RefCount holds the reference count and flags.
#define ST_REFCOUNTMASK		0x7FFF
#define ST_TAGGED			0x8000	// Used by texture management code.

// A texture shared by everything that uses it (Jupiter de_world.h), 0x40 bytes.
struct SharedTexture
{
					SharedTexture();		// 0x0042d450
					~SharedTexture();		// 0x0042d490

	// Copies the DTX command string into m_pCommandLine.
	void			SetCommandLine(const char *pCommandLine);	// 0x0042d4b0

	inline uint16	GetFlags() const			{return (uint16)(m_RefCount & ~ST_REFCOUNTMASK);}
	inline void		SetFlags(uint16 flags)		{m_RefCount &= ST_REFCOUNTMASK; m_RefCount |= (flags & ~ST_REFCOUNTMASK);}
	inline uint16	GetRefCount() const			{return (uint16)(m_RefCount & ST_REFCOUNTMASK);}

	uint8			m_Nexus[0x8];			// 0x00 Nexus (constructed by SharedTexture's ctor; video leeches attach here)
	void			*m_pEngineData;			// 0x08 TextureData* (render.cpp r_GetTexture)
	void			*m_pRenderData;			// 0x0c
	LTLink			m_Link;					// 0x10
	uint8			m_Pad1c[0x24 - 0x1c];
	FileIdentifier	*m_pFile;				// 0x24 File identifier so the client can load it.
	SharedTexture	*m_pLinkedTexture;		// 0x28 (detail texture)
	uint32			m_eTexType;				// 0x2c 0 = detail texture, 1 = EnvMap, 2 = EnvMapAlpha (r_LoadSystemTexture)
	uint16			m_Unknown30;			// 0x30
	uint16			m_RefCount;				// 0x32 Ref count and flags.
	StateChange		*m_pStateChange;		// 0x34 (IClientShell::OnTextureLoad)
	const char		*m_pCommandLine;		// 0x38 (IClientShell::OnTextureLoad)
	uint32			m_Unknown3C;			// 0x3c from the DTX header's m_Extra[4] (r_LoadSystemTexture)
};

struct Node;

// Polygon surface (WorldPoly::m_pSurface).
struct Surface
{
	uint8			m_Pad00[0x2c];
	SharedTexture	*m_pTexture;			// 0x2c
	uint32			m_Flags;				// 0x30 SURF_ flags.
	uint16			m_TextureFlags;			// 0x34
	uint16			m_Unknown36;			// 0x36
	uint16			m_iFirstPoly;			// 0x38 first poly using this surface (WorldPoly::m_iNextSurfacePoly)
	uint8			m_Pad3a[0x40 - 0x3a];	// (0x40 bytes: _TagWorldBspTextures)
};

#define SURF_INVISIBLE		(1<<2)

// A named portal in a WorldBsp (0x24 bytes).
struct BspPortal
{
	char			*m_pName;				// 0x00
	uint8			m_Pad04[0x6 - 0x4];
	uint16			m_Flags;				// 0x06 PORTAL_ flags (ILTServer::GetPortalFlags)
	uint8			m_Pad08[0x24 - 0x8];
};

class WorldBsp
{
public:
					~WorldBsp();			// 0x0042ca90 (not virtual)

	virtual uint32	IsUntransformed();		// vtable slot 0 (name unknown)
	virtual Node*	GetRootNode();			// 0x04 (slots 4-7 unknown)
	virtual HPOLY	MakeHPoly(Node *pNode);	// 0x08
	virtual WorldPoly*	GetPolyFromHPoly(HPOLY hPoly);	// 0x0c
	virtual LTBOOL	VSlot4(LTVector vPos);	// 0x10 (WorldModelInstance::IsPointInside)
	virtual void	VSlot5();
	virtual void	VSlot6();
	virtual void	VSlot7();
	virtual uint32	GetWorldInfoFlags();	// 0x20 WIF_ (ILTPhysics::IsWorldObject)
	virtual LTBOOL	WBSlot9();				// 0x24 (name unknown)

	char			m_WorldName[0x44];		// 0x04
	uint8			m_NodePath[0x68 - 0x48];	// 0x48 terrain section's world tree node path

	LTPlane			*m_Planes;				// 0x68 Planes.
	uint32			m_nPlanes;				// 0x6c
	Node			*m_Nodes;				// 0x70 Nodes (an HPOLY indexes these in Talon).
	uint32			m_nNodes;				// 0x74
	Surface			*m_Surfaces;			// 0x78 Surfaces.
	uint32			m_nSurfaces;			// 0x7c
	uint8			m_Pad80[0x9c - 0x80];
	Node			*m_RootNode;			// 0x9c
	WorldPoly		**m_Polies;				// 0xa0 Polies.
	uint32			m_nPolies;				// 0xa4
	LTVector		*m_Points;				// 0xa8 Vertices.
	uint32			m_nPoints;				// 0xac
	BspPortal		*m_Portals;				// 0xb0 (Talon)
	uint32			m_nPortals;				// 0xb4
	uint8			m_PadB8[0xc4 - 0xb8];
	LTVector		m_MinBox;				// 0xc4 Bounding box.
	LTVector		m_MaxBox;				// 0xd0
	uint8			m_PadDC[0xe8 - 0xdc];
	LTVector		m_WorldTranslation;		// 0xe8 Centering translation for WorldModels.
	uint8			m_PadF4[0xf8 - 0xf4];
	uint16			m_Index;				// 0xf8 index in the world's BSP list (HPOLY high word)
};

#define WD_ORIGINALBSPALLOCED   (1<<0)  // m_pOriginalBsp is ours.
#define WD_WORLDBSPALLOCED      (1<<1)  // m_pWorldBsp is ours.

// A world model's BSPs (Jupiter WorldData), 0x10 bytes.
struct WorldData
{
	WorldData();						// 0x00428360
	~WorldData();						// 0x00428370

	void Clear();						// 0x00428380
	void Term();						// 0x00428390

	// Combination of WD_ flags.
	uint32			m_Flags;			// 0x00
	// Unmodified version.  This is always valid.
	WorldBsp		*m_pOriginalBsp;	// 0x04
	// This version has transformed vertex positions (NULL for non-moving world models).
	WorldBsp		*m_pWorldBsp;		// 0x08
	// This points to m_pWorldBsp unless it's null, otherwise it points to m_pOriginalBsp.
	WorldBsp		*m_pValidBsp;		// 0x0c
};

#endif  // __DE_WORLD_H__
