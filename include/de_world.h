// Talon world BSP (Jupiter runtime/world/src/de_world.h). Only recovered members.
// Talon's WorldBsp has a vtable; slot 0 is non-zero for a BSP that isn't transformed
// (obj_SetupWorldModelTransform and w_TransformWorldModel skip it).
#ifndef __DE_WORLD_H__
#define __DE_WORLD_H__

#include <stddef.h>
#include "ltbasedefs.h"
#include "ltdynarray.h"
#include "de_objects.h"	// Node, WorldPoly (Jupiter keeps them in de_world.h)
#include "nexus.h"

struct WorldPoly;
struct FileIdentifier;
struct StateChange;
struct Node;
class ILTStream;

// The special nodes (de_nodes.cpp).
// GLOBAL: LITHTECH 0x004d1aec
extern Node *NODE_IN;
// GLOBAL: LITHTECH 0x004d1af0
extern Node *NODE_OUT;

// SharedTexture::m_RefCount holds the reference count and flags.
#define ST_REFCOUNTMASK		0x7FFF
#define ST_TAGGED			0x8000	// Used by texture management code.

// Two pointers SharedTexture's constructor clears (use unknown).
struct TexturePair
{
	void			*m_pA;
	void			*m_pB;
};

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

	Nexus			m_Nexus;				// 0x00 video leeches attach here
	void			*m_pEngineData;			// 0x08 TextureData* (render.cpp r_GetTexture)
	void			*m_pRenderData;			// 0x0c
	LTLink			m_Link;					// 0x10
	TexturePair		m_Unknown1C;			// 0x1c (use unknown)
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
					Surface()
					{
						m_Nexus.Init(this);
						m_pTexture = LTNULL;
						m_Flags = 0;
						m_TextureFlags = 0;
						m_Unknown36 = 0;
						m_iFirstPoly = 0;
						m_Unknown3A = 0x7FFF;
						m_Unknown3C = 0;
					}

	LTVector		O, P, Q;				// 0x00 texture vectors (SurfaceData)
	Nexus			m_Nexus;				// 0x24
	SharedTexture	*m_pTexture;			// 0x2c
	uint32			m_Flags;				// 0x30 SURF_ flags.
	uint16			m_TextureFlags;			// 0x34
	uint16			m_Unknown36;			// 0x36 index into WorldBsp::m_TextureNames
	uint16			m_iFirstPoly;			// 0x38 first poly using this surface (WorldPoly::m_iNextSurfacePoly)
	uint16			m_Unknown3A;			// 0x3a (0x7FFF until loaded)
	uint8			m_Unknown3C;			// 0x3c
	uint8			m_Color[3];				// 0x3d
};

#define SURF_INVISIBLE		(1<<2)

// WorldBsp::m_WorldInfoFlags.
#define WIF_MOVEABLE		(1<<1)	// The BSP gets a second, transformed copy.
#define WIF_MAINWORLD		(1<<2)
#define WIF_TERRAIN			(1<<3)	// Talon: has terrain sections.
#define WIF_PHYSICSBSP		(1<<4)
#define WIF_VISBSP			(1<<5)

// Node::m_PlaneType.
#define PLANE_POSX			0
#define PLANE_NEGX			1
#define PLANE_POSY			2
#define PLANE_NEGY			3
#define PLANE_POSZ			4
#define PLANE_NEGZ			5
#define PLANE_GENERIC		6

// WorldPoly::m_Flags.
#define WPF_RELIGHT			0xC000	// MainWorld::UpdateLightAnimPolies

// A named portal in a WorldBsp (0x24 bytes).
struct BspPortal
{
	char			*m_pName;				// 0x00
	uint16			m_Unknown04;			// 0x04 (from the file)
	uint16			m_Flags;				// 0x06 PORTAL_ flags (ILTServer::GetPortalFlags)
	uint16			m_Index;				// 0x08 index in WorldBsp::m_Portals
	uint8			m_Pad0A[0xc - 0xa];
	LTVector		m_Center;				// 0x0c
	LTVector		m_Dims;					// 0x18 half dims
};

