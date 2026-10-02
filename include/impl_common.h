// Shared client/server interface helpers (Jupiter runtime/shared/src/impl_common.h, Talon version).
// Talon's impl_common also holds the light anim info helpers and the compressed vector, position
// and rotation message helpers.
#ifndef __IMPL_COMMON_H__
#define __IMPL_COMMON_H__

#include "ltbasedefs.h"
#include "iltlightanim.h"

class ILTMessage;
class CPacket;
class MainWorld;
class WorldTree;
struct LightAnim;
struct Node;

// Compressed world position (Jupiter iltcommon.h).
struct CompWorldPos
{
	uint16	m_Pos[3];
	uint8	m_Extra;
};

// Compressed rotation (3 or 6 bytes).
struct CompRot
{
	char	m_Bytes[6];
};

// Light anim info.
void	la_GetInfo(LightAnim *pAnim, LAInfo *pInfo);
void	la_SetInfo(LightAnim *pAnim, LAInfo *pInfo);
LTBOOL	la_InfoChanged(LightAnim *pAnim, LAInfo *pInfo, uint32 *pChanged);

// BSP helpers.
LTBOOL	ci_IsPointInsideBSP(Node *pRoot, LTVector &P);
LTBOOL	ic_IsPointInWorld(WorldTree *pWorldTree, LTVector *pPoint);

// Message helpers.
void	ic_WriteCompVector(ILTMessage *pMsg, LTVector *pVec);
void	ic_EncodeCompPos(CompWorldPos *pPos, LTVector *pVal, MainWorld *pWorld);
void	ic_WriteCompWorldPos(ILTMessage *pMsg, CompWorldPos *pPos);
void	ic_WriteCompPos(ILTMessage *pMsg, LTVector *pPos, MainWorld *pWorld);
void	ic_ReadCompPos(ILTMessage *pMsg, LTVector *pPos, MainWorld *pWorld);
void	ic_EncodeCompRotation(LTRotation *pRot, CompRot *pCompRot);
void	ic_WriteCompRot(ILTMessage *pMsg, CompRot *pCompRot);
void	ic_WriteCompRotation(ILTMessage *pMsg, LTRotation *pRot);
void	ic_ReadCompRotation(ILTMessage *pMsg, LTRotation *pRot);
void	ic_WriteYRotation(CPacket *pPacket, LTRotation *pRot);
void	ic_ReadYRotation(CPacket *pPacket, LTRotation *pRot);

// Interface implementations.
uint32		ic_EndCounter(LTCounter *pCounter);
LTBOOL		ic_UpperStrcmp(char *pStr1, char *pStr2);
LTRESULT	ic_GetNextModelNode(HOBJECT hObject, HMODELNODE hNode, HMODELNODE *pNext);
LTRESULT	ic_GetModelNodeName(HOBJECT hObject, HMODELNODE hNode, char *pName, uint32 maxLen);
HMODELANIM	ic_GetAnimIndex(HOBJECT hObj, char *pAnimName);
const char*	ic_GetAnimName(HOBJECT hObject, HMODELANIM hAnim);
void		ic_FreeString(HSTRING hString);
void		ic_FreeFileList(FileEntry *pList);
float		ic_Random(float min, float max);

#endif  // __IMPL_COMMON_H__
