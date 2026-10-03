// Jupiter runtime/client/src/debuggraphmgr.cpp. Talon's graphs keep their ILTClient
// (m_pClientDE) instead of a holder; the linker dropped everything nothing references
// (DebugGraph::Init/InitLabel, the color tables, FindGraph, UpdateGraph...).
// FLAGS: /O2 /GX-
#include "bdefs.h"
#include "iltclient.h"
#include "ltdynarray.h"
#include "debuggraphmgr.h"


// Constants
#define	DG_MINWIDTH		32
#define	DG_MINHEIGHT	16
#define DG_DEFAULTSPACING	2
#define DG_DEFAULTWIDTH		158
#define DG_DEFAULTHEIGHT	78

#define DG_BORDERWIDTH	1
#define DG_BORDERHEIGHT	1
#define DG_BORDERCOLOR	SETRGB(255,0,0)





// ------------------------------------------------------------------ //
// DebugGraph
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x00430750
DebugGraph::~DebugGraph()
{
	Term();
}


// FUNCTION: LITHTECH 0x00430760
LTRESULT DebugGraph::IsInitted()
{
	return (m_pClientDE != NULL) ? LT_OK : LT_NOTINITIALIZED;
}


// FUNCTION: LITHTECH 0x00430770
LTRESULT DebugGraph::Term()
{
	if(m_pClientDE)
	{
		if(m_hSurface)
		{
			m_pClientDE->DeleteSurface(m_hSurface);
		}
	}

	m_hSurface = LTNULL;
	m_pClientDE = LTNULL;

	return LT_OK;
}


// FUNCTION: LITHTECH 0x004307a0
LTRESULT DebugGraph::AddSample(DGSample &sample, LTBOOL bOverwrite)
{
	LTRESULT dResult;
	LTRect rShift, rFill;
	float fVal;

	if(IsInitted() != LT_OK)
		return LT_NOTINITIALIZED;

	// Initialize the width of the rectangle
	rFill.left = GetWidth() - (DG_BORDERWIDTH + 1);
	rFill.right = rFill.left + 1;

	if(!bOverwrite)
	{
		// Shift the previous contents over.
		rShift.left = DG_BORDERWIDTH + 1;
		rShift.top = DG_BORDERHEIGHT;
		rShift.right = GetWidth();
		rShift.bottom = GetHeight() - DG_BORDERHEIGHT;

		dResult = m_pClientDE->DrawSurfaceToSurface(
			m_hSurface,
			m_hSurface,
			&rShift,
			DG_BORDERWIDTH,
			DG_BORDERHEIGHT);
		if(dResult != LT_OK)
			return dResult;

		// First clear out the area.
		rFill.top = DG_BORDERHEIGHT;
		rFill.bottom = GetHeight() - DG_BORDERHEIGHT;

		dResult = m_pClientDE->FillRect(m_hSurface, &rFill, SETRGB(0,0,0));
		if(dResult != LT_OK)
			return dResult;
	}

	// Now draw the slice.
	fVal = sample.m_fValue * (float)GetHeight();
	rFill.top = GetMaxLineHeight() - (int)fVal;
	rFill.bottom = GetHeight() - DG_BORDERHEIGHT;

	return m_pClientDE->FillRect(m_hSurface, &rFill, sample.m_Color);
}


