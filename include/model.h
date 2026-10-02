// Talon model structures (ABC v12), layouts recovered from lithtech.exe.
// Jupiter runtime/model/src/model.h is the guide, but Talon's layout differs.
// Only the members used by matched code are named so far.
#ifndef __MODEL_H__
#define __MODEL_H__

#include "ltbasedefs.h"
#include "ltdynarray.h"

class LTAnimTracker;
class LAlloc;
class AnimKeyFrame;

// Keyframe types.
#define KEYTYPE_POSITION	0
#define KEYTYPE_CALLBACK	1

// ModelNode flags.
#define MNODE_REMOVABLE		(1<<0)
#define MNODE_ROTATIONONLY	(1<<1)

#define MAX_GVP_ANIMS		8

typedef void (*KeyCallback)(LTAnimTracker *pTracker, AnimKeyFrame *pFrame);

// 0x14 bytes.
class AnimKeyFrame
{
public:
	AnimKeyFrame();				// 0x0044dea0

	uint32		m_Time;			// 0x00
	char		*m_pString;		// 0x04
	uint8		m_KeyType;		// 0x08 KEYTYPE_
	uint8		m_Pad09[3];
	KeyCallback	m_Callback;		// 0x0c
	void		*m_pUser;		// 0x10
};

// One node's transform at one keyframe (0x1c bytes).
class NodeKeyFrame
{
public:
	LTVector	m_vTranslation;		// 0x00
	LTRotation	m_Quaternion;		// 0x0c
};

class ModelAnim;
class ModelNode;

// Per-node animation data (0x30 bytes). vtable 0x004c7850.
class AnimNode
{
public:
					AnimNode();								// 0x0044dec0
					AnimNode(ModelAnim *pAnim, AnimNode *pParent);	// 0x0044df50
	virtual			~AnimNode();							// 0x0044dfc0
	virtual ModelAnim*	GetAnim()					{return m_pAnim;}	// 0x0044df20
	virtual void	SetAnim(ModelAnim *pAnim)		{m_pAnim = pAnim;}	// 0x0044e1e0
	virtual AnimNode*	Create(ModelAnim *pAnim, AnimNode *pParent);	// 0x0044e1f0

	class Model*	GetModel();								// 0x0044e2d0
	void			Term();									// 0x0044e150
	LTBOOL			FillNodeList(uint32 &curNodeIndex);		// 0x0044e210
	LTBOOL			SetNode_R(ModelNode *pNode);			// 0x0044e280

	uint32			NumChildren()		{return m_nChildren;}
	AnimNode*		GetChild(uint32 i)	{return m_Children[i];}

	ModelNode		*m_pNode;		// 0x04
	uint8			m_Pad08[0xc - 0x8];	// 0x08 CMoArray<NodeKeyFrame> vtable
	NodeKeyFrame	*m_KeyFrames;	// 0x0c
	uint32			m_nKeyFrames;	// 0x10
	uint8			m_Pad14[0x18 - 0x14];
	AnimNode		*m_pParentNode;	// 0x18
	uint8			m_Pad1C[0x20 - 0x1c];	// 0x1c CMoArray<AnimNode*> vtable
	AnimNode		**m_Children;	// 0x20
	uint32			m_nChildren;	// 0x24
	uint8			m_Pad28[0x2c - 0x28];
	ModelAnim		*m_pAnim;		// 0x2c
};

// 0x5c bytes. vtable 0x004c78c0.
class ModelAnim
{
public:
					ModelAnim(class Model *pModel);		// 0x0044e2e0
	virtual			~ModelAnim();						// 0x0044e360
	virtual void	SetModel(class Model *pModel);		// 0x0044e410
	virtual ModelAnim*	Create(class Model *pModel);	// 0x0044e440

	AnimNode*		GetAnimNode(uint32 i)	{return m_AnimNodes[i];}
	uint32			GetAnimTime();			// 0x0044e520
	void			FreeRootNode();			// 0x0044e460 (name unknown)
	LTBOOL			PrecalcNodeLists(LTBOOL bRebuild);	// 0x0044e4b0

	AnimNode		**m_AnimNodes;		// 0x04
	uint8			m_Pad08[0x4];
	AnimKeyFrame	*m_KeyFrames;		// 0x0c
	uint32			m_nKeyFrames;		// 0x10
	uint8			m_Pad14[0x8];
	uint32			m_InterpolationMS;	// 0x1c
	class Model		*m_pModel;			// 0x20
	char			*m_pName;			// 0x24 (ILTServer::GetAnimName)
	AnimNode		m_RootNode;			// 0x28
	AnimNode		*m_pRootNode;		// 0x58 &m_RootNode unless replaced
};

// Transform from a parent model node into a child model's node space (0x1c bytes).
class NodeRelation
{
public:
	LTVector	m_Pos;		// 0x00
	LTRotation	m_Rot;		// 0x0c
};

class Model;

// Child model info (the model an animation comes from).
class ChildInfo
{
public:
	CMoArray<NodeRelation>	m_Relation;		// 0x00 one per parent model node
	uint32			m_Unknown14;			// 0x14
	uint32			m_AnimOffset;			// 0x18
	const char		*m_pFilename;			// 0x1c
	uint8			m_Pad20[0x24 - 0x20];
	Model			*m_pParentModel;		// 0x24
	Model			*m_pModel;				// 0x28
};

