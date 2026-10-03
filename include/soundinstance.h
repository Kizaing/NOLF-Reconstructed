// Client sound instances (Talon layout recovered from lithtech.exe; Jupiter runtime/sound/src/soundinstance.h).
#ifndef __SOUNDINSTANCE_H__
#define __SOUNDINSTANCE_H__

#include "soundbuffer.h"

enum SoundType
{
	SOUNDTYPE_LOCAL = 0,
	SOUNDTYPE_AMBIENT,
	SOUNDTYPE_3D,
	SOUNDTYPE_NUMTYPES
};

// Sound instance flags
#define	SOUNDINSTANCEFLAG_READY			0
#define	SOUNDINSTANCEFLAG_FIRSTUPDATE	(1<<0)
#define	SOUNDINSTANCEFLAG_PLAYING		(1<<1)
#define	SOUNDINSTANCEFLAG_DONE			(1<<2)
#define	SOUNDINSTANCEFLAG_WASPLAYING	(1<<3)
#define SOUNDINSTANCEFLAG_EARSHOT		(1<<4)
#define SOUNDINSTANCEFLAG_PAUSED		(1<<5)
#define SOUNDINSTANCEFLAG_ENDLOOP		(1<<6)
#define SOUNDINSTANCEFLAG_FADE			(1<<7)	// Talon: KillSoundFade in progress

#ifndef INVALID_OBJECTID
#define INVALID_OBJECTID		0xFFFF
#endif

// Number of filter parameters a sound instance remembers.
#define SOUNDINSTANCE_MAXFILTERPARAMS	7

// User data slot that remembers which buffer a 3d sample was set up with.
#define SAMPLE_BUFFER	3

// 0xc4 bytes.
class CSoundInstance
{
public:

	CSoundInstance();

	virtual ~CSoundInstance();

	virtual LTRESULT	Init(CSoundBuffer &soundBuffer, PlaySoundInfo &playSoundInfo, uint32 dwOffsetTime = 0);
	virtual void		Term();

	SoundType		GetType() const
	{ return m_eType; }

	HLTSOUND		GetHSoundDE() const
	{ return m_hSound; }

	uint8			GetPriority() const
	{ return m_nPriority; }

	float			GetModifiedPriority() const
	{ return m_fModifiedPriority; }

	uint32			GetPlaySoundFlags() const
	{ return m_dwPlaySoundFlags; }

	void			SetSoundInstanceFlags(uint32 dwSoundInstanceFlags)
	{ m_dwSoundInstanceFlags = dwSoundInstanceFlags; }

	uint32			GetSoundInstanceFlags() const
	{ return m_dwSoundInstanceFlags; }

	CSoundBuffer *	GetSoundBuffer() const
	{ return m_pSoundBuffer; }

	uint32			GetOffsetTime() const
	{ return m_dwOffsetTime; }

	uint32			GetTimer() const
	{ return m_dwTimer; }

	void			SetTimer(uint32 dwTimer)
	{ m_dwTimer = dwTimer; }

	LTBOOL			UpdateTimer(uint32 dwFrameTime);

	uint32			GetDuration() const
	{ return m_dwDuration; }

	LTRESULT		AcquireSample();
	virtual LTRESULT	Acquire3DSample();
	LTRESULT		AcquireStream();

	HSAMPLE			GetSample() const { return m_hSample; }
	H3DSAMPLE		Get3DSample() const { return m_h3DSample; }
	HSTREAM			GetStream() const { return m_hStream; }

	virtual LTRESULT	UpdateOutput(uint32 dwFrameTime) = 0;
	virtual LTRESULT	Preupdate(LTVector const& vListenerPos) = 0;

	virtual LTRESULT	Stop(LTBOOL bForce = LTFALSE);
	virtual LTRESULT	Silence(LTBOOL bForce = LTFALSE);
	virtual LTRESULT	Pause();
	virtual LTRESULT	Resume();

	LTRESULT			EndLoop();
	LTRESULT			FadeOut(float fFadeOutTime);
	LTRESULT			DisconnectFromServer();

	virtual LTRESULT	SetObstruction(LTFLOAT fLevel)
	{ return LT_ERROR; }

	virtual LTRESULT	GetObstruction(LTFLOAT &fLevel)
	{ return LT_ERROR; }

	virtual LTRESULT	SetOcclusion(LTFLOAT fLevel)
	{ return LT_ERROR; }

	virtual LTRESULT	GetOcclusion(LTFLOAT &fLevel)
	{ return LT_ERROR; }

	virtual LTRESULT	SetPosition(const LTVector &vPos, LTBOOL bTeleport = LTFALSE);

	virtual LTRESULT	GetPosition(LTVector &vPos)
	{ vPos = m_vPosition; return LT_OK; }

	float				GetOuterRadius() { return m_fOuterRadius; }
	float				GetInnerRadius() { return m_fInnerRadius; }

	virtual LTRESULT	Get3DSamplePosition(LTVector &vPos)
	{ return LT_OK; }

	// Talon software filters (Miles filter providers).
	virtual LTRESULT	SetFilter(const char *pFilter);
	virtual LTRESULT	SetFilterParam(const char *pParam, float fValue);
	virtual LTRESULT	GetFilterParam(const char *pParam, float *pValue);
	virtual LTBOOL		HasFilter()
	{ return m_nFilter != -1; }