// A leaf's visibility list (8 bytes, WorldBsp::m_LeafLists).
struct LeafList
{
	uint16			m_PortalID;				// 0x00 0xFFFF = unconditional
	uint16			m_ListSize;				// 0x02
	uint8			*m_pList;				// 0x04 in WorldBsp::m_LeafListData
};

// A BSP leaf (0x30 bytes, only the VisBSP has them).
struct Leaf
{
	LTVector		m_Center;				// 0x00 bounding sphere (w_CalcBoundingSpheres)
	float			m_Radius;				// 0x0c
	LeafList		*m_LeafLists;			// 0x10
	CheapLTLink		m_LeafLinks;			// 0x14 LeafLinks of the objects in this leaf (de_nodes)
	uint8			m_Pad1C[0x20 - 0x1c];
	WorldPoly		**m_Polies;				// 0x20 LAPolyRef pairs until MainWorld::SetupLeafPolies
	uint32			m_nPolies;				// 0x24
	float			m_Unknown28;			// 0x28
	uint8			m_Pad2C[0x2e - 0x2c];
	uint16			m_nLeafLists;			// 0x2e
};

// A grid of mini BSPs over a BSP's box (0x30 bytes).
struct PBlock
{
	void			*m_pNodes;				// 0x00 m_nNodes 6-byte nodes
	uint16			m_nNodes;				// 0x04
	uint16			m_iRoot;				// 0x06
};

class PBlockTable
{
public:
					PBlockTable()
					{
						m_BlockSize.Init();
						m_Origin.Init();
						m_Size[0] = m_Size[1] = m_Size[2] = 0;
						m_XZSize = 0;
						m_nBlocks = 0;
						m_pBlocks = LTNULL;
					}

	void			Term();					// 0x0042c1c0
	LTBOOL			Load(ILTStream *pStream);	// 0x0042c200
	PBlock*			GetBlock(LTVector vPos);	// 0x0042c300

	LTVector		m_BlockSize;			// 0x00
	LTVector		m_Origin;				// 0x0c
	uint32			m_Size[3];				// 0x18 blocks in x, y, z
	uint32			m_XZSize;				// 0x24 m_Size[0] * m_Size[1]
	uint32			m_nBlocks;				// 0x28
	PBlock			*m_pBlocks;				// 0x2c
};

// Talon's BSP interface (vtable 0x004c7174, all pure): WorldBsp and TerrainSection
// (a piece of a terrain world model) implement it.
class WorldBspBase
{
public:
					WorldBspBase();			// 0x0042c9b0

	virtual uint32	IsUntransformed()=0;		// 0x00 (TRUE for a TerrainSection)
	virtual Node*	GetRootNode()=0;			// 0x04
	virtual HPOLY	MakeHPoly(Node *pNode)=0;	// 0x08
	virtual WorldPoly*	GetPolyFromHPoly(HPOLY hPoly)=0;	// 0x0c
	virtual LTBOOL	VSlot4(LTVector vPos)=0;	// 0x10 the PBlock at vPos, as a BOOL (WorldModelInstance::IsPointInside)
	virtual PBlockTable*	GetPBlockTable()=0;	// 0x14
	virtual LTVector*	GetPBlockOrigin()=0;	// 0x18
	virtual Node*	GetNodes()=0;				// 0x1c
	virtual uint32	GetWorldInfoFlags()=0;		// 0x20 WIF_ (ILTPhysics::IsWorldObject)
	virtual LTBOOL	WBSlot9()=0;				// 0x24 IsVisBSP: (m_WorldInfoFlags & WIF_VISBSP) for a WorldBsp
	virtual float	GetBoundRadius()=0;			// 0x28

	char			m_WorldName[0x41];		// 0x04 (copied as 0x41 bytes by the implicit operator=)
};

// Where a terrain section sits in the world tree (8 bytes).
struct WTNodePath
{
	uint8			m_Path[4];				// 0x00 child index bits
	uint32			m_Depth;				// 0x04
};

