// Jupiter runtime/client/src/consolecommands.cpp
// NOTE: in Talon this unit really starts around 0x00422220 (static init of g_ClientConIterator at
// 0x00422240, then the console command handlers in a region Ghidra never disassembled); only the
// tail from con_ListCommands on is covered here.
#include <string.h>
#include "concommand.h"
#include "console.h"
#include "consolecommands.h"
#include "dhashtable.h"

void* dalloc(size_t size);
void dfree(void *ptr);

// From engine_vars.cpp.
extern LTEngineVar* GetEngineVars();
extern int GetNumEngineVars();

// The console tables (static data in this file, not reconstructed yet).
// GLOBAL: LITHTECH 0x004d09b8
extern LTSaveFn g_SaveFns[2];
#define NUM_SAVEFNS (sizeof(g_SaveFns) / sizeof(g_SaveFns[0]))

// GLOBAL: LITHTECH 0x004d09c0
extern LTCommandStruct g_LTCommandStructs[36];
#define NUM_COMMANDSTRUCTS (sizeof(g_LTCommandStructs) / sizeof(g_LTCommandStructs[0]))


// The main console state.
// GLOBAL: LITHTECH 0x004e3378
ConsoleState g_ClientConsoleState;

// The client console iterator
//	Note : This iterator is forward only
class CClientConIterator : public CConIterator
{
protected:
	enum EState {
		STATE_COMMAND = 0,
		STATE_CONVAR = 1,
		STATE_ENGINEVAR = 2
	};
	EState m_eState;				// 0x04
	int m_iCommandIndex;			// 0x08
	HHashIterator *m_hVarIndex;		// 0x0c
	HHashElement *m_hCurVar;		// 0x10
	int m_iEngineIndex;				// 0x14

	virtual LTBOOL Begin();
	virtual LTBOOL NextItem();

public:
	CClientConIterator();
	virtual ~CClientConIterator();

	virtual const char *Get() const;
};

// GLOBAL: LITHTECH 0x004e33bc
CClientConIterator	g_ClientConIterator;


// The command handlers are static in the original and referenced from g_LTCommandStructs; they are
// extern here so the compiler keeps them while the table isn't reconstructed.

//------------------------------------------------------------------
// FUNCTION: LITHTECH 0x00423560
void con_ListCommands(int argc, char *argv[])
{
	int i;

	for(i=0; i < g_ClientConsoleState.m_nCommandStructs; i++)
	{
		con_WhitePrintf(g_ClientConsoleState.m_pCommandStructs[i].pCmdName);
	}
}


//------------------------------------------------------------------
// FUNCTION: LITHTECH 0x00423590
void con_Set(int argc, char *argv[])
{
	LTCommandVar *pCurVar;
	HHashIterator *hIterator;
	HHashElement *hElement;
	hIterator = hs_GetFirstElement( g_ClientConsoleState.m_VarHash );
	while(hIterator)
	{
		hElement = hs_GetNextElement(hIterator);
		if( !hElement )
			continue;

		pCurVar = ( LTCommandVar * )hs_GetElementUserData( hElement );
		cc_PrintVarDescription(&g_ClientConsoleState, pCurVar);
	}
}


//------------------------------------------------------------------
// The engine's global operator new/delete live at these addresses (the linker folded identical
// functions, so they double as the console state's Alloc/Free).
// FUNCTION: LITHTECH 0x004235e0 ??2@YAPAXI@Z
void* operator new(size_t size)
{
	return dalloc(size);
}

// FUNCTION: LITHTECH 0x004235f0 ??3@YAXPAX@Z
void operator delete(void *ptr)
{
	dfree(ptr);
}


//------------------------------------------------------------------
//------------------------------------------------------------------
// Main parsing / command handler
//------------------------------------------------------------------
//------------------------------------------------------------------

