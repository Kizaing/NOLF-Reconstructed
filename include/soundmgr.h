// Client sound manager (Talon layout recovered from lithtech.exe; Jupiter runtime/sound/src/soundmgr.h).
// Talon drives Miles directly: every AIL call is bracketed by AIL_lock/AIL_unlock.
#ifndef __SOUNDMGR_H__
#define __SOUNDMGR_H__

#include "bdefs.h"
#include "mss.h"
#include "iltsoundmgr.h"
#include "packet.h"
#include "soundbuffer.h"
#include "soundinstance.h"

// SoundMgr defines
#define SOUNDMGR_MAXSOUNDINSTANCES		256

// Sample types
#define SAMPLETYPE_SW		0
#define SAMPLETYPE_3D		1

// Sample user data indices
#define SAMPLE_INSTANCE			0
#define SAMPLE_TYPE				1
#define SAMPLE_LISTITEM			2

// Time between committing positions
#define COMMITTIME				100


// 0x118 bytes.
struct CProvider
{
	HPROVIDER	m_hProvider;					// 0x000
	uint32		m_dwProviderID;					// 0x004
	char		m_szProviderName[_MAX_PATH+1];	// 0x008
	uint32		m_dwCaps;						// 0x110
	CProvider *	m_pNextProvider;				// 0x114
};

// 0x10 bytes.
struct CSample
{
	union
	{
		HSAMPLE		m_hSample;
		H3DSAMPLE	m_h3DSample;
	};

	LTLink			m_Link;
};

// Per-type volumes (ILTClientSoundMgr::SetVolumeByType), all 100 at start.
struct CSoundTypeVolumes
{
	CSoundTypeVolumes()
	{
		memset(m_nVolume, 100, sizeof(m_nVolume));
	}

	uint8		m_nVolume[256];
};


// 0x910 bytes, embedded in CClientMgr at 0x784.
class CSoundMgr : public ILTClientSoundMgr
{
public:

	CSoundMgr();
	~CSoundMgr();

	LTRESULT	Init(InitSoundInfo &soundInit);
	void		Term(LTBOOL bRemoveSounds = LTTRUE);

	LTBOOL		IsValid()
	{ return m_bValid; }

	void		SetEnable(LTBOOL bEnable)
	{ m_bEnabled = bEnable; }

	LTBOOL		IsEnabled()
	{ return m_bEnabled; }

	HDIGDRIVER	GetDigDriver() const
	{ return m_hDigDriver; }

	CSoundBuffer *CreateBuffer(FileIdentifier &fileIdent);
	LTRESULT	RemoveBuffer(FileIdentifier &fileIdent);
	LTRESULT	UntagAllSoundBuffers();
	LTRESULT	RemoveAllUntaggedSoundBuffers();

	LTRESULT	PlaySound(PlaySoundInfo &playSoundInfo, FileIdentifier &fileIdent, uint32 dwOffsetTime = 0);
	CSoundInstance *FindSoundInstance(HLTSOUND hSound, LTBOOL bClientSound);
	LTRESULT	RemoveInstance(CSoundInstance &soundInstance);
	LTRESULT	StopAllSounds();
	LTRESULT	Update();

	static int	CompareSoundInstances(const void *pElem1, const void *pElem2);

	LTBOOL		IsDigitalHandleReleased() const
	{ return m_bDigitalHandleReleased; }

	LTRESULT	ReleaseDigitalHandle();
	LTRESULT	ReacquireDigitalHandle();

	LTBOOL		IsListenerInClient() const
	{ return m_bListenerInClient; }

	LTRESULT	SetListener(LTBOOL bListenerInClient, LTVector *pvListenerPos, LTVector *pvListenerFront, LTVector *pvListenerRight, LTBOOL bTeleport);

	const LTVector &	GetListenerPosition() const
	{ return m_vListenerPosition; }

	const LTVector &	GetListenerVelocity() const
	{ return m_vListenerVelocity; }

	const LTVector &	GetListenerFront() const
	{ return m_vListenerForward; }

	const LTVector &	GetListenerRight() const
	{ return m_vListenerRight; }

