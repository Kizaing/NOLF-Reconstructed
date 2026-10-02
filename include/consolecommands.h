// Client console commands (Jupiter client/src/consolecommands.h).
#ifndef __CONSOLECOMMANDS_H__
#define __CONSOLECOMMANDS_H__

#include "concommand.h"

extern ConsoleState g_ClientConsoleState;

void c_InitConsoleCommands();
void c_TermConsoleCommands();
void c_CommandHandler(const char *pCommand);

#endif