// FUNCTION: LITHTECH 0x00423600
void c_InitConsoleCommands()
{
	memset(&g_ClientConsoleState, 0, sizeof(g_ClientConsoleState));

	g_ClientConsoleState.m_SaveFns = g_SaveFns;
	g_ClientConsoleState.m_nSaveFns = NUM_SAVEFNS;

	g_ClientConsoleState.m_pEngineVars = GetEngineVars();
	g_ClientConsoleState.m_nEngineVars = GetNumEngineVars();

	g_ClientConsoleState.m_pCommandStructs = g_LTCommandStructs;
	g_ClientConsoleState.m_nCommandStructs = NUM_COMMANDSTRUCTS;

	g_ClientConsoleState.ConsolePrint = con_WhitePrintf;

	g_ClientConsoleState.Alloc = operator new;
	g_ClientConsoleState.Free = operator delete;

	cc_InitState(&g_ClientConsoleState);

	// Set up the completion iterator
	GETCONSOLE()->SetCompletionIterator( &g_ClientConIterator );
}


// FUNCTION: LITHTECH 0x00423690
void c_TermConsoleCommands()
{
	cc_TermState(&g_ClientConsoleState);
}


// FUNCTION: LITHTECH 0x004236a0
void c_CommandHandler(const char *pCommand)
{
	cc_HandleCommand(&g_ClientConsoleState, pCommand);
}



// FUNCTION: LITHTECH 0x004236c0
CClientConIterator::CClientConIterator() :
	m_eState(STATE_COMMAND),
	m_iCommandIndex(0),
	m_hVarIndex(0),
	m_hCurVar(0),
	m_iEngineIndex(0)
{
}

// Compiler-generated scalar deleting destructor.
// FUNCTION: LITHTECH 0x004236e0 ??_GCClientConIterator@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00423700
CClientConIterator::~CClientConIterator()
{
	// Nothing to destruct...
}

// FUNCTION: LITHTECH 0x00423710
LTBOOL CClientConIterator::Begin()
{
	// Go to the beginning of the command list
	m_eState = STATE_COMMAND;
	m_iCommandIndex = 0;

	// This isn't guaranteed to be true, but I'm pretty sure it will be...
	return LTTRUE;
}

// FUNCTION: LITHTECH 0x00423720
LTBOOL CClientConIterator::NextItem()
{
	LTBOOL bEnd = LTFALSE;

	switch (m_eState)
	{
		case STATE_COMMAND :
		{
			// Move to the next command
			m_iCommandIndex++;
			// Overflow to the variable list
			if ( m_iCommandIndex >= g_ClientConsoleState.m_nCommandStructs )
			{
				// Go to variable mode
				m_eState = STATE_CONVAR;
				m_hVarIndex = 0;
				bEnd = !NextItem();
			}
			break;
		}
		case STATE_CONVAR :
		{
			if (!m_hVarIndex)
			{
				m_hVarIndex = hs_GetFirstElement( g_ClientConsoleState.m_VarHash );
				m_hCurVar = LTNULL;
			}

			// find the next variable
			do {
				m_hCurVar = hs_GetNextElement(m_hVarIndex);
			} while ( m_hVarIndex && !m_hCurVar );

			// Overflow to the engine variable list
			if (!m_hVarIndex)
			{
				m_eState = STATE_ENGINEVAR;
				m_iEngineIndex = -1;
				bEnd = !NextItem();
			}
			break;
		}
		case STATE_ENGINEVAR :
		{
			if (m_iCommandIndex >= GetNumEngineVars())
			{
				// Nothing to overflow to, so we're at the end
				bEnd = LTTRUE;
			}
			else
			{
				m_iEngineIndex++;
			}
		}
	}

	return !bEnd;
}

// FUNCTION: LITHTECH 0x00423810
const char *CClientConIterator::Get() const
{
	const char *pResult = LTNULL;

	switch (m_eState)
	{
		// Get a command
		case STATE_COMMAND :
		{
			// Make sure we're not past the end of the list
			if ( m_iCommandIndex < g_ClientConsoleState.m_nCommandStructs )
				pResult = g_ClientConsoleState.m_pCommandStructs[m_iCommandIndex].pCmdName;
			break;
		}
		// Get a console variable
		case STATE_CONVAR :
		{
			// Make sure we're not past the end of the list
			if ( m_hCurVar )
				pResult = (( LTCommandVar * )hs_GetElementUserData( m_hCurVar ))->pVarName;
			break;
		}
		// Get an engine variable
		case STATE_ENGINEVAR :
		{
			// Make sure we're not past the end of the list
			if ( m_iEngineIndex < GetNumEngineVars() )
				pResult = GetEngineVars()[m_iEngineIndex].pVarName;
			break;
		}
	}

	return pResult;
}
