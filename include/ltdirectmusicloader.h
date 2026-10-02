// Jupiter runtime/kernel/src/sys/win/ltdirectmusicloader.h, Talon layout.
// Talon builds against the DirectX 7 DirectMusic interfaces (IDirectMusicLoader, IDirectMusicGetLoader,
// IDirectMusicObject), not the ...8 ones, and always uses DStreams (USE_DSTREAM).
// Units including this need the DirectX headers on the path (dmusici.h includes <dmusicc.h>):
//   // FLAGS: /O2 /IE:/AVP2Source/jupiter/dx9inc
#ifndef __LTDIRECTMUSICLOADER_H__
#define __LTDIRECTMUSICLOADER_H__

#include <windows.h>
#include <objbase.h>
#ifndef DIRECTSOUND_VERSION
#define DIRECTSOUND_VERSION 0x0700	// VC6's basetsd.h has no DWORD_PTR for dsound.h's DX8 parts
#endif
#include "../../jupiter/dx9inc/dmusici.h"
#include "dstreamopenqueuemgr.h"

class CLoadDirByClassList;
class CFileStreamList;


// global variable to hold our CDStreamOpenQueueMgr object
// GLOBAL: LITHTECH 0x004e4474
extern CDStreamOpenQueueMgr g_LTDMDStreamOpenQueueMgr;


// 0x18 bytes.
class CLTDMObjectRef
{
public:
	CLTDMObjectRef() { m_pNext = NULL; m_pObject = NULL; };
	CLTDMObjectRef*			m_pNext;		// 0x00
	GUID					m_guidObject;	// 0x04
	IDirectMusicObject*		m_pObject;		// 0x14
};


// 0x38 bytes.
class CLTDMLoader : public IDirectMusicLoader
{
public:
	// IUnknown
	//
	virtual STDMETHODIMP QueryInterface(const IID &iid, void **ppv);
	virtual STDMETHODIMP_(ULONG) AddRef();
	virtual STDMETHODIMP_(ULONG) Release();

	// IDirectMusicLoader
	virtual STDMETHODIMP GetObject(LPDMUS_OBJECTDESC pDesc, REFIID, LPVOID FAR *);
	virtual STDMETHODIMP SetObject(LPDMUS_OBJECTDESC pDesc);
	virtual STDMETHODIMP SetSearchDirectory(REFGUID rguidClass, WCHAR *pwzPath, BOOL fClear);
	virtual STDMETHODIMP ScanDirectory(REFGUID rguidClass, WCHAR *pwzFileExtension, WCHAR *pwzScanFileName);
	virtual STDMETHODIMP CacheObject(IDirectMusicObject * pObject);
	virtual STDMETHODIMP ReleaseObject(IDirectMusicObject * pObject);
	virtual STDMETHODIMP ClearCache(REFGUID rguidClass);
	virtual STDMETHODIMP EnableCache(REFGUID rguidClass, BOOL fEnable);
	virtual STDMETHODIMP EnumObject(REFGUID rguidClass, DWORD dwIndex, LPDMUS_OBJECTDESC pDesc);
	CLTDMLoader();
	~CLTDMLoader();
	ULONG				AddRefP();			// Private AddRef, for streams.
	ULONG				ReleaseP();			// Private Release, for streams.
	HRESULT				Init();

	// New functions added for LT version of directmusicloader
	STDMETHODIMP		SetSearchDirectory(REFGUID rguidClass, const char* sPath, BOOL fClear);
	CFileStreamList*	GetFileStreamList() { return m_pFileStreamList; };

	void ClearObjectList()
	{
		// used at end of level to get rid of references to objects
		while (m_pObjectList)
		{
			CLTDMObjectRef * pObject = m_pObjectList;
			m_pObjectList = pObject->m_pNext;

			if (pObject->m_pObject)
			{
				pObject->m_pObject->Release();
			}
			delete pObject;
		}
	}

private:
	HRESULT				LoadFromFile(LPDMUS_OBJECTDESC pDesc,
							IDirectMusicObject ** ppIObject);
	HRESULT				LoadFromMemory(LPDMUS_OBJECTDESC pDesc,
							IDirectMusicObject ** ppIObject);
	long				m_cRef;				// 0x04 Regular COM reference count.
	long				m_cPRef;			// 0x08 Private reference count.
	CRITICAL_SECTION	m_CriticalSection;	// 0x0c Critical section to manage internal object list.
	CLTDMObjectRef*		m_pObjectList;		// 0x24 List of already loaded objects.

	// New member variables added for LT version of directmusicloader
	CLoadDirByClassList* m_pLoadDirByClassList;	// 0x28
	CFileStreamList*	m_pFileStreamList;		// 0x2c
	WCHAR				m_wszForwardSlash[2];	// 0x30
	WCHAR				m_wszBackSlash[2];		// 0x34
};


