// Talon LMessageImpl support (the methods live in shared/lmessage.cpp, 0x00445830-0x00446170).
#ifndef __LMESSAGE_H__
#define __LMESSAGE_H__

#include <stdarg.h>
#include "ltbasedefs.h"

class ILTMessage;
class MainWorld;

// The engine services an LMessageImpl uses for object references, string resources and
// compressed positions. LMessageImpl::m_Unknown04 points at one: the server's is
// CServerSerializeHelper (sserializehelper.h). Name unknown.
class LMessageHelper
{
public:
	virtual LTRESULT	ReadObjectRef(ILTMessage *pMsg, HOBJECT *pObj)=0;
	virtual LTRESULT	WriteObjectRef(ILTMessage *pMsg, HOBJECT hObj)=0;
	virtual LTRESULT	WriteHStringArgList(ILTMessage *pMsg, int messageCode, va_list *pList)=0;
	virtual LTRESULT	GetWorld(MainWorld **ppWorld)=0;
};

#endif  // __LMESSAGE_H__