// FUNCTION: LITHTECH 0x004308c0
LTRESULT DebugGraph::Draw()
{
	LTRESULT dResult;
	uint32 width, height;


	if(IsInitted() != LT_OK)
		return LT_NOTINITIALIZED;

	dResult = m_pClientDE->DrawSurfaceToSurfaceTransparent(
		m_pClientDE->GetScreenSurface(),
		m_hSurface,
		LTNULL,
		m_rRect.left,
		m_rRect.top,
		SETRGB_T(0,0,0));
	if(dResult != LT_OK)
		return dResult;

	// Draw the label above us.
	if(m_hLabel)
	{
		m_pClientDE->GetSurfaceDims(m_hLabel, &width, &height);

		dResult = m_pClientDE->DrawSurfaceToSurfaceTransparent(
			m_pClientDE->GetScreenSurface(),
			m_hLabel,
			LTNULL,
			m_rRect.left,
			m_rRect.top - (int)height,
			SETRGB_T(0,0,0));

		if(dResult != LT_OK)
			return dResult;
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00430960
LTRESULT DebugGraph::MoveTo(LTRect *pRect)
{
	if(!pRect)
		return LT_ERROR;

	if((pRect->top + m_LabelHeight >= pRect->bottom) || (pRect->left >= pRect->right))
		return LT_ERROR;

	m_rRect = *pRect;
	m_rRect.top += m_LabelHeight;

	return LT_OK;
}



// ------------------------------------------------------------------ //
// CDebugGraphMgr
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x004309c0
CDebugGraphMgr::CDebugGraphMgr()
{
	m_pClientDE = LTNULL;
	m_nGraphWidth = DG_DEFAULTWIDTH;
	m_nGraphHeight = DG_DEFAULTHEIGHT;
	m_nGraphSpacing = DG_DEFAULTSPACING;
	m_nGraphXCount = 0;
	m_nGraphYCount = 0;
	m_nLeftBorder = 0;
	m_nBottomBorder = 0;
}


// FUNCTION: LITHTECH 0x00430a70
CDebugGraphMgr::~CDebugGraphMgr()
{
	Term();
}


// FUNCTION: LITHTECH 0x00430ae0
LTRESULT CDebugGraphMgr::Init(ILTClient *pClientDE, LTRect *pRect)
{
	if(!pClientDE)
		return LT_ERROR;

	LTRESULT result = MoveTo(pRect);

	if(result != LT_OK)
		return result;

	if(IsInitted())
		Term();

	m_ActiveIDs.Init();
	m_ActiveGraphs.Init();

	m_pClientDE = pClientDE;
	return LT_OK;
}


// Folded into DebugGraph::IsInitted (0x00430760).
LTRESULT CDebugGraphMgr::IsInitted()
{
	return (m_pClientDE != NULL) ? LT_OK : LT_NOTINITIALIZED;
}


// FUNCTION: LITHTECH 0x00430b70
LTRESULT CDebugGraphMgr::Term()
{
	if(!IsInitted())
		return LT_OK;

	// Clear out the graph list
	FlushGraphs();

	m_ActiveIDs.Term();
	m_ActiveGraphs.Term();

	m_pClientDE = LTNULL;
	return LT_OK;
}


// FUNCTION: LITHTECH 0x00430c00
void CDebugGraphMgr::FlushCache()
{
	m_FindCacheID = LTNULL;
	m_FindCacheGraph = LTNULL;
	m_FindCacheIndex = 0;
}


// FUNCTION: LITHTECH 0x00430c10
void CDebugGraphMgr::RemoveGraph(uint32 index)
{
	// Delete the graph from the list
	m_ActiveIDs.Remove(index);
	delete m_ActiveGraphs[index].m_pGraph;
	m_ActiveGraphs.Remove(index);

	// Flush the cache if necessary
	if(index <= m_FindCacheIndex)
		FlushCache();

	// Update the graph positions
	LTRect rect;
	while(index < m_ActiveGraphs.GetSize())
	{
		// Get the new rectangle
		if(!CalcGraphRect(rect, index))
			break;
		// Move the graph
		if(!m_ActiveGraphs[index].m_pGraph->MoveTo(&rect))
		{
			// If it couldn't be moved, remove it too and jump out
			RemoveGraph(index);
			return;
		}
		index++;
	}
}


// FUNCTION: LITHTECH 0x00430cd0
void CDebugGraphMgr::FlushGraphs()
{
	while(m_ActiveGraphs.GetSize())
		RemoveGraph(m_ActiveGraphs.GetSize() - 1);
	m_ActiveIDs.RemoveAll();
}


// FUNCTION: LITHTECH 0x00430d10
LTRESULT CDebugGraphMgr::Draw()
{
	// Shortcut out if nothing's in the list
	if(!m_ActiveGraphs.GetSize())
		return LT_OK;

	LTRESULT dResult = LT_OK;

	// Draw all of the graphs
	uint32 i,j;

	for(i = 0; (i < m_ActiveGraphs.GetSize()) && (dResult == LT_OK); i++)
	{
		DGTracker *pActiveGraph = &m_ActiveGraphs[i];
		DebugGraph *pGraph = pActiveGraph->m_pGraph;
		// Draw each of the samples
		if(pActiveGraph->m_Samples.GetSize())
		{
			for(j = 0; j < pActiveGraph->m_Samples.GetSize(); j++)
				pGraph->AddSample(pActiveGraph->m_Samples[j], j != 0);
			// Clear out the array without killing the cache
			pActiveGraph->m_Samples.NiceSetSize(0);
		}
		dResult = pGraph->Draw();
	}

	return dResult;
}


// FUNCTION: LITHTECH 0x00430e60
LTBOOL CDebugGraphMgr::CalcGraphRect(LTRect &rRect, uint32 nIndex)
{
	// Make sure we've got space left
	if(nIndex >= (m_nGraphXCount * m_nGraphYCount))
		return LTFALSE;

	uint32 nGraphX = m_nGraphXCount - ((nIndex / m_nGraphXCount) + 1);
	uint32 nGraphY = nIndex % m_nGraphYCount;

	// Get the rectangle based on the grid coordinate, left border, and grid size and spacing
	rRect.left = nGraphX * (m_nGraphWidth + m_nGraphSpacing) + m_nLeftBorder + m_nGraphSpacing;
	rRect.top = nGraphY * (m_nGraphHeight + m_nGraphSpacing);
	rRect.right = rRect.left + m_nGraphWidth;
	rRect.bottom = rRect.top + m_nGraphHeight;

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x00430ee0
LTRESULT CDebugGraphMgr::SetGraphSize(uint32 nWidth, uint32 nHeight)
{
	// Verify the new parameters
	if((nWidth < DG_MINWIDTH) || (nHeight < DG_MINHEIGHT) ||
		((nWidth + m_nGraphSpacing) > (uint32)(m_rRect.right - m_rRect.left)) ||
		((nHeight + m_nGraphSpacing) > (uint32)(m_rRect.bottom - m_rRect.top)))
		return LT_ERROR;

	// Remove the current graphs
	FlushGraphs();

	// Save the new sizes
	m_nGraphWidth = nWidth;
	m_nGraphHeight = nHeight;

	// Update the grid size
	m_nGraphXCount = (m_rRect.right - m_rRect.left) / (m_nGraphWidth + m_nGraphSpacing);
	m_nGraphYCount = (m_rRect.bottom - m_rRect.top) / (m_nGraphHeight + m_nGraphSpacing);

	// Update the borders
	m_nLeftBorder = (m_rRect.right - m_rRect.left) - (m_nGraphXCount * m_nGraphWidth);
	m_nBottomBorder = (m_rRect.bottom - m_rRect.top) - (m_nGraphYCount * m_nGraphHeight);

	return LT_OK;
}


// FUNCTION: LITHTECH 0x00430f90
LTRESULT CDebugGraphMgr::MoveTo(LTRect *pRect)
{
	if(!pRect)
		return LT_ERROR;

	LTRESULT result;

	// Change the rectangle
	m_rRect = *pRect;
	// Resize the graphs
	result = SetGraphSize(m_nGraphWidth, m_nGraphHeight);
	// If it didn't work, set the graph counts to 0 to avoid drawing anything
	if(result != LT_OK)
	{
		m_nGraphXCount = 0;
		m_nGraphYCount = 0;
	}

	return result;
}


// Template code this object instantiated first (the linker kept these copies).
// FUNCTION: LITHTECH 0x00430ff0 ?GenAppend@?$CMoArray@VDGSample@@VDefaultCache@@@@UAEHAAVDGSample@@@Z
// FUNCTION: LITHTECH 0x00431100 ?GenRemoveAt@?$CMoArray@VDGSample@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00431210 ?GenCopyList@?$CMoArray@VDGSample@@VDefaultCache@@@@UAEHABV?$GenList@VDGSample@@@@@Z
// FUNCTION: LITHTECH 0x00431340 ?GenAppendList@?$CMoArray@VDGSample@@VDefaultCache@@@@UAEHABV?$GenList@VDGSample@@@@@Z
// FUNCTION: LITHTECH 0x00431420 ?GenFindElement@?$CMoArray@VDGSample@@VDefaultCache@@@@UBEHABVDGSample@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00431450 ?GenBegin@?$CMoArray@PAXVDefaultCache@@@@UBE?AVGenListPos@@XZ
// FUNCTION: LITHTECH 0x00431460 ?GenFindElement@?$CMoArray@PAXVDefaultCache@@@@UBEHABQAXAAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00431490 ?GenGetNext@?$CMoArray@VDGTracker@@VDefaultCache@@@@UBE?AVDGTracker@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x004314f0 ?GenGetAt@?$CMoArray@VDGTracker@@VDefaultCache@@@@UBE?AVDGTracker@@AAVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00431540 ?GenAppend@?$CMoArray@VDGTracker@@VDefaultCache@@@@UAEHAAVDGTracker@@@Z
// FUNCTION: LITHTECH 0x00431710 ?GenRemoveAt@?$CMoArray@VDGTracker@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: LITHTECH 0x00431940 ?GenRemoveAll@?$CMoArray@VDGTracker@@VDefaultCache@@@@UAEXXZ
// FUNCTION: LITHTECH 0x004319a0 ?GenCopyList@?$CMoArray@VDGTracker@@VDefaultCache@@@@UAEHABV?$GenList@VDGTracker@@@@@Z
// FUNCTION: LITHTECH 0x00431b70 ?GenAppendList@?$CMoArray@VDGTracker@@VDefaultCache@@@@UAEHABV?$GenList@VDGTracker@@@@@Z
// FUNCTION: LITHTECH 0x00431d60 ?Init@?$CMoArray@VDGSample@@VDefaultCache@@@@QAEHKK@Z
// FUNCTION: LITHTECH 0x00431de0 ?SetSize2@?$CMoArray@VDGSample@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00431e80 ?InternalNiceSetSize@?$CMoArray@VDGSample@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00431f80 ?SetSize2@?$CMoArray@VDGTracker@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00432090 ??0?$CMoArray@VDGSample@@VDefaultCache@@@@QAE@XZ
// FUNCTION: LITHTECH 0x004320b0 ?InternalNiceSetSize@?$CMoArray@VDGTracker@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00432230 ??0DGTracker@@QAE@PAXPAVDebugGraph@@@Z
// FUNCTION: LITHTECH 0x004322a0 ?Remove2@?$CMoArray@PAXVDefaultCache@@@@QAEXKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00432390 ?Remove2@?$CMoArray@VDGTracker@@VDefaultCache@@@@QAEXKPAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x004325f0 ?_DeleteAndDestroyArray@?$CMoArray@VDGTracker@@VDefaultCache@@@@AAEXPAVLAlloc@@K@Z
// FUNCTION: LITHTECH 0x00432660 ?CopyArray2@?$CMoArray@VDGSample@@VDefaultCache@@@@QAEHABV1@PAVLAlloc@@@Z
// FUNCTION: LITHTECH 0x00432730 ?BaseNew@@YAPAVDGSample@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x00432770 ?BaseDelete@@YAXPAVLAlloc@@PAVDGTracker@@K@Z
// FUNCTION: LITHTECH 0x004327d0 ?BaseNew@@YAPAVDGTracker@@PAVLAlloc@@PAV1@K@Z
// FUNCTION: LITHTECH 0x00432870 ??_GDGTracker@@QAEPAXI@Z
