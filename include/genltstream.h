// Jupiter runtime/shared/src/genltstream.h
// CGenLTStream implements the generic parts of ILTStream on top of Read/Write.
// Talon's ILTStream (SDK iltstream.h) takes a non-const char* in WriteString.
#ifndef __GENLTSTREAM_H__
#define __GENLTSTREAM_H__

#include "ltbasedefs.h"
#include "iltstream.h"

class CGenLTStream : public ILTStream
{
public:
	virtual LTRESULT	ReadString(char *pStr, uint32 maxBytes);
	virtual LTRESULT	WriteString(char *pStr);
	virtual LTRESULT	WriteStream(ILTStream &dsSource, uint32 dwMin, uint32 dwMax);
};

#endif
