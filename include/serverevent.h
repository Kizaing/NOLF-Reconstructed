// Server events (Talon layout recovered from lithtech.exe; Jupiter server/src/serverevent.h).
#ifndef __SERVER_EVENT_H__
#define __SERVER_EVENT_H__

#include "servermgr.h"

struct UsedFile;

// Misc stuff.
#define EVENTNAME_LEN		50
#define MIN_SOUND_RADIUS	1.0f
#define MAX_SOUND_RADIUS	65000.0f

// Different types of events.
#define EVENT_PLAYSOUND		0

class CServerEvent
{
public:
	void			DecrementRefCount();

public:
	int				m_EventType;				// 0x00

	// Info for sound stuff
	UsedFile		*m_pUsedFile;				// 0x04 The file for the sound.
	PlaySoundInfo	m_PlaySoundInfo;			// 0x08

	// Objects that need to be told about the event.
	LTList			m_ClientStructNodeList;		// 0x144
	uint32			m_RefCount;					// 0x154
	uint8			m_Pad158[0x164 - 0x158];	// (CreateServerEvent clears 0x164 bytes)
};

#endif  // __SERVER_EVENT_H__
