// d3d.ren canvas drawing (unit unk/10021d70): the Talon-era form of Jupiter's render_a/src/sys/d3d/draw_canvas.h.
// Owner: package W3.  The canvases of a frame are collected in VisibleSet::m_SolidCanvases by d3d_ProcessCanvas; d3d_DrawSolidCanvases
// draws the solid ones (CF_SOLIDCANVAS) at once and moves the others to VisibleSet::m_TranslucentCanvases, which
// d3d_QueueTranslucentCanvases queues on the sorted transparent object list.  (Talon passes no arguments: the view state is the
// global g_ViewParams.)
//
// NAME: d3d_ProcessCanvas, d3d_DrawSolidCanvases, d3d_QueueTranslucentCanvases, d3d_DrawCanvasCB: Jupiter draw_canvas.h/.cpp.
#ifndef __D3DREN_DRAW_CANVAS_H__
#define __D3DREN_DRAW_CANVAS_H__

#include "ltbasedefs.h"
#include "d3dren/visibleset.h"		// DrawObjectFn

class ViewParams;

void d3d_DrawCanvasCB(ViewParams *pParams, LTObject *pCanvas);		// 0x10022a90
void d3d_ProcessCanvas(LTObject *pObject);							// 0x10022a9f (g_ObjectHandlers[OT_CANVAS].m_ProcessObjectFn)
void d3d_DrawSolidCanvases();										// 0x10022ae3
void d3d_QueueTranslucentCanvases();								// 0x10022b15

#endif
