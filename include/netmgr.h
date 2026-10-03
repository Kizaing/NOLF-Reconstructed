// Talon network manager (kernel/net/netmgr.h). Talon's CNetMgr still does its own guaranteed
// delivery (frame numbers, acks/NAKs, fragmenting) on top of the drivers; Jupiter moved that
// into the UDP driver. Layouts recovered from lithtech.exe (netmgr.cpp 004627d0-00466530).
#ifndef __NETMGR_H__
#define __NETMGR_H__

#include "ltbasedefs.h"
#include "ratetracker.h"
#include "counter.h"
#include "packet.h"
#include "ltdynarray.h"
#include "../../build/proj/LT2/lithshared/stdlith/goodlinklist.h"
#include "../../build/proj/LT2/lithshared/stdlith/object_bank.h"

#define INVALID_CONNID	NULL

class CBaseDriver;
class CBaseConn;
class CNetMgr;

// CNetMgr flags.
#define NETMGR_GETTINGPACKETS		1

// Connection flags.
#define CONNFLAG_LOCAL				(1<<0)
#define CONNFLAG_CRC				(1<<1)	// Packets carry a 2 byte CRC.
#define CONNFLAG_FORCEDISCONNECT	(1<<2)	// Disconnected on the next update.

// Packet ID byte: the low 6 bits are the ID, the top bits are netmgr flags.
#define PACKETID_MASK				0x3f
#define PACKETFLAG_GUARANTEED		0x80	// Has a 4 byte frame number at the end.
#define PACKETFLAG_SEND				0x40

// The netmgr's own packet ID and its subtypes.
#define NETMGR_PACKETID				1
#define NMPACKET_ACK				0	// Ack, NAKs and maybe a ping request.
#define NMPACKET_DISCONNECT			1
#define NMPACKET_PINGREPLY			2
#define NMPACKET_GROUP				3	// Several packets in one.
#define NMPACKET_FRAGMENT			4	// One piece of a big guaranteed packet.

#define FRAGMENT_INDEXFLAG			0x30	// | fragment index

// Max NAKs in one ack.
#define MAX_NAKS					8

// Disconnect reasons.
#define DISCONNECTREASON_LOCALDRIVER	1
#define DISCONNECTREASON_FORCED			2
#define DISCONNECTREASON_DEAD			3

// Guaranteed packet buffer, kept on sorted free lists by the netmgr (0x20 bytes).
class GPacket : public CGLLNode
{
public:
	uint8			*m_pData;		// 0x08
	int				m_AllocSize;	// 0x0c
	uint32			m_DataLen;		// 0x10
	uint32			m_nNaks;		// 0x14 Times it was NAKed.
	uint32			m_FrameNum;		// 0x18
	uint32			m_bResend;		// 0x1c Needs (re)sending.
};
typedef CGLinkedList<GPacket*> GPacketList;


// Outgoing packet held back for latency simulation (0x70 bytes).
class LatentPacket : public CGLLNode
{
public:
	float			m_SendTimeCounter;	// 0x08
	CPacket			m_Packet;			// 0x0c
};


// The fragments of a big guaranteed packet being reassembled (0x20 bytes).
#define MAX_PACKET_FRAGMENTS	4

class CFragmentGroup : public CGLLNode
{
public:
					CFragmentGroup()
					{
						Clear();
					}

	void			Clear()
	{
		uint32 i;

		for(i=0; i < MAX_PACKET_FRAGMENTS; i++)
			m_Fragments[i] = LTNULL;

		m_FrameNum = 0;
		m_pConn = LTNULL;
	}

	CPacketRef		m_Fragments[MAX_PACKET_FRAGMENTS];	// 0x08
	uint32			m_FrameNum;		// 0x18
	CBaseConn		*m_pConn;		// 0x1c
};


// Base data structure the internal stuff uses to represent a net connection (0xbc bytes).
class CBaseConn
{
public:
					CBaseConn();		// 00462870
					~CBaseConn();		// 004628e0

	LTBOOL			IsInTrouble();		// 00462a10

