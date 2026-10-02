// PCX loading (Jupiter runtime/kernel/src/sys/win/load_pcx.h), Talon layout.
#ifndef __LOAD_PCX_H__
#define __LOAD_PCX_H__

#ifndef __ILTSTREAM_H__
#include "iltstream.h"
#endif

#ifndef __PIXELFORMAT_H__
#include "pixelformat.h"
#endif


// LoadedBitmap::LoadedBitmap (0x00446170) constructs m_Data with Clear() alone, with no call to
// CMoArray::Init, unlike every other user of CMoArray<uint8> (e.g. packet_Get). load_pcx.obj was
// evidently built against an older StdLith dynarray.h, mirrored here. It can't be combined with
// ltdynarray.h in one unit.
#ifdef __MODYNARRAY_H__
#error load_pcx.h mirrors an older CMoArray; it cannot be combined with ltdynarray.h
#endif
#define __MODYNARRAY_H__

#include "bdefs.h"

class DefaultCache
{
public:
	uint32	GetCacheSize() const		{return m_CacheSize;}
	void	SetCacheSize(uint32 val)	{m_CacheSize = val;}
	uint32	GetWantedCache() const		{return m_WantedCache;}
	void	SetWantedCache(uint32 val)	{m_WantedCache = val;}

private:
	uint32	m_CacheSize;
	uint32	m_WantedCache;
};

// (size 0x14: vtable, m_pArray, m_nElements, m_Cache)
template<class T, class C=DefaultCache>
class CMoArray
{
public:
					CMoArray()			{ Clear(); }

	// The GenList interface lives in the vtable; it's not needed here.
	virtual uint32	GenGetSize() const;

	LTBOOL			SetSize(uint32 newSize)		{ return SetSize2(newSize, &g_DefAlloc); }
	LTBOOL			SetSize2(uint32 newSize, LAlloc *pAlloc);

	uint32			GetSize() const		{ return m_nElements; }
	T*				GetArray()			{ return m_pArray; }

	void	Clear()
	{
		m_pArray = 0;
		m_Cache.SetWantedCache(0);
		m_Cache.SetCacheSize(0);
		m_nElements = 0;
	}

	T		*m_pArray;		// 0x04
	uint32	m_nElements;	// 0x08
	C		m_Cache;		// 0x0C
};


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
