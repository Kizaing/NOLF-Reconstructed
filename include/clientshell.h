// Talon client shell (Jupiter runtime/client/src/clientshell.h). Only recovered members.
// One definition for the engine: add members here at their exact offsets.
#ifndef __CLIENTSHELL_H__
#define __CLIENTSHELL_H__

#include "ltbasedefs.h"

class LTObject;

class CBaseConn;

class CClientShell
{
public:
	virtual			~CClientShell();		// vtable slot 0 (CNetHandler)

	LTObject*		GetClientObject();		// 0x0048e980

	uint8			m_Pad04[0x18 - 0x4];	// earlier members
	float			m_GameTime;				// 0x18 ILTClient::GetGameTime
	float			m_GameFrameTime;		// 0x1c ILTClient::GetGameFrameTime
	uint8			m_Pad20[0x28 - 0x20];
	// On the first update from the server, this is synchronized with the server game time.
	float			m_ClientGameTime;		// 0x28
	// The current time in sync with m_ClientGameTime.
	float			m_ClientGameTimerSync;	// 0x2c
	uint8			m_Pad30[0x34 - 0x30];
	CBaseConn		*m_HostID;				// 0x34 the server connection
	uint16			m_ClientID;				// 0x38 our client ID on the server (0xFFFF if none)
	uint8			m_Pad3A[0x40 - 0x3a];
	class CServerMgr	*m_pServerMgr;		// 0x40 the local server, if we host one
	// Objects being interpolated by the prediction code (predict.cpp).
	LTLink			m_MovingObjects;		// 0x44
	LTLink			m_RotatingObjects;		// 0x50
	uint8			m_Pad5c[0x78 - 0x5c];
	class CClientMgr	*m_pClientMgr;		// 0x78
	uint8			m_Pad7c[0x84 - 0x7c];
	int				m_ShellMode;			// 0x84 STARTGAME_ mode (ILTClient::GetGameMode)
	LTObject		*m_pFrameClientObject;	// 0x88 client object the camera/listener follows
};

#endif  // __CLIENTSHELL_H__
