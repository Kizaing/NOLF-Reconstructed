// Talon client manager (Jupiter runtime/client/src/clientmgr.h). Only recovered members.
// One definition for the engine: add members here at their exact offsets.
// Talon passes the manager explicitly (cm_ functions take it as the first argument), while the
// ci_ interface functions reach it through g_pClientMgr.
#ifndef __CLIENTMGR_H__
#define __CLIENTMGR_H__

#include <stddef.h>
#include "ltbasedefs.h"
#include "motion.h"
#include "netmgr.h"
#include "concommand.h"
#include "world_tree.h"
#include "objectmgr.h"
#include "ratetracker.h"
#include "de_world.h"
#include "de_mainworld.h"

// Talon client file manager handle (client_filemgr).
struct ClientFileMgr;
class FileRef;
class CClientShell;
class VideoMgr;
class MoveAbstract;
class ILTCursor;
class ILTDirectMusicMgr;
class ModelInstance;
struct LightAnim;
struct WorldData;

// 0x00404820 (client_filemgr.h; Ghidra: CPacket_Data::Free). Jupiter: IClientFileMgr::OpenFile.
ILTStream* cf_OpenFile(ClientFileMgr *hFileMgr, FileRef *pRef);


// The client manager starts with its world (MainWorld, de_mainworld.h): cm_Init (0x004112c0)
// constructs a MainWorld at offset 0 and the ObjectMgr at 0x1f0, just as CServerMgr does at 0x1a4/0x394.
class CClientMgr
{
public:
	// 0x00425d30. Variadic: the arguments format the error's message.
	LTRESULT		SetupError(LTRESULT theError, ...);
	void			ForwardMessagesToScript();							// 0x00425e00
	void			ForwardCommandChanges(int32 *pChanges, int32 nChanges);	// 0x00425ea0
	void			UpdateFrameRate();									// 0x00425f00

	// clientmgr.cpp, used by render.cpp.
	LTBOOL			BindClientShellWorlds();							// 0x00410c00
	void			UnbindClientShellWorlds();							// 0x00410c20
	void			BindSharedTextures();								// 0x00410c30
	void			UnbindSharedTextures();								// 0x00410c80
	void			InitConsole();										// 0x00410cb0
	void			TermConsole();										// 0x00410d00
	uint16			IncCurTextureFrameCode();							// 0x00411cc0

	// 0x00411c50: a fresh packet for a message.
	class CPacket*	AllocPacket();

	// 0x00411d10.
	LTRESULT		FreeUnusedModels();

	// clientmgr.cpp, used by the ci_ interface functions.
	LTRESULT		StartShell(StartGameRequest *pRequest);			// 0x00410500
	LTRESULT		AppInitMusic(char *pMusicDLL);					// 0x00410a70
	LTRESULT		ClearInput();									// 0x00411150
	void			ShowDrawSurface(uint32 flags);					// 0x00411180

