// Client loader thread (Jupiter runtime/client/src/cloaderthread.h). Embedded in CClientMgr at
// 0x16b8 (constructed by cm_Init); vtable 0x004c6e8c.
#ifndef __CLOADERTHREAD_H__
#define __CLOADERTHREAD_H__

#include <stdarg.h>
#include "lthread.h"

struct FileIdentifier;
class CClientMgr;
class ILTMessage;
class MainWorld;

// The client's ILTMessage helper: object references and string resources for LMessageImpl
// (through LMessageImpl::m_Unknown04). 8 bytes, vtable 0x004c6e7c; cm_Init stores one in
// CClientMgr::m_pMessageHelper. The server's twin is CServerSerializeHelper (sloaderthread.h).
class CClientSerializeHelper
{
public:
					CClientSerializeHelper(CClientMgr *pClientMgr);

	virtual LTRESULT	ReadObjectRef(ILTMessage *pMsg, HOBJECT *pObj);
	virtual LTRESULT	WriteObjectRef(ILTMessage *pMsg, HOBJECT hObj);
	virtual LTRESULT	WriteHStringArgList(ILTMessage *pMsg, int messageCode, va_list *pList);
	virtual LTRESULT	GetWorld(MainWorld **ppWorld);

	CClientMgr		*m_pClientMgr;		// 0x04
};

// Messages in the loader thread.
// m_Data[0].m_dwData is a FT_ define from de_codes.h
// m_Data[1].m_pData is the FileIdentifier*.
// m_Data[2].m_dwData is a TEXTURELOAD_ define.
#define CLT_LOADFILE	0

// Notification that a resource is done loading.
#define CLT_LOADEDFILE	0
#define CLT_LOADERROR	1


// 0x64 bytes.
class CLoaderThread : public LThread
{
// Out of thread.
public:

					CLoaderThread();

	// 0x0048eb80 (identical code folded with CServerLoaderThread::Init): remembers the manager and starts.
	LTBOOL			Init(CClientMgr *pClientMgr);

	// Are we loading this file or is it in our queue to load?
	LTBOOL			IsLoadingFile(FileIdentifier *pIdent);


// In-thread.
protected:

	virtual void	ProcessMessage(LThreadMessage &msg);

	// After a model is loaded, this does the work of integrating it into the client
	// (add to the models list, update placeholder models, etc).
	void			LoadModel(FileIdentifier *pIdent);

public:
	CClientMgr		*m_pClientMgr;	// 0x60
};


#endif  // __CLOADERTHREAD_H__
