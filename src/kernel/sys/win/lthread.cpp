// Jupiter runtime/kernel/src/sys/win/lthread.cpp
// <windows.h> renames the queue's PostMessage/GetMessage/PeekMessage to their A versions, as
// in the original build.
// The unit starts at 0044c470 with the static initializers of the message bank and its
// critical section.
#include "bdefs.h"
#include "lthread.h"
#include "../../build/proj/LT2/lithshared/stdlith/object_bank.h"


// FUNCTION: LITHTECH 0x0044c470 _$E4
// FUNCTION: LITHTECH 0x0044c480 _$E1
// FUNCTION: LITHTECH 0x0044c4a0 _$E3
// FUNCTION: LITHTECH 0x0044c4b0 _$E2
// GLOBAL: LITHTECH 0x004e44c8
static ObjectBank<LThreadMessage> g_LThreadMessageBank;
// FUNCTION: LITHTECH 0x0044c4e0 _$E9
// FUNCTION: LITHTECH 0x0044c4f0 _$E6
// FUNCTION: LITHTECH 0x0044c500 _$E8
// FUNCTION: LITHTECH 0x0044c510 _$E7
// GLOBAL: LITHTECH 0x004e44b0
static LCriticalSection g_MessageBankCS; // Controls access to it.


// ------------------------------------------------------------------------------ //
// LCriticalSection
// ------------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044c520
LCriticalSection::LCriticalSection()
{
	memset(&m_CS, 0, sizeof(m_CS));
	InitializeCriticalSection(&m_CS);
}

// FUNCTION: LITHTECH 0x0044c540
LCriticalSection::~LCriticalSection()
{
	DeleteCriticalSection(&m_CS);
}

