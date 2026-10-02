// Jupiter runtime/shared/src/sys/win/dstreamopenqueuemgr.cpp
// Talon opens files through cf_OpenFile(g_pClientMgr->m_hFileMgr) instead of the IClientFileMgr holder.
#include <string.h>
#include "dstreamopenqueuemgr.h"
#include "clientmgr.h"


// open a ILTStream (calls close if old one was open)
// FUNCTION: LITHTECH 0x00433d50
LTRESULT CDStreamOpenQueueItem::Open(const char* sFileName)
{
	m_pDStreamOpenQueueMgr->EnterCriticalSection();

	// make sure the lock count is 0
	if (m_nLockCount > 0)
	{
		m_pDStreamOpenQueueMgr->LeaveCriticalSection();
		return LT_ERROR;
	}

	// close the file if it was open
	if (m_pDStream != LTNULL) Close();

	// check if we were passed in a new file name
	if (sFileName != LTNULL)
	{
		// assign the new file name to the item
		if (m_DStreamFileRef_.m_pFilename != LTNULL) {
			delete (char*)m_DStreamFileRef_.m_pFilename;
		}

		m_DStreamFileRef_.m_pFilename = new char[strlen(sFileName)+1];
		if (m_DStreamFileRef_.m_pFilename == LTNULL)
		{
			m_pDStreamOpenQueueMgr->LeaveCriticalSection();
			return LT_ERROR;
		}
		strcpy((char*)m_DStreamFileRef_.m_pFilename, sFileName);
	}

	// open the dstream
	LTRESULT nResult = OpenDStream();

	m_pDStreamOpenQueueMgr->LeaveCriticalSection();

	return nResult;
};


// close a ILTStream and set to LTNULL
// FUNCTION: LITHTECH 0x00433e00
LTRESULT CDStreamOpenQueueItem::Close()
{
	m_pDStreamOpenQueueMgr->EnterCriticalSection();

	// make sure the lock count is 0
	if (m_nLockCount > 0)
	{
		m_pDStreamOpenQueueMgr->LeaveCriticalSection();
		return LT_ERROR;
	}

	// check if it is already closed
	if (m_pDStream == LTNULL)
	{
		m_pDStreamOpenQueueMgr->LeaveCriticalSection();
		return LT_OK;
	}

	// call the close function for the dstream
	LTRESULT nResult = CloseDStream();

	m_pDStreamOpenQueueMgr->LeaveCriticalSection();
	return nResult;
};


// accessor for the ILTStream this locks it open
// FUNCTION: LITHTECH 0x00433e60
ILTStream* CDStreamOpenQueueItem::LockDStream()
{
	m_pDStreamOpenQueueMgr->EnterCriticalSection();

	// increment lock count
	m_nLockCount++;

	// check if the ILTStream does not exist
	if (m_pDStream == LTNULL)
	{
		// open the ILTStream
		if (OpenDStream() != LT_OK)
		{
			// if it failed to open then we must error
			m_nLockCount--;
			m_pDStreamOpenQueueMgr->LeaveCriticalSection();
			return LTNULL;
		}
	}

	// if the stream is already opened then we just need to put it as the most recently used in the opened list
	else
	{
		// see if it is not already at the top before we move it
		if (m_pDStreamOpenQueueMgr->m_lstOpenedItems.GetFirst() != this)
		{
			m_pDStreamOpenQueueMgr->m_lstOpenedItems.Delete(this);
			m_pDStreamOpenQueueMgr->m_lstOpenedItems.InsertFirst(this);
		}
	}

	m_pDStreamOpenQueueMgr->LeaveCriticalSection();

	return m_pDStream;
};