	// BPS=bytes per second, PPS=packets per second
	RateTracker		m_SendBPS;			// 0x00
	RateTracker		m_SendPPS;			// 0x0c
	RateTracker		m_RecvBPS;			// 0x18
	RateTracker		m_RecvPPS;			// 0x24

	CBaseDriver		*m_pDriver;			// 0x30 Driver this conn is connected thru.

	GPacketList		m_SendQueue;		// 0x34 Unacknowledged outgoing guaranteed packets.
	GPacketList		m_RecvQueue;		// 0x40 Guaranteed packets received out of order.
	CGLinkedList<LatentPacket*>	m_Latent;	// 0x4c Latency simulation queue.
	GPacketList		m_ReadyQueue;		// 0x58 Received packets ready to be handed out.

	uint32			m_Unknown64;		// 0x64
	uint32			m_Unknown68;		// 0x68
	uint32			m_Unknown6C;		// 0x6c

	uint32			m_ConnFlags;		// 0x70 CONNFLAG_ flags.

	uint32			m_OutgoingFrame;	// 0x74 Next guaranteed frame to send.
	uint32			m_nResent;			// 0x78
	uint32			m_IncomingFrame;	// 0x7c Next guaranteed frame expected.
	uint32			m_HighestFrame;		// 0x80 Highest frame received + 1.
	float			m_RecvWait;			// 0x84 Time since we heard from it.
	float			m_SendWait;			// 0x88 Time since we sent something.
	uint32			m_nPacketsReceived;	// 0x8c
	uint32			m_Unknown90;		// 0x90
	float			m_AckTimer;			// 0x94
	float			m_AckWait;			// 0x98 Time since we got an ack.
	float			m_Ping;				// 0x9c
	float			m_PingTimes[3];		// 0xa0 Last 3 ping times.
	Counter			m_PingCounter;		// 0xac
	float			m_PingTimer;		// 0xb4
	uint16			m_PingID;			// 0xb8
	uint16			m_PadBA;
};


// Base class for a network driver (vtable 004c7370).
class CBaseDriver
{
public:

						CBaseDriver()
						{
							m_Bandwidth = 120.0f;
							m_DriverFlags = 0;
						}

	virtual				~CBaseDriver() {}

	virtual LTBOOL		Init()=0;
	virtual void		Term()=0;

	virtual void		Update()=0;

	virtual void		LocalConnect(CBaseDriver *pOther) {}

	// Service, session and query functions (defaults live in netmgr.cpp's COMDATs).
	virtual	LTRESULT	GetServiceList(NetService* &pListHead) { return LT_ERROR; }
	virtual	LTRESULT	SelectService(HNETSERVICE hService) { return LT_ERROR; }
	virtual LTRESULT	GetSessionList(NetSession* &pListHead, char *pInfo) { return LT_ERROR; }
	virtual LTRESULT	StartQuery(char *pInfo) { return LT_ERROR; }
	virtual LTRESULT	UpdateQuery() { return LT_ERROR; }
	virtual LTRESULT	GetQueryResults(NetSession* &pListHead) { return LT_ERROR; }
	virtual LTRESULT	EndQuery() { return LT_ERROR; }
	virtual	LTRESULT	HostSession(NetHost* pHost) { return LT_ERROR; }
	virtual LTRESULT	JoinSession(NetSession *pSession) { return LT_ERROR; }
	virtual LTRESULT	SetSessionName(char* sName) { return LT_ERROR; }
	virtual LTRESULT	GetSessionName(char* sName, uint32 dwBufferSize) { return LT_ERROR; }
	virtual	LTBOOL		IsLobbyLaunched() { return LTFALSE; }
	virtual	LTBOOL		GetLobbyLaunchInfo(void** ppLobbyLaunchData) { return LTFALSE; }
	virtual	LTRESULT	HostLobbyLaunchSession(NetHost* pHost) { return LT_ERROR; }
	virtual LTBOOL		JoinLobbyLaunchSession() { return LTFALSE; }
	virtual	LTBOOL		GetLocalIpAddress(char* sBuffer, uint32 dwBufferSize, uint16 &hostPort) { return LTFALSE; }
	virtual LTRESULT	ConnectTCP(char* sAddress) { return LTFALSE; }
	virtual LTRESULT	SendTcpIp(void *pData, uint32 dataLen, char *sAddr, uint32 port) { return LT_ERROR; }

