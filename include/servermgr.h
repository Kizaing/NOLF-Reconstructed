// Talon server manager (Jupiter runtime/server/src/servermgr.h). One layout for the whole
// engine: add members here as they're recovered; never redeclare CServerMgr elsewhere.
#ifndef __SERVERMGR_H__
#define __SERVERMGR_H__

#include <stddef.h>
#include "bdefs.h"
#include "de_objects.h"
#include "classmgr.h"
#include "iltsoundmgr.h"
#include "server_filemgr.h"
#include "netmgr.h"
#include "world_tree.h"
#include "de_mainworld.h"
#include "objectmgr.h"
#include "ltdynarray.h"
#include "../../build/proj/LT2/lithshared/stdlith/stringholder.h"
#include "../../build/proj/LT2/lithshared/stdlith/object_bank.h"

// A file the clients should cache (4 bytes).
struct OtherFile
{
	uint16		m_FileType;		// 0x00 A FT_ define (from de_codes.h).
	uint16		m_FileID;		// 0x02
};

// Object/ID map entry (8 bytes).
struct ObjectMapEntry
{
	uint8		m_nRecordType;	// 0x00 RECORDTYPE_
	void		*m_pRecordData;	// 0x04
};

#define INVALID_OBJECTID		0xFFFF
#define MAX_ERRORSTRING_LEN		300

#define IDFLAG_BLOCKEDID	(1<<30)			// This is a blocked ID (shouldn't be used from the free list).
#define IDFLAG_MASK			IDFLAG_BLOCKEDID	// All the ID flags.

inline uint32 GetLinkID(LTLink *pLink)				{ return (uint32)pLink->m_pData; }
inline void SetLinkID(LTLink *pLink, uint32 id)		{ pLink->m_pData = (void*)id; }

// CServerMgr::m_InternalFlags.
#define SIFLAG_REMOVINGALLOBJECTS	(1<<0)
#define OBJECTCREATED_NORMAL	0

#define RECORDTYPE_OBJECT	1
#define RECORDTYPE_SOUND	2

// Server-side client (Jupiter s_client.h).
// Client flags (Client::m_ClientFlags).
#define CFLAG_LOCAL					(1<<1)
#define CFLAG_FULLRES				(1<<2)
#define CFLAG_SENDCOBJROTATION		(1<<3)
#define CFLAG_FORCENEXTUPDATE		(1<<4)
#define CFLAG_VIRTUAL				(1<<6)
#define CFLAG_AUTOACTIVATEOBJECTS	(1<<9)

#define MAX_CLIENT_COMMANDS	255

// Client::m_State.
#define CLIENT_INWORLD	3

// What a client knows about an object ID (4 bytes).
struct ObjInfo
{
	uint16		m_ChangeFlags;		// 0x00
	uint8		m_nSoundFlags;		// 0x02
};

// A client's outgoing packet buffer (0xc bytes).
struct ClientPacketBuf
{
	class CPacket	*m_pPacket;		// 0x00
	uint32		m_Unknown4;			// 0x04
	uint32		m_Unknown8;			// 0x08
};

struct Client
{
	LTLink		m_Link;				// 0x00 in CServerMgr::m_Clients
	uint8		m_Pad0C[0x110 - 0xc];
	void		*m_pClientData;		// 0x110 data the client sent when it connected
	uint32		m_ClientDataLen;	// 0x114
	uint8		m_Pad118[0x134 - 0x118];
	struct ClientPacketBuf	m_PacketBufs[2][2];	// 0x134 outgoing packets (reset by DoEndWorld)
	uint8		m_Pad164[0x174 - 0x164];
	LTVector	m_ViewPos;			// 0x174
	uint8		m_Pad180[0x18c - 0x180];
	struct FTServ	*m_hFTServ;		// 0x18c file transfer server
	struct ObjInfo	*m_ObjInfos;	// 0x190 per object ID (CServerMgr::m_nObjInfos)
	uint8		m_Pad194[0x1a8 - 0x194];
	uint8		m_Commands[2][255];	// 0x1a8 command on/off, double-buffered by m_iCurCommands
	uint8		m_Pad3A6[0x3a8 - 0x3a6];
	uint32		m_iCurCommands;		// 0x3a8 which m_Commands row is current
	LTObject	*m_pObject;			// 0x3ac the client's object
	void		*m_pPluginUserData;	// 0x3b0 SetClientUserData
	uint16		m_ClientID;			// 0x3b4
	uint8		m_Pad3B6[0x3b8 - 0x3b6];
	int			m_State;			// 0x3b8 CLIENT_
	uint8		m_Pad3BC[0x3c0 - 0x3bc];
	uint32		m_ClientFlags;		// 0x3c0 CFLAG_
	char		*m_Name;			// 0x3c4
	CBaseConn	*m_ConnectionID;	// 0x3c8
};