// A piece of a terrain world model (0xc0 bytes, vtable 0x004c7118); WorldBsp::m_TerrainSections.
class TerrainSection : public WorldBspBase
{
public:
					TerrainSection();		// 0x0042c3b0
					~TerrainSection();		// 0x0042c4a0

	void			Term();					// 0x0042c510
	LTBOOL			Load(WorldBsp *pBsp, ILTStream *pStream, int iSection);	// 0x0042c580
	void			CalcBoundRadius();		// 0x0042c820

	virtual uint32	IsUntransformed();		// 0x004b22a0 (returns 1)
	virtual Node*	GetRootNode();			// 0x0042c900
	virtual HPOLY	MakeHPoly(Node *pNode);	// 0x0042c910
	virtual WorldPoly*	GetPolyFromHPoly(HPOLY hPoly);	// 0x0042c920
	virtual LTBOOL	VSlot4(LTVector vPos);	// 0x0042c930
	virtual PBlockTable*	GetPBlockTable();	// 0x0042c960
	virtual LTVector*	GetPBlockOrigin();	// 0x0042c970
	virtual Node*	GetNodes();				// 0x0042c980
	virtual uint32	GetWorldInfoFlags();	// 0x0042c990
	virtual LTBOOL	WBSlot9();			// 0x0043dac0 (returns 0)
	virtual float	GetBoundRadius();		// 0x0042c9a0

	WTNodePath		m_NodePath;				// 0x48 world tree node of the section (WorldTree::FindNode)
	CMoArray<WorldPoly*>	m_Polies;		// 0x50 (vtable 0x004c6ccc)
	Node			*m_RootNode;			// 0x64
	CMoArray<Node>	m_Nodes;				// 0x68 (vtable 0x004c7144)
	class WorldBsp	*m_pWorldBsp;			// 0x7c the terrain world model it belongs to
	PBlockTable		m_PBlockTable;			// 0x80
	LTVector		m_Center;				// 0xb0
	float			m_BoundRadius;			// 0xbc
};

class WorldBsp : public WorldBspBase
{
public:
					WorldBsp();				// 0x0042c9c0
					~WorldBsp();			// 0x0042ca90 (not virtual)

	void			Clear();				// 0x0042cb10
	void			Term();					// 0x0042cc10
	void			TermNodes();			// 0x0042cde0

	// Shares pOther's data (sets m_BspFlags bit 0); only the nodes are our own (0x0042ce00).
	LTBOOL			InheritFrom(WorldBsp *pOther);
	// pOther's node pNode in our node array (0x0042d400).
	Node*			RemapNode(WorldBsp *pOther, Node *pNode);

	virtual uint32	IsUntransformed();		// 0x0043dac0 (returns 0)
	virtual Node*	GetRootNode();			// 0x0042d320
	virtual HPOLY	MakeHPoly(Node *pNode);	// 0x0042d330
	virtual WorldPoly*	GetPolyFromHPoly(HPOLY hPoly);	// 0x0042d370
	virtual LTBOOL	VSlot4(LTVector vPos);	// 0x0042d3a0
	virtual PBlockTable*	GetPBlockTable();	// 0x0042d3d0
	virtual LTVector*	GetPBlockOrigin();	// 0x0042d3e0
	virtual Node*	GetNodes();				// 0x0042d3f0
	virtual uint32	GetWorldInfoFlags();	// 0x0042d2f0
	virtual LTBOOL	WBSlot9();			// 0x0042d300
	virtual float	GetBoundRadius();		// 0x0042d310
	virtual void	WBSlot11(void *p);		// 0x0042d2e0 (name unknown)

	// Sets m_BoundRadius from the polies around m_WorldTranslation (0x0042d2b0).
	void			CalcBoundRadius();

