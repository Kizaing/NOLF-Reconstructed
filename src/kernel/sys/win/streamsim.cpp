// Jupiter runtime/kernel/src/sys/win/streamsim.cpp
// The unit starts at 00496cd0 (streamsim_Open), after the sprite control functions.
// Talon has no AbstractIO or Windows file handle streams, and SSBufStream lives here.
// The Release()s (0x00463340), SSMemStream::GetLen and SSBufStream::GetPos were folded into
// identical code elsewhere.
#include <stdio.h>
#include <string.h>
#include "bdefs.h"
#include "genltstream.h"
#include "ltdynarray.h"
#include "streamsim.h"


// ----------------------------------------------------------------------------- //
// Structures.
// ----------------------------------------------------------------------------- //

class SSFile : public CGenLTStream
{
public:

			SSFile()
			{
				m_bError = LTFALSE;
				m_pFile = LTNULL;
			}

			~SSFile()
			{
				if(m_pFile)
					fclose(m_pFile);
			}

	void	Release()
	{
		delete this;
	}

	LTRESULT	Read(void *pData, uint32 size)
	{
		size_t ret;

		if(!m_pFile)
			return LT_ERROR;

		if(size == 0)
		{
			return LT_OK;
		}
		else
		{
			ret = fread(pData, 1, size, m_pFile);
			if(ret == size)
			{
				return LT_OK;
			}
			else
			{
				memset(pData, 0, size);
				m_bError = TRUE;
				return LT_ERROR;
			}
		}
	}

	LTRESULT Write(const void *pData, uint32 size)
	{
		size_t ret;

		if(!m_pFile)
			return LT_ERROR;

		if(size == 0)
		{
			return LT_OK;
		}
		else
		{
			ret = fwrite(pData, 1, size, m_pFile);
			if(ret == size)
			{
				return LT_OK;
			}
			else
			{
				m_bError = TRUE;
				return LT_ERROR;
			}
		}
	}

	LTRESULT	ErrorStatus()
	{
		return m_bError ? LT_ERROR : LT_OK;
	}

	LTRESULT	SeekTo(uint32 offset)
	{
		if(!m_pFile)
			return LT_ERROR;

		if(fseek(m_pFile, offset, SEEK_SET) == 0)
		{
			return LT_OK;
		}
		else
		{
			return LT_ERROR;
		}
	}

	LTRESULT	GetPos(uint32 *offset)
	{
		long theOffset;

		if(!m_pFile)
		{
			*offset = 0;
			return LT_ERROR;
		}

		theOffset = ftell(m_pFile);
		*offset = (unsigned long)theOffset;
		return (theOffset == -1) ? LT_ERROR : LT_OK;
	}

	LTRESULT	GetLen(uint32 *len)
	{
		long curPos;

		if(!m_pFile)
		{
			*len = 0;
			return LT_ERROR;
		}

		curPos = ftell(m_pFile);
		fseek(m_pFile, 0, SEEK_END);
		*len = (unsigned long)ftell(m_pFile);
		fseek(m_pFile, curPos, SEEK_SET);
		return LT_OK;
	}

	FILE	*m_pFile;		// 0x04
	LTBOOL	m_bError;		// 0x08
};


class SSMemStream : public CGenLTStream
{
// Overrides.
public:

				SSMemStream(uint32 cacheSize)
				{
					m_Buffer.SetCacheSize(cacheSize);
					m_Pos = 0;
					m_bError = LTFALSE;
				}

	LTRESULT	Read(void *pData, uint32 dataLen)
	{
		// Prevent an unneeded assertion...
		if(dataLen == 0)
			return LT_OK;

		if((m_Pos+dataLen) > m_Buffer.GetSize())
		{
			m_Pos = m_Buffer.GetSize();
			m_bError = TRUE;
			memset(pData, 0, dataLen);
			return LT_ERROR;
		}

		memcpy(pData, &m_Buffer[m_Pos], dataLen);
		m_Pos += dataLen;
		return LT_OK;
	}