// The server's ILTSoundMgr implementation (embedded in CServerMgr at 0x04; lives in another unit).
// Remembers a client's object across a world change (Jupiter s_client.h).
struct ClientRef
{
	LTLink		m_Link;				// 0x00 in CServerMgr::m_ClientReferences
	uint8		m_ClientFlags;		// 0x0c CFLAG_
	uint8		m_Pad0D[0x10 - 0xd];
	uint16		m_ObjectID;			// 0x10
	char		m_ClientName[1];	// 0x12 (variable length)
};

class CServerSoundMgr : public ILTSoundMgr
{
public:
	virtual LTRESULT	PlaySound(PlaySoundInfo *pPlaySoundInfo, HLTSOUND &hResult);
	virtual LTRESULT	GetSoundDuration(HLTSOUND hSound, LTFLOAT &fDuration);
	virtual LTRESULT	IsSoundDone(HLTSOUND hSound, LTBOOL &bDone);
	virtual LTRESULT	KillSound(HLTSOUND hSound);
	virtual LTRESULT	KillSoundLoop(HLTSOUND hSound);
	virtual LTRESULT	KillSoundFade(HLTSOUND hSound, LTFLOAT fFadeOutTime);
};

class CSoundData;
struct UsedFile;
class LThreadMessage;

class CServerMgr
{
public:
	ClassBindModule*	GetClassModule()	{ return m_ClassMgr.m_ClassModule; }

	LTRESULT	DoStartWorld(char *pWorldName, uint32 flags, float curTime);
	LTRESULT	DoRunWorld();				// 0x00485020
	LTRESULT	CreateStringCRC();			// 0x00483a00
	LTRESULT	CreateWorldCRC();			// 0x00483c60
	LTRESULT	FreeUnusedModels();			// 0x004855b0
	LPBASECLASS	EZCreateObject(CClassData *pClass, ObjectCreateStruct *pStruct);	// 0x00483850
	void		SetGlobalLightObject(HOBJECT hObj);	// 0x00486fd0
	class CPacket*	AllocPacket();				// 0x00486f60
	void		SetupPacketMessage(class CPacket *pPacket);	// 0x00486fc0

	LTBOOL		Init();						// 0x004823b0
	void		Term();						// 0x004827e0
	LTBOOL		Listen(char *pDriverInfo, char *pListenInfo);	// 0x00482b40
	LTBOOL		TransferNetDriver(CBaseDriver *pDriver);		// 0x00482b90
	LTBOOL		AddResources(char **pResources, uint32 nResources);	// 0x00482cf0
	LTBOOL		LoadBinaries();				// 0x00482d80
	void		SetGameInfo(void *pData, uint32 dataLen);	// 0x00482da0
	void		UpdateObjects();			// 0x00482e50
	void		UpdateSounds(float fDeltaTime);	// 0x00483370
	void		RemoveSounds();				// 0x004833b0
	void		OnLoaderMessage(LThreadMessage &msg);	// 0x00483460
	void		ProcessLoaderMessages();	// 0x004834d0
	LTBOOL		Update(int32 updateFlags, float curTime);	// 0x00483520
	void		SetupGlobals();				// 0x00483810
	void		GetErrorString(char *pStr, int maxLen);	// 0x00483830
	LTRESULT	LoadWorld(ILTStream *pStream, char *pWorldName);	// 0x00483920
	void		ResizeUpdateInfos(uint32 nAllocatedIDs);	// 0x00483d00
	void		DoEndWorld(LTBOOL bKeepGeometryAround);	// 0x004850b0
	void		ProcessClientCommands(Client *pClient, uint8 *pCommands, int nCommands);	// 0x00485990
	void		ClearChildModelLinks();			// 0x00484d70 (STLport)
	LTBOOL		InitWorldObjects();				// 0x004856e0 (CreateVisContainerObjects)
	CBaseDriver*	GetLocalDriver();			// 0x004859f0
	CSoundData*	FindSoundData(UsedFile *pFile);	// 0x004860a0
	CSoundData*	GetSoundData(UsedFile *pFile);	// 0x004860d0
	void		UntouchAllSoundData();			// 0x00486590

