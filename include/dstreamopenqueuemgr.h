// Jupiter runtime/shared/src/sys/win/dstreamopenqueuemgr.h, Talon layout.
// Talon: the lists are lith's CBaseList, the FileRef owns the filename (no separate file_name),
// and the manager has no m_bInitialized.
#ifndef __DSTREAMOPENQUEUEMGR_H__
#define __DSTREAMOPENQUEUEMGR_H__

#include <windows.h>
#include "ltbasedefs.h"
#include "../../build/proj/LT2/lithshared/lith/baselist.h"
#include "sprite.h"		// FileRef

class CDStreamOpenQueueMgr;


// 0x24 bytes.
class CDStreamOpenQueueItem : public CBaseListItem
{
public:
	CDStreamOpenQueueItem(CDStreamOpenQueueMgr* pDStreamOpenQueueMgr)
	{
		m_pDStream = LTNULL;
		m_nLockCount = 0;
		m_DStreamFileRef_.m_pFilename = LTNULL;
		m_pDStreamOpenQueueMgr = pDStreamOpenQueueMgr;
		m_nSaveSeekPos = 0;
	};

	~CDStreamOpenQueueItem() {
		if (m_pDStream != LTNULL) Close();
		if (m_DStreamFileRef_.m_pFilename != LTNULL) {
			delete (char*)m_DStreamFileRef_.m_pFilename;
		}
	};

	LTRESULT Open(const char* sFileName = LTNULL);
	LTRESULT Close();
	ILTStream* LockDStream();
	void UnLockDStream();
	const char* GetFileName() { return m_DStreamFileRef_.m_pFilename; };

	CDStreamOpenQueueItem* Next() { return (CDStreamOpenQueueItem*)CBaseListItem::Next(); };
	CDStreamOpenQueueItem* Prev() { return (CDStreamOpenQueueItem*)CBaseListItem::Prev(); };

private:
	friend class CDStreamOpenQueueMgr;

	LTRESULT OpenDStream();
	LTRESULT CloseDStream();

	ILTStream*				m_pDStream;					// 0x08
	CDStreamOpenQueueMgr*	m_pDStreamOpenQueueMgr;		// 0x0c
	unsigned int			m_nLockCount;				// 0x10
	FileRef					m_DStreamFileRef_;			// 0x14
	uint32					m_nSaveSeekPos;				// 0x20
};


// 0xc bytes.
class CDStreamOpenQueueList : public CBaseList
{
public:
	CDStreamOpenQueueList() { m_nNumItems = 0; };
	void Insert(CBaseListItem* pItem) { m_nNumItems++; CBaseList::Insert(pItem); };
	void InsertFirst(CBaseListItem* pItem) { m_nNumItems++; CBaseList::InsertFirst(pItem); };
	void InsertLast(CBaseListItem* pItem) { m_nNumItems++; CBaseList::InsertLast(pItem); };
	void Delete(CBaseListItem* pItem) { m_nNumItems--; CBaseList::Delete(pItem); };
	void FastDeleteAll() { m_nNumItems = 0; CBaseList::FastDeleteAll(); };

	unsigned int GetNumItems() { return m_nNumItems; };
	CDStreamOpenQueueItem* GetFirst() { return (CDStreamOpenQueueItem*)CBaseList::GetFirst(); };
	CDStreamOpenQueueItem* GetLast() { return (CDStreamOpenQueueItem*)CBaseList::GetLast(); };

private:
	unsigned int m_nNumItems;	// 0x08
};


class CDStreamOpenQueueMgr
{
public:
	void Init(int nNumItems);
	void Term();
	CDStreamOpenQueueItem* Create(const char* sFileName = LTNULL);
	void Destroy(CDStreamOpenQueueItem* pItem);
	void DestroyAll();
	LTRESULT CloseAll();

private:
	friend class CDStreamOpenQueueItem;

	void ReduceOpenedItems();

	void EnterCriticalSection() { ::EnterCriticalSection(&m_CriticalSection); };
	void LeaveCriticalSection() { ::LeaveCriticalSection(&m_CriticalSection); };

	CRITICAL_SECTION		m_CriticalSection;		// 0x00
	CDStreamOpenQueueList	m_lstOpenedItems;		// 0x18
	CDStreamOpenQueueList	m_lstClosedItems;		// 0x24
	unsigned int			m_nMaxOpenedItems;		// 0x30
};

#endif
