// Jupiter runtime/sound/src/wave.cpp, Talon version.
// Talon has no WriteWave (the packer does that); ReadChunk is defined after GetWaveInfo.
#include "bdefs.h"
#include "wave.h"
#include "iltstream.h"

static const uint32 s_tagRIFF = mmioFOURCC('R', 'I', 'F', 'F');
static const uint32 s_tagFMT  = mmioFOURCC('f', 'm', 't', ' ');
static const uint32 s_tagDATA = mmioFOURCC('d', 'a', 't', 'a');
static const uint32 s_tagFACT = mmioFOURCC('f', 'a', 'c', 't');
static const uint32 s_tagLIST = mmioFOURCC('L', 'I', 'S', 'T');
static const uint32 s_tagINFO = mmioFOURCC('i', 'n', 'f', 'o');
static const uint32 s_tagWAVE = mmioFOURCC('W', 'A', 'V', 'E');
static const uint32 s_tagLITH = mmioFOURCC('l', 'i', 't', 'h');
static const uint32 s_tagCUE  = mmioFOURCC('c', 'u', 'e', ' ');
static const uint32 s_tagADTL = mmioFOURCC('a', 'd', 't', 'l');
static const uint32 s_tagLTXT = mmioFOURCC('l', 't', 'x', 't');

static LTBOOL ReadChunk(ILTStream &inStream, uint32 dwType, uint32 dwLength, uint32 dwForm, CWaveHeader &waveHeader);


// FUNCTION: LITHTECH 0x0049ec40
LTBOOL GetWaveInfo(ILTStream &inStream, CWaveHeader &waveHeader)
{
	uint32 dwType;
	uint32 dwTypeLength;

	inStream.SeekTo(0);
	memset(&waveHeader, 0, sizeof(CWaveHeader));

	// Read the file.
	while (inStream.GetPos() < inStream.GetLen())
	{
		if (inStream.Read(&dwType, sizeof(uint32)) != LT_OK)
			break;
		if (inStream.Read(&dwTypeLength, sizeof(uint32)) != LT_OK)
			break;
		if (!ReadChunk(inStream, dwType, dwTypeLength, 0, waveHeader))
			break;
	}

	if (waveHeader.m_WaveFormat.wFormatTag == WAVE_FORMAT_PCM ||
		waveHeader.m_WaveFormat.wFormatTag == WAVE_FORMAT_IMA_ADPCM ||
		waveHeader.m_WaveFormat.wFormatTag == WAVE_FORMAT_MPEGLAYER3)
		return LTTRUE;

	return LTFALSE;
}


// FUNCTION: LITHTECH 0x0049ed10
static LTBOOL ReadChunk(ILTStream &inStream, uint32 dwType, uint32 dwLength, uint32 dwForm, CWaveHeader &waveHeader)
{
	uint32 dwEnd;

	// Get end of chunk
	dwEnd = inStream.GetPos() + dwLength;
	// Must end on even word boundary.
	if (dwEnd & 1)
		dwEnd++;
	// Don't go past file length.
	if (dwEnd > inStream.GetLen())
		dwEnd = inStream.GetLen();

	// Look through WAVE form chunks.
	switch (dwType)
	{
		case s_tagFMT:
		{
			if (dwForm == s_tagWAVE)
			{
				uint32 nSizeToRead = sizeof(waveHeader.m_WaveFormat);
				if (dwLength < nSizeToRead)
					nSizeToRead = dwLength;
				inStream.Read(&waveHeader.m_WaveFormat, nSizeToRead);
			}

			break;
		}

		case s_tagDATA:
		{
			uint32 dwSamplesPerBlock;

			if (dwForm == s_tagWAVE)
			{
				waveHeader.m_dwDataPos = inStream.GetPos();
				waveHeader.m_dwDataSize = dwLength;

				// Calculate the default ADPCM samples value.
				if (waveHeader.m_WaveFormat.wFormatTag == WAVE_FORMAT_IMA_ADPCM)
				{
					dwSamplesPerBlock = 4 << (waveHeader.m_WaveFormat.nChannels / 2);
					dwSamplesPerBlock = 1 + (waveHeader.m_WaveFormat.nBlockAlign - dwSamplesPerBlock) * 8 / dwSamplesPerBlock;
					waveHeader.m_dwSamples = ((waveHeader.m_dwDataSize + waveHeader.m_WaveFormat.nBlockAlign - 1) /
						waveHeader.m_WaveFormat.nBlockAlign) * dwSamplesPerBlock;
				}
				else
				{
					waveHeader.m_dwSamples = 0;
				}
			}

			break;
		}

		case s_tagFACT:
		{
			if (dwForm == s_tagWAVE)
			{
				// Look for ADPCM extra data.
				if (waveHeader.m_WaveFormat.wFormatTag == WAVE_FORMAT_IMA_ADPCM)
				{
					inStream.Read(&waveHeader.m_dwSamples, sizeof(uint32));
				}
			}

			break;
		}

		case s_tagLITH:
		{
			if (dwForm == s_tagINFO)
			{
				uint32 nSizeToRead = sizeof(WAVEHEADER_lith);
				if (dwLength < nSizeToRead)
					nSizeToRead = dwLength;
				inStream.Read(&waveHeader.m_lith, nSizeToRead);
			}

			break;
		}

		case s_tagCUE:
		{
			uint32 dwNumCuePoints, nSizeToRead, dwNewLength;
			WAVEHEADER_cuepoint cuePoint;

			// Read number of cue points.
			inStream.Read(&dwNumCuePoints, sizeof(uint32));
			dwNewLength = dwLength - sizeof(uint32);

			if (dwNumCuePoints > 0)
			{
				nSizeToRead = sizeof(WAVEHEADER_cuepoint);
				if (dwLength < dwNewLength)
					nSizeToRead = dwNewLength;
				inStream.Read(&cuePoint, nSizeToRead);
				dwNewLength -= nSizeToRead;

				// Make sure the cue point is for the 'data' chunk.
				if (cuePoint.m_fccChunk == s_tagDATA)
				{
					waveHeader.m_CuePoint.m_bValid = LTTRUE;
					waveHeader.m_CuePoint.m_dwName = cuePoint.m_dwName;
					waveHeader.m_CuePoint.m_dwPosition = cuePoint.m_dwPosition;
				}
			}

			break;
		}

		case s_tagLTXT:
		{
			if (dwForm == s_tagADTL)
			{
				uint32 nSizeToRead;
				WAVEHEADER_ltxt ltxt;

				nSizeToRead = sizeof(WAVEHEADER_ltxt);
				if (dwLength < nSizeToRead)
					nSizeToRead = dwLength;
				inStream.Read(&ltxt, nSizeToRead);

				if (waveHeader.m_CuePoint.m_bValid && ltxt.m_dwName == waveHeader.m_CuePoint.m_dwName)
				{
					waveHeader.m_CuePoint.m_dwLength = ltxt.m_dwSampleLength;
				}
			}

			break;
		}

		case s_tagRIFF:
		case s_tagLIST:
		{
			uint32 dwForm;

			inStream.Read(&dwForm, sizeof(uint32));

			// Run through the tags.
			while (inStream.GetPos() < dwEnd)
			{
				uint32 dwLength;
				uint32 dwType;

				inStream.Read(&dwType, sizeof(uint32));
				inStream.Read(&dwLength, sizeof(uint32));
				if (!ReadChunk(inStream, dwType, dwLength, dwForm, waveHeader))
					break;
			}

			break;
		}
	}

	// Read to end of chunk.
	inStream.SeekTo(dwEnd);

	return LTTRUE;
}
