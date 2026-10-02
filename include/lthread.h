// Talon thread primitives (Jupiter runtime/kernel/src/sys/win/lthread.h).
// Talon's LCriticalSection wraps a CRITICAL_SECTION directly (no CSysSerialVar base), and
// LThread is not a CSysThread.
#ifndef __LTHREAD_H__
#define __LTHREAD_H__

#include <windows.h>
#include "ltbasedefs.h"
#include "../../build/proj/LT2/lithshared/stdlith/goodlinklist.h"

// Thread priorities go from 0 to LTPRI_MAX.
#define LTPRI_LOWEST	0
#define LTPRI_NORMAL	3
#define LTPRI_MAX		5

// How much data each LThreadMessage contains..
#define NUM_THREADMESSAGE_DATA	4

class LThread;

// Used to synchronize access to things.
class LCriticalSection
{
public:
				LCriticalSection();		// 0x0044c520
				~LCriticalSection();	// 0x0044c540

	// Always TRUE (identical-code folded at 0x004b22a0).
	LTBOOL		IsValid();

	// No two threads can enter a critical section at the same time so if two
	// call Enter at the same time, one will wait until the other one leaves.
	void		Enter();				// 0x0044c550
	void		Leave();				// 0x0044c560

	CRITICAL_SECTION	m_CS;			// 0x00
};

// This class automatically enters and leaves the critical section in its constructor and destructor.
class CSAccess
{
public:
	CSAccess(LCriticalSection *pSection)
	{
		m_pSection = pSection;
		pSection->Enter();
	}

	~CSAccess()
	{
		m_pSection->Leave();
	}

	LCriticalSection *m_pSection;
};

// This is what threads use to communicate between eachother. 0x20 bytes.
class LThreadMessage : public CGLLNode
{
public:
				LThreadMessage();		// 0x0044c570

	uint32		m_ID;			// 0x08
	LThread		*m_pSender;		// 0x0c Optional..

	union
	{
		void	*m_pData;
		uint32	m_dwData;
		uint16	m_wData[sizeof(uint32)/sizeof(uint16)];
		uint8	m_bData[sizeof(uint32)/sizeof(uint8)];
	} m_Data[NUM_THREADMESSAGE_DATA];	// 0x10
};

// A message queue. 0x28 bytes.
class LThreadQueue
{
public:
				LThreadQueue();
				~LThreadQueue();

	LTRESULT	Init();
	void		Clear();	// Clear all the messages.

	// Post a message to the queue.
	LTRESULT	PostMessage(LThreadMessage &msg);

	// GetMessage gets the next message and pops it off the list, PeekMessage leaves it.
	// Both return LT_NOTFOUND if there are no messages.
	LTRESULT	GetMessage(LThreadMessage &msg, LTBOOL bWait=LTFALSE);
	LTRESULT	PeekMessage(LThreadMessage &msg);

	// Pops the top message off, and copies it into pMsg if it's non-NULL.
	LTBOOL		PopMessage(LThreadMessage *pMsg = NULL);

public:
	LCriticalSection				m_MessageCS;	// 0x00
	CGLinkedList<LThreadMessage*>	m_Messages;		// 0x18 new messages are added at the end.
	HANDLE							m_hMsgEvent;	// 0x24 Message waiting
};

class LThread
{
public:
				LThread();
	virtual		~LThread();

	LTRESULT	Start(int priority=LTPRI_NORMAL);

	// Stops the thread and waits for it to exit.
	LTRESULT	Terminate(LTBOOL bWait=TRUE);

	LTRESULT	PostMessage(LThreadMessage &msg) {return m_Incoming.PostMessage(msg);}

public:
	virtual void	Term();		// Frees resources and stuff.

	// The default implementation waits for a message in the incoming queue, calls
	// ProcessMessage and then pops the message.
	virtual void	ThreadRun();

	// This is called by the default ThreadRun() when there's a new message.
	virtual void	ProcessMessage(LThreadMessage &msg);

public:
	LThreadQueue	m_Incoming;		// 0x04 Messages to the thread go in here.
	LThreadQueue	m_Outgoing;		// 0x2c The thread posts status messages in here.

	HANDLE			m_Handle;		// 0x54
	DWORD			m_ThreadID;		// 0x58
	HANDLE			m_hStopEvent;	// 0x5c Notification to shut down
};

#endif  // __LTHREAD_H__
