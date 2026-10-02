// Server-tracked sounds (Talon layout recovered from lithtech.exe; Jupiter server/src/soundtrack.h).
#ifndef __SOUNDTRACK_H__
#define __SOUNDTRACK_H__

#include "ltbasedefs.h"
#include "servermgr.h"

struct UsedFile;

#include "interlink.h"

// Jupiter sounddata.h.
class CSoundData
{
public:
	LTBOOL	Init(UsedFile *pFile, ILTStream *pStream, uint32 dwFileSize);	// sound/sounddata.cpp
	void	Term();

	float	GetDuration()	{ return m_fDuration; }
	LTBOOL	IsTouched()		{ return m_bTouched; }
	UsedFile*	GetFile()	{ return m_pFile; }

	LTLink		m_Link;			// 0x00
	UsedFile	*m_pFile;		// 0x0c
	float		m_fDuration;	// 0x10
	uint32		m_dwFlags;		// 0x14 Any of the SOUNDBUFFERFLAG_X
	LTBOOL		m_bTouched;		// 0x18
};

#define CF_POSITION		(1<<1)


LTRESULT sm_AllocateID(CServerMgr *pServerMgr, LTLink **ppIDLink, uint16 id);


// 0x68 bytes.
class CSoundTrack
{
public:
	LTBOOL			Init(PlaySoundInfo *pPlaySoundInfo, float fStartTime, UsedFile *pFile, CSoundData *pSoundData);
	void			Update(float fDeltaTime);

	float			GetTimeLeft()				{ return m_fTimeLeft; }
	float			GetDuration()				{ return m_fDuration; }
	void			SetRemove(LTBOOL bRemove)	{ m_bRemove = bRemove; }
	LTBOOL			GetRemove()					{ return m_bRemove; }

	// A client is done with the sound (inlined in s_client/s_net; Jupiter soundtrack.cpp).
	void			Release(uint8 *pnClientSoundFlags)
	{
		// Some sounds the client has to tell us when it's done...
		if (m_dwFlags & (PLAYSOUND_TIME | PLAYSOUND_TIMESYNC | PLAYSOUND_ATTACHED) &&
			!(m_dwFlags & PLAYSOUND_LOOP))
		{
			*pnClientSoundFlags |= 1;	// OBJINFOSOUNDF_CLIENTDONE

			// Remove client reference count to sound.
			if (m_nClientRefs > 0)
			{
				m_nClientRefs--;
			}

			// Time it out if no references left...
			if (m_nClientRefs == 0)
				m_fTimeLeft = 0.0f;
		}
	}

	// A client has been sent the sound (inlined in s_net; Jupiter soundtrack.cpp).
	void			AddRef()
	{
		// Some sounds the client has to tell us when it's done...
		if (m_dwFlags & (PLAYSOUND_TIME | PLAYSOUND_TIMESYNC | PLAYSOUND_ATTACHED) &&
			!(m_dwFlags & PLAYSOUND_LOOP))
		{
			m_nClientRefs++;
		}
	}

	LTObject*		GetObject()
	{
		if (m_dwFlags & PLAYSOUND_ATTACHED)
		{
			if (m_pInterLink)
				return m_pInterLink->m_pOwner;
		}
		else if (m_dwFlags & PLAYSOUND_CLIENTLOCAL)
		{
			return m_pClientLocalObject;
		}

		return LTNULL;
	}

	LTLink			m_Link;				// 0x00 Link for list of CSoundTracks
	LTLink			*m_pIDLink;			// 0x0c Link to ID list
	LTBOOL			m_bRemove;			// 0x10
	UsedFile		*m_pFile;			// 0x14
	CSoundData		*m_pSoundData;		// 0x18
	float			m_fStartTime;		// 0x1c
	float			m_fTimeLeft;		// 0x20
	float			m_fDuration;		// 0x24
	uint32			m_nClientRefs;		// 0x28
	uint16			m_wChangeFlags;		// 0x2c
	CSoundTrack		*m_pChangedNext;	// 0x30

	// PlaySoundInfo information...
	uint32			m_dwFlags;			// 0x34
	union
	{
		InterLink	*m_pInterLink;
		LTObject	*m_pClientLocalObject;
	};									// 0x38
	unsigned char	m_nPriority;		// 0x3c
	float			m_fOuterRadius;		// 0x40
	float			m_fInnerRadius;		// 0x44
	uint8			m_nVolume;			// 0x48
	float			m_fPitchShift;		// 0x4c
	LTVector		m_vPosition;		// 0x50
	float			m_Unknown5C;		// 0x5c fade-out time (KillSoundFade); cleared by Init
	uint8			m_nUserSoundType;	// 0x60
	uint32			m_UserData;			// 0x64
};


// Adds the object to the list of changed objects.
inline void AddSoundTrackToChangeList(CServerMgr *pServerMgr, CSoundTrack *pSoundTrack)
{
	pSoundTrack->m_pChangedNext = pServerMgr->m_ChangedSoundTrackHead;
	pServerMgr->m_ChangedSoundTrackHead = pSoundTrack;
}

// ORs the object's flags with the flags you specify and adds
// the object to the 'changed object' list.
// Same, for callers that have their server manager (CServerSoundMgr).
inline LTRESULT SetSoundTrackChangeFlags(CServerMgr *pServerMgr, CSoundTrack *pSoundTrack, uint32 flags)
{
	if (pServerMgr->m_ObjectMap[GetLinkID(pSoundTrack->m_pIDLink)].m_nRecordType != RECORDTYPE_SOUND ||
		!pServerMgr->m_ObjectMap[GetLinkID(pSoundTrack->m_pIDLink)].m_pRecordData)
	{
		RETURN_ERROR(1, SetSoundTrackChangeFlags, LT_ERROR);
	}

	// Make sure not to re-add it and screw it up.
	if (pSoundTrack->m_wChangeFlags == 0)
	{
		AddSoundTrackToChangeList(pServerMgr, pSoundTrack);
	}

	pSoundTrack->m_wChangeFlags |= flags;
	return LT_OK;
}

inline LTRESULT SetSoundTrackChangeFlags(CSoundTrack *pSoundTrack, uint32 flags)
{
	CServerMgr *pServerMgr = g_pServerMgr;

	if (pServerMgr->m_ObjectMap[GetLinkID(pSoundTrack->m_pIDLink)].m_nRecordType != RECORDTYPE_SOUND ||
		!pServerMgr->m_ObjectMap[GetLinkID(pSoundTrack->m_pIDLink)].m_pRecordData)
	{
		RETURN_ERROR(1, SetSoundTrackChangeFlags, LT_ERROR);
	}

	// Make sure not to re-add it and screw it up.
	if (pSoundTrack->m_wChangeFlags == 0)
	{
		AddSoundTrackToChangeList(pServerMgr, pSoundTrack);
	}

	pSoundTrack->m_wChangeFlags |= flags;
	return LT_OK;
}

#endif  // __SOUNDTRACK_H__