	union
	{
		uint32		m_WorldInfoFlags;	// 0x48 WIF_
		uint8		m_NodePath[4];		// (TerrainSection::m_NodePath, read through a WorldBsp* by objectmgr)
	};
	uint32			m_BspFlags;				// 0x4c bit 0: the data belongs to another BSP (Term frees nothing)
	uint32			m_MemoryUse;			// 0x50 added to g_WorldGeometryMemory
	CMoArray<uint32>	m_PolyAnimRefs;		// 0x54 (vtable 0x004c7200) WorldPoly light anim refs

	LTPlane			*m_Planes;				// 0x68 Planes.
	uint32			m_nPlanes;				// 0x6c
	Node			*m_Nodes;				// 0x70 Nodes (an HPOLY indexes these in Talon).
	uint32			m_nNodes;				// 0x74
	Surface			*m_Surfaces;			// 0x78 Surfaces.
	uint32			m_nSurfaces;			// 0x7c
	LeafList		*m_LeafLists;			// 0x80
	uint32			m_nLeafLists;			// 0x84
	Leaf			*m_Leafs;				// 0x88
	uint32			m_nLeafs;				// 0x8c
	WorldPoly		**m_LeafPolies;			// 0x90
	uint32			m_nLeafPolies;			// 0x94
	uint32			m_LeafListDataSize;		// 0x98
	Node			*m_RootNode;			// 0x9c
	WorldPoly		**m_Polies;				// 0xa0 Polies.
	uint32			m_nPolies;				// 0xa4
	LTVector		*m_Points;				// 0xa8 Vertices.
	uint32			m_nPoints;				// 0xac
	BspPortal		*m_Portals;				// 0xb0 (Talon)
	uint32			m_nPortals;				// 0xb4
	char			*m_TextureNameData;		// 0xb8 The list of texture names used in this world.
	char			**m_TextureNames;		// 0xbc
	uint32			m_nTextures;			// 0xc0
	LTVector		m_MinBox;				// 0xc4 Bounding box.
	LTVector		m_MaxBox;				// 0xd0
	uint32			m_MaxTreeDepth;			// 0xdc
	uint32			m_UnknownE0;			// 0xe0
	uint32			m_UnknownE4;			// 0xe4
	LTVector		m_WorldTranslation;		// 0xe8 Centering translation for WorldModels.
	void			*m_pUnknownF4;			// 0xf4 (freed by Term)
	uint16			m_Index;				// 0xf8 index in the world's BSP list (HPOLY high word)
	uint8			m_PadFA[0xfc - 0xfa];
	char			*m_PolyData;			// 0xfc Data blocks
	uint32			m_PolyDataSize;			// 0x100
	uint8			*m_LeafListData;		// 0x104
	PBlockTable		m_PBlockTable;			// 0x108
	CMoArray<TerrainSection>	m_TerrainSections;	// 0x138 (vtable 0x004c71d0)
	float			m_BoundRadius;			// 0x14c
	int32			m_Unknown150;			// 0x150 (-1 when cleared)
};

#define WB_CHECKOFFSET(member, ofs) \
	typedef char WB_Check##member[(offsetof(WorldBsp, member) == (ofs)) ? 1 : -1];
WB_CHECKOFFSET(m_WorldInfoFlags, 0x48)
WB_CHECKOFFSET(m_Planes, 0x68)
WB_CHECKOFFSET(m_MinBox, 0xc4)
WB_CHECKOFFSET(m_Index, 0xf8)
WB_CHECKOFFSET(m_PBlockTable, 0x108)
WB_CHECKOFFSET(m_TerrainSections, 0x138)
typedef char WB_CheckSize[(sizeof(WorldBsp) == 0x154) ? 1 : -1];
typedef char TS_CheckSize[(sizeof(TerrainSection) == 0xc0) ? 1 : -1];

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

// World BSP leaf object lists (de_nodes.cpp).
class LTObject;
void	w_RemoveObjectFromLeaf(LTObject *pObj);					// 0x00430680
Node*	w_AddObjectToLeaf(WorldBsp *pBsp, LTObject *pObj);		// 0x004305f0

#endif  // __DE_WORLD_H__