	MainWorld		m_World;			// 0x0000
	ObjectMgr		m_ObjectMgr;		// 0x01f0 (its m_ObjectLists are at 0x410)
	uint8			m_Pad04c0[0x4c4 - 0x4c0];
	class ILTClient	*m_pClientDE;		// 0x04c4 handed to the client shell's create function
	MotionState		m_MotionState;		// 0x04c8
	uint8			m_Pad0508[0x50c - 0x508];
	MoveAbstract	*m_MoveAbstract;	// 0x050c
	CNetMgr			m_NetMgr;			// 0x0510 (0x104 bytes)
	class CBindModuleType	*m_hClientResourceModule;			// 0x0614 cres.dll
	class CBindModuleType	*m_hLocalizedClientResourceModule;	// 0x0618 cresl.dll
	struct ShellBindModule	*m_hShellModule;					// 0x061c cshell.dll
	class IClientShell		*m_pClientShell;					// 0x0620
	char			m_ErrorString[301];	// 0x0624 (SetupError)
	uint8			m_Pad0751[0x76c - 0x751];
	LTVector		m_GlobalLightScale;	// 0x076c (values 0-2)
	LTVector		m_GlobalVertexTint;	// 0x0778 (values 0-1)
	uint8			m_SoundMgr[0x4];	// 0x0784 client ILTSoundMgr implementation (type unknown)
	uint8			m_Pad0788[0x1200 - 0x788];
	LTList			m_TextureUsers;		// 0x1200 objects whose +0xc0 is a SharedTexture* (cm_TagUsedTextures; type unknown)
	LTList			m_Sprites;			// 0x1210
	LTList			m_SharedTextures;	// 0x1220
	uint8			m_Pad1230[0x124c - 0x1230];
	ObjectBank<SharedTexture>	m_SharedTextureBank;	// 0x124c
	uint8			m_Pad1270[0x12b0 - 0x1270];
	SkyDef			m_SkyDef;			// 0x12b0
	uint8			m_Pad12e0[0x12ec - 0x12e0];
	struct ObjectMapEntry	*m_ObjectMap;	// 0x12ec (indexed by object ID; servermgr.h)
	uint32			m_ObjectMapSize;	// 0x12f0
	RateTracker		m_FramerateTracker;	// 0x12f4
	uint8			m_Pad1300[0x1368 - 0x1300];
	float			m_FrameTime;		// 0x1368
	float			m_CurTime;			// 0x136c (pd_InitialServerUpdate)
	uint8			m_Pad1370[0x1378 - 0x1370];
	ConsoleState	m_ServerConsoleMirror;	// 0x1378
	uint8			m_Commands[2][255];	// 0x13bc command states per input slot
	uint8			m_Pad15ba[0x15bc - 0x15ba];
	int				m_iCurInputSlot;	// 0x15bc
	uint8			m_Pad15c0[0x160c - 0x15c0];
	float			m_AxisOffsets[3];	// 0x160c
	class InputMgr	*m_InputMgr;		// 0x1618 (input.h)
	CClientShell	*m_pCurShell;		// 0x161c
	uint8			m_Pad1620[0x162c - 0x1620];
	LTBOOL			m_bInputState;		// 0x162c FALSE tells the server to ignore our input.
	LTBOOL			m_bTrackingInputDevices;	// 0x1630
	ModelHookFn		m_ModelHookFn;		// 0x1634
	void			*m_ModelHookUser;	// 0x1638
	uint8			m_Pad163c[0x1644 - 0x163c];
	ClientFileMgr	*m_hFileMgr;		// 0x1644
	uint8			m_Pad1648[0x1720 - 0x1648];
	VideoMgr		*m_pVideoMgr;		// 0x1720
	ILTCursor		*m_pCursorMgr;		// 0x1724
	uint8			m_Pad1728[0x22c0 - 0x1728];
	ILTDirectMusicMgr	*m_pDirectMusicMgr;	// 0x22c0
};

// Layout checks: a member that moves breaks the build here instead of silently shifting offsets.
#define CM_CHECKOFFSET(member, ofs) \
	typedef char CM_Check##member[(offsetof(CClientMgr, member) == (ofs)) ? 1 : -1];
CM_CHECKOFFSET(m_World, 0x0)
CM_CHECKOFFSET(m_MotionState, 0x4c8)
CM_CHECKOFFSET(m_NetMgr, 0x510)
CM_CHECKOFFSET(m_hClientResourceModule, 0x614)
CM_CHECKOFFSET(m_SoundMgr, 0x784)
CM_CHECKOFFSET(m_ServerConsoleMirror, 0x1378)
CM_CHECKOFFSET(m_ObjectMgr, 0x1f0)
CM_CHECKOFFSET(m_ErrorString, 0x624)
CM_CHECKOFFSET(m_SharedTextureBank, 0x124c)
CM_CHECKOFFSET(m_FramerateTracker, 0x12f4)
CM_CHECKOFFSET(m_iCurInputSlot, 0x15bc)
CM_CHECKOFFSET(m_pCurShell, 0x161c)
CM_CHECKOFFSET(m_hFileMgr, 0x1644)
CM_CHECKOFFSET(m_pVideoMgr, 0x1720)
CM_CHECKOFFSET(m_pDirectMusicMgr, 0x22c0)
CM_CHECKOFFSET(m_GlobalLightScale, 0x76c)
CM_CHECKOFFSET(m_SkyDef, 0x12b0)
CM_CHECKOFFSET(m_AxisOffsets, 0x160c)
CM_CHECKOFFSET(m_bInputState, 0x162c)
CM_CHECKOFFSET(m_ModelHookFn, 0x1634)

// GLOBAL: LITHTECH 0x004defac
extern CClientMgr *g_pClientMgr;

// ------------------------------------------------------------------ //
// cutil.cpp (cm_ functions take the manager).
// ------------------------------------------------------------------ //

// 0x00426750
void cm_UpdateModelDims(CClientMgr *pClientMgr, ModelInstance *pInstance);
// 0x00426860
void cm_MoveObject(CClientMgr *pClientMgr, LTObject *pObject, LTVector *pNewPos, LTBOOL bForce);
// 0x00426520 (Ghidra: CClientMgr::AddToObjectMap). Grows m_ObjectMap to hold id.
void cm_AddToObjectMap(CClientMgr *pClientMgr, uint16 id);
// 0x004265b0 (Ghidra: so_ExtraTerm). Clears an m_ObjectMap entry.
void cm_ClearObjectMapEntry(CClientMgr *pClientMgr, uint16 id);

#endif  // __CLIENTMGR_H__
