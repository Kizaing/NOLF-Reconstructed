// Jupiter runtime/kernel/src/sys/win/load_pcx.cpp, Talon version.
// Talon differences: the RLE data is read from the stream a byte at a time (no whole-file
// buffer), ILTStream::GetLen takes an out parameter, and there's no TGA loader here.
#include "ltbasedefs.h"
#include "load_pcx.h"


// FUNCTION: LITHTECH 0x00446170
LoadedBitmap::LoadedBitmap()
{
	Term();
}

// FUNCTION: LITHTECH 0x004461b0
void LoadedBitmap::Term()
{
	m_Width = 0;
	m_Height = 0;
	m_Pitch = 0;
}


// FUNCTION: LITHTECH 0x004461d0
LTBOOL pcx_Create2(ILTStream *pStream, LoadedBitmap *pBitmap)
{
	REZ_PCXHDR pcxHdr;
	uint8 *pDestBuffer, *pDestPos, *pDestLine;
	int width, height;
	int bytesLeft, nInSpan, i, y;
	unsigned char temp;
	uint32 curPos, bytesPerPixel, planeBytesLeft;
	int iPlane, nImageBytes;
	PFormat format;
	uint8 tempPal[768];
	uint8 *pBytesStart, *pBytesEnd;


	pBitmap->Term();

	// Read in the header.
	STREAM_READ(pcxHdr);
	if(pcxHdr.bitsPerPixel != 8)
	{
		return LTFALSE;
	}

	// Palettized or 24 bit?
	if(pcxHdr.planes != 1 && pcxHdr.planes != 3)
	{
		return LTFALSE;
	}

	// Figure out what format we'll be using.
	if(pcxHdr.planes == 1)
	{
		format.Init(BPP_8P, 0, 0, 0, 0);
	}
	else if(pcxHdr.planes == 3)
	{
		format.InitPValueFormat();
	}

	bytesPerPixel = format.GetBytesPerPixel();


	// Try to create a surface.
	width = (pcxHdr.x1 - pcxHdr.x0 + 1);
	height = (pcxHdr.y1 - pcxHdr.y0 + 1);

	nImageBytes = pcxHdr.bytesPerLine * height * bytesPerPixel;
	if(!pBitmap->m_Data.SetSize(nImageBytes))
		return LTFALSE;

	pBytesStart = pBitmap->m_Data.GetArray();
	pBytesEnd = pBytesStart + nImageBytes;

	pBitmap->m_Width = width;
	pBitmap->m_Height = height;
	pBitmap->m_Pitch = pcxHdr.bytesPerLine * bytesPerPixel;
	pBitmap->m_Format = format;

	// Read in the palette.
	pStream->GetPos(&curPos);
	{	// (own scope: VC6 shares len's stack slot with y)
		uint32 len;
		pStream->GetLen(&len);
		pStream->SeekTo(len - 768);
	}
	pStream->Read(tempPal, sizeof(tempPal));
	pStream->SeekTo(curPos);

	for(i=0; i < 256; i++)
	{
		pBitmap->m_Palette[i].rgb.a = 0;
		pBitmap->m_Palette[i].rgb.r = tempPal[i*3];
		pBitmap->m_Palette[i].rgb.g = tempPal[i*3+1];
		pBitmap->m_Palette[i].rgb.b = tempPal[i*3+2];
	}

	// Uncompress!
	pDestBuffer = pBitmap->m_Data.GetArray();
	if(pDestBuffer)
	{
		for(y=0; y < height; y++)
		{
			pDestLine = &pDestBuffer[y*pBitmap->m_Pitch];
			pDestPos = pDestLine;

			iPlane = 0;
			bytesLeft = pcxHdr.bytesPerLine * pcxHdr.planes;
			planeBytesLeft = pcxHdr.bytesPerLine;
			while(bytesLeft > 0)
			{
				STREAM_READ(temp);

				if((temp & 0xC0) == 0xC0)
				{
					nInSpan = temp & 0x3F;

					// Read the pixel.
					STREAM_READ(temp);
				}
				else
				{
					nInSpan = 1;
				}

				// Check boundaries..
				if(nInSpan > bytesLeft)
				{
					pBitmap->Term();
					return LTFALSE;
				}

				if(pBitmap->m_Format.m_eType == BPP_8P)
				{
					for(i=0; i < nInSpan; i++)
					{
						if(pDestPos >= pBytesStart && pDestPos <= pBytesEnd)
						{
							*pDestPos = temp;
						}

						pDestPos++;
						--bytesLeft;
					}
				}
				else
				{
					for(i=0; i < nInSpan; i++)
					{
						PValue &theValue = *((PValue*)pDestPos);

						if(pDestPos >= pBytesStart && pDestPos <= pBytesEnd)
						{
							if(iPlane == 0)
								theValue = PValue_Set(0, temp, PValue_GetG(theValue), PValue_GetB(theValue));
							else if(iPlane == 1)
								theValue = PValue_Set(0, PValue_GetR(theValue), temp, PValue_GetB(theValue));
							else if(iPlane == 2)
								theValue = PValue_Set(0, PValue_GetR(theValue), PValue_GetG(theValue), temp);
						}

						pDestPos += bytesPerPixel;
						--bytesLeft;
						--planeBytesLeft;
						if(planeBytesLeft == 0)
						{
							++iPlane;
							pDestPos = pDestLine;
							planeBytesLeft = pcxHdr.bytesPerLine;
						}
					}
				}
			}
		}
	}

	if(pStream->ErrorStatus() != LT_OK)
	{
		pBitmap->Term();
		return LTFALSE;
	}

	return LTTRUE;
}
