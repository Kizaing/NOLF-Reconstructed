// d3d.ren fixed-point / rounding helpers.  Only the members the decompiled code uses; Jupiter's descendant is
// runtime/render_a/src/sys/d3d/FixedPoint.h.
#ifndef __D3DREN_FIXEDPOINT_H__
#define __D3DREN_FIXEDPOINT_H__

// NAME: RoundFloatToInt: Jupiter FixedPoint.h, same body (`fld f; fistp nResult` inline asm: the exe's inlined copies
// are `fstp [argtemp]; fld [argtemp]; fistp [result]`, Ghidra's ROUND()), used for 8-bit colour channels exactly as in
// Jupiter's drawlinesystem.cpp.
inline int RoundFloatToInt(float f)
{
	int nResult;
	__asm
	{
		fld f
		fistp nResult
	}
	return nResult;
}

#endif