#define MAX_CHILD_MODELS	16

// One string in a ModelStringList.
struct ModelString
{
	uint32			m_AllocSize;		// 0x00
	ModelString		*m_pNext;			// 0x04
	char			m_String[1];		// 0x08
};

// 0x8 bytes.
class ModelStringList
{
public:
					ModelStringList(LAlloc *pAlloc);	// 0x0044dbe0
					~ModelStringList();					// 0x0044dc00

	void			Term();								// 0x0044dc10
	const char*		AddString(const char *pStr);		// 0x0044dc40
	LTBOOL			SetAlloc(LAlloc *pAlloc);			// 0x0044dd00

	inline LAlloc*	GetAlloc()	{return m_pAlloc;}

	ModelString		*m_StringList;		// 0x00
	LAlloc			*m_pAlloc;			// 0x04
};

// One level of detail of a piece (0x24 bytes); a ModelPiece starts with its own LOD 0.
class PieceLOD
{
public:
	uint8			m_Pad00[0x8];
	uint32			m_nVerts;			// 0x08
	uint8			m_Pad0C[0x18 - 0xc];
	uint32			m_nTris;			// 0x18
	uint8			m_Pad1C[0x24 - 0x1c];
};

class ModelPiece : public PieceLOD
{
public:
	// LOD 0 is the piece itself.
	PieceLOD*		GetLOD(uint32 iLOD)
	{
		if(iLOD == 0)
			return this;

		iLOD--;
		if(iLOD < m_nLODs)
			return &m_LODs[iLOD];
		else
			return LTNULL;
	}

	uint8			m_Pad24[0x28 - 0x24];
	uint32			m_VertOffset;		// 0x28
	uint8			m_Pad2C[0x38 - 0x2c];
	PieceLOD		*m_LODs;			// 0x38 LODs 1..n
	uint32			m_nLODs;			// 0x3c
	uint8			m_Pad40[0x48 - 0x40];
	char			m_Name[1];			// 0x48
};

// 0x20 bytes.
class AnimInfo
{
public:
	ModelAnim	*m_pAnim;			// 0x00
	ChildInfo	*m_pChildInfo;		// 0x04
	uint8		m_Pad08[0xc];
	LTVector	m_vTranslation;		// 0x14 added to the root node
};

class WeightSet
{
public:
	uint8		m_Pad00[0x14];
	float		*m_Weights;			// 0x14 one per node
};

// 0xb4 bytes. vtable 0x004c78fc.
class ModelNode
{
public:
				ModelNode();								// 0x0044e570
				ModelNode(class Model *pModel);				// 0x0044e5e0
	virtual		~ModelNode();								// 0x0044e610
	virtual void	MNSlot1(uint32 a, uint32 b, uint32 c) {}	// 0x0044e5b0 (name unknown)
	virtual ModelNode*	Create(class Model *pModel);		// 0x0044e770
	virtual void	SetModel(class Model *pModel);			// 0x0044e790

	uint32		GetNodeIndex()		{return m_NodeIndex;}
	uint32		CalcNumNodes();							// 0x0044e7e0
	void		Term();									// 0x0044e650
	void		Clear();								// 0x0044e6c0
	LTBOOL		FillNodeList(uint32 &curNodeIndex);		// 0x0044e810
	void		SetParent_R(uint32 iParent);			// 0x0044e870
	uint32		NumChildren()		{return m_nChildren;}
	ModelNode*	GetChild(uint32 i)	{return m_Children[i];}
	char*		GetName()			{return m_pName;}

	LTVector	m_vOffsetFromParent;	// 0x04
	uint16		m_NodeIndex;			// 0x10
	uint8		m_Flags;				// 0x12 MNODE_
	uint8		m_Pad13[0x9];
	ModelNode	**m_Children;			// 0x1c
	uint32		m_nChildren;			// 0x20
	uint8		m_Pad24[0x28 - 0x24];
	uint32		m_iParentNode;			// 0x28
	LTMatrix	m_mGlobalTransform;		// 0x2c
	LTMatrix	m_mInvGlobalTransform;	// 0x6c
	class Model	*m_pModel;				// 0xac
	char		*m_pName;				// 0xb0 (ILTClient::GetModelNodeName)
};

// 0x34 bytes.
class ModelSocket
{
public:
	ModelSocket();				// 0x0044ebb0

	LTVector	m_Pos;			// 0x00
	LTRotation	m_Rot;			// 0x0c
	char		m_Name[16];		// 0x1c
	uint32		m_iNode;		// 0x2c
	uint32		m_Unknown30;	// 0x30
};

class Model
{
public:
	Model(LAlloc *pAlloc, LAlloc *pDefAlloc);	// 0x0044ebe0
	virtual ~Model();

	char*			GetFilename();				// 0x0046c290
	void			Delete()	{ delete this; }