	uint8		m_Pad0[0x4];				// 0x00 vtable (CNetHandler)
	CServerSoundMgr	m_SoundMgr;			// 0x04 the server's ILTSoundMgr
	StructBank	m_SoundDataBank;		// 0x0c CSoundData
	StructBank	m_SoundTrackBank;		// 0x28 CSoundTrack
	LTList		m_SoundDataList;		// 0x44
	LTList		m_SoundTrackList;		// 0x54
	int32		m_LastErrorCode;		// 0x64
	char		m_ErrorString[MAX_ERRORSTRING_LEN+1];	// 0x68
	uint32		m_State;				// 0x198 SERV_
	uint32		m_ServerFlags;			// 0x19c SFLAG_
	uint32		m_InternalFlags;		// 0x1a0 SIFLAG_
	MainWorld	m_World;				// 0x1a4 (de_mainworld.h)
	ObjectMgr	m_ObjectMgr;			// 0x394 (its m_ObjectLists are at 0x5b4)
	LTBOOL		m_bTrackChanges;		// 0x664 changed objects go on m_pChangeListHead
	struct UsedFile	*m_pWorldFile;		// 0x668
	char		m_CRCString[0x400];		// 0x66c
	uint32		m_StringCRC;			// 0xa6c
	uint32		m_WorldCRC;				// 0xa70
	uint8		m_PadA74[0xa88 - 0xa74];
	LTVector	m_GlobalForce;			// 0xa88 (start of the MotionInfo?)
	uint8		m_PadA94[0xab4 - 0xa94];
	class SMoveAbstract	*m_MoveAbstract;	// 0xab4
	CollisionInfo	*m_pCollisionInfo;	// 0xab8 only valid during touch notifies
	float		m_FrameTime;			// 0xabc
	float		m_LastServerFPS;		// 0xac0 Used to detect changes in server FPS.
	float		m_TimeOffset;			// 0xac4 added to the time passed to Update
	float		m_TargetTimeBase;		// 0xac8
	float		m_TargetTime;			// 0xacc
	uint32		m_nTargetTimeSteps;		// 0xad0
	float		m_LastTargetTimeBase;	// 0xad4
	float		m_GameTime;				// 0xad8
	uint32		m_FrameCode;			// 0xadc
	float		m_TrueFrameTime;		// 0xae0
	float		m_LastTime;				// 0xae4 passed to DoStartWorld by si_LoadWorld
	uint32		m_nFramesToSkip;		// 0xae8 debug: frames left before stopping
	CNetMgr		m_NetMgr;				// 0xaec
	uint8		m_PadNetMgr[0xbf4 - 0xaec - sizeof(CNetMgr)];
	LTList		m_Clients;				// 0xbf4
	uint32		m_UpdatesSize;			// 0xc04
	uint32		m_nUpdatesSent;			// 0xc08
	uint32		m_nSendPackets;			// 0xc0c
	uint32		m_nDroppedSendPackets;	// 0xc10
	class CServerSerializeHelper	*m_pSerializeHelper;	// 0xc14
	ILTStream	*m_pTracePacketFile;	// 0xc18
	CClassMgr	m_ClassMgr;				// 0xc1c
	LTLink		m_RemovedObjectHead;	// 0xc4c objects to remove at the end of the frame
	StructBank	m_InterLinkBank;		// 0xc58 InterLinks
	StructBank	m_ClientStructNodeBank;	// 0xc74
	StructBank	m_FileIDInfoBank;		// 0xc90 (0xc0-byte structs)
	StructBank	m_BankCAC;				// 0xcac (10-byte structs)
	uint32		m_nObjInfos;			// 0xcc8
	uint32		m_nAllocatedIDs;		// 0xccc
	HHashTable	*m_hNameTable;			// 0xcd0 object names
	LTLink		m_FreeIDs;				// 0xcd4 ID free list.
	LTLink		m_IDs;					// 0xce0 Allocated ID list (m_pData = ID).
	LTList		m_Objects;				// 0xcec All the objects.
	LTList		m_ClientReferences;		// 0xcfc
	CMoArray<ObjectMapEntry>	m_ObjectMap;	// 0xd0c (indexed by object ID)
	LTObject	*m_pChangeListHead;		// 0xd20 linked through sd->m_pChangeNext
	class CSoundTrack		*m_ChangedSoundTrackHead;	// 0xd24
	HHashTable	*m_hModelTable;			// 0xd28 cached models by filename
	struct OtherFile	*m_CacheList;	// 0xd2c
	uint32		m_CacheListSize;		// 0xd30 How many elements are used.
	uint32		m_CacheListAllocedSize;	// 0xd34 How many elements are allocated.
	ILTServer	*m_pServerInterface;	// 0xd38 (handed to object.lto)
	SkyDef		m_SkyDef;				// 0xd3c
	uint16		m_SkyObjects[MAX_SKYOBJECTS];	// 0xd6c object IDs (INVALID_OBJECTID if unused)
	LTObject	*m_pGlobalLightObject;	// 0xda8
	StructBank	m_ObjectListBank;		// 0xdac ObjectLists
	StructBank	m_ObjectLinkBank;		// 0xdc8 ObjectLinks
	StructBank	m_ServerEventBank;		// 0xde4
	ObjectBank<ServerData>	m_SObjBank;	// 0xe00 ServerDatas
	CStringHolder	m_StringHolder;		// 0xe24
	ServerFileMgr	m_FileMgr;			// 0xe3c server file manager (sf_ functions)
	struct PropEntry	*m_pCurProps;	// 0xe78 properties of the object being created
	void		*m_pGameInfo;			// 0xe7c
	uint32		m_GameInfoLen;			// 0xe80
	ConsoleState	m_ConsoleState;		// 0xe84
	uint8		m_PadEC8[0xed0 - 0xec8];
	class ServerAppHandler	*m_pServerAppHandler;	// 0xed0
	uint8		m_LoaderThread[0x64];	// 0xed4 CServerLoaderThread (sloaderthread.h; kept opaque so
										// servermgr.h doesn't pull in windows.h)
	class Model	*m_pDefaultModel;		// 0xf38 stands in for models that fail to load
};

