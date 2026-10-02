// Version resource (Jupiter runtime/kernel/src/sys/win/version_resource.h).
#ifndef __VERSION_RESOURCE_H__
#define __VERSION_RESOURCE_H__

#include <windows.h>
#include "ltbasedefs.h"
#include "version_info.h"

// Resource ID
#define VERSION_RESOURCE_ID	12346

LTRESULT GetLTExeVersion(HINSTANCE hInstance, LTVersionInfo &info);

#endif