LTBOOL LCriticalSection::IsValid()
{
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x0044c550
void LCriticalSection::Enter()
{
	EnterCriticalSection(&m_CS);
}

// FUNCTION: LITHTECH 0x0044c560
void LCriticalSection::Leave()
{
	LeaveCriticalSection(&m_CS);
}


// ------------------------------------------------------------------------------ //
// LThreadMessage
// ------------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044c570
LThreadMessage::LThreadMessage()
{
	m_ID = 0;
	m_pSender = LTNULL;
}


// ------------------------------------------------------------------------------ //
// LThreadQueue functions.
// ------------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044c580
LThreadQueue::LThreadQueue()
{
	// Create ourselves a message event
	m_hMsgEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
}


// FUNCTION: LITHTECH 0x0044c5b0
LThreadQueue::~LThreadQueue()
{
	Clear();
	// Delete the message event
	CloseHandle(m_hMsgEvent);
}


// FUNCTION: LITHTECH 0x0044c5e0
LTRESULT LThreadQueue::Init()
{
	// This must be initialized for this to work.
	if(!g_MessageBankCS.IsValid() || !m_MessageCS.IsValid())
	{
		RETURN_ERROR(1, LThreadQueue::Init, LT_NOTINITIALIZED);
	}

	Clear();
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0044c640
void LThreadQueue::Clear()
{
	GDeleteAndRemoveElementsOB(m_Messages, g_LThreadMessageBank);
	// Pulse the message event in case someone's waiting on it
	PulseEvent(m_hMsgEvent);
}


// FUNCTION: LITHTECH 0x0044c690 ?PostMessageA@LThreadQueue@@QAEKAAVLThreadMessage@@@Z
LTRESULT LThreadQueue::PostMessage(LThreadMessage &msg)
{
	LThreadMessage *pToAdd;

	if(!g_MessageBankCS.IsValid())
	{
		RETURN_ERROR(1, LThreadQueue::PostMessage, LT_NOTINITIALIZED);
	}

	m_MessageCS.Enter();
	g_MessageBankCS.Enter();
		pToAdd = g_LThreadMessageBank.Allocate();
		if(pToAdd)
		{
			*pToAdd = msg;
			m_Messages.AddTail(pToAdd);

			// Signal to ourself that we've got a message
			SetEvent(m_hMsgEvent);
		}
	g_MessageBankCS.Leave();
	m_MessageCS.Leave();

	if(pToAdd)
	{
		return LT_OK;
	}
	else
	{
		RETURN_ERROR(1, LThreadQueue::PostMessage, LT_OUTOFMEMORY);
	}
}


// FUNCTION: LITHTECH 0x0044c7d0 ?GetMessageA@LThreadQueue@@QAEKAAVLThreadMessage@@I@Z
LTRESULT LThreadQueue::GetMessage(LThreadMessage &msg, LTBOOL bWait)
{
	// Do we have a message?
	if(WaitForSingleObject(m_hMsgEvent, (bWait) ? INFINITE : 0) == WAIT_TIMEOUT)
		return LT_NOTFOUND;

	// Get the message
	if(!PopMessage(&msg))
		return LT_NOTFOUND;

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0044c810 ?PeekMessageA@LThreadQueue@@QAEKAAVLThreadMessage@@@Z
LTRESULT LThreadQueue::PeekMessage(LThreadMessage &msg)
{
	LThreadMessage *pMsg;

	if(WaitForSingleObject(m_hMsgEvent, 0) == WAIT_TIMEOUT)
	{
		return LT_NOTFOUND;
	}

	// Protect our message access
	CSAccess cs(&m_MessageCS);

	// Jump out if the queue's actually empty
	if(m_Messages.IsEmpty())
		return LT_NOTFOUND;

	pMsg = m_Messages.GetHead();
	msg = *pMsg;

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0044c870
LTBOOL LThreadQueue::PopMessage(LThreadMessage *pMsg)
{
	// Don't do anything if there's nothing in the queue
	if(WaitForSingleObject(m_hMsgEvent, 0) == WAIT_TIMEOUT)
		return LTFALSE;

	// Protect our message access
	CSAccess cs(&m_MessageCS);

	// Reset the message event and skip out if there isn't a message
	if(m_Messages.IsEmpty())
	{
		ResetEvent(m_hMsgEvent);
		return LTFALSE;
	}

	// Get the head message
	LThreadMessage *pHeadMessage = m_Messages.GetHead();
	// Copy it out if necessary
	if(pMsg)
		*pMsg = *pHeadMessage;
	// Remove it from the list
	m_Messages.RemoveAt(pHeadMessage);

	// Remove it from the message bank
	g_MessageBankCS.Enter();
		g_LThreadMessageBank.Free(pHeadMessage);
	g_MessageBankCS.Leave();

	// Reset the message event if the queue's empty
	if(m_Messages.IsEmpty())
		ResetEvent(m_hMsgEvent);

	return LTTRUE;
}


// ------------------------------------------------------------------------------ //
// LThread functions.
// ------------------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0044c940
LThread::LThread()
{
	m_Handle = LTNULL;
	m_ThreadID = 0;
}


// FUNCTION: LITHTECH 0x0044c970
LThread::~LThread()
{
}


// FUNCTION: LITHTECH 0x0044c990
void LThread::Term()
{
	// Clear our message queues.
	m_Incoming.Clear();
	m_Outgoing.Clear();
}


// FUNCTION: LITHTECH 0x0044ca80
static DWORD WINAPI ThreadFn(void *pData)
{
	LThread *pThread = (LThread*)pData;

	pThread->ThreadRun();
	pThread->Term();
	return 0;
}


// FUNCTION: LITHTECH 0x0044caa0
static int GetWindowsPriority(int priority)
{
	if(priority == 0)
		return THREAD_PRIORITY_IDLE;
	else if(priority == 1)
		return THREAD_PRIORITY_LOWEST;
	else if(priority == 2)
		return THREAD_PRIORITY_BELOW_NORMAL;
	else if(priority == 3)
		return THREAD_PRIORITY_NORMAL;
	else if(priority == 4)
		return THREAD_PRIORITY_ABOVE_NORMAL;
	else
		return THREAD_PRIORITY_HIGHEST;
}


// FUNCTION: LITHTECH 0x0044c9b0
LTRESULT LThread::Start(int priority)
{
	if(m_Incoming.Init() != LT_OK || m_Outgoing.Init() != LT_OK)
	{
		m_Incoming.Clear();
		m_Outgoing.Clear();
		return LT_ERROR;
	}

	m_hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	m_Handle = CreateThread(NULL, 0, ThreadFn, this, CREATE_SUSPENDED, &m_ThreadID);
	if(!m_Handle)
	{
		RETURN_ERROR(1, LThread::Start, LT_ERROR);
	}

	SetThreadPriority(m_Handle, GetWindowsPriority(priority));
	ResumeThread(m_Handle);
	return LT_OK;
}


// FUNCTION: LITHTECH 0x0044cae0
LTRESULT LThread::Terminate(LTBOOL bWait)
{
	if(m_Handle)
	{
		SetEvent(m_hStopEvent);
		WaitForSingleObject(m_Handle, INFINITE);

		CloseHandle(m_Handle);
		CloseHandle(m_hStopEvent);
		m_Handle = LTNULL;
		m_hStopEvent = LTNULL;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0044cb30
void LThread::ThreadRun()
{
	LThreadMessage msg;
	HANDLE handles[2];

	handles[0] = m_hStopEvent;
	handles[1] = m_Incoming.m_hMsgEvent;

	// Wait for a message or the stop event.
	while(WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0)
	{
		if(m_Incoming.PeekMessage(msg) == LT_OK)
		{
			ProcessMessage(msg);
			m_Incoming.PopMessage(LTNULL);
		}
	}
}


void LThread::ProcessMessage(LThreadMessage &msg)
{
}


// Template code emitted here.
// FUNCTION: LITHTECH 0x0044cbb0 ?GenGetNext@?$CGLinkedList@PAVLThreadMessage@@@@UBEPAVLThreadMessage@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x0044cbe0 ?AllocVoid@?$ObjectBank@VLThreadMessage@@VNullCS@@@@UAEPAXXZ
// FUNCTION: LITHTECH 0x0044cc20 ?Term@?$ObjectBank@VLThreadMessage@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x0044cc40 ??_G?$ObjectBank@VLThreadMessage@@VNullCS@@@@UAEPAXI@Z