	H3DPOBJECT	GetListenerObject() const { return m_h3DListener; }
	HSAMPLE		GetFreeSWSample();
	H3DSAMPLE	GetFree3DSample(CSoundInstance *pSoundInstance = LTNULL);
	void		ReleaseSWSample(HSAMPLE hSample);
	void		Release3DSample(H3DSAMPLE h3DSample);
	LTRESULT		LinkSampleSoundInstance(HSAMPLE hSample, CSoundInstance *pSoundInstance);
	CSoundInstance *GetLinkSampleSoundInstance(HSAMPLE hSample);
	LTRESULT		Link3DSampleSoundInstance(H3DSAMPLE h3DSample, CSoundInstance *pSoundInstance);
	CSoundInstance *GetLink3DSampleSoundInstance(H3DSAMPLE h3DSample);
	LTRESULT		LinkStreamSoundInstance(HSTREAM hStream, CSoundInstance *pSoundInstance);

	CPacket &	GetSoundUpdatePacket()
	{ return m_SoundUpdatePacket; }

	uint32		GetNumSoundsPlaying()
	{ return m_dwNumSoundInstances; }

	uint32		GetNumSoundsHeard()
	{ return m_nNumSoundsHeard; }

	LTBOOL		GetConvert16to8() const
	{ return m_bConvert16to8; }

	float		GetDistanceFactor() const
	{ return m_fDistanceFactor; }

	LTBOOL		UseSWReverb() const
	{ return m_bSWReverb; }

	LTBOOL		Use3DReverb() const
	{ return m_b3DReverb; }

	LTBOOL		CommitChanges() const
	{ return m_bCommitChanges; }

	ObjectBank<CSoundBuffer> &GetSoundBufferBank()
	{ return m_SoundBufferBank; }

//// ILTClientSoundMgr Implementation /////////////////////////////////////
public:
	virtual LTRESULT	PlaySound(PlaySoundInfo *pPlaySoundInfo, HLTSOUND &hResult);
	virtual LTRESULT	GetSoundDuration(HLTSOUND hSound, LTFLOAT &fDuration);
	virtual LTRESULT	IsSoundDone(HLTSOUND hSound, LTBOOL &bDone);
	virtual LTRESULT	KillSound(HLTSOUND hSound);
	virtual LTRESULT	KillSoundLoop(HLTSOUND hSound);
	virtual LTRESULT	KillSoundFade(HLTSOUND hSound, LTFLOAT fFadeOutTime);

	virtual LTRESULT	GetSound3DProviderLists(Sound3DProvider *&pSound3DProviderList, LTBOOL bVerify);
	virtual LTRESULT	ReleaseSound3DProviderList(Sound3DProvider *pSound3DProviderList);
	virtual LTRESULT	InitSound(InitSoundInfo *pSoundInfo);
	virtual LTRESULT	GetVolume(uint16 &nVolume);
	virtual LTRESULT	SetVolume(uint16 nVolume);
	virtual LTRESULT	GetVolumeByType(uint16 &nVolume, uint8 nSoundType);
	virtual LTRESULT	SetVolumeByType(uint16 nVolume, uint8 nSoundType);
	virtual LTRESULT	SetReverbProperties(ReverbProperties *pReverbProperties);
	virtual LTRESULT	GetReverbProperties(ReverbProperties *pReverbProperties);
	virtual	LTRESULT	SetSoundOcclusion(HLTSOUND hSound, LTFLOAT fLevel);
	virtual LTRESULT	GetSoundOcclusion(HLTSOUND hSound, LTFLOAT *pLevel);
	virtual	LTRESULT	SetSoundObstruction(HLTSOUND hSound, LTFLOAT fLevel);
	virtual LTRESULT	GetSoundObstruction(HLTSOUND hSound, LTFLOAT *pLevel);
	virtual LTRESULT	SetSoundFilter(HLTSOUND hSound, const char *pFilter);
	virtual LTRESULT	SetSoundFilterParam(HLTSOUND hSound, const char *pParam, float fValue);
	virtual LTRESULT	GetSoundFilterParam(HLTSOUND hSound, const char *pParam, float *pValue);
	virtual LTRESULT	GetFilterName(uint32 nIndex, const char **pFilter);
	virtual LTRESULT	GetFilterParamName(uint32 nIndex, const char *pFilter, const char **pParam);
	virtual LTRESULT	SetSoundPosition(HLTSOUND hSound, LTVector *pPos);
	virtual LTRESULT	GetSoundPosition(HLTSOUND hSound, LTVector *pPos);
	virtual LTRESULT	PauseSounds();
	virtual LTRESULT	ResumeSounds();
	virtual LTRESULT	SetListener(LTBOOL bListenerInClient, LTVector *pPos, LTRotation *pRot, LTBOOL bTeleport);
	virtual LTRESULT	ReleaseSoundHandle(HLTSOUND hSound);

