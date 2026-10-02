// Jupiter runtime/kernel/src/sys/win/lt_cursor_impl.cpp
// Talon creates the cursor manager for a client manager (no interface holders) and reaches
// ILTClient through it. GetCursorMode and CLTCursorInst::GetData were identical-code folded
// into other units (0x004173d0, 0x00465e00).
#include <windows.h>
#include "bdefs.h"
#include "iltclient.h"
#include "iltcursor.h"
#include "clientmgr.h"


//
//Internal implementation class for ILTCursorInst.
//

class CLTCursorInst : public ILTCursorInst
{
public:

	CLTCursorInst() { m_hCursor = LTNULL; };
	~CLTCursorInst() {};

	virtual LTRESULT IsValid() { return (m_hCursor ? LT_YES : LT_NO); };
	HCURSOR GetCursor() { return m_hCursor; };
	virtual void SetData(const void *pData) { m_hCursor = (HCURSOR)(pData); };
	virtual void *GetData() { return (void *)m_hCursor; };

	HCURSOR m_hCursor;
};


//
//Our implementation class for the ILTCursor interface.
//

class CLTCursor : public ILTCursor
{
public:

	CLTCursor(CClientMgr *pClientMgr)
	{
		m_pClientMgr = pClientMgr;
		m_hCurrentCursor = LTNULL;
		m_eCursorMode = CM_None;
	}

	// Enable/disable hardware cursor.
	virtual LTRESULT	SetCursorMode(CursorMode cMode);

	// Get current cursor mode.  Always returns LT_OK and always fills in cMode.
	virtual LTRESULT	GetCursorMode(CursorMode &cMode);

	// Returns LT_YES if a hardware cursor can be used, LT_NO otherwise.
	// Since we can't detect this, just return LT_YES for now.
	virtual LTRESULT	IsCursorModeAvailable(CursorMode cMode);

	// Set the current hardware cursor bitmap.  The bitmap comes from cshell.dll.
	virtual LTRESULT	LoadCursorBitmapResource(const char *pName, HLTCURSOR &hCursor);

	// Free a cursor.
	virtual LTRESULT	FreeCursor(const HLTCURSOR hCursor);

	// Set the current cursor.
	virtual LTRESULT	SetCursor(HLTCURSOR hCursor);

	// Check if an HLTCURSOR is a valid one; returns LT_YES or LT_NO
	virtual LTRESULT	IsValidCursor(HLTCURSOR hCursor);

	// Refresh the cursor
	virtual LTRESULT	RefreshCursor();

protected:

	LTRESULT		PreSetMode(CursorMode eNewMode);

	CursorMode		m_eCursorMode;		// 0x04
	HLTCURSOR		m_hCurrentCursor;	// 0x08
	CClientMgr		*m_pClientMgr;		// 0x0c
};


// FUNCTION: LITHTECH 0x00446e50
ILTCursor* CreateCursorMgr(CClientMgr *pClientMgr)
{
	return new CLTCursor(pClientMgr);
}


// FUNCTION: LITHTECH 0x00446e80
LTRESULT CLTCursor::PreSetMode(CursorMode eNewMode)
{
	int temp;

	temp = 0;

	switch(eNewMode)
	{
		case CM_Hardware:
			do {
				temp = ShowCursor(LTTRUE);
			} while (temp < 0);
			break;
		case CM_None:
			do {
				temp = ShowCursor(LTFALSE);
			} while (temp >= 0);
			break;
		default:
			break;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00446ec0
LTRESULT CLTCursor::SetCursorMode(CursorMode cMode)
{
	// Saaaaaanity check
	if (!IsCursorModeAvailable(cMode)) {
		return LT_UNSUPPORTED;
	}

	// If we're already in this mode, let's just save some cycles, shall we?
	if (cMode == m_eCursorMode) {
		return LT_OK;
	}

	PreSetMode(cMode);
	m_eCursorMode = cMode;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00446f00
LTRESULT CLTCursor::IsValidCursor(HLTCURSOR hCursor)
{
	return hCursor->IsValid();
}

// FUNCTION: LITHTECH 0x00446f10
LTRESULT CLTCursor::SetCursor(HLTCURSOR hCursor)
{
	if (!hCursor->IsValid())
		return LT_INVALIDPARAMS;

	::SetCursor((HCURSOR)hCursor->GetData());

	return LT_OK;
}

LTRESULT CLTCursor::GetCursorMode(CursorMode &cMode)
{
	cMode = m_eCursorMode;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00446f40
LTRESULT CLTCursor::FreeCursor(const HLTCURSOR hCursor)
{
	/* We don't really do anything here, since we're not presently creating the cursor
	 * from scratch. */

	delete ((CLTCursorInst*)hCursor);

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00446f60
LTRESULT CLTCursor::IsCursorModeAvailable(CursorMode cMode)
{
	/* Eventually there may be more checks here */

	return LT_YES;
}

// FUNCTION: LITHTECH 0x00446f70
LTRESULT CLTCursor::LoadCursorBitmapResource(const char *pName, HLTCURSOR &hCursor)
{
	HINSTANCE hInst;
	LTRESULT dRes;
	HCURSOR hWinCursor;
	HLTCURSOR hNew;

	if (!m_pClientMgr)
		return LT_NOTINITIALIZED;

	if ((dRes = m_pClientMgr->m_pClientDE->GetEngineHook("cres_hinstance",(void **)&hInst)) != LT_OK)
		return dRes;

	hWinCursor = (HCURSOR)(::LoadImage(hInst, pName, IMAGE_CURSOR, 0, 0, LR_DEFAULTCOLOR));

	if (!hWinCursor)
		return LT_MISSINGCURSORRESOURCE;

	hNew = new CLTCursorInst();

	hNew->SetData((void *)hWinCursor);
	hCursor = hNew;

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00447020
LTRESULT CLTCursor::RefreshCursor()
{
	int temp;

	temp = 0;

	switch(m_eCursorMode)
	{
		case CM_Hardware:
			do {
				temp = ShowCursor(LTTRUE);
			} while (temp < 0);
			break;
		case CM_None:
			do {
				temp = ShowCursor(LTFALSE);
			} while (temp >= 0);
			break;
		default:
			break;
	}

	return LT_OK;
}

// FUNCTION: LITHTECH 0x00447000 ?IsValid@CLTCursorInst@@UAEKXZ
// FUNCTION: LITHTECH 0x00447010 ?SetData@CLTCursorInst@@UAEXPBX@Z
