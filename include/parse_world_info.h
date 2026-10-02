// World info string parsing (Jupiter runtime/shared/src/parse_world_info.h).
#ifndef __PARSE_WORLD_INFO_H__
#define __PARSE_WORLD_INFO_H__

#include "ltbasedefs.h"

LTBOOL ParseAmbientLight(char *pStr, LTVector *pAmbient);
float ParseLightTableRes(char *pStr);

#endif
