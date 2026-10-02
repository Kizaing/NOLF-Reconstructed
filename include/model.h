// Talon model structures (ABC v12), layouts recovered from lithtech.exe.
// Jupiter runtime/model/src/model.h is the guide, but Talon's layout differs.
// Only the members used by matched code are named so far.
#ifndef __MODEL_H__
#define __MODEL_H__

#include "ltbasedefs.h"
#include "ltdynarray.h"

class LTAnimTracker;
class ILTStream;
class ModelAllocations;
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

	// model_load.cpp
	LTBOOL			Load(ILTStream &file);					// 0x00455370

	// Talon's CMoArray members (the raw pointer/count members below alias them).
	CMoArray<NodeKeyFrame, NoCache>&	KeyFrameArray()	{return *(CMoArray<NodeKeyFrame, NoCache>*)m_Pad08;}
	CMoArray<AnimNode*, NoCache>&		ChildArray()	{return *(CMoArray<AnimNode*, NoCache>*)m_Pad1C;}

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
	LTBOOL			SetupNodeLists(LTBOOL bRebuild);	// 0x0044e4a0 (name unknown; calls PrecalcNodeLists)
	LTBOOL			PrecalcNodeLists(LTBOOL bRebuild);	// 0x0044e4b0
	void			Term();					// 0x0044e3b0

	// model_load.cpp
	LTBOOL			Load(ILTStream &file);	// 0x00455530

	CMoArray<AnimKeyFrame, NoCache>&	KeyFrameArray()	{return *(CMoArray<AnimKeyFrame, NoCache>*)m_Pad08;}

	AnimNode		**m_AnimNodes;		// 0x04
	uint8			m_Pad08[0x4];		// 0x08 CMoArray<AnimKeyFrame, NoCache> vtable
	AnimKeyFrame	*m_KeyFrames;		// 0x0c
	uint32			m_nKeyFrames;		// 0x10
	uint8			m_Pad14[0x18 - 0x14];
	int32			m_Unknown18;		// 0x18 file version > 10, default -1
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

class ModelLoadRequest;

// pRequest is a load request with everything setup except m_pFile.
// ppModel is what you should fill in with the model you loaded.
// Returns LT_OK if the model loaded ok, LT_NOCHANGE if it didn't load the model but it's
// not a fatal error.  Any other return signals an error and means to stop loading.
typedef LTRESULT (*LoadChildFn)(ModelLoadRequest *pRequest, Model **ppModel);

// Default implementation.. just returns LT_NOCHANGE.
LTRESULT DefaultLoadChildFn(ModelLoadRequest *pRequest, Model **ppModel);	// 0x0044db70

// Talon's model load request (layout partly known).
class ModelLoadRequest
{
public:
	ILTStream		*m_pFile;				// 0x00
	LoadChildFn		m_LoadChildFn;			// 0x04
	void			*m_pLoadFnUserData;		// 0x08
	void			*m_pExtraChildModels;	// 0x0c ExtraChildMap* (ILTServer::LinkModelToExtraChildModel)
	const char		*m_pFilename;			// 0x10
	LTBOOL			m_bLoadChildModels;		// 0x14
	LTBOOL			m_Unknown18;			// 0x18
	LTBOOL			m_bTreesValid;			// 0x1c cleared when a child model's stamp or anim count changed
	LTBOOL			m_bAllChildrenLoaded;	// 0x20
};

// Child model info (the model an animation comes from).
class ChildInfo
{
public:
	// model_load.cpp
	LTBOOL			Load(ILTStream &file);	// 0x004558c0

	CMoArray<NodeRelation>	m_Relation;		// 0x00 one per parent model node
	uint32			m_Unknown14;			// 0x14
	uint32			m_AnimOffset;			// 0x18
	const char		*m_pFilename;			// 0x1c
	uint32			m_ModelStamp;			// 0x20 compared with the child model's stamp
	Model			*m_pParentModel;		// 0x24
	Model			*m_pModel;				// 0x28
	LTBOOL			m_bTreesValid;			// 0x2c
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

// One bone influence on a vertex (0x14 bytes).
class NewVertexWeight
{
public:
				NewVertexWeight();		// 0x0044dda0

	float		m_Vec[4];			// 0x00 offset in node space (premultiplied by the weight), weight
	uint32		m_iNode;			// 0x10
};

// 0x20 bytes.
class ModelVert
{
public:
				ModelVert();			// 0x0044ddc0

	NewVertexWeight	*m_Weights;		// 0x00 points into Model::m_VertexWeights
	uint16		m_nWeights;			// 0x04
	uint16		m_iReplacement;		// 0x06 vertex in the next coarser LOD
	LTVector	m_Vec;				// 0x08
	LTVector	m_Normal;			// 0x14
};

struct UVPair
{
	float		tu, tv;
};

// 0x20 bytes.
class ModelTri
{
public:
	void		operator=(const ModelTri &other)
	{
		uint32 i;
		for(i=0; i < 3; i++)
		{
			m_Indices[i] = other.m_Indices[i];
			m_UVs[i] = other.m_UVs[i];
		}
	}