	ModelNode*		FindNode(const char *pName, uint32 *index=LTNULL);			// 0x0044f640
	ModelPiece*		FindPiece(const char *pName, uint32 *index=LTNULL);			// 0x0044f590
	WeightSet*		FindWeightSet(const char *pName, uint32 *index=LTNULL);		// 0x0044f5e0
	ModelSocket*	FindSocket(const char *pName, uint32 *index=LTNULL);		// 0x004500e0
	ModelAnim*		FindAnim(const char *pName, uint32 *index=LTNULL, AnimInfo **ppInfo=LTNULL);	// 0x0044f6a0
	AnimInfo*		FindAnimInfo(const char *pAnimName, Model *pOwner, uint32 *index=LTNULL);	// 0x0044f720
	const char*		AddString(const char *pStr);	// 0x0044f630
	void			SetNodeParentOffsets();			// 0x0044f790
	uint32			CalcNumTris(uint32 iLOD);		// 0x0044f920 (name unknown)
	uint32			CalcNumVerts();					// 0x0044f970 (name unknown)
	uint32			CalcNumChildModelAnims(LTBOOL bIncludeSelf);	// 0x0044f990
	uint32			CalcNumParentAnims();			// 0x0044f9e0
	LTBOOL			SetFilename(const char *pFilename);	// 0x0044ff50
	void			FreeFilename();					// 0x0044ffa0
	LTBOOL			VerifyChildModelTree(Model *pChild, ModelNode* &pErrNode);	// 0x0044ffd0
	LTBOOL			InitChildInfo(uint32 index, ChildInfo *pChildModel, Model *pModel, const char *pFilename);	// 0x00450080

	uint32			NumPieces()				{return m_nPieces;}
	ModelPiece*		GetPiece(uint32 i)		{return m_Pieces[i];}
	uint32			NumChildModels()		{return m_nChildModels;}
	ChildInfo*		GetChildModel(uint32 i)	{return m_ChildModels[i];}
	ChildInfo*		GetSelfChildModel()		{return m_ChildModels[0];}

	uint32			NumNodes()				{return m_nNodes;}
	uint32			NumSockets()			{return m_nSockets;}
	ModelSocket*	GetSocket(uint32 i)		{return m_Sockets[i];}

	uint32		NumAnims()				{return m_nAnims;}
	ModelAnim*	GetAnim(uint32 i)		{return m_Anims[i].m_pAnim;}
	AnimInfo*	GetAnimInfo(uint32 i)	{return &m_Anims[i];}

	uint32		NumWeightSets()			{return m_WeightSets.GetSize();}
	WeightSet*	GetWeightSet(uint32 i)	{return (i >= m_WeightSets.GetSize()) ? LTNULL : m_WeightSets[i];}

	ModelNode*	GetNode(uint32 i)		{return m_FlatNodeList[i];}
	ModelNode*	GetRootNode()			{return m_pRootNode;}

	char		*m_pFilename;		// 0x04
	uint8		m_Pad008[0x20 - 0x8];
	uint32		m_Flags;			// 0x20 server: MODELFLAG_ (bit 0 = cached)
	uint8		m_Pad024[0x28 - 0x24];
	ModelNode	**m_FlatNodeList;	// 0x28
	uint32		m_nFlatNodes;		// 0x2c
	uint8		m_Pad030[0x38 - 0x30];
	ModelPiece	**m_Pieces;			// 0x38
	uint32		m_nPieces;			// 0x3c
	uint8		m_Pad040[0x44 - 0x40];
	CMoArray<WeightSet*>	m_WeightSets;	// 0x44
	uint8		m_Pad058[0x64 - 0x58];
	uint32		m_nTotalVerts;		// 0x64
	uint32		m_nTotalTris;		// 0x68
	uint32		m_nNodeDWords;		// 0x6c (m_nNodes+3)/4
	uint8		m_Pad070[0x74 - 0x70];
	LTMatrix	*m_Transforms;		// 0x74
	uint32		m_nNodes;			// 0x78
	uint8		m_Pad07C[0x80 - 0x7c];
	char		*m_CommandString;	// 0x80 ILTServer::GetModelCommandString
	ModelStringList	m_StringList;	// 0x84
	uint8		m_Pad08C[0xa8 - 0x8c];
	float		m_VisRadius;		// 0xa8
	uint8		m_Pad0AC[0x158 - 0xac];
	ModelSocket	**m_Sockets;		// 0x158
	uint32		m_nSockets;			// 0x15c
	uint8		m_Pad160[0x168 - 0x160];
	AnimInfo	*m_Anims;			// 0x168
	uint32		m_nAnims;			// 0x16c
	uint8		m_Pad170[0x174 - 0x170];
	LAlloc		*m_pAlloc;			// 0x174
	uint8		m_Pad178[0x194 - 0x178];
	ChildInfo	*m_ChildModels[MAX_CHILD_MODELS];	// 0x194
	uint32		m_nChildModels;		// 0x1d4
	uint8		m_Pad1D8[0x20c - 0x1d8];
	uint32		m_RefCount;			// 0x20c server references (server_extradata)
	uint8		m_Pad210[0x2c4 - 0x210];
	ModelNode	*m_pRootNode;		// 0x2c4
};

#endif
