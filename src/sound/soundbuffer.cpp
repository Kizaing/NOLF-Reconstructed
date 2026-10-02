// Jupiter runtime/sound/src/soundbuffer.cpp, Talon version.
// Talon calls Miles directly (each call bracketed by AIL_lock/AIL_unlock), opens files through
// cf_OpenFileIdent(g_pClientMgr->m_hFileMgr) and reports through g_DebugLevel.
#include <windows.h>
#include <stddef.h>
#include "bdefs.h"
#include "clientmgr.h"
#include "soundmgr.h"
#include "soundbuffer.h"
#include "iltstream.h"


// Inline in the header in Talon; never inlined by the original.
// FUNCTION: LITHTECH 0x0048f250
CSoundBuffer::CSoundBuffer()
{
	m_pFileIdent = LTNULL;
	m_pSoundData = LTNULL;
	m_pDecompressedSoundBuffer = LTNULL;
	m_pFileData = LTNULL;
	m_dwFileSize = 0;
	m_nSampleType = DIG_F_MONO_8;
	dl_InitList(&m_InstanceList);
	m_bTouched = LTFALSE;
	m_dwLoopPoints[0] = m_dwLoopPoints[1] = 0;
	memset(&m_SoundInfo, 0, sizeof(m_SoundInfo));
}

// FUNCTION: LITHTECH 0x0048f2b0 ??_GCSoundBuffer@@UAEPAXI@Z

// FUNCTION: LITHTECH 0x0048f2d0
CSoundBuffer::~CSoundBuffer()
{
	Term();
}