	// MUST call the net mugger's DisconnectNotify.
	virtual void		Disconnect(CBaseConn *id, int reason)=0;

	// Driver-level functions.
	// spaceAfter is how many bytes the driver may use after the data (for its header).
	virtual LTBOOL		SendPacket(void *pData, uint32 dataLen, uint32 spaceAfter, CBaseConn *idSendTo)=0;
	virtual LTBOOL		GetPacket(CPacket *pPacket)=0;

	// Bytes of driver overhead per packet.
	virtual uint32		GetPacketOverhead()=0;

public:

	char				m_Name[64];		// 0x04
	uint32				m_DriverFlags;	// 0x44
	CNetMgr				*m_pNetMgr;		// 0x48
	float				m_Bandwidth;	// 0x4c Also the connection timeout (seconds without packets).
};


class BaseService
{
public:
	virtual	~BaseService() {}

	CBaseDriver	*m_pDriver; // Where it came from.
};


// Drivers should derive from this for their sessions so the NetMgr knows what
// driver the session came from.
class NetMgrSession : public NetSession
{
public:
				NetMgrSession(CBaseDriver *pDriver)
				{
					m_pDriver = pDriver;
				}

	virtual		~NetMgrSession() {}

	CBaseDriver	*m_pDriver;		// 0x109c
};


class CNetHandler
{
public:

	virtual			~CNetHandler() {}

	// Return TRUE to accept connection.  FALSE to ignore.
	virtual LTBOOL	NewConnectionNotify(CBaseConn *id, LTBOOL bIsLocal)=0;
	virtual void	DisconnectNotify(CBaseConn *id)=0;

	// If an unknown packet comes in on tcp/ip (unknown sender and unknown packet ID)
	// this is called).
	virtual void	HandleUnknownPacket(CPacket *pPacket, uint8 senderAddr[4], uint16 senderPort)=0;

	// Called when a disconnection event occurs
	virtual void	SetDisconnectCode(uint32 nCode, char *pMsg) {}
};


class CNetMgr
{
public:
					CNetMgr();
					~CNetMgr();

	LTBOOL			Init(char *pPlayerName);
	void			Term();

	// Creates all the necessary drivers.
	LTRESULT		InitDrivers();
	void			TermDrivers();

	LTRESULT		GetServiceList(NetService* &pListHead);
	LTRESULT		FreeServiceList(NetService *pListHead);
	LTRESULT		SelectService(HNETSERVICE hService);

	LTRESULT		GetSessionList(NetSession* &pListHead, char *pInfo);
	LTRESULT		FreeSessionList(NetSession *pListHead);

	LTRESULT		GetSessionName(char *pName, uint32 bufLen);
	LTRESULT		SetSessionName(char *pName);

	LTRESULT		GetLocalIpAddress(char *pAddress, uint32 bufLen, uint16 &hostPort);

	void			NetDebugOut(int debugLevel, char *pMsg, ...);
	void			NetDebugOut2(CBaseConn *pConn, int debugLevel, char *pMsg, ...);

	void			SetAppGuid(LTGUID* pAppGuid);
	LTGUID*			GetAppGuid() { return(&m_guidApp); }	// inline (Jupiter); CreateServerMgr calls it

	// pPrefix is inserted in front of some debugging messages.
	void			Update(char *pPrefix, float fCurTime, LTBOOL bAllowTimeout);

	CBaseDriver*	AddDriver(char *pDriverInfo);
	CBaseDriver*	GetDriver(char *sDriver);
	void			RemoveDriver(CBaseDriver *pDriver);

	// This will still call DisconnectNotify() on you.
	void			Disconnect(CBaseConn *id, int reason);

	LTBOOL			SendPacket(CPacket *pPacket, CBaseConn *idSendTo, uint32 packetFlags);

	void			StartGettingPackets();
	void			EndGettingPackets();
	LTBOOL			GetPacket(CPacket *pPacket, CBaseConn **pSender);

	// Resends the connection's unacknowledged guaranteed packets.
	void			ResendGuaranteed(CBaseConn *pConn);

// Misc helpers.
public:

	// Inline (Jupiter netmgr.h); CServerMgr::TransferNetDriver calls SetMainDriver.
	void			SetMainDriver(CBaseDriver *pDriver) { m_pMainDriver = pDriver; }
	CBaseDriver*	GetMainDriver() { return(m_pMainDriver); }

	LTBOOL			LagOrSend(CPacket *pPacket, CBaseConn *idSendTo, uint8 oldPacketID);

	void			IncRecvCounter(CBaseConn *id, uint32 packetLen);
	void			IncSendCounter(CBaseConn *id, uint32 packetLen);

// Functions for drivers to call.  Just calls through to the handler.
public:

	LTBOOL			NewConnectionNotify(CBaseConn *id);
	void			DisconnectNotify(CBaseConn *id);

// Internal stuff.
public:

	void			FillAckPacket(CBaseConn *pConn, CPacket *pPacket);
	void			NextIncomingFrame(CBaseConn *pConn);
	LTBOOL			SendFragmented(void *pData, uint32 dataLen, uint32 extra, CBaseConn *pConn);
	LTBOOL			ReallySendPacket(CPacket *pPacket, CBaseConn *idSendTo);
	LTBOOL			HandleReceivedPacket(CPacket *pPacket, CBaseConn *pSender, LTBOOL bMaybeDrop);
	LTBOOL			HandleNetMgrPacket(CPacket *pPacket, CBaseConn *pSender);
	LTBOOL			HandleUnknownPacket(CBaseConn *pSender, CPacket *pPacket);

	CFragmentGroup*	FindFragmentGroup(uint32 frameNum, CBaseConn *pConn);
	LTBOOL			AddFragment(CPacket *pPacket, uint8 index, CFragmentGroup *pGroup);
	LTBOOL			IsFragmentGroupComplete(CFragmentGroup *pGroup);
	CFragmentGroup*	AllocFragmentGroup();
	void			FreeFragmentGroup(CFragmentGroup *pGroup);
	void			FreeFragmentGroups();
	void			RemoveConnFragments(CBaseConn *pConn);

	void			FreeGPacketsUpTo(GPacketList *pList, uint32 frameNum);
	void			FreeGPacketsAbove(GPacketList *pList, uint32 frameNum);
	LTBOOL			InsertGPacket(GPacketList *pList, GPacket *pPacket);
	void			FreeGPacket(GPacket *pPacket);
	GPacket*		FindGPacket(GPacketList *pList, uint32 frameNum);
	GPacket*		AllocGPacket(uint32 size);
	void			DeleteGPackets(GPacketList *pList);

	void			StartGroupPacket(CPacketRef &cPacket);
	void			AddToGroupPacket(CPacket *pGroup, CPacket *pPacket);
	void			AddDataToGroupPacket(CPacket *pGroup, void *pData, uint32 dataLen);

public:

	uint32					m_Flags;				// 0x00 State flags.

	ObjectBank<LatentPacket>	m_LatentPacketBank;	// 0x04 Used for latency simulation.

	// Elements in here are allocated (and owned) by the drivers.
	CMoArray<CBaseConn*>	m_Connections;			// 0x28
	CMoArray<CBaseDriver*>	m_Drivers;				// 0x3c

	char					m_PlayerName[100];		// 0x50
	CNetHandler				*m_pHandler;			// 0xb4

	GPacketList				m_FreeGPackets;			// 0xb8 Sorted by size, largest first.
	CGLinkedList<CFragmentGroup*>	m_FragmentGroups;	// 0xc4

	char					*m_pCurPrefix;			// 0xd0
	float					m_FrameTime;			// 0xd4

	uint32					m_nGPacketBytes;		// 0xd8
	uint32					m_nDroppedPackets;		// 0xdc
	RateTracker				m_SendBPS;				// 0xe0
	float					m_fLastTime;			// 0xec

	LTGUID					m_guidApp;				// 0xf0
	CBaseDriver				*m_pMainDriver;			// 0x100
};


// Helper routines.
uint16 GetWordCRC(uint8 *pData, uint16 dataLen);

#endif