	uint16		m_Indices[3];		// 0x00
	UVPair		m_UVs[3];			// 0x08
};

// A model-wide LOD switch distance (4 bytes).
class LODDistance
{
public:
				LODDistance()	{m_Dist = 0.0f;}

	float		m_Dist;
};

// One level of detail of a piece (0x24 bytes); a ModelPiece starts with its own LOD 0.
class PieceLOD
{
public:
				PieceLOD();							// 0x0044e910
				PieceLOD(class Model *pModel);		// 0x0044e8b0
				~PieceLOD();						// 0x0044e980
	void		Init(class Model *pModel);			// 0x0044e970 (name unknown)

	// model_load.cpp
	LTBOOL		Load(ILTStream &file, uint32 &curWeight);	// 0x004559c0

	CMoArray<ModelVert, NoCache>	m_Verts;	// 0x00
	CMoArray<ModelTri, NoCache>		m_Tris;		// 0x10
	class Model		*m_pModel;					// 0x20
};

class ModelPiece : public PieceLOD
{
public:
					ModelPiece(class Model *pModel);	// 0x0044ea20
					~ModelPiece();						// 0x0044ea60

	// model_load.cpp
	LTBOOL			Load(ILTStream &file, uint32 &curWeight);	// 0x00455cb0

	// LOD 0 is the piece itself.
	PieceLOD*		GetLOD(uint32 iLOD)
	{
		if(iLOD == 0)
			return this;

		iLOD--;
		if(iLOD < m_LODs.GetSize())
			return &m_LODs[iLOD];
		else
			return LTNULL;
	}

	uint32			m_TextureIndex;		// 0x24
	uint32			m_VertOffset;		// 0x28
	float			m_SpecularPower;	// 0x2c
	float			m_SpecularScale;	// 0x30
	CMoArray<PieceLOD, NoCache>	m_LODs;	// 0x34 LODs 1..n
	float			m_LODWeight;		// 0x44 file version > 9
	char			m_Name[32];			// 0x48
	class Model		*m_pPieceModel;		// 0x68
};

// 0x20 bytes.
class AnimInfo
{
public:
				AnimInfo();			// 0x0044e540

	// model_load.cpp
	LTBOOL		Load(ILTStream &file);	// 0x00455870

	ModelAnim	*m_pAnim;			// 0x00
	ChildInfo	*m_pChildInfo;		// 0x04
	LTVector	m_vDims;			// 0x08 user dimensions (ILTCommon::GetModelAnimUserDims)
	LTVector	m_vTranslation;		// 0x14 added to the root node
};

// 0x28 bytes.
class WeightSet
{
public:
					WeightSet(class Model *pModel);		// 0x0044eb20
					~WeightSet();						// 0x0044eb50

	// model_load.cpp
	LTBOOL			Load(ILTStream &file);				// 0x00455e40

	char			m_Name[16];			// 0x00
	CMoArray<float>	m_Weights;			// 0x10 one per node
	class Model		*m_pModel;			// 0x24
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

	inline void	SetGlobalTransform(LTMatrix mat)
	{
		m_mGlobalTransform = mat;
		m_mInvGlobalTransform = ~mat;
	}

	// model_load.cpp
	LTBOOL		Load(ILTStream &file);					// 0x00455670

	CMoArray<ModelNode*, NoCache>&	ChildArray()	{return *(CMoArray<ModelNode*, NoCache>*)m_Pad18;}

	LTVector	m_vOffsetFromParent;	// 0x04
	uint16		m_NodeIndex;			// 0x10
	uint8		m_Flags;				// 0x12 MNODE_
	uint8		m_Pad13[0x18 - 0x13];
	uint8		m_Pad18[0x1c - 0x18];	// 0x18 CMoArray<ModelNode*, NoCache> vtable
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
	LAlloc*		GetAlloc()				{return m_pAlloc;}

	// LOD 0 is m_LODDist0, then the LOD switch distances.
	float*		GetLODDist(uint32 iLOD)
	{
		if(iLOD == 0)
			return &m_LODDist0;

		iLOD--;
		if(iLOD < m_nLODDists)
			return &m_LODDists[iLOD].m_Dist;
		else
			return LTNULL;
	}

	// model_load.cpp
	void			Term(LTBOOL bX);								// 0x0044f050 (argument unknown)
	LTBOOL			PostLoadNodes(LTBOOL bX);						// 0x0044f360 (name unknown)
	void			ParseCommandString();							// 0x0044fa10
	LTRESULT		Load(ModelLoadRequest *pRequest);				// 0x00456390
	LTBOOL			LoadString(ILTStream &file, char* &pStr);		// 0x00455f10
	LTBOOL			LoadAnimBindings(ModelLoadRequest *pRequest, ILTStream &file, LTBOOL bAllowUpdates);	// 0x00455f70
	LTRESULT		InitAllocations(ILTStream &file, LAlloc *pDelegate);	// 0x00457100 (name unknown)
	LTBOOL			LoadSockets(ILTStream &file);					// 0x00456100
	LTBOOL			LoadWeightSets(ILTStream &file);				// 0x00456230
	LTBOOL			LoadHeader(ILTStream &file, ModelAllocations &allocs);	// 0x00457090 (name unknown)