// 0x14 bytes.
class CLTDMFileStream : public IStream, public IDirectMusicGetLoader
{
public:
	// IUnknown
	//
	virtual STDMETHODIMP QueryInterface(const IID &iid, void **ppv);
	virtual STDMETHODIMP_(ULONG) AddRef();
	virtual STDMETHODIMP_(ULONG) Release();

	/* IStream methods */
	virtual STDMETHODIMP Read(void* pv, ULONG cb, ULONG* pcbRead);
	virtual STDMETHODIMP Write(const void* pv, ULONG cb, ULONG* pcbWritten);
	virtual STDMETHODIMP Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER* plibNewPosition);
	virtual STDMETHODIMP SetSize(ULARGE_INTEGER /*libNewSize*/);
	virtual STDMETHODIMP CopyTo(IStream* /*pstm */, ULARGE_INTEGER /*cb*/,
						 ULARGE_INTEGER* /*pcbRead*/,
						 ULARGE_INTEGER* /*pcbWritten*/);
	virtual STDMETHODIMP Commit(DWORD /*grfCommitFlags*/);
	virtual STDMETHODIMP Revert();
	virtual STDMETHODIMP LockRegion(ULARGE_INTEGER /*libOffset*/, ULARGE_INTEGER /*cb*/,
							 DWORD /*dwLockType*/);
	virtual STDMETHODIMP UnlockRegion(ULARGE_INTEGER /*libOffset*/, ULARGE_INTEGER /*cb*/,
							   DWORD /*dwLockType*/);
	virtual STDMETHODIMP Stat(STATSTG* /*pstatstg*/, DWORD /*grfStatFlag*/);
	virtual STDMETHODIMP Clone(IStream** /*ppstm*/);

	/* IDirectMusicGetLoader */
	virtual STDMETHODIMP GetLoader(IDirectMusicLoader ** ppLoader);

						CLTDMFileStream(CLTDMLoader *pLoader);
						~CLTDMFileStream();
	HRESULT				Open(WCHAR *lpFileName, DWORD dwDesiredAccess);
	HRESULT				Close();

private:
	LONG					m_cRef;				// 0x08 object reference count
	CDStreamOpenQueueItem*	m_pOpenQueueItem;	// 0x0c
	CLTDMLoader*			m_pLoader;			// 0x10 pointer to the loader
};


// 0x28 bytes.
class CLTDMMemStream : public IStream, public IDirectMusicGetLoader
{
public:
	// IUnknown
	//
	virtual STDMETHODIMP QueryInterface(const IID &iid, void **ppv);
	virtual STDMETHODIMP_(ULONG) AddRef();
	virtual STDMETHODIMP_(ULONG) Release();

	/* IStream methods */
	virtual STDMETHODIMP Read(void* pv, ULONG cb, ULONG* pcbRead);
	virtual STDMETHODIMP Write(const void* pv, ULONG cb, ULONG* pcbWritten);
	virtual STDMETHODIMP Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER* plibNewPosition);
	virtual STDMETHODIMP SetSize(ULARGE_INTEGER /*libNewSize*/);
	virtual STDMETHODIMP CopyTo(IStream* /*pstm */, ULARGE_INTEGER /*cb*/,
						 ULARGE_INTEGER* /*pcbRead*/,
						 ULARGE_INTEGER* /*pcbWritten*/);
	virtual STDMETHODIMP Commit(DWORD /*grfCommitFlags*/);
	virtual STDMETHODIMP Revert();
	virtual STDMETHODIMP LockRegion(ULARGE_INTEGER /*libOffset*/, ULARGE_INTEGER /*cb*/,
							 DWORD /*dwLockType*/);
	virtual STDMETHODIMP UnlockRegion(ULARGE_INTEGER /*libOffset*/, ULARGE_INTEGER /*cb*/,
							   DWORD /*dwLockType*/);
	virtual STDMETHODIMP Stat(STATSTG* /*pstatstg*/, DWORD /*grfStatFlag*/);
	virtual STDMETHODIMP Clone(IStream** /*ppstm*/);

	/* IDirectMusicGetLoader */
	virtual STDMETHODIMP GetLoader(IDirectMusicLoader ** ppLoader);

						CLTDMMemStream(CLTDMLoader *pLoader);
						~CLTDMMemStream();
	HRESULT				Open(BYTE *pbData, LONGLONG llLength);
	HRESULT				Close();

private:
	LONG			m_cRef;			// 0x08 object reference count
	BYTE*			m_pbData;		// 0x0c memory pointer
	LONGLONG		m_llLength;		// 0x10
	LONGLONG		m_llPosition;	// 0x18 Current file position.
	CLTDMLoader*	m_pLoader;		// 0x20
};

#endif