	LTRESULT	Write(const void *pData, uint32 dataLen)
	{
		LTRESULT dResult;

		// Prevent an unneeded assertion...
		if(dataLen == 0)
			return LT_OK;

		dResult = Size(m_Pos+dataLen);
		if(dResult != LT_OK)
			return dResult;

		memcpy(&m_Buffer[m_Pos], pData, dataLen);
		m_Pos += dataLen;
		return LT_OK;
	}

	LTRESULT	ErrorStatus()
	{
		return m_bError ? LT_ERROR : LT_OK;
	}

	LTRESULT	SeekTo(uint32 offset)
	{
		if(offset > m_Buffer.GetSize())
			return LT_ERROR;

		m_Pos = offset;
		return LT_OK;
	}

	LTRESULT	GetPos(uint32 *offset)
	{
		*offset = m_Pos;
		return LT_OK;
	}

	LTRESULT	GetLen(uint32 *len)
	{
		*len = m_Buffer.GetSize();
		return LT_OK;
	}

	void			Release()
	{
		delete this;
	}

// Helpers.
public:

	LTRESULT			Size(uint32 size)
	{
		if(m_Buffer.GetSize() < size)
		{
			return m_Buffer.Fast_NiceSetSize(size) ? LT_OK : LT_ERROR;
		}
		else
		{
			return LT_OK;
		}
	}

	CMoArray<uint8>	m_Buffer;	// 0x04
	uint32			m_Pos;		// 0x18
	LTBOOL			m_bError;	// 0x1c
};


class SSBufStream : public CGenLTStream
{
// Overrides.
public:
	SSBufStream(uint8 *pData, uint32 dwDataSize)
	{
		m_pData = pData;
		m_dwDataSize = dwDataSize;
		m_Pos = 0;
		m_bError = LTFALSE;
	}

	LTRESULT Read(void *pData, uint32 dataLen)
	{
		uint32 dwReadEnd;

		// Prevent an unneeded assertion...
		if (dataLen == 0)
		{
			return LT_OK;
		}

		dwReadEnd = m_Pos + dataLen;
		if (dwReadEnd > m_dwDataSize)
		{
			m_Pos = m_dwDataSize;
			m_bError = LTTRUE;
			memset(pData, 0, dataLen);
			return LT_ERROR;
		}

		memcpy(pData, &m_pData[m_Pos], dataLen);
		m_Pos += dataLen;
		return LT_OK;
	}

	LTRESULT Write(const void *pData, uint32 dataLen)
	{
		LTRESULT dResult;

		// Prevent an unneeded assertion...
		if (dataLen == 0)
		{
			return LT_OK;
		}

		dResult = Size(m_Pos+dataLen);
		if (dResult != LT_OK)
		{
			return dResult;
		}

		memcpy(&m_pData[m_Pos], pData, dataLen);
		m_Pos += dataLen;
		return LT_OK;
	}

	LTRESULT ErrorStatus()
	{
		return m_bError ? LT_ERROR : LT_OK;
	}

	LTRESULT SeekTo(uint32 offset)
	{
		if (offset > m_dwDataSize)
			return LT_ERROR;

		m_Pos = offset;
		return LT_OK;
	}

	LTRESULT GetPos(uint32 *offset)
	{
		*offset = m_Pos;
		return LT_OK;
	}

	LTRESULT GetLen(uint32 *len)
	{
		*len = m_dwDataSize;
		return LT_OK;
	}

	void Release()
	{
		delete this;
	}

// Helpers.
public:
	LTRESULT Size(uint32 size)
	{
		if (size > m_dwDataSize)
		{
			return LT_ERROR;
		}

		return LT_OK;
	}