	// Talon's CMoArray members (the raw pointer/count members below alias them).
	CMoArray<ModelNode*, NoCache>&		FlatNodeArray()	{return *(CMoArray<ModelNode*, NoCache>*)m_Pad024;}
	CMoArray<ModelPiece*, NoCache>&		PieceArray()	{return *(CMoArray<ModelPiece*, NoCache>*)m_Pad034;}
	CMoArray<LTMatrix, NoCache>&		TransformArray()	{return *(CMoArray<LTMatrix, NoCache>*)m_Pad070;}
	CMoArray<LODDistance>&				LODDistArray()	{return *(CMoArray<LODDistance>*)m_Pad08C;}
	CMoArray<ModelSocket*, NoCache>&	SocketArray()	{return *(CMoArray<ModelSocket*, NoCache>*)m_Pad154;}
	CMoArray<AnimInfo, NoCache>&		AnimArray()		{return *(CMoArray<AnimInfo, NoCache>*)m_Pad164;}

	char		*m_pFilename;		// 0x04
	LTLink		m_Link;				// 0x08 client: in CClientMgr::m_TextureUsers (setupobject)
	uint8		m_Nexus[0x8];		// 0x14 Nexus (nexus.h): the client leeches onto server models
	uint32		m_FileID;			// 0x1c server file ID
	uint32		m_Flags;			// 0x20 server: MODELFLAG_ (bit 0 = cached)
	uint8		m_Pad024[0x28 - 0x24];	// 0x24 CMoArray<ModelNode*, NoCache> vtable
	ModelNode	**m_FlatNodeList;	// 0x28
	uint32		m_nFlatNodes;		// 0x2c
	uint8		m_Pad030[0x34 - 0x30];
	uint8		m_Pad034[0x38 - 0x34];	// 0x34 CMoArray<ModelPiece*, NoCache> vtable
	ModelPiece	**m_Pieces;			// 0x38
	uint32		m_nPieces;			// 0x3c
	uint8		m_Pad040[0x44 - 0x40];
	CMoArray<WeightSet*, NoCache>	m_WeightSets;	// 0x44
	CMoArray<NewVertexWeight, NoCache>	m_VertexWeights;	// 0x54 shared by all the pieces' vertices
	uint32		m_nTotalVerts;		// 0x64
	uint32		m_nTotalTris;		// 0x68
	uint32		m_nNodeDWords;		// 0x6c (m_nNodes+3)/4
	uint8		m_Pad070[0x74 - 0x70];	// 0x70 CMoArray<LTMatrix, NoCache> vtable
	LTMatrix	*m_Transforms;		// 0x74
	uint32		m_nNodes;			// 0x78
	uint8		m_Pad07C[0x80 - 0x7c];
	char		*m_CommandString;	// 0x80 ILTServer::GetModelCommandString
	ModelStringList	m_StringList;	// 0x84
	uint8		m_Pad08C[0x90 - 0x8c];	// 0x8c CMoArray<LODDistance> vtable
	LODDistance	*m_LODDists;		// 0x90
	uint32		m_nLODDists;		// 0x94
	uint8		m_Pad098[0xa0 - 0x98];
	float		m_LODDist0;			// 0xa0 distance of LOD 0
	float		m_GlobalRadius;		// 0xa4
	float		m_VisRadius;		// 0xa8
	uint8		m_Pad0AC[0xc0 - 0xac];
	struct SharedTexture	*m_pFadeSpriteTex;	// 0xc0 client: the "FadeSpriteTex" command string texture (setupobject)
	uint8		m_Pad0C4[0x154 - 0xc4];
	uint8		m_Pad154[0x158 - 0x154];	// 0x154 CMoArray<ModelSocket*, NoCache> vtable
	ModelSocket	**m_Sockets;		// 0x158
	uint32		m_nSockets;			// 0x15c
	uint8		m_Pad160[0x164 - 0x160];
	uint8		m_Pad164[0x168 - 0x164];	// 0x164 CMoArray<AnimInfo, NoCache> vtable
	AnimInfo	*m_Anims;			// 0x168
	uint32		m_nAnims;			// 0x16c
	uint8		m_Pad170[0x174 - 0x170];
	LAlloc		*m_pAlloc;			// 0x174
	LAlloc		*m_pDefAlloc;		// 0x178
	LAllocSimpleBlock	m_BlockAlloc;	// 0x17c
	uint32		m_Unknown190;		// 0x190 set when extra child models were linked in
	ChildInfo	*m_ChildModels[MAX_CHILD_MODELS];	// 0x194
	uint32		m_nChildModels;		// 0x1d4
	ChildInfo	m_SelfChildModel;	// 0x1d8
	uint32		m_FileVersion;		// 0x208
	uint32		m_RefCount;			// 0x20c server references (server_extradata)
	uint8		m_Pad210[0x2c4 - 0x210];
	ModelNode	*m_pRootNode;		// 0x2c4
};

#endif
