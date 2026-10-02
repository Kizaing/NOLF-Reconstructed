// Jupiter runtime/sound/src/sounddata.cpp, Talon version (CSoundData is declared in soundtrack.h).
#include <windows.h>
#include "bdefs.h"
#include "soundtrack.h"
#include "iltstream.h"
#include "wave.h"


// FUNCTION: LITHTECH 0x0048fea0
LTBOOL CSoundData::Init(UsedFile *pFile, ILTStream *pStream, uint32 dwFileSize)
{
	CWaveHeader waveHeader;

	if (!pFile || !pStream || dwFileSize == 0)
		return LTFALSE;

	Term();

	m_pFile = pFile;

	if (!GetWaveInfo(*pStream, waveHeader))
	{
		return LTFALSE;
	}

	m_fDuration = ((float)waveHeader.m_dwDataSize / (float)waveHeader.m_WaveFormat.nAvgBytesPerSec);

	// Inflate it a little so we don't tell the client to stop a sound premuturely due to lag...
	m_fDuration += 0.1f;

	// Mark this file as needed
	m_bTouched = LTFALSE;

	m_dwFlags = 0;

	return LTTRUE;
}

// FUNCTION: LITHTECH 0x0048ff30
void CSoundData::Term()
{
	m_fDuration = 0.0f;
	m_dwFlags = 0;
}
