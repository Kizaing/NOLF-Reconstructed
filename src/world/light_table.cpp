// Jupiter runtime/world/src/light_table.cpp. Talon's table is a flat grid of LTRGB samples
// over the world box (Jupiter's is a compressed per-block grid).
// FLAGS: /O2 /GX-
#include "bdefs.h"
#include "de_mainworld.h"
#include "de_memory.h"


// FUNCTION: LITHTECH 0x00444ad0
CLightTable::CLightTable()
{
	Reset();
}


// FUNCTION: LITHTECH 0x00444ae0
CLightTable::~CLightTable()
{
	FreeAll();
}


// FUNCTION: LITHTECH 0x00444af0
void CLightTable::InitLightTable(const LTVector *pMin, const LTVector *pMax, float res)
{
	m_BlockSize.Init(res, res, res);
	m_InvBlockSize.x = 1.0f / m_BlockSize.x;
	m_InvBlockSize.y = 1.0f / m_BlockSize.y;
	m_InvBlockSize.z = 1.0f / m_BlockSize.z;

	m_Dims[0] = (uint32)((pMax->x - pMin->x) * m_InvBlockSize.x) + 1;
	m_Dims[1] = (uint32)((pMax->y - pMin->y) * m_InvBlockSize.y) + 1;
	m_Dims[2] = (uint32)((pMax->z - pMin->z) * m_InvBlockSize.z) + 1;

	if(m_Dims[0] == 0)
		m_Dims[0] = 1;
	if(m_Dims[1] == 0)
		m_Dims[1] = 1;
	if(m_Dims[2] == 0)
		m_Dims[2] = 1;

	m_DimsMinus1[0] = m_Dims[0] - 1;
	m_DimsMinus1[1] = m_Dims[1] - 1;
	m_DimsMinus1[2] = m_Dims[2] - 1;

	m_XSizeTimesYSize = m_Dims[0] * m_Dims[1];
	m_nData = m_Dims[0] * m_Dims[1] * m_Dims[2];

	m_LookupStart = *pMin;
}


// FUNCTION: LITHTECH 0x00444be0
void CLightTable::Reset()
{
	m_pData = LTNULL;
	m_nData = 0;
	m_Dims[0] = m_Dims[1] = m_Dims[2] = 0;
	m_DimsMinus1[0] = m_DimsMinus1[1] = m_DimsMinus1[2] = 0;
	m_BlockSize.Init();
	m_InvBlockSize.Init();
	m_LookupStart.Init();
}


// FUNCTION: LITHTECH 0x00444c20
void CLightTable::FreeAll()
{
	dfree(m_pData);
	Reset();
}
