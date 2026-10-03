// Talon UDP network driver (kernel/net/sys/win/udpdriver.h). Talon's driver is a plain
// non-threaded winsock driver; Jupiter's udpdriver.h is a later rewrite.
// Layouts recovered from lithtech.exe (udpdriver.cpp 00497770-0049ac10).
#ifndef __UDPDRIVER_H__
#define __UDPDRIVER_H__

#include <winsock2.h>
#include "netmgr.h"
#include "../../build/proj/LT2/lithshared/stdlith/multilinklist.h"

#define DEFAULT_PORT		27888	// 0x6cf0

// Packet ID 0 is the driver's own; the next byte is one of these.
#define TCPSUB_DISCONNECT			0
#define TCPSUB_QUERY				1
#define TCPSUB_QUERYRESPONSE		2
#define TCPSUB_CONNECTREQUEST		3
#define TCPSUB_CONNECTACCEPTED		5
#define TCPSUB_CONNECTREJECTED		6
#define TCPSUB_NOTSAMEGUID			7


// A connection (0xe0 bytes).
class CUDPConn : public CBaseConn
{
public:
					CUDPConn()
					{
						m_Socket = 0;
					}

					~CUDPConn()
					{
						if(m_Socket)
							closesocket(m_Socket);
					}

	sockaddr_in		m_Addr;			// 0xbc
	CMLLNode		m_Link;			// 0xcc In CUDPDriver::m_Connections.
	SOCKET			m_Socket;		// 0xd8
	LTBOOL			m_bConnected;	// 0xdc
};


// A host we're querying for sessions (0xa4 bytes).
#define NUM_QUERY_TIMES		32

class CUDPQuery
{
public:
					CUDPQuery()
					{
						m_pInfo = LTNULL;
						m_Unknown9C = 0.0f;
						m_iTime = 0;
						memset(m_SendTimes, 0, sizeof(m_SendTimes));
						m_Ping = 0.0f;
						m_LastResponseTime = 0.0f;
					}

					CUDPQuery(const CUDPQuery &other)
					{
						m_pInfo = LTNULL;
						Copy(other);
					}

					~CUDPQuery()
					{
						if(m_pInfo)
							delete m_pInfo;
					}

	CUDPQuery&		operator=(const CUDPQuery &other)
	{
		Copy(other);
		return *this;
	}

	void			Copy(const CUDPQuery &other)
	{
		if(m_pInfo)
			delete m_pInfo;

		memcpy(this, &other, sizeof(*this));

		if(other.m_pInfo)
		{
			m_pInfo = new char[strlen(other.m_pInfo) + 1];
			strcpy(m_pInfo, other.m_pInfo);
		}
	}

	float			m_SendTimes[NUM_QUERY_TIMES];	// 0x00
	uint32			m_iTime;				// 0x80 0xff = broadcast
	float			m_LastResponseTime;		// 0x84
	sockaddr_in		m_Addr;					// 0x88
	char			*m_pInfo;				// 0x98 Session info from the response.
	float			m_Unknown9C;			// 0x9c (a float: its zero is not counted with the integer zero stores)
	float			m_Ping;					// 0xa0
};


// The sessions it hands out (0x10b0 bytes).
class CUDPSession : public NetMgrSession
{
public:
					CUDPSession(CBaseDriver *pDriver) : NetMgrSession(pDriver) {}

	sockaddr_in		m_Addr;			// 0x10a0
};


// vtable 004c8a20
class CUDPDriver : public CBaseDriver
{
public:

						CUDPDriver();
	virtual				~CUDPDriver();

	virtual LTBOOL		Init();
	virtual void		Term();

	virtual void		Update() {}

	virtual	LTRESULT	GetServiceList(NetService* &pListHead);
	virtual	LTRESULT	SelectService(HNETSERVICE hService) { return LT_OK; }
	virtual LTRESULT	GetSessionList(NetSession* &pListHead, char *pInfo);
	virtual LTRESULT	StartQuery(char *pInfo);
	virtual LTRESULT	UpdateQuery();
	virtual LTRESULT	GetQueryResults(NetSession* &pListHead);
	virtual LTRESULT	EndQuery();
	virtual	LTRESULT	HostSession(NetHost* pHost);
	virtual LTRESULT	JoinSession(NetSession *pSession);
	virtual LTRESULT	SetSessionName(char* sName);
	virtual LTRESULT	GetSessionName(char* sName, uint32 dwBufferSize);
	virtual	LTBOOL		GetLobbyLaunchInfo(void** ppLobbyLaunchData) { return LTFALSE; }
	virtual	LTBOOL		GetLocalIpAddress(char* sBuffer, uint32 dwBufferSize, uint16 &hostPort);
	virtual LTRESULT	ConnectTCP(char* sAddress);
	virtual LTRESULT	SendTcpIp(void *pData, uint32 dataLen, char *sAddr, uint32 port);

	virtual void		Disconnect(CBaseConn *id, int reason);

	virtual LTBOOL		SendPacket(void *pData, uint32 dataLen, uint32 spaceAfter, CBaseConn *idSendTo);
	virtual LTBOOL		GetPacket(CPacket *pPacket);

	virtual uint32		GetPacketOverhead();

public:

	void				TermConnections(LTBOOL bCleanup);
	LTBOOL				SendTo(SOCKET theSocket, void *pData, int dataLen, sockaddr_in *pAddr);
	void				SendDisconnect(CUDPConn *pConn);
	CUDPConn*			FindConnByAddr(sockaddr_in *pAddr);
	void				HandleDriverPacket(CPacket *pPacket, sockaddr_in *pSender);
	LTRESULT			JoinSession(sockaddr_in *pAddr);

public:

	CMoArray<CUDPQuery>	m_Queries;			// 0x50
	SOCKET				m_QuerySocket;		// 0x64
	float				m_LastQueryTime;	// 0x68
	sockaddr_in			m_QueryAddr;		// 0x6c
	LTBOOL				m_bHosting;			// 0x7c
	sockaddr_in			m_HostAddr;			// 0x80
	SOCKET				m_Socket;			// 0x90
	uint32				m_MaxConnections;	// 0x94
	char				*m_pSessionName;	// 0x98
	CMultiLinkList<CUDPConn*>	m_Connections;	// 0x9c
	LTBOOL				m_bWSAStarted;		// 0xa4
	BaseService			m_Service;			// 0xa8
};

CBaseDriver* udp_CreateDriver();

#endif
