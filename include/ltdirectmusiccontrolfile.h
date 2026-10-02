// Jupiter runtime/kernel/src/sys/win/ltdirectmusiccontrolfile.h, Talon layout.
// Needs /I lithshared/lith and /I lithshared/controlfilemgr (Talon's controlfilemgr.h).
#ifndef __LTDIRECTMUSICCONTROLFILE_H__
#define __LTDIRECTMUSICCONTROLFILE_H__

#include <windows.h>
#include "controlfilemgr.h"


// special derived controlfilemgr that uses dstreams (0x38 bytes)
class CControlFileMgrDStream : public CControlFileMgr
{
public:
	// open a file for reading in read only text mode
	virtual BOOL FileOpen(const char* sName);

	// close file
	virtual void FileClose();

	// get next character in file
	virtual int FileGetChar();

	// return TRUE if we are at the end of the file
	virtual BOOL FileEOF();

protected:
	char* m_pData;		// 0x2c
	char* m_pPos;		// 0x30
	char* m_pEnd;		// 0x34
};

#endif