	// Talon additions.
	virtual LTRESULT	GetFilterIndex(const char *pFilter, uint32 *pIndex);
	virtual LTRESULT	GetFilterParamIndex(const char *pFilter, const char *pParam, uint32 *pIndex);

private:

	void		SetPreferences();
	LTRESULT	Get3DProviderLists(CProvider *&p3DProviderList, LTBOOL bVerifyOpens);
	void		ReleaseProviderList(CProvider *pProviderList);
	LTRESULT	Set3DProvider(char *psz3DProviderName);
	LTRESULT	Create3DSamples();
	LTRESULT	Remove3DSamples();
	LTRESULT	CreateSWSamples();
	LTRESULT	RemoveSWSamples();
	LTRESULT	RemoveBuffer(CSoundBuffer &soundBuffer);

public:

	InitSoundInfo	m_InitSoundInfo;		// 0x008
	LTBOOL		m_bValid;					// 0x124
	LTBOOL		m_bEnabled;					// 0x128
	CProvider *	m_p3DProviderList;			// 0x12c
	HDIGDRIVER	m_hDigDriver;				// 0x130
	LTBOOL		m_bDigitalHandleReleased;	// 0x134
	LTBOOL		m_bReacquireDigitalHandle;	// 0x138
	CProvider 	m_3DProvider;				// 0x13c
	uint8		m_nNum3DSamples;			// 0x254
	uint8		m_nMax3DSamples;			// 0x255
	CSample *	m_p3DSampleList;			// 0x258
	LTList		m_3DFreeSampleList;			// 0x25c
	uint8		m_nNumSWSamples;			// 0x26c
	uint8		m_nMaxSWSamples;			// 0x26d
	CSample *	m_pSWSampleList;			// 0x270
	LTList		m_SWFreeSampleList;			// 0x274
	ObjectBank<CSoundBuffer>			m_SoundBufferBank;			// 0x284
	LTList		m_SoundBufferList;			// 0x2a8
	ObjectBank<CLocalSoundInstance>		m_LocalSoundInstanceBank;	// 0x2b8
	ObjectBank<CAmbientSoundInstance>	m_AmbientSoundInstanceBank;	// 0x2dc
	ObjectBank<C3DSoundInstance>		m_3DSoundInstanceBank;		// 0x300
	CSoundInstance *	m_SoundInstanceList[SOUNDMGR_MAXSOUNDINSTANCES];	// 0x324
	uint32		m_dwNumSoundInstances;		// 0x724
	H3DPOBJECT	m_h3DListener;				// 0x728
	LTVector	m_vListenerPosition;		// 0x72c
	LTVector	m_vLastListenerPosition;	// 0x738
	LTVector	m_vListenerVelocity;		// 0x744
	LTVector	m_vListenerForward;			// 0x750
	LTVector	m_vListenerRight;			// 0x75c
	LTVector	m_vListenerUp;				// 0x768
	LTBOOL		m_bListenerInClient;		// 0x774
	float		m_fDistanceFactor;			// 0x778
	uint8		m_nNumSoundsHeard;			// 0x77c
	CPacket		m_SoundUpdatePacket;		// 0x780
	LTBOOL		m_bConvert16to8;			// 0x7e4
	LTBOOL		m_bSWReverb;				// 0x7e8
	LTBOOL		m_b3DReverb;				// 0x7ec
	uint32		m_dwReverbAcoustics;		// 0x7f0
	float		m_fReverbReflectTime;		// 0x7f4
	float		m_fReverbVolume;			// 0x7f8
	float		m_fReverbDecayTime;			// 0x7fc
	float		m_fReverbDamping;			// 0x800
	uint32		m_dwCurTime;				// 0x804
	uint32		m_dwCommitTime;				// 0x808
	LTBOOL		m_bCommitChanges;			// 0x80c
	CSoundTypeVolumes	m_SoundTypeVolumes;	// 0x810
};

// 0x0040fba0: &g_pClientMgr->m_SoundMgr.
CSoundMgr *GetClientILTSoundMgrImpl();

#endif  // __SOUNDMGR_H__
