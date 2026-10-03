// Talon local (same process) network driver (kernel/net/localdriver.h).
#ifndef __LOCALDRIVER_H__
#define __LOCALDRIVER_H__

#include "netmgr.h"

// Packet buffer (0x1c bytes). Holders are kept on a size-sorted free list and
// a doubly-linked waiting list.
class CLocalPacketHolder
{
public:
	CLocalPacketHolder();
	~CLocalPacketHolder();

	uint32				m_Flags;		// 0x00 bit 0: on the waiting list
	int					m_AllocSize;	// 0x04
	int					m_DataLen;		// 0x08
	uint8				*m_pData;		// 0x0c
	CLocalPacketHolder	*m_pPrev;		// 0x10 waiting list
	CLocalPacketHolder	*m_pNext;		// 0x14
	CLocalPacketHolder	*m_pNextFree;	// 0x18 holder list (sorted by m_AllocSize)
};


// vtable 004c75d0
class CLocalDriver : public CBaseDriver
{
public:

						CLocalDriver();
	virtual				~CLocalDriver();

	virtual LTBOOL		Init();
	virtual void		Term();

	virtual void		Update();

	virtual void		LocalConnect(CBaseDriver *pOther);

	virtual void		Disconnect(CBaseConn *id, int reason);

	virtual LTBOOL		SendPacket(void *pData, uint32 dataLen, uint32 spaceAfter, CBaseConn *idSendTo);
	virtual LTBOOL		GetPacket(CPacket *pPacket);

	virtual uint32		GetPacketOverhead() { return 0; }

// Functions called between local drivers.
// NOTE:  These MUST be virtual so it actually executes the code from
//        the correct module!!
public:

	virtual void		ConnectToMe(CLocalDriver *pDriver);
	virtual void		DisconnectFromMe();

	virtual void		DoConnection(CLocalDriver *pDriver);
	virtual void		RecvPacket(void *pData, int dataLen);

public:

	LTBOOL				m_bPendingConnection;	// 0x50

	CLocalDriver		*m_pConnection;			// 0x54
	CBaseConn			*m_pBaseConn;			// 0x58

	CLocalPacketHolder	m_Holders;				// 0x5c list head (m_pNextFree)
	CLocalPacketHolder	m_Waiting;				// 0x78 list head (m_pPrev/m_pNext)

	uint32				m_nAllocatedBytes;		// 0x94
	uint32				m_nWaiting;				// 0x98
	uint32				m_nPackets;				// 0x9c
};

CBaseDriver* ld_CreateDriver();

#endif
