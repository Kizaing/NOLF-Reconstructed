// d3d.ren shared/geomroutines (0x10031e94-0x10032031): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// one surviving engine function.
// FLAGS: /O1 /Ob2
#include <windows.h>
#include "bdefs.h"
#include "de_world.h"
#include "de_objects.h"
#include "world_tree.h"
#include "fullintersectline.h"
#include "geomroutines.h"
#include "d3dren/d3ddevice.h"
#include "d3dren/common_stuff.h"
extern LTObject *g_pIntersection;
extern float g_IntersectionBestDistSqr;
extern LTPlane g_IntersectionPlane;
extern LTVector g_IntersectionPos;
extern HPOLY g_hWorldPoly;
#define DO_PLANE_TEST_X(planeCoord, coord0, coord1, coord2, normalDirection) \
    t = (planeCoord - Point1.coord0) / (Point2.coord0 - Point1.coord0);\
    testCoords[0] = Point1.coord1 + ((Point2.coord1 - Point1.coord1) * t);\
    if (testCoords[0] > pServerObj->m_MinBox.coord1 && testCoords[0] < pServerObj->m_MaxBox.coord1)\
    {\
        testCoords[1] = Point1.coord2 + ((Point2.coord2 - Point1.coord2) * t);\
        if (testCoords[1] > pServerObj->m_MinBox.coord2 && testCoords[1] < pServerObj->m_MaxBox.coord2)\
        {\
            pIntersectPt->coord0 = planeCoord;\
            pIntersectPt->coord1 = testCoords[0];\
            pIntersectPt->coord2 = testCoords[1];\
            pIntersectPlane->m_Normal.x = normalDirection;\
            pIntersectPlane->m_Normal.y = 0.0f;\
            pIntersectPlane->m_Normal.z = 0.0f;\
            pIntersectPlane->m_Dist = pServerObj->m_MinBox.x * normalDirection;\
            return true;\
        }\
    }

#define DO_PLANE_TEST_Y(planeCoord, coord0, coord1, coord2, normalDirection) \
    t = (planeCoord - Point1.coord0) / (Point2.coord0 - Point1.coord0);\
    testCoords[0] = Point1.coord1 + ((Point2.coord1 - Point1.coord1) * t);\
    if (testCoords[0] > pServerObj->m_MinBox.coord1 && testCoords[0] < pServerObj->m_MaxBox.coord1)\
    {\
        testCoords[1] = Point1.coord2 + ((Point2.coord2 - Point1.coord2) * t);\
        if (testCoords[1] > pServerObj->m_MinBox.coord2 && testCoords[1] < pServerObj->m_MaxBox.coord2)\
        {\
            pIntersectPt->coord0 = planeCoord;\
            pIntersectPt->coord1 = testCoords[0];\
            pIntersectPt->coord2 = testCoords[1];\
            pIntersectPlane->m_Normal.x = 0.0f;\
            pIntersectPlane->m_Normal.y = normalDirection;\
            pIntersectPlane->m_Normal.z = 0.0f;\
            pIntersectPlane->m_Dist = pServerObj->m_MinBox.y * normalDirection;\
            return true;\
        }\
    }

#define DO_PLANE_TEST_Z(planeCoord, coord0, coord1, coord2, normalDirection) \
    t = (planeCoord - Point1.coord0) / (Point2.coord0 - Point1.coord0);\
    testCoords[0] = Point1.coord1 + ((Point2.coord1 - Point1.coord1) * t);\
    if (testCoords[0] > pServerObj->m_MinBox.coord1 && testCoords[0] < pServerObj->m_MaxBox.coord1)\
    {\
        testCoords[1] = Point1.coord2 + ((Point2.coord2 - Point1.coord2) * t);\
        if (testCoords[1] > pServerObj->m_MinBox.coord2 && testCoords[1] < pServerObj->m_MaxBox.coord2)\
        {\
            pIntersectPt->coord0 = planeCoord;\
            pIntersectPt->coord1 = testCoords[0];\
            pIntersectPt->coord2 = testCoords[1];\
            pIntersectPlane->m_Normal.x = 0.0f;\
            pIntersectPlane->m_Normal.y = 0.0f;\
            pIntersectPlane->m_Normal.z = normalDirection;\
            pIntersectPlane->m_Dist = pServerObj->m_MinBox.z * normalDirection;\
            return true;\
        }\
    }


// Just sets up the current 'closest object'.
#define USE_THIS_OBJECT(pServerObj, distSqr, plane, intersectionPt, hPoly) \
    g_IntersectionBestDistSqr = distSqr;\
    g_pIntersection = pServerObj;\
    g_IntersectionPlane = plane;\
    g_IntersectionPos = intersectionPt;\
    g_hWorldPoly = hPoly;


// ---------------------------------------------------------------------------------------------------------------------------
// geomroutines.cpp (engine twin src/shared/geomroutines.cpp) and intersect_line.cpp (engine twin src/world/intersect_line.cpp)
#include <math.h>

// FUNCTION: D3DREN 0x10031e94
void gr_GetPerpendicularVector(LTVector *pVec, LTVector *pRef, LTVector *pPerp)
{
	float dot, t;
	LTVector temp, tempRef;

	if(!pRef)
	{
		tempRef.Init(0, 1, 0);
		pRef = &tempRef;
	}

	*pPerp = *pRef;

	// Are pRef and pVec the same?  If not, we can exit.
	dot = pVec->Dot(*pPerp);
	if(dot > 0.99f || dot < -0.99f)
	{
		// Try to modify it as little as possible.
		pPerp->z += 5.0f;
		pPerp->Norm();
		dot = VEC_DOT(*pVec, *pPerp);
		if(dot > 0.99f || dot < -0.99f)
		{
			pPerp->x += 5.0f;
			pPerp->y += 5.0f;
			pPerp->Norm();

			dot = pVec->Dot(*pPerp);
			if(dot > 0.99f || dot < -0.99f)
			{
				pPerp->x += 5.0f;
				pPerp->y -= 2.0f;
				pPerp->z -= 5.0f;
				pPerp->Norm();
			}
		}
	}

	// Make pVec and pPerp linear independent.
	t = -pVec->Dot(*pPerp);
	temp = *pVec * t;
	*pPerp += temp;
	pPerp->Norm();
}


#define INTERSECT_EPSILON	0.01f

#define FrontSide	1
#define BackSide	0

// d3d_surface.h declares a class with a static member whose definition consumes a static-initialiser number: included here, after
// the static initialisers of the first object, so that their `_$E` numbers stay those of the exe.
#include "d3dren/d3d_surface.h"		// D3DShadowTextureFactory, IShadowTexture

// ---- dynamic lightmap console variables -----------------------------------------------------------------------------------------
#include "d3dren/rendererconsolevars.h"

// ---- dynamic lights on the lightmaps (LMDynamic) ----------------------------------------------------------------------------------
#include "pixelformat.h"
#include "ltmatrix.h"
#include "d3dren/tlvertex.h"
#include "d3dren/lightmap.h"
