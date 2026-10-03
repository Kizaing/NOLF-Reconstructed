// Jupiter runtime/shared/src/dhashtable.cpp
// FLAGS: /O2 /GX-
// Talon uses the lithshared StructBank-based ObjectBank for the 2-byte-number elements.
#include <string.h>
#include "ltbasedefs.h"
#include "dhashtable.h"
#include "../../../build/proj/LT2/lithshared/stdlith/object_bank.h"

void* dalloc(size_t size);
void dfree(void *ptr);


// ------------------------------------------------------------ //
// Structures.
// ------------------------------------------------------------ //

typedef uint32 (*GetCodeFn)(const void *pData, uint32 dataLen);
typedef int (*CompareKeyFn)(const void *pData1, const void *pData2, uint32 dataLen);

struct HashTable;

struct MapEntry
{
	HashTable		*m_pHashTable;	// 0x00
	uint32			m_Index;		// 0x04
	uint32			m_nElements;	// 0x08
	LTLink			m_Elements;		// 0x0C
};

struct HashElement
{
	LTLink			m_Link;			// 0x00
	MapEntry		*m_pMapEntry;	// 0x0C
	void			*m_pUser;		// 0x10
	unsigned short	m_KeySize;		// 0x14
	char			m_Key[2];		// 0x16
};

struct HashTable
{
	int				m_HashType;		// 0x00
	uint32			m_nCollisions;	// 0x04
	uint32			m_MapSize;		// 0x08
	MapEntry		m_Map[1];		// 0x0C
};


// ------------------------------------------------------------ //
// Globals.
// ------------------------------------------------------------ //

// Dynamic initializer/terminator of g_HashElementBank (compiler generated).
// FUNCTION: LITHTECH 0x004335e0 _$E4
// FUNCTION: LITHTECH 0x004335f0 _$E1
// FUNCTION: LITHTECH 0x00433610 _$E3
// FUNCTION: LITHTECH 0x00433620 _$E2
static ObjectBank<HashElement> g_HashElementBank(32, 64);
// ObjectBank<HashElement> virtuals instantiated here (AllocVoid at 0x437a20 lives elsewhere).
// FUNCTION: LITHTECH 0x00433c50 ?FreeVoid@?$ObjectBank@UHashElement@@VNullCS@@@@UAEXPAX@Z
// FUNCTION: LITHTECH 0x00433c70 ?Term@?$ObjectBank@UHashElement@@VNullCS@@@@UAEXXZ
// FUNCTION: LITHTECH 0x00433c90 ??_G?$ObjectBank@UHashElement@@VNullCS@@@@UAEPAXI@Z
static GetCodeFn g_GetHashCodeFns[NUM_HASH_TYPES] = {0};
static CompareKeyFn g_CompareKeyFns[NUM_HASH_TYPES] = {0};


// ------------------------------------------------------------ //
// Internal functions.
// ------------------------------------------------------------ //

inline char hs_Toupper(const char theChar)
{
	if(theChar >= 'a' && theChar <= 'z')
		return 'A' + (theChar - 'a');
	else
		return theChar;
}


// FUNCTION: LITHTECH 0x00433720
static uint32 hs_GetCode_2ByteNumber(const void *pData, uint32 dataLen)
{
	return *((const unsigned short*)pData);
}


// FUNCTION: LITHTECH 0x00433730
static uint32 hs_GetCode_StringNoCase(const void *pData, uint32 dataLen)
{
	const char *pCurByte;
	uint32 sum, i;

	sum = 0;

	pCurByte = (const char*)pData;
	for(i=0; i < dataLen; i++)
	{
		sum += ((uint32)hs_Toupper(*pCurByte)) * i;
		++pCurByte;
	}

	return sum;
}

// FUNCTION: LITHTECH 0x00433770
static uint32 hs_GetCode_Raw(const void *pData, uint32 dataLen)
{
	const char *pCurByte;
	uint32 sum, i;

	sum = 0;

	pCurByte = (const char*)pData;
	for(i=0; i < dataLen; i++)
	{
		sum += ((uint32)*pCurByte) * i;
		++pCurByte;
	}

	return sum;
}

