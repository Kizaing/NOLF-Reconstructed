// Jupiter runtime/kernel/src/sys/win/ltdirectmusiccontrolfile.cpp
// FLAGS: /O2 /IE:/AVP2Source/build/proj/LT2/lithshared/lith /IE:/AVP2Source/build/proj/LT2/lithshared/controlfilemgr
// Talon opens the file through cf_OpenFile(g_pClientMgr->m_hFileMgr) with a FileRef that owns a copy
// of the name, and has no FileReadTest.
#include <windows.h>
#include <string.h>
#include "bdefs.h"
#include "ltdirectmusiccontrolfile.h"
#include "clientmgr.h"
#include "sprite.h"		// FileRef


///////////////////////////////////////////////////////////////////////////////////////////
// read in data from a file
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044b100
BOOL CControlFileMgrDStream::FileOpen(const char* sName)
{
	ILTStream *pFileStream;
	FileRef ref;
	ref.m_FileType = FILE_ANYFILE;
	ref.m_pFilename = new char[strlen(sName)+1];
	strcpy((char*)ref.m_pFilename, sName);

	// open the file
	pFileStream = cf_OpenFile(g_pClientMgr->m_hFileMgr, &ref);
	if (pFileStream == LTNULL)
	{
		delete (char*)ref.m_pFilename;
		return FALSE;
	}

	// make some data to hold the file data (these files are small so just load it all in)
	m_pData = new char[pFileStream->GetLen()];
	if (m_pData == LTNULL)
	{
		delete (char*)ref.m_pFilename;
		pFileStream->Release();
		return FALSE;
	}

	// read in the data
	if (pFileStream->Read(m_pData, pFileStream->GetLen()) != LT_OK)
	{
		delete (char*)ref.m_pFilename;
		pFileStream->Release();
		delete [] m_pData;
		return FALSE;
	}

	// set current position to start of data
	m_pPos = m_pData;

	// figure out last position in data
	m_pEnd = m_pData + pFileStream->GetLen() - 1;

	// close the file
	pFileStream->Release();

	delete (char*)ref.m_pFilename;

	// we succeeded
	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////////
// delete file data
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044b250
void CControlFileMgrDStream::FileClose()
{
	// free the file data
	if (m_pData != LTNULL) delete [] m_pData;
}


///////////////////////////////////////////////////////////////////////////////////////////
// get next character
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044b260
int CControlFileMgrDStream::FileGetChar()
{
	// make sure file data is not null
	if (m_pData == LTNULL) return 0;

	// make sure we are not past the end of the data
	if (FileEOF()) return 0;

	// get the value to return
	int nRetVal = *m_pPos;

	// increment the position
	m_pPos++;

	// return the value
	return nRetVal;
}


///////////////////////////////////////////////////////////////////////////////////////////
// return true if we are at the end of the data
///////////////////////////////////////////////////////////////////////////////////////////
// FUNCTION: LITHTECH 0x0044b290
BOOL CControlFileMgrDStream::FileEOF()
{
	// make sure file data is not LTNULL
	if (m_pData == LTNULL) return FALSE;

	// return true if we are past the end of the file
	if (m_pPos > m_pEnd) return TRUE;

	// return false if not past end of file
	else return FALSE;
}
