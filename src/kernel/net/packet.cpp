// Talon kernel/net/packet.cpp (no Jupiter counterpart: Jupiter rewrote packets as CPacket_Read/Write).
// FLAGS: /O2 /GX-
#include <string.h>
#include "packet.h"

// Free packets. Its dynamic initializer/atexit functions:
// FUNCTION: LITHTECH 0x00469070 _$E4
// FUNCTION: LITHTECH 0x00469080 _$E1
// FUNCTION: LITHTECH 0x00469090 _$E3
// FUNCTION: LITHTECH 0x004690a0 _$E2
// GLOBAL: LITHTECH 0x004e4608
static CMultiLinkList<CPacket*> g_FreePackets;

// GLOBAL: LITHTECH 0x004e4610
static int g_PacketInitCount;

// GLOBAL: LITHTECH 0x004e4614
static uint32 g_nPacketsAllocated;


// FUNCTION: LITHTECH 0x004690b0
void packet_Init()
{
	++g_PacketInitCount;
}

// FUNCTION: LITHTECH 0x004690c0
void packet_Term()
{
	if(g_PacketInitCount > 0)
	{
		--g_PacketInitCount;
		if(g_PacketInitCount == 0)
		{
			MDeleteAndRemoveElements(g_FreePackets);
			g_nPacketsAllocated = 0;
		}
	}
}

inline CPacket* packet_Alloc()
{
	CPacket *pPacket;

	if(g_FreePackets.GetSize() > 0)
	{
		pPacket = g_FreePackets.GetHead();
		g_FreePackets.RemoveAt(&pPacket->m_FreeLink);
	}
	else
	{
		pPacket = new CPacket;
		if(pPacket)
			++g_nPacketsAllocated;
	}

	return pPacket;
}

// The inlined constructor calls CPacket::Init out of line (00465f40); the second call inlines it.
// FUNCTION: LITHTECH 0x00469120
CPacketRef packet_Get(uint16 maxSize, uint16 cacheSize)
{
	CPacket *pPacket;

	if(g_FreePackets.GetSize() > 0)
	{
		pPacket = g_FreePackets.GetHead();
		g_FreePackets.RemoveAt(&pPacket->m_FreeLink);
	}
	else
	{
		pPacket = new CPacket;
		if(pPacket)
			++g_nPacketsAllocated;
	}

	if(pPacket)
		pPacket->Init(maxSize, cacheSize);

	return CPacketRef(pPacket);
}

// FUNCTION: LITHTECH 0x00469270
void CPacket::Free()
{
	g_FreePackets.AddHead(this, &m_FreeLink);
}

// Appends pPacket's data (without its ID byte) if it fits.
// FUNCTION: LITHTECH 0x004692d0
LTBOOL CPacket::WritePacket(CPacket *pPacket)
{
	int spaceLeft;

	spaceLeft = (int)m_MaxSize - (int)m_Pos - 7;
	if(spaceLeft < 0)
		spaceLeft = 0;

	if(spaceLeft >= (int)(pPacket->m_DataLen - 1))
	{
		WriteRaw(&pPacket->m_Data[1], pPacket->m_DataLen - 1);
		return TRUE;
	}

	return FALSE;
}

// FUNCTION: LITHTECH 0x00469320
char* CPacket::ReadString()
{
	char *pRet;

	pRet = (char*)&m_Data[m_Pos];
	while(m_Pos < m_MaxSize && m_Pos < m_Data.GetSize() && m_Data[m_Pos] != 0)
	{
		++m_Pos;
	}

	++m_Pos;
	return pRet;
}

// FUNCTION: LITHTECH 0x00469370
void CPacket::ReadRaw(void *pData, uint16 len)
{
	uint16 nBytes = len;

	if(m_Pos + nBytes >= m_MaxSize)
		nBytes = m_MaxSize - m_Pos;

	if(m_Pos + nBytes >= (uint16)m_Data.GetSize())
		nBytes = (uint16)m_Data.GetSize() - m_Pos;

	memcpy(pData, &m_Data[m_Pos], nBytes);
	m_Pos += nBytes;
}

// FUNCTION: LITHTECH 0x004693f0
void CPacket::WriteString(char *pStr)
{
	if(pStr)
	{
		WriteRaw(pStr, strlen(pStr) + 1);
	}
	else
	{
		char zero = 0;
		WriteRaw(&zero, 1);
	}
}

// FUNCTION: LITHTECH 0x00469430
void CPacket::WriteData(void *pData, uint16 len)
{
	uint16 nBytes = len;

	if(m_Pos + nBytes >= m_MaxSize)
		nBytes = m_MaxSize - m_Pos;

	while(m_Pos + nBytes >= (uint16)m_Data.GetSize())
		m_Data.Append(0);

	memcpy(&m_Data[m_Pos], pData, nBytes);
	m_Pos += nBytes;
	m_DataLen += nBytes;
}