inline char hs_FilenameChar(char theChar)
{
	theChar = hs_Toupper(theChar);
	if(theChar == '/')
		theChar = '\\';

	return theChar;
}

// FUNCTION: LITHTECH 0x004337a0
static uint32 hs_GetCode_Filename(const void *pData, uint32 dataLen)
{
	const char *pCurByte;
	uint32 sum, i;

	sum = 0;

	pCurByte = (const char*)pData;
	for(i=0; i < dataLen; i++)
	{
		sum += ((uint32)hs_FilenameChar(*pCurByte)) * i;
		++pCurByte;
	}

	return sum;
}

// FUNCTION: LITHTECH 0x004337e0
static int hs_CompareKey_2ByteNumber(const void *pData1, const void *pData2, uint32 dataLen)
{
	unsigned short num1, num2;

	num1 = *((const unsigned short*)pData1);
	num2 = *((const unsigned short*)pData2);

	return num1 == num2;
}

// FUNCTION: LITHTECH 0x00433800
static int hs_CompareKey_StringNoCase(const void *pData1, const void *pData2, uint32 dataLen)
{
	const char *pEndKey1, *pKey1, *pKey2;

	pKey1 = (const char*)pData1;
	pKey2 = (const char*)pData2;
	pEndKey1 = pKey1 + dataLen;

	while(pKey1 < pEndKey1)
	{
		if(hs_Toupper(*pKey1) != hs_Toupper(*pKey2))
			return 0;

		++pKey1;
		++pKey2;
	}

	return 1;
}

// FUNCTION: LITHTECH 0x00433850
static int hs_CompareKey_Raw(const void *pData1, const void *pData2, uint32 dataLen)
{
	const char *pEndKey1, *pKey1, *pKey2;

	pKey1 = (const char*)pData1;
	pKey2 = (const char*)pData2;
	pEndKey1 = pKey1 + dataLen;

	while(pKey1 < pEndKey1)
	{
		if(*pKey1 != *pKey2)
			return 0;

		++pKey1;
		++pKey2;
	}

	return 1;
}

// FUNCTION: LITHTECH 0x00433890
static int hs_CompareKey_Filename(const void *pData1, const void *pData2, uint32 dataLen)
{
	const char *pEndKey1, *pKey1, *pKey2;

	pKey1 = (const char*)pData1;
	pKey2 = (const char*)pData2;
	pEndKey1 = pKey1 + dataLen;

	while(pKey1 < pEndKey1)
	{
		if(hs_FilenameChar(*pKey1) != hs_FilenameChar(*pKey2))
			return 0;

		++pKey1;
		++pKey2;
	}

	return 1;
}


// FUNCTION: LITHTECH 0x00433bf0
static LTLink* hs_SeekToNext(MapEntry *pMapEntry, LTLink *pCur)
{
	for(;;)
	{
		pCur = pCur->m_pNext;

		// If we're at the end, seek to the next map entry.
		if(pCur == &pMapEntry->m_Elements)
		{
			if(pMapEntry->m_Index == (pMapEntry->m_pHashTable->m_MapSize-1))
			{
				// All thru..
				return 0;
			}
			else
			{
				++pMapEntry;
				pCur = &pMapEntry->m_Elements;
				continue;
			}
		}

		break;
	}

	return pCur;
}


inline int hs_CompareKeys(HashTable *pTable, const char *pKey1, const char *pKey2, uint32 len)
{
	return g_CompareKeyFns[pTable->m_HashType](pKey1, pKey2, len);
}


// FUNCTION: LITHTECH 0x00433a20
static uint32 hs_GetMapEntryIndex(HashTable *pTable, const void *pKey, uint32 keyLen)
{
	uint32 sum;

	sum = g_GetHashCodeFns[pTable->m_HashType](pKey, keyLen);
	sum %= pTable->m_MapSize - 1;
	return sum;
}


