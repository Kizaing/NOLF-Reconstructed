// Hash table API (Jupiter shared/src/dhashtable.h).
// Talon declares the handle types as structs (the mangled names of matched callers depend on it).
#ifndef __DHASHTABLE_H__
#define __DHASHTABLE_H__

#include "ltbasetypes.h"

// Hash types.
#ifndef HASH_2BYTENUMBER
#define HASH_2BYTENUMBER    0   // Treats keys as 2 byte numbers.
#endif
#ifndef HASH_STRING_NOCASE
#define HASH_STRING_NOCASE  1   // Treats keys as case-insensitive strings.
#endif
#ifndef HASH_RAW
#define HASH_RAW            2   // Keys are raw data (like case-sensitive strings).
#endif
#ifndef HASH_FILENAME
#define HASH_FILENAME       3   // Keys are case-insensitive and '/' maps to '\'.
#endif
#ifndef NUM_HASH_TYPES
#define NUM_HASH_TYPES      4
#endif

struct HHashElement;
struct HHashTable;
struct HHashIterator;

HHashTable* hs_CreateHashTable(uint32 mapSize, int hashType);
void hs_DestroyHashTable(HHashTable *hTable);
uint32 hs_GetNumCollisions(HHashTable *hTable);
HHashElement* hs_AddElement(HHashTable *hTable, const void *pKey, uint32 keyLen);
void hs_RemoveElement(HHashTable *hTable, HHashElement *hElement);
HHashElement* hs_FindElement(HHashTable *hTable, const void *pKey, uint32 keyLen);
HHashElement* hs_FindNextElement(HHashTable *hTable, HHashElement *hInElement, const void *pKey, uint32 keyLen);
void* hs_GetElementKey(HHashElement *hElement, uint32 *pKeyLen);
void* hs_GetElementUserData(HHashElement *hElement);
void hs_SetElementUserData(HHashElement *hElement, void *pUser);

// Iteration: hIter = hs_GetFirstElement(hTable); while(hIter) { hEl = hs_GetNextElement(hIter); ... }
HHashIterator* hs_GetFirstElement(HHashTable *hTable);
HHashElement* hs_GetNextElement(HHashIterator *&pIterator);

#endif
