// Jupiter runtime/shared/src/lightmap_planes.cpp (Talon has no SetupLMPlaneVectors).
// FLAGS: /O2 /GX-
#include "bdefs.h"


class LMPlane
{
public:
	LMPlane(LTVector inP, LTVector inQ, LTVector inNormal);

	LTVector	P, Q, Normal;
};

#define NUM_LMPLANES	6


// Principal planes the lightmap planes come from.
// FUNCTION: LITHTECH 0x00444c40 _$E2
// FUNCTION: LITHTECH 0x00444c50 _$E1
// GLOBAL: LITHTECH 0x004e4318
LMPlane g_LMPlanes[NUM_LMPLANES] =
{
	LMPlane(LTVector(1.0f, 0.0f, 0.0f), LTVector(0.0f, 0.0f, -1.0f), LTVector(0.0f, 1.0f, 0.0f)),
	LMPlane(LTVector(1.0f, 0.0f, 0.0f), LTVector(0.0f, 0.0f, 1.0f), LTVector(0.0f, -1.0f, 0.0f)),
	LMPlane(LTVector(1.0f, 0.0f, 0.0f), LTVector(0.0f, 1.0f, 0.0f), LTVector(0.0f, 0.0f, 1.0f)),
	LMPlane(LTVector(1.0f, 0.0f, 0.0f), LTVector(0.0f, -1.0f, 0.0f), LTVector(0.0f, 0.0f, -1.0f)),
	LMPlane(LTVector(0.0f, 0.0f, 1.0f), LTVector(0.0f, -1.0f, 0.0f), LTVector(1.0f, 0.0f, 0.0f)),
	LMPlane(LTVector(0.0f, 0.0f, -1.0f), LTVector(0.0f, -1.0f, 0.0f), LTVector(-1.0f, 0.0f, 0.0f))
};



// FUNCTION: LITHTECH 0x00445010
LMPlane::LMPlane(LTVector inP, LTVector inQ, LTVector inNormal)
{
	P = inP;
	Q = inQ;
	Normal = inNormal;
}


// FUNCTION: LITHTECH 0x00445060
uint32 SelectLMPlaneVector(LTVector vNormal)
{
	uint32 i, iBestPlane;
	float fTest, fBestDot;

	// Pick a lightmap plane.
	iBestPlane = 0;
	fBestDot = -2.0f;
	for(i=0; i < NUM_LMPLANES; i++)
	{
		fTest = g_LMPlanes[i].Normal.Dot(vNormal);
		if(fTest > fBestDot)
		{
			fBestDot = fTest;
			iBestPlane = i;
		}
	}

	return iBestPlane;
}
