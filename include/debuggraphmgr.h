// Talon debug graph manager (Jupiter runtime/client/src/debuggraphmgr.h): the classes debuggraphmgr.cpp
// implements. CDebugGraphMgr is embedded in CClientMgr at 0x1300 (0x64 bytes).
#ifndef __DEBUGGRAPHMGR_H__
#define __DEBUGGRAPHMGR_H__

#include "ltbasedefs.h"
#include "ltdynarray.h"

class ILTClient;

// Used to define color tables and sample values
class DGSample
{
public:
	DGSample(float fValue = 1.0f, uint32 Color = 0) : m_fValue(fValue), m_Color(Color) {};

	float m_fValue;
	uint32 m_Color;
};


class DebugGraph
{
public:

				~DebugGraph();

	// LT_OK if yes and LT_NOTINITIALIZED if not.
	LTRESULT	IsInitted();

	LTRESULT	Term();

	// Add a new sample.
	LTRESULT	AddSample(DGSample &sample, LTBOOL bOverwrite = LTFALSE);

	// Render it.
	LTRESULT	Draw();

	// Move to a new position
	LTRESULT	MoveTo(LTRect *pRect);


private:

	inline int	GetWidth()	{return m_rRect.right - m_rRect.left;}
	inline int	GetHeight()	{return m_rRect.bottom - m_rRect.top;}
	inline int	GetMaxLineHeight()	{return m_rRect.bottom - m_rRect.top - 2;}


private:

	ILTClient	*m_pClientDE;		// 0x00

	// The surface we blit to the screen.
	HSURFACE	m_hSurface;			// 0x04

	// Our text label.
	HSURFACE	m_hLabel;			// 0x08
	int			m_LabelWidth;		// 0x0c
	int			m_LabelHeight;		// 0x10

	// Where we draw on the screen.
	LTRect		m_rRect;			// 0x14
};


// The graph identifier type
typedef void *DGuid;

const uint32 MAX_GRAPH_SIZE = 1024;

// Internal class for tracking graphs (0x1c bytes).
class DGTracker
{
public:
	DGTracker(DGuid ID = 0, DebugGraph *pGraph = LTNULL) : m_ID(ID), m_pGraph(pGraph) { m_Samples.Init(0, MAX_GRAPH_SIZE); };
	DGuid m_ID;
	DebugGraph *m_pGraph;
	CMoArray<DGSample> m_Samples;
};


class CDebugGraphMgr
{
public:
				CDebugGraphMgr();	// 0x004309c0
				~CDebugGraphMgr();	// 0x00430a70

	// Initialize the graph manager
	LTRESULT	Init(ILTClient *pClientDE, LTRect *pRect);

	// LT_OK if yes and LT_NOTINITIALIZED if not.
	LTRESULT	IsInitted();

	LTRESULT	Term();

	// Change the size of the graphs.  (Note : This will flush the active graphs)
	LTRESULT	SetGraphSize(uint32 nWidth, uint32 nHeight);

	LTRESULT	Draw();

	LTRESULT	MoveTo(LTRect *pRect);

private:

	ILTClient	*m_pClientDE;					// 0x00

	// The list of currently active graphs
	CMoArray<DGuid>			m_ActiveIDs;	// 0x04
	CMoArray<DGTracker>		m_ActiveGraphs;	// 0x18
	void	AddGraph(DGuid id, DebugGraph *pGraph);
	// Remove a graph from the list
	void	RemoveGraph(uint32 index);
	// Remove all graphs from the list
	void	FlushGraphs();

	// Graph searching cache to avoid linear searches when possible
	DGuid		m_FindCacheID;		// 0x2c
	DebugGraph	*m_FindCacheGraph;	// 0x30
	uint32		m_FindCacheIndex;	// 0x34
	void	FlushCache();

	// Where we draw on the screen.
	LTRect	m_rRect;				// 0x38

	// The current graph layout information
	uint32	m_nGraphWidth, m_nGraphHeight;		// 0x48
	uint32	m_nGraphSpacing;					// 0x50
	uint32	m_nGraphXCount, m_nGraphYCount;		// 0x54
	uint32	m_nLeftBorder, m_nBottomBorder;		// 0x5c

	// Calculate the next screen rectangle for a new graph
	LTBOOL	CalcGraphRect(LTRect &rRect, uint32 nIndex);
};

#endif  // __DEBUGGRAPHMGR_H__
