// Talon main world (Jupiter runtime/world/src/de_mainworld.h).
// The Talon SDK calls it MainWorld (ltbasetypes.h: "m_pInternalWorld ... MainWorld* on init"), and
// Jupiter later split it into CWorldSharedBSP / CWorldClientBSP / CWorldServerBSP ("Data taken out
// of the old MainWorld class", world_server_bsp.cpp). Member names follow Jupiter's split classes
// in Talon's m_ style.
//
// Both managers embed one (0x1f0 bytes): CClientMgr::m_World at 0x000 (cm_Init 0x004112c0) and
// CServerMgr::m_World at 0x1a4 (CClientShell::CreateServerMgr 0x00414cd0). Each is followed by its
// ObjectMgr. Layout from the constructor (0x004283e0).
#ifndef __DE_MAINWORLD_H__
#define __DE_MAINWORLD_H__

#include <stddef.h>
#include "ltbasedefs.h"
#include "ltdynarray.h"
#include "de_world.h"
#include "world_tree.h"

class WorldModelInstance;

// World flags (m_WorldFlags).
#define WORLD_OBJECTSLOADED	(1<<0)
#define WORLD_HASVISBSP		(1<<1)	// a world model has WIF_VISBSP (WorldBsp vtable slot 9)
#define WORLD_HASBASELIGHT	(1<<2)	// LoadObjects found "LightAnim_BASE"

// A world model poly that a light anim touches (index pair).
struct LAPolyRef
{
	uint16			m_iWorld;			// 0x00 index into MainWorld::m_WorldModels
	uint16			m_iPoly;			// 0x02 index into that model's original BSP polies
};

// One poly's data in one light anim frame (0x18 bytes, MainWorld::m_LightAnimPolyFrames).
struct LAPolyFrame
{
	uint8			*m_pLightmap;		// 0x00 compressed lightmap (in m_LightAnimData)
	uint16			m_LightmapSize;		// 0x04
	uint8			m_Pad06[0x8 - 0x6];
	uint8			*m_pVertR;			// 0x08 per-vertex colours (file version 70 only)
	uint8			*m_pVertG;			// 0x0c
	uint8			*m_pVertB;			// 0x10
	uint8			m_nVerts;			// 0x14
	uint8			m_Pad15[0x18 - 0x15];
};

// Talon light animation (0x60 bytes, world light anims driven by ILTLightAnim).
struct LightAnim
{
					LightAnim();					// 0x00428320 (de_mainworld.cpp; not inline: every caller calls it)

	char			m_Name[0x20];		// 0x00
	LTBOOL			m_bShadowMap;		// 0x20 LAInfo::m_bShadowMap (impl_common la_GetInfo)
	LAPolyFrame		**m_pFrames;		// 0x24 m_nFrames entries, each m_nPolies LAPolyFrames
	uint8			m_nFrames;			// 0x28
	uint8			m_Pad29[0x2c - 0x29];
	LAPolyRef		*m_pPolyRefs;		// 0x2c
	uint16			m_nPolies;			// 0x30
	uint8			m_Pad32[0x34 - 0x32];
	uint32			m_iFrames[2];		// 0x34
	uint32			m_PercentBetween;	// 0x3c 0-255
	float			m_fBlendPercent;	// 0x40
	LTVector		m_vLightPos;		// 0x44
	LTVector		m_vLightColor;		// 0x50
	float			m_fLightRadius;		// 0x5c
};

// A named 0x38-byte entry (found by w_FindNamedEntry; type unknown).
struct MainWorldNamedEntry
{
	char			m_Name[0x38];		// 0x00 (name length unknown)
};

// A light table sample (Jupiter LTRGB).
struct LTRGB
{
	uint8			b, g, r, a;
};

// Talon light table (Jupiter world/src/light_table.h, CLightTable), 0x48 bytes. light_table.cpp.
struct CLightTable
{
					CLightTable();		// 0x00444ad0
					~CLightTable();		// 0x00444ae0

	// Sets up the table for the world box (0x00444af0).
	void			InitLightTable(const LTVector *pMin, const LTVector *pMax, float res);
	void			Reset();			// 0x00444be0
	void			FreeAll();			// 0x00444c20