// unlock the ILTStream for this item
// FUNCTION: LITHTECH 0x00433ee0
void CDStreamOpenQueueItem::UnLockDStream()
{
	m_pDStreamOpenQueueMgr->EnterCriticalSection();

	// make sure counter is not already 0
	if (m_nLockCount == 0)
	{
		m_pDStreamOpenQueueMgr->LeaveCriticalSection();
		return;
	}

	// decrement lock count
	m_nLockCount--;

	// reduce any opened files over our maximum
	m_pDStreamOpenQueueMgr->ReduceOpenedItems();

	m_pDStreamOpenQueueMgr->LeaveCriticalSection();
};


// open the dstream
// FUNCTION: LITHTECH 0x00433f20
LTRESULT CDStreamOpenQueueItem::OpenDStream()
{
	m_pDStreamOpenQueueMgr->EnterCriticalSection();

	// make sure we have a file name
	if (m_DStreamFileRef_.m_pFilename == LTNULL)
	{
		m_pDStreamOpenQueueMgr->LeaveCriticalSection();
		return LT_ERROR;
	}

	// check if the file is already opened
	if (m_pDStream != LTNULL)
	{
		m_pDStreamOpenQueueMgr->LeaveCriticalSection();
		return LT_OK;
	}

	// set the file type
	m_DStreamFileRef_.m_FileType = FILE_ANYFILE;

	// open the file
	m_pDStream = cf_OpenFile(g_pClientMgr->m_hFileMgr, &m_DStreamFileRef_);
	if (m_pDStream == LTNULL)
	{
		m_pDStreamOpenQueueMgr->LeaveCriticalSection();
		return LT_ERROR;
	}

	// seek to the position
	if (m_nSaveSeekPos != 0)
	{
		// seek to the saved position
		m_pDStream->SeekTo(m_nSaveSeekPos);
	}

	// since this is the most recently used put it at the top of the Opened list
	m_pDStreamOpenQueueMgr->m_lstClosedItems.Delete(this);
	m_pDStreamOpenQueueMgr->m_lstOpenedItems.InsertFirst(this);

	// reduce any opened files over our maximum
	m_pDStreamOpenQueueMgr->ReduceOpenedItems();

	m_pDStreamOpenQueueMgr->LeaveCriticalSection();

	return LT_OK;
};


// close the dstream
// FUNCTION: LITHTECH 0x00433fe0
LTRESULT CDStreamOpenQueueItem::CloseDStream()
{
	m_pDStreamOpenQueueMgr->EnterCriticalSection();

	// check if the file is already closed
	if (m_pDStream == LTNULL)
	{
		m_pDStreamOpenQueueMgr->LeaveCriticalSection();
		return LT_OK;
	}

	// save off our current position in the stream
	m_pDStream->GetPos(&m_nSaveSeekPos);

	// close the file
	m_pDStream->Release();
	m_pDStream = LTNULL;

	// move the file from the opened list to the closed list
	m_pDStreamOpenQueueMgr->m_lstOpenedItems.Delete(this);
	m_pDStreamOpenQueueMgr->m_lstClosedItems.Insert(this);

	m_pDStreamOpenQueueMgr->LeaveCriticalSection();

	return LT_OK;
};


// Initialize the queue with the specified number of items
// FUNCTION: LITHTECH 0x00434050
void CDStreamOpenQueueMgr::Init(int nNumItems)
{
	// initialize the critical section
	InitializeCriticalSection(&m_CriticalSection);

	// set the maximum number of open items we are targeting
	m_nMaxOpenedItems = nNumItems;
};


// Terminate the queue releasing all items
// FUNCTION: LITHTECH 0x00434070
void CDStreamOpenQueueMgr::Term()
{
	// destroy all of the items
	DestroyAll();

	// delete the critical section
	DeleteCriticalSection(&m_CriticalSection);
};


