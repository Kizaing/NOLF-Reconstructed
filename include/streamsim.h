// This module just simulates FILE structures with DStreams so tools don't have to do it
// everywhere (Jupiter runtime/kernel/src/sys/win/streamsim.h).
#ifndef __STREAMSIM_H__
#define __STREAMSIM_H__

#include "ltbasedefs.h"

// File streams.
ILTStream* streamsim_Open(const char *pFilename, const char *pAccess);

// Memory streams.
ILTStream* streamsim_OpenMemStream(uint32 cacheSize=256);

// Create a memory stream from the contents of a file.
ILTStream* streamsim_MemStreamFromFile(char *pFilename);

// Create a memory stream from the contents of a buffer.
ILTStream* streamsim_MemStreamFromBuffer(uint8 *pData, uint32 dwDataSize);

#endif  // __STREAMSIM_H__