	uint8		*m_pData;		// 0x04
	uint32		m_Pos;			// 0x08
	uint32		m_dwDataSize;	// 0x0c
	LTBOOL		m_bError;		// 0x10
};


// ----------------------------------------------------------------------------- //
// Main interface functions.
// ----------------------------------------------------------------------------- //

// FUNCTION: LITHTECH 0x00496cd0
ILTStream* streamsim_Open(const char *pFilename, const char *pAccess)
{
	FILE *fp;
	SSFile *pFile;

	fp = fopen(pFilename, pAccess);
	if(!fp)
		return 0;

	pFile = new SSFile;
	if(pFile)
		pFile->m_pFile = fp;

	return pFile;
}


// FUNCTION: LITHTECH 0x00496ef0
ILTStream* streamsim_OpenMemStream(uint32 cacheSize)
{
	return new SSMemStream(cacheSize);
}


// FUNCTION: LITHTECH 0x00497170
ILTStream* streamsim_MemStreamFromFile(char *pFilename)
{
	FILE *fp;
	SSMemStream *pRet;
	long len, amtRead;

	pRet = LTNULL;
	fp = fopen(pFilename, "rb");
	if(fp)
	{
		fseek(fp, 0, SEEK_END);
		len = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		pRet = new SSMemStream(256);
		if(pRet->m_Buffer.SetSize(len))
		{
			amtRead = fread(pRet->m_Buffer.GetArray(), 1, len, fp);
			if(amtRead != len)
			{
				delete pRet;
				pRet = LTNULL;
			}
		}
		else
		{
			delete pRet;
			pRet = LTNULL;
		}

		fclose(fp);
	}

	return pRet;
}


// FUNCTION: LITHTECH 0x004972a0
ILTStream* streamsim_MemStreamFromBuffer( uint8 *pData, uint32 dwDataSize )
{
	return new SSBufStream( pData, dwDataSize );
}


// Virtual members emitted here.
// FUNCTION: LITHTECH 0x00496d20 ?Read@SSFile@@UAEKPAXK@Z
// FUNCTION: LITHTECH 0x00496d80 ?Write@SSFile@@UAEKPBXK@Z
// FUNCTION: LITHTECH 0x00496dd0 ?ErrorStatus@SSFile@@UAEKXZ
// FUNCTION: LITHTECH 0x00496de0 ?SeekTo@SSFile@@UAEKK@Z
// FUNCTION: LITHTECH 0x00496e10 ?GetPos@SSFile@@UAEKPAK@Z
// FUNCTION: LITHTECH 0x00496e50 ?GetLen@SSFile@@UAEKPAK@Z
// FUNCTION: LITHTECH 0x00496eb0 ??_GSSFile@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x00496f60 ?Read@SSMemStream@@UAEKPAXK@Z
// FUNCTION: LITHTECH 0x00496fd0 ?Write@SSMemStream@@UAEKPBXK@Z
// FUNCTION: LITHTECH 0x004970d0 ?ErrorStatus@SSMemStream@@UAEKXZ
// FUNCTION: LITHTECH 0x004970e0 ?SeekTo@SSMemStream@@UAEKK@Z
// FUNCTION: LITHTECH 0x00497100 ?GetPos@SSMemStream@@UAEKPAK@Z
// FUNCTION: LITHTECH 0x00497110 ??_GSSMemStream@@UAEPAXI@Z
// FUNCTION: LITHTECH 0x004972d0 ?Read@SSBufStream@@UAEKPAXK@Z
// FUNCTION: LITHTECH 0x00497340 ?Write@SSBufStream@@UAEKPBXK@Z
// FUNCTION: LITHTECH 0x00497390 ?ErrorStatus@SSBufStream@@UAEKXZ
// FUNCTION: LITHTECH 0x004973a0 ?SeekTo@SSBufStream@@UAEKK@Z
// FUNCTION: LITHTECH 0x004973c0 ?GetLen@SSBufStream@@UAEKPAK@Z