// Create a new item
// FUNCTION: LITHTECH 0x00434090
CDStreamOpenQueueItem* CDStreamOpenQueueMgr::Create(const char* sFileName)
{
	// make sure file name is not null
	if (sFileName == LTNULL) return LTNULL;

	// create a new item
	CDStreamOpenQueueItem* pItem;
	pItem = new CDStreamOpenQueueItem(this);
	if (pItem == LTNULL) return LTNULL;

	// set the filename in the new item unless it was LTNULL
	if (sFileName != LTNULL)
	{
		// assign the new file name to the item
		pItem->m_DStreamFileRef_.m_pFilename = new char[strlen(sFileName)+1];
		if (pItem->m_DStreamFileRef_.m_pFilename == LTNULL)
		{
			return LTNULL;
		}
		strcpy((char*)pItem->m_DStreamFileRef_.m_pFilename, sFileName);
	}
	else pItem->m_DStreamFileRef_.m_pFilename = LTNULL;

	// insert the new item into the closed list
	EnterCriticalSection();
	m_lstClosedItems.Insert(pItem);
	LeaveCriticalSection();

	return pItem;
};


// Destroy an item that was created
// FUNCTION: LITHTECH 0x00434140
void CDStreamOpenQueueMgr::Destroy(CDStreamOpenQueueItem* pItem)
{
	// make sure the item is not null
	if (pItem == LTNULL) return;

	EnterCriticalSection();

	// is this an open item
	if (pItem->m_pDStream != LTNULL)
	{
		// close the item
		if (pItem->Close() != LT_OK)
		{
			// if we failed to close the item remove it from the open list
			m_lstOpenedItems.Delete(pItem);
		}
		else
		{
			// remove it from the closed list
			m_lstClosedItems.Delete(pItem);
		}
	}

	// if the item is already closed
	else
	{
		// remove it from the closed list
		m_lstClosedItems.Delete(pItem);
	}

	LeaveCriticalSection();

	// delete the item
	delete pItem;
};


// Destroy all items
// FUNCTION: LITHTECH 0x004341b0
void CDStreamOpenQueueMgr::DestroyAll()
{
	EnterCriticalSection();

	// close all items
	if (CloseAll() != LT_OK)
	{
		// go through closed list and destroy all items if they could not all be closed
		CDStreamOpenQueueItem* pItem = m_lstOpenedItems.GetFirst();
		while (pItem != LTNULL)
		{
			// destroy the item
			Destroy(pItem);

			// get first item again
			pItem = m_lstOpenedItems.GetFirst();
		}
	}

	// go through closed list and destroy all items
	CDStreamOpenQueueItem* pItem = m_lstClosedItems.GetFirst();
	while (pItem != LTNULL)
	{
		// destroy the item
		Destroy(pItem);

		// get first item again
		pItem = m_lstClosedItems.GetFirst();
	}

	LeaveCriticalSection();
};


// Close all items
// FUNCTION: LITHTECH 0x00434200
LTRESULT CDStreamOpenQueueMgr::CloseAll()
{
	EnterCriticalSection();

	// go through closed list and destroy all items
	CDStreamOpenQueueItem* pItem = m_lstOpenedItems.GetFirst();
	while (pItem != LTNULL)
	{
		// close the item
		if (pItem->Close() != LT_OK)
		{
			LeaveCriticalSection();
			return LT_ERROR;
		}

		// get next item
		pItem = pItem->Next();
	}

	LeaveCriticalSection();

	return LT_OK;
};


// reduce the number of opened files down to the max if possible
// FUNCTION: LITHTECH 0x00434240
void CDStreamOpenQueueMgr::ReduceOpenedItems()
{
	EnterCriticalSection();

	// loop through opened items until we are not under the max opened or we have looked at them all
	// we must go through the list in reverse order because we want to get ride of the oldest items
	CDStreamOpenQueueItem* pItem = m_lstOpenedItems.GetLast();
	CDStreamOpenQueueItem* pPrevItem;
	while ((m_lstOpenedItems.GetNumItems() > m_nMaxOpenedItems) && (pItem != LTNULL))
	{
		// get the next item we will look at
		pPrevItem = pItem->Prev();

		// is this item not locked
		if (pItem->m_nLockCount == 0)
		{
			// close the item
			pItem->CloseDStream();
		}

		// go to next item
		pItem = pPrevItem;
	}

	LeaveCriticalSection();
};
