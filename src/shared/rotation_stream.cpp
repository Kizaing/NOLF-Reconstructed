// Talon's out-of-line rotation stream reader, used by model loading.
#include "bdefs.h"
#include "iltstream.h"

// FUNCTION: LITHTECH 0x0046ea70
void LTStream_Read(ILTStream *pStream, LTRotation &rot)
{
	pStream->Read(&rot.m_Quat[0], sizeof(float));
	pStream->Read(&rot.m_Quat[1], sizeof(float));
	pStream->Read(&rot.m_Quat[2], sizeof(float));
	pStream->Read(&rot.m_Quat[3], sizeof(float));
}
