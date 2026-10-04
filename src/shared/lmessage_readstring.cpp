// The original function-name pointer is at 0x004d3c30, after the shared
// lmessage strings, with its string literal beginning at 0x004d3c34. Keep
// this function in a separate contribution to preserve that data boundary.
// FLAGS: /O2 /GX-
#include "bdefs.h"
#include "packet.h"
#include "lmessage.h"

inline int32 GetBytesLeftToRead(CPacket *pPacket)
{
	return pPacket->m_DataLen - pPacket->m_Pos;
}

#define CHECK_AUTORESET(pPacket) \
	if(GetBytesLeftToRead(pPacket) <= 0 && ((pPacket)->m_ErrorFlags & 1)) \
		(pPacket)->m_Pos = 1;

// FUNCTION: LITHTECH 0x00446130
// GLOBAL: LITHTECH 0x004d3c30 ?___bdefs__pFnName@?1??ReadString@LMessageImpl@@UAEPBDXZ@4PADA
// GLOBAL: LITHTECH 0x004d3c34 ??_C@_0BJ@MILM@LMessageImpl?3?3ReadString?$AA@
char const* LMessageImpl::ReadString()
{
	FN_NAME(LMessageImpl::ReadString);
	char *pRet;

	pRet = m_pPacket->ReadString();
	CHECK_AUTORESET(m_pPacket);
	return pRet;
}