// FUNCTION: LITHTECH 0x0048f2e0
LTRESULT CSoundBuffer::Init(FileIdentifier &fileIdent)
{
	// Start fresh
	Term();

	m_pFileIdent = &fileIdent;
	m_bTouched = LTTRUE;

	if (LoadData() != LT_OK)
		return LT_ERROR;

	dl_InitList(&m_InstanceList);

	m_pFileIdent->m_pData = this;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0048f330
LTRESULT CSoundBuffer::InitFromCompressed(CSoundBuffer &compressedSoundBuffer)
{
	// Start fresh
	Term();

	m_bTouched = LTTRUE;

	if (compressedSoundBuffer.m_WaveHeader.m_WaveFormat.wFormatTag == WAVE_FORMAT_IMA_ADPCM)
	{
		if (!AIL_decompress_ADPCM(&compressedSoundBuffer.m_SoundInfo, (void **)&m_pFileData, &m_dwFileSize))
		{
			AIL_unlock();
			return LT_ERROR;
		}
	}
	else if (compressedSoundBuffer.m_WaveHeader.m_WaveFormat.wFormatTag == WAVE_FORMAT_MPEGLAYER3)
	{
		if (!AIL_decompress_ASI(compressedSoundBuffer.m_pSoundData, compressedSoundBuffer.m_WaveHeader.m_dwDataSize, ".mp3",
			(void **)&m_pFileData, &m_dwFileSize, LTNULL))
		{
			AIL_unlock();
			return LT_ERROR;
		}
	}
	else
		return LT_ERROR;

	if (LoadDataFromDecompressed() != LT_OK)
	{
		AIL_lock();
		AIL_mem_free_lock(m_pFileData);
		AIL_unlock();

		m_pFileData = LTNULL;
		return LT_ERROR;
	}
	g_dwSoundMemory += m_dwFileSize;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0048f3f0
void CSoundBuffer::Term()
{
	CSoundInstance *pSoundInstance;
	LTLink *pCur, *pNext;

	if (m_pFileIdent)
		m_pFileIdent->m_pData = LTNULL;
	m_pFileIdent = LTNULL;

	pCur = m_InstanceList.m_Head.m_pNext;
	while (pCur != &m_InstanceList.m_Head)
	{
		pSoundInstance = (CSoundInstance *)pCur->m_pData;
		pNext = pCur->m_pNext;

		pSoundInstance->Term();
		dl_RemoveAt(&m_InstanceList, pCur);
		dl_TieOff(pCur);

		pCur = pNext;
	}
	dl_InitList(&m_InstanceList);

	memset(&m_WaveHeader, 0, sizeof(CWaveHeader));
	m_bTouched = LTFALSE;

	if (m_pDecompressedSoundBuffer)
	{
		GetClientILTSoundMgrImpl()->GetSoundBufferBank().Free(m_pDecompressedSoundBuffer);
		m_pDecompressedSoundBuffer = LTNULL;
	}

	if (m_pFileData)
	{
		AIL_lock();
		AIL_mem_free_lock(m_pFileData);
		AIL_unlock();

		m_pFileData = LTNULL;
		g_dwSoundMemory -= m_dwFileSize;
		m_pSoundData = LTNULL;
	}
}

inline void CSoundBuffer::CalcSampleType(S32 &sampleType, WAVEFORMATEX &waveFormat)
{
	// Get the MSS sample type.
	switch (waveFormat.wBitsPerSample * (waveFormat.nChannels + 1) >> 3)
	{
		case 4:
			sampleType = DIG_F_MONO_16;
			break;
		case 3:
			sampleType = DIG_F_STEREO_8;
			break;
		case 6:
			sampleType = DIG_F_STEREO_16;
			break;
		case 2:
		default:
			sampleType = DIG_F_MONO_8;
			break;
	}
}

// FUNCTION: LITHTECH 0x0048f4e0
LTRESULT CSoundBuffer::LoadData()
{
	uint8 *pTempBuffer;
	LTBOOL bRet;
	ILTStream *pFileStream;
	uint32 dwTemp;

	// Open the file for reading
	pFileStream = cf_OpenFileIdent(g_pClientMgr->m_hFileMgr, m_pFileIdent);
	if (!pFileStream)
	{
		if (g_DebugLevel >= 2)
		{
			dsi_ConsolePrint("Missing sound file %s", m_pFileIdent->m_Filename);
		}
		return LT_ERROR;
	}

	// Start at the beginning.
	pFileStream->SeekTo(0);

	// Parse the header of the sound file
	bRet = GetWaveInfo(*pFileStream, m_WaveHeader);

	// Catch a bad format that isn't caught by MSS.
	if (m_WaveHeader.m_WaveFormat.wFormatTag == WAVE_FORMAT_IMA_ADPCM &&
		m_WaveHeader.m_WaveFormat.wBitsPerSample != 4)
		bRet = LTFALSE;
	if (!bRet)
	{
		// Close the file
		pFileStream->Release();

		if (g_DebugLevel >= 2)
		{
			dsi_ConsolePrint("Corrupt sound file %s", m_pFileIdent->m_Filename);
		}
		return LT_ERROR;
	}

	// Calculate the duration
	m_dwDuration = (uint32)((1000.0f * (float)m_WaveHeader.m_dwDataSize / (float)m_WaveHeader.m_WaveFormat.nAvgBytesPerSec) + 0.5f);

	// Calculate the loop points in time.
	if (m_WaveHeader.m_CuePoint.m_bValid)
	{
		// Make sure it's got some valid data.
		if (m_WaveHeader.m_CuePoint.m_dwLength > 0 && m_WaveHeader.m_WaveFormat.nSamplesPerSec)
		{
			m_dwLoopPoints[0] = m_WaveHeader.m_CuePoint.m_dwPosition * 1000L /
				m_WaveHeader.m_WaveFormat.nSamplesPerSec;
			m_dwLoopPoints[1] = m_dwLoopPoints[0] + m_WaveHeader.m_CuePoint.m_dwLength * 1000L /
				m_WaveHeader.m_WaveFormat.nSamplesPerSec;

			// Make sure the loop points are in range.
			m_dwLoopPoints[0] = LTCLAMP(m_dwLoopPoints[0], 0, m_dwDuration);
			if (m_dwLoopPoints[1] > m_dwDuration)
				m_dwLoopPoints[1] = m_dwDuration;
			if (m_dwLoopPoints[1] <= m_dwLoopPoints[0])
				m_WaveHeader.m_CuePoint.m_bValid = LTFALSE;
		}
		else
		{
			m_WaveHeader.m_CuePoint.m_bValid = LTFALSE;
		}
	}

	// Check if the loop points have been made invalid.
	if (!m_WaveHeader.m_CuePoint.m_bValid)
	{
		m_dwLoopPoints[0] = 0;
		m_dwLoopPoints[1] = m_dwDuration;
	}

	// Do non-streaming stuff.
	if (!(GetSoundBufferFlags() & SOUNDBUFFERFLAG_STREAM))
	{
		pFileStream->SeekTo(0);
		m_dwFileSize = pFileStream->GetLen();
		AIL_lock();
		m_pFileData = (uint8*)AIL_mem_alloc_lock(m_dwFileSize);
		AIL_unlock();
		pFileStream->Read(m_pFileData, m_dwFileSize);

		// Close the file
		pFileStream->Release();

		m_pSoundData = &m_pFileData[m_WaveHeader.m_dwDataPos];

		// If compressed and sound will won't be decompressed as it's played,
		// then we need to have the sound data info.
		if (!IsCompressed() ||
			GetSoundBufferFlags() & (SOUNDBUFFERFLAG_DECOMPRESSATSTART | SOUNDBUFFERFLAG_DECOMPRESSONLOAD))
		{
			// Get the MSS data format values.
			m_SoundInfo.format = m_WaveHeader.m_WaveFormat.wFormatTag;
			m_SoundInfo.data_len = m_WaveHeader.m_dwDataSize;
			m_SoundInfo.rate = m_WaveHeader.m_WaveFormat.nSamplesPerSec;
			m_SoundInfo.bits = m_WaveHeader.m_WaveFormat.wBitsPerSample;
			m_SoundInfo.channels = m_WaveHeader.m_WaveFormat.nChannels;
			m_SoundInfo.samples = m_WaveHeader.m_dwSamples;
			m_SoundInfo.block_size = m_WaveHeader.m_WaveFormat.nBlockAlign;
			m_SoundInfo.data_ptr = m_pSoundData;
			m_SoundInfo.initial_ptr = m_pSoundData;

			// Decompress immediately if told to.
			if (IsCompressed() && GetSoundBufferFlags() & SOUNDBUFFERFLAG_DECOMPRESSONLOAD)
			{
				if (DecompressData() != LT_OK)
				{
					AIL_lock();
					AIL_mem_free_lock(m_pFileData);
					AIL_unlock();
					m_pFileData = LTNULL;
					return LT_ERROR;
				}

				// DecompressData recorded the memory alloc.  Since we're doing that below,
				// remove it.
				g_dwSoundMemory -= m_pDecompressedSoundBuffer->m_dwFileSize;

				// Get rid of the compressed file data.
				AIL_lock();
				AIL_mem_free_lock(m_pFileData);
				AIL_unlock();

				// Steal the data from the decompressed buffer.
				m_WaveHeader.m_dwDataPos = m_pDecompressedSoundBuffer->m_WaveHeader.m_dwDataPos;
				m_WaveHeader.m_dwDataSize = m_pDecompressedSoundBuffer->m_WaveHeader.m_dwDataSize;
				m_WaveHeader.m_dwSamples = m_pDecompressedSoundBuffer->m_WaveHeader.m_dwSamples;
				m_WaveHeader.m_WaveFormat = m_pDecompressedSoundBuffer->m_WaveHeader.m_WaveFormat;
				m_pFileData = m_pDecompressedSoundBuffer->m_pFileData;
				m_dwFileSize = m_pDecompressedSoundBuffer->m_dwFileSize;
				m_pSoundData = m_pDecompressedSoundBuffer->m_pSoundData;
				m_pDecompressedSoundBuffer->m_pFileData = LTNULL;
				m_SoundInfo = m_pDecompressedSoundBuffer->m_SoundInfo;

				// No longer need the decompressed buffer.
				GetClientILTSoundMgrImpl()->GetSoundBufferBank().Free(m_pDecompressedSoundBuffer);
				m_pDecompressedSoundBuffer = LTNULL;
			}

			// Convert sample if needed
			if (!IsCompressed() && GetClientILTSoundMgrImpl()->GetConvert16to8())
			{
				if (GetWaveFormat()->wBitsPerSample == 16 && GetWaveFormat()->wFormatTag == WAVE_FORMAT_PCM)
				{
					m_WaveHeader.m_dwDataSize /= 2;
					GetWaveFormat()->wBitsPerSample = 8;
					GetWaveFormat()->nAvgBytesPerSec /= 2;
					GetWaveFormat()->nBlockAlign /= 2;

					// Create a new file buffer that has half the size for the data buffer.
					AIL_lock();
					pTempBuffer = (uint8*)AIL_mem_alloc_lock(m_dwFileSize - m_WaveHeader.m_dwDataSize);
					AIL_unlock();

					// Copy in part before sound data.
					dwTemp = m_pSoundData - m_pFileData;
					memcpy(pTempBuffer, m_pFileData, dwTemp);
					// Copy in sound data.
					CopySoundData16to8(pTempBuffer + dwTemp, m_pSoundData, m_WaveHeader.m_dwDataSize);
					// Copy in part after sound data.
					memcpy(pTempBuffer + dwTemp + m_WaveHeader.m_dwDataSize,
						m_pFileData + dwTemp + 2 * m_WaveHeader.m_dwDataSize,
						m_dwFileSize - dwTemp - 2 * m_WaveHeader.m_dwDataSize);

					AIL_lock();
					AIL_mem_free_lock(m_pFileData);
					AIL_unlock();
					m_pFileData = pTempBuffer;
					m_pSoundData = m_pFileData + dwTemp;
					m_dwFileSize -= m_WaveHeader.m_dwDataSize;
				}
			}
		}

		g_dwSoundMemory += m_dwFileSize;
	}
	else
	{
		// Close the file
		pFileStream->Release();
	}

	CalcSampleType(m_nSampleType, m_WaveHeader.m_WaveFormat);

	if (g_DebugLevel >= 2)
	{
		dsi_ConsolePrint("Loaded sound file %s", m_pFileIdent->m_Filename);
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0048f9c0
LTRESULT CSoundBuffer::LoadDataFromDecompressed()
{
	ILTStream	*pDataStream;

	if (!m_pFileData)
		return LT_ERROR;

	pDataStream = streamsim_MemStreamFromBuffer(m_pFileData, m_dwFileSize);
	if (!pDataStream)
		return LT_ERROR;

	// Parse the header of the sound file
	if (!GetWaveInfo(*pDataStream, m_WaveHeader))
	{
		return LT_ERROR;
	}

	pDataStream->Release();

	m_pSoundData = &m_pFileData[m_WaveHeader.m_dwDataPos];

	// Get the MSS data format values.
	m_SoundInfo.format = m_WaveHeader.m_WaveFormat.wFormatTag;
	m_SoundInfo.data_len = m_WaveHeader.m_dwDataSize;
	m_SoundInfo.rate = m_WaveHeader.m_WaveFormat.nSamplesPerSec;
	m_SoundInfo.bits = m_WaveHeader.m_WaveFormat.wBitsPerSample;
	m_SoundInfo.channels = m_WaveHeader.m_WaveFormat.nChannels;
	m_SoundInfo.samples = m_WaveHeader.m_dwSamples;
	m_SoundInfo.block_size = m_WaveHeader.m_WaveFormat.nBlockAlign;
	m_SoundInfo.data_ptr = m_pSoundData;
	m_SoundInfo.initial_ptr = m_pSoundData;

	CalcSampleType(m_nSampleType, m_WaveHeader.m_WaveFormat);

	// Calculate the duration
	m_dwDuration = (uint32)((1000.0f * (float)m_WaveHeader.m_dwDataSize /
		(float)m_WaveHeader.m_WaveFormat.nAvgBytesPerSec) + 0.5f);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0048faf0
LTRESULT CSoundBuffer::DecompressData()
{
	// Check if already decompressed.
	if (m_pDecompressedSoundBuffer)
		return LT_OK;

	// Create a new sound buffer
	m_pDecompressedSoundBuffer = GetClientILTSoundMgrImpl()->GetSoundBufferBank().Allocate();
	if (!m_pDecompressedSoundBuffer)
	{
		return LT_ERROR;
	}

	if (m_pDecompressedSoundBuffer->InitFromCompressed(*this) != LT_OK)
	{
		GetClientILTSoundMgrImpl()->GetSoundBufferBank().Free(m_pDecompressedSoundBuffer);
		m_pDecompressedSoundBuffer = LTNULL;
		return LT_ERROR;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0048fb90
LTRESULT CSoundBuffer::Unload()
{
	CSoundInstance *pSoundInstance;
	LTLink *pCur, *pNext;

	// Get rid of any instances referencing this buffer.
	pCur = m_InstanceList.m_Head.m_pNext;
	while (pCur != &m_InstanceList.m_Head)
	{
		pSoundInstance = (CSoundInstance *)pCur->m_pData;
		pNext = pCur->m_pNext;

		dl_RemoveAt(&m_InstanceList, pCur);
		dl_TieOff(pCur);

		pCur = pNext;
	}
	dl_InitList(&m_InstanceList);

	if (m_pDecompressedSoundBuffer)
	{
		GetClientILTSoundMgrImpl()->GetSoundBufferBank().Free(m_pDecompressedSoundBuffer);
		m_pDecompressedSoundBuffer = LTNULL;
	}

	if (m_pFileData)
	{
		AIL_lock();
		AIL_mem_free_lock(m_pFileData);
		AIL_unlock();

		m_pFileData = LTNULL;
		g_dwSoundMemory -= m_dwFileSize;
		m_pSoundData = LTNULL;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0048fc50
LTRESULT CSoundBuffer::Reload()
{
	// Start fresh
	Unload();

	// Make sure our file pointer still exists
	if (!m_pFileIdent)
		return LT_ERROR;

	if (LoadData() != LT_OK)
		return LT_ERROR;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x0048fc80
LTRESULT CSoundBuffer::AddInstance(CSoundInstance &soundInstance)
{
	// Load the buffer back up if it was unloaded
	if (!m_pFileData && m_pFileIdent)
	{
		if (Reload() != LT_OK)
			return LT_ERROR;
	}

	// If the buffer is compressed and it should be decompressed at start, then do it.
	if (IsCompressed() && !(GetSoundBufferFlags() & SOUNDBUFFERFLAG_STREAM) &&
		GetSoundBufferFlags() & SOUNDBUFFERFLAG_DECOMPRESSATSTART)
	{
		// If the buffer is compressed and it doesn't have a decompressed buffer, decompress now.
		if (!m_pDecompressedSoundBuffer)
		{
			if (DecompressData() != LT_OK)
				return LT_ERROR;
		}
	}

	dl_AddTail(&m_InstanceList, (LTLink *)soundInstance.GetSoundBufferLink(), &soundInstance);
	return LT_OK;
}

// FUNCTION: LITHTECH 0x0048fd10
LTRESULT CSoundBuffer::RemoveInstance(CSoundInstance &soundInstance)
{
	dl_RemoveAt(&m_InstanceList, (LTLink *)soundInstance.GetSoundBufferLink());

	// Toss the decompressed buffer if we just needed it for a decompress at start buffer.
	if (m_pDecompressedSoundBuffer && GetSoundBufferFlags() & SOUNDBUFFERFLAG_DECOMPRESSATSTART)
	{
		// If there are no more instances of this buffer, then dump the decompressed data.
		if (m_InstanceList.m_nElements == 0)
		{
			GetClientILTSoundMgrImpl()->GetSoundBufferBank().Free(m_pDecompressedSoundBuffer);
			m_pDecompressedSoundBuffer = LTNULL;
		}
	}

	return LT_OK;
}

// Talon: finds the instance of this buffer closest to finishing.
// FUNCTION: LITHTECH 0x0048fd90
CSoundInstance *CSoundBuffer::GetLowestPriorityInstance()
{
	LTLink *pCur;
	CSoundInstance *pSoundInstance, *pLowest;

	if (m_InstanceList.m_nElements == 0)
		return LTNULL;

	pCur = m_InstanceList.m_Head.m_pNext;
	pLowest = (CSoundInstance *)pCur->m_pData;
	pCur = pCur->m_pNext;
	while (pCur != &m_InstanceList.m_Head)
	{
		pSoundInstance = (CSoundInstance *)pCur->m_pData;
		pCur = pCur->m_pNext;

		if (pSoundInstance->GetTimer() < pLowest->GetTimer())
			pLowest = pSoundInstance;
	}

	return pLowest;
}

// FUNCTION: LITHTECH 0x0048fdd0
LTBOOL CSoundBuffer::CanPlay(CSoundInstance &soundInstance)
{
	LTLink *pCur;
	uint32 dwPlaying;
	CSoundInstance *pSoundInstance;

	// Check if sound is paused
	if (soundInstance.GetSoundInstanceFlags() & SOUNDINSTANCEFLAG_PAUSED)
	{
		return LTFALSE;
	}

	if (GetInstanceList()->m_nElements > 1)
	{
		// Check other instances using this same sound buffer.  If there is at least one other instance
		// playing from this buffer at the sampe timer, then this one can't play
		pCur = GetInstanceList()->m_Head.m_pNext;
		dwPlaying = 0;
		while (pCur != &GetInstanceList()->m_Head && dwPlaying < 1)
		{
			pSoundInstance = (CSoundInstance *)pCur->m_pData;
			pCur = pCur->m_pNext;
			if (!pSoundInstance)
				continue;

			if (pSoundInstance == &soundInstance)
				continue;

			// If the other instance doesn't have a sample, then it's not playing
			if (!pSoundInstance->GetSample() && !pSoundInstance->Get3DSample())
				continue;

			// Check if the other instance has a timer close to this one, and is higher in priority
			if ((abs((int32)pSoundInstance->GetTimer() - (int32)soundInstance.GetTimer()) < 5) &&
				(pSoundInstance->GetListIndex() > soundInstance.GetListIndex()))
				dwPlaying++;
		}

		if (dwPlaying > 0)
		{
			soundInstance.SetCollisions(soundInstance.GetCollisions() + 1);
			return LTFALSE;
		}
	}

	return LTTRUE;
}

#pragma warning(disable : 4244)  // ignore the warning about data loss (we want to lose data!)
// FUNCTION: LITHTECH 0x0048fe70
LTRESULT CSoundBuffer::CopySoundData16to8(uint8 *pDest, uint8 *pSource, uint32 nSize)
{
	uint8* pTo = pDest;
	uint8* pToEnd = pTo+nSize;
	uint16* pFrom = (uint16 *)pSource;

	while (pTo < pToEnd)
	{
		*pTo = (((uint32)(*pFrom)) + 32768) >> 8;
		pFrom++;
		pTo++;
	}

	return LT_OK;
}
#pragma warning(default : 4244)  // restore the warning about data loss
