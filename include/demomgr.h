// Talon demo recording/playback (no Jupiter equivalent). Embedded in CClientMgr at 0x169c
// (cm_Init constructs it at 0x0041138c, ~CClientMgr destroys it at 0x00411924).
#ifndef __DEMOMGR_H__
#define __DEMOMGR_H__

#include "ltbasedefs.h"

class CClientMgr;
class ILTStream;

// CDemoMgr::m_State.
#define DEMO_NONE		0
#define DEMO_RECORDING	1
#define DEMO_PLAYING	2

// The first dword of a demo file.
#define DEMO_VERSION	0xF0F0AABC

// Sync bytes written in front of each frame's data.
#define DEMOSYNC_TIME	0	// UpdateTime: the frame's time follows
#define DEMOSYNC_INPUT	1	// ProcessInput: axis offsets and command changes follow
#define DEMOSYNC_END	2	// end of the demo

class CDemoMgr
{
public:
	CDemoMgr();						// 0x00432af0
	~CDemoMgr();					// 0x00432b10

	// FALSE while a demo records or plays.
	LTBOOL		IsConsoleUp();		// 0x00432b20

	// Reseeds the random number generators so a demo plays back the same way.
	void		SRand();			// 0x00432b40
	void		DemoSerialize(ILTStream *pStream, LTBOOL bLoad);	// 0x00432b70

	LTRESULT	RecordDemo(char *pWorldName, const char *pFilename);					// 0x00432b90
	LTRESULT	PlayDemo(const char *pFilename, char *pWorldName, uint32 maxWorldNameLen);	// 0x00432c70
	void		StopDemo();			// 0x00432de0

	// Reads the input (or this frame's recorded input) and records it. bClearInput drops it.
	LTRESULT	ProcessInput(int32 *pChanges, int32 *pnChanges, int32 *pOn, int32 *pnOn,
					LTBOOL bClearInput);	// 0x00432e50
	// Sets the client's frame time (from the demo while playing).
	LTRESULT	UpdateTime();		// 0x00433120

	// Reads a sync byte. Returns 2 if the demo ended (or is broken) and was stopped.
	LTRESULT	ReadSyncByte(int expected);	// 0x00433200

	uint32		m_nFramesDrawn;		// 0x00
	float		m_TimeOffset;		// 0x04 added to the time when not playing
	float		m_StartTime;		// 0x08 when playback started (-1 until the world is loaded)
	CClientMgr	*m_pClientMgr;		// 0x0c
	ILTStream	*m_pStream;			// 0x10 the demo file
	ILTStream	*m_pSaveStream;		// 0x14 game state saved before playback (restored by StopDemo)
	int			m_State;			// 0x18 DEMO_
};

#endif
