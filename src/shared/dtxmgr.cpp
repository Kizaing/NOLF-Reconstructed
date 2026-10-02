// Jupiter runtime/shared/src/dtxmgr.cpp, Talon version (closer to Jupiter's libs/dtxmgr):
// TextureData keeps the DTX sections and optional per-mipmap alpha masks, and dtx_Create can
// skip the image data or load the sections.
#include "bdefs.h"
#include <string.h>
#include "dtxmgr.h"
#include "pixelformat.h"
#include "de_memory.h"


// --------------------------------------------------------------------------------- //
// TextureMipData.
// --------------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x004352d0
TextureMipData::TextureMipData()
{
	m_Width = 0;
	m_Height = 0;
	m_Pitch = 0;
	m_Data = LTNULL;
	m_AlphaMask = LTNULL;
	m_AlphaPitch = 0;
}


// --------------------------------------------------------------------------------- //
// TextureData functions.
// --------------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x004352f0
TextureData::TextureData()
{
	m_pDataBuffer = LTNULL;
}

// FUNCTION: LITHTECH 0x00435330
TextureData::~TextureData()
{
	uint32 i;
	DtxSection *pCur, *pNext;

	if(m_Header.m_IFlags & DTX_MIPSALLOCED)
	{
		for(i=0; i < m_Header.m_nMipmaps; i++)
		{
			if(m_Mips[i].m_Data)
				dfree(m_Mips[i].m_Data);
		}
	}

	if(m_pDataBuffer)
		delete [] m_pDataBuffer;

	// Free the sections.
	pCur = m_pSections;
	while(pCur)
	{
		pNext = pCur->m_pNext;
		dfree(pCur);
		pCur = pNext;
	}
}

// FUNCTION: LITHTECH 0x004353a0
void TextureData::SetupPFormat(PFormat *pFormat)
{
	dtx_SetupDTXFormat2(m_Header.GetBPPIdent(), pFormat);
}


// --------------------------------------------------------------------------------- //
// dtx_ functions.
// --------------------------------------------------------------------------------- //

static void dtx_ReadOrSkip(LTBOOL bSkip, ILTStream *pStream, void *pData, uint32 dataLen);

// FUNCTION: LITHTECH 0x004353e0
TextureData* dtx_Alloc(BPPIdent bpp, uint32 baseWidth, uint32 baseHeight, uint32 nMipmaps,
	uint32 *pAllocSize, uint32 *pTextureDataSize, LTBOOL bAlphaMasks)
{
	TextureData *pRet;
	TextureMipData *pMip;
	uint32 i, size, width, height;
	uint32 textureDataSize;
	uint8 *pOutData;

	textureDataSize = 0;
	width = baseWidth;
	height = baseHeight;
	for(i=0; i < nMipmaps; i++)
	{
		size = CalcImageSize(bpp, width, height);
		if(bAlphaMasks)
			size += width * height;

		textureDataSize += size;

		width >>= 1;
		height >>= 1;
	}

	pRet = new TextureData;
	if(!pRet)
		return LTNULL;

	pRet->m_pDataBuffer = new uint8[textureDataSize];
	if(!pRet->m_pDataBuffer)
	{
		delete pRet;
		return LTNULL;
	}

	pRet->m_ResHeader.m_Type = LT_RESTYPE_DTX;
	pRet->m_Header.m_ResType = LT_RESTYPE_DTX;
	pRet->m_Header.m_IFlags = DTX_SECTIONSFIXED;
	pRet->m_Header.m_UserFlags = 0;
	pRet->m_Header.m_nMipmaps = (uint16)nMipmaps;
	pRet->m_Header.m_BaseWidth = (uint16)baseWidth;
	pRet->m_Header.m_BaseHeight = (uint16)baseHeight;
	pRet->m_Header.m_Version = CURRENT_DTX_VERSION;
	pRet->m_Header.m_CommandString[0] = 0;

	pRet->m_AllocSize = sizeof(TextureData) + textureDataSize;
	pRet->m_Link.m_pData = pRet;
	pRet->m_pSections = LTNULL;
	pRet->m_Header.m_nSections = 0;
	pRet->m_Header.m_ExtraLong[0] = pRet->m_Header.m_ExtraLong[1] = pRet->m_Header.m_ExtraLong[2] = 0;
	pRet->m_Header.SetBPPIdent(bpp);
	pRet->m_pSharedTexture = LTNULL;
	pRet->m_Flags2 = 0;

	// Setup the mipmap structures.
	pOutData = pRet->m_pDataBuffer;

	width = baseWidth;
	height = baseHeight;
	for(i=0; i < nMipmaps; i++)
	{
		pMip = &pRet->m_Mips[i];

		pMip->m_Width = width;
		pMip->m_Height = height;
		pMip->m_Data = pOutData;

		if(bpp == BPP_32)
			pMip->m_Pitch = (int32)width * sizeof(uint32);
		else if(bpp == BPP_32P)
			pMip->m_Pitch = width;
		else
			pMip->m_Pitch = 0;

		pOutData += CalcImageSize(bpp, width, height);

		if(bAlphaMasks)
		{
			pMip->m_AlphaMask = pOutData;
			pMip->m_AlphaPitch = width;
			pOutData += width * height;
		}

		width >>= 1;
		height >>= 1;
	}

	if(pAllocSize)
		*pAllocSize = pRet->m_AllocSize;

	if(pTextureDataSize)
		*pTextureDataSize = textureDataSize;

	return pRet;
}

