// Wave file parsing (Talon layout recovered from lithtech.exe; Jupiter runtime/sound/src/wave.h).
#ifndef __WAVE_H__
#define __WAVE_H__

#include <windows.h>
#include <mmsystem.h>
#include "ltbasedefs.h"

class ILTStream;

#ifndef WAVE_FORMAT_IMA_ADPCM
#define WAVE_FORMAT_IMA_ADPCM	0x0011
#endif

#define WAVE_FORMAT_MPEGLAYER3	85

// Defines used with WAVEHEADER_lith::m_dwSoundBufferFlags
#define	SOUNDBUFFERFLAG_STREAM				(1<<0)		// Buffer used for streaming sound
#define SOUNDBUFFERFLAG_DECOMPRESSONLOAD	(1<<1)		// Decompress the file when loaded.
#define SOUNDBUFFERFLAG_DECOMPRESSATSTART	(1<<2)		// Decompress the whole when first played.

struct WAVEHEADER_cuepoint
{
	uint32		m_dwName;
	uint32		m_dwPosition;
	FOURCC		m_fccChunk;
	uint32		m_dwChunkStart;
	uint32		m_dwBlockStart;
	uint32		m_dwSampleOffset;
};

struct WAVEHEADER_ltxt
{
	uint32		m_dwName;
	uint32		m_dwSampleLength;
	uint32		m_dwPurpose;
	uint16		m_wCountry;
	uint16		m_wLanguage;
	uint16		m_wDialect;
	uint16		m_wCodePage;
};

struct WAVEHEADER_lith
{
	uint32		m_dwSoundBufferFlags;
	float		m_fPitchMod;
};

struct CCuePoint
{
	LTBOOL		m_bValid;
	uint32		m_dwName;
	uint32		m_dwPosition;
	uint32		m_dwLength;
};

// 0x38 bytes.
struct CWaveHeader
{
	WAVEFORMATEX		m_WaveFormat;	// 0x00
	uint32				m_dwDataPos;	// 0x14
	uint32				m_dwDataSize;	// 0x18
	uint32				m_dwSamples;	// 0x1c
	WAVEHEADER_lith		m_lith;			// 0x20
	CCuePoint			m_CuePoint;		// 0x28
};

LTBOOL GetWaveInfo(ILTStream &inStream, CWaveHeader &waveHeader);

#endif  // __WAVE_H__
