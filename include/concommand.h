// Console command/variable state (Talon layout recovered from lithtech.exe; Jupiter shared/src/concommand.h).
#ifndef __CONCOMMAND_H__
#define __CONCOMMAND_H__

#include <stdio.h>
#include "ltbasedefs.h"

struct HHashElement;
struct HHashTable;
struct ConsoleState;

// Command flags.
#define CMD_USERCOMMAND			(1<<0)	// User command (not an engine command).

// Flags to cc_HandleCommand2.
#define CC_NOVARS		(1<<0)	// Don't set variables.
#define CC_NOCOMMANDS	(1<<1)	// Don't run commands.

#define VARFLAG_SAVE	(1<<0)	// Save in cc_SaveConfigFile.

#define VARBUF_LEN		16

typedef void (*LTCommandFn)(int argc, char **argv);
typedef void (*LTSaveFn)(FILE *fp);

// This is a general variable that can be added/changed thru the console.
struct LTCommandVar
{
	char			m_Buffer[VARBUF_LEN];	// 0x00 This is used instead of allocating if possible.
	char			*pVarName;				// 0x10
	char			*pStringVal;			// 0x14
	float			floatVal;				// 0x18
	HHashElement	*hElement;				// 0x1c
	uint32			m_VarFlags;				// 0x20 Combination of VARFLAG_ stuff.
};

// Engine vars are command vars bound to the address of a global.
struct LTEngineVar
{
	const char		*pVarName;				// 0x00
	float			*pValueAddressFloat;	// 0x04
	int32			*pValueAddressLong;		// 0x08
	LTCommandVar	**pCommandVarAddress;	// 0x0c
	char			**pValueAddressString;	// 0x10
};

// Only used in cc_InitState to register a list of commands.
struct LTCommandStruct
{
	const char		*pCmdName;				// 0x00
	LTCommandFn		fn;						// 0x04
	uint32			flags;					// 0x08
};

// These are used for 'extra' commands registered with cc_AddCommand.
struct LTExtraCommandStruct
{
	const char	*pCmdName;		// 0x00
	LTCommandFn	fn;				// 0x04
	LTLink		link;			// 0x08
	uint32		flags;			// 0x14
};

// A console state is a global state for all your variables and functions (0x44 bytes).
struct ConsoleState
{
	LTSaveFn		*m_SaveFns;				// 0x00
	int				m_nSaveFns;				// 0x04
	LTEngineVar		*m_pEngineVars;			// 0x08
	int				m_nEngineVars;			// 0x0c
	LTCommandStruct	*m_pCommandStructs;		// 0x10
	int				m_nCommandStructs;		// 0x14
	LTLink			m_ExtraCommands;		// 0x18
	void			(*ConsolePrint)(const char *pMsg, ...);					// 0x24
	void*			(*Alloc)(size_t size);									// 0x28
	void			(*Free)(void *ptr);										// 0x2c
	void			(*NewVar)(ConsoleState *pState, LTCommandVar *pVar);	// 0x30
	void			(*VarChange)(ConsoleState *pState, LTCommandVar *pVar);	// 0x34
	uint32			m_Unknown38;			// 0x38 (not in Jupiter)
	HHashTable		*m_VarHash;				// 0x3c
	HHashTable		*m_StringHash;			// 0x40
};

void cc_InitState(ConsoleState *pState);
void cc_TermState(ConsoleState *pState);

LTExtraCommandStruct* cc_AddCommand(ConsoleState *pState, const char *pCmdName, LTCommandFn fn, uint32 flags);
void cc_RemoveCommand(ConsoleState *pState, LTExtraCommandStruct *pCommand);
LTExtraCommandStruct* cc_FindCommand(ConsoleState *pState, const char *pName);

void cc_HandleCommand(ConsoleState *pState, const char *pCommand);
void cc_HandleCommand2(ConsoleState *pState, const char *pCommand, uint32 flags);
void cc_HandleCommand3(ConsoleState *pState, const char *pCommand, uint32 flags, uint32 varFlags);

void cc_SetConsoleVariable(ConsoleState *pState, const char *pName, const char *pValue);
LTCommandVar* cc_FindConsoleVar(ConsoleState *pState, const char *pName);

LTBOOL cc_RunConfigFile(ConsoleState *pState, const char *pFilename, uint32 flags, uint32 varFlags);
LTBOOL cc_SaveConfigFile(ConsoleState *pState, const char *pFilename);
void cc_PrintVarDescription(ConsoleState *pState, LTCommandVar *pVar);

#endif
