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

// Talon light animation (0x60 bytes, world light anims driven by ILTLightAnim).
struct LightAnim
{
	uint8			m_Pad00[0x20];
	LTBOOL			m_bShadowMap;		// 0x20 LAInfo::m_bShadowMap (impl_common la_GetInfo)
	uint8			m_Pad24[0x28 - 0x24];
	uint8			m_nFrames;			// 0x28
	uint8			m_Pad29[0x34 - 0x29];
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

// Talon light table (Jupiter world/src/light_table.h, CLightTable), 0x48 bytes. Constructor 0x00444ad0.
struct CLightTable
{
	uint8			m_Pad00[0x48];
};

// vtable 0x004c70e0.
class MainWorld
{
public:
	MainWorld();														// 0x004283e0
	void			Clear();											// 0x004284f0 (called by the constructor)

	// Slot 0: looks up a light anim by name (ClientLightAnimLT::FindLightAnim).
	virtual LTBOOL	FindLightAnim(const char *pName, uint32 *pLightAnim);

	// 0x0042bd00: flags the polies a light anim touches for relighting.
	void			UpdateLightAnimPolies(LightAnim *pAnim);

	LTRESULT		Load(struct WorldLoadInfo *pInfo);					// 0x004285c0
	void			Term();												// 0x0042b1c0
	void			ClearWorldData();									// 0x0042b3b0
	LTBOOL			InitWorldModel(WorldModelInstance *pInstance, const char *pName);	// 0x0042b520
	LTRESULT		LoadObjects(ILTStream *pStream);					// 0x0042b6e0

	uint32			NumWorldModels()	{ return m_WorldModels.GetSize(); }

	CMoArray<LightAnim>	m_LightAnims;		// 0x004 (vtable 0x004c6bdc)
	uint8			m_Array18[0x14];		// 0x018 CMoArray, element type unknown (vtable 0x004c6c0c)
	uint8			m_Array2C[0x14];		// 0x02c CMoArray, element type unknown (vtable 0x004c6c3c)
	uint8			m_Array40[0x14];		// 0x040 CMoArray<uint8> (vtable 0x004c6b58)
	uint8			m_Array54[0x14];		// 0x054 CMoArray, element type unknown (vtable 0x004c6c6c)
	uint8			m_Pad068[0x6c - 0x68];
	WorldTree		m_WorldTree;		// 0x06c objects in the world (Jupiter: world_tree / ClientTree())
	uint8			m_PadWT[0xfc - 0x6c - sizeof(WorldTree)];
	CLightTable		m_LightTable;		// 0x0fc
	LTVector		m_ExtentsMin;		// 0x144 world box (impl_common compressed positions)
	LTVector		m_ExtentsMax;		// 0x150
	LTVector		m_ExtentsDiffInv;	// 0x15c 1 / (max - min)
	LTVector		m_BoxMin;			// 0x168 box enclosing everything, + 100 (Jupiter: box_min_padded)
	LTVector		m_BoxMax;			// 0x174
	uint32			m_WorldFlags;		// 0x180 WORLD_
	ILTStream		*m_pWorldStream;	// 0x184 the open world file (objects load from it later)
	CMoArray<WorldData*>	m_WorldModels;	// 0x188 (vtable 0x004c6c9c) indexed by HPOLY >> 16
	uint8			m_Array19C[0x14];		// 0x19c CMoArray, element type unknown (vtable 0x004c6ccc)
	MainWorldNamedEntry	*m_NamedEntries;	// 0x1b0
	uint32			m_nNamedEntries;	// 0x1b4
	uint8			m_Pad1B8[0x1cc - 0x1b8];
	LTBOOL			m_bLoaded;			// 0x1cc TRUE while a world is loaded
	uint8			m_Pad1D0[0x1f0 - 0x1d0];
};

#define MW_CHECKOFFSET(member, ofs) \
	typedef char MW_Check##member[(offsetof(MainWorld, member) == (ofs)) ? 1 : -1];
MW_CHECKOFFSET(m_LightAnims, 0x4)
MW_CHECKOFFSET(m_WorldTree, 0x6c)
MW_CHECKOFFSET(m_LightTable, 0xfc)
MW_CHECKOFFSET(m_ExtentsMin, 0x144)
MW_CHECKOFFSET(m_BoxMin, 0x168)
MW_CHECKOFFSET(m_WorldFlags, 0x180)
MW_CHECKOFFSET(m_WorldModels, 0x188)
MW_CHECKOFFSET(m_NamedEntries, 0x1b0)
MW_CHECKOFFSET(m_bLoaded, 0x1cc)
typedef char MW_CheckSize[(sizeof(MainWorld) == 0x1f0) ? 1 : -1];

void w_TransformWorldModel(WorldModelInstance *pInst, LTMatrix *pMat, LTBOOL bPartial);	// 0x00427ed0

BspPortal* w_FindBspPortal(WorldBsp *pBsp, const char *pName, uint32 *pIndex);			// 0x00428180
BspPortal* w_FindPortal(MainWorld *pWorld, const char *pName, uint32 *pWorldIndex, uint32 *pPortalIndex);	// 0x00428200
MainWorldNamedEntry* w_FindNamedEntry(MainWorld *pWorld, const char *pName, uint32 *pIndex);	// 0x00428260
LTBOOL w_MakeSpecialName(const char *pName, int id, char *pBuf, uint32 bufLen);			// 0x004282d0

#endif  // __DE_MAINWORLD_H__