// ------------------------------------------------------------ //
// Interface functions.
// ------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00433650
HHashTable *hs_CreateHashTable(uint32 mapSize, int hashType)
{
	HashTable *pTable;
	uint32 size, i;

	if(mapSize == 0)
		return 0;

	if(hashType < 0 || hashType >= NUM_HASH_TYPES)
		return 0;

	size = sizeof(HashTable) + (sizeof(MapEntry) * (mapSize-1));
	pTable = (HashTable*)dalloc(size);
	memset(pTable, 0, size);
	pTable->m_HashType = hashType;

	pTable->m_MapSize = mapSize;
	for(i=0; i < mapSize; i++)
	{
		pTable->m_Map[i].m_pHashTable = pTable;
		pTable->m_Map[i].m_Index = i;
		dl_TieOff(&pTable->m_Map[i].m_Elements);
	}

	// Initialize the global hashing tables..
	g_GetHashCodeFns[HASH_2BYTENUMBER] = hs_GetCode_2ByteNumber;
	g_GetHashCodeFns[HASH_STRING_NOCASE] = hs_GetCode_StringNoCase;
	g_GetHashCodeFns[HASH_RAW] = hs_GetCode_Raw;
	g_GetHashCodeFns[HASH_FILENAME] = hs_GetCode_Filename;

	g_CompareKeyFns[HASH_2BYTENUMBER] = hs_CompareKey_2ByteNumber;
	g_CompareKeyFns[HASH_STRING_NOCASE] = hs_CompareKey_StringNoCase;
	g_CompareKeyFns[HASH_RAW] = hs_CompareKey_Raw;
	g_CompareKeyFns[HASH_FILENAME] = hs_CompareKey_Filename;

	return (HHashTable *)pTable;
}


// FUNCTION: LITHTECH 0x004338f0
void hs_DestroyHashTable(HHashTable *hTable)
{
	HashTable *pTable;
	HashElement *pElement;
	LTLink *pCur, *pNext;
	uint32 i;

	if(!hTable)
		return;

	pTable = (HashTable*)hTable;

	// Free all the elements.
	for(i=0; i < pTable->m_MapSize; i++)
	{
		pCur = pTable->m_Map[i].m_Elements.m_pNext;
		while(pCur != &pTable->m_Map[i].m_Elements)
		{
			pNext = pCur->m_pNext;
			pElement = (HashElement*)pCur->m_pData;

			if(pTable->m_HashType == HASH_2BYTENUMBER)
			{
				g_HashElementBank.Free(pElement);
			}
			else
			{
				dfree(pElement);
			}

			pCur = pNext;
		}
	}

	// Free the table.
	dfree(pTable);
}


uint32 hs_GetNumCollisions(HHashTable *hTable)
{
	if(!hTable)
		return 0;

	return ((HashTable*)hTable)->m_nCollisions;
}


// FUNCTION: LITHTECH 0x00433960
HHashElement *hs_AddElement(HHashTable *hTable, const void *pKey, uint32 keyLen)
{
	HashTable *pTable;
	uint32 mapEl;
	HashElement *pElement;
	MapEntry *pMapEntry;

	if(!hTable)
		return 0;

	pTable = (HashTable*)hTable;
	mapEl = hs_GetMapEntryIndex(pTable, pKey, keyLen);
	pMapEntry = &pTable->m_Map[mapEl];

	if(pTable->m_HashType == HASH_2BYTENUMBER)
	{
		pElement = g_HashElementBank.Allocate();
	}
	else
	{
		pElement = (HashElement*)dalloc(sizeof(HashElement) + (((int)keyLen)-2));
	}
	pElement->m_KeySize = (unsigned short)keyLen;
	memcpy(pElement->m_Key, pKey, keyLen);
	pElement->m_pUser = 0;
	pElement->m_pMapEntry = pMapEntry;
	pElement->m_Link.m_pData = pElement;

	dl_Insert(&pMapEntry->m_Elements, &pElement->m_Link);
	++pMapEntry->m_nElements;

	if(pMapEntry->m_nElements > 1)
	{
		++pTable->m_nCollisions;
	}

	return (HHashElement *)pElement;
}


