// Jupiter runtime/server/src/soundtrack.cpp
#include "soundtrack.h"


// Talon: no bTrackTime parameter.
// FUNCTION: LITHTECH 0x00496460
LTBOOL CSoundTrack::Init(PlaySoundInfo *pPlaySoundInfo, float fStartTime,
	UsedFile *pFile, CSoundData *pSoundData)
{
	LTObject *pObj;
	float fExtraTime;

	if (!pPlaySoundInfo)
		return LTFALSE;

	m_wChangeFlags = 0;
	m_nClientRefs = 0;
	m_pFile = pFile;
	m_Unknown5C = 0;

	// Copy the playinfo information...
	m_dwFlags = pPlaySoundInfo->m_dwFlags;

	// Create link to object...
	if (m_dwFlags & (PLAYSOUND_ATTACHED | PLAYSOUND_CLIENTLOCAL))
	{
		pObj = (LTObject*)pPlaySoundInfo->m_hObject;
		if (!pObj)
		{
			m_fTimeLeft = 0.0f;
			SetRemove(LTTRUE);
			return LTFALSE;
		}

		// If it's an attachment, then we have to create an interlink, cuz object may go
		// away later...
		if (m_dwFlags & PLAYSOUND_ATTACHED)
			CreateInterLink(g_pServerMgr, pObj, this, LINKTYPE_SOUND);
		// The clientlocal sounds only need the object pointer this frame...
		else
			m_pClientLocalObject = pObj;
	}
	else
	{
		m_pInterLink = LTNULL;
		pObj = LTNULL;
	}

	m_nPriority = pPlaySoundInfo->m_nPriority;
	m_fOuterRadius = pPlaySoundInfo->m_fOuterRadius;
	m_fInnerRadius = pPlaySoundInfo->m_fInnerRadius;
	m_nVolume = pPlaySoundInfo->m_nVolume;
	m_fPitchShift = pPlaySoundInfo->m_fPitchShift;
	m_nUserSoundType = pPlaySoundInfo->m_nUserSoundType;

	// Need position from playsoundinfo if not attached...
	if (!(m_dwFlags & PLAYSOUND_ATTACHED))
	{
		if (m_dwFlags & (PLAYSOUND_3D | PLAYSOUND_AMBIENT))
		{
			m_vPosition = pPlaySoundInfo->m_vPosition;
		}
	}
	// Need position from object if attached...
	else
	{
		if (m_dwFlags & (PLAYSOUND_3D | PLAYSOUND_AMBIENT))
		{
			m_vPosition = pObj->GetPos();
		}
	}

	// Remember the user data
	m_UserData = pPlaySoundInfo->m_UserData;

	m_bRemove = LTFALSE;

	// Check if server needs to keep track of time...
	if (pSoundData)
	{
		m_pSoundData = pSoundData;
		m_fDuration = m_pSoundData->GetDuration();

		// Give the client some extra time to finish first.
		fExtraTime = LTMIN(0.5f, m_fDuration * 0.5f);

		m_fTimeLeft = m_fDuration + fExtraTime;
	}
	else
	{
		m_pSoundData = LTNULL;
		m_fTimeLeft = 0.0f;
		m_fDuration = 0.0f;
	}

	m_fStartTime = fStartTime;

	// Get an id...
	if (sm_AllocateID(g_pServerMgr, &m_pIDLink, INVALID_OBJECTID) != LT_OK)
	{
		return LTFALSE;
	}

	// Assign the id...
	g_pServerMgr->m_ObjectMap[GetLinkID(m_pIDLink)].m_nRecordType = RECORDTYPE_SOUND;
	g_pServerMgr->m_ObjectMap[GetLinkID(m_pIDLink)].m_pRecordData = this;

	SetSoundTrackChangeFlags(this, 3);

	return LTTRUE;
}


// FUNCTION: LITHTECH 0x00496680
void CSoundTrack::Update(float fDeltaTime)
{
	LTObject *pObj;

	// Update the timer...
	if (m_fTimeLeft > fDeltaTime)
	{
		m_fTimeLeft -= fDeltaTime;
	}
	else
	{
		m_fTimeLeft = 0.0f;
	}

	if (GetRemove())
		return;

	// Check to see if we need to remove the track for some reason.  Only do
	// this if no handle.  If handle, then user always deletes...
	if (!(m_dwFlags & PLAYSOUND_GETHANDLE))
	{
		// Remove the track if timed out...
		if (!(m_dwFlags & PLAYSOUND_LOOP) && GetTimeLeft() <= 0.0f)
		{
			// Can only really remove when all the clients that know about have told us they are done with it...
			if (m_nClientRefs == 0)
			{
				SetRemove(LTTRUE);
				return;
			}
		}
	}

	if ((m_dwFlags & PLAYSOUND_ATTACHED) && (m_dwFlags & (PLAYSOUND_3D | PLAYSOUND_AMBIENT)))
	{
		pObj = GetObject();
		if (!pObj)
		{
			// If no handle, dump the sound...
			if (!(m_dwFlags & PLAYSOUND_GETHANDLE))
			{
				m_fTimeLeft = 0.0f;
				SetRemove(LTTRUE);
			}

			// Someone left a sound handle without an object!
			return;
		}

		if (pObj->GetPos().x != m_vPosition.x ||
			pObj->GetPos().y != m_vPosition.y ||
			pObj->GetPos().z != m_vPosition.z)
		{
			m_vPosition = pObj->GetPos();
			SetSoundTrackChangeFlags(this, CF_POSITION);
		}
	}
}
