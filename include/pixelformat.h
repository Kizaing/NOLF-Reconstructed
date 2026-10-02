// Pixel format (Jupiter shared/src/pixelformat.h), Talon layout recovered from lithtech.exe.
// Talon differences: no m_nBPP, no BPP_24, FMConvertRequest has m_Flags (CONVERT_OR) instead of
// a transparent color, and the PFormat accessors are out-of-line (defined in pixelformat.cpp).
#ifndef __PIXELFORMAT_H__
#define __PIXELFORMAT_H__

#ifndef __LTPVALUE_H__
#include "ltpvalue.h"
#endif

#ifndef __LTBASETYPES_H__
#include "ltbasetypes.h"
#endif

#ifndef __LTRECT_H__
#include "ltrect.h"
#endif

// 0-8 inclusive.
#define NUM_SCALE_TABLES    9

// Bits-per-pixel identifiers.
enum BPPIdent
{
	BPP_8P=0,       // 8 bit palettized
	BPP_8,          // 8 bit RGB
	BPP_16,
	BPP_32,
	BPP_S3TC_DXT1,
	BPP_S3TC_DXT3,
	BPP_S3TC_DXT5,
	BPP_32P,        // true color palette
	NUM_BIT_TYPES
};

// FMConvertRequest::m_Flags.
#define CONVERT_OR	(1<<0)	// OR the converted pixels into the destination.

// This is what is used to represent a color value in a particular format.
union GenericColor
{
	uint32	dwVal;
	uint16	wVal;
	uint8	bVal;
};

// A palette color.
struct RPaletteColor
{
	union
	{
		struct
		{
			unsigned char	a, r, g, b;
		} rgb;

		uint32 dword;
	};
};

typedef uint8 ScaleFrom8Table[256];

inline LTBOOL IsFormatCompressed(BPPIdent eType)
{
	return (eType == BPP_S3TC_DXT1) || (eType == BPP_S3TC_DXT3) || (eType == BPP_S3TC_DXT5);
}

// Pixel format.  (size 0x38)
class PFormat
{
public:

	// (virtual so renderers don't have to link it in..)
	virtual void	Init(BPPIdent Type, uint32 aMask, uint32 rMask, uint32 gMask, uint32 bMask);

	void		InitPValueFormat();

	BPPIdent	GetType() const;
	uint32		GetBytesPerPixelShift() const;	// log2(bytes per pixel)
	uint32		GetBytesPerPixel() const;
	LTBOOL		IsCompressed() const	{ return IsFormatCompressed(m_eType); }

	LTBOOL		IsSameFormat(PFormat *pOther) const;

public:
	BPPIdent	m_eType;							// 0x04

	uint32		m_Masks[NUM_COLORPLANES];			// 0x08
	uint32		m_nBits[NUM_COLORPLANES];			// 0x18 Bit counts for each plane.
	uint32		m_FirstBits[NUM_COLORPLANES];		// 0x28 Tells at which bit the color plane starts.
};


// Passed to FormatMgr::ConvertPixels.  (size 0x98)
class FMConvertRequest
{
public:
	FMConvertRequest();

	LTBOOL		IsValid() const;

public:
	PFormat			*m_pSrcFormat;		// 0x00
	uint8			*m_pSrc;			// 0x04
	long			m_SrcPitch;			// 0x08
	RPaletteColor	*m_pSrcPalette;		// 0x0C

	PFormat			*m_pDestFormat;		// 0x10
	uint8			*m_pDest;			// 0x14
	long			m_DestPitch;		// 0x18

	uint32			m_Width;			// 0x1C
	uint32			m_Height;			// 0x20

	uint32			m_Flags;			// 0x24 CONVERT_ flags.

public:
	PFormat		m_DefaultSrcFormat;		// 0x28
	PFormat		m_DefaultDestFormat;	// 0x60
};


// Passed to FormatMgr::FillRect.  (size 0x58)
class FMRectRequest
{
public:
	FMRectRequest();

	LTBOOL		IsValid();

public:
	PFormat		*m_pDestFormat;		// 0x00
	uint8		*m_pDest;			// 0x04
	long		m_DestPitch;		// 0x08

	// Does NOT include the right and bottom pixels.
	LTRect		m_Rect;				// 0x0C
	PValue		m_Color;			// 0x1C

public:
	PFormat		m_DefaultFormat;	// 0x20
};


class FormatMgr
{
public:
	FormatMgr();

	LTRESULT	ConvertPixels(const FMConvertRequest *pRequest);
	LTRESULT	FillRect(FMRectRequest *pRequest);

	LTRESULT	PValueToFormatColor(PFormat *pFormat, PValue in, GenericColor &out);
	LTRESULT	PValueFromFormatColor(PFormat *pFormat, GenericColor in, PValue &out);

protected:
	void		InitScaleTables();

public:
	PFormat		m_32BitFormat;									// 0x000
	PFormat		m_RGB565Format;									// 0x038

	ScaleFrom8Table	m_ScaleFrom8[NUM_SCALE_TABLES];				// 0x070
	uint8		*m_ScaleTo8[NUM_SCALE_TABLES];					// 0x970

	uint8		m_0to8[(1<<0)];									// 0x994
	uint8		m_1to8[(1<<1)];
	uint8		m_2to8[(1<<2)];
	uint8		m_3to8[(1<<3)];
	uint8		m_4to8[(1<<4)];
	uint8		m_5to8[(1<<5)];
	uint8		m_6to8[(1<<6)];
	uint8		m_7to8[(1<<7)];
	uint8		m_8to8[(1<<8)];
};


uint32 CalcImageSize(BPPIdent bpp, uint32 width, uint32 height);

#endif