	LTRGB			*m_pData;			// 0x00 m_nData entries
	uint32			m_nData;			// 0x04
	uint32			m_Dims[3];			// 0x08 grid size
	uint32			m_DimsMinus1[3];	// 0x14
	uint32			m_XSizeTimesYSize;	// 0x20
	LTVector		m_BlockSize;		// 0x24
	LTVector		m_InvBlockSize;		// 0x30
	LTVector		m_LookupStart;		// 0x3c world position of the first block
};

// An effect registered with ILTClient::AddSurfaceEffect (cm_AddSurfaceEffect 0x00425cd0).
struct SurfaceEffect
{
	void*			(*InitEffect)(SurfaceData *pSurfaceData, int argc, char **argv);	// 0x00
	void			(*UpdateEffect)(SurfaceData *pSurfaceData, void *pData);			// 0x04
	void			(*TermEffect)(void *pData);										// 0x08
	SurfaceEffect	*m_pNext;			// 0x0c
	char			m_Name[1];			// 0x10 allocated to fit
};

// An effect running on a world surface (MainWorld::m_pSurfaceEffects), 0x14 bytes.
struct SurfaceEffectInst
{
	WorldBsp		*m_pBsp;			// 0x00
	Surface			*m_pSurface;		// 0x04
	void			*m_pData;			// 0x08 what InitEffect returned
	SurfaceEffect	*m_pEffect;			// 0x0c
	SurfaceEffectInst	*m_pNext;		// 0x10
};

// vtable 0x004c70e0.
class MainWorld
{
public:
	MainWorld();														// 0x004283e0
	void			Clear();											// 0x004284f0 (called by the constructor)

	// Looks up a light anim by name (ClientLightAnimLT::FindLightAnim).
	virtual LightAnim*	FindLightAnim(const char *pName, uint32 *pIndex);	// 0x0042bcb0
	// The world model with WIF_VISBSP / WIF_PHYSICSBSP.
	virtual WorldBsp*	GetVisBSP();									// 0x0042be10
	virtual WorldBsp*	GetPhysicsBSP();								// 0x0042be40

	LTRESULT		Load(struct WorldLoadInfo *pInfo);					// 0x004285c0
	void			Term();												// 0x0042b1c0
	void			ClearWorldData();									// 0x0042b3b0
	LTBOOL			SetupSkyPolies();									// 0x0042b3d0
	void			InsertStaticLights(LTLink *pListHead);				// 0x0042b4f0
	LTBOOL			InitWorldModel(WorldModelInstance *pInstance, const char *pName);	// 0x0042b520
	LTRESULT		LoadObjects(ILTStream *pStream);					// 0x0042b6e0
	// Shares another world's data (the client inherits the local server's world).
	LTBOOL			InheritFrom(MainWorld *pWorld);						// 0x0042aeb0
	void			TermLightAnims();									// 0x0042bbf0

	// Flags the polies a light anim touches for relighting.
	void			UpdateLightAnimPolies(LightAnim *pAnim);			// 0x0042bd00
	LTBOOL			SetupLeafPolies();									// 0x0042bd50
	void			CalcBoundingSpheres();								// 0x0042be70

	uint32			NumWorldModels()	{ return m_WorldModels.GetSize(); }