// FUNCTION: LITHTECH 0x004355b0
LTRESULT dtx_Create(ILTStream *pStream, TextureData **ppOut, LTBOOL bLoadSections, LTBOOL bSkipImageData)
{
	DtxHeader hdr;
	TextureData *pRet;
	uint32 i, allocSize, textureDataSize;
	SectionHeader sectionHeader;
	DtxSection *pSection;
	uint32 iMipmap;
	TextureMipData *pMip;
	uint32 y, size;

	STREAM_READ(hdr);
	if(hdr.m_ResType != LT_RESTYPE_DTX)
	{
		RETURN_ERROR(1, dtx_Create, LT_INVALIDDATA);
	}

	// Correct version and valid data?
	if(hdr.m_Version != CURRENT_DTX_VERSION)
	{
		RETURN_ERROR(1, dtx_Create, LT_INVALIDVERSION);
	}

	if(hdr.m_nMipmaps == 0 || hdr.m_nMipmaps > 15)
	{
		RETURN_ERROR(1, dtx_Create, LT_INVALIDDATA);
	}

	// Allocate it.
	pRet = dtx_Alloc(hdr.GetBPPIdent(), hdr.m_BaseWidth, hdr.m_BaseHeight, hdr.m_nMipmaps,
		&allocSize, &textureDataSize);
	if(!pRet)
	{
		RETURN_ERROR(1, dtx_Create, LT_OUTOFMEMORY);
	}

	pRet->m_pSharedTexture = LTNULL;
	memcpy(&pRet->m_Header, &hdr, sizeof(DtxHeader));

	// Read in mipmap data.
	for(iMipmap=0; iMipmap < hdr.m_nMipmaps; iMipmap++)
	{
		pMip = &pRet->m_Mips[iMipmap];

		if(hdr.GetBPPIdent() == BPP_32)
		{
			for(y=0; y < pMip->m_Height; y++)
			{
				// Read the line.
				dtx_ReadOrSkip(bSkipImageData, pStream,
					&pMip->m_Data[y*pMip->m_Pitch],
					pMip->m_Width * sizeof(uint32));
			}
		}
		else
		{
			size = CalcImageSize(hdr.GetBPPIdent(), pMip->m_Width, pMip->m_Height);
			dtx_ReadOrSkip(bSkipImageData, pStream, pMip->m_Data, size);
		}
	}

	// Read in the sections?
	if(hdr.m_IFlags & DTX_SECTIONSFIXED)
	{
		if(bLoadSections)
		{
			for(i=0; i < hdr.m_nSections; i++)
			{
				STREAM_READ(sectionHeader);
				pSection = (DtxSection*)dalloc((sizeof(DtxSection)-1) + sectionHeader.m_DataLen);
				memcpy(&pSection->m_Header, &sectionHeader, sizeof(pSection->m_Header));
				pStream->Read(pSection->m_Data, sectionHeader.m_DataLen);

				pSection->m_pNext = pRet->m_pSections;
				pRet->m_pSections = pSection;
			}
		}
	}
	else
	{
		// Fix it...
		pRet->m_Header.m_IFlags |= DTX_SECTIONSFIXED;
		pRet->m_Header.m_nSections = 0;
	}

	// Check the error status.
	if(pStream->ErrorStatus() != LT_OK)
	{
		dtx_Destroy(pRet);
		RETURN_ERROR(1, dtx_Create, LT_INVALIDDATA);
	}

	*ppOut = pRet;
	return LT_OK;
}

// Based on the value of bSkip, it either memset()s the memory to 0 or reads it from the file.
// FUNCTION: LITHTECH 0x004358c0
static void dtx_ReadOrSkip(LTBOOL bSkip, ILTStream *pStream, void *pData, uint32 dataLen)
{
	uint32 pos;

	if(bSkip)
	{
		memset(pData, 0, dataLen);
		pStream->GetPos(&pos);
		pStream->SeekTo(pos + dataLen);
	}
	else
	{
		pStream->Read(pData, dataLen);
	}
}

// FUNCTION: LITHTECH 0x00435920
void dtx_Destroy(TextureData *pData)
{
	if(pData)
	{
		delete pData;
	}
}

// FUNCTION: LITHTECH 0x00435940
void dtx_SetupDTXFormat2(BPPIdent bpp, PFormat *pFormat)
{
	if(bpp == BPP_32)
	{
		pFormat->InitPValueFormat();
	}
	else
	{
		pFormat->Init(bpp, 0, 0, 0, 0);
	}
}
