// Jupiter runtime/server/src/sloaderthread.cpp (Talon version).
// Talon also keeps the server's ILTMessage helper here (object references and string
// resources for LMessageImpl, through LMessageImpl::m_Unknown04). Its file is unknown; the
// helper's code sits between shellutil.cpp and the loader thread.
// FLAGS: /O2 /GX-
#include "bdefs.h"
#include "sloaderthread.h"
#include "servermgr.h"
#include "packet.h"
#include "stringmgr.h"
#include "s_object.h"
#include "server_extradata.h"

#define SLT_LOADFILE		0
#define SLT_LOADEDFILE		1

CBindModuleType* sb_GetModule(ShellBindModule *pModule);		// 0x0048a7b0


// ------------------------------------------------------------------ //
// CServerSerializeHelper
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0048e9a0
CServerSerializeHelper::CServerSerializeHelper(CServerMgr *pServerMgr)
{
	m_pServerMgr = pServerMgr;
}


// FUNCTION: LITHTECH 0x0048e9c0
LTRESULT CServerSerializeHelper::ReadObjectRef(ILTMessage *pMsg, HOBJECT *pObj)
{
	uint16 objectID;
	LTLink *pListHead, *pCur;
	uint32 i;
	ObjectMapEntry *pEntry;
	LTObject *pObject;

	*pObj = LTNULL;
	pMsg->ReadWordFL(objectID);

	if(((LMessageImpl*)pMsg)->IsInvalid() == LTTRUE)
	{
		// Save games refer to objects by their serialize ID.
		if(objectID != INVALID_OBJECTID)
		{
			for(i=0; i < NUM_OBJECTTYPES; i++)
			{
				pListHead = &g_pServerMgr->m_ObjectMgr.m_ObjectLists[i].m_Head;
				for(pCur=pListHead->m_pNext; pCur != pListHead; pCur=pCur->m_pNext)
				{
					pObject = (LTObject*)pCur->m_pData;
					if(pObject->m_SerializeID == objectID)
					{
						*pObj = pObject;
						return LT_OK;
					}
				}
			}
		}
	}
	else
	{
		if(objectID != INVALID_OBJECTID && objectID < m_pServerMgr->m_ObjectMap.GetSize())
		{
			pEntry = &m_pServerMgr->m_ObjectMap[objectID];
			if(pEntry->m_nRecordType == RECORDTYPE_LTOBJECT)
				*pObj = (HOBJECT)pEntry->m_pRecordData;
		}
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048ea70
LTRESULT CServerSerializeHelper::WriteObjectRef(ILTMessage *pMsg, HOBJECT hObj)
{
	LTObject *pObj = (LTObject*)hObj;

	if(((LMessageImpl*)pMsg)->IsInvalid() == LTTRUE)
	{
		if(pObj)
			pMsg->WriteWord(pObj->m_SerializeID);
		else
			pMsg->WriteWord(INVALID_OBJECTID);
	}
	else
	{
		if(pObj)
			pMsg->WriteWord(pObj->m_ObjectID);
		else
			pMsg->WriteWord(INVALID_OBJECTID);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048ead0
LTRESULT CServerSerializeHelper::WriteHStringArgList(ILTMessage *pMsg, int messageCode, va_list *pList)
{
	uint8 *pBuffer;
	int bufferLen;

	pBuffer = str_FormatString(sb_GetModule(m_pServerMgr->m_ClassMgr.m_hShellModule),
		messageCode, pList, &bufferLen);
	if(pBuffer)
	{
		pMsg->WriteWord((uint16)bufferLen);
		pMsg->WriteRaw(pBuffer, bufferLen);
		str_FreeStringBuffer(pBuffer);
	}
	else
	{
		pMsg->WriteWord(0);
	}

	return LT_OK;
}


// FUNCTION: LITHTECH 0x0048eb40
LTRESULT CServerSerializeHelper::GetWorld(MainWorld **ppWorld)
{
	*ppWorld = &m_pServerMgr->m_World;
	return LT_OK;
}


// ------------------------------------------------------------------ //
// CServerLoaderThread
// ------------------------------------------------------------------ //

// FUNCTION: LITHTECH 0x0048eb60
CServerLoaderThread::CServerLoaderThread()
{
	m_pServerMgr = LTNULL;
}


// FUNCTION: LITHTECH 0x0048eb80
LTBOOL CServerLoaderThread::Init(CServerMgr *pServerMgr)
{
	m_pServerMgr = pServerMgr;
	return LTTRUE;
}


// FUNCTION: LITHTECH 0x0048eb90
void CServerLoaderThread::ProcessMessage(LThreadMessage &msg)
{
	LThreadMessage result;
	UsedFile *pFile;
	char *pFilename;
	Model *pModel;
	LTRESULT dResult;

	if(!m_pServerMgr || msg.m_ID != SLT_LOADFILE)
		return;

	if(msg.m_Data[0].m_dwData == FT_MODEL)
	{
		pFile = (UsedFile*)msg.m_Data[1].m_pData;
		pFilename = sf_GetUsedFilename(&m_pServerMgr->m_FileMgr, pFile);
		dResult = se_LoadModelData(m_pServerMgr, pFilename, pFile, &pModel);

		result.m_ID = (dResult != LT_OK);
		result.m_Data[0].m_dwData = FT_MODEL;
		result.m_Data[1].m_pData = pFile;
		result.m_Data[2].m_pData = pModel;
		m_Outgoing.PostMessage(result);
	}
}
