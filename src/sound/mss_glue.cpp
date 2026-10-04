#include <windows.h>
#include <stdlib.h>

typedef long S32;

// mss32.lib takes the address of the import thunk when registering shutdown.
extern "C" void __stdcall AIL_shutdown(void);

// FUNCTION: LITHTECH 0x004afc20
extern "C" S32 MSS_auto_cleanup(void)
{
	atexit((void (__cdecl *)(void))AIL_shutdown);
	return 0;
}
