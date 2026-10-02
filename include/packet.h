// Talon network packet (kernel/net/packet.h). Talon's packets are reference-counted, pooled byte
// buffers with an embedded ILTMessage implementation; Jupiter replaced them with CPacket_Read/Write.
// Layout recovered from lithtech.exe (packet.cpp 00469080-00469590, netmgr.cpp).
#ifndef __PACKET_H__
#define __PACKET_H__

#include <string.h>
#include "bdefs.h"
#include "iltmessage.h"
#include "ltdynarray.h"
#include "../../build/proj/LT2/lithshared/stdlith/multilinklist.h"

#define MAX_PACKET_LEN	1100	// 0x44c

// CPacket::m_ErrorFlags
#define PACKETERR_READOVERFLOW		(1<<1)
#define PACKETERR_WRITEOVERFLOW		(1<<2)
#define PACKETERR_FRAGMENTED		(1<<3)	// Sent in fragments (set by the netmgr).
#define PACKETERR_GROUPED			(1<<4)

class CPacket;

// ILTMessage implementation embedded in each packet (vtable 004c7d50, methods in netmgr.cpp).
class LMessageImpl : public ILTMessage
{
public:
	LMessageImpl() {}

	virtual LTRESULT	Release();
	virtual char const*	ReadString();
	virtual LTRESULT	ReadByteFL(uint8 &val);
	virtual LTRESULT	ReadWordFL(uint16 &val);
	virtual LTRESULT	ReadDWordFL(uint32 &val);
	virtual LTRESULT	ReadFloatFL(float &val);
	virtual LTRESULT	ReadStringFL(char *pData, uint32 maxBytes);
	virtual LTRESULT	ReadHStringFL(HSTRING &hString);
	virtual LTRESULT	ReadHStringAsStringFL(char *pMsg, uint32 msgBufSize);
	virtual LTRESULT	ReadRawFL(void *pData, uint32 len);
	virtual LTRESULT	ReadVectorFL(LTVector &vec);
	virtual LTRESULT	ReadCompVectorFL(LTVector &vec);
	virtual LTRESULT	ReadCompPosFL(LTVector &vec);
	virtual LTRESULT	ReadRotationFL(LTRotation &rot);
	virtual LTRESULT	ReadCompRotationFL(LTRotation &rot);
	virtual LTRESULT	ReadMessageFL(ILTMessage* &pMsg);
	virtual LTRESULT	ReadObjectFL(HOBJECT &hObj);
	virtual LTRESULT	WriteByte(uint8 val);
	virtual LTRESULT	WriteWord(uint16 val);
	virtual LTRESULT	WriteDWord(uint32 val);
	virtual LTRESULT	WriteFloat(float val);
	virtual LTRESULT	WriteString(char *pData);
	virtual LTRESULT	WriteVector(LTVector &vec);
	virtual LTRESULT	WriteCompVector(LTVector &vec);
	virtual LTRESULT	WriteCompPos(LTVector &vec);
	virtual LTRESULT	WriteRotation(LTRotation &rot);
	virtual LTRESULT	WriteCompRotation(LTRotation &rot);
	virtual LTRESULT	WriteRaw(void *pData, uint32 len);
	virtual LTRESULT	WriteMessage(ILTMessage &msg);
	virtual LTRESULT	WriteHString(HSTRING hString);
	virtual LTRESULT	WriteHStringFormatted(int messageCode, ...);
	virtual LTRESULT	WriteHStringArgList(int messageCode, va_list *pList);
	virtual LTRESULT	WriteStringAsHString(char *pStr);
	virtual LTRESULT	WriteObject(HOBJECT hObj);
	virtual LTRESULT	ResetPos();
	virtual LTRESULT	GetStatus(uint32 &flags);

	// Engine-only slot 0x90 (shares 0x0043dac0, which returns 0). The client's EndMessage2
	// and SendToServer refuse a message when it's nonzero.
	virtual LTBOOL		IsInvalid();

public:
	uint32		m_Unknown04;		// +0x04
	CPacket		*m_pPacket;			// +0x08
	uint32		m_MsgType;			// +0x0c server: who EndMessage2 sends it to (MSGTYPE_)
	struct Client	*m_pClient;		// +0x10 server: MSGTYPE_CLIENT target (LTNULL = everybody)
	HOBJECT		m_hSender;			// +0x14 server: sending object
	HOBJECT		m_hObject;			// +0x18 server: MSGTYPE_OBJECT target / MSGTYPE_SFX object
	uint32		m_MsgID;			// +0x1c message ID (ILTClient::StartMessage)
	LTVector	m_Pos;				// +0x20 server: MSGTYPE_INSTANTSFX position
};


// Reference-counted base (vtable 004c7d28).
class CPacketBase
{
public:
	CPacketBase() { m_RefCount = 0; }
	virtual			~CPacketBase() {}

	virtual void	AddRef() { ++m_RefCount; }
	virtual void	Release() { if(--m_RefCount == 0) Free(); }
	virtual void	Free() { delete this; }

public:
	uint32		m_RefCount;			// 0x04
};


// vtable 004c7d3c
class CPacket : public CPacketBase
{
public:
	CPacket()
	{
		m_Unknown3A = 1;
		m_ErrorFlags = 0;
		Init(MAX_PACKET_LEN, MAX_PACKET_LEN);
	}