// FUNCTION: LITHTECH 0x00433a50
void hs_RemoveElement(HHashTable *hTable, HHashElement *hElement)
{
	HashElement *pElement;
	HashTable *pTable;

	if(!hTable)
		return;
	if(!hElement)
		return;

	pTable = (HashTable*)hTable;

	pElement = (HashElement*)hElement;
	pElement->m_pMapEntry->m_nElements--;
	dl_Remove(&pElement->m_Link);
	if(pTable->m_HashType == HASH_2BYTENUMBER)
	{
		g_HashElementBank.Free(pElement);
	}
	else
	{
		dfree(pElement);
	}
}


// FUNCTION: LITHTECH 0x00433aa0
HHashElement *hs_FindElement(HHashTable *hTable, const void *pKey, uint32 keyLen)
{
	HashTable *pTable;
	uint32 mapEl;
	MapEntry *pMapEntry;
	HashElement *pElement;
	LTLink *pCur;

	if(!hTable)
		return 0;

	pTable = (HashTable*)hTable;

	mapEl = hs_GetMapEntryIndex(pTable, pKey, keyLen);
	pMapEntry = &pTable->m_Map[mapEl];

	pCur = pMapEntry->m_Elements.m_pNext;
	while(pCur != &pMapEntry->m_Elements)
	{
		pElement = (HashElement*)pCur->m_pData;

		if(pElement->m_KeySize == keyLen)
		{
			if(hs_CompareKeys(pTable, (const char*)pElement->m_Key, (const char*)pKey, keyLen))
				return (HHashElement *)pElement;
		}

		pCur = pCur->m_pNext;
	}

	return 0;
}


// FUNCTION: LITHTECH 0x00433b20
HHashElement *hs_FindNextElement(HHashTable *hTable, HHashElement *hInElement, const void *pKey, uint32 keyLen)
{
	HashTable *pTable;
	MapEntry *pMapEntry;
	HashElement *pElement, *pInElement;
	LTLink *pCur;

	if(!hTable || !hInElement)
		return 0;

	pInElement = (HashElement*)hInElement;
	pTable = (HashTable*)hTable;
	pMapEntry = pInElement->m_pMapEntry;

	pCur = pInElement->m_Link.m_pNext;
	while(pCur != &pMapEntry->m_Elements)
	{
		pElement = (HashElement*)pCur->m_pData;

		if(pElement->m_KeySize == keyLen)
		{
			if(hs_CompareKeys(pTable, (const char*)pElement->m_Key, (const char*)pKey, keyLen))
				return (HHashElement *)pElement;
		}

		pCur = pCur->m_pNext;
	}

	return 0;
}


// FUNCTION: LITHTECH 0x00433b90
void* hs_GetElementKey(HHashElement *hElement, uint32 *pKeyLen)
{
	if(!hElement)
		return 0;

	if(pKeyLen)
		*pKeyLen = ((HashElement*)hElement)->m_KeySize;

	return ((HashElement*)hElement)->m_Key;
}


// FUNCTION: LITHTECH 0x00433bb0
void* hs_GetElementUserData(HHashElement *hElement)
{
	if(!hElement)
		return 0;

	return ((HashElement*)hElement)->m_pUser;
}


// FUNCTION: LITHTECH 0x00433bc0
void hs_SetElementUserData(HHashElement *hElement, void *pUser)
{
	if(!hElement)
		return;

	((HashElement*)hElement)->m_pUser = pUser;
}


// FUNCTION: LITHTECH 0x00433bd0
HHashIterator *hs_GetFirstElement(HHashTable *hTable)
{
	LTLink *pIterator;
	HashTable *pTable;

	if(!hTable)
		return 0;

	pTable = (HashTable*)hTable;
	pIterator = &pTable->m_Map[0].m_Elements;
	return (HHashIterator *)hs_SeekToNext(&pTable->m_Map[0], pIterator);
}


// FUNCTION: LITHTECH 0x00433c20
HHashElement *hs_GetNextElement(HHashIterator *&pIterator)
{
	LTLink *pRet;

	if(!&pIterator || !pIterator)
		return 0;

	pRet = (LTLink*)(pIterator);

	pIterator = (HHashIterator *)hs_SeekToNext(((HashElement*)pRet)->m_pMapEntry, pRet);
	return (HHashElement *)pRet->m_pData;
}
