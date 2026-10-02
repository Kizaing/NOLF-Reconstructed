// Talon client object setup (Jupiter runtime/client/src/setupobject.h). Only what's recovered.
#ifndef __SETUPOBJECT_H__
#define __SETUPOBJECT_H__

#include "ltbasedefs.h"
#include "sprite.h"

class CClientMgr;
class Model;
struct FileIdentifier;

// Any requests from outside to create an object go thru these (0x44 bytes).
class InternalObjectSetup
{
public:
					InternalObjectSetup()
					{
						m_pSetup = LTNULL;
						m_bResetAnimations = LTTRUE;
					}

public:
	// Filename and skin names converted into FileRefs.
	FileRef				m_Filename;						// 0x00
	FileRef				m_SkinNames[MAX_MODEL_TEXTURES];	// 0x0c
	ObjectCreateStruct	*m_pSetup;						// 0x3c
	LTBOOL				m_bResetAnimations;				// 0x40
};

// Performs extra data initialization on the object (like model and skin loading). 0x0048a600
LTRESULT so_ExtraInit(CClientMgr *pClientMgr, LTObject *pObject, InternalObjectSetup *pSetup,
	LTBOOL bFromLocalServer);

// Finds the model's file; returns it if it's loaded, otherwise loads it (bLoad).
LTRESULT cm_LoadModel2(CClientMgr *pClientMgr, FileRef *pRef, Model **ppModel, FileIdentifier **ppIdent,
	LTBOOL bLoad, LTBOOL bNow);																	// 0x00489970
// Hooks a loaded model up to its file identifier (also when the loader thread finishes one).
LTRESULT cm_BindModel(CClientMgr *pClientMgr, Model *pModel, FileIdentifier *pIdent, LTBOOL bUpdateObjects);	// 0x00489f50
// Removes the client objects that use pModel and clears pIdent->m_pData.
void cm_RemoveModelObjects(CClientMgr *pClientMgr, Model *pModel, FileIdentifier *pIdent);		// 0x0048a090

#endif  // __SETUPOBJECT_H__