	virtual void	Free();		// Returns it to the free list.

	// Inlined into packet_Get. The original keeps the constructor's call out of line (copy at 00465f40).
	void	Init(uint16 maxSize, uint16 cacheSize);

	LTBOOL	WritePacket(CPacket *pPacket);
	char*	ReadString();
	void	ReadRaw(void *pData, uint16 len);
	void	WriteString(char *pStr);
	void	WriteRaw(void *pData, uint16 len);

	// WriteType/ReadType are one-line wrappers around the real bodies (names of the inner
	// functions unknown). The extra inline level is what the original's decisions need: the
	// bodies are expanded as nested sites whose share of the inline budget shrinks with every
	// inline call still to come in the caller, so the early WriteType/ReadType calls in big
	// functions (FillAckPacket, HandleNetMgrPacket, JoinSession, sm_SetPortalFlags...) stay
	// out of line while later ones are inlined. The out-of-line copies (0x00417c20,
	// 0x004370c0, 0x00437a50, 0x00466270, 0x00436fc0, 0x00437040) are the inner functions.
	template<class T>
	void	WriteType(T val)	{ WriteTypeImpl(val); }

	template<class T>
	void	WriteTypeImpl(T val)
	{
		if(m_Pos + sizeof(T) < m_MaxSize)
		{
			while(m_DataLen + sizeof(T) >= m_Data.GetSize())
				m_Data.Append(0);

			*((T*)&m_Data[m_Pos]) = val;
			m_Pos += sizeof(T);
			m_DataLen += sizeof(T);
		}
		else
		{
			GENERATE_ERROR(1, CPacket::WriteType, LT_ERROR, "ERROR: Packet write overrun!");
			m_ErrorFlags |= PACKETERR_WRITEOVERFLOW;
		}
	}

	template<class T>
	T		ReadType(T *pDummy)	{ return ReadTypeImpl(pDummy); }

	template<class T>
	T		ReadTypeImpl(T *pDummy)
	{
		T val;

		if(m_Pos + sizeof(T) < m_Data.GetSize() && m_Pos + sizeof(T) <= m_DataLen)
		{
			val = *((T*)&m_Data[m_Pos]);
			m_Pos += sizeof(T);
		}
		else
		{
			m_ErrorFlags |= PACKETERR_READOVERFLOW;
			GENERATE_ERROR(1, CPacket::ReadType, LT_ERROR, "ERROR: Packet read overrun!");
		}

		return val;
	}

	uint8	GetPacketID()	{return m_Data[0];}

	// The message interface embedded in the packet. StartHMessageWrite returns it through this
	// inline call: a trailing inline call site halves the inline budget left for Init's nested
	// expansions, which keeps all three CMoArray::SetSize2 calls out of line.
	LMessageImpl*	GetMessageImpl()	{return &m_Message;}

public:
	LMessageImpl	m_Message;		// 0x08
	uint16			m_DataLen;		// 0x34 bytes written (byte 0 is the packet ID)
	uint16			m_MaxSize;		// 0x36
	uint16			m_Pos;			// 0x38 read/write position
	uint16			m_Unknown3A;	// 0x3a
	class CBaseConn	*m_pSender;		// 0x3c connection it came from
	uint32			m_ErrorFlags;	// 0x40
	CMLLNode		m_FreeLink;		// 0x44
	CMoArray<uint8>	m_Data;			// 0x50
};


inline void CPacket::Init(uint16 maxSize, uint16 cacheSize)
{
	m_DataLen = 1;
	m_MaxSize = maxSize;
	m_Pos = 1;
	m_Unknown3A = 1;
	m_ErrorFlags = 0;
	m_Message.m_pPacket = this;

	if(maxSize > m_Data.GetSize())
	{
		m_Data.Term();
		m_Data.Init(maxSize, cacheSize);
	}
	else
	{
		m_Data.SetCacheSize(cacheSize);
	}
}


// Smart pointer returned by packet_Get.
class CPacketRef
{
public:
	CPacketRef()
	{
		m_pPacket = LTNULL;
	}

	CPacketRef(CPacket *pPacket)
	{
		m_pPacket = pPacket;
		if(m_pPacket)
			m_pPacket->AddRef();
	}

	~CPacketRef()
	{
		if(m_pPacket)
			m_pPacket->Release();
	}

	CPacketRef&	operator=(const CPacketRef &other)
	{
		if(m_pPacket)
			m_pPacket->Release();

		m_pPacket = other.m_pPacket;
		if(m_pPacket)
			m_pPacket->AddRef();

		return *this;
	}

	CPacketRef&	operator=(CPacket *pPacket)
	{
		if(m_pPacket)
			m_pPacket->Release();

		m_pPacket = pPacket;
		if(m_pPacket)
			m_pPacket->AddRef();

		return *this;
	}

	CPacket*	operator->() const	{return m_pPacket;}
				operator CPacket*() const	{return m_pPacket;}

	CPacket	*m_pPacket;
};


void packet_Init();
void packet_Term();
CPacketRef packet_Get(uint16 maxSize, uint16 cacheSize);

// Takes a reference of our own on a packet (release it with CPacket::Release).
inline CPacket* packet_AddRef(const CPacketRef &cPacketRef)
{
	CPacket *pPacket = cPacketRef.m_pPacket;

	if(pPacket)
		pPacket->AddRef();

	return pPacket;
}

#endif
