// Client sound buffers (Talon layout recovered from lithtech.exe; Jupiter runtime/sound/src/soundbuffer.h).
// Talon calls Miles (mss.h) directly instead of going through ILTSoundSys.
#ifndef __SOUNDBUFFER_H__
#define __SOUNDBUFFER_H__

#include "bdefs.h"
#include "mss.h"
#include "wave.h"
#include "de_file.h"
#include "../../build/proj/LT2/lithshared/stdlith/object_bank.h"

class CSoundInstance;
struct ClientFileMgr;


// FileIdentifier and cf_OpenFileIdent (0x004047f0).
#include "client_filemgr.h"

// 0x004972a0 (streamsim). Wraps a memory buffer in a stream.
ILTStream* streamsim_MemStreamFromBuffer(uint8 *pData, uint32 dataLen);

// Total bytes of loaded sound data (memorywatch.cpp).
extern unsigned long g_dwSoundMemory;


inline float GetRandom(float min, float max)
{
	float randNum = (float)rand() / RAND_MAX;
	float num = min + (max - min) * randNum;
	return num;
}


// 0xa4 bytes.
class CSoundBuffer
{
public:

	CSoundBuffer();

	virtual ~CSoundBuffer();

	virtual LTRESULT	Init(FileIdentifier &fileIdent);
	virtual LTRESULT	InitFromCompressed(CSoundBuffer &compressedSoundBuffer);
	virtual void		Term();

	uint8 *			GetFileData(LTBOOL bDelegate = LTTRUE) const
	{ return (bDelegate && m_pDecompressedSoundBuffer) ? m_pDecompressedSoundBuffer->m_pFileData : m_pFileData; }

	uint32			GetFileDataLen(LTBOOL bDelegate = LTTRUE) const
	{ return (bDelegate && m_pDecompressedSoundBuffer) ? m_pDecompressedSoundBuffer->m_dwFileSize : m_dwFileSize; }

	uint8 *			GetSoundData(LTBOOL bDelegate = LTTRUE) const
	{ return (bDelegate && m_pDecompressedSoundBuffer) ? m_pDecompressedSoundBuffer->m_pSoundData : m_pSoundData; }

	uint32			GetSoundDataLen(LTBOOL bDelegate = LTTRUE) const
	{ return (bDelegate && m_pDecompressedSoundBuffer) ? m_pDecompressedSoundBuffer->m_WaveHeader.m_dwDataSize : m_WaveHeader.m_dwDataSize; }

	uint32			GetSoundBufferFlags() const
	{ return m_WaveHeader.m_lith.m_dwSoundBufferFlags; }

	void			SetTouched(LTBOOL bTouched)
	{ m_bTouched = bTouched; }

	LTBOOL			IsTouched()
	{ return m_bTouched; }

	uint32			GetDuration() const
	{ return m_dwDuration; } // In milliseconds

	WAVEFORMATEX *	GetWaveFormat(LTBOOL bDelegate = LTTRUE)
	{ return (bDelegate && m_pDecompressedSoundBuffer) ? &m_pDecompressedSoundBuffer->m_WaveHeader.m_WaveFormat : &m_WaveHeader.m_WaveFormat; }

	float			RandomPitchMod()
	{ return GetRandom(-m_WaveHeader.m_lith.m_fPitchMod, m_WaveHeader.m_lith.m_fPitchMod); }

	S32				GetSampleType(LTBOOL bDelegate = LTTRUE)
	{ return (bDelegate && m_pDecompressedSoundBuffer) ? m_pDecompressedSoundBuffer->m_nSampleType : m_nSampleType; }

	S32				GetPlaybackRate(LTBOOL bDelegate = LTTRUE)
	{ return GetWaveFormat(bDelegate)->nSamplesPerSec; }

	const AILSOUNDINFO *GetSampleInfo(LTBOOL bDelegate = LTTRUE)
	{ return (bDelegate && m_pDecompressedSoundBuffer) ? &m_pDecompressedSoundBuffer->m_SoundInfo : &m_SoundInfo; }

	LTBOOL			IsCompressed()
	{ return (m_WaveHeader.m_WaveFormat.wFormatTag == WAVE_FORMAT_IMA_ADPCM || m_WaveHeader.m_WaveFormat.wFormatTag == WAVE_FORMAT_MPEGLAYER3); }

	CSoundBuffer *	GetDecompressedSoundBuffer()
	{ return m_pDecompressedSoundBuffer; }

	LTRESULT		DecompressData();

	const FileIdentifier *GetFileIdent() const
	{ return m_pFileIdent; }

	virtual LTRESULT	Unload();
	virtual LTRESULT	Reload();

	const LTLink *	GetLink() const
	{ return &m_Link; }

	const LTList *	GetInstanceList() const
	{ return &m_InstanceList; }

	LTRESULT		AddInstance(CSoundInstance &soundInstance);
	LTRESULT		RemoveInstance(CSoundInstance &soundInstance);
	LTBOOL			CanPlay(CSoundInstance &soundInstance);
	CSoundInstance	*GetLowestPriorityInstance();

	uint32			GetLoopPoint(int i)
	{ return m_dwLoopPoints[i]; }

	CWaveHeader	&	GetWaveHeader(LTBOOL bDelegate = LTTRUE)
	{
		if (bDelegate && m_pDecompressedSoundBuffer)
			return m_pDecompressedSoundBuffer->m_WaveHeader;
		else
			return m_WaveHeader;
	}

protected:

	LTRESULT			CopySoundData16to8(uint8 *pDest, uint8 *pSource, uint32 nSize);

	virtual LTRESULT	LoadData();

	LTRESULT			LoadDataFromDecompressed();

	void				CalcSampleType(S32 &sampleType, WAVEFORMATEX &waveFormat);

public:

	FileIdentifier	*m_pFileIdent;		// 0x04
	CWaveHeader		m_WaveHeader;		// 0x08
	LTBOOL			m_bTouched;			// 0x40
	uint8			*m_pFileData;		// 0x44
	uint32			m_dwFileSize;		// 0x48
	uint8			*m_pSoundData;		// 0x4c
	CSoundBuffer	*m_pDecompressedSoundBuffer;	// 0x50
	uint32			m_dwDuration;		// 0x54
	uint32			m_dwLoopPoints[2];	// 0x58
	S32				m_nSampleType;		// 0x60
	AILSOUNDINFO	m_SoundInfo;		// 0x64
	LTList			m_InstanceList;		// 0x88
	LTLink			m_Link;				// 0x98
};


#endif  // __SOUNDBUFFER_H__
