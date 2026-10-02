// Bruce Dawson's exception handler (Game Programming Gems 2, "ExceptionHandler.h"), Talon's copy
// in exceptionhandler.cpp. Include <windows.h> first.
#ifndef __EXCEPTIONHANDLER_H__
#define __EXCEPTIONHANDLER_H__

// Writes crashlog.txt for an unhandled exception. Use it in an __except() filter.
int __cdecl RecordExceptionInfo(PEXCEPTION_POINTERS data, const char *Message);	// 0x00436270

#endif  // __EXCEPTIONHANDLER_H__
