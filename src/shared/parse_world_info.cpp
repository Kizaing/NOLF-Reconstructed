// Jupiter runtime/shared/src/parse_world_info.cpp
#include <stdlib.h>
#include "parse_world_info.h"

// FUNCTION: LITHTECH 0x00469590
LTBOOL ParseAmbientLight(char *pStr, LTVector *pAmbient)
{
	ConParse parse;

	parse.Init(pStr);
	pAmbient->Init();

	if(parse.ParseFind("AmbientLight", LTFALSE, 4))
	{
		// <AmbientLight> <r> <g> <b>
		// colors are 0-255
		pAmbient->x = (float)atof(parse.m_Args[1]);
		pAmbient->y = (float)atof(parse.m_Args[2]);
		pAmbient->z = (float)atof(parse.m_Args[3]);
		VEC_CLAMP(*pAmbient, 0.0f, 255.0f);
		return LTTRUE;
	}

	return LTFALSE;
}

// Not in Jupiter. Name taken from the "LightTableRes" key.
// FUNCTION: LITHTECH 0x004696c0
float ParseLightTableRes(char *pStr)
{
	ConParse parse;

	parse.Init(pStr);
	if(parse.ParseFind("LightTableRes", LTFALSE, 2))
	{
		return (float)atof(parse.m_Args[1]);
	}

	return 250.0f;
}