	CMoArray<LightAnim>		m_LightAnims;			// 0x004 (vtable 0x004c6bdc)
	CMoArray<LAPolyRef>		m_LightAnimPolyRefs;	// 0x018 (vtable 0x004c6c0c) LightAnim::m_pPolyRefs
	CMoArray<LAPolyFrame*>	m_LightAnimFrames;		// 0x02c (vtable 0x004c6c3c) LightAnim::m_pFrames
	CMoArray<uint8>			m_LightAnimData;		// 0x040 (vtable 0x004c6b58) lightmaps and vertex colours
	CMoArray<LAPolyFrame>	m_LightAnimPolyFrames;	// 0x054 (vtable 0x004c6c6c)
	uint32			m_RenderDataPos;	// 0x068 file position of the light anims (LoadObjects)
	WorldTree		m_WorldTree;		// 0x06c objects in the world (Jupiter: world_tree / ClientTree())
	LTLink			m_StaticLights;		// 0x0e8 StaticLight objects (AddStaticLights)
	SurfaceEffectInst	*m_pSurfaceEffects;	// 0x0f4
	float			m_LMGridSize;		// 0x0f8 lightmap grid spacing (read after the info string)
	CLightTable		m_LightTable;		// 0x0fc
	LTVector		m_ExtentsMin;		// 0x144 world box (impl_common compressed positions)
	LTVector		m_ExtentsMax;		// 0x150
	LTVector		m_ExtentsDiffInv;	// 0x15c 1 / (max - min)
	LTVector		m_BoxMin;			// 0x168 box enclosing everything, + 100 (Jupiter: box_min_padded)
	LTVector		m_BoxMax;			// 0x174
	uint32			m_WorldFlags;		// 0x180 WORLD_
	ILTStream		*m_pWorldStream;	// 0x184 the open world file (objects load from it later)
	CMoArray<WorldData*>	m_WorldModels;	// 0x188 (vtable 0x004c6c9c) indexed by HPOLY >> 16
	CMoArray<WorldPoly*>	m_SkyPolies;	// 0x19c (vtable 0x004c6ccc) polies with SURF_SKY
	MainWorldNamedEntry	*m_NamedEntries;	// 0x1b0
	uint32			m_nNamedEntries;	// 0x1b4
	char			*m_pWorldInfoString;	// 0x1b8
	LTLink			m_Link1BC;			// 0x1bc (list head, use unknown)
	uint32			m_Unknown1C8;		// 0x1c8
	LTBOOL			m_bLoaded;			// 0x1cc TRUE while a world is loaded
	LTLink			m_InheritedWorlds;	// 0x1d0 worlds that share our data (Term terms them too)
	LTLink			m_InheritLink;		// 0x1dc our link in the m_InheritedWorlds of the world we inherited from
	LTBOOL			m_bInherited;		// 0x1e8 our data belongs to another world (InheritFrom)
	uint32			m_FileVersion;		// 0x1ec world file version (70)
};

#define MW_CHECKOFFSET(member, ofs) \
	typedef char MW_Check##member[(offsetof(MainWorld, member) == (ofs)) ? 1 : -1];
MW_CHECKOFFSET(m_LightAnims, 0x4)
MW_CHECKOFFSET(m_LightAnimPolyFrames, 0x54)
MW_CHECKOFFSET(m_WorldTree, 0x6c)
MW_CHECKOFFSET(m_StaticLights, 0xe8)
MW_CHECKOFFSET(m_LightTable, 0xfc)
MW_CHECKOFFSET(m_ExtentsMin, 0x144)
MW_CHECKOFFSET(m_BoxMin, 0x168)
MW_CHECKOFFSET(m_WorldFlags, 0x180)
MW_CHECKOFFSET(m_WorldModels, 0x188)
MW_CHECKOFFSET(m_SkyPolies, 0x19c)
MW_CHECKOFFSET(m_NamedEntries, 0x1b0)
MW_CHECKOFFSET(m_bLoaded, 0x1cc)
MW_CHECKOFFSET(m_bInherited, 0x1e8)
typedef char MW_CheckSize[(sizeof(MainWorld) == 0x1f0) ? 1 : -1];
typedef char LA_CheckSize[(sizeof(LightAnim) == 0x60) ? 1 : -1];

// Reads the world file header. Returns FALSE if the version isn't CURRENT_WORLD_VERSION.
LTBOOL w_ReadWorldHeader(ILTStream *pStream, uint32 &version, uint32 &objectDataPos, uint32 &renderDataPos);	// 0x00427cf0
LTRESULT w_GetWorldInfoString(ILTStream *pStream, char *pInfoString, uint32 maxLen, uint32 *pActualLen);	// 0x00427db0
void w_TermSurfaceEffects(MainWorld *pWorld);											// 0x00427e40
WorldData* w_FindWorldModel(MainWorld *pWorld, const char *pName);						// 0x00427e80

void w_TransformWorldModel(WorldModelInstance *pInst, LTMatrix *pMat, LTBOOL bPartial);	// 0x00427ed0

BspPortal* w_FindBspPortal(WorldBsp *pBsp, const char *pName, uint32 *pIndex);			// 0x00428180
BspPortal* w_FindPortal(MainWorld *pWorld, const char *pName, uint32 *pWorldIndex, uint32 *pPortalIndex);	// 0x00428200
MainWorldNamedEntry* w_FindNamedEntry(MainWorld *pWorld, const char *pName, uint32 *pIndex);	// 0x00428260
LTBOOL w_MakeSpecialName(const char *pName, int id, char *pBuf, uint32 bufLen);			// 0x004282d0

#endif  // __DE_MAINWORLD_H__
