// PCX loading (Jupiter runtime/kernel/src/sys/win/load_pcx.h), Talon layout.
#ifndef __LOAD_PCX_H__
#define __LOAD_PCX_H__

#ifndef __ILTSTREAM_H__
#include "iltstream.h"
#endif

#ifndef __PIXELFORMAT_H__
#include "pixelformat.h"
#endif


// The stock StdLith CMoArray. LoadedBitmap's constructor (0x00446170) inlines CMoArray::Init
// completely: Term()/SetSize(0) fold away on the freshly cleared array, leaving only the stores
// (m_pArray, wanted cache, cache size, m_nElements). Other callers run out of inline budget and
// call Init or SetSize2 out of line.
#include "ltdynarray.h"
#include "bdefs.h"


// PCX header structures.
struct REZ_PCXRGB
{
	uint8 r, g, b;
};

struct REZ_PCXHDR
{
	int8			manufacturer;
	int8			version;
	int8			encoding;
	uint8			bitsPerPixel;
	int16			x0, y0, x1, y1;
	int16			xDPI, yDPI;
	REZ_PCXRGB		pal16[16];
	int8			reserved;
	int8			planes;
	int16			bytesPerLine;
	int16			paletteInfo;
	int16			xScreenSize, yScreenSize;
	int8			filler[54];
};


// This is a PCX file loaded into memory.  (size 0x458)
class LoadedBitmap
{
public:

					LoadedBitmap();
	void			Term();

	uint8&			Pixel(uint32 x, uint32 y) {return m_Data.GetArray()[y*m_Pitch+x];}

	PFormat			m_Format;			// 0x000 Tells what format the loaded PCX was.
	RPaletteColor	m_Palette[256];		// 0x038

	unsigned long	m_Width;			// 0x438
	unsigned long	m_Height;			// 0x43C
	unsigned long	m_Pitch;			// 0x440
	CMoArray<uint8>	m_Data;				// 0x444
};


LoadedBitmap* pcx_Create(ILTStream *pStream);
LTBOOL pcx_Create2(ILTStream *pStream, LoadedBitmap *pBitmap);
void pcx_Destroy(LoadedBitmap *pBitmap);


#endif  // __LOAD_PCX_H__