// Layout checks (a wrong pad breaks every user of CServerMgr).
#define SM_CHECKOFFSET(member, ofs) 	typedef char SM_CHECK_##member[(offsetof(CServerMgr, member) == (ofs)) ? 1 : -1];
SM_CHECKOFFSET(m_SoundDataBank, 0xc)
SM_CHECKOFFSET(m_InternalFlags, 0x1a0)
SM_CHECKOFFSET(m_World, 0x1a4)
SM_CHECKOFFSET(m_ObjectMgr, 0x394)
SM_CHECKOFFSET(m_bTrackChanges, 0x664)
SM_CHECKOFFSET(m_MoveAbstract, 0xab4)
SM_CHECKOFFSET(m_NetMgr, 0xaec)
SM_CHECKOFFSET(m_Clients, 0xbf4)
SM_CHECKOFFSET(m_ClassMgr, 0xc1c)
SM_CHECKOFFSET(m_ObjectMap, 0xd0c)
SM_CHECKOFFSET(m_pServerInterface, 0xd38)
SM_CHECKOFFSET(m_SObjBank, 0xe00)
SM_CHECKOFFSET(m_FileMgr, 0xe3c)
SM_CHECKOFFSET(m_ConsoleState, 0xe84)
SM_CHECKOFFSET(m_LoaderThread, 0xed4)
SM_CHECKOFFSET(m_pDefaultModel, 0xf38)

// GLOBAL: LITHTECH 0x004e5dc8
extern CServerMgr *g_pServerMgr;

// GLOBAL: LITHTECH 0x004def70
extern ObjectBank<LTLink> g_DLinkBank;

#endif  // __SERVERMGR_H__
