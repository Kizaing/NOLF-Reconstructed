// Jupiter runtime/client/src/polygrid.cpp
// Talon has no valid-vertex mask or PolyGrid flags: pg_Init takes a bHalfTriangles flag
// (ILTClient::SetupPolyGrid) and builds one or two triangles per grid square.
#include "bdefs.h"
#include "de_objects.h"
#include "de_memory.h"

LTBOOL pg_Init(LTPolyGrid *pGrid, uint32 width, uint32 height, LTBOOL bHalfTriangles);
void pg_Term(LTPolyGrid *pGrid);


// FUNCTION: LITHTECH 0x0046dc60
LTBOOL pg_Init(LTPolyGrid *pGrid, uint32 width, uint32 height, LTBOOL bHalfTriangles)
{
	uint32 xSize, ySize, x, y;
	uint16 *pIndexPos;
	uint32 upperLeftIndex;
	uint32 nTrisPerSquare;

	pg_Term(pGrid);

	if(width < 2 || height < 2)
		return LTFALSE;
	else if(width*height > 65000)
		return LTFALSE;

	pGrid->m_Data = (char*)dalloc_z(width*height);

	// Setup the index list.
	xSize = width - 1;
	ySize = height - 1;

	nTrisPerSquare = bHalfTriangles ? 1 : 2;
	pGrid->m_nTris = xSize * ySize * nTrisPerSquare;
	pGrid->m_nIndices = pGrid->m_nTris * 3;
	pGrid->m_Indices = (uint16*)dalloc(sizeof(uint16) * pGrid->m_nIndices);

	for(y=0; y < ySize; y++)
	{
		pIndexPos = &pGrid->m_Indices[y * xSize * 3 * nTrisPerSquare];
		upperLeftIndex = y * width;

		for(x=0; x < xSize; x++)
		{
			*pIndexPos++ = (uint16)(upperLeftIndex+width+1);
			*pIndexPos++ = (uint16)(upperLeftIndex+1);
			*pIndexPos++ = (uint16)(upperLeftIndex);

			if(!bHalfTriangles)
			{
				*pIndexPos++ = (uint16)(upperLeftIndex+width);
				*pIndexPos++ = (uint16)(upperLeftIndex+width+1);
				*pIndexPos++ = (uint16)(upperLeftIndex);
			}

			++upperLeftIndex;
		}
	}

	pGrid->m_Width		= width;
	pGrid->m_Height		= height;

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0046dda0
void pg_Term(LTPolyGrid *pGrid)
{
	if(pGrid->m_Data)
	{
		dfree(pGrid->m_Data);
		pGrid->m_Data = NULL;
	}

	if(pGrid->m_Indices)
	{
		dfree(pGrid->m_Indices);
		pGrid->m_Indices = NULL;
	}
}
