// d3d.ren: Talon's static light as the model lighting reads it (the object that world_tree.h's FindObjInfo returns for the
// NOA_Lights array).  Owner: package W4 (unit seed/w4_light, later unk/100098d0).
//
// NAME: StaticLight and its members (m_Link, m_Pos, m_Radius, m_Color, m_Dir, m_FOV, m_OuterColor): the engine decomp's
// local definition in src/world/de_mainworld.cpp (the offsets 0x5c..0x94 are the ones d3d.ren's StaticLightCB reads);
// WTObj_Light: same file.  The engine keeps both in its .cpp, so the renderer needs its own declaration.
#ifndef __D3DREN_STATICLIGHT_H__
#define __D3DREN_STATICLIGHT_H__

#include "world_tree.h"

#define WTObj_Light		1

class StaticLight : public WorldTreeObj
{
public:
	LTLink			m_Link;				// 0x5c in MainWorld::m_StaticLights
	LTVector		m_Pos;				// 0x68 Position of the light
	float			m_Radius;			// 0x74 Maximum radius of the light
	LTVector		m_Color;			// 0x78 0-255
	LTVector		m_Dir;				// 0x84 Normalized direction vector for directional lights
	float			m_FOV;				// 0x90 cos(fov/2)  -1 for omnidirectional lights
	LTVector		m_OuterColor;		// 0x94
};

#endif