	uint32			GetListIndex() const
	{ return m_dwListIndex; }

	void			SetListIndex(uint32 dwIndex)
	{ m_dwListIndex = dwIndex; }

	const LTLink *	GetSoundBufferLink() const
	{ return &m_BufferLink; }

	LTRESULT		Unload();
	LTRESULT		Reload();

	uint16			GetCollisions()
	{ return m_nNumCollisions; }

	void			SetCollisions(uint16 nCollisions)
	{ m_nNumCollisions = nCollisions; }

	uint8			GetUserSoundType()
	{ return m_nUserSoundType; }

protected:

	virtual LTRESULT	StartRendering();
	virtual LTRESULT	ApplyFilter();

	LTRESULT		PreUpdatePositionalSound(LTVector const& vListenerPos);

public:

	SoundType		m_eType;				// 0x04
	CSoundBuffer	*m_pSoundBuffer;		// 0x08
	FileIdentifier	*m_pFileIdent;			// 0x0c
	uint32			m_dwPlaySoundFlags;		// 0x10
	HLTSOUND		m_hSound;				// 0x14
	uint8			m_nPriority;			// 0x18
	float			m_fModifiedPriority;	// 0x1c
	uint8			m_nVolume;				// 0x20
	uint32			m_dwTimer;				// 0x24 In ms
	uint32			m_dwOffsetTime;			// 0x28 In ms
	uint32			m_dwDuration;			// 0x2c
	uint32			m_dwLastTime;			// 0x30
	float			m_fPitchShift;			// 0x34
	uint32			m_dwFadeTime;			// 0x38 In ms
	float			m_fFadeVolume;			// 0x3c
	uint32			m_dwPauseCount;			// 0x40 Reference count of number of times paused
	uint32			m_dwResumeTime;			// 0x44 In ms
	uint32			m_dwSoundInstanceFlags;	// 0x48
	HSAMPLE			m_hSample;				// 0x4c
	H3DSAMPLE		m_h3DSample;			// 0x50
	HSTREAM			m_hStream;				// 0x54
	HANDLE			m_hStreamFile;			// 0x58 file the stream reads from
	uint16			m_nNumCollisions;		// 0x5c
	uint32			m_dwListIndex;			// 0x60
	LTLink			m_BufferLink;			// 0x64
	LTVector		m_vPosition;			// 0x70
	float			m_fInnerRadius;			// 0x7c
	float			m_fOuterRadius;			// 0x80
	int				m_nFilter;				// 0x84 index of the Miles filter provider
	int				m_nFilterParam[SOUNDINSTANCE_MAXFILTERPARAMS];			// 0x88
	float			m_fFilterParamValue[SOUNDINSTANCE_MAXFILTERPARAMS];	// 0xa4
	uint8			m_nUserSoundType;		// 0xc0
};

inline LTRESULT CSoundInstance::SetPosition(const LTVector &vPos, LTBOOL bTeleport)
{ m_vPosition = vPos; return LT_OK; }


class CLocalSoundInstance : public CSoundInstance
{
public:

	virtual LTRESULT	Init(CSoundBuffer &soundBuffer, PlaySoundInfo &playSoundInfo, uint32 dwOffsetTime = 0);
	virtual LTRESULT	UpdateOutput(uint32 dwFrameTime);
	virtual LTRESULT	Preupdate(LTVector const& vListenerPos);
	virtual LTRESULT	Get3DSamplePosition(LTVector &vPos);
};

class CAmbientSoundInstance : public CSoundInstance
{
public:

	CAmbientSoundInstance();

	virtual LTRESULT	Init(CSoundBuffer &soundBuffer, PlaySoundInfo &playSoundInfo, uint32 dwOffsetTime = 0);
	virtual LTRESULT	UpdateOutput(uint32 dwFrameTime);
	virtual LTRESULT	Preupdate(LTVector const& vListenerPos);
	virtual LTRESULT	Get3DSamplePosition(LTVector &vPos);
};

// 0xdc bytes.
class C3DSoundInstance : public CSoundInstance
{
public:

	C3DSoundInstance();

	virtual LTRESULT	Init(CSoundBuffer &soundBuffer, PlaySoundInfo &playSoundInfo, uint32 dwOffsetTime = 0);
	virtual LTRESULT	UpdateOutput(uint32 dwFrameTime);
	virtual LTRESULT	Preupdate(LTVector const& vListenerPos);
	virtual LTRESULT	SetObstruction(LTFLOAT fLevel);
	virtual LTRESULT	GetObstruction(LTFLOAT &fLevel);
	virtual LTRESULT	SetOcclusion(LTFLOAT fLevel);
	virtual LTRESULT	GetOcclusion(LTFLOAT &fLevel);
	virtual LTRESULT	SetPosition(const LTVector &vPos, LTBOOL bTeleport = LTFALSE);
	virtual LTRESULT	Get3DSamplePosition(LTVector &vPos);

public:

	LTVector			m_vLastPosition;	// 0xc4
	LTVector			m_vVelocity;		// 0xd0
};

#endif  // __SOUNDINSTANCE_H__
