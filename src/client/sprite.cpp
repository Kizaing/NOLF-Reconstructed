// Jupiter runtime/client/src/sprite.cpp
#include <string.h>
#include "sprite.h"

void* dalloc(size_t size);
void dfree(void *ptr);


// Talon takes the client manager instead of using g_pClientMgr.
// FUNCTION: LITHTECH 0x004967e0
Sprite* spr_Create(CClientMgr *pClientMgr, ILTStream *pStream)
{
	uint32 nFrames, nFrameRate, bTransparent, bTranslucent, colourKey;
	uint32 i, iAnim;
	char s[1024];
	Sprite *pSprite;
	SpriteAnim *pAnim;
	uint16 strLen;
	FileRef ref;

	// Setup the Sprite.
	pSprite = (Sprite*)dalloc(sizeof(Sprite));
	memset(pSprite, 0, sizeof(Sprite));
	pSprite->m_Link.m_pData = pSprite;

	// Read in the animations
	pSprite->m_nAnims = 1;  // Sprites only get one animation currently...
	pSprite->m_Anims = (SpriteAnim*)dalloc(sizeof(SpriteAnim) * pSprite->m_nAnims);
	memset(pSprite->m_Anims, 0, sizeof(SpriteAnim) * pSprite->m_nAnims);

	for(iAnim=0; iAnim < pSprite->m_nAnims; iAnim++)
	{
		pAnim = &pSprite->m_Anims[iAnim];

		STREAM_READ(nFrames);
		STREAM_READ(nFrameRate);
		STREAM_READ(bTransparent);
		STREAM_READ(bTranslucent);
		STREAM_READ(colourKey);

		// Allocate array for resource ID's
		pAnim->m_Frames = (SpriteEntry*)dalloc(sizeof(SpriteEntry) * nFrames);

		// Record the name of the animation
		strncpy(pAnim->m_sName, "Untitled", sizeof(pAnim->m_sName) - 1);

		// Set the number of frames in this animation to zero
		pAnim->m_nFrames = nFrames;
		pAnim->m_MsFrameRate = nFrameRate;
		pAnim->m_MsAnimLength = (1000 / nFrameRate) * nFrames;
		pAnim->m_bKeyed = (uint8)bTransparent;
		pAnim->m_bTranslucent = (uint8)bTranslucent;
		pAnim->m_ColourKey = colourKey;

		// Read in the frames for the animation.
		for(i=0; i < nFrames; i++)
		{
			// Read in frame file name
			STREAM_READ(strLen);
			if(strLen > 1000)
			{
				spr_Destroy(pSprite);
				return LTNULL;
			}

			pStream->Read(s, strLen);
			s[strLen] = 0;

			ref.m_FileType = FILE_CLIENTFILE;
			ref.m_pFilename = s;
			pAnim->m_Frames[i].m_pTex = cm_AddSharedTexture(pClientMgr, &ref);
		}
	}

	if(pStream->ErrorStatus() != LT_OK)
	{
		spr_Destroy(pSprite);
		return LTNULL;
	}
	else
	{
		return pSprite;
	}
}


// FUNCTION: LITHTECH 0x004969e0
void spr_Destroy(Sprite *pSprite)
{
	uint32 i;

	if(pSprite->m_Anims)
	{
		for(i=0; i < pSprite->m_nAnims; i++)
		{
			if(pSprite->m_Anims[i].m_Frames)
			{
				dfree(pSprite->m_Anims[i].m_Frames);
			}
		}

		dfree(pSprite->m_Anims);
	}

	dfree(pSprite);
}


// FUNCTION: LITHTECH 0x00496a40
void spr_InitTracker(SpriteTracker *pTracker, Sprite *pSprite)
{
	pTracker->m_pSprite = pSprite;
	pTracker->m_pCurAnim = &pSprite->m_Anims[0];

	if(pTracker->m_pCurAnim->m_nFrames > 0)
		pTracker->m_pCurFrame = &pTracker->m_pCurAnim->m_Frames[0];
	else
		pTracker->m_pCurFrame = LTNULL;

	pTracker->m_MsCurTime = 0;
	pTracker->m_Flags = SC_PLAY | SC_LOOP;
}


// FUNCTION: LITHTECH 0x00496a80
void spr_UpdateTracker(SpriteTracker *pTracker, uint32 msDelta)
{
	uint32 len;
	uint32 iFrame;

	if(pTracker->m_pCurAnim)
	{
		len = pTracker->m_pCurAnim->m_MsAnimLength;
		if(len)
		{
			if(pTracker->m_Flags & SC_PLAY)
			{
				pTracker->m_MsCurTime += msDelta;
			}

			if(pTracker->m_MsCurTime >= pTracker->m_pCurAnim->m_MsAnimLength && !(pTracker->m_Flags & SC_LOOP))
			{
				pTracker->m_MsCurTime = pTracker->m_pCurAnim->m_MsAnimLength - 1;
			}
			else
			{
				pTracker->m_MsCurTime %= pTracker->m_pCurAnim->m_MsAnimLength;
			}

			// Figure out current frame
			iFrame = (pTracker->m_MsCurTime / (1000 / pTracker->m_pCurAnim->m_MsFrameRate)) % pTracker->m_pCurAnim->m_nFrames;
			pTracker->m_pCurFrame = &pTracker->m_pCurAnim->m_Frames[iFrame];
		}
		else
		{
			pTracker->m_pCurFrame = LTNULL;
		}
	}
}
