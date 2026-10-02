// Jupiter runtime/shared/src/spritecontrolimpl.cpp: the ILTSpriteControl implementation embedded in
// SpriteInstance. Talon only has the first six functions (no GetAnimLength/GetFrameTextureHandle).
#include "bdefs.h"
#include "de_objects.h"
#include "sprite.h"

// SpriteInstance keeps its SpriteTracker (sprite.h) as raw bytes in Talon's de_objects.h.
#define GetTracker()	((SpriteTracker*)m_pSprite->m_SpriteTracker)
#define GetSprite()		(GetTracker()->m_pSprite)


// FUNCTION: LITHTECH 0x00496b00
LTRESULT SpriteControlImpl::GetNumAnims(uint32 &nAnims)
{
	nAnims = GetSprite()->m_nAnims;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00496b20
LTRESULT SpriteControlImpl::GetNumFrames(uint32 iAnim, uint32 &nFrames)
{
	CHECK_PARAMS(iAnim < GetSprite()->m_nAnims, SpriteControl::GetNumFrames);
	nFrames = GetSprite()->m_Anims[iAnim].m_nFrames;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00496b90
LTRESULT SpriteControlImpl::GetCurPos(uint32 &iAnim, uint32 &iFrame)
{
	SpriteAnim *pCurAnim;

	pCurAnim = GetTracker()->m_pCurAnim;
	iAnim = pCurAnim - GetSprite()->m_Anims;
	iFrame = GetTracker()->m_pCurFrame - pCurAnim->m_Frames;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00496be0
LTRESULT SpriteControlImpl::SetCurPos(uint32 iAnim, uint32 iFrame)
{
	SpriteAnim *pAnim;

	CHECK_PARAMS(iAnim < GetSprite()->m_nAnims &&
		iFrame < GetSprite()->m_Anims[iAnim].m_nFrames, SpriteControl::SetCurPos);

	pAnim = &GetSprite()->m_Anims[iAnim];
	GetTracker()->m_pCurAnim = pAnim;
	GetTracker()->m_pCurFrame = &GetTracker()->m_pCurAnim->m_Frames[iFrame];
	GetTracker()->m_MsCurTime = (iFrame * 1000) / pAnim->m_MsFrameRate;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00496c90
LTRESULT SpriteControlImpl::GetFlags(uint32 &flags)
{
	flags = GetTracker()->m_Flags;
	return LT_OK;
}

// FUNCTION: LITHTECH 0x00496cb0
LTRESULT SpriteControlImpl::SetFlags(uint32 flags)
{
	GetTracker()->m_Flags = flags;
	return LT_OK;
}
